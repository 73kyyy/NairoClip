# S5 Native UI Responsive Follow-up Audit — 2026-11-23

## Scope and evidence

Repository: `73kyyy/NairoClip`
PR: #32 (`work/v3.3.3-s5-native-editor-foundation`)
Reviewed HEAD: `fc4ed21752852d640f7e622f6936abdec52e8e57`

This is a static source audit of the current native Compose/View surfaces. It is not a runtime layout test, screenshot test, Android device test, or approval to switch the application entry point. No protected persistence, Rational/time, Activity, Manifest, JNI/C ABI, or codec source was changed.

## Confirmed CI evidence at this HEAD

- S5 Android Validation run `45103367804`: successful.
  - Android NDK arm64 core build and JNI translation-unit compile: successful.
  - Android Kotlin compilation and JVM unit tests: successful.
  - Host CMake build and targeted CTest: successful.
- CI run `45103367879`: successful.
  - UBSan build/diagnostic tests: successful.
  - ASan build and test suite: successful.
  - build-and-test, including native full tests and targeted checks: successful.

These results establish the listed build/test jobs only. They do not establish Compose visual correctness, font-scale behavior, IME behavior, Surface lifecycle on physical devices, or cross-device acceptance.

## Findings

### P0 — editor vertical-space allocation is not yet responsive

Evidence: `NativeEditorScreen.kt`.

- The root editor is a full-size `Column` with a weighted preview, quick-action strip, a fixed `218.dp` timeline panel, selected-clip context, and potentially several parameter panels.
- The fixed timeline height does not account for short landscape windows, split-screen, large font scale, or the simultaneous presence of a parameter panel.
- The root shown in this file does not itself apply a scroll strategy or a window-inset policy. A future host may provide some insets, but that is not established by this composable.
- Consequence: on short-height windows, the weighted preview can collapse while fixed-height and content-driven regions compete for the remaining height; controls may become compressed or inaccessible.

Required direction:
- Introduce a window-size-aware editor layout policy before finalizing the root composition.
- Give preview, timeline, and parameter area explicit minimum/maximum constraints and a defined compact mode.
- In compact height, allow the parameter area to scroll/collapse while keeping transport and timeline interactions reachable.
- Keep the requested horizontal level-1/level-2 tool-strip behavior; do not replace it with a drawer.

### P0 — timeline geometry and text are density-scaled but not accessibility-font-scaled

Evidence: `NativeTimelineView.kt`.

- Ruler height (27dp), track label gutter (120dp), lane height (47dp), and row gap (4dp) are converted using display density.
- Canvas text sizes are also multiplied by density (for example, 9f/13f/10f), not by Android scaled density or a Compose-provided font-scale-aware dimension.
- The same geometry values drive drawing and hit-testing, which is good for parity, but the values are fixed rather than derived from the available viewport and accessibility font scale.
- Consequence: at large system font scales, timeline labels do not grow consistently with surrounding Compose text; at narrow widths the fixed 120dp gutter consumes a large share of the visible timeline.

Required direction:
- Centralize timeline geometry in one immutable metrics object consumed by both draw and hit-test paths.
- Derive text metrics from scaled density/font scale, with bounded compact fallbacks.
- Make label gutter and lane/ruler metrics adaptive while preserving exact draw/hit-test parity.
- Add geometry assertions for minimum touch targets and non-overlapping lane/control hit regions.

### P1 — home screen quick actions assume a two-column arrangement

Evidence: `NativeHomeSettingsScreens.kt`.

- Quick actions are authored as two fixed pairs of cards in full-width Rows, each card taking equal weight.
- Text sizes and paddings are fixed sp values/dp values; card titles are single-line.
- The screen uses a LazyColumn for vertical content, which is a useful baseline, but the quick-action row has no width-class branch or compact reflow in the reviewed implementation.
- Consequence: narrow windows and large font scale can cause text compression/truncation even though the outer page scrolls.

Required direction:
- Use available width to select one or two columns, and allow cards to reflow vertically at compact widths.
- Avoid making critical labels single-line where that hides meaning; provide sensible wrapping and minimum card height.
- Keep a bounded content width on large windows rather than stretching every card indefinitely.

### P1 — account/support forms need explicit IME and short-height policy

Evidence: `NativeAccountSupportScreens.kt` reviewed sections.

- Feedback form content uses a full-height Column with a weighted description field and fixed contact/status/action rows.
- Login and account forms require explicit keyboard/inset handling and scroll-to-focused-field behavior to guarantee the submit/continue action remains reachable when the IME is visible.
- A full-size LazyColumn in account settings helps vertical scrolling, but does not by itself prove that focused fields are brought above the IME.

