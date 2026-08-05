# Third-party code provenance + pinning

Where Orkige's third-party code comes from, how each source is pinned, and how a
maintainer re-pins it on an intentional bump. The security-relevant slice is the
**single-file libraries that parse untrusted asset data** — audio (OGG/Vorbis),
fonts (TrueType), vector art (SVG), image data (PNG) — because a malformed asset
an agent fetched off the web reaches those parsers first. This page is the map;
the runtime input-trust guarantees around them live in
[security.md](security.md).

## Almost everything is vcpkg-pinned

The library set is resolved from `vcpkg.json` in manifest mode against a **single
checked-out vcpkg commit**, not a moving registry. The manifest carries no
`builtin-baseline`; instead the pinned tree defines every port version. CI pins
it explicitly:

    VCPKG_COMMIT: 42e4e33e1505c9f47b58c21e0f557c1571b751ee   # .github/workflows/ci.yml

and a dev machine points `VCPKG_ROOT` at a checkout on the same commit. So the
whole dependency closure — including every single-file asset parser — moves only
when that commit is deliberately bumped and the port set rebuilds through the ABI
hash. **The vast majority of parsing code (tinyxml2, stb, nanosvg, earcut-hpp,
freetype/libpng via OGRE, zlib, Lua) is vcpkg-managed and therefore already
version-pinned by the vcpkg commit above — none of it is vendored in-tree.**

### Single-file asset parsers (vcpkg-pinned, confined to one TU)

Each is confined to a single dedicated translation unit so the library stays out
of every header and the precompiled header (the `*Impl.cpp` convention). Versions
below are what `VCPKG_COMMIT` resolves today.

| Library | Parses | vcpkg port (version) | Translation unit | License |
|---|---|---|---|---|
| miniaudio | (no asset parsing — the audio device, mixer and voices) | `miniaudio` (0.11.25) | `engine_sound/MiniaudioImpl.cpp` | Unlicense (public domain) / MIT-0 (dual) |
| stb_vorbis | OGG/Vorbis music | `stb` (2024-07-29; stb_vorbis v1.22) | `engine_sound/StbVorbisImpl.cpp` | MIT / public-domain (dual) |
| stb_truetype | TrueType glyphs | `stb` (2024-07-29) | `engine_gui/FontBakeImpl.cpp` | MIT / public-domain (dual) |
| stb_image | PNG decode | `stb` (2024-07-29) | `tools/editor/EditorImageDecode.cpp` | MIT / public-domain (dual) |
| nanosvg | SVG UI sprites | `nanosvg` (2023-12-29) | `engine_gui/SvgRasterImpl.cpp` | Zlib |
| earcut | polygon triangulation | `earcut-hpp` (2.2.4) | `core_util/VectorTessellator.cpp` | ISC |

miniaudio is in the table because it is a single-file library confined the same
way (`MINIAUDIO_IMPLEMENTATION` in one TU), not because it reads assets: the
engine decodes every sound itself and hands it finished PCM, and the library's
own file decoders are compiled out (`MA_NO_DECODING`). Its licence terms place
no condition on a distributed binary, which is what lets a shipped game and the
editor link it statically with nothing to carry.

The `stb` headers define their implementation in the TU above
(`STB_*_IMPLEMENTATION`); nanosvg is precompiled by its vcpkg port into static
libs (`NanoSVG::nanosvg` / `NanoSVG::nanosvgrast`) and only its declaration
headers are included — the porting rationale is in
[ports.md](ports.md#nanosvg-stock-port--no-overlay). tinyxml2 (`version-semver`
11.0.0, Zlib) backs `core_serialization/XMLArchive` but is a precompiled library,
not a single-file TU, so it is not in the table.

## The one genuinely in-tree vendored single-file library

One single-file header is vendored in the source tree rather than pulled from
vcpkg:

| Library | Purpose | Pinned version | File | License |
|---|---|---|---|---|
| FastDelegate | compile-time delegates for the event/callback layer | 1.5 (30-Mar-2005, Don Clugston) | `orkige_core/core_util/FastDelegate.h` | CodeProject release (CPOL); no explicit license header in-file |

Upstream: Don Clugston, *"Member Function Pointers and the Fastest Possible C++
Delegates"* — https://www.codeproject.com/Articles/7150/. It backs
`core_event/EventListener.h` and is included by a handful of headers
(`CameraComponent.h`, `IngameConsole.h`). It is **pure C++ template machinery and
parses no external data** — it turns a member-function pointer into a callable, so
it carries none of the untrusted-input risk the asset parsers do. It is tracked
here because it is the sole third-party source not pinned by vcpkg.

## Update cadence

The vcpkg-managed libraries and the one in-tree library are re-checked on the same
cadence — a deliberate manual pass, since neither is auto-updated:

