# Native Editor UI Reference

**Reference captured:** 2026-11-27  
**Source screenshot:** User-supplied portrait editor screenshot (692 × 1536 px).  
**Persistent source copy:** ChatGPT Library — `/NairoClip/Design References/Editor UI Reference 2026-11-27.png`.

## Authority

- This screenshot is the visual/layout reference for the NairoClip native editor.
- The NairoClip repository source remains the authority for the actual feature inventory, button semantics, event handlers, and command wiring.
- Do not infer or replace feature behavior from icon appearance alone.

## Layout invariants

Unless the user explicitly changes them, preserve the screenshot's:
- top share/export row;
- preview frame size, position, aspect ratio, and surrounding spacing;
- quick-action strip position, order, and overall alignment;
- timeline ruler, tracks, playhead, transport row, and bottom tool-category region;
- dark background, restrained contrast, and existing visual hierarchy.

The toolbar button count may change only as needed to expose the complete feature inventory discovered in the repository. Do not redesign or rearrange other regions to accommodate it.

## Quick-action button geometry

**All quick-action buttons must use one consistent circular visual diameter, calibrated to the AI button shown in the reference screenshot.** This applies to every quick-action item, including add, home, navigation/jump, marker, render, curve, hash, image, AI, undo, and redo controls.

Implementation requirements:
- equal width and height for every button's visual container;
- circular shape, not oval or pill-shaped;
- identical outer diameter and consistent center alignment;
- consistent spacing between button centers;
- icons centered independently of label or hit-target sizing;
- preserve accessible touch targets without changing the visible circle diameter.

## Additional known visual constraints

- Preview expand control remains a true circle.
- Timeline ruler tick marks remain short enough not to obscure the time labels.
- Transport scissors icon is a single scissors glyph rotated 90 degrees clockwise; do not add a line through it or duplicate the icon.
- Do not add glow, excessive blur, or high-brightness decoration.

## Verification checklist

- [ ] Compare implementation against the source screenshot, not a newly invented mock.
- [ ] Confirm all quick-action visual circles have the same measured diameter as the AI reference button.
- [ ] Confirm circles remain circular across supported screen sizes and densities.
- [ ] Confirm all quick-action controls are wired to the actual repository feature/command, or explicitly marked as not yet implemented.
- [ ] Confirm no unrelated editor region changed.
