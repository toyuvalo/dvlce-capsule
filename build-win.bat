@echo off
REM Build Dvlce Capsule on Windows: Visual Studio 2022 (or Build Tools) + CMake + Ninja.
REM   build-win.bat            core + tests + plugin (VST3, Standalone)
REM   build-win.bat core       core + tests only (no JUCE)
REM Environment (all optional): CMAKE, NINJA, BUILD_DIR, JUCE_DIR (a local JUCE 8.0.13 checkout), PYTHON.
setlocal
if not defined VSVARS set VSVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VSVARS%" set VSVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat
call "%VSVARS%" x64 >nul
if errorlevel 1 (
    echo vcvarsall failed: set VSVARS to your vcvarsall.bat
    exit /b 1
)
cd /d "%~dp0"
if not defined CMAKE set CMAKE=cmake
if not defined NINJA set NINJA=ninja
if not defined BUILD_DIR set BUILD_DIR=%~dp0build
set PLUGIN=ON
if /i "%1"=="core" (
    set BUILD_DIR=%BUILD_DIR%-core
    set PLUGIN=OFF
)
set JUCEARG=
if defined JUCE_DIR set JUCEARG=-DFETCHCONTENT_SOURCE_DIR_JUCE=%JUCE_DIR%
if not exist "%BUILD_DIR%\build.ninja" (
    "%CMAKE%" -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_MAKE_PROGRAM=%NINJA% ^
        -DDVLCE_CAPSULE_BUILD_PLUGIN=%PLUGIN% %JUCEARG%
    if errorlevel 1 exit /b 1
)
"%CMAKE%" --build "%BUILD_DIR%" -j 2
if errorlevel 1 exit /b 1
REM C++ core vs the Python prototype (needs numpy and scipy; skipped when python is missing).
if not defined PYTHON set PYTHON=python
"%PYTHON%" -I core\tests\make_refs.py "%BUILD_DIR%\capsule_refs.bin"
if errorlevel 1 (
    echo python failed: running the C++ tests without the prototype comparison
    "%BUILD_DIR%\dvlce_capsule_core_tests.exe"
    exit /b %errorlevel%
)
"%BUILD_DIR%\dvlce_capsule_core_tests.exe" "%BUILD_DIR%\capsule_refs.bin"
exit /b %errorlevel%
