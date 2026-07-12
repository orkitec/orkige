#!/bin/bash
# package_apk.sh - assemble + sign the Orkige Player APK from the
# android-debug CMake build, WITHOUT Gradle.
#
# Why no Gradle: the only JDK on this machine is Homebrew OpenJDK 26, which
# Gradle/AGP do not support yet, and everything Gradle would do for this app
# (compile ~12 Java files, dex, pack, sign) maps 1:1 onto the SDK's own
# tools - so the APK is built directly with javac + d8 + aapt2 + zipalign +
# apksigner. Deterministic, no daemon, no downloads. Revisit Gradle when a
# store release needs it.
#
# What goes in:
#   lib/arm64-v8a/libmain.so  <- build/android-debug/tools/player/libmain.so
#                                (stripped copy; SDL3+OGRE+engine statically
#                                linked, built by 'cmake --build --preset
#                                android-debug')
#   classes.dex               <- SDL3's Java glue (taken from the exact SDL
#                                source vcpkg built, see sdl_java_sources) +
#                                OrkigeActivity
#   assets/                   <- same media set as the iOS bundle: the backend
#                                shader media (classic = RTSS shader lib, next
#                                = Hlms templates), sample assets, jumper media,
#                                example.oscene + orkige_assets.txt manifest
#                                (the player extracts them at first launch,
#                                see tools/player/main.cpp)
#
# Usage: tools/player/android/package_apk.sh [options] [build-dir]
#   build-dir defaults to build/android-debug. Output:
#   <build-dir>/apk/OrkigePlayer.apk
#
# Project-export options (Util/orkige_export.py drives these):
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
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

PROJECT_PAYLOAD=""
PACKAGE=""
LABEL=""
RES_DIR=""
LAUNCH_COLOR="#12161f"
OUTPUT=""
STAGE_ONLY=""
BUILD_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --project-payload) PROJECT_PAYLOAD="$2"; shift 2 ;;
        --package)         PACKAGE="$2"; shift 2 ;;
        --label)           LABEL="$2"; shift 2 ;;
        --res-dir)         RES_DIR="$2"; shift 2 ;;
        --launch-color)    LAUNCH_COLOR="$2"; shift 2 ;;
        --output)          OUTPUT="$2"; shift 2 ;;
        --stage-only)      STAGE_ONLY=1; shift ;;
        -*)                fail "unknown option '$1'" ;;
        *)                 BUILD_DIR="$1"; shift ;;
    esac
done
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/android-debug}"

SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
BUILD_TOOLS="$SDK/build-tools/35.0.0"
PLATFORM_JAR="$SDK/platforms/android-35/android.jar"
NDK="${ANDROID_NDK_HOME:-$SDK/ndk/27.2.12479018}"
STRIP="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk/libexec/openjdk.jdk/Contents/Home}"
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/Development/vcpkg}"
VCPKG_INSTALLED="$BUILD_DIR/vcpkg_installed/arm64-android"

# render flavor of the build tree (classic ships the RTSS shader lib, next
# ships the Hlms shader templates) - read from the tree's CMake cache
RENDER_BACKEND="$(grep -m1 '^ORKIGE_RENDER_BACKEND' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || true)"
RENDER_BACKEND="${RENDER_BACKEND:-classic}"

NATIVE_LIB="$BUILD_DIR/tools/player/libmain.so"
if [ -n "$OUTPUT" ]; then
    mkdir -p "$(dirname "$OUTPUT")"
    APK="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
    OUT_DIR="$(dirname "$APK")/apk-work"
else
    OUT_DIR="$BUILD_DIR/apk"
    APK="$OUT_DIR/OrkigePlayer.apk"
fi
STAGE="$OUT_DIR/stage"

[ -f "$NATIVE_LIB" ] || fail "no $NATIVE_LIB - build it first: cmake --build --preset android-debug"
[ -f "$PLATFORM_JAR" ] || fail "no android-35 platform jar at $PLATFORM_JAR"
[ -x "$BUILD_TOOLS/aapt2" ] || fail "no build-tools 35.0.0 at $BUILD_TOOLS"
[ -x "$STRIP" ] || fail "no llvm-strip at $STRIP"

# --- SDL3's Java side ---------------------------------------------------
# vcpkg's sdl3 port only builds/installs the native library; the Java glue
# (SDLActivity & co) lives in the SDL source tree's android-project. Take it
# from the exact source vcpkg built: buildtrees when present, else re-extract
# the verified source archive from vcpkg's downloads cache.
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

rm -rf "$STAGE" "$OUT_DIR/classes" "$OUT_DIR/dex"
mkdir -p "$STAGE/lib/arm64-v8a" "$STAGE/assets" "$OUT_DIR/classes" "$OUT_DIR/dex"

# --- native lib (stripped - the debug .so carries hundreds of MB of DWARF;
# symbols stay available in the build tree for ndk-stack) ------------------
echo "== stripping libmain.so"
"$STRIP" --strip-unneeded -o "$STAGE/lib/arm64-v8a/libmain.so" "$NATIVE_LIB"

