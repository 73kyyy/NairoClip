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


## Source-verified quick-action semantics

Verified against `mobile/pages/10-editor.html` and the `E.circles` click handler in `mobile/assets/editor.js`:

| Order | Source action | Actual behavior |
|---:|---|---|
| 1 | `import` | Open the add-media sheet (video / audio / image) |
| 2 | `home` | Leave the current project and return to the project list |
| 3 | `step-prev` | Single tap steps backward by segment; double tap jumps to project start |
| 4 | `marker` | Add a time marker at the current playhead; long press opens the marker list |
| 5 | `render` | Toggle preview-cache rendering and show render progress |
| 6 | `keyframes` | Open the current clip's keyframe sheet |
| 7 | `guides` | Toggle the preview composition guide lines (thirds grid) |
| 8 | `images` | Open image/background management |
| 9 | `ai` | Open the AI editing sheet (AI-assisted editing / “仿剪”) |
| 10 | `undo` | Invoke engine undo |
| 11 | `redo` | Invoke engine redo |

The shortcut immediately before the marker is **segment-back navigation**, not a generic jump button. The marker icon is a timeline marker; long-press opens its list. The keyframe icon is distinct from the curve tool in the lower editing toolbar.

## Current native implementation boundary

The native quick-action strip now renders all eleven controls in the verified source order, with a fixed **30dp × 30dp circular visual diameter** for every control (including AI). Undo/redo are wired to native callbacks. The other nine controls currently delegate to the retained WebView editor because the current native screen host does not yet expose their dedicated callbacks/engine adapters. This is an intentional fallback, not a claim that those nine actions are already native.


## Bottom toolbar source inventory

The first-level category list in `mobile/assets/editor.js` is the authoritative 13-category inventory:

`剪辑、模板、音频、文字、画中画、特效、转场、滤镜、调节、比例、背景、蒙版、更多`.

The native toolbar now exposes these same 13 first-level categories in that order. Existing native-only controls (such as animation/keyframe, transform parameters, and native effect panels) remain reachable as additional second-level entries rather than being removed when the legacy feature taxonomy is restored.

Second-level commands that already have native callbacks are routed to their native panels/actions. Commands without native host callbacks continue to open the retained WebView editor. This prevents visually available but nonfunctional dead ends while the remaining native adapters are implemented.


## Typed native quick-action callback contract

The native editor now exposes `NativeQuickAction` values for the nine non-history shortcut controls: `IMPORT_MEDIA`, `RETURN_HOME`, `STEP_PREVIOUS`, `ADD_MARKER`, `PREVIEW_RENDER`, `KEYFRAMES`, `TOGGLE_GUIDES`, `IMAGES`, and `AI_EDIT`.

`NativeEditorScreen` accepts an optional `onQuickAction` callback. When a host supplies it, each button emits its own semantic action rather than collapsing all buttons into one generic callback. Until a host supplies that callback, the screen retains the complete WebView fallback via `onOpenLegacy`. Undo and redo continue to use their dedicated native callbacks.

This is an interface boundary only: it does not claim that the nine actions already have native engine implementations or that the native screen is runtime-integrated into MainActivity.


## Native toolbar parity correction

The native toolbar's second-level labels and order have been reconciled against the exact `L2` object in `mobile/assets/editor.js`, not a manually expanded list. The native toolbar now uses the repository-defined source labels for all 13 categories. Native-only panel identifiers are internal routing targets only and are not injected as extra legacy-visible actions.

A shadowed transition-routing branch was also removed so the source-defined `转场 → 预设转场` item can reach the native transition panel when its required callbacks are present. Other unavailable callbacks continue to route to the retained legacy editor.


## Runtime integration gate (not yet passed)

Repository call-site inspection confirms:
- `NativeEditorScreen` currently has no runtime caller outside its own declaration.
- `NativeAppShell.kt` remains a placeholder.
- `MainActivity.kt` still creates and loads the WebView editor.
- `NativeEditorHostState` is a pure state/reducer contract and does not own Activity, ViewModel, JNI, or lifecycle resources.

