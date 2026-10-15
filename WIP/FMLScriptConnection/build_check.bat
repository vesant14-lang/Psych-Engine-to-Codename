@echo off
setlocal
set "WIPROOT=%~dp0"
call "%WIPROOT%..\..\..\setup_msvc.bat" || exit /b 1
if not exist "%WIPROOT%build" mkdir "%WIPROOT%build"
pushd "%WIPROOT%build"
set "FLAGS=/nologo /c /O1 /EHsc /std:c++17 /utf-8 /bigobj /W3 /wd4996 /wd4267 /wd4244"
for %%U in (LuaAst PsychApiCatalog HscriptEmit PsychBehaviorPlan PsychCompat PsychCompatScript) do (
    cl %FLAGS% "%WIPROOT%upstream\src\fml_formats\%%U.cpp" /Fo:%%U.obj || goto :fail
)
cl %FLAGS% "%WIPROOT%upstream\src\fml_core\Hash.cpp" /Fo:Hash.obj || goto :fail
cl %FLAGS% "%WIPROOT%upstream\src\fml_io\SafeWrite.cpp" /Fo:SafeWrite.obj || goto :fail
cl %FLAGS% "%WIPROOT%checks.cpp" /Fo:checks.obj || goto :fail
link /nologo /STACK:8388608 checks.obj LuaAst.obj PsychApiCatalog.obj HscriptEmit.obj PsychBehaviorPlan.obj PsychCompat.obj PsychCompatScript.obj Hash.obj SafeWrite.obj /OUT:FMLScriptConnectionChecks.exe || goto :fail
FMLScriptConnectionChecks.exe || goto :fail
popd
exit /b 0
:fail
popd
exit /b 1
