# Client-owned display settings — Android — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the Android app the same client-owned display settings (resolution, FPS, audio) the Linux client got, sent to the host in HELLO v4; rotation stays on Android's existing sensor auto-rotate.

**Architecture:** Phase 1 (merged to `master`) already added HELLO v4 on the host + Linux client, and the host honors a v4 client's `fps`/`audio_wanted`/`orientation_code` (falling back to defaults for v2/v3 clients). This plan updates the Android app to send v4: extend the Kotlin `encodeHello`, add a SharedPreferences-backed settings store + a settings screen, thread the settings through `TransportClient`/`StreamActivity` (replacing the hardcoded 1920×1080), and reconnect to apply mid-stream changes. Rotation continues via `OrientationMapper` + live `ORIENTATION` messages.

**Tech Stack:** Kotlin, Android SDK (Activities + XML layouts), JUnit (JVM unit tests via `ANDROID_HOME=$HOME/android-sdk sh gradlew`). Builds run in the `droppix-android` distrobox (JDK 17), off the CIFS mount (build dir `~/droppix-android-build`).

## Global Constraints

- Kotlin; match surrounding code style (the `com.droppix.app` packages, existing naming).
- Build/test inside the `droppix-android` distrobox, off-mount. From the repo's `android/` dir:
  - Unit tests: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest'`
  - APK build: `... ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug'`
  (The repo is on a CIFS mount with **no exec bit**, so the wrapper MUST be run as `sh gradlew` — `./gradlew` fails with "Permission denied". `ANDROID_HOME=$HOME/android-sdk` is required or Gradle errors "SDK location not found". The Gradle build directory is redirected off-mount by `android/build.gradle.kts`; JDK 17 is pinned by `scripts/android-container.sh`. Baseline confirmed: current `testDebugUnitTest` is green.)
- **HELLO v4 wire body — MUST byte-match the merged C++ host** (`host/src/protocol.cpp`), all integers big-endian: `u32 version, u32 width, u32 height, u32 density, u32 fps, u8 audio_wanted, u8 orientation_code, u16-len name, u16-len id`. The three new fixed fields sit after `density`, before the strings, and are written only when `version >= 4` (a v2/v3 body keeps the old layout).
- The host is authoritative for a v4 client and falls back to its defaults for v2/v3 — so this change is safe to land incrementally; nothing on the host side changes.
- **Rotation is unchanged:** keep `OrientationMapper` (sensor auto-rotate) and the live `ORIENTATION` messages. Do NOT add a rotation picker.
- **Orientation in HELLO must be the device's ACTUAL current code** (`orientationMapper.currentCode()`), never a hardcoded 0. The host now seeds its session orientation from a v4 HELLO; if HELLO says landscape but the device is portrait, Android's immediate live `ORIENTATION(portrait)` triggers a host restart, whose reconnect HELLO again says landscape → **infinite restart loop**. Sending the real current code makes the HELLO seed and the live report agree, so no restart.
- **Resolution is landscape-normalized:** the value sent is `(max(realW,realH), min(realW,realH))` (width ≥ height). The host bakes in "app sends landscape dims; orientation code drives the portrait swap"; sending portrait-shaped dims would build a mis-shaped monitor.
- Work on a new branch `feat/android-client-settings` off `master`. Commit after each task.

---

