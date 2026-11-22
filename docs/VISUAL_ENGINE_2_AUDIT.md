# Visual Engine 2.0 — baseline audit

Baseline: `main` / current implementation branch after modal-stack and surface-shell work.
Scope inspected: `source/main.cpp`, `source/FontRenderer.hpp`, and current SDL draw /
input paths. This is an architecture audit, not a claim that the white-screen defect
has already been reproduced or fixed.

## Implementation update

The first Visual Engine 2.0 stability slice is now implemented in this branch:

- `ModalStack` owns the single bottom-to-top z-order list.
- `drawScreen()` builds the stack once and renders all active layers in that order.
- `keyInput()` and `touchInput()` resolve the same stack and dispatch only to its top layer.
- Parent/child surfaces such as Launcher Settings + Media Picker remain visible together,
  while the child receives input.
- Host tests cover overlapping modal precedence.
- All outer modal shells now use the shared `drawModalBackdrop()` and
  `drawModalSurface()` primitives.
- Workspace deletion now uses a shared `ConfirmationDialogLayout` for both visible
  button geometry and touch hit-testing; the layout has a host test for boundaries,
  containment and non-overlap.
- The frame resets SDL clip and draw-blend state before composition and before Present.
- Scoped RAII guards now restore clip rects around wallpaper, workspace-background and
  tile-artwork copies, and restore the rounded-mask texture's previous color/alpha
  modulation rather than assuming white/opaque was its prior state.
- `WindowComponents.hpp` now defines shared dialog, side-panel, context-menu and
  repeated-row geometry contracts. Workspace Picker, Workspace Options and the App
  Library details panel use the same layouts for drawing and touch hit testing; row
  gaps are explicitly non-interactive.
- `Typography.hpp` adds Display, Heading, Subheading, Body, Caption and Metadata
  tokens, retaining existing FontRenderer scales. A role-aware drawing adapter has
  been introduced and used first in the confirmation dialog, library and workspace
  picker to avoid an all-at-once text refactor.

This closes the z-order mismatch described below. It does not yet prove the white-screen
issue fixed; on-console reproduction and compositing regression tests remain open.

## Findings

### 1. Window ownership and modal layering are not centralized

`drawScreen()` draws each window using independent boolean checks. The inspected path
contains separate blocks for the application library, workspace menu, workspace
settings, workspace picker, delete confirmation, recent apps, tools, launcher settings,
media picker, power menu and user picker.

Input dispatch is separately ordered in `keyInput()` and touch handling. Because draw
order and input precedence are implemented in different places, two flags can be true
at once and a visually covered surface can still own input. This is a concrete
architecture risk; it is not yet proven to be the sole cause of the reported white screen.

**Required correction:** one modal/layer state model must be authoritative for both
rendering and input. Modal transitions must close, replace or explicitly stack a
previous modal; no hidden dialog may receive input.

### 2. Surface construction is repeated inline

The renderer has useful low-level primitives:
- `roundRect()` clamps corners to the global 10 px target.
- `softCard()` draws a shadow, fill and edge.

But every modal still repeats its own dim overlay, card geometry, title, footer and
button styling. The primitives are not yet a component system with consistent sizing,
focus, disabled, pressed, selected and destructive states.

**Required correction:** extract a small component layer without changing the visual
baseline in one large rewrite. Migrate one modal at a time and preserve its hit regions.

### 3. Render state needs an explicit frame boundary

The examined paths mutate SDL clip state in wallpaper/background/artwork functions and
reset it manually afterward. Texture color/alpha modulation is also manually restored
for rounded masks. The visible paths currently do contain resets, but there is no
single frame-boundary invariant or scoped state guard to protect future early returns
and new components.

**Required correction:** establish a known renderer state at frame start and after
each clipped surface/image operation. Add tests or debug assertions for clip reset and
texture state restoration. This reduces state leakage risk; it does not by itself prove
or fix the reported white-screen defect.

### 4. Typography is functional but not semantic

`FontRenderer` has TTF texture caching and a bounded fitted-text cache. The public draw
API still accepts an integer scale rather than named roles. The bitmap fallback maps
non-ASCII code points to a replacement glyph, so correct Cyrillic depends on successful
TTF/shared/custom-font rendering.

**Required correction:** introduce roles (Display, Heading, Subheading, Body, Caption,
Metadata) mapped to actual font sizes/weights/line metrics. Keep a predictable Cyrillic
fallback and test it when shared-font initialization fails.

