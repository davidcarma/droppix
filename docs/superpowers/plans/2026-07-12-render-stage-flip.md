# Android GL Render Stage + Horizontal Flip — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a client-side horizontal flip of the streamed image — via a new Android GL render stage (which also sets up T2 brightness/contrast) and a trivial Linux-client mirror.

**Architecture:** On Android, replace the zero-copy `MediaCodec → SurfaceView` path with `MediaCodec → SurfaceTexture(external OES) → GLSurfaceView` drawing a fullscreen quad through a `samplerExternalOES` shader; flip is a texture-matrix transform. On Linux, set `QVideoFrameFormat::setMirrored(true)` on pushed frames. Flip is a persisted client setting; **no host or protocol change**.

**Tech Stack:** Kotlin/Android (GLES 2.0, `GLSurfaceView`, `SurfaceTexture`), Qt6 C++ (Linux client), Gradle + CMake.

## Global Constraints

- **Client-only display transform** — no `host/` or protocol change in this plan. Do not touch HELLO/`protocol`/`stream_daemon`.
- **Flip = horizontal mirror only** (the rear-projection/mirror case). No vertical flip.
- **Video-only flip:** do NOT remap touch coordinates when flipped (documented view-only behavior).
- **T2 hook:** the Android fragment shader must sample the external texture in a form where brightness/contrast can later be added as `uniform float` on the sampled color — leave a comment marking the spot; do not implement them now.
- **Android GL threading:** the `Renderer` runs on the GL thread. When the decode `Surface` becomes ready in `onSurfaceCreated`, deliver it to the activity via `runOnUiThread(...)` (the existing `onSurfaceReady → startStreaming` path expects the UI thread).
- **Build/test environments** (verified this session; repo on a CIFS no-exec mount):
  - Android: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew <task>'` — always `sh gradlew` (not `./gradlew`) and set `ANDROID_HOME`.
  - Linux client: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build <-R filter> --output-on-failure'`
- Work on the current branch `feat/render-stage-flip` (stacked on `feat/client-settings-quality`). Commit after each task.

---

### Task 1: Android `AppSettings.flipHorizontal`

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/settings/AppSettings.kt`
- Test: `android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt`

**Interfaces:**
- Produces: `AppSettings(..., flipHorizontal: Boolean = false)` (appended after `showOverlay`); `SettingsStore` persists key `flip_h`.

- [ ] **Step 1: Write the failing test** — add to `AppSettingsTest.kt`

```kotlin
@Test fun flipDefault() { assertFalse(AppSettings().flipHorizontal) }
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*AppSettingsTest*"'`
Expected: FAIL — no `flipHorizontal`.

- [ ] **Step 3: Implement.** Append `val flipHorizontal: Boolean = false` to the `AppSettings` data class (after `showOverlay`). In `SettingsStore.load()` add `flipHorizontal = prefs.getBoolean("flip_h", false)`; in `save()` add `.putBoolean("flip_h", s.flipHorizontal)`.

- [ ] **Step 4: Run to verify PASS** — same command as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/settings/AppSettings.kt android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt
git commit -m "feat(android): AppSettings gains flipHorizontal"
```

---

### Task 2: Android `GlDisplayView` (GL render pipeline)

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt`
- Read (to port from): `android/app/src/main/java/com/droppix/app/ui/DisplaySurfaceView.kt`

**Interfaces:**
- Produces: `class GlDisplayView : GLSurfaceView` exposing the SAME public surface/touch API the current `DisplaySurfaceView` exposes (its `SurfaceListener` with `onSurfaceReady(surface: Surface)`, its `TouchListener`, `setSurfaceListener(...)`, `setTouchListener(...)`), plus `@Volatile var flipHorizontal: Boolean = false`. `onSurfaceReady` now fires from the GL thread with a `SurfaceTexture`-backed `Surface`.

- [ ] **Step 1: Read `DisplaySurfaceView.kt` fully.** Note its `SurfaceListener`/`TouchListener` interfaces, `setSurfaceListener`/`setTouchListener`, and the entire `onTouchEvent` → contacts logic (normalization, throttling, multi-touch, right-click gesture). This exact touch code is copied verbatim into `GlDisplayView` (a `GLSurfaceView` is also a `SurfaceView`, so `onTouchEvent` behaves identically).

