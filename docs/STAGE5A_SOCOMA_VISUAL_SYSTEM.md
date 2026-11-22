# Stage 5A — SOCOMA as an independent visual system

## Intent

SOCOMA is no longer a recolour of the 0.1 Frosted Glass theme. It has its own surface grammar and composition. The 0.1 theme is deliberately left on its original rendering path.

## SOCOMA visual rules

- **Flat geometry:** cards, workspace panels and modal surfaces use rectangular fills and thin borders instead of translucent rounded-glass surfaces.
- **Action grid:** all buttons use a consistent compact corner radius (maximum 4 px), solid surface variant and explicit accent border for active/focused state. Inactive actions remain visible as grid cells rather than disappearing.
- **Technical top rail:** the header has a dark full-width band, accent rule and a compact Transwitch mark. Existing tool positions, input mappings and touch targets are retained.
- **Workspace frame:** the desktop uses a flat framed surface, a named workspace header and a persistent 6×3 cell grid. Empty cells remain visible without entering edit mode.
- **Tile language:** widget tiles use flat surfaces, thin outlines and a vertical accent rail. Game and homebrew tiles keep their full-cover artwork and existing title overlay; they do not inherit widget styling.
- **Command footer and dock:** the existing footer actions, workspace tabs, add/menu/edit controls use the same square button policy, giving the navigation a segmented grid appearance.
- **System overlays:** modal and side-panel surfaces use the same SOCOMA surface treatment, avoiding a mixture of glass and flat cards.
- **SVG assets:** existing semantic IconIds, SVG sources, cache and fallback rendering remain unchanged.

## Compatibility and non-goals

- `themeIndex=0` (0.1) keeps the old glass surface path, rounded cards and original button geometry.
- The accent colour remains independently configurable.
- The patch does not move interaction targets or change controller/touch action routing.
- Existing saved workspace positions, artwork, widget configuration and desktop order are not migrated or reset.
- This is a visual-system implementation increment, not a claim that all every settings subpage has been independently art-directed. Dialogs and system panels inherit the new surface/button rules; detailed per-screen composition can continue in later increments.

## Validation

CI must compile the standalone NRO and HOME applet and run the repository host tests. Physical Switch review is still required for visual balance, font metrics, focus visibility and touch target ergonomics. No hardware validation is claimed by the code change alone.
