# Psych Engine

Psych Engine is developed by ShadowMario and contributors and is licensed under Apache License 2.0. The accompanying `LICENSE` is an unchanged copy of the reviewed upstream license.

Upstream: [ShadowMario/FNF-PsychEngine](https://github.com/ShadowMario/FNF-PsychEngine). Psych Importer reads Psych formats; it does not ship or run the Psych Engine runtime.

FML's `PsychApiCatalog.cpp`, `HscriptEmit.cpp` and `psych-compat/fml-psych-compat.hx` reference Psych's Lua API contracts. They map supported calls, callbacks and data to Codename, and report unresolved behavior. These are FML compatibility implementations, not an unmodified copy of the engine. Upstream-derived material retains its Apache 2.0 terms; the tool's license does not replace those terms. Compatibility with an API does not assign the authorship of FML's original implementation to the upstream project.

Those general script-connection sources are supplied only as an isolated Developer WIP snapshot. They are not enabled in the application. The normal importer reads Psych chart, character, stage and resource data through the shared FML core.

The source references below identify the local upstream copies reviewed on October 15, 2026. The folder labels identify those copies, not a promise to support every release or fork. No engine source files from this table are redistributed in this package.

| Copy | File | SHA-256 |
|---|---|---|
| main | `source/psychlua/FunkinLua.hx` | `3f0b4a2ac9d89b3aebb9722bc973269657e867c23ed8c5c0c73a19d3c5486956` |
| main | `source/psychlua/LuaUtils.hx` | `96cb370f5ce8ebbda7f0b8b80dd8c14f19921245527aeaab857bb953cf9a6625` |
| main | `source/psychlua/ReflectionFunctions.hx` | `3ac9f3dd62945fba77d58c418e003e2213727913fd71d2d2bc74aea2765de1a5` |
| main | `source/psychlua/TextFunctions.hx` | `7d98b7d022e2aaf354da55c80c5484e50d43277da0cad1a88f44441d3352136a` |
| main | `source/psychlua/ShaderFunctions.hx` | `23e538a2768ec2b5f9bd07e8d7304cdbec8e09b4b47228bd1813c2aa47fe3541` |
| main | `source/psychlua/ExtraFunctions.hx` | `72c9b65ef2a6b49138587f4cfc05280c7f4bdc0b9e79c6812d845393cb09ad97` |
| main | `source/psychlua/DeprecatedFunctions.hx` | `6657e89fc2a63c983bab3d4d130add1acb65edc772e6575e6f19024d2657b5ed` |
| main | `source/psychlua/FlxAnimateFunctions.hx` | `98817db6d192c3c54d9a168a1e6f8ab909442c4758324a24fccc741dbbacfe36` |
| 0.7 | `source/psychlua/FunkinLua.hx` | `06ca6aaafc7c16e6740366a2e6d01eaec2cb6f655cee117687aeeafa7ba0ddf6` |

The reviewed main and 0.7 source roots did not contain a separate upstream NOTICE file. This document is Psych Importer's attribution record, not a replacement for an upstream NOTICE. Any applicable upstream copyright, license and attribution notices must be retained when importing source in future changes.

Engine licensing does not grant rights to third-party mod art, audio, charts or scripts. Those files keep their own ownership and permissions.