Therefore, the Compose editor and its typed `NativeQuickAction` callback are not yet runtime-integrated into the app. The current quick-action callback is an extension point; without a host implementation it deliberately falls back to the complete WebView editor. Do not report the native editor migration as complete until a separately gated host/controller consumes the state contract, supplies engine-backed callbacks, and passes lifecycle, persistence, and device validation. The formal Activity entry and WebView fallback remain unchanged.

## Touch-target geometry follow-up (2026-11-27)

The shortcut visual circle remains **30dp × 30dp** for all eleven actions, including AI. Each visual circle is centered inside an independent **48dp × 48dp** clickable target. The larger hit target must not change the visible circle diameter; horizontal scrolling handles the resulting strip width. This separates visual parity from Android touch accessibility.


## Native layout implementation update (2026-11-28)

The current S5 native editor now follows the reference's major vertical regions:
- compact top row with share and export grouped at the right;
- centered 9:16 preview frame with a separate circular expand affordance at the lower-right of the preview stage;
- eleven quick actions in the established order, each using a 30dp visual circle inside a 48dp touch target;
- fixed-height timeline region with a 27dp ruler, 4dp major ticks, and 3dp minor ticks;
- centered three-action transport row (scissors, play/pause, delete);
- horizontally scrollable first-level toolbar and same-row second-level expansion.

The corresponding geometry is centralized in `mobile/native/android/NativeEditorLayoutSpec.kt` and guarded by `NativeEditorLayoutSpecTest`. The preview expand control is 36dp; the major region heights are 40dp (top row), 46dp (quick actions), 282dp (timeline), 56dp (transport), and 104dp (bottom toolbar), matching the 692x1536 reference's vertical proportions. These are reference-layout baselines, not permission to resize unrelated regions to make room for more controls.

The transport row uses the existing `onSplitClip` and `onDeleteClip` command callbacks when a clip is selected. Playback, share, export, and preview expansion expose typed optional callbacks; until the runtime host supplies them, those actions explicitly return to the retained WebView editor. This update does not claim the callbacks are connected to MainActivity or that the native editor is runtime-integrated.

- The native first-level category toolbar now uses vector assets converted from the repository's existing `mobile/assets/icons.js` icon sprite, rather than rendering the first character of each category name. Share, fullscreen, scissors, play/pause, and delete also use the matching existing icon artwork. The scissors vector is rotated clockwise by 90 degrees and remains a single scissors glyph.
- The quick-action strip now reuses the existing icon sprite for add, home, segment-back, marker, keyframes, guides, image/background, undo, and redo; the reference's compact “渲染” and “AI” text treatments are retained. The AI circle keeps the same 30dp diameter as every other action and uses the reference-style restrained purple-to-blue fill.

The native timeline's time origin is now centered, matching the reference and the original editor's `t2x()` mapping. Initial zoom fits the visible sequence around the centered playhead; ruler intervals adapt to pixel density (56px major-tick spacing), and labels switch between seconds, sub-second precision, and `MM:SS` for longer timelines. The fixed start badge seeks to zero. Track clips render across the full timeline width while the eye, speaker, and lock controls float in a 40dp overlay rather than reserving a 120dp label gutter.

## Track-control state and feather behavior

The fixed 40dp track-control overlay contains, from left to right, visibility (eye), audio (speaker), and lock controls. The clip lane continues underneath it; do not reserve a blank 40dp gutter or move the clip origin.

A per-track horizontal blur feather is composited over the clip lane before the controls:
- On Android 12 / API 31+, draw a clipped copy of the lane and clip content with a 3dp Gaussian `RenderEffect`, then apply a horizontal `DST_IN` alpha mask.
- The mask starts at 88% opacity at the far-left edge and decreases smoothly to 0% at the right edge of the 40dp control overlay. The blur is strongest at the left and fades away toward the timeline body.
- On Android 7–11 / API 24–30, where `RenderEffect` is unavailable, retain the same horizontal alpha feather as a compatibility fallback; do not claim that these versions apply Gaussian blur.
- The eye, speaker, and lock glyphs are redrawn above the feather at full clarity. The feather must never tint, blur, or reduce their contrast.
- Recompute the effect for each row so alternating lane backgrounds remain correct. The feather is screen-space anchored and does not move independently of the fixed controls when the timeline scrolls or zooms.