- [ ] **Step 2: Create `GlDisplayView.kt`.** Structure: the ported interfaces + touch code, plus the GL renderer below. Full renderer code:

```kotlin
package com.droppix.app.ui

import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.opengl.Matrix
import android.util.AttributeSet
import android.view.Surface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class GlDisplayView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null)
    : GLSurfaceView(context, attrs) {

    // ---- ported from DisplaySurfaceView (verbatim): SurfaceListener, TouchListener,
    // ---- setSurfaceListener/setTouchListener, and the full onTouchEvent contacts logic ----
    // (copy those members here unchanged)

    @Volatile var flipHorizontal: Boolean = false

    private val renderer = GlRenderer()

    init {
        setEGLContextClientVersion(2)
        setRenderer(renderer)
        renderMode = RENDERMODE_WHEN_DIRTY
    }

    private inner class GlRenderer : Renderer {
        private var program = 0
        private var aPosition = 0
        private var aTexCoord = 0
        private var uTexMatrix = 0
        private var uTex = 0
        private var texId = 0
        private var surfaceTexture: SurfaceTexture? = null
        private val stMatrix = FloatArray(16)
        private val texMatrix = FloatArray(16)
        private val mirror = FloatArray(16)

        // Fullscreen triangle-strip quad: clip-space positions + texcoords.
        private val quad: FloatBuffer = floatBuf(floatArrayOf(
            //   x,    y,     u, v
            -1f, -1f,   0f, 0f,
             1f, -1f,   1f, 0f,
            -1f,  1f,   0f, 1f,
             1f,  1f,   1f, 1f))

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            program = buildProgram(VERT, FRAG)
            aPosition = GLES20.glGetAttribLocation(program, "aPosition")
            aTexCoord = GLES20.glGetAttribLocation(program, "aTexCoord")
            uTexMatrix = GLES20.glGetUniformLocation(program, "uTexMatrix")
            uTex = GLES20.glGetUniformLocation(program, "uTex")

            val ids = IntArray(1); GLES20.glGenTextures(1, ids, 0); texId = ids[0]
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texId)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)

            val st = SurfaceTexture(texId)
            st.setOnFrameAvailableListener { requestRender() }
            surfaceTexture = st
            val surface = Surface(st)
            // Hand the decode Surface to the activity on the UI thread.
            post { surfaceListener?.onSurfaceReady(surface) }
        }

        override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) = GLES20.glViewport(0, 0, w, h)

        override fun onDrawFrame(gl: GL10?) {
            val st = surfaceTexture ?: return
            st.updateTexImage()
            st.getTransformMatrix(stMatrix)
            if (flipHorizontal) {
                // mirror about S=0.5, then apply the codec's stMatrix: texMatrix = stMatrix * mirror
                Matrix.setIdentityM(mirror, 0)
                Matrix.translateM(mirror, 0, 0.5f, 0f, 0f)
                Matrix.scaleM(mirror, 0, -1f, 1f, 1f)
                Matrix.translateM(mirror, 0, -0.5f, 0f, 0f)
                Matrix.multiplyMM(texMatrix, 0, stMatrix, 0, mirror, 0)
            } else {
                System.arraycopy(stMatrix, 0, texMatrix, 0, 16)
            }
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
            GLES20.glUseProgram(program)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, texId)
            GLES20.glUniform1i(uTex, 0)
            GLES20.glUniformMatrix4fv(uTexMatrix, 1, false, texMatrix, 0)
            quad.position(0)
            GLES20.glVertexAttribPointer(aPosition, 2, GLES20.GL_FLOAT, false, 16, quad)
            GLES20.glEnableVertexAttribArray(aPosition)
            quad.position(2)
            GLES20.glVertexAttribPointer(aTexCoord, 2, GLES20.GL_FLOAT, false, 16, quad)
            GLES20.glEnableVertexAttribArray(aTexCoord)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        }
    }

    companion object {
        private const val VERT = """
            attribute vec4 aPosition;
            attribute vec4 aTexCoord;
            uniform mat4 uTexMatrix;
            varying vec2 vTexCoord;
            void main() {
                gl_Position = aPosition;
                vTexCoord = (uTexMatrix * aTexCoord).xy;
            }
        """
        private const val FRAG = """
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 vTexCoord;
            uniform samplerExternalOES uTex;
            void main() {
                vec4 c = texture2D(uTex, vTexCoord);
                // T2 hook: brightness/contrast go here, e.g.
                //   c.rgb = (c.rgb - 0.5) * uContrast + 0.5 + uBrightness;
                gl_FragColor = c;
            }
        """
        private fun floatBuf(a: FloatArray): FloatBuffer =
            ByteBuffer.allocateDirect(a.size * 4).order(ByteOrder.nativeOrder())
                .asFloatBuffer().apply { put(a); position(0) }
        private fun buildProgram(vs: String, fs: String): Int {
            fun sh(type: Int, src: String): Int {
                val s = GLES20.glCreateShader(type); GLES20.glShaderSource(s, src); GLES20.glCompileShader(s)
                val ok = IntArray(1); GLES20.glGetShaderiv(s, GLES20.GL_COMPILE_STATUS, ok, 0)
                check(ok[0] != 0) { "shader compile: " + GLES20.glGetShaderInfoLog(s) }
                return s
            }
            val p = GLES20.glCreateProgram()
            GLES20.glAttachShader(p, sh(GLES20.GL_VERTEX_SHADER, vs))
            GLES20.glAttachShader(p, sh(GLES20.GL_FRAGMENT_SHADER, fs))
            GLES20.glLinkProgram(p)
            val ok = IntArray(1); GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0)
            check(ok[0] != 0) { "program link: " + GLES20.glGetProgramInfoLog(p) }
            return p
        }
    }
}
```
Replace the `// ported from DisplaySurfaceView` comment block with the actual copied members (`surfaceListener`/`touchListener` fields, the two interfaces, the setters, and `onTouchEvent`). The `post { ... }` in `onSurfaceCreated` runs on the UI thread (View.post), satisfying the threading constraint.

