# HTTP client

The engine's way to talk to a web server: a leaderboard, remote config, a
backend API, an asset or update download. One facade — `core_http/HttpClient` —
with a per-platform transport behind it, and a Lua `http` table on top.

Everything about it is **asynchronous by construction**. There is no blocking
call anywhere in the surface: a request is submitted, it progresses off the main
thread, and its progress and completion are delivered at a frame boundary the
runtime already owns. A game cannot stall a frame on the network even by
mistake, and a callback never runs in the middle of a world update.

## The shape

```cpp
HttpClientRequest request;
request.url = "https://api.example.com/scores";
request.method = "POST";
request.body = "{\"score\":42}";
request.contentType = "application/json";
request.headers.push_back(std::make_pair("Authorization", "Bearer " + token));

HttpClient::getSingleton().submit(request,
    [](HttpClientResponse const & response)
    {
        if (response.ok()) { use(response.body); }
        else               { report(response.reason); }
    });
```

`submit` returns an `HttpRequestId` immediately — it resolves no name and opens
no socket on the calling thread. Everything else is optional and bounded:

| Field | Default | Meaning |
| --- | --- | --- |
| `method` | `"GET"` | any method token — GET/POST/PUT/PATCH/DELETE/HEAD |
| `headers` | none | request headers, in submission order |
| `body` / `contentType` | empty | the entity body and its `Content-Type` |
| `savePath` | empty | non-empty = **save to file** instead of memory |
| `timeoutMs` | 30000 | whole-request timeout |
| `maxResponseBytes` | 16 MiB | hard cap; a bigger response is refused |
| `allowInsecureHttp` | false | the explicit opt-in for a plain `http://` URL |
| `followRedirects` / `maxRedirects` | true / 5 | redirect policy |

The answer is an `HttpClientResponse`: `completed`, `status`, `headers` (names
lower-cased), `body`, `savedPath`, `bytes`, `finalUrl`, `failure` and `reason`.

### An HTTP status is not a failure

`completed` means *the exchange happened*; `status` carries the server's
verdict. A 404 or a 500 is a completed exchange with `failure == HF_NONE` — the
server answered, and the answer was "no". Only a transport-level problem or a
policy refusal sets `failure`, and every one of those also fills `reason` with a
line fit to show a player. `ok()` is the shorthand for "completed with a 2xx".

The failure vocabulary (`httpFailureName` gives the stable token the Lua surface
and the logs report): `unavailable`, `bad-url`, `unsupported-scheme`,
`insecure-scheme`, `credentials-in-url`, `bad-header`, `bad-method`,
`bad-save-path`, `connect-failed`, `tls-failed`, `timeout`, `too-large`,
`redirect-refused`, `write-failed`, `cancelled`, `transport`.

### Every request is answered exactly once

A submitted request always delivers one completion — a success, a status, a
refusal or a cancellation — on the main thread, from `update()`. A request the
security policy refuses still gets a handle and still reports through the
completion callback, so a caller has ONE error path instead of two.

`cancel(id)` aborts a transfer and still delivers that one completion, with
`cancelled`. The two exceptions are deliberate: `cancelOwner(owner)` and
`cancelAll()` drop their callbacks WITHOUT calling them, because their whole
point is that the owner is going away and must not be called into.

### Where completions are delivered

`HttpClient::update()` is the only place a callback runs. The player calls it
inside the canonical tick order's async-answers slot (`tools/player/main.cpp`):

```
input -> [async answers] -> scripts/world -> tweens -> physics -> load pump
```

It sits with input, before the scripts that read it: an answer that landed
between frames is applied before the code that looks at it runs, exactly like a
key the OS delivered. It is INSIDE the fence, so a paused runtime holds its
answers and delivers them on resume — a callback must not mutate a frozen
world. Any other host (a tool, the editor) owns the same one call per frame; a
host that never calls `update()` never sees a callback.

### Progress

`onProgress(received, total)` is called at most once per request per drain
(steps coalesce, so a fast transfer cannot flood a frame with stale numbers).
`total` is the server's `Content-Length` when it sends one and 0 while unknown —
which is exactly what a progress bar needs, including the "indeterminate" case.
The size cap is checked against the ANNOUNCED size too, so an oversized download
is refused before its first body byte rather than after.

### Save to file

