# Third-party notices

Psych Importer's original code is covered by the [MIT license](LICENSE). Third-party components and upstream-derived portions retain their respective licenses and attribution. The product license does not relicense those portions.

## Engine references and adaptations

| Project | License | Scope in this distribution |
|---|---|---|
| [Codename Engine](https://github.com/CodenameCrew/CodenameEngine) | Apache 2.0 | Parser behavior adapted in FML's C++ chart reader; native export contracts and script API references. |
| [Psych Engine](https://github.com/ShadowMario/FNF-PsychEngine) | Apache 2.0 | Imported formats and Lua API semantics referenced by FML's compatibility implementation, including the isolated Developer WIP. |

The Windows package carries each engine's complete license and attribution record as `licenses/CodenameEngine-LICENSE.txt`, `licenses/CodenameEngine-NOTICE.md`, `licenses/PsychEngine-LICENSE.txt` and `licenses/PsychEngine-NOTICE.md`. The Developer/source edition keeps the same records in `PsychImporterStandalone/third_party/CodenameEngine/` and `PsychImporterStandalone/third_party/PsychEngine/`.

The notices identify source copies and adaptation scope. Modified upstream-derived files must identify their origin and changes, and retain applicable original attribution. Apache 2.0 redistribution conditions are available in the [official license, section 4](https://www.apache.org/licenses/LICENSE-2.0). This package does not include either engine runtime or imply endorsement by either project.

## Runtime and build dependencies

| Component | License shipped with this package |
|---|---|
| SDL3 | zlib |
| Dear ImGui | MIT |
| Embedded ProggyClean and ProggyForever fonts | MIT (Tristan Grimmer; Disco Hello) |
| pugixml | MIT |
| miniz | MIT |
| nlohmann/json | MIT |
| stb image, image-write and Vorbis readers | Upstream MIT/public-domain alternatives |
| miniaudio | Upstream public-domain/MIT alternatives |

Both editions include full dependency notices under `licenses/`, including the embedded default fonts in `Dear-ImGui.txt`. The source edition also retains license headers in vendored files and standalone license files where supplied. Original third-party terms and attribution are preserved; this summary does not replace them.

The UI can load Segoe UI and icon fonts from the user's Windows installation. Those Windows font files are not redistributed in either edition.

## Mod content

Imported art, music, charts, fonts and scripts keep their original ownership and permissions. No test mods, engine assets or private source folders are included in either edition. The tool and engine licenses do not grant redistribution rights to a mod's content.
