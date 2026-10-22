@echo off
rem san14patch proxy DLL build. Needs Visual Studio 2022+ (MSVC x64). Output: out\dll\dinput8.dll
setlocal
if not defined VS for %%v in (18 2022) do for %%e in (Community Professional Enterprise BuildTools) do if exist "%ProgramFiles%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat" if not defined VS set "VS=%ProgramFiles%\Microsoft Visual Studio\%%v\%%e"
if not defined VS (echo Visual Studio not found. Set VS to its install folder. & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set SRC=%~dp0
set OUT=%~dp0..\..\out\dll
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /MT /W4 /WX /utf-8 /GS- /LD /Fo"%OUT%\\" /Fe"%OUT%\dinput8.dll" ^
  "%SRC%main.c" "%SRC%support.c" "%SRC%feat_jeodol.c" "%SRC%feat_autosearch.c" "%SRC%feat_autosearch_ui.c" "%SRC%feat_tooltip.c" "%SRC%feat_expert.c" "%SRC%feat_post.c" "%SRC%feat_train.c" "%SRC%feat_logs.c" "%SRC%feat_logwnd.c" "%SRC%feat_develop.c" ^
  /link /DEF:"%SRC%dinput8.def" /NOLOGO kernel32.lib user32.lib || exit /b 1
echo built: %OUT%\dinput8.dll