### Task 1: Kotlin HELLO v4 (`encodeHello` + VERSION bump)

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt` (VERSION 2→4; extend `encodeHello`)
- Test: `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`

**Interfaces:**
- Produces: `fun encodeHello(version: Int, width: Int, height: Int, density: Int, name: String = "", id: String = "", fps: Int = 0, audioWanted: Int = 0, orientationCode: Int = 0): ByteArray` — new fields are trailing Kotlin params but written in wire order (after `density`) and only when `version >= 4`. `const val VERSION = 4`.

- [ ] **Step 1: Confirm VERSION has no other consumers**

Run: `grep -rn "Protocol.VERSION\|\.VERSION" android/app/src/main/java` — expect the only use is the `encodeHello` call in `TransportClient.kt`. If another site branches on version, note it (there should be none).

- [ ] **Step 2: Write the failing test** — add to `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`

```kotlin
@Test fun helloV4LayoutMatchesHost() {
    val b = Protocol.encodeHello(4, 1280, 720, 160, "n", "i",
                                 fps = 30, audioWanted = 1, orientationCode = 1)
    fun u32(o: Int) = ((b[o].toInt() and 0xFF) shl 24) or ((b[o+1].toInt() and 0xFF) shl 16) or
                      ((b[o+2].toInt() and 0xFF) shl 8) or (b[o+3].toInt() and 0xFF)
    assertEquals(4, u32(0)); assertEquals(1280, u32(4)); assertEquals(720, u32(8))
    assertEquals(160, u32(12))
    assertEquals(30, u32(16))                       // fps
    assertEquals(1, b[20].toInt() and 0xFF)         // audio_wanted
    assertEquals(1, b[21].toInt() and 0xFF)         // orientation_code
    assertEquals(0, b[22].toInt() and 0xFF); assertEquals(1, b[23].toInt() and 0xFF) // name len u16 = 1
    assertEquals('n'.code, b[24].toInt() and 0xFF)  // name
    assertEquals(0, b[25].toInt() and 0xFF); assertEquals(1, b[26].toInt() and 0xFF) // id len u16 = 1
    assertEquals('i'.code, b[27].toInt() and 0xFF)  // id
}