- [ ] **Step 3: Build** (compiles the new file; not wired yet)

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew compileDebugKotlin 2>&1 | tail -8'`
Expected: `BUILD SUCCESSFUL` (the file compiles standalone; unused until Task 3).

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/GlDisplayView.kt
git commit -m "feat(android): GlDisplayView — GL render pipeline (SurfaceTexture + shader, flip-capable)"
```

---

### Task 3: Rewire `StreamActivity` + layout to `GlDisplayView`

**Files:**
- Modify: `android/app/src/main/res/layout/activity_stream.xml` (swap the view class)
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt`
- Delete: `android/app/src/main/java/com/droppix/app/ui/DisplaySurfaceView.kt`

**Interfaces:**
- Consumes: `GlDisplayView` (Task 2), `AppSettings.flipHorizontal` (Task 1).

- [ ] **Step 1: Layout.** In `activity_stream.xml`, change the `@+id/surface` element's class from `com.droppix.app.ui.DisplaySurfaceView` to `com.droppix.app.ui.GlDisplayView` (keep the id, `layout_width/height=match_parent`, and its position in the FrameLayout under the overlay + settings button).

- [ ] **Step 2: StreamActivity.** Change the `surface` field type and `findViewById` to `GlDisplayView`. In `startStreaming()`, after loading `settings`, set `surface.flipHorizontal = settings.flipHorizontal`. Everything else (the `SurfaceListener`/`TouchListener` wiring, `onSurfaceReady → startStreaming`, `VideoDecoder(surface = <the onSurfaceReady Surface>, ...)`) is unchanged — `GlDisplayView` exposes the same API. Confirm `VideoDecoder` still receives the `Surface` delivered by `onSurfaceReady` (now the SurfaceTexture-backed one).

- [ ] **Step 3: Delete `DisplaySurfaceView.kt`** (fully subsumed). Grep for stragglers: `grep -rn "DisplaySurfaceView" android/app/src` must return zero hits.