Required direction:
- Apply a consistent IME-aware scroll/bring-into-view policy to login, feedback, and other text-entry surfaces.
- Verify submit/continue actions remain visible or reachable at the smallest supported height with the keyboard open.
- Test both gesture navigation and three-button navigation insets.

### P1 — subtitle panel has compact typography and fixed internal spacing

Evidence: `NativeAutoSubtitlePanel.kt`.

- Panel padding is 12dp, vertical spacing 9dp, corner radius 12dp, and text sizes 9–12sp.
- Start/cancel actions share a Row; no narrow-width alternate arrangement is defined in this component.
- Status/error strings are not explicitly bounded or given a wrapping/scroll strategy here.

Required direction:
- Promote these values to shared component tokens and use accessible typography roles.
- Allow action buttons to wrap/stack on narrow widths.
- Ensure long localized status/error text wraps and does not push actions off-screen.

### P2 — preview Surface sizing must remain aspect-ratio-safe

Evidence: `NativePreviewSurfaceController.kt` and editor preview host usage.

- The controller validates positive buffer dimensions and serializes lifecycle/render operations, but it does not choose the preview content aspect ratio; that belongs to the preview host/layout.
- Responsive host integration must preserve the project canvas aspect ratio and letterbox/crop according to an explicit preview policy, never stretch the frame to arbitrary window bounds.
- Surface size changes, rotations, window resizing, and detach/reattach need physical-device stress coverage.

## Cross-cutting implementation requirements

1. Establish one shared window/layout policy for compact, medium, and expanded widths plus compact/regular heights.
2. Establish a shared token set for spacing, corner radii, minimum touch targets, icon boxes, typography, and content maximum widths.
3. Apply system-bar, display-cutout, gesture-navigation, and IME insets at the eventual native host boundary; avoid each screen inventing its own inset math.
4. Preserve aspect ratios for circular/square controls and media; do not size them with independent unconstrained width and height.
5. On compact windows, prefer scroll/reflow/stacking over clipping, overlap, or reducing touch targets below the agreed minimum.
6. Test font scales including 1.0x, 1.3x, and 2.0x, display-size changes, portrait/landscape, split-screen, and fold/unfold/window resize.
7. Add Compose UI tests for measured bounds and semantics, screenshot/visual regression tests for representative window classes, and instrumented device tests for IME/insets/Surface lifecycle.

## Acceptance checklist

- [ ] No overlapping or clipped interactive controls in compact/medium/expanded windows.
- [ ] Circle and square controls retain equal width/height; icons/media preserve source aspect ratio.
- [ ] Timeline drawing and hit-testing consume the same adaptive geometry snapshot.
- [ ] Timeline and surrounding UI typography respond coherently to system font scale.
- [ ] Editor preview, transport, timeline, and active parameter panel remain reachable in short-height layouts.
- [ ] Login/feedback actions remain reachable with IME visible.
- [ ] System bars, cutouts, gesture areas, and navigation mode are covered.
- [ ] Portrait, landscape, split-screen, foldable/window resize, and font/display-size matrix passes.
- [ ] Automated layout/semantics and visual regression tests pass.
- [ ] Physical Android device validation is recorded separately from CI.

## Gate status

This audit does not change S1-3, S3-1, A-5, A-6, A-12, S1-8D, or any other registered approval/dependency gate. It does not authorize MainActivity/Manifest entrypoint changes or WebView removal. S5 runtime integration remains subject to its existing prerequisites and approvals.

## 2026-11-28 implementation follow-up — compact-height editor policy

The P0 vertical-space finding now has an initial source implementation:

- Added `NativeEditorResponsiveSpec` with compact/medium/regular height classes. The reference-height class retains the established 282dp timeline; shorter windows progressively reduce timeline height and reserve a bounded viewport for parameter content.
- `NativeEditorScreen` now reads the available `BoxWithConstraints.maxHeight`, applies a minimum preview height, and sizes the timeline from the responsive metrics.
- Selected-clip controls, keyframe/transform panels, marker list and preview-prewarm status now share a bounded vertically scrollable region. The transport row and bottom tool strip remain outside that scroll region so they remain reachable.
- Added JVM tests for compact/medium/regular breakpoints and non-finite height fallback.

**Remaining gaps:** this is a first height-class policy, not full responsive acceptance. Timeline Canvas typography is still density-scaled rather than font-scale-aware; home quick-action reflow, IME/bring-into-view, insets, subtitle-panel compact behavior, split-screen/foldable and landscape screenshot tests, and physical-device validation remain open. The new source/tests have no associated GitHub Actions run yet, so no build/test pass is claimed.
