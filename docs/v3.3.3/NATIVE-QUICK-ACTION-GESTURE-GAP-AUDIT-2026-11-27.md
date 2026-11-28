# Native Quick-Action Gesture Gap Audit — 2026-11-27

## Scope

Read-only comparison of the native Compose quick-action strip against the current legacy editor's authoritative controls in:

- `mobile/pages/10-editor.html`
- `mobile/assets/editor.js`
- `mobile/native/android/NativeEditorScreen.kt`
- `android/app/src/main/java/com/nairoclip/app/MainActivity.kt`

This audit does not change runtime behavior, Activity routing, JNI/C ABI, persistence, or the WebView fallback.

## Verified source behavior

The legacy editor exposes eleven actions in this order:

1. Import: opens the media sheet.
2. Home: confirms before leaving when the project has undo/redo history, then returns to the project list.
3. Step previous: single tap moves to the immediately preceding clip boundary; double tap jumps to project time zero. Single-tap execution is delayed 220 ms to disambiguate the double tap.
4. Marker: single tap writes a marker at the current playhead; a 520 ms pointer hold opens the marker list.
5. Render: toggles preview-cache rendering and its progress state.
6. Keyframes: opens the keyframe sheet.
7. Guides: toggles the thirds composition overlay.
8. Images: opens image/background management.
9. AI: opens the AI editing sheet.
10. Undo: invokes the engine undo operation.
11. Redo: invokes the engine redo operation.

The marker long-press implementation is explicitly bound through pointer down/up/cancel handling in `bindMarkerLongPress()`; the step-previous double-tap behavior is implemented in the delegated click handler.

## Native implementation findings

- The Compose strip renders all eleven controls in the same order.
- Each visible action circle is 30dp × 30dp, centered in a 48dp clickable target.
- Undo and redo have dedicated native callbacks.
- The other nine actions are represented by `NativeQuickAction` and sent through the optional `onQuickAction` callback. If the callback is absent, the screen calls `onOpenLegacy()`.
- `MainActivity.kt` does not instantiate `NativeEditorScreen`; the native screen is not currently part of the running Activity path.
- `QuickActionButton` currently exposes a simple click only. It does not express the legacy step-previous double-tap or marker long-press gestures.
- The current native AI circle is visually emphasized with a blue fill/border while the other circles use the neutral lane fill. The 30dp geometry is consistent, but visual color parity with the reference has not been measured against the original screenshot in this audit.

## Required native parity work

1. Add explicit gesture semantics for step-previous single/double tap without executing the single action before the double-tap decision window expires.
2. Add marker long-press routing to the native marker-list surface while preserving single-tap marker creation.
3. Implement and host the nine typed quick actions; do not count the callback enum or legacy fallback as native completion.
4. Preserve home's unsaved-work confirmation and project-list navigation semantics.
5. Add Compose tests for single/double tap arbitration, marker tap/long-press arbitration, cancellation, and accessibility semantics.
6. Keep the screenshot's visual circle size and strip placement unchanged while retaining 48dp touch targets; separately verify AI fill/border treatment against the user's screenshot.

## Status

**Incomplete / not runtime-integrated.** This audit is source inspection only. No Android Gradle build, unit test execution, CI run, emulator, or physical-device test was performed. The formal Activity entry and WebView fallback remain unchanged. PR #32 remains Draft and unmerged.

## 2026-11-28 follow-up — gesture parity and legacy action dispatch

The earlier finding that the native quick-action button was click-only is superseded by the current source:

- `QuickActionButton` uses Compose `combinedClickable` on a 48dp target while retaining the 30dp visual circle.
- `NativeQuickActionGestureContract` maps step-previous double tap to `STEP_PREVIOUS_TO_START`, and marker long press to `SHOW_MARKER_LIST`.
- The native screen dispatches those distinct commands; single-tap previous uses the nearest earlier clip start/end boundary without a synthetic zero boundary.
- `NativeQuickActionGestureContractTest` covers the gesture mappings and previous-boundary behavior.
- The four actions that still use the legacy editor (Import, Home, Images, AI) now have an explicit allowlisted route: the Activity restores the still-live WebView editor, checks that the current document is the local editor page, then clicks the matching existing `.cbtn[data-act]` control. Home therefore retains the existing unsaved-work confirmation and project-list navigation instead of merely revealing the old editor without performing the requested action.
- A JVM route test verifies the four legacy selectors and that native actions do not fall through to the WebView route.

**Updated disposition:** gesture parity is implemented at source level; the four listed actions remain intentionally delegated to the retained WebView and are not counted as native conversions. The quick-action strip and S5 migration remain incomplete overall. The latest GitHub-content commits do not yet have associated Actions runs; Android compilation, JVM execution, CI, and physical-device gesture verification are still pending.
