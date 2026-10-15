# Architecture

Psych Importer has a small standalone frontend over shared Funkin Mod Lab modules. The Developer/source edition retains those modules' relative paths so the same code can build without the FML application.

## Data flow

```text
Psych mod or loose files
        |
        v
Catalog and source resolution
        |
        v
FML format readers -> universal chart / character / stage data
        |                          |
        v                          v
Inspection views               Export plan
and song playback                 |
                                  v
                         Report and validation
                                  |
                                  v
                         Folder / ZIP transaction
```

## One chart draft

The readers produce FML's universal chart representation; the Codename writer serializes it. Charts and Preview use that draft rather than independently interpreting the original chart. The read-only chart view describes the data intended for export.

Preview composition is separate from export bindings. Trying another stage or character in Preview does not rewrite the exported chart. Export assignments belong to Charts and the export options.

## Module boundaries

| Area | Responsibility |
|---|---|
| `src/Catalog.hpp`, `Loose.hpp` | Catalog identity, redundant discovery, loose-file classification and song grouping. |
| `src/Import.hpp`, `Batch.hpp` | Resource plans, dependency selection, chart bindings, reports, partial plans and ZIP/folder output. |
| `src/Gameplay.hpp`, `GameplayUi.hpp`, `Playback.hpp` | Preview session, scene composition and shared song playback. |
| `src/Resources*.hpp` | File users, resource inspection, allowed loose selections and readiness. |
| `src/Modchart.hpp`, `LuaLite.hpp` | Collection and conversion of supported modchart actions. |
| FML `fml_formats` | Psych readers, Codename writers, atlas readers and format validation. |
| FML `fml_runtime`, `fml_render`, `fml_audio` | Scene animation, gameplay state, rendering, texture management and audio. |
| FML `fml_io` | Source protection, validated writes, transaction history and rollback. |
| `WIP/FMLScriptConnection/` | An isolated upstream source snapshot for developing the general script connection. Excluded from the normal build. |

## Stable keys and difficulty audio

Codename resolves `strumLines[].characters` and `stage` by key. A binding associates a loaded resource with that key. A plan rejects different resources assigned to one exported key across difficulties.

Audio resolution is stored per difficulty. Alternate instrumentals and vocals are resolved together; an unrelated normal vocal track is not added to an alternate instrumental by assumption. Chart metadata reflects the audio actually selected for export.

## Reviewed writes

Plans identify each output and its source before writing. Confirmation rechecks relevant source and destination conditions. Updating Freeplay retains entries added since the report was opened. A late failure rolls back the accepted operation.

Partial export prepares a valid subset before the write transaction. Recognized omissions remove whole resource units. Unknown blockers stay blocking, and an empty resource selection cannot create an empty mod.

## Events and scripts

Events with verified equivalents are converted. Unresolved Psych events preserve their provenance and serialize as inactive `Unknown` entries. Original scripts can be archived as references without becoming executable output.

The modchart collector handles a narrower contract than general behavior porting. The WIP source snapshot contains FML's broader dependency, behavior-plan, Lua-to-HScript and compatibility-layer work. Its presence does not activate that path in Psych Importer.

## Development policy

Fix shared format, atlas, scene or I/O behavior in the FML core and bring the validated change back into the standalone. Avoid permanent private copies of active core code. WIP snapshots are explicitly identified, hashed and reviewed before synchronization.

Markdown shipped in either edition or used in the public repository is written in English. Internal design documents, research and the development log remain in Spanish and are excluded from release/source publication.