@Test fun helloV2OmitsNewFields() {
    // Legacy shape: strings immediately after density (offset 16), no fps/audio/orient.
    val b = Protocol.encodeHello(2, 1920, 1080, 96, "a", "b")
    assertEquals(0, b[16].toInt() and 0xFF); assertEquals(1, b[17].toInt() and 0xFF) // name len at 16
    assertEquals('a'.code, b[18].toInt() and 0xFF)
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*ProtocolTest*"'`
Expected: FAIL — `encodeHello` has no `fps`/`audioWanted`/`orientationCode` params.

- [ ] **Step 4: Implement** in `Protocol.kt`

Bump the constant: `const val VERSION = 4`. Replace `encodeHello`:

```kotlin
fun encodeHello(version: Int, width: Int, height: Int, density: Int,
                name: String = "", id: String = "",
                fps: Int = 0, audioWanted: Int = 0, orientationCode: Int = 0): ByteArray {
    val out = ArrayList<Byte>()
    putU32(out, version); putU32(out, width); putU32(out, height); putU32(out, density)
    if (version >= 4) {
        putU32(out, fps); out.add(audioWanted.toByte()); out.add(orientationCode.toByte())
    }
    val n = name.toByteArray(Charsets.UTF_8); val i = id.toByteArray(Charsets.UTF_8)
    out.add((n.size ushr 8).toByte()); out.add(n.size.toByte()); for (x in n) out.add(x)
    out.add((i.size ushr 8).toByte()); out.add(i.size.toByte()); for (x in i) out.add(x)
    return out.toByteArray()
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*ProtocolTest*"'`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/protocol/Protocol.kt android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt
git commit -m "feat(android/protocol): HELLO v4 carries fps/audio/orientation; VERSION=4"
```

---

### Task 2: Settings model + store + resolution helpers

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/settings/AppSettings.kt`
- Test: `android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt`

**Interfaces:**
- Produces:
  - `data class AppSettings(val width: Int = 0, val height: Int = 0, val fps: Int = 60, val audio: Boolean = false)` (`width/height == 0` ⇒ use native).
  - `object Resolutions { val PRESETS: List<Pair<Int,Int>>; fun landscape(realW: Int, realH: Int): Pair<Int,Int>; fun resolve(s: AppSettings, realW: Int, realH: Int): Pair<Int,Int> }` — `landscape` returns `(max,min)`; `resolve` returns the explicit setting if `width>0` else `landscape(realW,realH)`.
  - `class SettingsStore(context: Context)` with `fun load(): AppSettings` and `fun save(s: AppSettings)` over `getSharedPreferences("droppix", MODE_PRIVATE)` (keys `res_w`, `res_h`, `fps`, `audio`). Thin (Context-bound) — not unit-tested; the pure `Resolutions` logic is.

- [ ] **Step 1: Write the failing test** — `android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt`

```kotlin
package com.droppix.app.settings
import org.junit.Assert.*
import org.junit.Test
class AppSettingsTest {
    @Test fun landscapeNormalizesToWidthGeHeight() {
        assertEquals(2400 to 1080, Resolutions.landscape(1080, 2400))   // portrait device -> landscape
        assertEquals(2560 to 1600, Resolutions.landscape(2560, 1600))   // already landscape
    }
    @Test fun resolveUsesNativeWhenUnset() {
        assertEquals(2400 to 1080, Resolutions.resolve(AppSettings(), 1080, 2400))  // width==0 -> native
    }
    @Test fun resolveUsesExplicitWhenSet() {
        assertEquals(1280 to 720, Resolutions.resolve(AppSettings(width = 1280, height = 720), 1080, 2400))
    }
    @Test fun defaults() {
        val s = AppSettings()
        assertEquals(0, s.width); assertEquals(60, s.fps); assertFalse(s.audio)
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*AppSettingsTest*"'`
Expected: FAIL — `com.droppix.app.settings` does not exist.

- [ ] **Step 3: Implement** `android/app/src/main/java/com/droppix/app/settings/AppSettings.kt`

```kotlin
package com.droppix.app.settings

import android.content.Context

// Per-device display prefs the Android client sends to the host in HELLO v4. width/height == 0
// means "use this device's native screen resolution" (resolved at connect time). Rotation is
// NOT here — Android keeps its sensor auto-rotate.
data class AppSettings(val width: Int = 0, val height: Int = 0, val fps: Int = 60, val audio: Boolean = false)

object Resolutions {
    // Presets offered in the UI, in addition to "Native". Landscape-oriented (width >= height).
    val PRESETS: List<Pair<Int, Int>> = listOf(1280 to 720, 1920 to 1080, 2560 to 1440, 800 to 480)

    // The host expects landscape dims (orientation code drives the portrait swap).
    fun landscape(realW: Int, realH: Int): Pair<Int, Int> =
        if (realW >= realH) realW to realH else realH to realW

    // The (w,h) to send in HELLO: explicit setting when set, else the device's native (landscape).
    fun resolve(s: AppSettings, realW: Int, realH: Int): Pair<Int, Int> =
        if (s.width > 0 && s.height > 0) s.width to s.height else landscape(realW, realH)
}

class SettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("droppix", Context.MODE_PRIVATE)
    fun load(): AppSettings = AppSettings(
        width = prefs.getInt("res_w", 0), height = prefs.getInt("res_h", 0),
        fps = prefs.getInt("fps", 60), audio = prefs.getBoolean("audio", false))
    fun save(s: AppSettings) = prefs.edit()
        .putInt("res_w", s.width).putInt("res_h", s.height)
        .putInt("fps", s.fps).putBoolean("audio", s.audio).apply()
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*AppSettingsTest*"'`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/settings/AppSettings.kt android/app/src/test/java/com/droppix/app/settings/AppSettingsTest.kt
git commit -m "feat(android): AppSettings store + landscape/native resolution helpers"
```

---

### Task 3: Thread fps/audio/orientation through `TransportClient`

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/net/TransportClient.kt:63` (`run`) and `:95-103` (`runOverChannel`)
- Test: `android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt` (only if it calls `run`/`runOverChannel` — update those calls to the new arity)

**Interfaces:**
- Consumes: `Protocol.encodeHello(...)` v4 (Task 1).
- Produces (both gain `fps: Int, audioWanted: Int, orientationCode: Int` after `density`):
  - `fun run(host: String, port: Int, width: Int, height: Int, density: Int, fps: Int, audioWanted: Int, orientationCode: Int, listener: ..., isRunning: ..., stats: ..., pingIntervalMs: ..., name: ..., id: ...)`
  - `fun runOverChannel(input: InputStream, output: OutputStream, width: Int, height: Int, density: Int, fps: Int, audioWanted: Int, orientationCode: Int, listener: ..., isRunning: ..., stats: ..., pingIntervalMs: ..., name: ..., id: ...)`

- [ ] **Step 1: Read the current signatures**

Read `TransportClient.kt` lines 60-110 to capture the exact current parameter lists/order of `run` and `runOverChannel` (the interface block above abbreviates the tail params — preserve them verbatim, inserting the three new ones right after `density`).

- [ ] **Step 2: Edit `run`** — insert `fps`, `audioWanted`, `orientationCode` after `density`, and forward them to `runOverChannel` in the internal call (`TransportClient.kt:85-86`):

```kotlin
runOverChannel(socket.getInputStream(), socket.getOutputStream(),
    width, height, density, fps, audioWanted, orientationCode,
    listener, isRunning, stats, pingIntervalMs, name, id)
```

- [ ] **Step 3: Edit `runOverChannel`** — insert the three params after `density`, and pass them into `encodeHello` (`TransportClient.kt:103`):

```kotlin
Protocol.encodeHello(Protocol.VERSION, width, height, density, name, id,
                     fps, audioWanted, orientationCode)))
```

- [ ] **Step 4: Fix any test call sites**

Run: `grep -n "runOverChannel\|\.run(" android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt android/app/src/test/java/com/droppix/app/net/TransportClientStatsTest.kt`. For each call, insert `0, 0, 0` (fps/audio/orient) after the `density` argument so the tests compile unchanged in behavior (they assert on the loop, not the HELLO fields). If a test decodes/asserts the HELLO body length, update its expected bytes to the v4 layout.

- [ ] **Step 5: Build + run the net tests**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest --tests "*TransportClient*"'`
Expected: PASS (existing transport tests green with the new arity).

- [ ] **Step 6: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/net/TransportClient.kt android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt android/app/src/test/java/com/droppix/app/net/TransportClientStatsTest.kt
git commit -m "feat(android/net): TransportClient sends fps/audio/orientation in HELLO v4"
```

---

### Task 4: `StreamActivity` sends the settings (native res + current orientation)

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt` (both connect call sites ~191 AOA, ~213 Wi-Fi; load settings; resolve resolution + orientation)

**Interfaces:**
- Consumes: `SettingsStore`/`AppSettings`/`Resolutions` (Task 2); `TransportClient.run`/`runOverChannel` v4 arity (Task 3); `orientationMapper.currentCode()` (existing).

- [ ] **Step 1: Load settings + resolve resolution once, near the top of the streaming setup**

Where `running` is set true (around `StreamActivity.kt:139-140`), add:

```kotlin
val settings = com.droppix.app.settings.SettingsStore(this).load()
val real = android.util.DisplayMetrics()
@Suppress("DEPRECATION") windowManager.defaultDisplay.getRealMetrics(real)
val (sendW, sendH) = com.droppix.app.settings.Resolutions.resolve(settings, real.widthPixels, real.heightPixels)
val sendFps = settings.fps
val sendAudio = if (settings.audio) 1 else 0
```
(`getRealMetrics` is deprecated but returns the true panel size across API levels; keep the `@Suppress`.)

- [ ] **Step 2: Replace the AOA call site** (`StreamActivity.kt:191-193`)

```kotlin
c.runOverChannel(FileInputStream(pfd.fileDescriptor),
    FileOutputStream(pfd.fileDescriptor), sendW, sendH,
    resources.displayMetrics.densityDpi, sendFps, sendAudio, orientationMapper.currentCode(),
    listener, { running }, stats, /* keep the remaining args verbatim */ ...)
```

- [ ] **Step 3: Replace the Wi-Fi call site** (`StreamActivity.kt:213-214`)

```kotlin
c.run(host, port, sendW, sendH,
    resources.displayMetrics.densityDpi, sendFps, sendAudio, orientationMapper.currentCode(),
    listener, { running }, stats, /* keep the remaining args verbatim */ ...)
```

(Preserve every trailing argument at both sites exactly as it is today — only `1920, 1080` is replaced by `sendW, sendH` and the three new args are inserted after `densityDpi`.)

- [ ] **Step 4: Confirm the orientation invariant**

The `orientation_code` sent in HELLO is `orientationMapper.currentCode()` at both sites (NOT 0). The existing post-connect `c.sendOrientation(orientationMapper.currentCode())` (`StreamActivity.kt:151`) now reports the SAME value, so the host's v4 seed and the live report agree — no spurious restart. Leave line 151 as-is.

- [ ] **Step 5: Build the APK**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -5'`
Expected: `BUILD SUCCESSFUL`; APK under `~/droppix-android-build/`.

- [ ] **Step 6: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt
git commit -m "feat(android): send native-resolution/fps/audio + current orientation in HELLO"
```

---

### Task 5: Settings screen + entry point from ConnectActivity

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/ui/SettingsActivity.kt`
- Create: `android/app/src/main/res/layout/activity_settings.xml`
- Modify: `android/app/src/main/AndroidManifest.xml` (register `SettingsActivity`)
- Modify: `android/app/src/main/res/layout/activity_connect.xml` (add a "Settings" button)
- Modify: `android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt` (wire the button)

**Interfaces:**
- Consumes: `SettingsStore`/`AppSettings`/`Resolutions` (Task 2).
- Produces: a `SettingsActivity` that loads `AppSettings`, presents Resolution (a `Spinner`: "Native" + `Resolutions.PRESETS`), FPS (a `Spinner`: 30/60), Audio (a `Switch`), and saves on back/confirm.

- [ ] **Step 1: Layout** — `android/app/src/main/res/layout/activity_settings.xml`

A vertical `LinearLayout` with labelled rows: `Spinner` `@+id/res_spinner`, `Spinner` `@+id/fps_spinner`, `Switch` `@+id/audio_switch`, and a `Button` `@+id/save_btn`. (Mirror the style/padding of `activity_connect.xml`.)

- [ ] **Step 2: `SettingsActivity.kt`**

```kotlin
package com.droppix.app.ui
import android.app.Activity
import android.os.Bundle
import android.widget.*
import com.droppix.app.R
import com.droppix.app.settings.*

class SettingsActivity : Activity() {
    override fun onCreate(b: Bundle?) {
        super.onCreate(b); setContentView(R.layout.activity_settings)
        val store = SettingsStore(this); val cur = store.load()
        val resSpinner = findViewById<Spinner>(R.id.res_spinner)
        val resItems = listOf("Native") + Resolutions.PRESETS.map { "${it.first}x${it.second}" }
        resSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, resItems)
        resSpinner.setSelection(
            if (cur.width == 0) 0 else 1 + Resolutions.PRESETS.indexOfFirst { it.first == cur.width && it.second == cur.height }.coerceAtLeast(0))
        val fpsSpinner = findViewById<Spinner>(R.id.fps_spinner)
        val fpsItems = listOf(30, 60)
        fpsSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, fpsItems.map { it.toString() })
        fpsSpinner.setSelection(fpsItems.indexOf(cur.fps).coerceAtLeast(0))
        val audioSwitch = findViewById<Switch>(R.id.audio_switch); audioSwitch.isChecked = cur.audio
        findViewById<Button>(R.id.save_btn).setOnClickListener {
            val res = if (resSpinner.selectedItemPosition == 0) 0 to 0
                      else Resolutions.PRESETS[resSpinner.selectedItemPosition - 1]
            store.save(AppSettings(res.first, res.second, fpsItems[fpsSpinner.selectedItemPosition], audioSwitch.isChecked))
            finish()
        }
    }
}
```

- [ ] **Step 3: Manifest** — register the activity inside `<application>`:

```xml
<activity android:name=".ui.SettingsActivity" android:exported="false" />
```

- [ ] **Step 4: ConnectActivity button** — add a `Button` `@+id/settings_btn` to `activity_connect.xml`, then in `ConnectActivity.onCreate` (near the other button wiring, ~line 62):

```kotlin
findViewById<Button>(R.id.settings_btn).setOnClickListener {
    startActivity(Intent(this, SettingsActivity::class.java))
}
```

- [ ] **Step 5: Build**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -5'`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 6: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/SettingsActivity.kt android/app/src/main/res/layout/activity_settings.xml android/app/src/main/AndroidManifest.xml android/app/src/main/res/layout/activity_connect.xml android/app/src/main/java/com/droppix/app/ui/ConnectActivity.kt
git commit -m "feat(android): Settings screen (resolution/fps/audio) + ConnectActivity entry"
```

---

### Task 6: Apply-immediately — reconnect on a settings change during streaming

**Files:**
- Modify: `android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt` (a Settings entry point + restart the stream when settings change)

**Interfaces:**
- Consumes: `SettingsStore` (Task 2), `SettingsActivity` (Task 5), the existing `running`/streaming-thread structure.

- [ ] **Step 1: Read the streaming lifecycle**

Read `StreamActivity.kt` for how the streaming thread is started (the block guarded by `running` set true at ~139, spawning the connect loop) and stopped (`running = false`, ~254 / `onPause`/`onDestroy`). Identify the single method (or code block) that launches the connect thread so it can be re-invoked; if it's inline in `onCreate`/`onResume`, extract it into `private fun startStreaming()` and add `private fun stopStreaming()` that sets `running = false` and joins/waits for the worker thread to end (mirror the existing teardown).

- [ ] **Step 2: Add a Settings affordance + change detection**

Add a way to open settings from the stream view — the simplest that fits this app: a long-press on the `surfaceView` opens `SettingsActivity`:

```kotlin
private var lastSettings: com.droppix.app.settings.AppSettings? = null
// in onCreate, after the surfaceView is available:
surfaceView.setOnLongClickListener {
    startActivity(android.content.Intent(this, SettingsActivity::class.java)); true
}
// capture the settings the current session was started with, inside startStreaming():
lastSettings = com.droppix.app.settings.SettingsStore(this).load()
```

- [ ] **Step 3: Reconnect on resume when settings changed**

In `onResume` (after `super.onResume()`), compare the freshly-loaded settings against the running session's:

```kotlin
override fun onResume() {
    super.onResume()
    val now = com.droppix.app.settings.SettingsStore(this).load()
    if (running && lastSettings != null && now != lastSettings) {
        stopStreaming(); startStreaming()   // startStreaming() re-reads settings into lastSettings
    }
    // ... keep existing onResume work (orientation listener enable, etc.) ...
}
```
(`AppSettings` is a `data class`, so `!=` is value inequality. `startStreaming()` sets `lastSettings` from the fresh load, so a subsequent resume with no change is a no-op.)

- [ ] **Step 4: Build**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug 2>&1 | tail -5'`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/ui/StreamActivity.kt
git commit -m "feat(android): reconnect to apply a settings change made during streaming"
```

---

### Task 7: Verification

**Files:** none (verification only).

- [ ] **Step 1: Full unit tests + APK build**

Run:
```
distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_HOME=$HOME/android-sdk sh gradlew testDebugUnitTest && ANDROID_HOME=$HOME/android-sdk sh gradlew assembleDebug'
```
Expected: all unit tests pass; `BUILD SUCCESSFUL`.

- [ ] **Step 2: On-device E2E against the v4 host** (needs the tablet + a running host)

Install the debug APK on the Nexus/tablet. With the host streamer running (already v4 on `master`):
1. Set Resolution to a preset (e.g. 1280×720) in Settings → connect → host log shows `client HELLO v4 1280x720 fps=<n> audio=<n> orient=<n>` and builds a 1280×720 evdi monitor.
2. Set Resolution to Native → the host builds a monitor at the tablet's real (landscape-normalized) panel resolution.
3. Change FPS 60→30 while streaming (long-press → Settings → save → back) → the stream reconnects and resumes at 30.
4. Toggle Audio on → the tablet plays host audio; with a second client already holding audio, this tablet is video-only (host `already claimed` log).
5. Rotate the tablet → it still auto-rotates and the host restreams portrait/landscape — no restart loop on connect (HELLO carried the current orientation).

- [ ] **Step 3: Commit any fixes, then stop**

If Steps 1-2 surface bugs, fix within the relevant task's files and commit. Otherwise the Android phase — and the whole client-owned-display-settings feature — is complete.

---

## Self-review notes

- **Spec coverage:** Android settings UI = Resolution (native default) + FPS + Audio (T5), rotation unchanged (kept `OrientationMapper` + live `ORIENTATION`, no picker — Global Constraints + T4 Step 4); HELLO v4 send with native-default resolution (T1, T3, T4); reconnect-to-apply (T6); tests + on-device E2E (T7).
- **Wire compatibility:** T1's `encodeHello` writes the exact byte layout the merged C++ `decode_hello` expects (verified offset-by-offset in the T1 test), gated on `version >= 4`.
- **Restart-loop hazard** (the one real trap): documented in Global Constraints and enforced in T4 — HELLO carries `orientationMapper.currentCode()`, matching the immediate live `ORIENTATION`, so the host's v4 orientation seed and the live report agree.
- **Resolution shape:** landscape-normalized (`Resolutions.landscape`) to satisfy the host's "app sends landscape dims" assumption; the pure helper is unit-tested (T2).
- **Incremental safety:** the host already honors v4 and falls back for v2/v3, so this branch can land independently; no host changes.
