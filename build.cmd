@echo off
setlocal

set "BRANCH_NAME=%~1"
set "EMULATORS=%~2"

if "%BRANCH_NAME%"=="" set "BRANCH_NAME=local"

docker run --rm ^
  -v "%cd%":/src ^
  -w /src ^
  -e BRANCH_NAME="%BRANCH_NAME%" ^
  -e EMULATORS="%EMULATORS%" ^
  emscripten/emsdk:3.1.45 ^
  bash -lc "bash webassembly/build.sh"

if errorlevel 1 (
  exit /b 1
)
