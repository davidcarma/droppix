# Brightness / Contrast Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Client-side brightness + contrast adjustment of the streamed image, applied live (no reconnect) — Android via GL shader uniforms, Linux via a luma-plane transform.

**Architecture:** Android adds `uBrightness`/`uContrast` uniforms to the existing GL fragment shader (the render stage left a hook). Linux applies a pure `adjust_luma()` per-pixel on the decoded Y plane. Both read `@Volatile`/plain-int fields per frame, so a slider change is pushed straight to the renderer/decoder without a reconnect. Two sliders per client. **Client-only — no host/protocol change.**

**Tech Stack:** Kotlin/Android (GLES2), Qt6 C++ (Linux client), Gradle + CMake.

## Global Constraints

- **Client-only** — no `host/` or protocol change. Do not touch HELLO/protocol/version.
- **Value mapping (must match across clients):** `uBrightness = brightness/200f` (brightness −100..100 → ≈ ±0.5); `uContrast = contrast/100f` (contrast 0..200 → 0..2, 100 = neutral 1.0). Linux luma mirrors this on 0..255: `adjust_luma(y,b,c) = clamp((y-128)*c/100 + 128 + b*255/200, 0, 255)`.
- **Defaults are neutral:** brightness `0`, contrast `100`. At neutral the image must be byte-identical to no-adjustment (Linux keeps the plain `memcpy` fast path; the shader formula is identity at 0/100).
- **Live-apply:** brightness/contrast are pushed to the renderer/decoder directly and are NOT added to any reconnect-triggering condition.
- **Build/test envs** (repo on CIFS no-exec mount):
  - Android: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew <task>'` — `sh gradlew` (not `./gradlew`), set `ANDROID_HOME`.
  - Linux client: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build <-R filter> --output-on-failure'`
- Work on branch `feat/brightness-contrast` (off `master`). Commit after each task.

---

