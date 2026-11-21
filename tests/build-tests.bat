@echo off
rem Build and run moonlight-mic QtTests
setlocal enabledelayedexpansion

set QT_PATH=C:\Qt\6.10.3\msvc2022_64\bin
rem The tests link OpenSSL, SDL2 and opus from the repo prebuilts; add the DLL
rem directory so the test executable can start.
set PATH=%QT_PATH%;%~dp0..\libs\windows\lib\x64;%PATH%

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed
    exit /b 1
)

set SOURCE_DIR=<repo-root>\moonlight-mic\moonlight-qt\tests
set BUILD_DIR=<repo-root>\moonlight-mic\moonlight-qt-tests-build
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo.
echo === qmake configure ===
cd /d "%BUILD_DIR%"
qmake "%SOURCE_DIR%\moonlight-mic-tests.pro" CONFIG+=debug 2>&1
if errorlevel 1 (
    echo ERROR: qmake failed
    exit /b 1
)

echo.
echo === nmake build ===
nmake debug 2>&1
set BUILD_RC=%errorlevel%
if %BUILD_RC% NEQ 0 (
    echo ERROR: nmake failed with code %BUILD_RC%
    exit /b %BUILD_RC%
)

echo.
echo === run tests ===
set SDL_AUDIODRIVER=dummy
debug\moonlight-mic-tests.exe -v2 2>&1
set TEST_RC=%errorlevel%

echo.
echo === done (exit %TEST_RC%) ===
exit /b %TEST_RC%
