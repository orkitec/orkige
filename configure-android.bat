
@echo "%VS100COMNTOOLS%vsvars32.bat"
@call "%VS100COMNTOOLS%vsvars32.bat"

@echo off

set MAINFOLDER=%CD%


if "%1" == "" (
	echo "Enter build target directory."
	goto End
)

set NDK=C:\Development\android-ndk-r8\
if "%NDK%" == "" (
	echo "NDK Path is empty. Please enter Path or set NDK enviroment variable to the correct PATH"
	SET /p NDK= 
)
	set NDK_BIN=%NDK%\toolchains\arm-linux-androideabi-4.4.3\prebuilt\windows\bin
	set PATH=%NDK_BIN%;%PATH%
	
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
	"cmake.exe" %MAINFOLDER% -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE="%MAINFOLDER%/CMake/Android/android.toolchain" -DORKIGE_ANDROID_DEBUG=1 -DOGRE_BUILD_PLATFORM_ANDROID=1 -DORKIGE_BUILD_ANDROID=1 -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1 --debug-trycompile
	"cmake.exe" %MAINFOLDER% -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE="%MAINFOLDER%/CMake/Android/android.toolchain" -DORKIGE_ANDROID_DEBUG=1 -DOGRE_BUILD_PLATFORM_ANDROID=1 -DORKIGE_BUILD_ANDROID=1 -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1 --debug-trycompile
) else (
	"cmake.exe" %MAINFOLDER% -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE="%MAINFOLDER%/CMake/Android/android.toolchain" -DOGRE_BUILD_PLATFORM_ANDROID=1 -DORKIGE_BUILD_ANDROID=1 -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1 -DCMAKE_BUILD_TYPE="Release"
	"cmake.exe" %MAINFOLDER% -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE="%MAINFOLDER%/CMake/Android/android.toolchain" -DOGRE_BUILD_PLATFORM_ANDROID=1 -DORKIGE_BUILD_ANDROID=1 -DORKIGE_MINIMAL_FREEIMAGE_CODEC=1 -DCMAKE_BUILD_TYPE="Release"
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
nmake /X android_build_errors.txt
if ERRORLEVEL 2 (
	type android_build_errors.txt
	ECHO Buildlog has been written to %CD%\android_build_errors.txt
	ECHO "Build had some errors try to rebuild? (y / n)." 
) else (
	ECHO "Build succesful finished... rebuild? (y / n)."
)
	SET /p choice= 
	if /i not '%choice%' == 'n' (if /i '%choice%' == 'y' (goto Yes ) ) else goto No  
	if defined choice ECHO Don't start a conversation i'm not interested! 
	ECHO Answer with y for Yes and n for No. & goto Question 
:End
cd %MAINFOLDER%

pause


rem cd %MAINFOLDER%
