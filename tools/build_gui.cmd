@echo off
rem Build the GUI patcher for distribution.
rem Output: dist\San14CE_MiniPatch\ (San14CE_MiniPatch.exe + _internal\) and dist\San14CE_MiniPatch_v<PATCHER_VERSION>.zip
rem         (zip top folder: San14CE_MiniPatch_v<PATCHER_VERSION>\)
rem Builds in out\pyinstaller\dist first, so the zip is made even while dist\San14CE_MiniPatch is in use (patcher open);
rem dist\San14CE_MiniPatch is then refreshed only if it is not locked.
rem Setup once: python -m venv .venv-build  then  .venv-build\Scripts\python -m pip install pyinstaller
setlocal
set ROOT=%~dp0..
set PY=%ROOT%\.venv-build\Scripts\python.exe
if not exist "%PY%" (echo .venv-build not found. Run the setup commands above first. & exit /b 1)
call "%ROOT%\src\dll\build.cmd" || exit /b 1
pushd "%ROOT%"
"%PY%" -m PyInstaller --noconfirm --clean --log-level WARN --windowed --onedir --uac-admin ^
  --name San14CE_MiniPatch --distpath out\pyinstaller\dist --workpath out\pyinstaller --specpath out\pyinstaller ^
  --add-data "%ROOT%\patcher\known_files.json;patcher" ^
  --add-binary "%ROOT%\out\dll\dinput8.dll;." ^
  tools\gui_main.py || (popd & exit /b 1)
"%PY%" -c "from patcher.versions import PATCHER_VERSION as v; print(v)" > out\pyinstaller\version.txt || (popd & exit /b 1)
set /p VER=<out\pyinstaller\version.txt
if not defined VER (popd & echo version not found & exit /b 1)
rem Only this version's zip is replaced; zips of other versions in dist are kept
rem The zip holds one top folder named with the version, so "extract here" stays tidy
set NAME=San14CE_MiniPatch_v%VER%
set ZIP=dist\%NAME%.zip
set STAGE=out\pyinstaller\stage
if exist %ZIP% del %ZIP%
if exist %STAGE% rmdir /s /q %STAGE%
robocopy out\pyinstaller\dist\San14CE_MiniPatch %STAGE%\%NAME% /e /nfl /ndl /njh /njs /nc /ns /np >nul
if errorlevel 8 (popd & echo stage copy failed & exit /b 1)
powershell -NoProfile -Command "Compress-Archive -Path %STAGE%\%NAME% -DestinationPath %ZIP%" || (popd & exit /b 1)
rmdir /s /q %STAGE%
set COPY=dist\San14CE_MiniPatch\San14CE_MiniPatch.exe
robocopy out\pyinstaller\dist\San14CE_MiniPatch dist\San14CE_MiniPatch /mir /r:0 /w:0 /nfl /ndl /njh /njs /nc /ns /np >nul
if errorlevel 8 set COPY=dist\San14CE_MiniPatch not updated (in use, close the patcher) - fresh copy in out\pyinstaller\dist
popd
echo built: %ZIP%, %COPY%
