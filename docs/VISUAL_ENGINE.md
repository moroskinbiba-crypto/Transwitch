# Transwitch Visual Engine 1.0

## Scope and non-goals

Visual Engine 1.0 is the native 2D rendering and component foundation for DeskNX / Transwitch. It is built on the existing SDL2 renderer and does not introduce Unity, Godot, a browser runtime, WebKit, a second graphics context, or a second renderer.

This is a rendering foundation, not a replacement for the launcher application model. Input ownership, hit testing, workspace placement, application discovery, title launching, account selection, IPC, persistence and service polling remain in their existing subsystems. The engine receives bounds and visual state; it does not decide what a control does.

The existing 0.1 and SOCOMA themes are not changed by this engine milestone. A separate visual showcase theme will be built only after the engine contract is stable.

## Layered architecture

1. **SDL backend** — borrows the launcher's existing SDL_Renderer. The app still chooses accelerated + VSync first and SDL software rendering as a fallback.
2. **VisualEngine** — owns only small, ordinary SDL textures used for cached gradient ramps and a radial-alpha mask. It exposes renderer-neutral operations for fills, strokes, gradients, glows and soft shadows. No effect requires a render-target texture or framebuffer capture.
3. **Visual tokens** — semantic colors, radii, spacing, icon sizes, focus width and minimum touch target. Theme selection supplies values; components do not hard-code a palette.
4. **UI components** — surfaces, widget shells, buttons, progress bars and dividers. They consume bounds + tokens + explicit state and remain independent from input handling.
5. **Content renderers** — text, SVG glyphs, game artwork and widget-specific values continue to use their bounded caches. They are progressively routed through the shared component contract without moving I/O into the draw loop.

## Rendering contract

- All SDL renderer calls and SDL texture creation/destruction run on the UI/render thread.
- Worker threads may read files, perform network requests and decode CPU-side image data, but must not create or mutate renderer textures.
- Rounded fills use fixed-capacity geometry spans and one SDL_RenderFillRects batch.
- Gradients use two lazily-created 256-sample ramps (vertical and horizontal), reused until endpoint colors change.
- Focus glow uses one lazily-created 96×96 radial alpha mask, tinted at draw time.
- Soft shadows use three low-alpha rounded silhouettes. This is deliberately not a Gaussian blur; it avoids render-target support, framebuffer copies and expensive per-frame blur passes.
- Modal backdrops stay on the normal logical window target. Modal composition must not switch render targets or modify SDL viewport/scale. It saves and restores draw color, blend mode and clip state around the scrim.
- All visible modal surfaces draw back-to-front. Only the top input-owning surface receives controller/touch input; lower parent surfaces remain visible under nested windows.
- Every primitive validates dimensions and tracks failures. Failure in an optional visual effect must not prevent input, state updates, launching or persistence.

## Tokens

include/desknx/VisualTokens.hpp defines the shared visual contract: semantic surface and text colors, status colors, radius scale, spacing scale, icon sizes, minimum touch target, focus ring width and glow opacity. Normalization clamps values and preserves monotonic scales. The first four token fields remain compatible with the launcher's existing aggregate initializers. Color tokens themselves are SDL-independent; conversion happens only at the rendering boundary.

## UI component contract

- **Surface** — fill, one-pixel stroke and optional three-layer shadow. The stroke is a real rounded outline; it must not tint the entire interior.
- **Widget surface** — shared surface plus a restrained accent rail and optional radial focus glow. Widget contents remain data-specific.
- **Button** — Glass, Rail, Minimal, Chip and Solid variants. Active, hover, focus, pressed and disabled are independent presentation states. The input/controller code owns all actions and hitboxes.
- **Progress bar** — bounded normalized progress, themed track/fill and rounded ends.
- **Divider** — one-pixel semantic separator.
- **Artwork tile** — shared desktop/library renderer for image destination, cover/contain/stretch fit, clip save/restore, cached rounded-corner mask, title scrim and badge shells. Tile model identity, text shaping, selection actions and input hitboxes remain in the launcher.
- **Modal surface / dialog surface** — common token-driven elevated containers for modal panels and confirmation dialogs.

## Performance and memory budgets

- Gradient cache: at most two small SDL textures, each 256×1 or 1×256.
- Glow cache: one 96×96 RGBA texture.
- No per-frame allocation in primitive rendering.
- No renderer-thread file reads, image decodes, network operations or font file parsing.
- Effects are opt-in at component call sites and must have a non-effect fallback.
- Track filledRectCalls, rounded batches, outlines, gradient draws/rebuilds, texture draws, radial glows, shadow layers and failures per frame.
- Frame-time and memory acceptance must be measured on physical Switch hardware before increasing shadow layers, icon cache limits, or introducing blur.

## Rollout and acceptance gates

### VE-1 — primitives and tokens

- [x] Shared semantic token contract and normalization.
- [x] Cached vertical and horizontal gradient primitives.
- [x] Cached radial glow mask.
- [x] Multi-layer soft shadow without render targets.
- [x] Unified surface/widget/button/progress/divider component API.
- [x] Pure host tests for token normalization and button paint states.
- [x] Current-branch native standalone NRO build.
- [ ] Current-branch HOME applet build completion.
- [ ] Physical Switch visual and frame-time verification.

### VE-2 — content and layout migration

Status: in progress.

- [x] Add a dedicated TileRenderer component for artwork surfaces, elevation and title scrims.
- [x] Replace flat game-title strips with cached vertical alpha gradients.
- [x] Extract tile label sizing into SDL-independent measured layout rules and host tests.
- [x] Preserve the current cover/contain fit policy, rounded-corner mask, tile geometry and selection/input hitboxes.
- [x] Move image fit, clipping, rounded-corner masking and reusable badge shells behind TileRenderer.
- [x] Migrate library game/homebrew artwork cards to the same TileRenderer contract.
- [x] Add host coverage for cover/contain/stretch math and library-card title layout.
- [ ] Add rendered screenshot fixtures for all tile sizes and both legacy themes.
- [ ] Verify cover crop, label contrast, focus and selection styles on console.

### VE-3 — windows and motion integration

- [x] Reuse shared modal/dialog surface shells across current windows.
- [x] Draw visible surfaces in registered z-order while keeping input on the top owner.
- [x] Keep modal scrim on the normal logical target and preserve renderer state without viewport/scale switching.
- [ ] Standardize transitions and reduced-motion behavior.
- [ ] Add automated open/close/reopen cycle tests where the state can be isolated from SDL.
- [ ] Verify full open/close/reopen loops and applet return on physical Switch.

### VE-4 — runtime validation

- [ ] Capture per-frame renderer metrics and texture counts on Switch.
- [ ] Measure 1×1, 2×1, 2×2, 3×2 and 2×3 tile/widget layouts.
- [ ] Test memory pressure, missing SD assets, software renderer fallback and sleep/resume.
- [ ] Only after these gates, build the independent showcase theme.

## Explicitly deferred

Gaussian blur, shader effects, OpenGL ES/Vulkan migration, the new showcase theme, wholesale redesign of library/settings, and changes to application/backend behavior are intentionally outside the current VE-2 slice.