- [ ] **Step 4: Build the APK**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -8'`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/res/layout/activity_stream.xml android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt
git rm android/app/src/main/java/com/droppix/app/ui/DisplaySurfaceView.kt
git commit -m "feat(android): stream through GlDisplayView; apply flipHorizontal; drop DisplaySurfaceView"
```

---

### Task 4: Android Settings — "Flip horizontal" switch

**Files:**
- Modify: `android/app/src/main/res/layout/activity_settings.xml`
- Modify: `android/app/src/main/java/com/droppix/app/ui/SettingsActivity.kt`

**Interfaces:**
- Consumes: `AppSettings.flipHorizontal` (Task 1).

- [ ] **Step 1: Layout.** In `activity_settings.xml`, add a "Flip horizontal" row copying the existing "Performance overlay" `LinearLayout` block verbatim, with label text "Flip horizontal" and `Switch @+id/flip_switch` (place it after the overlay row, before the Save button).

- [ ] **Step 2: SettingsActivity.** After the overlay switch wiring: `val flipSwitch = findViewById<Switch>(R.id.flip_switch); flipSwitch.isChecked = cur.flipHorizontal`. Extend the Save `AppSettings(...)` call with the new trailing arg `flipSwitch.isChecked` (matching the data-class field order — `flipHorizontal` is the last field).

- [ ] **Step 3: Build**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -6'`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/res/layout/activity_settings.xml android/app/src/main/java/com/droppix/app/ui/SettingsActivity.kt
git commit -m "feat(android): Flip-horizontal switch in Settings"
```

---

### Task 5: Linux client `ClientSettings.flip_horizontal`

**Files:**
- Modify: `client/src/client_settings.h`, `client/src/client_settings.cpp`
- Test: `client/tests/test_client_settings.cpp`

**Interfaces:**
- Produces: `ClientSettings.flip_horizontal` (default `false`); persisted key `flip_h`.

- [ ] **Step 1: Write the failing test** — add to `test_client_settings.cpp` (use the file's existing `QSettings::IniFormat` isolation idiom)

```cpp
TEST(ClientSettings, FlipDefaultAndRoundTrip) {
  droppix::ClientSettings s; EXPECT_FALSE(s.flip_horizontal);
  s.flip_horizontal = true; droppix::ClientSettingsStore::save(s);
  EXPECT_TRUE(droppix::ClientSettingsStore::load().flip_horizontal);
}
```

- [ ] **Step 2: Run to verify FAIL**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build -R ClientSettings --output-on-failure'`
Expected: FAIL — no `flip_horizontal`.

- [ ] **Step 3: Implement.** Add `bool flip_horizontal = false;` to `ClientSettings`. In `load()`: `s.flip_horizontal = q.value("flip_h", false).toBool();`. In `save()`: `q.setValue("flip_h", s.flip_horizontal);`.

- [ ] **Step 4: Run to verify PASS** — same as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add client/src/client_settings.h client/src/client_settings.cpp client/tests/test_client_settings.cpp
git commit -m "feat(client): ClientSettings gains flip_horizontal"
```

---

### Task 6: Linux client `VideoDecoder` mirror

**Files:**
- Modify: `client/src/video_decoder.h`, `client/src/video_decoder.cpp`
- Test: `client/tests/` (add `test_video_decoder_flip.cpp` if the decoder is unit-testable without a real stream; otherwise assert the format flag via a tiny helper — see Step 1)

**Interfaces:**
- Produces: `VideoDecoder::setFlipHorizontal(bool)`; produced `QVideoFrame`s carry `format().isMirrored() == flip`.

- [ ] **Step 1: Write the failing test.** The decoder needs a real H.264 NAL to produce a frame, which is heavy for a unit test. Instead, extract the format-building into a testable pure helper and test THAT:
  - Add a free function in `video_decoder.cpp` (declared in `.h`): `QVideoFrameFormat droppix::make_frame_format(int w, int h, bool mirrored);` that builds `QVideoFrameFormat(QSize(w,h), Format_YUV420P)` and calls `setMirrored(mirrored)`.
  - Test (`client/tests/test_video_decoder_flip.cpp`, add to the `droppix_client_tests` target in `client/CMakeLists.txt`):
```cpp
#include "video_decoder.h"
#include <gtest/gtest.h>
TEST(VideoDecoderFlip, FormatCarriesMirror) {
  EXPECT_FALSE(droppix::make_frame_format(1280, 720, false).isMirrored());
  EXPECT_TRUE(droppix::make_frame_format(1280, 720, true).isMirrored());
}
```

- [ ] **Step 2: Run to verify FAIL** (build the client tests)

Run: `distrobox enter droppix-dev -- bash -lc 'cmake -S client -B ~/droppix-client-build -DDROPPIX_CLIENT_BUILD_TESTS=ON && cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build -R VideoDecoderFlip --output-on-failure'`
Expected: FAIL — `make_frame_format` undefined.

- [ ] **Step 3: Implement.**
  - Implement `make_frame_format` as above.
  - Add `void setFlipHorizontal(bool f) { flip_ = f; }` + `bool flip_ = false;` to `VideoDecoder`.
  - In `submit(...)`, replace the inline `QVideoFrameFormat fmt(QSize(...), Format_YUV420P);` with `QVideoFrameFormat fmt = make_frame_format(frame_->width, frame_->height, flip_);` (keep the rest of the frame mapping unchanged).

- [ ] **Step 4: Run to verify PASS** — same as Step 2 (drop `--build`'s `-S` reconfigure isn't needed after the first). Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add client/src/video_decoder.h client/src/video_decoder.cpp client/tests/test_video_decoder_flip.cpp client/CMakeLists.txt
git commit -m "feat(client): VideoDecoder mirrors frames when flip enabled"
```