A request with a `savePath` streams the body to disk and keeps NOTHING in
memory (`body` comes back empty; `savedPath` names the file). The bytes land in
a sibling temp file that is renamed over the target only on success, so a
failed, capped or cancelled download never leaves a truncated file where a good
one was — the write plumbing is `core_filesystem/FileWriter`, the write side of
the [filesystem funnel](filesystem.md).

On the web the whole body is buffered first and written once: the browser's
fetch API hands a wasm module no incremental file sink. The cap still applies.

## Security defaults

The defaults are the safe ones, and opting out is explicit, per-request and
visible in the calling code.

- **Certificates are verified, always**, against the trust store the PLATFORM
  maintains — never a CA bundle shipped with the engine (a bundle rots, and a
  rotted bundle is a silent outage or, worse, a silent downgrade). This is the
  main reason the transports differ per platform (below).
- **https by default.** A plain `http://` URL is refused unless the caller sets
  `allowInsecureHttp` — a developer pointing a game at a service on their own
  machine is the real use case, and it stays a visible choice at the call site.
- **A redirect can never downgrade.** An https request that is redirected to
  `http://` is refused, whatever the caller opted into. A request that started
  as plain http may be redirected either way; it was never secure.
- **Only http and https exist.** No `ftp://`, no `file://`, no other protocol,
  from a URL or from a redirect. On the libcurl transport the library itself is
  built HTTP-only and the protocol set is pinned per request as well.
- **No credentials in URLs.** `https://user:pass@host/` is refused by name, with
  the reason pointing at the `Authorization` header — URL credentials leak into
  logs, proxies and crash reports.
- **No header injection.** A header name must be an HTTP token and no header may
  carry CR, LF or NUL, so a value can never split the request into a second one.
- **No ambient identity.** No cookie jar, no credential store, no `.netrc`, no
  shared cache. A request carries exactly what the caller put in it, and a
  response is never served out of a stale disk cache.
- **TLS 1.2 floor.**
- **Bounded.** The per-request timeout and the response cap are always enforced.
- **Nothing sensitive is logged.** A failure logs one line with the URL and the
  reason — never a body, never headers. A request may carry a token.

These rules are PURE code (`core_http/HttpPolicy`), so every backend applies the
identical decision and the whole set is unit-tested headlessly.

### What the tests prove, and what they cannot

The loopback suites (`HttpClientTests`, `HttpScriptTests`) run against the
tree's own `HttpServer` on 127.0.0.1: status codes, headers, POST bodies,
multi-megabyte transfers, progress, the size cap, timeouts, cancellation,
save-to-file byte-exactness, a refused connection, and every policy refusal —
all deterministic and offline. One case also points an `https://` request at
that plain-http server and asserts it fails rather than falling back, which
proves TLS is genuinely attempted.

What a plain loopback server CANNOT prove is certificate chain verification.
That is the one thing the opt-in `http_network` test does, against a real HTTPS
endpoint: **TLS verification is only proven when that test runs.** It skips
(exit 77) when the machine cannot reach the endpoint, when `ORKIGE_NO_NETWORK`
is set, or when the build has no client, and fails only when a server WAS
reached and the result was wrong — so a missing network never turns it red.
Override the endpoint with `ORKIGE_HTTP_NETWORK_URL`.

## The transports

One backend per platform behind `HttpBackend`, selected in CMake so no call
site carries a platform `#ifdef` (the `HapticManager` + `HapticBridgeApple.mm`
split), and each confined to ONE translation unit (the `StbVorbisImpl.cpp`
convention).

| Platform | Transport | Certificate trust | New dependency |
| --- | --- | --- | --- |
| macOS, iOS | `NSURLSession` (`HttpBackendApple.mm`) | system keychain | none |
| Windows | libcurl + Schannel (`HttpBackendCurl.cpp`) | Windows cert store | libcurl |
| Linux, Android | libcurl + OpenSSL (same TU) | system CA store | libcurl + OpenSSL |
| web (wasm) | the page's `fetch` (`HttpBackendFetch.cpp`) | the browser's | none |
| `ORKIGE_HTTP=OFF` | none (`HttpBackendNone.cpp`) | — | none |

Why not one library everywhere: current libcurl has no Apple-native TLS
backend, so a curl build on macOS/iOS would need OpenSSL, and OpenSSL on Apple
platforms has no trust anchors — the engine would have to ship and maintain its
own CA bundle. The platform's own stack verifies against the keychain, adds
nothing to the binary, and is what the App Store expects an app to use. A wasm
module has no sockets at all, so the page's fetch is the only road out. Windows
and Linux/Android share one curl TU because curl reaches the platform trust
store on both (Schannel and the OpenSSL CA store).