Toggle-state colors (callback availability must not be confused with the actual toggle state):
| Control | State | Icon |
|---|---|---|
| Eye | Visible | #E4E8EA |
| Eye | Hidden | #687177 |
| Speaker | Sound on | #E4E8EA |
| Speaker | Muted | #D69A58 |
| Lock | Unlocked | #9AA3A8 |
| Lock | Locked | #D69A58 |

The glyph also changes with state (eye/eye-off, speaker/speaker-off, unlock/lock). Keep the amber restrained; do not add glow or animated flashing. A state change should update only the affected track row and must not shift timeline geometry.

## Activity runtime pilot boundary

The native editor is mounted by the existing `MainActivity` only when both conditions are true:
1. The app is a debug build (`BuildConfig.DEBUG`).
2. The Activity is explicitly launched with `com.nairoclip.app.extra.ENABLE_NATIVE_EDITOR=true`.

With that opt-in enabled, the home page adds `nativeEditor=1` to the editor route. After the legacy editor has booted and loaded its current engine snapshot, it calls the `NCNativeEditor` bridge. The Activity accepts that call only while the local `10-editor.html` document is active, validates the current engine response envelope and `result.project`, then mounts `NativeEditorHost` in a full-screen `ComposeView`.

When the home page opens an existing draft, `Engine.boot()` now consumes the `project` query parameter before reading the editor snapshot. Android's `NativeEngineBridge.loadProject()` accepts only a single `.nclip` filename under the app-private files directory, rejects traversal/out-of-root paths and files larger than 64 MB, and calls the existing `nc_project_load` C ABI on the same engine handle. If the platform cannot load the requested file, editor boot fails explicitly rather than silently showing a blank project.

The pilot reuses the Activity's existing `NativeEngineBridge` and moves the same `PreviewSurfaceView` into the Compose host. It does not create a second engine or renderer Surface. The WebView remains attached and alive underneath the overlay, but `WebView.onPause()` is called while the native host owns the shared engine so the hidden page does not continue polling/rendering concurrently. The native host's legacy-return action and Android Back remove the overlay, restore the same preview Surface to the original root, call `WebView.onResume()`, and reveal the existing WebView editor. A resize event asks the WebView to recalculate the legacy preview rectangle.

This is an internal validation route, not the formal application route:
- The launcher intent filter remains on `MainActivity`.
- The extra is ignored by release builds.
- No Activity entrypoint is replaced and no WebView fallback is removed.
- ADB launch for a debug APK: `adb shell am start -n com.nairoclip.app/.MainActivity --ez com.nairoclip.app.extra.ENABLE_NATIVE_EDITOR true`.

Acceptance still requires Android Kotlin compilation/unit tests, NDK and host CI, plus real-device checks for Surface attach/detach, rotation/recreation, system Back, repeated native↔legacy switching, and preservation of the current project state. Until those pass, the pilot must remain opt-in.

## Native host boundary

`NativeEditorHost` and `NativeEditorHostController` provide a feature-scoped host without changing `MainActivity` or the formal WebView entry. The host serializes engine mutations and reads back authoritative timeline/history state. Native clip split/delete/duplicate/ripple-delete, undo/redo, track visibility/mute/lock, markers, transforms/keyframes, speed/speed curves, clip audio/denoise, filters/LUT/color grade/masks, effects, transitions, text clips and subtitle cues are wired through the command adapter. Scrub requests render through `NativePreviewSurfaceController` on its dedicated worker, including a render request when the Surface attaches.

For automatic subtitles, the timeline snapshot now retains each clip's `assetId`. The host accepts an asset-ID-to-`content://` URI resolver, a loaded `NativeSpeechSessionWorker`, and its model-ready state; it maps the selected clip through `NativeSpeechSourceRange` and connects the existing `NativeAutoSubtitleCoordinator` to the native panel. The host takes ownership of a supplied speech worker and closes it when disposed. The model loader/asset repository must supply the worker and URI resolver; this does not change Activity routing.

Full synchronized A/V playback, export/share, media import and AI entry points still use the retained WebView fallback. They are not represented as completed native functions. The host remains feature-scoped and is not mounted as the formal Activity entry.
