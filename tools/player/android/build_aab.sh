#!/bin/bash
# build_aab.sh - assemble a release Android App Bundle (.aab) from the same
# CMake build package_apk.sh packs into a dev APK, WITHOUT Gradle.
#
# Google Play accepts only App Bundles for new releases. An .aab is a zip of
# protobuf-encoded modules plus a BundleConfig; Play's servers split it into
# per-device APKs at install time. The minimal correct producer is:
#
#   1. reuse package_apk.sh --stage-only to build the shared tree
#      (classes.dex + lib/arm64-v8a/libmain.so + assets/), identical to the APK
#   2. aapt2 link --proto-format  -> a protobuf AndroidManifest + resources.pb
#   3. repackage that + the staged dex/assets/lib into a bundle MODULE zip
#      (manifest/ dex/ res/ resources.pb assets/ lib/ at the zip root)
#   4. bundletool build-bundle    -> wrap the module(s) into the .aab
#   5. jarsigner                  -> sign the .aab with the RELEASE upload key
#
# Steps 1-3 need only the Android SDK build-tools (present) and produce the
# bundle module - the structural, signing-free slice this script can always
# build and a test can assert (--module-only stops there). Steps 4-5 need
# bundletool (a separate download, NOT part of build-tools) and a release
# keystore; absent either, the script degrades honestly - it refuses the signed
# .aab and points at Docs/store-release.md, exactly like the iOS device-signing
# gate. It NEVER emits a half-signed or unsigned .aab masquerading as
# submittable.
#
# Passwords are read from the environment (never the command line, never a
# committed file):
#   ORKIGE_ANDROID_KEYSTORE_PASS   keystore (store) password
#   ORKIGE_ANDROID_KEY_PASS        key password (defaults to the store password)
#
# Usage: tools/player/android/build_aab.sh [options] [build-dir]
#   build-dir defaults to build/android-release (fall back: android-debug).
#
# Options (Util/orkige_export.py drives these):
#   --project-payload <dir>  staged project payload -> assets/project + marker
#   --package <name>         manifest package name (release id)
#   --label <text>           app label
#   --res-dir <dir>          staged res/ (launcher mipmaps); enables icon+theme
#   --launch-color <#RRGGBB> cold-start window background (needs --res-dir)
#   --version-code <int>     android:versionCode (Play: strictly increasing)
#   --version-name <str>     android:versionName (human-facing)
#   --output <path>          output artifact path (.aab, or the module .zip in
#                            --module-only mode)
#   --module-only            stop after the bundle module zip (no bundletool /
#                            keystore needed) - the structural / CI slice
#   --keystore <path>        release keystore (full mode)
#   --key-alias <alias>      release key alias (full mode)
#   --bundletool <jar>       bundletool jar (full mode)
set -euo pipefail

fail() { echo "build_aab.sh: ERROR: $*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Play's minimum target API for new-app/app-update uploads. A build below this
# still packages, but Play will reject it - warn loudly (see Docs/store-release).
PLAY_TARGET_SDK_FLOOR=35

PROJECT_PAYLOAD=""
PACKAGE=""
LABEL=""
RES_DIR=""
LAUNCH_COLOR="#12161f"
ORIENTATION=""
VERSION_CODE=""
VERSION_NAME=""
# assets packaging mode (mirrors package_apk.sh): "stored" keeps the module's
# asset entries uncompressed AND passes bundletool a BundleConfig that keeps
# them uncompressed in the generated APKs, so the installed app mounts + reads
# them in place; "compressed" lets bundletool deflate them (extract path).
ASSETS_MODE="stored"
OUTPUT=""
MODULE_ONLY=""
KEYSTORE=""
KEY_ALIAS=""
BUNDLETOOL=""
BUILD_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --project-payload) PROJECT_PAYLOAD="$2"; shift 2 ;;
        --package)         PACKAGE="$2"; shift 2 ;;
        --label)           LABEL="$2"; shift 2 ;;
        --res-dir)         RES_DIR="$2"; shift 2 ;;
        --launch-color)    LAUNCH_COLOR="$2"; shift 2 ;;
        --orientation)     ORIENTATION="$2"; shift 2 ;;
        --version-code)    VERSION_CODE="$2"; shift 2 ;;
        --version-name)    VERSION_NAME="$2"; shift 2 ;;
        --assets)          ASSETS_MODE="$2"; shift 2 ;;
        --output)          OUTPUT="$2"; shift 2 ;;
        --module-only)     MODULE_ONLY=1; shift ;;
        --keystore)        KEYSTORE="$2"; shift 2 ;;
        --key-alias)       KEY_ALIAS="$2"; shift 2 ;;
        --bundletool)      BUNDLETOOL="$2"; shift 2 ;;
        -*)                fail "unknown option '$1'" ;;
        *)                 BUILD_DIR="$1"; shift ;;
    esac
done
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="$REPO_ROOT/build/android-release"
    [ -f "$BUILD_DIR/CMakeCache.txt" ] || BUILD_DIR="$REPO_ROOT/build/android-debug"
