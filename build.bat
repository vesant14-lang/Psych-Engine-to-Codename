@echo off
setlocal
set "APP=%~dp0"
set "ROOT=%~dp0.."
call "%ROOT%\setup_msvc.bat" || exit /b 1
set "SDL=%ROOT%\third_party\SDL3-3.4.14"
set "IMGUI=%ROOT%\third_party\imgui"
if not exist "%APP%build" mkdir "%APP%build"
pushd "%APP%build"
set "FLAGS=/nologo /c /O1 /EHsc /std:c++17 /utf-8 /bigobj /W3 /wd4996 /wd4267 /wd4244 /I"%SDL%\include" /I"%IMGUI%""
set "CORE=PsychCharacter.obj SparrowAtlas.obj LegacyChart.obj ChartExchange.obj CodenameChart.obj SongMeta.obj JsonSplice.obj Vfs.obj SafeWrite.obj AudioImport.obj Hash.obj CodenameSongAuthor.obj pugixml.obj miniz.obj miniz_tdef.obj miniz_tinfl.obj miniz_zip.obj"
if /i "%~1"=="quick" goto :localExtensions
if /i "%~1"=="checks" goto :localExtensions
if /i "%~1"=="ui" goto :extensions
if /i "%~1"=="test-fast" goto :extensions
for %%U in (PsychCharacter SparrowAtlas LegacyChart ChartExchange CodenameChart SongMeta JsonSplice) do (
    cl %FLAGS% "%ROOT%\src\fml_formats\%%U.cpp" /Fo:%%U.obj || goto :fail
)
for %%U in (Vfs SafeWrite AudioImport) do (
    cl %FLAGS% "%ROOT%\src\fml_io\%%U.cpp" /Fo:%%U.obj || goto :fail
)
cl %FLAGS% "%ROOT%\src\fml_core\Hash.cpp" /Fo:Hash.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_adapters\CodenameSongAuthor.cpp" /Fo:CodenameSongAuthor.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_audio\AudioEngine.cpp" /Fo:AudioEngine.obj || goto :fail
cl %FLAGS% "%ROOT%\src\tools\stb_image_png_impl.cpp" /Fo:stb_image.obj || goto :fail
cl %FLAGS% /TC "%ROOT%\src\tools\stb_vorbis_impl.c" /Fo:stb_vorbis.obj || goto :fail
cl %FLAGS% "%ROOT%\third_party\pugixml.cpp" /Fo:pugixml.obj || goto :fail
for %%U in (miniz miniz_tdef miniz_tinfl miniz_zip) do (
    cl %FLAGS% /TC "%ROOT%\third_party\miniz\%%U.c" /Fo:%%U.obj || goto :fail
)
set "CORE=PsychCharacter.obj SparrowAtlas.obj LegacyChart.obj ChartExchange.obj CodenameChart.obj SongMeta.obj JsonSplice.obj Vfs.obj SafeWrite.obj AudioImport.obj Hash.obj CodenameSongAuthor.obj pugixml.obj miniz.obj miniz_tdef.obj miniz_tinfl.obj miniz_zip.obj"
:extensions
for %%U in (AnimateAtlas CodenameXml PsychStage LuaStatic LuaAst) do (
    cl %FLAGS% "%ROOT%\src\fml_formats\%%U.cpp" /Fo:%%U.obj || goto :fail
)
cl %FLAGS% "%ROOT%\src\fml_runtime\Scene.cpp" /Fo:Scene.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_adapters\SpriteResolver.cpp" /Fo:SpriteResolver.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_app\ModExplorerCatalog.cpp" /Fo:ModExplorerCatalog.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_app\UiText.cpp" /Fo:UiText.obj || goto :fail
:localExtensions
cl %FLAGS% "%ROOT%\src\fml_formats\LegacyChart.cpp" /Fo:LegacyChart.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_runtime\PreviewGameplay.cpp" /Fo:PreviewGameplay.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_runtime\Gameplay.cpp" /Fo:Gameplay.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_runtime\PreviewDirector.cpp" /Fo:PreviewDirector.obj || goto :fail
cl %FLAGS% "%ROOT%\src\fml_notelab\NoteImage.cpp" /Fo:NoteImage.obj || goto :fail
for %%U in (NoteStyle NoteStyleRead NotePreview NoteGameplay) do (
    cl %FLAGS% "%ROOT%\src\fml_notelab\%%U.cpp" /Fo:%%U.obj || goto :fail
)
cl %FLAGS% "%ROOT%\src\fml_app\chart_editor\ChartSpace.cpp" /Fo:ChartSpace.obj || goto :fail
cl %FLAGS% "%APP%src\I18n.cpp" /Fo:I18n.obj || goto :fail
cl %FLAGS% "%APP%src\Media.cpp" /Fo:Media.obj || goto :fail
set "CORE=%CORE% AnimateAtlas.obj CodenameXml.obj PsychStage.obj LuaStatic.obj LuaAst.obj Scene.obj SpriteResolver.obj ModExplorerCatalog.obj UiText.obj I18n.obj ChartSpace.obj PreviewGameplay.obj Gameplay.obj PreviewDirector.obj NoteImage.obj NoteStyle.obj NoteStyleRead.obj NotePreview.obj NoteGameplay.obj"
if /i "%~1"=="checks" goto :checks
if /i "%~1"=="test-fast" goto :checks
if /i "%~1"=="test" (
    goto :checks
)
:gui
if /i "%~1"=="quick" goto :main
for %%U in (imgui imgui_draw imgui_tables imgui_widgets imgui_impl_sdl3 imgui_impl_opengl3) do (
    cl %FLAGS% "%IMGUI%\%%U.cpp" /Fo:%%U.obj || goto :fail
)
:main
for %%U in (GlRenderer ShaderLibrary FxShaders) do (
    cl %FLAGS% "%ROOT%\src\fml_render\%%U.cpp" /Fo:%%U.obj || goto :fail
)
cl %FLAGS% "%APP%src\main.cpp" /Fo:main.obj || goto :fail
link /nologo /STACK:8388608 /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup main.obj %CORE% AudioEngine.obj GlRenderer.obj ShaderLibrary.obj FxShaders.obj stb_vorbis.obj imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj imgui_impl_sdl3.obj imgui_impl_opengl3.obj "%SDL%\lib\x64\SDL3.lib" opengl32.lib shell32.lib ole32.lib /OUT:"%APP%PsychImporter.exe" || goto :fail
if not exist "%APP%SDL3.dll" copy /y "%SDL%\lib\x64\SDL3.dll" "%APP%SDL3.dll" >nul || goto :fail
popd
echo [OK] PsychImporterStandalone\PsychImporter.exe
exit /b 0
:checks
cl %FLAGS% "%APP%tests\checks.cpp" /Fo:checks.obj || goto :fail
link /nologo /STACK:8388608 checks.obj %CORE% Media.obj stb_image.obj /OUT:PsychImporterChecks.exe || goto :fail
"%CD%\PsychImporterChecks.exe" || goto :fail
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%APP%tests\metadata-junction.ps1" -CheckExecutable "%CD%\PsychImporterChecks.exe" || goto :fail
popd
exit /b 0
:fail
popd
exit /b 1
