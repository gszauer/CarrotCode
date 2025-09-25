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

cl %CFLAGS% %INCLUDES% /c code\renderer.cpp /Fobuild\windows\renderer.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\strings.cpp /Fobuild\windows\strings.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\document.cpp /Fobuild\windows\document.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\imgui.cpp /Fobuild\windows\imgui.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\view.cpp /Fobuild\windows\view.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\font.cpp /Fobuild\windows\font.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\lex.cpp /Fobuild\windows\lex.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\trie.cpp /Fobuild\windows\trie.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\search.cpp /Fobuild\windows\search.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\image.cpp /Fobuild\windows\image.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\language_service.cpp /Fobuild\windows\language_service.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree_sitter.c /Fobuild\windows\tree_sitter.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-c\src\parser.c /Fobuild\windows\ts_c_parser.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-cpp\src\parser.c /Fobuild\windows\ts_cpp_parser.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-cpp\src\scanner.c /Fobuild\windows\ts_cpp_scanner.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-javascript\src\parser.c /Fobuild\windows\ts_js_parser.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-javascript\src\scanner.c /Fobuild\windows\ts_js_scanner.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-python\src\parser.c /Fobuild\windows\ts_python_parser.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-python\src\scanner.c /Fobuild\windows\ts_python_scanner.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-rust\src\parser.c /Fobuild\windows\ts_rust_parser.obj
if %errorlevel% neq 0 goto error

cl %CFLAGS% %INCLUDES% /c code\tree-sitter-rust\src\scanner.c /Fobuild\windows\ts_rust_scanner.obj
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
    build\windows\font.obj ^
    build\windows\lex.obj ^
    build\windows\trie.obj ^
    build\windows\search.obj ^
    build\windows\image.obj ^
    build\windows\language_service.obj ^
    build\windows\tree_sitter.obj ^
    build\windows\ts_c_parser.obj ^
    build\windows\ts_cpp_parser.obj ^
    build\windows\ts_cpp_scanner.obj ^
    build\windows\ts_js_parser.obj ^
    build\windows\ts_js_scanner.obj ^
    build\windows\ts_python_parser.obj ^
    build\windows\ts_python_scanner.obj ^
    build\windows\ts_rust_parser.obj ^
    build\windows\ts_rust_scanner.obj ^
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