- **vcpkg-managed (the whole table above + tinyxml2, freetype, libpng, zlib, Lua,
  …).** Bumping `VCPKG_COMMIT` in `ci.yml` (and the matching local checkout) rolls
  every port forward to that commit's versions; the ABI hash forces the affected
  ports to rebuild on all triplets and the full suite proves the bump. Review
  upstream security advisories for the asset parsers (stb, nanosvg, earcut,
  tinyxml2, libpng, freetype) and for miniaudio at least each release cycle,
  and when a CVE for one is reported; a security fix is a `VCPKG_COMMIT` bump,
  tested, in its own commit.
- **FastDelegate (in-tree).** Upstream is effectively frozen (last revision 2005),
  so there is no feed to poll; if a correctness or safety issue ever surfaces, the
  fix is re-vendoring the single header and updating the version row above in the
  same commit. Do not silently edit it in place without recording the new version.

Do not bump a library version as a side effect of an unrelated change — a version
move is its own tested commit.

## Redistribution: the third-party notices

Pinning answers *which* code is in a build. Redistribution asks a second
question: several of those licenses require their copyright notice and license
text to travel with a **binary**, not merely with the source. Orkige links its
closure statically, so a published editor, a published player and every
exported game each contain that closure and each owe the notices.

[`THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md) at the repository root is
that text: a tier table of what ships in which artifact, a section naming every
license whose obligation goes beyond attribution, and the verbatim copyright
file of each component. Its per-component texts come from the authoritative
source — the `share/<port>/copyright` file vcpkg installs, and the committed
license file for media and in-tree sources. `Util/make_third_party_notices.py`
assembles it from those files (a maintainer tool, run deliberately like a
`VCPKG_COMMIT` bump; the committed document is what ships):

    python3 Util/make_third_party_notices.py \
        --share build/macos-debug/vcpkg_installed/arm64-osx/share

**The file ships, it does not merely exist.** It is packaged at the resource
root of every artifact — the directory that already holds the default-project
marker, which is what `SDL_GetBasePath()` answers:

| Artifact | Path | Staged by |
|---|---|---|
| Orkige editor | the resource root, and the download archive's root | `tools/editor/CMakeLists.txt` + `Util/orkige_nightly_package.py` |
| Exported macOS app | `Contents/Resources/` | `tools/exporter/ExportMacos.cpp` |
| Exported iOS app | the bundle root (an iOS bundle is flat) | `tools/exporter/ExportIos.cpp` |
| Android APK / App Bundle | `assets/` | `tools/exporter/ExportAndroidAssemble.cpp` |
| Web build | beside `index.html`, so a visitor can open it | `tools/exporter/ExportWeb.cpp` |
| Engine SDK pack | the pack root | `cmake/OrkigeSdk.cmake` |
| Fetched device payload | the payload root | `Util/orkige_nightly_package.py` |

`OrkigeExport::stageThirdPartyNotices` is the one copy every packaging path
calls; it resolves the file from the same engine sources everything else comes
from (a distributed editor's own bundle, a fetched device payload, an installed
SDK pack, an engine source tree — in that order) and WARNS rather than refusing
when a source carries none. The export suite is what makes that warning
impossible to ship past: `export_*` asserts the file in each packaged artifact,
and the nightly packager's layout check asserts both editor copies.

**Adding a dependency means answering here.** `third_party_notices_lint`
(`Util/check_third_party_notices.py`) reads `vcpkg.json` and `ports/` and fails
on a dependency with no answer — a license section in the notices file, or a
recorded reason why it reaches no shipped artifact. A build-time-only component
(the test framework, a header registry) needs no attribution in a distributed
notice, and saying so explicitly is what keeps the file's silence meaningful.

One entry in that document is worth knowing without reading it:

- **LibRaw (LGPL-2.1 or CDDL-1.0) and FreeImage (GPL-2.0, GPL-3.0 or the
  FreeImage Public License)** reach the desktop editor and desktop player
  through the default render backend's image codec. Both are absent from the
  mobile and browser builds, and the classic flavor links neither.

## GitHub Actions are SHA-pinned

Every `uses:` step across all workflows (`.github/workflows/ci.yml`, `pages.yml`,
the `soak-next` job in `nightly.yml`) is pinned to a **full 40-hex commit SHA**
with the human-readable version in a trailing comment, e.g.:

    uses: actions/checkout@9c091bb21b7c1c1d1991bb908d89e4e9dddfe3e0  # v7.0.0

A mutable tag (`@v7`) can be repointed at attacker code by a compromised action
repo and would then rerun as trusted CI with the workflow's secrets; a commit SHA
is immutable, so a pinned step runs exactly the reviewed code. Local composite
actions (`./.github/...`) are in-repo and are left unpinned.

**Re-pinning on an intentional version bump.** Resolve the new tag to its commit
and swap the SHA + comment together (`gh` is authenticated on the dev machine):

    gh api repos/actions/checkout/commits/v7 --jq .sha    # -> new 40-hex SHA

Then update the `uses:` line to `@<newsha>  # <newtag>`. The invariant to keep
green: `grep -rE "uses:.*@v[0-9]" .github/workflows/` returns nothing — every
version tag lives in a comment, never in the ref.