fi
[ -n "$OUTPUT" ] || fail "--output is required"

SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
BUILD_TOOLS="$SDK/build-tools/35.0.0"
PLATFORM_JAR="$SDK/platforms/android-35/android.jar"
JAVA_HOME="${JAVA_HOME:-/opt/homebrew/opt/openjdk/libexec/openjdk.jdk/Contents/Home}"

[ -x "$BUILD_TOOLS/aapt2" ] || fail "no build-tools 35.0.0 at $BUILD_TOOLS"
[ -f "$PLATFORM_JAR" ] || fail "no android-35 platform jar at $PLATFORM_JAR"

mkdir -p "$(dirname "$OUTPUT")"
OUTPUT="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
WORK="$(dirname "$OUTPUT")/aab-work"
rm -rf "$WORK"
mkdir -p "$WORK"

# --- step 1: reuse package_apk.sh staging (dex + native lib + assets) ------
echo "== staging (via package_apk.sh --stage-only)"
STAGE_ARGS=(--stage-only --assets "$ASSETS_MODE" --output "$WORK/stage.apk")
[ -n "$PROJECT_PAYLOAD" ] && STAGE_ARGS+=(--project-payload "$PROJECT_PAYLOAD")
STAGE_OUT="$("$SCRIPT_DIR/package_apk.sh" "${STAGE_ARGS[@]}" "$BUILD_DIR")"
echo "$STAGE_OUT"
STAGE="$(echo "$STAGE_OUT" | sed -n 's/^STAGE_DIR: //p' | tail -1)"
[ -n "$STAGE" ] && [ -d "$STAGE" ] || fail "staging produced no STAGE_DIR"
[ -f "$STAGE/classes.dex" ] || fail "no classes.dex in staged tree"

# --- release manifest (non-debuggable + versionCode/Name) -----------------
# The checked-in template is the debuggable dev-player manifest; a release
# bundle flips android:debuggable to false and stamps the version. The package
# / label / icon+theme substitutions mirror package_apk.sh so the two stay in
# lockstep.
MANIFEST="$WORK/AndroidManifest.xml"
SED_ARGS=(
    -e "s|package=\"com.orkitec.orkigeplayer\"|package=\"${PACKAGE:-com.orkitec.orkigeplayer}\"|"
    -e "s|android:label=\"Orkige Player\"|android:label=\"${LABEL:-Orkige Player}\"|"
    -e "s|android:debuggable=\"true\"|android:debuggable=\"false\"|"
)
[ -n "$VERSION_CODE" ] && SED_ARGS+=(-e "s|android:versionCode=\"1\"|android:versionCode=\"$VERSION_CODE\"|")
[ -n "$VERSION_NAME" ] && SED_ARGS+=(-e "s|android:versionName=\"2.0.0-dev\"|android:versionName=\"$VERSION_NAME\"|")
# lock the activity's screen orientation when the release requests one (auto
# passes no --orientation, keeping the template default)
[ -n "$ORIENTATION" ] && SED_ARGS+=(-e "s|android:configChanges=|android:screenOrientation=\"$ORIENTATION\" android:configChanges=|")
if [ -n "$RES_DIR" ]; then
    SED_ARGS+=(
        -e "s|android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\"|android:icon=\"@mipmap/ic_launcher\"\n        android:theme=\"@style/OrkigeLaunch\"|"
    )
fi
sed "${SED_ARGS[@]}" "$SCRIPT_DIR/AndroidManifest.xml" > "$MANIFEST"
echo "== manifest: package ${PACKAGE:-com.orkitec.orkigeplayer}, versionCode ${VERSION_CODE:-1}, versionName ${VERSION_NAME:-2.0.0-dev} (debuggable=false)"

# Play target-SDK sanity check (a below-floor build packages but Play rejects it)
TARGET_SDK="$(sed -n 's/.*android:targetSdkVersion="\([0-9]*\)".*/\1/p' "$MANIFEST" | head -1)"
if [ -n "$TARGET_SDK" ] && [ "$TARGET_SDK" -lt "$PLAY_TARGET_SDK_FLOOR" ]; then
    echo "build_aab.sh: WARNING: targetSdkVersion $TARGET_SDK is below Google Play's current floor ($PLAY_TARGET_SDK_FLOOR) - Play will reject the upload (see Docs/store-release.md)" >&2
fi

# --- step 2: aapt2 link --proto-format ------------------------------------
RES_LINK=()
if [ -n "$RES_DIR" ]; then
    [ -d "$RES_DIR" ] || fail "no res dir at $RES_DIR"
    echo "$LAUNCH_COLOR" | grep -Eq '^#[0-9A-Fa-f]{6}$' \
        || fail "launch-color '$LAUNCH_COLOR' is not #RRGGBB"
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
    "$BUILD_TOOLS/aapt2" compile --dir "$RES_DIR" -o "$WORK/res.zip"
    RES_LINK=("$WORK/res.zip")
