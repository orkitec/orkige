#!/bin/bash
# package_apk.sh - assemble + sign the Orkige Player APK, WITHOUT Gradle, from
# either of the two engine sources: an android-* CMake BUILD TREE (the
# developer case) or a fetched device PAYLOAD (--payload, what an editor with
# no repository packages from - Docs/device-payloads.md).
#
# The payload carries every ENGINE piece this script needs: the already-
# stripped libmain.so, the engine media in its boot layout, this script, the
# manifest template, the res/ policy file and the Java sources compiled below.
# What stays the machine's is the Android SDK's own programs (aapt2, zipalign,
# apksigner, d8) and a JDK - a toolchain, which we never ship. The exporter
# names each missing one before it ever runs this script.
#
# Why no Gradle: the only JDK on this machine is Homebrew OpenJDK 26, which
# Gradle/AGP do not support yet, and everything Gradle would do for this app
# (compile ~12 Java files, dex, pack, sign) maps 1:1 onto the SDK's own
# tools - so the APK is built directly with javac + d8 + aapt2 + zipalign +
# apksigner. Deterministic, no daemon, no downloads. Revisit Gradle when a
# store release needs it.
#
# What goes in:
#   lib/<abi>/libmain.so      <- <build-dir>/tools/player/libmain.so (stripped
#                                copy; arm64-v8a for shipping builds, x86_64
#                                for the CI emulator)
#   classes.dex               <- SDL3's Java glue (taken from the exact SDL
#                                source vcpkg built, see sdl_java_sources) +
#                                OrkigeActivity
#   assets/                   <- same media set as the iOS bundle: the backend
#                                shader media (classic = RTSS shader lib, next
#                                = Hlms templates + Atmosphere sky media),
#                                sample assets, jumper media, example.oscene +
#                                orkige_assets.txt manifest (the player
#                                extracts them at first launch, see
#                                tools/player/main.cpp)
#
# Usage: tools/player/android/package_apk.sh [options] [build-dir]
#   build-dir defaults to build/android-debug. Output:
#   <build-dir>/apk/OrkigePlayer.apk
#
# Engine-source options (exactly one; a build-dir is the default):
#   --payload <dir>          assemble from a fetched device payload instead of
#                            a build tree. Needs --output, since a payload is
#                            read-only and has no apk/ directory of its own.
#
# Toolchain options (the exporter passes both, so the programs it checked for
# and reported on are exactly the ones that run; a hand run resolves its own):
#   --build-tools <dir>      the Android SDK build-tools directory to use
#   --java-home <dir>        the JDK to compile and sign with
#
# Project-export options (the project exporter drives these):
#   --project-payload <dir>  bundle a staged project payload as assets/project
#                            plus the assets/orkige_project.txt marker the
#                            player's PlayerBundle reads after extraction (the
#                            no-args default-project mechanism)
#   --package <name>         manifest package name (default
#                            com.orkitec.orkigeplayer; the Java classes keep
#                            their package - the manifest names them fully
#                            qualified for exactly this reason)
#   --label <text>           app label (default "Orkige Player")
#   --res-dir <dir>          a staged res/ tree (launcher mipmaps) to compile
#                            into the APK; enables the launcher icon + a
#                            windowBackground launch theme. Without it the
#                            manifest stays resource-free (framework theme, no
#                            icon) so a bare packaging run needs no res/.
#   --launch-color <#RRGGBB> cold-start window background colour (needs
#                            --res-dir; default #12161f)
#   --output <apk>           output APK path (intermediates go to
#                            <dir-of-apk>/apk-work instead of <build-dir>/apk)
#   --stage-only             stage the payload (classes.dex + lib/ + assets/)
#                            and stop before the binary-format link / pack /
#                            sign, printing "STAGE_DIR: <dir>". build_aab.sh
#                            drives this to reuse the staging for the release
#                            App Bundle (which links the same tree in protobuf
#                            format instead).
set -euo pipefail