### 5. Motion is now centralized for the first interaction

`MotionController.hpp` provides deterministic Linear, EaseOutCubic and EaseInOutCubic
tracks, with Full, Reduced and LowPerformance duration policies. A 160 ms eased track
now drives selected-tile focus emphasis; state navigation remains immediate and only
the visual emphasis is animated. Host tests cover easing, reduced/low-performance
modes and 32-bit tick wraparound.

Full / Reduced / Low Power mode selection now lives in Launcher Settings and is
persisted through the existing backward-compatible `DESKNXSET1` settings file. The
settings renderer, gamepad navigation and touch selection share the same row layout.
Older settings files without a `motion=` key retain the default Full mode.

**Still required:** migrate modal, panel and workspace transitions in small slices.
Keep input/state changes immediate; animation must remain presentation-only.

## Implementation order

1. Modal state model shared by draw and input; add transition/invariant tests.
2. Renderer state guards and a deterministic full-frame recovery test.
3. Shared surfaces and controls; migrate library and settings first, then remaining
   modal types.
4. Semantic typography roles and Cyrillic fallback coverage.
5. Motion controller and reduced-motion mode.
6. Visual regression captures and on-console white-screen acceptance run.

## Acceptance criteria

- At most one modal owns input unless a parent/child stack is explicitly declared.
- The modal drawn on top is the modal that receives focus and input.
- Every modal can open, close, reopen and be replaced repeatedly without a white or
  unpainted frame.
- SDL clip rect is disabled at frame end; texture modulation is restored after use.
- Buttons and rows have the same geometry and focus states across all windows.
- Typography roles are consistent and Cyrillic remains legible in shared-font,
  custom-font and fallback paths.
- Reduced-motion mode disables nonessential transitions without affecting navigation.

## Control system implementation update

The next VE 2.0 slice adds `ControlComponents.hpp` as a renderer-independent
contract for Primary, Secondary, Destructive and Quiet buttons, including focused,
pressed and disabled states. Selectable rows now have shared Normal, Current,
Focused and Disabled visual states. The draw adapter remains in `main.cpp`, where
it converts the pure palette tokens into the existing SDL primitives.

The first migrations are intentionally narrow: workspace picker rows/cancel action,
workspace option rows, and destructive confirmation actions. Their existing rectangles,
labels and touch boundaries are preserved. Button hit testing uses the same
`ModalRect` as rendering, and disabled buttons explicitly reject touch activation.
A host test covers visual-state resolution and half-open hit boundaries.

This does not yet complete the entire control library: badges, toggles, fields,
cards and indicators remain open. It also does not address the synchronous text-cache
miss path; the new control adapter deliberately performs no allocation or file/network
work of its own.

```text
Disabled selectable rows are now non-interactive in both gamepad and touch
dispatch. The workspace "Move selected tile" action remains in the menu when
unavailable and reports why it cannot be activated; the visual Disabled state
and activation policy use the same shared predicate.
```

## Typography Engine implementation update

`TextLayout.hpp` now provides host-testable UTF-8 code-point traversal, word wrapping,
long-word splitting and max-line ellipsis. `FontRenderer::drawWrapped` applies the
semantic role's line-height and max-lines policy, with a bounded 256-entry wrapped-layout
cache keyed by text, scale, width and line count. Changing the custom font clears both
the fitted-text and wrapped-layout caches.

The confirmation dialog description is the first real text-block migration. Existing
single-line `textRole` calls and the normal text texture cache remain unchanged.
Tests cover word boundaries, Cyrillic code points, long words, explicit newlines,
truncation and malformed UTF-8 input.

Important remaining limits: the bitmap fallback still cannot render full Cyrillic,
and TTF surface creation / SDL texture upload still occur synchronously on a cache miss.
The new layout cache removes repeated wrapping work; it does not claim to solve that
separate frame-time issue.

## Expanded control library update

The shared control layer now also defines Badge, Toggle, ValueField, Card and Status
Indicator visual contracts. Palette resolution remains pure data and renderer-independent.
The live migrations use the shared components for app-type/update badges in the library,
the edit-mode setting toggle, theme/icon/motion value fields, the library-service status
dot, System Tools cards and the library details panel.

The settings toggle reflects the actual `editing` state and remains informational within
the row; the existing whole-row activation path still owns the action. No new input route
was added. Value fields likewise preserve the existing row activation/navigation logic.
Status tones distinguish Loading, Ready and Offline without adding polling or any
additional service calls.

