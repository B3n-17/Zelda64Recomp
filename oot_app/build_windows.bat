@echo off
REM Windows build for the Ocarina of Time milestone target.
REM
REM Run from a normal cmd prompt, or from WSL via:
REM   cmd.exe /c "D:\gmz\mmrecomp\Zelda64Recomp\oot_app\build_windows.bat"
REM
REM clang-cl is required, not optional: the patch override mechanism depends on
REM Clang emitting RECOMP_FUNC as a weak symbol. See the note in CMakeLists.txt.

setlocal

set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"
set "REPO=%~dp0.."

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "CLANGCL=%VSDIR%\VC\Tools\Llvm\x64\bin\clang-cl.exe"

"%CMAKE%" -S "%REPO%\oot_app" -B "%REPO%\build-oot-win" -G Ninja ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER="%CLANGCL%" ^
    -DCMAKE_CXX_COMPILER="%CLANGCL%" || exit /b 1

"%CMAKE%" --build "%REPO%\build-oot-win" -j 4 || exit /b 1

echo.
echo Built: %REPO%\build-oot-win\OoTRecompiled.exe
endlocal
