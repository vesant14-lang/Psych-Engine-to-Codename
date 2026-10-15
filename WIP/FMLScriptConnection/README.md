# FML script connection

This is source material for developing the general script connection in Psych Importer. It contains an identified snapshot of the FML implementation, its local include dependencies, the HScript compatibility-layer source and its embedding tool.

The application does not compile or activate this directory. Reading these sources or running the independent C++ check does not install or run generated mod scripts.

## Included components

| Upstream component | Responsibility |
|---|---|
| `PsychBehaviorPlan` | Decide which behavior files can be preserved, connected, approximated or blocked, and assign native Codename destinations. |
| `PsychScriptDependencies` | Find literal script dependencies and callback references without claiming every runtime path was reached. |
| `PsychScriptLibrary`, `PsychScriptRelations` | Stored originals, source identities and relationships between selected and archived scripts. |
| `PsychSourceBinding` | Associate a mod with its verified source file/cache. |
| `PsychBehaviorSource`, `PsychBundle` | Classify behavior by document type and engine scope. |
| `LuaAst`, `HscriptEmit` | Parse Lua and emit supported HScript with hooks and explicit pending calls. |
| `PsychCompat`, `PsychCompatScript` | Emit and inspect the versioned compatibility layer, preserving user-edited installed copies. |
| `psych-compat/fml-psych-compat.hx` | Editable compatibility-layer source, mirrored by the generated C++ string. |

`upstream/` preserves FML's relative paths. `SOURCE_MANIFEST.json` records the source revision, whether its working tree was dirty, and each captured file's size and SHA-256. The snapshot is identified source material, not a second active core for the application.

## Run the independent check

From the source root:

```bat
.\PsychImporterStandalone\WIP\FMLScriptConnection\build_check.bat
```

It uses the repository's MSVC setup and builds inside this directory's ignored `build/` folder. It checks dependency discovery, inactive versus active plans, malformed Lua, unresolved calls, preservation of unrelated globals and compatibility-layer integrity. It only evaluates the planning/emission code; it does not execute emitted HScript or start an engine.

To edit the compatibility layer, modify `upstream/src/fml_formats/psych-compat/fml-psych-compat.hx` and regenerate its embedded C++ from the snapshot root:

```bat
python tools\embed_compat.py
```

The independent check compares the embedded body with the HScript source. An intentional local edit will make the snapshot differ from its recorded upstream manifest; keep that diff for review instead of presenting it as unchanged upstream code.

## Integration still required

1. Expose a reviewed behavior plan in the standalone without mixing source references with executable output.
2. Connect it to the existing export transaction, source identities and chart dependency choices.
3. Preserve user-declined connections, edited destination scripts, partial plans and cancellation behavior.
4. Report unsupported calls and approximations with source locations.
5. Test representative output in the selected Codename build before exposing the feature as supported.

The upstream compatibility layer can emit executable behavior. It belongs behind an explicit user choice in any future integration. Its presence here is not a promise that arbitrary Psych Lua or HScript will run unchanged.

## Refresh from FML

Use `tools/sync_wip.py` from the application source tree with an explicit FML root. The tool refuses to overwrite edits that differ from the previous snapshot. Review the source manifest and diff after updating it. Build fixes shared with FML belong in FML first; keep this directory isolated until the integration has its own validated contract.

## Licensing and source attribution

Preserve original third-party licenses and notices. Codename Engine and Psych Engine use Apache 2.0; original FML code and upstream-derived portions have distinct licensing scope. Record the source and changes when adapting upstream code. See the distribution's `THIRD_PARTY_NOTICES.md` and engine-specific records under `third_party/`; publishing WIP does not remove these requirements.
