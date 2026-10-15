# Psych Importer

Bring characters, songs, stages and icons from Psych Engine into Codename Engine. Inspect what was found, choose what to export, and review the conversion report before creating a folder or ZIP.

Psych Importer is a standalone tool from Funkin Mod Lab (FML). It shares FML's format readers, scene rendering, atlas support and audio code. You can use the Windows build without installing FML, Psych Engine or a separate runtime.

Version 1.0.1 | Windows 10 / 11, x64 | English and Spanish interface | MIT license

## Start here

| You want to… | Use |
|---|---|
| Convert assets on Windows | `PsychImporter-1.0.1-Windows-x64.zip` |
| Build or modify the program | `PsychImporter-1.0.1-Developer.zip` or the source repository |
| Check the target format and tested versions | [Codename compatibility](PsychImporterStandalone/CODENAME_COMPATIBILITY.md) |
| Work on the code | `DEVELOPMENT.md` in the source edition |
| Explore the unfinished FML script connection | `WIP/FMLScriptConnection/` in the source edition |

Extract the Windows ZIP to a folder and open `PsychImporter.exe`. Keep `SDL3.dll` beside it. Extracting the archive also gives the program a persistent place for its logs.

The interface starts in English. Use ES or EN in the top bar to change language; the choice is saved. The documentation uses the English button names.

## Your first export

1. Choose Open mod and select a Psych mod folder. You can also open the full game's folder, choose Loose files, or drag files and folders into the window.
2. Choose a destination. Selecting Codename's `mods` folder lets the importer inspect that installation's base assets. For another destination, Base lets you choose a Codename installation to read from.
3. Mark the characters, songs, stages or icons you want. A song can include its characters and stage, or keep those dependencies outside the package.
4. Choose Export and read the plan. Resolve blockers, review items that may affect the game, then confirm the export.
5. Install the result using its `HOW-TO-INSTALL.txt` guide and test it in Codename. With the Spanish interface, the generated guide is named `COMO-INSTALAR.txt`.

Opening and previewing a source does not modify it. Export protects the source folder, preserves existing destination files, and cancels if reviewed source or destination files change during the operation.

## What you can inspect

| Tab | What it does |
|---|---|
| Characters | Animation playback, offsets, scale, looping, health icons and a separate sprite sheet view. Supports Sparrow, Packer and supported Adobe Animate atlases. |
| Songs | Difficulties, chart details and the audio associated with each difficulty: instrumental, combined voices or separate vocal tracks. |
| Stages | Recovered scenery, layers, objects and animations, with optional characters for inspection. |
| Icons | Normal, losing, winning and additional states when present. Export a whole icon or an individual state as PNG. |
| Charts | The Codename chart draft used for export, including strumlines, sustains, waveform and note details. The chart view is read-only in this release. |
| Preview | Song-driven scene playback with autoplay, seeking, loop, speed, volume and camera controls. The first public and Developer release has no gameplay HUD or HUD activation option. |
| Resources | Discovered files, their users, audio playback, image/atlas inspection, manual selection of supported loose resources and export readiness. |

Viewers support the mouse wheel for zoom, left or middle button drag for pan, and Center to reset the view. Sprite sheets have their own zoom and pan. Songs, Charts and Preview share the selected song's playback position when switching between them.

Mark the whole mod is available in Characters, Songs, Stages, Icons, Charts and Resources. It marks exportable entries from the same lists you can mark manually. It leaves out non-exportable entries and base-game assets already provided by Codename. The status bar explains skipped entries. The action is absent from Preview.

The action does not select executables, scripts, internal files or miscellaneous loose resources. Select supported extra images, music and effects manually in Resources when you want them in the output. A song's required assets are handled through its export options.

## Full mods and loose files

Use a full mod folder when possible: it gives the importer more information about charts, character definitions, stages, icons and audio associations. Resource discovery searches beyond the usual folder layout, while preserving the source of each result.

Loose files can be imported together. The catalog identifies supported resources and groups charts and audio by song when there is enough information. Review ambiguous associations before exporting.

An instrumental and voices without a chart can be auditioned and associated with a difficulty. They do not create a playable chart automatically. A partial preview can use the stage and characters you select even when some of a song's assets are missing.

### Audio belongs to a difficulty