The host control contract test now covers all of these resolvers and interactive
hit-test boundaries. Remaining migrations include other modals, toolbar/footer elements
and workspace controls; this change does not claim the full UI has already been migrated.

## App Library toolbar geometry update

The seven App Library header actions now use `AppLibraryToolbarControls` as their
single geometry source. The drawing loop uses the same rectangles and shared Toolbar
button variant that touch dispatch resolves through `itemAt`. Visible chip height is
now 30 px, and the formerly duplicated touch hit boxes have been removed. The empty
gutters remain inert so a tap between chips cannot trigger another action.

The controller shortcuts (Y/X/L/R/Minus/Right Stick/B/A) are unchanged. The host
window-layout test covers every chip edge, the 30 px vertical bound and the gutters.

## System Tools grid geometry update

System Tools now uses `SystemToolsWindowControls` for the window surface, six card
rectangles and Back button. The render loop obtains each card rectangle from this layout;
the touch dispatcher resolves the same rectangles through `itemAt`, and Back uses a
shared `ButtonControl` for both drawing and hit testing. The keyboard's row/column
navigation remains row-major and unchanged.

This removes the old independent x/y ranges for card taps, including inclusive right
and bottom edges that were one pixel outside the painted surface. Tests cover all six
cards, row gutters, outer edges and the Back button's half-open bounds.

## Media Picker geometry update

Media Picker now uses `MediaPickerWindowControls` for the surface, directory toggle,
Back action and 12 file rows. Buttons are drawn with shared ToolbarSelected/Secondary
variants and use their `ButtonControl` bounds for touch dispatch. File rows use the
same `ItemListLayout` for drawing and hit testing; 4 px gaps between 30 px rows are
now deliberately inert instead of selecting the adjacent row.

Keyboard navigation and file-selection semantics are unchanged. The new layout test
checks the first/last row, row gutters, all button edges and the outer list bounds.

## User Picker and Power dialog geometry update

User Picker uses `UserPickerDialogControls` for its Back button and up to eight
account rows, matching the Horizon `accountListAllUsers(..., 8, ...)` cap. Each
painted row is 40 px high with a 6 px gutter; touch now selects only the visible
row rectangle, and the Back hit target matches its 76 x 34 px button.

Power uses `PowerDialogControls` for its three action rows, Back and Execute buttons.
Row selection is separate from execution: tapping an action only moves the selected
row; tapping Execute retains the existing keyInput(A) flow. Gamepad input is unchanged.
The test suite verifies row gaps, each button's half-open bounds and containment inside
the parent dialog.

## Recent dialog geometry update

Recent uses `RecentWindowControls` for the surface, Back/Launch buttons and ten
history rows. The history is already capped at ten entries by the persistence layer,
so the fixed ten-row layout fully covers the visible list without introducing paging.
Each row uses a 35 px hit target on a 39 px stride; the 4 px visual gutters are inert.
The Back and Launch actions now use the exact drawn bounds.

Keyboard Up/Down/A/B behaviour is unchanged. A tap on a history row only selects it;
the separate Launch button retains the existing launch path. Tests cover first/last
row bounds, gutters, button edges and containment.

## Settings windows geometry update

Launcher Settings and Workspace Settings now each expose one `SettingsDialogLayout`
containing the parent surface, Back button and selectable rows. Both close actions use
the shared Secondary Button contract for drawing and hit testing. Launcher Settings
keeps 12 rows (26 px high / 28 px stride); Workspace Settings keeps seven rows
(40 px high / 48 px stride). Their 2 px and 8 px gutters respectively are inert.

Touch now resolves the same row rectangles that render, instead of a separate formula
that previously accepted trailing pixels beyond the painted Workspace Settings rows.
Keyboard navigation remains cyclic and uses the layout row count. Selected row visuals
use the shared Focused state.

## App Library list hit-target update

App Library now uses `AppLibraryListRows` for both row drawing and touch selection.
The painted row is 730 x 35 px on a 40 px cadence; the 5 px vertical gutters and the
side-panel margins are no longer accepted as list taps. Existing icon, title and badge
coordinates are retained relative to the row rectangle.

The details-panel "Add to Workspace" action is now drawn and hit-tested through one
Primary `ButtonControl` using the existing `primaryAction` rectangle. The separate
toolbar Add action is unchanged. The double-tap-to-launch behaviour, 500 ms interval,
selection update and play-stat refresh path are preserved.
