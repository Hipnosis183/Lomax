@echo off
rem Builds LOADER.EXE + LOADER.DLL (32-bit) into build\.
rem Copy both into the game folder to use them.
setlocal enabledelayedexpansion
if "%MSYS2%"=="" set MSYS2=C:\msys64
if "%MINHOOK%"=="" set MINHOOK=%~dp0minhook
set PATH=%MSYS2%\mingw32\bin;%PATH%
set OUT=%~dp0build
if not exist "%OUT%" mkdir "%OUT%"
set CFLAGS=-m32 -O2 -Wall -Wextra -Wno-cast-function-type -Wno-format-truncation -static-libgcc -s

rem the DLL: every module in dll\
set DLLSRC=
for %%f in ("%~dp0dll\*.c") do set DLLSRC=!DLLSRC! "%%f"

gcc %CFLAGS% -shared -o "%OUT%\LOADER.DLL" !DLLSRC! -I"%MINHOOK%" -I"%~dp0dll" "%MINHOOK%\libminhook.a" -lgdi32 -luser32 -lwinmm -lole32 -lmfuuid -Wl,--kill-at || exit /b 1
gcc %CFLAGS% -mwindows -o "%OUT%\LOADER.EXE" "%~dp0loader.c" || exit /b 1
echo Built "%OUT%\LOADER.EXE" and "%OUT%\LOADER.DLL"