fail() { echo "package_apk.sh: ERROR: $*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# the engine source tree this script sits in. Inside a device PAYLOAD the
# script sits at <payload>/android instead and there is no repository at all,
# so every use of this is guarded by the payload check below.
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." 2>/dev/null && pwd || echo "")"

PROJECT_PAYLOAD=""
ENGINE_PAYLOAD=""
BUILD_TOOLS_ARG=""
JAVA_HOME_ARG=""
PACKAGE=""
LABEL=""
RES_DIR=""
LAUNCH_COLOR="#12161f"
ORIENTATION=""
OUTPUT=""
STAGE_ONLY=""
# assets packaging mode: "stored" (the default) keeps the APK's asset entries
# UNCOMPRESSED so the player mounts the APK and reads them in place - no
# first-launch extraction (export.android.assets); "compressed" deflates them
# for a smaller APK and the player extracts on first launch (the older path).
ASSETS_MODE="stored"
BUILD_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --project-payload) PROJECT_PAYLOAD="$2"; shift 2 ;;
        --payload)         ENGINE_PAYLOAD="$2"; shift 2 ;;
        --build-tools)     BUILD_TOOLS_ARG="$2"; shift 2 ;;
        --java-home)       JAVA_HOME_ARG="$2"; shift 2 ;;
        --package)         PACKAGE="$2"; shift 2 ;;
        --label)           LABEL="$2"; shift 2 ;;
        --res-dir)         RES_DIR="$2"; shift 2 ;;
        --launch-color)    LAUNCH_COLOR="$2"; shift 2 ;;
        --orientation)     ORIENTATION="$2"; shift 2 ;;
        --assets)          ASSETS_MODE="$2"; shift 2 ;;
        --output)          OUTPUT="$2"; shift 2 ;;
        --stage-only)      STAGE_ONLY=1; shift ;;
        -*)                fail "unknown option '$1'" ;;
        *)                 BUILD_DIR="$1"; shift ;;
    esac
done
case "$ASSETS_MODE" in
    stored|compressed) ;;
    *) fail "--assets must be 'stored' or 'compressed' (got '$ASSETS_MODE')" ;;
esac
if [ -n "$ENGINE_PAYLOAD" ]; then
    [ -z "$BUILD_DIR" ] || fail "--payload and a build directory are two engine sources - pass one"
    [ -d "$ENGINE_PAYLOAD" ] || fail "no engine payload at $ENGINE_PAYLOAD"
    ENGINE_PAYLOAD="$(cd "$ENGINE_PAYLOAD" && pwd)"
    [ -n "$OUTPUT" ] || fail "--payload needs --output (a payload is read-only)"
else
    BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/android-debug}"
    [ -d "$BUILD_DIR" ] || fail "no build directory at $BUILD_DIR"
    BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
fi

SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
# the NEWEST installed build-tools and platform rather than a pinned pair: on a
# developer's machine those are whatever their SDK manager handed them, and
# every release since the min-api below assembles this package. Sorted with -V
# so 9.0.0 does not outrank 35.0.0.
newest_dir() {
    ls -1 "$1" 2>/dev/null | grep -E "$2" | sort -V | tail -1
}
if [ -n "$BUILD_TOOLS_ARG" ]; then
    BUILD_TOOLS="$BUILD_TOOLS_ARG"
else
    BUILD_TOOLS_VERSION="$(newest_dir "$SDK/build-tools" '^[0-9]+(\.[0-9]+)*$')"
    BUILD_TOOLS="$SDK/build-tools/$BUILD_TOOLS_VERSION"
fi
PLATFORM_DIR="$(newest_dir "$SDK/platforms" '^android-[0-9]+$')"
PLATFORM_JAR="$SDK/platforms/$PLATFORM_DIR/android.jar"
NDK="${ANDROID_NDK_HOME:-$SDK/ndk/27.2.12479018}"
if [ -n "$JAVA_HOME_ARG" ]; then
    JAVA_HOME="$JAVA_HOME_ARG"
elif [ -z "${JAVA_HOME:-}" ]; then
    # the JDK a `javac` on the PATH belongs to, else macOS's own locator, else
    # the Homebrew default - resolved rather than assumed, because a downloaded
    # editor runs on somebody else's machine
    if [ -x /usr/libexec/java_home ]; then
        # macOS ships /usr/bin/javac as a STUB that forwards to a real JDK, so
        # deriving a home from it lands on /usr; the platform's own locator
        # names the JDK the stub would have reached
        JAVA_HOME="$(/usr/libexec/java_home 2>/dev/null || true)"
    fi
    if [ -z "${JAVA_HOME:-}" ]; then
        JAVAC_PATH="$(command -v javac || true)"
        if [ -n "$JAVAC_PATH" ]; then
            JAVA_HOME="$(cd "$(dirname "$(readlink "$JAVAC_PATH" || echo "$JAVAC_PATH")")/.." && pwd)"
        fi
    fi
    JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk/libexec/openjdk.jdk/Contents/Home}"
