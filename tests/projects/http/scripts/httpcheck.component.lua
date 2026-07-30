-- The engine's HTTP client, driven from game script inside a REAL app on a
-- REAL phone (or emulator/simulator) against a loopback server the host runs.
-- The desktop suite proves the same contract in process; this run proves it on
-- the platform transports - NSURLSession on iOS, libcurl+OpenSSL on Android -
-- and through each platform's own network policy gate.
--
-- Every leg reports one line:
--   [http-device] PASS <leg>
--   [http-device] FAIL <leg>: <reason>
-- and the run ends with exactly one
--   [http-device] RESULT pass      |  [http-device] RESULT fail <n>
--
-- Each line goes out twice: printed (visible in a simulator console) and
-- POSTed back to the host, which is the channel the driver reads - a device's
-- stdout is not a dependable transport, and the POST also has to work for the
-- run to mean anything.

-- the base URL is authored ONCE in project.orkproj as the manifest setting
-- cvar.httpdevice.baseUrl; the host driver parses that same file
local BASE_URL_CVAR = 'httpdevice.baseUrl'

local TAG = '[http-device] '
local OK_BODY = 'orkige-http-device-ok'
local OK_HEADER = 'x-orkige-note'
local OK_HEADER_VALUE = 'device-ok'
local ECHO_BODY = '{"device":"orkige","score":42}'
-- a few hundred kilobytes, written by the host in paced chunks so progress
-- genuinely steps instead of arriving in one read
local PROGRESS_BYTES = 786432
local CAP_BYTES = 262144
local CAP_LIMIT = 4096
local BLOB_BYTES = 32768
local CANCEL_BYTES = 8388608
-- the whole run is bounded: a leg that never answers must still produce a
-- verdict rather than hang the host driver until its own deadline
local WATCHDOG_SECONDS = 60

local LEGS = {
	'ok-200', 'response-header', 'status-404', 'post-echo', 'progress',
	'max-bytes', 'timeout', 'cancel', 'download', 'https-no-fallback',
	'plain-http-refused', 'plain-http-allowed',
}

local startLegs
local checkFinished

--! one line out: printed and posted back to the host
local function emit(self, line)
	print(line)
	if self.base ~= '' then
		http.request{
			url = self.base .. '/report',
			method = 'POST',
			body = line,
			contentType = 'text/plain',
			allowInsecureHttp = true,
			timeout = 10,
			onComplete = function(res) end,
		}
	end
end

local function finish(self)
	if self.finished then
		return
	end
	self.finished = true
	if self.failures == 0 then
		emit(self, TAG .. 'RESULT pass')
	else
		emit(self, TAG .. 'RESULT fail ' .. self.failures)
	end
end

--! record one leg's verdict; a leg reports exactly once
local function settle(self, leg, passed, reason)
	if self.results[leg] then
		return
	end
	self.results[leg] = true
	self.answered = self.answered + 1
	if passed then
		emit(self, TAG .. 'PASS ' .. leg)
	else
		self.failures = self.failures + 1
		emit(self, TAG .. 'FAIL ' .. leg .. ': ' .. reason)
	end
	checkFinished(self)
end

checkFinished = function(self)
	if self.answered >= #LEGS then
		finish(self)
	end
end

--! the answer table in one readable line, for a FAIL reason
local function describe(res)
	return 'ok=' .. tostring(res.ok)
		.. ' status=' .. tostring(res.status)
		.. " error='" .. tostring(res.error) .. "'"
		.. " reason='" .. tostring(res.reason) .. "'"
end

local function failAll(self, reason)
	for index = 1, #LEGS do
		settle(self, LEGS[index], false, reason)
	end
end