Each difficulty has its own resolved audio. Alternate versions such as `legacy` can use a different instrumental and different voices from `hard`, even within the same song. Inspect the paths in Songs rather than assuming every difficulty shares its audio.

An alternate instrumental does not silently inherit unrelated normal vocals. Missing or rejected voices are reflected in the exported chart and metadata.

## Choose how to package the result

| Output | Result |
|---|---|
| One folder | A single Codename mod containing the selected resources. |
| Separate folders | A mod per selected resource, grouped into category folders. Install each resulting mod individually. |
| One ZIP | A Codename ZIP mod with its contents at the archive root. Place it in `mods/` without extracting it. |
| ZIP of separate folders | A collection of individual mod folders. Extract it and install the individual mods. |
| Charts only | Charts and difficulty metadata, optionally with selected character/stage dependencies. Supply compatible song audio separately. |

Separate folders cannot be exported directly into Codename's `mods` folder: category folders would hide the actual mods one level below the location Codename scans. Use a separate output folder and follow the generated guide.

### Read the report before confirming

- Blockers prevent the package from being generated.
- May affect the game covers missing resources, substitutions, events without equivalents and behavior that needs review.
- Informational notes describe reference copies and completed conversions. Expand them when needed; functional warnings stay visible.

Export without blocked items can omit supported units that cannot be exported. The plan lists the omissions first. Unknown or unresolved blockers remain blocking. A character, stage or song is omitted as a unit rather than being left half-written.

Writing the accepted plan is transactional: a late failure rolls the operation back. Updating an existing Freeplay list retains its current entries. Every export also records its complete manifest under `.psych-importer/` in the destination mod.

### Keep chart keys flexible

A Codename chart declares keys such as `matt`, `arena` or a custom slot name. In Charts, choose which loaded character or stage supplies each key, and whether to include it in the package.

For example, a chart can keep the key `matt-slot` while the selected asset supplies that slot. You can later replace the resource under that key in the game without editing the chart. If multiple difficulties use the same key, they must agree on the resource exported under it; conflicting assignments block the plan.

When exporting songs without their characters or stage, keep the declared keys for the destination game to provide, or use Fit chart to substitute assets available in the selected Codename base. The report records what changes and what remains missing.

## Preview workflow

Choose a song from Preview's song list and select its difficulty. Playback uses that song's resolved audio.

When switching songs, choose All song assets to load its composition, or Chart and music only to keep your current stage, characters and playback settings. Scene and character choices in Preview are inspection settings; export assignments remain in Charts and the export options.

Game camera follows the singing characters with the stage zoom and supported camera events. Fit scene offers a broader inspection view when game camera is disabled. The optional camera frame shows the game's 1280 × 720 view inside the preview canvas.

The preview uses FML's scene and gameplay code. It is an inspection aid; final timing, custom mechanics and converted behavior still need testing in Codename.

## Events, custom notes and modcharts

Events with known Codename equivalents are converted. Events without an equivalent retain their original name and parameters in the chart as inactive `Unknown` data and appear in `events-report.json`. They do not become active just because their name matches a native Codename event. Native events generated from the chart's structure remain active.

Custom note types retain their names. Their original scripts can accompany a song as references under `psych-scripts/`; archiving them does not activate their gameplay behavior.

Supported modchart data is handled separately:

- ModchartLoader XML timelines can provide shader actions and their shader files.
- Supported Psych Lua calls are replayed by the importer's bounded interpreter to collect receptor, camera, shader and scroll actions.
- Converted song modcharts are exported as `songs/<song>/scripts/psych-modchart.hx`, with their required shaders. Convert modcharts can be disabled in the export options.

Compiled behavior inside a mod's executable, arbitrary HScript ports, unsupported Lua operations, sprite-driven effects and custom mechanics are outside automatic conversion. Review the report for each song. The general FML script connection in WIP is source material for development and is not enabled in this release.

## Command line

Export a song without opening the window:

```bat
PsychImporter.exe --root="C:\Mods\MyPsychMod" --export-batch=my-import --songs=my-song --destination="C:\Exports" --base="C:\Games\Codename" --report="C:\Exports\report.txt"
```

`--destination` must name an existing folder. `--export-batch` names the new mod or ZIP; the importer preserves an existing output with that name.

