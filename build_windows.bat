@echo off
setlocal

rem Build script for Carrot Code on Windows
rem This script builds a release version of the application

echo Building Carrot Code for Windows...

rem Check for Visual Studio compiler
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Visual Studio compiler not found in PATH
    echo Please run this script from a Visual Studio Developer Command Prompt
    exit /b 1
)

rem Create build directory if it doesn't exist
if not exist build mkdir build
if not exist build\windows mkdir build\windows

rem Set compiler flags
set CFLAGS=/O2 /MT /W3 /nologo /EHsc /DNDEBUG /DCARROT_WINDOWS /D_CRT_SECURE_NO_WARNINGS
set INCLUDES=/I code
set LIBS=user32.lib gdi32.lib shell32.lib comdlg32.lib ole32.lib

rem Compile source files
echo Compiling...
cl %CFLAGS% %INCLUDES% /c code\windows.cpp /Fobuild\windows\windows.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\application.cpp /Fobuild\windows\application.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\software_renderer.cpp /Fobuild\windows\renderer.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\strings.cpp /Fobuild\windows\strings.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\document.cpp /Fobuild\windows\document.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\imgui.cpp /Fobuild\windows\imgui.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\view.cpp /Fobuild\windows\view.obj
if %errorlevel% neq 0 goto error

echo Linking...
link /NOLOGO /SUBSYSTEM:WINDOWS /OUT:build\windows\CarrotCode.exe ^
    build\windows\windows.obj ^
    build\windows\application.obj ^
    build\windows\renderer.obj ^
    build\windows\strings.obj ^
    build\windows\document.obj ^
    build\windows\imgui.obj ^
    build\windows\view.obj ^
    %LIBS%

if %errorlevel% neq 0 goto error

echo.
echo Build successful!
echo Executable: build\windows\CarrotCode.exe
goto end

:error
echo.
echo Build failed!
exit /b 1

:end
endlocal