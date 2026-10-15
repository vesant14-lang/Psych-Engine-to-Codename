# Codename Engine

Codename Engine is developed by CodenameCrew and contributors and is licensed under Apache 2.0. The accompanying `LICENSE` file reproduces that license without changes.

Psych Importer does not run the Codename Engine runtime. The following upstream parsers were consulted and their behavior adapted to FML's C++ readers so exported data follows the native chart contracts:

- [PsychParser.hx](https://github.com/CodenameCrew/CodenameEngine/blob/main/source/funkin/backend/chart/PsychParser.hx): Psych 1.0 detection and lane ordering.
- [FNFLegacyParser.hx](https://github.com/CodenameCrew/CodenameEngine/blob/main/source/funkin/backend/chart/FNFLegacyParser.hx): section ownership, note types, BPM, `sectionBeats` and empty sections.
- [Chart.hx](https://github.com/CodenameCrew/CodenameEngine/blob/main/source/funkin/backend/chart/Chart.hx): format detection, difficulty metadata and resource-key resolution.

The adapted behavior is in FML's `src/fml_formats/LegacyChart.cpp`, including section duration, meter and numeric/packed note-type indices, with `Default Note` and repeated entries handled. FML also accepts some fields, such as `keyCount`, that are not present in every Psych fork.

SHA-256 hashes of the source copies consulted on October 11, 2026:

| File | SHA-256 |
|---|---|
| PsychParser.hx | `5A9C2AF4256E82915060831933B0B5ACFC2408BA4DEE8C943E8894B05CFB75BE` |
| FNFLegacyParser.hx | `15AF5153BBB4D44C3B9EBDA81BA327ADC1B6F6162470C8546BE001C3BA9AE515` |
| Chart.hx | `3FB960A75FADBFA90AAA38710E89A6D030E1B7E02994019DC79A5E555A1E9844` |

## Adaptation and distribution scope

The C++ adaptation identifies the upstream authors, Apache 2.0 terms and FML changes in `src/fml_formats/LegacyChart.cpp`. Its upstream-derived portions remain under Apache 2.0. The product's MIT license covers its original code and does not replace upstream terms.

The Developer WIP also references Codename's script and gameplay APIs while implementing FML's own compatibility layer. This does not ship or start the Codename runtime. The reviewed source root contained no separate upstream NOTICE file; this document is Psych Importer's attribution record. Preserve any applicable original notices in future source imports.
