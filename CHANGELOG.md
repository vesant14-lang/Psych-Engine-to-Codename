# Changelog

## 1.0.1 - 2026-10-15

- Protect icon/atlas bounds, chart timing and key counts, sustain geometry, Animate work budgets and font parsing against invalid or excessive input.
- Make reviewed writes transactional, retain concurrent Freeplay entries, reject changed copied sources and keep linked history metadata inside the destination.
- Preserve correct Packer and animated-icon layouts, align chart metadata with exported vocals and retain loose-file edits across catalog reloads.
- Honor character/stage key bindings in full-song exports and keep unknown partial-export blockers visible.
- Reject empty packages, prevent writes from invalid CLI selections and return a failure when ZIP creation fails.
- Offer Mark the whole mod on resource tabs with the same supported selections as the manual lists; keep it out of Preview.
- Separate blockers, possible game-impact warnings and informational notes in the pre-export report.
- Ship Preview without a gameplay HUD or an experimental HUD activation option in both editions.
- Receive native Unicode paths in the metadata regression check.
- Provide English publication documentation and a source-repository README, keeping internal Spanish documents outside the release recipe.
- Include the isolated FML script-connection sources, their provenance manifest and an independent build check in Developer. This work is not connected to the application.
- Ship Codename and Psych Apache 2.0 licenses and attribution in both editions; distinguish original product code from upstream-derived portions and retain embedded ImGui font notices in the complete dependency records.
- Exclude generated Python caches even when tracked, verify engine-license contents, and preserve snapshot/license bytes across Git line-ending settings.

The application build and representative exports were tested against Codename Engine 1.0.1. See [CODENAME_COMPATIBILITY.md](CODENAME_COMPATIBILITY.md) for the scope and remaining limitations.
