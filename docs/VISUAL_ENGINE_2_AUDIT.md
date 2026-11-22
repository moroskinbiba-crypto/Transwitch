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
- The frame resets SDL clip and draw-blend state before composition and before Present.

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

### 5. No shared motion controller was found in the inspected path

The current renderer contains no central duration/easing/transition owner. Modal
visibility is boolean-driven and rendered immediately.

**Required correction:** add one small deterministic motion controller after modal
ownership is centralized. It must support reduced-motion and low-performance modes,
and must never delay input or workspace state changes.

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