### Task 1: Android `AppSettings` — brightness + contrast

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/settings/AppSettings.kt`
- Test: `android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt`

**Interfaces:**
- Produces: `AppSettings(..., brightness: Int = 0, contrast: Int = 100)` (appended after `flipHorizontal`); `SettingsStore` persists keys `brightness`, `contrast`.

- [ ] **Step 1: Write the failing test** — add to `AppSettingsTest.kt`

```kotlin
@Test fun brightnessContrastDefaults() {
    val s = AppSettings()
    assertEquals(0, s.brightness); assertEquals(100, s.contrast)
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*AppSettingsTest*"'`
Expected: FAIL.

- [ ] **Step 3: Implement.** Append `val brightness: Int = 0, val contrast: Int = 100` to the `AppSettings` data class (after `flipHorizontal`). In `SettingsStore.load()` add `brightness = prefs.getInt("brightness", 0), contrast = prefs.getInt("contrast", 100)`; in `save()` add `.putInt("brightness", s.brightness).putInt("contrast", s.contrast)`.

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/settings/AppSettings.kt android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt
git commit -m "feat(android): AppSettings gains brightness + contrast"
```

---

### Task 2: Android GL shader — uBrightness/uContrast

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt`

**Interfaces:**
- Produces: `GlDisplayView` gains `@Volatile var brightness: Int = 0`, `@Volatile var contrast: Int = 100`.

- [ ] **Step 1: Read the shader + uniform pattern.** In `GlDisplayView.kt`, read the `FRAG` shader string (the `// T2 hook` comment), and how `uTexMatrix` is: declared as a renderer field, its location fetched in `onSurfaceCreated` via `glGetUniformLocation`, and uploaded in `onDrawFrame`. Mirror that for two float uniforms.

- [ ] **Step 2: Implement.**
  - `FRAG`: add the two uniforms and apply them (replacing the hook comment):
    ```glsl
    uniform samplerExternalOES uTex;
    uniform float uBrightness;
    uniform float uContrast;
    ...
    vec4 c = texture2D(uTex, vTexCoord);
    c.rgb = (c.rgb - 0.5) * uContrast + 0.5 + uBrightness;
    gl_FragColor = vec4(clamp(c.rgb, 0.0, 1.0), c.a);
    ```
  - Add `@Volatile var brightness: Int = 0` and `@Volatile var contrast: Int = 100` next to `flipHorizontal`.
  - In the renderer: add fields `private var uBrightness = 0` / `private var uContrast = 0`; in `onSurfaceCreated` after the other `glGetUniformLocation` calls: `uBrightness = GLES20.glGetUniformLocation(program, "uBrightness"); uContrast = GLES20.glGetUniformLocation(program, "uContrast")`. In `onDrawFrame` after binding the program/texture: `GLES20.glUniform1f(uBrightness, brightness / 200f); GLES20.glUniform1f(uContrast, contrast / 100f)`.

- [ ] **Step 3: Build (compiles; GL not unit-testable — on-device in Task 7)**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -6'`
Expected: `BUILD SUCCESSFUL`. (At default 0/100 the formula is identity: `(c-0.5)*1 + 0.5 + 0 = c`.)

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt
git commit -m "feat(android): GL shader brightness/contrast uniforms"
```

---

### Task 3: Android Settings sliders + live-apply

**Files:**
- Modify: `android/app/src/main/res/layout/activity_settings.xml`
- Modify: `android/app/src/main/java/com/droppix/app/ui/SettingsActivity.kt`
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt`

**Interfaces:**
- Consumes: `AppSettings.brightness/contrast` (Task 1); `GlDisplayView.brightness/contrast` (Task 2).

- [ ] **Step 1: Layout.** In `activity_settings.xml`, before the Save button, add two rows. Each: a label `TextView` (`"Brightness"` / `"Contrast"`, style like the "Resolution" label `#9aa5b1`), a `SeekBar` (`@+id/brightness_seek` / `@+id/contrast_seek`, `android:max="200"`, `layout_marginBottom="16dp"`), and a small value `TextView` (`@+id/brightness_val` / `@+id/contrast_val`, `#e5e7eb`) to show the number.

- [ ] **Step 2: `SettingsActivity` wiring.** After the flip switch:
```kotlin
val brightnessSeek = findViewById<SeekBar>(R.id.brightness_seek)
val brightnessVal = findViewById<TextView>(R.id.brightness_val)
brightnessSeek.progress = cur.brightness + 100                 // stored -100..100 -> 0..200
brightnessVal.text = cur.brightness.toString()
brightnessSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
    override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) { brightnessVal.text = (p - 100).toString() }
    override fun onStartTrackingTouch(sb: SeekBar) {} ; override fun onStopTrackingTouch(sb: SeekBar) {}
})
val contrastSeek = findViewById<SeekBar>(R.id.contrast_seek)
val contrastVal = findViewById<TextView>(R.id.contrast_val)
contrastSeek.progress = cur.contrast                            // stored 0..200
contrastVal.text = cur.contrast.toString()
contrastSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
    override fun onProgressChanged(sb: SeekBar, p: Int, fromUser: Boolean) { contrastVal.text = p.toString() }
    override fun onStartTrackingTouch(sb: SeekBar) {} ; override fun onStopTrackingTouch(sb: SeekBar) {}
})
```
Extend the Save `store.save(AppSettings(...))` call with the two new trailing args (matching field order — brightness, contrast last): `brightnessSeek.progress - 100, contrastSeek.progress`.

- [ ] **Step 3: `StreamActivity` live-apply.**
  - In `startStreaming()`, after loading `settings` and setting `surface.flipHorizontal`, add `surface.brightness = settings.brightnessValue; surface.contrast = settings.contrast` (use the actual field names: `settings.brightness`, `settings.contrast`).
  - In `onResume()` (after `super.onResume()` and the existing surface-listener re-attach): reload and push directly, so a brightness/contrast-only change applies WITHOUT a reconnect —
    ```kotlin
    val s = com.droppix.app.settings.SettingsStore(this).load()
    surface.brightness = s.brightness
    surface.contrast = s.contrast
    ```
    (This runs every resume; it's a cheap field set and doesn't trigger streaming restart. Do NOT add brightness/contrast to any reconnect condition.)

- [ ] **Step 4: Build**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -6'`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/res/layout/activity_settings.xml android/app/src/main/java/com/droppix/app/ui/SettingsActivity.kt android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt
git commit -m "feat(android): Brightness/Contrast sliders (live-applied)"
```

---

### Task 4: Linux `adjust_luma` helper + tests

**Files:**
- Modify: `client/src/video_decoder.h`, `client/src/video_decoder.cpp`
- Test: `client/tests/test_video_decoder_flip.cpp` (this file already includes `video_decoder.h` and the target already compiles `video_decoder.cpp`)

**Interfaces:**
- Produces: `int droppix::adjust_luma(int y, int brightness, int contrast);`

- [ ] **Step 1: Write the failing tests** — add to `client/tests/test_video_decoder_flip.cpp`

```cpp
TEST(AdjustLuma, NeutralIsIdentity) {
  for (int y : {0, 60, 128, 200, 255}) EXPECT_EQ(droppix::adjust_luma(y, 0, 100), y);
}
TEST(AdjustLuma, BrightnessShifts) {
  EXPECT_GT(droppix::adjust_luma(100, 50, 100), 100);
  EXPECT_LT(droppix::adjust_luma(150, -50, 100), 150);
}
TEST(AdjustLuma, ContrastAboutMid) {
  EXPECT_GT(droppix::adjust_luma(200, 0, 200), 200);          // >128 pushed up
  EXPECT_LT(droppix::adjust_luma(60, 0, 200), 60);            // <128 pushed down
  int lo = droppix::adjust_luma(200, 0, 50);
  EXPECT_GT(lo, 128); EXPECT_LT(lo, 200);                     // contrast<100 pulls toward 128
}
TEST(AdjustLuma, Clamps) {
  EXPECT_EQ(droppix::adjust_luma(250, 100, 200), 255);
  EXPECT_EQ(droppix::adjust_luma(10, -100, 100), 0);
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build -R AdjustLuma --output-on-failure'`
Expected: FAIL (undefined `adjust_luma`).

- [ ] **Step 3: Implement.** Declare in `video_decoder.h` (near `make_frame_format`): `int adjust_luma(int y, int brightness, int contrast);`. Define in `video_decoder.cpp`:
```cpp
int adjust_luma(int y, int brightness, int contrast) {
  int v = (y - 128) * contrast / 100 + 128 + brightness * 255 / 200;
  return v < 0 ? 0 : (v > 255 ? 255 : v);
}
```

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS (all 4 AdjustLuma tests).

- [ ] **Step 5: Commit**

```bash
git add client/src/video_decoder.h client/src/video_decoder.cpp client/tests/test_video_decoder_flip.cpp
git commit -m "feat(client): adjust_luma brightness/contrast helper"
```

---

### Task 5: Linux `VideoDecoder` — apply brightness/contrast to the Y plane

**Files:**
- Modify: `client/src/video_decoder.h`, `client/src/video_decoder.cpp`

**Interfaces:**
- Consumes: `adjust_luma` (Task 4).
- Produces: `VideoDecoder::setBrightness(int)`, `VideoDecoder::setContrast(int)`.

- [ ] **Step 1: Read `submit`'s plane-copy loop** (`video_decoder.cpp:67-79`) — the `for (plane...)` with `memcpy(dst + row*dstStride, src + row*srcStride, copyBytes)`. Plane 0 is Y (`copyBytes` = width bytes/row).

- [ ] **Step 2: Implement.** Add `int brightness_ = 0; int contrast_ = 100;` members + `void setBrightness(int b) { brightness_ = b; }` / `void setContrast(int c) { contrast_ = c; }` (header). In the plane loop, replace the single `memcpy` with:
```cpp
if (plane == 0 && (brightness_ != 0 || contrast_ != 100)) {
  for (int row = 0; row < planeH; ++row) {
    const uint8_t* s = src + row * srcStride;
    uint8_t* d = dst + row * dstStride;
    for (int col = 0; col < copyBytes; ++col) d[col] = static_cast<uint8_t>(adjust_luma(s[col], brightness_, contrast_));
  }
} else {
  for (int row = 0; row < planeH; ++row)
    std::memcpy(dst + row * dstStride, src + row * srcStride, copyBytes);
}
```
(Keep whatever the existing loop's `copyBytes`/`planeH`/`srcStride`/`dstStride` definitions are — only the per-row body changes. At neutral 0/100 the `else` branch is the original `memcpy`.) `brightness_`/`contrast_` are plain ints written by the UI thread and read by the decode thread — a benign race (a torn value can't occur for an aligned int; worst case one frame uses the previous value). No lock needed.

- [ ] **Step 3: Build + client suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'`
Expected: clean build; client suite green (no regressions).

- [ ] **Step 4: Commit**

```bash
git add client/src/video_decoder.h client/src/video_decoder.cpp
git commit -m "feat(client): apply brightness/contrast to decoded luma"
```

---

### Task 6: Linux dialog sliders + live-apply wire

**Files:**
- Modify: `client/gui/settings_dialog.h`, `client/gui/settings_dialog.cpp`
- Modify: `client/gui/main_window.cpp`

**Interfaces:**
- Consumes: `ClientSettings.brightness/contrast`, `VideoDecoder::setBrightness/setContrast` (Task 5).
- Note: `ClientSettings.brightness = 0` / `contrast = 100` fields + persistence are part of THIS task (add them to `client/src/client_settings.{h,cpp}` keys `brightness`/`contrast` if not already present — mirror the existing `bitrate_kbps` field + a round-trip test in `client/tests/test_client_settings.cpp`).

- [ ] **Step 1: `ClientSettings` fields + test.** Add `int brightness = 0; int contrast = 100;` to `ClientSettings`; `load()`/`save()` keys `brightness`(0)/`contrast`(100). Add a `test_client_settings.cpp` round-trip (`brightness=-40`, `contrast=150` → save → load), mirroring the bitrate test. Run the ClientSettings tests to confirm.

- [ ] **Step 2: Dialog sliders.** In `ClientSettingsDialog`, add `QSlider* brightness_` (Horizontal, range −100..100) + `QSlider* contrast_` (Horizontal, range 0..200), each with a value `QLabel` updated via the slider's `valueChanged` signal. Seed from `s.brightness`/`s.contrast`; in `result()`, `out.brightness = brightness_->value(); out.contrast = contrast_->value();`. Add them to the form layout like the other controls.

- [ ] **Step 3: main_window live-apply.** Where the decoder is set up in `netThreadMain` (alongside `setFlipHorizontal`), add `decoder_->setBrightness(settings_.brightness); decoder_->setContrast(settings_.contrast);`. In `onSettingsAction`, AFTER saving `settings_`, always call `decoder_->setBrightness(settings_.brightness); decoder_->setContrast(settings_.contrast);` (guard `if (decoder_)`), so a change applies live. Do NOT add `brightness`/`contrast` to the `changed` reconnect condition.

- [ ] **Step 4: Build + client suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'`
Expected: clean build; client suite green (incl. the new ClientSettings brightness/contrast round-trip + the AdjustLuma tests).

- [ ] **Step 5: Commit**

```bash
git add client/src/client_settings.h client/src/client_settings.cpp client/tests/test_client_settings.cpp client/gui/settings_dialog.h client/gui/settings_dialog.cpp client/gui/main_window.cpp
git commit -m "feat(client): Brightness/Contrast sliders (live-applied)"
```

---

### Task 7: Verification

**Files:** none.

- [ ] **Step 1: Full builds + suites**

```
distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'
distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest assembleDebug'
```
Expected: client suite green (incl. `AdjustLuma.*` + ClientSettings round-trips); Android unit tests green; APK builds.

- [ ] **Step 2: On-device (tablet + Linux client).** Brightness slider brightens/darkens the streamed image; Contrast raises/lowers contrast; both apply **live** (no stream blink / reconnect); neutral (0 / 100) = unchanged image; other settings unaffected.

- [ ] **Step 3: Commit any fixes; otherwise done.**

---

## Self-review notes

- **Spec coverage:** Android settings (T1) + shader (T2) + sliders/live-apply (T3); Linux helper (T4) + decoder apply (T5) + settings/sliders/live-apply (T6); testing per-task + T7. `ClientSettings` fields folded into T6 Step 1 (they're only consumed by the Linux dialog/decoder).
- **Value mapping consistent:** `uBrightness=b/200`, `uContrast=c/100` (Android) mirror `adjust_luma`'s `(y-128)*c/100 + 128 + b*255/200` (Linux). Neutral 0/100 is identity on both (shader: `(c-0.5)*1+0.5+0`; Linux: keeps `memcpy`).
- **Type consistency:** `brightness`/`contrast` Int everywhere; `GlDisplayView.brightness/contrast`, `VideoDecoder::setBrightness/setContrast(int)`, `adjust_luma(int,int,int)` consistent.
- **Live-apply:** not added to any reconnect condition (Android onResume pushes fields; Linux onSettingsAction pushes to the decoder). Neutral keeps the fast path (no per-pixel cost when off).