Threading is each backend's own business, hidden behind the seam: the curl
backend owns one worker thread and one curl multi handle, `NSURLSession` uses
its own queues, and the browser backend has no thread at all. All three publish
results only through the same mutex-guarded event queue that `update()` drains —
the discipline the physics contact queue uses.

### Platform behaviour worth knowing

- **iOS/macOS**: App Transport Security is a second gate in front of ours. In a
  bundled app a plain-http load to a remote host is refused by the OS even with
  `allowInsecureHttp` set (loopback works). Ask for https.
- **Android**: the system trust anchors are a hashed PEM directory
  (`/system/etc/security/cacerts`), which the curl backend points at.
- **web**: the browser owns redirects and CORS. A cross-origin request needs the
  server's CORS headers, and the browser deliberately does not say WHY a request
  failed (that would be a probing oracle) — the reason is honest about that.
  Blocked mixed content surfaces as a failed request, which is the same verdict
  our own downgrade rule reaches.
- **Compression**: transparent content encoding is the platform's default
  behaviour, so a `total` may be absent or refer to the encoded size on some
  transports. Treat `total == 0` as "unknown", which a progress bar must handle
  anyway.

## The Lua surface

```lua
http.get(url, onComplete [, onProgress]) -> id
http.post(url, body, contentType, onComplete [, onProgress]) -> id
http.download(url, savePath, onComplete [, onProgress]) -> id
http.request{ url = ..., method = ..., body = ..., contentType = ...,
              savePath = ..., timeout = <seconds>, maxBytes = ...,
              headers = { "Authorization: Bearer x" },
              allowInsecureHttp = ..., followRedirects = ...,
              maxRedirects = ...,
              onComplete = function(res) end,
              onProgress = function(received, total) end } -> id
http.cancel(id) -> bool
http.isAvailable() -> bool
http.pending() -> number
```

`onComplete` receives one table:
`{ ok, status, body, path, bytes, url, error, reason, headers }` — `error` is
the failure token and is `""` for a completed exchange, `headers` is a table
keyed by lower-cased header name. Headers are authored the way HTTP writes
them, `"Name: value"`, and `timeout` is in SECONDS (the unit a game thinks in).

`onComplete` is required: a request whose answer nobody reads does nothing and
says so in the log. Worked examples are in
[the Lua API reference](lua-api.md#canonical-snippets).

**A request belongs to the script that made it.** It is tagged with the calling
sandbox, so when that component is removed, its scene switches or its script
hot-reloads, its pending requests retire silently — an answer is never delivered
into a dead sandbox, exactly like a scheduled timer never fires into one.

**Without a client the whole table is an honest no-op**: `isAvailable()` is
false, a submission returns 0 and logs why. That is the editor's edit mode (it
creates no client) and an `ORKIGE_HTTP=OFF` build. In an
`ORKIGE_SCRIPTING=OFF` build the table does not exist at all — the C++ seam is
unchanged and still compiles.

## No MCP verb — deliberately

The editor's MCP endpoint exposes no HTTP verb, and this is a decision rather
than an omission. The endpoint exists to give an agent control of the EDITOR:
scenes, assets, play sessions, tests, the debugger. A "fetch this URL" verb
would turn it into a general network egress path out of the machine — an agent
could read from or post to anywhere, with the editor's credentials and inside
whatever network the editor can reach. That is the same reasoning that keeps git
mutations off MCP: some capabilities are the owner's, not the agent's.

An agent that needs a game to talk to a server writes that into the game's own
Lua and runs it — where the request is visible in the project, reviewable in a
diff, and bounded by this page's policy.

## Build option

`ORKIGE_HTTP` (default ON) selects whether a transport is compiled at all. With
it OFF the seam still compiles, `HttpClient::available()` is false and every
request refuses with a reason that names the option — the lever for a
size-constrained target whose game never calls out.

The weight is asymmetric, which is why the option exists at all rather than
being on by decree: on macOS, iOS, Windows-with-Schannel and the web there is
no new third-party library, only a system stack (Windows adds libcurl itself,
~half a megabyte of static code). Linux and Android are the ones that pay,
pulling libcurl plus OpenSSL into the closure; on Android that is the only
place the option is likely to matter for a shipping APK. If it ever does, the
principled fix is a JNI backend over the platform's own HTTP stack behind this
same seam — one more file beside the other four, with no change above it.
