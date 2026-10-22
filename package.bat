@echo off
rem Builds Eclipse (Release) and puts everything it needs to run into the Release folder:
rem the program, its resources, and the Qt and compiler runtime files.  Usage: package.bat
rem
rem The new folder is made beside the old one and takes its place only at the end, and only
rem when Eclipse is not running from Release: a folder in use is left exactly as it is.
rem
rem Needs the same things as build.bat (see there, and scripts\findtools.bat).
setlocal
cd /d "%~dp0"
call "%~dp0scripts\findtools.bat" || exit /b 1

rem Is Eclipse running from the Release folder?  (A program that is running cannot be opened for writing.)
if exist Release\Eclipse.exe (
    2>nul ( >>Release\Eclipse.exe call ) || (
        echo Eclipse is running from the Release folder. Close it and run package.bat again.
        echo Nothing in Release was changed.
        exit /b 2
    )
)

cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%QT_ROOT%" || exit /b 1
cmake --build out --target Eclipse || exit /b 1

if exist Release.new rmdir /s /q Release.new
mkdir Release.new || exit /b 1
copy /y out\Eclipse.exe Release.new\ >nul || exit /b 1
rem The libraries the program is made of: they must stay beside it.
copy /y out\EclipseModel.dll Release.new\ >nul || exit /b 1
copy /y out\EclipseAudio.dll Release.new\ >nul || exit /b 1
copy /y out\EclipseFormats.dll Release.new\ >nul || exit /b 1
copy /y out\EclipseRender.dll Release.new\ >nul || exit /b 1
copy /y out\EclipseActions.dll Release.new\ >nul || exit /b 1
copy /y out\EclipseUi.dll Release.new\ >nul || exit /b 1
xcopy /e /i /q /y resources Release.new\resources >nul || exit /b 1
"%QT_ROOT%\bin\windeployqt.exe" --release --no-translations Release.new\Eclipse.exe Release.new\EclipseUi.dll Release.new\EclipseActions.dll Release.new\EclipseFormats.dll Release.new\EclipseRender.dll Release.new\EclipseAudio.dll Release.new\EclipseModel.dll >nul || exit /b 1

if exist Release rmdir /s /q Release
if exist Release (
    echo The old Release folder could not be removed: something in it is in use.
    echo The new build is in Release.new.
    exit /b 3
)
ren Release.new Release || exit /b 1
echo Release folder ready: %~dp0Release
