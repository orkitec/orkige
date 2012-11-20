#!/bin/bash

export MAINFOLDER=`pwd`

export OPTIONS=("$MAINFOLDER" -G "Unix Makefiles"
    -DOGRE_BUILD_PLATFORM_ANDROID=1
    -DCMAKE_TOOLCHAIN_FILE="$MAINFOLDER/CMake/Android/android.toolchain"
    -DORKIGE_BUILD_ANDROID=1 
    -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1
)

if [ "" = "$1" ]; 
then
	echo "Use: $0 <build target directory> [ debug|release|clean ] [ install ]"
	exit 1
fi

if [ "clean" = "$2" ];
then
echo cleaning...
if [ -d $1 ]; 
then
rm -rf "$1"
rm -f $MAINFOLDER/../game/Android/libs/armeabi/*.so
fi
exit 0
fi

if [ "" = "${NDK}" ]; 
then
	echo "NDK Path is empty."
    echo export NDK=/Users/User/Development/android-ndk-r8b
	exit 1
fi

if [ "" = "${ANDROID_HOME}" ]; 
then
    echo "ANDROID_HOME Path is empty."
    echo export ANDROID_HOME=/Users/User/android-sdks
    exit 1
fi

export NDK_BIN=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/darwin-x86/bin
export PATH=$PATH:$NDK_BIN


if [ ! -d ../../build ]; 
then
	mkdir ../../build
fi

if [ ! -d $1 ]; 
then
	mkdir "$1"
fi

if [ ! -d $1/orkige ]; 
then
	mkdir "$1/orkige"
fi

#
cd "$1" #>/dev/null 2>&1 && 
cd orkige

# It's necessary to run cmake twice in order to generate files needed for RTSS
if [ "debug" = "$2" ];
then
cmake "${OPTIONS[@]}" -DORKIGE_ANDROID_DEBUG=1 -DCMAKE_BUILD_TYPE="Debug" --debug-trycompile && \
cmake "${OPTIONS[@]}" -DORKIGE_ANDROID_DEBUG=1 -DCMAKE_BUILD_TYPE="Debug" --debug-trycompile
else
cmake "${OPTIONS[@]}" -DCMAKE_BUILD_TYPE="Release" && \
cmake "${OPTIONS[@]}" -DCMAKE_BUILD_TYPE="Release"
fi

make

cd $MAINFOLDER/../game/Android
./create_assets.sh
ant clean

if [ "install" = "$3" ];
then
    ant debug install
else
    ant debug
fi

