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

Give `savePath` an ABSOLUTE path inside a directory the app may write. A mobile
app's working directory is not one — a relative path there resolves somewhere
read-only and the request refuses with `bad-save-path` before a byte is
fetched, which is the honest answer but an easy one to trip over when a desktop
run of the same code worked.

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

### The mobile runtime proof

A desktop suite proves the desktop transports. The mobile ones are different
code against different platform rules, so they get their own runs: the
`http_device_android` and `http_device_ios` tests export a real project as a
real app, install it on an emulator/simulator/device and let the GAME make the
requests, against a loopback server on the host (Android reaches it through
`adb reverse`; a simulator app shares the host loopback). They assert the same
contract as the desktop suite — status, headers, POST body, progress, the cap,
the timeout, cancellation, byte-exact save-to-file, the https-to-plain refusal —
plus the two things only a device can answer: that the platform's own cleartext
gate lets the `allowInsecureHttp` opt-in through, and that a real certificate
chain verifies against the device's trust store. Both skip (exit 77) when no
device is attached and the prerequisite app is not built; neither ever turns a
real failure into a skip.

On a device the platform log is the only channel a run reports through, so the
app host bridges the process's stdio into it at boot — `adb logcat -s orkige`
shows the engine's diagnostics and a script's `print` on Android, which is how
these tests read their verdicts.

## The transports

One backend per platform behind `HttpBackend`, selected in CMake so no call
site carries a platform `#ifdef` (the `HapticManager` + `HapticBridgeApple.mm`
split), and each confined to ONE translation unit (the `StbVorbisImpl.cpp`
convention).

| Platform | Transport | Certificate trust | New dependency |
| --- | --- | --- | --- |
| macOS, iOS | `NSURLSession` (`HttpBackendApple.mm`) | system keychain | none |
| Android | the platform's own HTTP stack over JNI (`HttpBackendAndroid.cpp` + `OrkigeHttp.java`) | device trust anchors, filtered by the app's network security config | none |
| Windows | libcurl + Schannel (`HttpBackendCurl.cpp`) | Windows cert store | libcurl |
| Linux | libcurl + OpenSSL (same TU) | system CA store | libcurl + OpenSSL |
| web (wasm) | the page's `fetch` (`HttpBackendFetch.cpp`) | the browser's | none |
| `ORKIGE_HTTP=OFF` | none (`HttpBackendNone.cpp`) | — | none |

Why not one library everywhere: certificate verification has to go through the
trust store the PLATFORM maintains, and on the two mobile platforms a bundled
library cannot see all of it.

- Current libcurl has no Apple-native TLS backend, so a curl build on
  macOS/iOS would need OpenSSL, and OpenSSL on Apple platforms has no trust
  anchors — the engine would have to ship and maintain its own CA bundle. The
  platform's own stack verifies against the keychain, adds nothing to the
  binary, and is what the App Store expects an app to use.
- On Android the device's trust decisions are not a directory of public roots.
  They are the system anchors PLUS whatever a managed device or a developer has
  installed, filtered by the app's own **network security config** — and that
  config is enforced by the platform's HTTP stack, not by the socket layer. A
  library reading `/system/etc/security/cacerts` sees none of it: it would
  quietly ignore a config that says "do not talk cleartext", quietly miss an
  enterprise anchor, and quietly bypass the device's proxy settings. Driving the
  platform stack over JNI gets all three right, and drops both libcurl and
  OpenSSL out of the APK entirely.
- A wasm module has no sockets at all, so the page's fetch is the only road out.
- Windows and Linux share one curl TU because curl reaches the platform trust
  store on both (Schannel and the OpenSSL CA store).

Threading is each backend's own business, hidden behind the seam: the curl
backend owns one worker thread and one curl multi handle, `NSURLSession` uses
its own queues, the Android backend runs one attached worker thread per transfer,
and the browser backend has no thread at all. All of them publish results only
through the same mutex-guarded event queue that `update()` drains — the
discipline the physics contact queue uses.

### The Android transport in detail