fi
echo "== aapt2 link --proto-format"
"$BUILD_TOOLS/aapt2" link --proto-format \
    --manifest "$MANIFEST" \
    -I "$PLATFORM_JAR" \
    -o "$WORK/proto.apk" \
    ${RES_LINK[@]+"${RES_LINK[@]}"}

# --- step 3: assemble the bundle MODULE zip -------------------------------
# bundletool build-bundle consumes a per-module zip whose root is the module
# content: manifest/AndroidManifest.xml, dex/, res/, resources.pb, assets/, lib/
echo "== assembling bundle module"
PROTO_UNZIP="$WORK/proto"
rm -rf "$PROTO_UNZIP"; mkdir -p "$PROTO_UNZIP"
(cd "$PROTO_UNZIP" && unzip -qo "$WORK/proto.apk")
MODULE="$WORK/module"
rm -rf "$MODULE"; mkdir -p "$MODULE/manifest" "$MODULE/dex"
cp "$PROTO_UNZIP/AndroidManifest.xml" "$MODULE/manifest/AndroidManifest.xml"
[ -f "$PROTO_UNZIP/resources.pb" ] && cp "$PROTO_UNZIP/resources.pb" "$MODULE/resources.pb"
[ -d "$PROTO_UNZIP/res" ] && cp -R "$PROTO_UNZIP/res" "$MODULE/res"
cp "$STAGE/classes.dex" "$MODULE/dex/classes.dex"
cp -R "$STAGE/assets" "$MODULE/assets"
cp -R "$STAGE/lib" "$MODULE/lib"
BASE_ZIP="$WORK/base.zip"
rm -f "$BASE_ZIP"
(cd "$MODULE" && zip -q -r -X "$BASE_ZIP" .)
echo "   module: $(cd "$MODULE" && find . -type f | wc -l | tr -d ' ') files"

if [ -n "$MODULE_ONLY" ]; then
    cp "$BASE_ZIP" "$OUTPUT"
    echo "== done (module only): $OUTPUT"
    echo "   this is the proto bundle MODULE, NOT a submittable .aab - a signed"
    echo "   .aab needs bundletool + a release keystore (see Docs/store-release.md)"
    exit 0
fi

# --- step 4: bundletool build-bundle (gated) ------------------------------
[ -n "$BUNDLETOOL" ] && [ -f "$BUNDLETOOL" ] \
    || fail "bundletool jar not found ('$BUNDLETOOL') - install it and set ORKIGE_BUNDLETOOL (see Docs/store-release.md)"
AAB="$WORK/app.aab"
rm -f "$AAB"
# stored mode: a BundleConfig keeps the asset entries UNCOMPRESSED in the APKs
# bundletool generates, so the installed app can mount + read them in place
# (bundletool otherwise re-compresses per its defaults). glob syntax is
# bundletool's (assets/** under the module root).
BUNDLE_CONFIG_ARG=()
if [ "$ASSETS_MODE" = "stored" ]; then
    cat > "$WORK/BundleConfig.json" <<'JSON'
{
  "compression": {
    "uncompressedGlob": ["assets/**"]
  }
}
JSON
    BUNDLE_CONFIG_ARG=(--config="$WORK/BundleConfig.json")
fi
echo "== bundletool build-bundle (assets: $ASSETS_MODE)"
"$JAVA_HOME/bin/java" -jar "$BUNDLETOOL" build-bundle \
    --modules="$BASE_ZIP" --output="$AAB" \
    ${BUNDLE_CONFIG_ARG[@]+"${BUNDLE_CONFIG_ARG[@]}"}

# --- step 5: jarsigner (release upload key) -------------------------------
[ -f "$KEYSTORE" ] || fail "release keystore '$KEYSTORE' does not exist (see Docs/store-release.md)"
[ -n "$KEY_ALIAS" ] || fail "no --key-alias (ORKIGE_ANDROID_KEY_ALIAS)"
[ -n "${ORKIGE_ANDROID_KEYSTORE_PASS:-}" ] || fail "ORKIGE_ANDROID_KEYSTORE_PASS is not set"
# the key password defaults to the store password when unset
export ORKIGE_ANDROID_KEY_PASS="${ORKIGE_ANDROID_KEY_PASS:-$ORKIGE_ANDROID_KEYSTORE_PASS}"
echo "== jarsigner (release)"
"$JAVA_HOME/bin/jarsigner" -sigalg SHA256withRSA -digestalg SHA-256 \
    -keystore "$KEYSTORE" \
    -storepass:env ORKIGE_ANDROID_KEYSTORE_PASS \
    -keypass:env ORKIGE_ANDROID_KEY_PASS \
    "$AAB" "$KEY_ALIAS"
"$JAVA_HOME/bin/jarsigner" -verify "$AAB" >/dev/null \
    || fail "jarsigner could not verify the signed bundle"

cp "$AAB" "$OUTPUT"
echo "== done: $OUTPUT ($(du -h "$OUTPUT" | cut -f1 | tr -d ' '))"
echo "   upload the signed .aab to Google Play (see Docs/store-release.md)"
