# Development

This guide covers the source layout, Windows build, tests, WIP sources and release packaging. See [README.md](README.md) for the user workflow and [ARCHITECTURE.md](ARCHITECTURE.md) for module boundaries.

## Requirements

- Windows x64.
- Visual Studio 2022 or Build Tools with the C++ x64 compiler and Windows SDK.
- Python 3 for the optional test and packaging scripts. Building the application itself does not require Python.
- Git when preparing a source/release package from the FML working tree.

`setup_msvc.bat` discovers an installed compiler. If `cl` is already available in the environment, it uses it. SDL3 and the required third-party sources are included in the Developer edition.

## Source layout

```text
README.md                         repository entry point
LICENSE                           project license
setup_msvc.bat                    MSVC discovery
src/fml_*/                        shared modules required by the build
third_party/                     required libraries and headers
PsychImporterStandalone/
  README.md                       user guide
  DEVELOPMENT.md                  this guide
  ARCHITECTURE.md                  boundaries and data flow
  CODENAME_COMPATIBILITY.md        target contracts and validation
  build.bat                       application and regression build
  src/                            standalone frontend
  tests/                          synthetic and local-mod checks
  tools/                          packaging and WIP synchronization
  third_party/CodenameEngine/      attribution and license
  WIP/FMLScriptConnection/         isolated FML script-connection sources
```

The Developer archive contains only the active shared files opened by the compiler, their required libraries, and the explicitly isolated WIP snapshot. The snapshot has its own manifest and is not compiled into the application.

## Build the application

From the source root:

```bat
.\PsychImporterStandalone\build.bat
.\PsychImporterStandalone\build.bat checks
```

Use an explicit path to the batch file. This also works when Windows is configured not to search the current directory for executables.

| Command | Result |
|---|---|
| `build.bat` | Compile the application from source and place `PsychImporter.exe` and `SDL3.dll` in `PsychImporterStandalone/`. |
| `build.bat quick` | Rebuild the frontend and local extensions using the existing core/library objects. Requires a previous full build. |
| `build.bat test` | Compile the shared units and run the regression executable. |
| `build.bat checks` | Rebuild the local checks using existing core objects and run the regressions. Requires a previous full build. |

Run a full build after changing shared class layouts, public headers or build dependencies. Reusing an object compiled against an older layout is not a valid check of a new header.

The first public and Developer release has no gameplay HUD in Preview. `PreviewHudAvailable` is false; no control or experimental command-line option enables it. Tests of shared HUD readers do not enable that feature in the application.

## Test the change that matters

`tests/checks.cpp` creates synthetic fixtures and checks parsing, timing, difficulty audio, export, ZIP layout, partial plans, custom types, source isolation, numeric bounds and transaction behavior. `tests/metadata-junction.ps1` checks that linked transaction metadata cannot write outside the destination; the regression executable receives native Unicode arguments on Windows.

The remaining scripts can use local Psych mods and a local Codename installation. Review their configured paths first. They accept `PSYCH_IMPORTER_EXE` to test another application build.

| Check | Scope |
|---|---|
| `tests/ui_suite.py [mod ...]` | Hidden real windows: tabs, language, viewer zoom/pan, audio continuity, resource audio and sprite sheet controls. |
| `tests/export_suite.py <output> [mod ...]` | Export combinations, package references, Codename ZIP layout and base-asset overrides. `PI_SCENARIOS=mark-all,zip` narrows the scenarios. |
| `tests/validate_bundle.py <mod> <Codename assets> [json]` | Static references and format checks for a complete mod. Use `--chart-only` only for intentional chart-only artifacts. |
| `tests/engine_matrix.py <label> [mod ...]` | Exported songs run in the real engine through FML's external bridge. This helper is not included. Review its installation and cleanup paths before using it. |
| `tests/release_checks.py <release folder>` | Archive hashes/content, public runtime checks, Developer closure, clean build and documentation checks. |

An exported structure check does not establish that the game behaves correctly. Record the song, difficulty, segment and engine build when reporting an engine run. Do not label an unplayed export as engine-validated.

## WIP: general script connection

`WIP/FMLScriptConnection/` contains actual upstream FML sources for behavior plans, source bindings, dependency discovery, stored script relations, Lua-to-HScript emission and the compatibility-layer source. It is separate from `src/Modchart.hpp` and `src/LuaLite.hpp`, which implement the modchart path already used by the application.

Run the independent check from the source root:

```bat
.\PsychImporterStandalone\WIP\FMLScriptConnection\build_check.bat
```

This compiles and tests the source snapshot. It does not install generated scripts, execute a mod or enable the connection in the application. Its README describes the remaining integration work.

Refresh the snapshot deliberately from a reviewed FML tree:

```bat
python PsychImporterStandalone\tools\sync_wip.py C:\Work\FML
```

The sync tool records revision, dirty-tree status and hashes. It preserves upstream paths and refuses to overwrite locally edited snapshot files. Inspect the manifest and diff after syncing. Do not develop two diverging active copies of the same FML component. The source edition includes `.gitattributes` rules that preserve exact snapshot and original-license bytes across Git line-ending settings. Keep those rules when creating a public repository; the publication check verifies real Git staging and checkout with LF and CRLF settings.

## Prepare a release

1. Update `src/Version.hpp`, README and the compatibility information.
2. Build and run the regressions appropriate to the change. Check the UI and export paths affected. Run representative exports in Codename when export behavior changes.
3. Create an empty output folder. From the application folder, run `python tools/package_release.py <output>` in the FML working tree.
4. Run `python tests/release_checks.py <output>` against the generated archives. The default includes a clean Developer build; `--skip-build` is only for a later documentation/package check when the build has already been verified.

The packager creates:

```text
PsychImporter-<version>-Windows-x64.zip
PsychImporter-<version>-Developer.zip
SHA256SUMS.txt
manifest.json
```

It preserves existing archives instead of overwriting them. The public archive includes the executable, SDL3 DLL, English usage/compatibility documents and dependency licenses. The Developer archive is suitable as a source repository layout, including its root README, product license and complete `licenses/` directory. Dependency records include the two default fonts embedded in Dear ImGui; their copyright notices are extracted from the vendored source.

The active FML file list comes from MSVC's includes, using the same compilation units as `build.bat`. WIP is an explicitly separate source snapshot, verified against its own manifest and checked with its own batch file. It is excluded from the application's binary freshness check.

## Documentation and contribution

Markdown published in a repository or shipped in either archive is written in English. Internal design, research, audits and the development log remain in Spanish and are excluded from the publication recipe. English documentation uses English filenames and working local links. Required third-party legal notices retain their original terms and attribution.

For a change, explain the concrete problem, resulting behavior, relevant tests and remaining limitations. Reproduce format problems without hardcoding behavior for a particular mod. Keep common fixes in FML and synchronize them with the standalone; keep frontend layout and preferences local to this application.

## Licensing and source attribution

Preserve original third-party licenses and notices. Codename Engine and Psych Engine use Apache 2.0; original FML code and upstream-derived portions have distinct licensing scope. Record the source and changes when adapting upstream code. See the distribution's `THIRD_PARTY_NOTICES.md` and engine-specific records under `third_party/`; publishing WIP does not remove these requirements.
