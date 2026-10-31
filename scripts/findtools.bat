@echo off
rem Finds what the build needs on this computer and sets it up in the caller's environment:
rem the Visual Studio C++ compiler (through vswhere, which every Visual Studio installs) and Qt.
rem Called by build.bat and package.bat.  Sets QT_ROOT.

rem ---- the compiler ----------------------------------------------------------------------------
if defined VSCMD_VER goto :have_compiler
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio was not found. Install Visual Studio 2022 or later with "Desktop development with C++".
    exit /b 1
)
set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (
    echo No Visual Studio with the C++ tools was found. Add "Desktop development with C++" in the Visual Studio Installer.
    exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:have_compiler

rem ---- Qt ------------------------------------------------------------------------------------------
if exist "%~dp0..\thirdparty\qt\bin\windeployqt.exe" (
    for %%i in ("%~dp0..\thirdparty\qt") do set "QT_ROOT=%%~fi"
    goto :have_qt
)
if defined QT_ROOT if exist "%QT_ROOT%\bin\windeployqt.exe" goto :have_qt
set "QT_ROOT="
rem The newest Qt 6 for 64-bit MSVC under C:\Qt, which is where the Qt installer puts it.
for /f "delims=" %%v in ('dir /b /ad /o:n "C:\Qt\6.*" 2^>nul') do (
    for /f "delims=" %%k in ('dir /b /ad "C:\Qt\%%v\msvc*_64" 2^>nul') do (
        if exist "C:\Qt\%%v\%%k\bin\windeployqt.exe" set "QT_ROOT=C:\Qt\%%v\%%k"
    )
)
if not defined QT_ROOT (
    echo Qt 6 was not found. Install Qt 6 for MSVC 64-bit, then tell the build where it is, for example:
    echo     set QT_ROOT=C:\Qt\6.8.3\msvc2022_64
    exit /b 1
)
:have_qt
set "CMAKE_PREFIX_PATH=%~dp0..\thirdparty;%QT_ROOT%"
exit /b 0
