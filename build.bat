@echo off
rem Configures and builds Eclipse.  Usage: build.bat
rem
rem Needs: Visual Studio 2022 or later with the C++ tools, CMake, Ninja, and Qt 6 (MSVC 64-bit).
rem Qt is looked for in the folder named by QT_ROOT; set it if Qt is not under C:\Qt, for example
rem     set QT_ROOT=D:\Qt\6.8.3\msvc2022_64
setlocal
cd /d "%~dp0"
call "%~dp0scripts\findtools.bat" || exit /b 1

cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%QT_ROOT%" || exit /b 1
cmake --build out || exit /b 1
rem Copy the Qt runtime next to the program the first time.
rem (The libraries of Eclipse are looked at too: what only they use of Qt is needed as well.)
if not exist out\Qt6Qml.dll "%QT_ROOT%\bin\windeployqt.exe" --no-translations --no-compiler-runtime out\Eclipse.exe out\EclipseUi.dll out\EclipseActions.dll out\EclipseAudio.dll out\EclipseModel.dll out\EclipseColour.dll >nul
echo Built: %~dp0out\Eclipse.exe
