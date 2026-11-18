# Cards home view — November 2026

A CardsNX-style showcase for the DeskNX home screen, modelled on HomeNX's
`projects/cards` look: one large cover per app on a horizontal rail, with a
detail panel and a Play button under it.

## What it does not change

Transwitch keeps its Android-style workspaces. The showcase is a second *view*
of the same data, not a second model:

- The 6 × 3 grid, tile shapes, drag/drop, resize and persistence are untouched.
- `desknx::cards::makeDeck()` projects the active workspace into a left-to-right
  deck each frame, ordered by grid position. A multi-cell tile yields exactly
  one card.
- Switching views only moves the cursor: entering the showcase focuses the card
  under the grid cursor, leaving it puts the grid cursor on the focused card.
- Every modal (library, tools, settings, power, pickers, prompts) is shared and
  draws over whichever view is active.

## Files

| File | Role |
|---|---|
| `include/desknx/CardsLayout.hpp` | Geometry, hit testing, swipe thresholds, deck projection. No SDL, no I/O. |
| `source/CardsHomeRenderer.hpp/.cpp` | SDL2 drawing. Owns one cached backdrop texture; borrows icon textures through callbacks. |
| `source/main.cpp` | View state, input routing, persistence. |
| `tests/cards_layout_test.cpp` | 48 checks over the layout and deck logic. |
| `tests/localization_test.cpp` | 19 checks over the translation table. |

Both new sources are picked up automatically by `Makefile.switch` and
`projects/DeskNXMenu/Makefile`, which glob `source/*.cpp`.

## Controls

| Input | Showcase |
|---|---|
| D-pad / stick ←→ | Move along the rail (clamped at both ends, no wrap) |
| A | Launch the focused app; on an empty desktop, open the library |
| X | Arrange mode (returns to the showcase when you leave it) |
| Y | Open the library |
| L / R | Previous / next workspace |
| ZL | Switch to the desktop grid |
| +, −, L3, R3, B | Unchanged global meaning |
| Touch | Tap a neighbour to focus, tap the focused card to launch, swipe to scroll |

From the grid, ZL returns to the showcase. The choice is written to
`sdmc:/switch/DeskNX/settings.ini` as `view=cards` / `view=grid`, and is also
exposed in Settings as "HOME SCREEN STYLE".

## Rendering notes

- Artwork is drawn with `SDL_RenderGeometry` and a rounded UV mesh, so covers
  follow the card silhouette. There is a `SDL_RenderCopy` fallback for SDL
  older than 2.0.18.
- Portrait covers fill the card. Square NACP icons — which every installed
  title ships with — get a framed treatment with the title underneath instead
  of being cropped.
- The backdrop is one 320 × 180 texture, regenerated only when the theme
  changes, then stretched. No per-pixel work per frame.
- Only visible cards are drawn; off-screen ones are skipped.
- No filesystem, network or decode work was added to the render thread. Covers
  still come from the existing bounded `IconCache` worker.

## Verification

Run from the directory containing the clone:

| Script | What it proves |
|---|---|
| `check-host-tests.ps1` | 20 host suites, including the two new ones |
| `check-cards.ps1` | Layout tests + renderer compiles against upstream SDL2 headers |
| `check-switch-syntax.ps1` | `main.cpp` and the renderer compile for aarch64 against upstream libnx headers |

`DeskNX-Cards-preview.png` and `DeskNX-Cards-icons.png` are rendered by
`preview-work/preview.exe`, which links the real `CardsHomeRenderer.cpp`
against desktop SDL2 and writes a frame offscreen. Sample art there is
generated procedurally and is not part of the repository.

## Not verified

- **No hardware test.** devkitPro/devkitA64 cannot be installed in this
  environment, so there is no `.nro`. The aarch64 check validates syntax,
  types and overload resolution with clang — it is not the devkitA64 GCC build
  and says nothing about runtime behaviour, performance or Atmosphère
  compatibility on a console.
- GitHub Actions does not run on PRs in this repository, so CI is not a gate.
- Frame timing, memory use and touch feel are unmeasured on real hardware.

## Follow-ups

1. Build with devkitPro and test on a console.
2. Wire the SteamGridDB cover picker (`SteamGridDbCovers.hpp` is ready) so the
   rail shows real portrait art rather than falling back to NACP icons.
3. Give the showcase its own widget presentation; widget tiles currently appear
   as cards with an icon and label.