| Option | Purpose |
|---|---|
| `--songs=a,b`, `--characters=a,b`, `--stages=a,b`, `--icons=a,b` | Select resources by identifier. `*` selects all entries in a category. |
| `--mark-all` | Use the same selection rules as Mark the whole mod. |
| `--charts=song:difficulty` | Export a chart without turning it into a full song package. |
| `--chart-characters`, `--chart-stages` | Include dependencies of selected charts. |
| `--base=<Codename folder>` | Read the target game's available base resources. |
| `--zip`, `--separate` | Choose ZIP output or separate mod folders. |
| `--partial` | Prepare the supported remainder after omitting resolvable blocked units. |
| `--no-characters`, `--no-stages` | Exclude those dependencies from song exports. |
| `--fit-chart` | Adjust missing chart dependencies to resources available in the target base. |
| `--resources=all` | Explicitly include supported loose music, effects and images. |
| `--no-modcharts` | Disable modchart conversion. |
| `--lang=en`, `--lang=es` | Choose the interface and generated guide language. |
| `--version` | Print the version. |
| `--readiness=<file>` | Write resource readiness as JSON. |
| `--modchart-dump=<song>` | Inspect the collected modchart data. |
| `--logs=<folder>` | Write logs to another folder. |

If several sources share an identifier, `id@1` selects the second catalog entry. A selection error stops the CLI export before writing. A failed write or ZIP creation returns a nonzero exit code.

## Compatibility and current limits

Codename Engine 1.0.1 is the reference target. See [CODENAME_COMPATIBILITY.md](PsychImporterStandalone/CODENAME_COMPATIBILITY.md) for the tested contracts, engine checks and limits of that validation.

- Charts is read-only in 1.0.1.
- Preview has no gameplay HUD in either edition.
- Assets or behavior compiled into a Psych executable cannot be recovered as full source assets.
- Character Animate atlases support up to eight pages.
- Workspace assignments and preview composition are session settings; a complete saved project is not available.
- Conversion reports can require manual work for events, scripts, stage details and custom note behavior.

Defensive limits cover resource counts, image sizes, chart timing, strumlines, keys, sustained-note geometry, Animate composition and export size. A limit violation is reported for review rather than silently emitted as valid content.

## Troubleshooting and useful bug reports

Start with the conversion report and `logs/psych-importer.log` beside the executable. If that folder cannot be written, logs go to `%LOCALAPPDATA%\PsychImporter\logs`. Logs rotate at 2 MB and retain five previous files.

For a useful issue, include the importer version, Windows version, target Codename version, exact steps, selected song/difficulty or resource, the conversion report, and the relevant log. A small reproducible sample helps more than an entire mod. Share only files you are allowed to redistribute.

## Build and contribute

Use the Developer/source edition with Visual Studio 2022 or its C++ Build Tools for x64. From the source root:

```bat
.\PsychImporterStandalone\build.bat
.\PsychImporterStandalone\build.bat checks
```

The build finds MSVC through `setup_msvc.bat`. The source edition includes the required FML files and third-party dependencies. See `PsychImporterStandalone/DEVELOPMENT.md` for the build modes, tests and release workflow, and `PsychImporterStandalone/ARCHITECTURE.md` for module boundaries.

Keep format and rendering fixes in the shared FML core. Keep the standalone's layout and preferences in its frontend. Include a reproducible case, the validation performed and any remaining behavior differences when proposing a change.

Read the [contribution guide](PsychImporterStandalone/CONTRIBUTING.md), [release history](PsychImporterStandalone/CHANGELOG.md) and [issue-reporting notes](PsychImporterStandalone/SECURITY.md) for the repository workflow.

## License and acknowledgements

The tool's original code is available under the [MIT license](PsychImporterStandalone/LICENSE). Codename Engine and Psych Engine use Apache 2.0; upstream-derived portions retain those terms and attribution. The MIT license does not replace their licenses. Read [third-party notices](PsychImporterStandalone/THIRD_PARTY_NOTICES.md) for the adaptation scope and included license records.

Psych Importer also uses SDL3, Dear ImGui, pugixml, miniz, nlohmann/json, stb and miniaudio. Both editions include the relevant licenses and attribution. Imported mod assets keep their original ownership and permissions.