fi
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/Development/vcpkg}"

cache_value() {
    sed -n "s/^$1:[^=]*=//p" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | head -1
}

# the payload's own manifest answers what a build tree's CMake cache does
payload_value() {
    sed -n "s/^$1: *//p" "$ENGINE_PAYLOAD/orkige_payload.txt" 2>/dev/null | head -1
}

if [ -n "$ENGINE_PAYLOAD" ]; then
    ANDROID_ABI="$(payload_value abi)"
    RENDER_BACKEND="$(payload_value flavor)"
    [ -n "$ANDROID_ABI" ] || fail "the payload's orkige_payload.txt names no abi"
    NATIVE_LIB="$ENGINE_PAYLOAD/libmain.so"
    # a payload's library is stripped at COMPOSITION time, on the machine that
    # has the NDK: the debug .so carries hundreds of MB of DWARF, which is not
    # something to download, and a client has no strip tool for this target
    STRIP=""
else
    ANDROID_ABI="$(cache_value ANDROID_ABI)"
    TARGET_TRIPLET="$(cache_value VCPKG_TARGET_TRIPLET)"
    ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
    case "$ANDROID_ABI" in
        arm64-v8a) TARGET_TRIPLET="${TARGET_TRIPLET:-arm64-android}" ;;
        x86_64)    TARGET_TRIPLET="${TARGET_TRIPLET:-x64-android}" ;;
        *)         fail "unsupported Android ABI '$ANDROID_ABI'" ;;
    esac

    # Prefer the exact strip tool recorded by the Android configure. This keeps
    # packaging reproducible from an existing build tree even when the calling
    # shell does not export ANDROID_NDK_HOME (for example a host-side export
    # test).
    STRIP="$(cache_value CMAKE_STRIP)"
    if [ ! -x "$STRIP" ]; then
        STRIP="$(find "$NDK/toolchains/llvm/prebuilt" -path '*/bin/llvm-strip' -type f -print -quit 2>/dev/null)"
    fi
    VCPKG_INSTALLED="$BUILD_DIR/vcpkg_installed/$TARGET_TRIPLET"

    # render flavor of the build tree (classic ships the RTSS shader lib, next
    # ships the Hlms shader templates) - read from the tree's CMake cache
    RENDER_BACKEND="$(grep -m1 '^ORKIGE_RENDER_BACKEND' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || true)"
    NATIVE_LIB="$BUILD_DIR/tools/player/libmain.so"
fi
RENDER_BACKEND="${RENDER_BACKEND:-classic}"

if [ -n "$OUTPUT" ]; then
    mkdir -p "$(dirname "$OUTPUT")"
    APK="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
    OUT_DIR="$(dirname "$APK")/apk-work"
else
    OUT_DIR="$BUILD_DIR/apk"
    APK="$OUT_DIR/OrkigePlayer.apk"
fi
STAGE="$OUT_DIR/stage"

[ -f "$NATIVE_LIB" ] || fail "no $NATIVE_LIB - build the Android preset first"
[ -f "$PLATFORM_JAR" ] || fail "no Android platform jar under $SDK/platforms (install one with 'sdkmanager \"platforms;android-35\"')"
[ -x "$BUILD_TOOLS/aapt2" ] || fail "no Android SDK build-tools under $SDK/build-tools (install them with 'sdkmanager \"build-tools;35.0.0\"')"
[ -x "$JAVA_HOME/bin/javac" ] || fail "no JDK at $JAVA_HOME (set JAVA_HOME, or install one)"

