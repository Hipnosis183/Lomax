@echo off
rem Builds FLINK.DLL (32-bit) from dll\*.c into build\, with flink.ini as FLINK.INI. Copy both into MODS\FLINK in the game folder.
rem build.bat -extractor builds only FLINK.EXE (32-bit console program) from extractor\*.c into build\. It takes no
rem parameters: it goes into the game's MODS\FLINK with FLINK.DLL and the Flink disc image (.cue and .bin), and writes
rem the mod's data into that folder.
setlocal enabledelayedexpansion
if "%MSYS2%"=="" set MSYS2=C:\msys64
set PATH=%MSYS2%\mingw32\bin;%PATH%
set OUT=%~dp0build
set EXTRACTOR=
if /i "%~1"=="-extractor" (set EXTRACTOR=1) else if not "%~1"=="" (echo Usage: build.bat [-extractor] & exit /b 1)
if not exist "%OUT%" mkdir "%OUT%"
set CFLAGS=-m32 -fno-asynchronous-unwind-tables -fno-unwind-tables -ffunction-sections -fno-ident -Wl,--gc-sections -Wl,--build-id=none -Wall -Wextra -Wno-format-truncation -static-libgcc -s

if defined EXTRACTOR goto extractor

rem the DLL: every file in dll\
set DLLSRC=
for %%f in ("%~dp0dll\*.c") do set DLLSRC=!DLLSRC! "%%f"
gcc %CFLAGS% -Os -Wl,--exclude-all-symbols -shared -o "%OUT%\FLINK.DLL" !DLLSRC! || exit /b 1
copy /y "%~dp0flink.ini" "%OUT%\FLINK.INI" >nul || exit /b 1
echo Built "%OUT%\FLINK.DLL" and "%OUT%\FLINK.INI"
exit /b 0

:extractor
rem the extractor: every file in extractor\ (its output depends on -fexcess-precision=standard, see README "Extractor")
set EXTSRC=
for %%f in ("%~dp0extractor\*.c") do set EXTSRC=!EXTSRC! "%%f"
gcc %CFLAGS% -O2 -fexcess-precision=standard -o "%OUT%\FLINK.EXE" !EXTSRC! || exit /b 1
echo Built "%OUT%\FLINK.EXE"