# --- Java -> dex ----------------------------------------------------------
echo "== compiling Java (SDL glue + OrkigeActivity)"
# -source/-target 8 + -bootclasspath: the only combo javac still accepts a
# custom bootclasspath for - which is what keeps java.* resolving against
# android.jar instead of the host JDK. d8 happily consumes Java 8 bytecode.
"$JAVA_HOME/bin/javac" \
    -source 8 -target 8 -encoding UTF-8 \
    -bootclasspath "$PLATFORM_JAR" \
    -d "$OUT_DIR/classes" \
    -nowarn \
    "$SDL_JAVA_DIR"/*.java \
    "$SCRIPT_DIR/java/com/orkitec/orkigeplayer/OrkigeActivity.java" \
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
if [ "$RENDER_BACKEND" = "next" ]; then
    # next flavor: the Hlms shader templates (main.cpp points setHlmsMediaDir
    # at <extracted>/Media)
    cp -R "$VCPKG_INSTALLED/share/ogre-next/Media/Hlms" "$STAGE/assets/Media/Hlms"
else
    # classic flavor: the RTSS shader library
    cp -R "$VCPKG_INSTALLED/share/ogre/Media/Main"        "$STAGE/assets/Media/Main"
    cp -R "$VCPKG_INSTALLED/share/ogre/Media/RTShaderLib" "$STAGE/assets/Media/RTShaderLib"
fi
# the engine-default font (Nunito, SIL OFL) - flavor-independent; rides in the
# same bundled Media dir so a project referencing it by name ships self-
# contained (the player registers <extracted>/Media/fonts at boot)
[ -d "$REPO_ROOT/orkige_engine/media/fonts" ] \
    && cp -R "$REPO_ROOT/orkige_engine/media/fonts" "$STAGE/assets/Media/fonts"
# the engine water media (plane mesh + tiling normal) so a scene's
# WaterComponent ships self-contained (player registers <extracted>/Media/water)
[ -d "$REPO_ROOT/orkige_engine/media/water" ] \
    && cp -R "$REPO_ROOT/orkige_engine/media/water" "$STAGE/assets/Media/water"
cp -R "$REPO_ROOT/samples/hello_orkige/media"         "$STAGE/assets/assets"
cp -R "$REPO_ROOT/samples/jumper/media"               "$STAGE/assets/jumper_media"
cp    "$REPO_ROOT/samples/scenes/example.oscene"      "$STAGE/assets/example.oscene"
# project export: the payload (manifest, scenes/, assets/, scripts/ - staged
# by Util/orkige_export.py) plus the default-project marker; both extract
# with the rest of the assets, PlayerBundle then finds the marker at the
# extracted root and boots the project without any arguments
if [ -n "$PROJECT_PAYLOAD" ]; then
    [ -d "$PROJECT_PAYLOAD" ] || fail "no project payload dir at $PROJECT_PAYLOAD"
    echo "== staging project payload"
    cp -R "$PROJECT_PAYLOAD" "$STAGE/assets/project"
    printf 'project\n' > "$STAGE/assets/orkige_project.txt"
fi
# the extraction manifest the player reads at launch (paths relative to the
# assets root = relative to the extracted <files>/bundle/ root)
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

# --- resources (launcher icon + launch theme) -----------------------------
# Compiled only when --res-dir is given (project export). A bare run stays
# resource-free: the checked-in manifest then keeps the framework theme and no
# icon, so aapt2 links against no res/.
RES_LINK=()
if [ -n "$RES_DIR" ]; then
    [ -d "$RES_DIR" ] || fail "no res dir at $RES_DIR"
    echo "$LAUNCH_COLOR" | grep -Eq '^#[0-9A-Fa-f]{6}$' \
        || fail "launch-color '$LAUNCH_COLOR' is not #RRGGBB"
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
    echo "== aapt2 compile (res)"
    "$BUILD_TOOLS/aapt2" compile --dir "$RES_DIR" -o "$OUT_DIR/res.zip"
    # compiled resources are a positional link input (not -R, which is for
    # overlaying an existing table)
    RES_LINK=("$OUT_DIR/res.zip")
fi

# --- link + pack ----------------------------------------------------------
# project export: package name / app label overrides go through a substituted
# manifest copy (the activity is named fully qualified in the template, so a
# renamed package cannot break the component resolution). The launcher icon +
# launch theme references only resolve once res/ is linked in, so they are
# swapped in ONLY when --res-dir is set (a bare run keeps the resource-free
# framework theme).
MANIFEST="$SCRIPT_DIR/AndroidManifest.xml"
if [ -n "$PACKAGE" ] || [ -n "$LABEL" ] || [ -n "$RES_DIR" ]; then
    MANIFEST="$OUT_DIR/AndroidManifest.xml"
    SED_ARGS=(
        -e "s|package=\"com.orkitec.orkigeplayer\"|package=\"${PACKAGE:-com.orkitec.orkigeplayer}\"|"
        -e "s|android:label=\"Orkige Player\"|android:label=\"${LABEL:-Orkige Player}\"|"
    )
    if [ -n "$RES_DIR" ]; then
        SED_ARGS+=(
            -e "s|android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\"|android:icon=\"@mipmap/ic_launcher\"\n        android:theme=\"@style/OrkigeLaunch\"|"
        )
    fi
    sed "${SED_ARGS[@]}" "$SCRIPT_DIR/AndroidManifest.xml" > "$MANIFEST"
    echo "== manifest: package ${PACKAGE:-com.orkitec.orkigeplayer}, label '${LABEL:-Orkige Player}'${RES_DIR:+, launcher icon + launch theme}"
fi
echo "== aapt2 link"
"$BUILD_TOOLS/aapt2" link \
    --manifest "$MANIFEST" \
    -I "$PLATFORM_JAR" \
    -o "$OUT_DIR/unaligned.apk" \
    ${RES_LINK[@]+"${RES_LINK[@]}"}
echo "== packing"
(cd "$STAGE" && zip -q -r -X "$OUT_DIR/unaligned.apk" classes.dex lib assets)

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
