# Contributing

Start with a reproducible problem or a concrete workflow improvement. The [README](README.md) describes the supported user-facing behavior; `DEVELOPMENT.md` in the source edition explains builds and checks.

## Report a bug

Include the importer and Codename versions, the selected resource or song/difficulty, reproduction steps, expected behavior, actual behavior, the conversion report and relevant log lines. A small sample with clear redistribution permission is preferable to an entire mod.

Distinguish a missing source asset, an unsupported behavior and a regression in a supported path. Report a successful structure check separately from a test performed in the actual engine.

## Propose a change

- Preserve the source mod and existing destination data.
- Keep resource identity tied to its source and logical path.
- Make shared format, atlas, scene and I/O fixes in the FML core; avoid a separate active implementation in the standalone.
- Keep layout and preferences in the standalone frontend.
- Give unsupported behavior an explicit report rather than emitting a silent approximation.
- Keep the isolated WIP sources out of the normal build until their integration is reviewed and validated.

Use a focused change description: the concrete trigger, resulting behavior, relevant tests and any remaining limitations. Run checks appropriate to the change; use engine runs when an export behavior changes.

## Documentation

All Markdown published in the repository or in either release archive is written in English. Use English filenames and working relative links. The app retains its English and Spanish interface.

Internal Spanish design documents, research, audits and development logs stay outside the publication recipe. Preserve third-party attribution and license terms.

## Licensing and source attribution

Preserve original third-party licenses and notices. Codename Engine and Psych Engine use Apache 2.0; original FML code and upstream-derived portions have distinct licensing scope. Record the source and changes when adapting upstream code. See the distribution's `THIRD_PARTY_NOTICES.md` and engine-specific records under `third_party/`; publishing WIP does not remove these requirements.