{"content":"# Cards home view — November 2026\n\nA CardsNX-style showcase for the DeskNX home screen, modelled on HomeNX's\n`projects/cards` look: one large cover per app on a horizontal rail, with a\ndetail panel and a Play button under it.\n\n## What it does not change\n\nTranswitch keeps its Android-style workspaces. The showcase is a second *view*\nof the same data, not a second model:\n\n- The 6 × 3 grid, tile shapes, drag/drop, resize and persistence are untouched.\n- `desknx::cards::makeDeck()` projects the active workspace into a left-to-right\n  deck each frame, ordered by grid position. A multi-cell tile yields exactly\n  one card.\n- Switching views only moves the cursor: entering the showcase focuses the card\n  under the grid cursor, leaving it puts the grid cursor on the focused card.\n- Every modal (library, tools, settings, power, pickers, prompts) is shared and\n  draws over whichever view is active.\n\n## Files\n\n| File | Role |\n|---|---|\n| `include/desknx/CardsLayout.hpp` | Geometry, hit testing, swipe thresholds, deck projection. No SDL, no I/O. |\n| `source/CardsHomeRenderer.hpp/.cpp` | SDL2 drawing. Owns one cached backdrop texture; borrows icon textures through callbacks. |\n| `source/main.cpp` | View state, input routing, persistence. |\n| `tests/cards_layout_test.cpp` | 48 checks over the layout and deck logic. |\n| `tests/localization_test.cpp` | 19 checks over the translation table. |\n\nBoth new sources are picked up automatically by `Makefile.switch` and\n`projects/DeskNXMenu/Makefile`, which glob `source/*.cpp`.\n\n## Controls\n\n| Input | Showcase |\n|---|---|\n| D-pad / stick ←→ | Move along the rail (clamped at both ends, no wrap) |\n| A | Launch the focused app; on an empty desktop, open the library |\n| X | Arrange mode (returns to the showcase when you leave it) |\n| Y | Open the library |\n| L / R | Previous / next workspace |\n| ZL | Switch to the desktop grid |\n| +, −, L3, R3, B | Unchanged global meaning |\n| Touch | Tap a neighbour to focus, tap the focused card to launch, swipe to scroll |\n\nFrom the grid, ZL returns to the showcase. The choice is written to\n`sdmc:/switch/DeskNX/settings.ini` as `view=cards` / `view=grid`, and is also\nexposed in Settings as \"HOME SCREEN STYLE\".\n\n## Rendering notes\n\n- Artwork is drawn with `SDL_RenderGeometry` and a rounded UV mesh, so covers\n  follow the card silhouette. There is a `SDL_RenderCopy` fallback for SDL\n  older than 2.0.18.\n- Portrait covers fill the card. Square NACP icons — which every installed\n  title ships with — get a framed treatment with the title underneath instead\n  of being cropped.\n- The backdrop is one 320 × 180 texture, regenerated only when the theme\n  changes, then stretched. No per-pixel work per frame.\n- Only visible cards are drawn; off-screen ones are skipped.\n- No filesystem, network or decode work was added to the render thread. Covers\n  still come from the existing bounded `IconCache` worker.\n\n## Verification\n\nRun from the directory containing the clone:\n\n| Script | What it proves |\n|---|---|\n| `check-host-tests.ps1` | 20 host suites, including the two new ones |\n| `check-cards.ps1` | Layout tests + renderer compiles against upstream SDL2 headers |\n| `check-switch-syntax.ps1` | `main.cpp` and the renderer compile for aarch64 against upstream libnx headers |\n\n`DeskNX-Cards-preview.png` and `DeskNX-Cards-icons.png` are rendered by\n`preview-work/preview.exe`, which links the real `CardsHomeRenderer.cpp`\nagainst desktop SDL2 and writes a frame offscreen. Sample art there is\ngenerated procedurally and is not part of the repository.\n\n## Not verified\n\n- **No hardware test.** devkitPro/devkitA64 cannot be installed in this\n  environment, so there is no `.nro`. The aarch64 check validates syntax,\n  types and overload resolution with clang — it is not the devkitA64 GCC build\n  and says nothing about runtime behaviour, performance or Atmosphère\n  compatibility on a console.\n- GitHub Actions does not run on PRs in this repository, so CI is not a gate.\n- Frame timing, memory use and touch feel are unmeasured on real hardware.\n\n## Follow-ups\n\n1. Build with devkitPro and test on a console.\n2. Wire the SteamGridDB cover picker (`SteamGridDbCovers.hpp` is ready) so the\n   rail shows real portrait art rather than falling back to NACP icons.\n3. Give the showcase its own widget presentation; widget tiles currently appear\n   as cards with an icon and label.\n"}