---

### Task 7: Linux client — Flip checkbox + wire

**Files:**
- Modify: `client/gui/settings_dialog.h`, `client/gui/settings_dialog.cpp` (a `QCheckBox`)
- Modify: `client/gui/main_window.cpp` (apply `settings_.flip_horizontal` to the decoder)

**Interfaces:**
- Consumes: `ClientSettings.flip_horizontal` (Task 5), `VideoDecoder::setFlipHorizontal` (Task 6).

- [ ] **Step 1: Read** how `ClientSettingsDialog` builds its checkboxes/combos + `result()`, and where `main_window.cpp` owns the `VideoDecoder` (`decoder_`) and applies settings at session start (`netThreadMain` / `startSession`).

- [ ] **Step 2: Dialog.** Add `QCheckBox* flip_` to `ClientSettingsDialog` (label "Flip horizontal"), seeded `flip_->setChecked(s.flip_horizontal)`; in `result()`, `out.flip_horizontal = flip_->isChecked();`. Mirror whatever the existing Audio/overlay-style checkbox does (if none, add a plain `QCheckBox` in the form layout).

- [ ] **Step 3: Wire.** In `main_window.cpp`, where the decoder is created/used for a session, call `decoder_->setFlipHorizontal(settings_.flip_horizontal)` before/at stream start, so a settings change (which reconnects) re-applies it.

- [ ] **Step 4: Build + client suite**

Run: `distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'`
Expected: clean build; client suite green.

- [ ] **Step 5: Commit**

```bash
git add client/gui/settings_dialog.h client/gui/settings_dialog.cpp client/gui/main_window.cpp
git commit -m "feat(client): Flip-horizontal checkbox applies mirror to the decoder"
```

---

### Task 8: Verification

**Files:** none.

- [ ] **Step 1: Full builds + suites**

```
distrobox enter droppix-dev -- bash -lc 'cmake --build ~/droppix-client-build -j && QT_QPA_PLATFORM=offscreen ctest --test-dir ~/droppix-client-build --output-on-failure'
distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest assembleDebug'
```
Expected: client suite green (incl. `ClientSettings.FlipDefaultAndRoundTrip`, `VideoDecoderFlip.FormatCarriesMirror`); Android unit tests green; APK builds.

- [ ] **Step 2: On-device (user's tablet).** Install the APK. Confirm: with flip OFF the stream renders normally through the new GL path (no regression, no added stutter, touch controls the host, the perf overlay still draws); toggle **Flip horizontal** ON → the image mirrors left-right; touch is NOT remapped (documented). Reconnect applies the change.

- [ ] **Step 3: Commit any fixes; otherwise done.**

---

## Self-review notes

- **Spec coverage:** Android GL pipeline (T2) + rewire/remove SurfaceView (T3); flip via `uTexMatrix` mirror (T2 `onDrawFrame`); Android setting + UI (T1, T4); Linux mirror via `setMirrored` (T6) + setting/UI (T5, T7); testing per-task + T8. T2-hook comment present in the fragment shader (T2).
- **Client-only:** no `host/`/protocol files in any task.
- **Type consistency:** `flipHorizontal` (Kotlin) / `flip_horizontal` (C++); `GlDisplayView.flipHorizontal`, `VideoDecoder::setFlipHorizontal`, `make_frame_format(w,h,mirrored)` used consistently across tasks.
- **Threading:** `onSurfaceReady` delivered via `post {}` (UI thread) from the GL thread — satisfies the constraint.
- **Testability:** the un-unit-testable GL renderer is isolated in T2/T3 (build + on-device gate); the Linux mirror's logic is made unit-testable via the pure `make_frame_format` helper (T6).
