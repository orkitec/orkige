@call "%VS90COMNTOOLS%vsvars32.bat"

@echo off

set NDK=e:\SVN\android-ndk-r5c-windows\android-ndk-r5c\
set NDK_BIN=%NDK%\toolchains\arm-linux-androideabi-4.4.3\prebuilt\windows\bin
set PATH=%NDK_BIN%;%PATH%

set MAINFOLDER=%CD%

if "%1" == "" (
	echo "Enter build target directory."
	goto End
)

if "%NDK%" == "" (
	echo "NDK Path is empty."
	goto End
)

if "%LIBPATH%" == "" (
	echo "Warning: vc++ environment is missing."
	goto End
)

if not exist %1 (
	mkdir %1
)

if not exist "%1/orkige" (
	mkdir "%1/orkige"
)

cd %1
cd orkige

if "%2" == "debug" (	
	"%ProgramFiles%\CMake 2.8\bin\cmake.exe" %MAINFOLDER% -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE="%MAINFOLDER%/CMake/Android/android.toolchain" -DOGRE_BUILD_PLATFORM_ANDROID=1 -DORKIGE_BUILD_ANDROID=1 -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1 --debug-trycompile
	"%ProgramFiles%\CMake 2.8\bin\cmake.exe" %MAINFOLDER% -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE="%MAINFOLDER%/CMake/Android/android.toolchain" -DOGRE_BUILD_PLATFORM_ANDROID=1 -DORKIGE_BUILD_ANDROID=1 -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1 --debug-trycompile
) else (
	"%ProgramFiles%\CMake 2.8\bin\cmake.exe" %MAINFOLDER% -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE="%MAINFOLDER%/CMake/Android/android.toolchain" -DOGRE_BUILD_PLATFORM_ANDROID=1 -DORKIGE_BUILD_ANDROID=1 -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1 -DCMAKE_BUILD_TYPE="MinSizeRel"
	"%ProgramFiles%\CMake 2.8\bin\cmake.exe" %MAINFOLDER% -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE="%MAINFOLDER%/CMake/Android/android.toolchain" -DOGRE_BUILD_PLATFORM_ANDROID=1 -DORKIGE_BUILD_ANDROID=1 -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1 -DCMAKE_BUILD_TYPE="MinSizeRel"
)

ECHO Start orkige build? (y / n) 
:Question
SET /p choice= 
if /i not '%choice%' == 'n' (if /i '%choice%' == 'y' (goto Yes ) ) else goto No  
if defined choice ECHO Don't start a conversation i'm not interested! 
ECHO Answer with y for Yes and n for No. & goto Question 
:No
echo Leaving...
goto End
:Yes
echo Starting orkige build...
nmake
:End
cd %MAINFOLDER%

pause


rem cd %MAINFOLDER%