# --- the Java side ------------------------------------------------------
# SDL3's Java glue (SDLActivity & co) plus Orkige's own activity and HTTP
# transport. From a BUILD TREE those come from the exact SDL source vcpkg built
# plus the repository; from a PAYLOAD they were copied in at composition time,
# on the machine that had vcpkg - a downloaded editor has none, and the glue
# has to be the one that matches the libmain.so beside it anyway.
JAVA_SOURCES=()
if [ -n "$ENGINE_PAYLOAD" ]; then
    PAYLOAD_JAVA="$SCRIPT_DIR/java"
    [ -f "$PAYLOAD_JAVA/org/libsdl/app/SDLActivity.java" ] \
        || fail "the payload carries no SDL Java glue at $PAYLOAD_JAVA/org/libsdl/app"
    while IFS= read -r file; do JAVA_SOURCES+=("$file"); done \
        < <(find "$PAYLOAD_JAVA" -name '*.java' | LC_ALL=C sort)
    echo "== Java: ${#JAVA_SOURCES[@]} sources from the payload"
else
    sdl_java_sources() {
        local src
        src=$(ls -d "$VCPKG_ROOT"/buildtrees/sdl3/src/*.clean 2>/dev/null | head -1 || true)
        if [ -n "$src" ] && [ -d "$src/android-project/app/src/main/java/org/libsdl/app" ]; then
            echo "$src/android-project/app/src/main/java/org/libsdl/app"
            return
        fi
        local tarball
        tarball=$(ls "$VCPKG_ROOT"/downloads/libsdl-org-SDL-release-3.*.tar.gz 2>/dev/null | sort | tail -1 || true)
        [ -n "$tarball" ] || fail "SDL3 source not found (neither vcpkg buildtrees nor downloads) - run the android-debug configure once"
        local extract="$OUT_DIR/sdl3-java-src"
        rm -rf "$extract" && mkdir -p "$extract"
        tar -xzf "$tarball" -C "$extract" --strip-components=1 \
            "*/android-project/app/src/main/java/org/libsdl/app" 2>/dev/null \
            || tar -xzf "$tarball" -C "$extract" --strip-components=1
        echo "$extract/android-project/app/src/main/java/org/libsdl/app"
    }

    SDL_JAVA_DIR="$(sdl_java_sources)"
    [ -f "$SDL_JAVA_DIR/SDLActivity.java" ] || fail "SDLActivity.java not found under $SDL_JAVA_DIR"
    echo "== SDL3 Java glue: $SDL_JAVA_DIR"
    while IFS= read -r file; do JAVA_SOURCES+=("$file"); done \
        < <(find "$SDL_JAVA_DIR" -name '*.java' | LC_ALL=C sort)
    JAVA_SOURCES+=("$SCRIPT_DIR/java/com/orkitec/orkigeplayer/OrkigeActivity.java")
    JAVA_SOURCES+=("$REPO_ROOT/orkige_core/core_http/OrkigeHttp.java")
fi

rm -rf "$STAGE" "$OUT_DIR/classes" "$OUT_DIR/dex"
mkdir -p "$STAGE/lib/$ANDROID_ABI" "$STAGE/assets" "$OUT_DIR/classes" "$OUT_DIR/dex"

# --- native lib -----------------------------------------------------------
# From a build tree the debug .so carries hundreds of MB of DWARF, so it is
# stripped here (symbols stay in the tree for ndk-stack). A payload's library
# was stripped when the payload was composed, on the machine that had the NDK.
if [ -n "$STRIP" ]; then
    [ -x "$STRIP" ] || fail "no llvm-strip at $STRIP"
    echo "== stripping libmain.so"
    "$STRIP" --strip-unneeded -o "$STAGE/lib/$ANDROID_ABI/libmain.so" "$NATIVE_LIB"
else
    echo "== staging the payload's libmain.so ($ANDROID_ABI)"
    cp "$NATIVE_LIB" "$STAGE/lib/$ANDROID_ABI/libmain.so"
fi

# --- Java -> dex ----------------------------------------------------------
echo "== compiling Java (SDL glue + OrkigeActivity + the HTTP transport)"
# -source/-target 8 + -bootclasspath: the only combo javac still accepts a
# custom bootclasspath for - which is what keeps java.* resolving against
# android.jar instead of the host JDK. d8 happily consumes Java 8 bytecode.
"$JAVA_HOME/bin/javac" \
    -source 8 -target 8 -encoding UTF-8 \
    -bootclasspath "$PLATFORM_JAR" \
    -d "$OUT_DIR/classes" \
    -nowarn \
    "${JAVA_SOURCES[@]}" \
    2>&1 | (grep -v "deprecat\|source value 8\|target value 8" || true)

echo "== dexing"
find "$OUT_DIR/classes" -name '*.class' > "$OUT_DIR/classlist.txt"
"$JAVA_HOME/bin/java" -cp "$BUILD_TOOLS/lib/d8.jar" com.android.tools.r8.D8 \
    --release --min-api 28 --lib "$PLATFORM_JAR" \
    --output "$OUT_DIR/dex" \
    @"$OUT_DIR/classlist.txt"
cp "$OUT_DIR/dex/classes.dex" "$STAGE/classes.dex"

# --- assets (mirror of the iOS bundle layout) -----------------------------
echo "== staging assets ($RENDER_BACKEND flavor)"
mkdir -p "$STAGE/assets/Media"
if [ -n "$ENGINE_PAYLOAD" ]; then
    # a payload's Media/ IS this layout already: it was staged from the same
    # build tree the player came out of, so the shaders beside the library are
    # the ones it was built against
    cp -R "$ENGINE_PAYLOAD/Media/." "$STAGE/assets/Media/"
elif [ "$RENDER_BACKEND" = "next" ]; then
    # next flavor: the Hlms shader templates (main.cpp points setHlmsMediaDir
    # at <extracted>/Media) plus the Atmosphere sky material media, which the
    # next backend's registerAtmosphereMedia looks up as a sibling of Hlms/ -
    # optional (an older vcpkg port pin may not ship it yet; the runtime
    # degrades that honestly - no sky, flat fog colour)
    cp -R "$VCPKG_INSTALLED/share/ogre-next/Media/Hlms" "$STAGE/assets/Media/Hlms"
    [ -d "$VCPKG_INSTALLED/share/ogre-next/Media/Atmosphere" ] \
        && cp -R "$VCPKG_INSTALLED/share/ogre-next/Media/Atmosphere" "$STAGE/assets/Media/Atmosphere"
else
    # classic flavor: the RTSS shader library, plus the engine-owned
    # metal-rough shader library merged into the same registered location
    cp -R "$VCPKG_INSTALLED/share/ogre/Media/Main"        "$STAGE/assets/Media/Main"
    cp -R "$VCPKG_INSTALLED/share/ogre/Media/RTShaderLib" "$STAGE/assets/Media/RTShaderLib"
    [ -d "$REPO_ROOT/orkige_engine/media/rtss" ] \
        && cp -R "$REPO_ROOT/orkige_engine/media/rtss/." "$STAGE/assets/Media/RTShaderLib/"
fi
# the CONTENT media below and the sample assets after it live in the engine
# SOURCE tree. A payload's Media/ already carries the content half (its
# composition staged exactly these directories), and the samples are the
# player's own demo content, which a packaged game never boots into.
if [ -z "$ENGINE_PAYLOAD" ]; then
# the engine-default font (Nunito, SIL OFL) - flavor-independent; rides in the
# same bundled Media dir so a project referencing it by name ships self-
# contained (the player registers <extracted>/Media/fonts at boot)
[ -d "$REPO_ROOT/orkige_engine/media/fonts" ] \
    && cp -R "$REPO_ROOT/orkige_engine/media/fonts" "$STAGE/assets/Media/fonts"
# the engine water media (plane mesh + tiling normal) so a scene's
# WaterComponent ships self-contained (player registers <extracted>/Media/water)
[ -d "$REPO_ROOT/orkige_engine/media/water" ] \
    && cp -R "$REPO_ROOT/orkige_engine/media/water" "$STAGE/assets/Media/water"
# the engine bloom compositor media (bright/blur/combine material + shaders) so
# an Android next scene's engine:setBloom ships self-contained. The player only
# registers <extracted>/Media/bloom/next on a NEXT build (classic bloom is gated
# off), so a classic APK carrying it is a harmless few KB.
[ -d "$REPO_ROOT/orkige_engine/media/bloom/next" ] \
    && mkdir -p "$STAGE/assets/Media/bloom" \
    && cp -R "$REPO_ROOT/orkige_engine/media/bloom/next" "$STAGE/assets/Media/bloom/next"
# the engine output-grade compositor media (grade material + shaders) so an
# Android next scene's engine:setGrade ships self-contained (the player registers
# <extracted>/Media/grade/next on a NEXT build; a classic APK carrying it is a
# harmless few KB).
[ -d "$REPO_ROOT/orkige_engine/media/grade/next" ] \
    && mkdir -p "$STAGE/assets/Media/grade" \
    && cp -R "$REPO_ROOT/orkige_engine/media/grade/next" "$STAGE/assets/Media/grade/next"
cp -R "$REPO_ROOT/samples/hello_orkige/media"         "$STAGE/assets/assets"
cp -R "$REPO_ROOT/samples/jumper/media"               "$STAGE/assets/jumper_media"
cp    "$REPO_ROOT/samples/scenes/example.oscene"      "$STAGE/assets/example.oscene"
fi
# project export: the payload (manifest, scenes/, assets/, scripts/ - staged
# by the project exporter) plus the default-project marker; both extract
# with the rest of the assets, PlayerBundle then finds the marker at the
# extracted root and boots the project without any arguments
if [ -n "$PROJECT_PAYLOAD" ]; then
    [ -d "$PROJECT_PAYLOAD" ] || fail "no project payload dir at $PROJECT_PAYLOAD"
    echo "== staging project payload"
    cp -R "$PROJECT_PAYLOAD" "$STAGE/assets/project"
    printf 'project\n' > "$STAGE/assets/orkige_project.txt"
fi
# the mount marker: in `stored` mode the player MOUNTS the APK and reads its
# assets/ sub-tree in place (no extraction); its presence is how the player
# picks mount vs. extract at boot (a compressed APK carries no marker and takes
# the older extract-on-first-launch path). See PlayerBundle / tools/player/main.cpp.
if [ "$ASSETS_MODE" = "stored" ]; then
    printf 'stored\n' > "$STAGE/assets/orkige_mount.txt"
fi
# the extraction manifest the player reads at launch (paths relative to the
# assets root = relative to the extracted <files>/bundle/ root). Unused in the
# `stored` mount path, but kept so a compressed APK (or a fallback) still lists.
(cd "$STAGE/assets" && find . -type f ! -name orkige_assets.txt | sed 's|^\./||' | LC_ALL=C sort) \
    > "$STAGE/assets/orkige_assets.txt"
echo "   $(wc -l < "$STAGE/assets/orkige_assets.txt" | tr -d ' ') bundled files"

# --- stage-only seam (release App Bundle path) ----------------------------
# build_aab.sh reuses everything up to here (dex + native lib + assets) and
# links the SAME tree into a protobuf bundle module instead of a binary-format
# APK, so it stops here and takes over. A normal (APK) run never sets this.
if [ -n "$STAGE_ONLY" ]; then
    echo "STAGE_DIR: $STAGE"
    exit 0
fi

# --- resources ------------------------------------------------------------
# Every APK carries res/xml/orkige_network_security.xml: the manifest names it,
# because the platform's cleartext and trust-anchor policy has to agree with
# the engine's own HTTP policy rather than silently overrule it (see that file
# and Docs/http.md). --res-dir adds the launcher icon + launch theme on top;
# without it those references stay out of the manifest and the framework theme
# is kept.
HAVE_LAUNCHER_RES=""
if [ -n "$RES_DIR" ]; then
    HAVE_LAUNCHER_RES="1"
    [ -d "$RES_DIR" ] || fail "no res dir at $RES_DIR"
else
    RES_DIR="$OUT_DIR/res"
    mkdir -p "$RES_DIR"
fi
mkdir -p "$RES_DIR/xml"
cp "$SCRIPT_DIR/res/xml/orkige_network_security.xml" "$RES_DIR/xml/"
echo "$LAUNCH_COLOR" | grep -Eq '^#[0-9A-Fa-f]{6}$' \
    || fail "launch-color '$LAUNCH_COLOR' is not #RRGGBB"
if [ -n "$HAVE_LAUNCHER_RES" ]; then
    # generate the launch-screen theme + colour alongside the staged mipmaps
    mkdir -p "$RES_DIR/values"
    cat > "$RES_DIR/values/colors.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="launch_bg">$LAUNCH_COLOR</color>
</resources>
EOF
    cat > "$RES_DIR/values/styles.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <style name="OrkigeLaunch" parent="@android:style/Theme.NoTitleBar.Fullscreen">
        <item name="android:windowBackground">@color/launch_bg</item>
    </style>
</resources>
EOF
fi
echo "== aapt2 compile (res)"
"$BUILD_TOOLS/aapt2" compile --dir "$RES_DIR" -o "$OUT_DIR/res.zip"
# compiled resources are a positional link input (not -R, which is for
# overlaying an existing table)
RES_LINK=("$OUT_DIR/res.zip")

# --- link + pack ----------------------------------------------------------
# project export: package name / app label overrides go through a substituted
# manifest copy (the activity is named fully qualified in the template, so a
# renamed package cannot break the component resolution). The launcher icon +
# launch theme references only resolve once res/ is linked in, so they are
# swapped in ONLY when --res-dir is set (a bare run keeps the resource-free
# framework theme).
MANIFEST="$SCRIPT_DIR/AndroidManifest.xml"
if [ -n "$PACKAGE" ] || [ -n "$LABEL" ] || [ -n "$HAVE_LAUNCHER_RES" ] || [ -n "$ORIENTATION" ]; then
    MANIFEST="$OUT_DIR/AndroidManifest.xml"
    SED_ARGS=(
        -e "s|package=\"com.orkitec.orkigeplayer\"|package=\"${PACKAGE:-com.orkitec.orkigeplayer}\"|"
        -e "s|android:label=\"Orkige Player\"|android:label=\"${LABEL:-Orkige Player}\"|"
    )
    # lock the activity's screen orientation when the export requests one; auto
    # passes no --orientation, so the template's default (unspecified) stays
    if [ -n "$ORIENTATION" ]; then
        SED_ARGS+=(
            -e "s|android:configChanges=|android:screenOrientation=\"$ORIENTATION\" android:configChanges=|"
        )
    fi
    if [ -n "$HAVE_LAUNCHER_RES" ]; then
        SED_ARGS+=(
            -e "s|android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\"|android:icon=\"@mipmap/ic_launcher\"\n        android:theme=\"@style/OrkigeLaunch\"|"
        )
    fi
    sed "${SED_ARGS[@]}" "$SCRIPT_DIR/AndroidManifest.xml" > "$MANIFEST"
    echo "== manifest: package ${PACKAGE:-com.orkitec.orkigeplayer}, label '${LABEL:-Orkige Player}'${HAVE_LAUNCHER_RES:+, launcher icon + launch theme}"
fi
echo "== aapt2 link"
"$BUILD_TOOLS/aapt2" link \
    --manifest "$MANIFEST" \
    -I "$PLATFORM_JAR" \
    -o "$OUT_DIR/unaligned.apk" \
    ${RES_LINK[@]+"${RES_LINK[@]}"}
echo "== packing (assets: $ASSETS_MODE)"
if [ "$ASSETS_MODE" = "stored" ]; then
    # dex + native libs stay deflated; the ASSET entries go in UNCOMPRESSED
    # (zip -0) so the player mounts the APK and reads/streams them in place -
    # a compressed zip entry is not randomly seekable (the OGG-stream case)
    (cd "$STAGE" && zip -q -r -X "$OUT_DIR/unaligned.apk" classes.dex lib)
    (cd "$STAGE" && zip -q -r -X -0 "$OUT_DIR/unaligned.apk" assets)
else
    (cd "$STAGE" && zip -q -r -X "$OUT_DIR/unaligned.apk" classes.dex lib assets)
fi

echo "== zipalign"
"$BUILD_TOOLS/zipalign" -f 4 "$OUT_DIR/unaligned.apk" "$APK"

# --- sign (shared Android debug keystore; created on demand) --------------
DEBUG_KEYSTORE="$HOME/.android/debug.keystore"
if [ ! -f "$DEBUG_KEYSTORE" ]; then
    echo "== creating debug keystore"
    mkdir -p "$HOME/.android"
    "$JAVA_HOME/bin/keytool" -genkeypair -v \
        -keystore "$DEBUG_KEYSTORE" -storepass android -keypass android \
        -alias androiddebugkey -dname "CN=Android Debug,O=Android,C=US" \
        -keyalg RSA -keysize 2048 -validity 10000 >/dev/null 2>&1
fi
echo "== signing"
"$JAVA_HOME/bin/java" -jar "$BUILD_TOOLS/lib/apksigner.jar" sign \
    --ks "$DEBUG_KEYSTORE" --ks-pass pass:android --key-pass pass:android \
    "$APK"

echo "== done: $APK ($(du -h "$APK" | cut -f1 | tr -d ' '))"
echo "   install: $SDK/platform-tools/adb install -r $APK"