The Java half (`core_http/OrkigeHttp.java`, packaged into the APK beside the
window toolkit's own glue) performs ONE exchange per call and streams the
response back through native callbacks; the C++ half owns everything else.

- **The policy stays above the transport.** The platform is told *not* to follow
  redirects, so every hop comes back to `HttpPolicy::resolveRedirect` — the rule
  that a secure request can never be redirected onto a plain one has one
  implementation for every platform, not one per backend.
- **The size cap, the whole-request deadline and the save-to-file funnel** are
  enforced in the native half, from the same callbacks that carry the body: the
  announced size is refused before the first body byte, and a stream that
  outlives its deadline is stopped between chunks. (The platform's own timeouts
  bound a connect and a single read, which is not the same promise.)
- **The TLS floor is pinned in Java**, by narrowing each socket's enabled
  protocols on top of the platform's own factory — so the trust anchors, the
  network security config and any certificate pinning the device applies all
  still hold, and only the protocol versions are restricted.
- **No ambient identity**: caches are off per connection and per default, and
  no cookie handler is installed.
- **Cancellation** unblocks a transfer waiting on the network (the connection is
  disconnected from the cancelling thread) as well as stopping it between chunks.
- The process's Java VM reaches the transport through one registration seam
  (`core_http/HttpAndroid.h`), which the app host fills in at boot. A host that
  registers none gets a transport that refuses with a reason — never a crash.

What it does NOT do, stated rather than discovered: a request body is sent as
one buffer, so there is no streaming UPLOAD of a large file (downloads stream);
and each transfer costs its own thread, which suits a game making a handful of
requests and would want a pool if one ever made hundreds.

### The mobile cleartext gates

Both mobile platforms have a policy of their own in front of this one, and both
of them block plain http by default. That is the same verdict our policy
reaches, so the two agree — until a caller sets `allowInsecureHttp` for a
service on their own machine, at which point a platform that still says no turns
a deliberate choice into a mystery. Each export therefore ships the NARROWEST
declaration that permits exactly the localhost/LAN case and nothing else.

- **iOS**: App Transport Security. Every iOS `Info.plist` the exporter writes
  carries `NSAppTransportSecurity` with **`NSAllowsLocalNetworking`** — which
  covers loopback, `.local` and LAN literal addresses. `NSAllowsArbitraryLoads`
  is deliberately NOT emitted: it opens cleartext to the whole internet and is
  an App Store review question. A plain-http request to a REMOTE host is still
  refused by the OS whatever the caller opted into, and the refusal arrives as a
  normal failed request with the system's own reason in `reason`.
- **Android**: the app's network security config. Every APK and App Bundle
  ships `res/xml/orkige_network_security.xml` and names it from the manifest:
  cleartext is permitted for `localhost`, `127.0.0.1`, `::1` and `10.0.2.2`
  (the address an emulator reaches its host on) and refused everywhere else,
  the base config trusts the system anchors only, and a **debuggable** build
  additionally trusts user-installed certificates so a developer's own proxy or
  local certificate authority works during development. A shipped game keeps
  the system anchors. Without this file the platform answers a loopback request
  with `Cleartext HTTP traffic to 127.0.0.1 not permitted` — an honest message,
  but only because the platform stack is the one enforcing it.
- A game that genuinely needs a plain-http host beyond these edits its own copy
  of the declaration. The engine will not widen the default for it.

`android.permission.INTERNET` is in the player manifest and in every exported
one: without it every request — including one to loopback — fails at the socket.

### Other platform behaviour worth knowing

- **web**: the browser owns redirects and CORS, and **CORS is a real constraint
  rather than a bug to route around**. A browser build can only read a response
  from another origin if THAT server opts in with `Access-Control-Allow-Origin`
  (and, for anything beyond a simple request, answers the preflight `OPTIONS`
  with the matching `Access-Control-Allow-Headers`/`-Methods`). A custom request
  header — an `Authorization: Bearer …` among them — turns a simple request into
  a preflighted one, so a server that works for the desktop build can still
  refuse the same call from the web build. Nothing in the engine can change
  that: the check is the browser's and it happens before our code sees an
  answer. The browser also deliberately does not say WHY a cross-origin request
  failed (that would be a probing oracle), so the failure arrives as a generic
  transport error and the `reason` says so instead of inventing a cause. Plan
  for it: serve the game and its API from one origin, or make the API send the
  headers. Blocked mixed content surfaces as a failed request, which is the same
  verdict our own downgrade rule reaches. Everything else on this page holds:
  the policy refusals are identical (they are the same pure code), an HTTP
  status is still an answer, the cap and the timeout are still enforced, and
  `savePath` still works — the whole body is buffered first and written once,
  because the fetch API hands a wasm module no incremental file sink.
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
being on by decree: on macOS, iOS, Android and the web there is no third-party
library at all, only a system stack. Windows adds libcurl itself (~half a
megabyte of static code) and Linux adds libcurl plus OpenSSL — the two desktop
platforms, where the size hardly matters. Neither mobile platform pays anything
for having a network client, which is the point of driving each one's own stack.