function init(self)
	self.results = {}
	self.answered = 0
	self.failures = 0
	self.elapsed = 0
	self.finished = false
	self.cancelId = 0
	self.cancelSent = false
	self.cancelMoving = false
	self.cancelStartedAt = 0
	self.progressSteps = 0
	self.progressTotal = 0
	self.progressLast = 0

	cvar.registerString(BASE_URL_CVAR, '')
	self.base = cvar.get(BASE_URL_CVAR)
	if self.base == '' then
		-- nothing can be reported anywhere, so say it on the one channel left
		print(TAG .. 'FAIL setup: the manifest names no cvar.' .. BASE_URL_CVAR)
		print(TAG .. 'RESULT fail 1')
		self.finished = true
		return
	end
	if not http.isAvailable() then
		failAll(self, 'this build has no HTTP client')
		return
	end

	-- the bootstrap fetch: only the HOST can name a path this app may write
	-- to (an iOS Simulator's data container is a per-install UUID path), so
	-- the download leg's save path is asked for rather than guessed
	http.request{
		url = self.base .. '/config',
		allowInsecureHttp = true,
		timeout = 20,
		onComplete = function(res)
			if not res.ok then
				failAll(self, 'the host /config fetch failed - the loopback '
					.. 'server is unreachable from this app (' .. describe(res)
					.. ')')
				return
			end
			startLegs(self, res.body:match('savePath=([^\r\n]+)') or '')
		end,
	}
end

startLegs = function(self, savePath)
	local base = self.base

	-- a 200 with a known body, and one of its response headers read back
	http.request{
		url = base .. '/ok',
		allowInsecureHttp = true,
		timeout = 20,
		onComplete = function(res)
			settle(self, 'ok-200',
				res.ok and res.status == 200 and res.body == OK_BODY,
				'expected a 200 carrying "' .. OK_BODY .. '", got '
					.. describe(res) .. ' body="' .. tostring(res.body) .. '"')
			local seen = (res.headers and res.headers[OK_HEADER]) or ''
			settle(self, 'response-header', seen == OK_HEADER_VALUE,
				"expected the response header " .. OK_HEADER .. "='"
					.. OK_HEADER_VALUE .. "', got '" .. tostring(seen) .. "'")
		end,
	}

	-- an HTTP status is an ANSWER: a 404 completes with no failure token
	http.request{
		url = base .. '/missing',
		allowInsecureHttp = true,
		timeout = 20,
		onComplete = function(res)
			settle(self, 'status-404',
				res.status == 404 and res.error == '' and not res.ok,
				'a 404 must arrive as a completed exchange carrying no '
					.. 'failure token, got ' .. describe(res))
		end,
	}

	-- a POST body the server echoes back
	http.request{
		url = base .. '/echo',
		method = 'POST',
		body = ECHO_BODY,
		contentType = 'application/json',
		allowInsecureHttp = true,
		timeout = 20,
		onComplete = function(res)
			settle(self, 'post-echo', res.ok and res.body == ECHO_BODY,
				'the server must echo the posted body, got ' .. describe(res)
					.. ' body="' .. tostring(res.body) .. '"')
		end,
	}

	-- progress over a multi-hundred-kilobyte body: received climbs, and the
	-- total is the size the server announced
	http.request{
		url = base .. '/big?bytes=' .. PROGRESS_BYTES,
		allowInsecureHttp = true,
		timeout = 60,
		maxBytes = 4194304,
		onProgress = function(received, total)
			self.progressSteps = self.progressSteps + 1
			self.progressLast = received
			self.progressTotal = total
		end,
		onComplete = function(res)
			settle(self, 'progress',
				res.ok and self.progressSteps >= 2
					and self.progressTotal == PROGRESS_BYTES
					and self.progressLast == PROGRESS_BYTES
					and res.bytes == PROGRESS_BYTES,
				'expected progress to climb to the announced ' .. PROGRESS_BYTES
					.. ' bytes over several steps, saw ' .. self.progressSteps
					.. ' steps ending at ' .. self.progressLast .. '/'
					.. self.progressTotal .. ' (' .. describe(res) .. ')')
		end,
	}

	-- the size cap refuses an oversized response
	http.request{
		url = base .. '/big?bytes=' .. CAP_BYTES,
		allowInsecureHttp = true,
		timeout = 30,
		maxBytes = CAP_LIMIT,
		onComplete = function(res)
			settle(self, 'max-bytes', res.error == 'too-large' and not res.ok,
				'a response over the ' .. CAP_LIMIT .. '-byte cap must be '
					.. 'refused as too-large, got ' .. describe(res))
		end,
	}

	-- a server that answers only after the deadline
	http.request{
		url = base .. '/slow',
		allowInsecureHttp = true,
		timeout = 2,
		onComplete = function(res)
			settle(self, 'timeout', res.error == 'timeout',
				'a server answering past the timeout must fail as timeout, '
					.. 'got ' .. describe(res))
		end,
	}

	-- cancelling mid-transfer still delivers exactly one answer, as cancelled.
	-- The cancel itself is issued from update(), never from inside a callback:
	-- callbacks run while the client drains its completed queue.
	self.cancelStartedAt = self.elapsed
	self.cancelId = http.request{
		url = base .. '/big?bytes=' .. CANCEL_BYTES,
		allowInsecureHttp = true,
		timeout = 60,
		maxBytes = 16777216,
		onProgress = function(received, total)
			self.cancelMoving = true
		end,
		onComplete = function(res)
			settle(self, 'cancel', res.error == 'cancelled',
				'a cancelled transfer must answer once, as cancelled, got '
					.. describe(res))
		end,
	}

	-- save straight to a file. The script sandbox has no file access, so the
	-- reported size, path and empty in-memory body are what CAN be asserted
	-- here; the host driver reads the saved file back off the device and
	-- compares it byte for byte against what it served.
	if savePath == '' then
		settle(self, 'download', false,
			'the host named no save path in its /config answer')
	else
		http.request{
			url = base .. '/blob',
			savePath = savePath,
			allowInsecureHttp = true,
			timeout = 30,
			maxBytes = 1048576,
			onComplete = function(res)
				settle(self, 'download',
					res.ok and res.bytes == BLOB_BYTES
						and res.path == savePath and res.body == '',
					'expected ' .. BLOB_BYTES .. ' bytes saved to ' .. savePath
						.. ' with nothing kept in memory, got ' .. describe(res)
						.. ' bytes=' .. tostring(res.bytes) .. " path='"
						.. tostring(res.path) .. "'")
			end,
		}
	end

	-- https against the plain-http host: the client must speak TLS and fail,
	-- never quietly fall back to the answer the server was ready to give
	http.request{
		url = 'https' .. base:sub(5) .. '/ok',
		timeout = 8,
		onComplete = function(res)
			local honest = res.error == 'tls-failed'
				or res.error == 'connect-failed'
				or res.error == 'timeout'
			settle(self, 'https-no-fallback',
				not res.ok and res.status == 0 and res.body == '' and honest,
				'an https request to a plain-http server must fail rather '
					.. 'than fall back, got ' .. describe(res))
		end,
	}

	-- a plain http:// URL is refused unless the caller opts in...
	http.request{
		url = base .. '/ok',
		timeout = 20,
		onComplete = function(res)
			settle(self, 'plain-http-refused',
				res.error == 'insecure-scheme',
				'a plain http:// URL without allowInsecureHttp must be '
					.. 'refused, got ' .. describe(res))
		end,
	}

	-- ...and reaches the server when it does. This is also the PLATFORM's own
	-- cleartext gate on both mobile targets: iOS app transport security and
	-- Android's network security config each refuse a plain-http load of their
	-- own accord, and a loopback address is the case both still permit. A
	-- packaged app that never declared that exemption fails this leg.
	http.request{
		url = base .. '/ok',
		allowInsecureHttp = true,
		timeout = 20,
		onComplete = function(res)
			settle(self, 'plain-http-allowed', res.ok and res.status == 200,
				'a loopback plain-http URL with allowInsecureHttp must reach '
					.. 'the server, got ' .. describe(res))
		end,
	}
end

function update(self, dt)
	self.elapsed = self.elapsed + dt
	if self.cancelId > 0 and not self.cancelSent then
		-- pull the plug once the transfer is demonstrably running (or after a
		-- grace period, so a stalled one still gets cancelled)
		if self.cancelMoving or self.elapsed - self.cancelStartedAt > 2 then
			self.cancelSent = true
			http.cancel(self.cancelId)
		end
	end
	if not self.finished and self.elapsed > WATCHDOG_SECONDS then
		failAll(self, 'no answer within ' .. WATCHDOG_SECONDS .. 's')
		finish(self)
	end
end
