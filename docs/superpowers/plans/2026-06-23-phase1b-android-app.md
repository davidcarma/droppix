# Phase 1b — Android Receiver App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the native Kotlin Android app that connects to the Phase 1a host over a USB `adb reverse` tunnel, decodes the H.264 stream with MediaCodec, and renders it full-screen — so dragging a window onto the droppix monitor shows it live on the tablet.

**Architecture:** A pure-JVM core (`Protocol` mirroring the host wire format, `TransportClient` over `java.net.Socket`) that is JUnit-testable without a device, plus Android-specific pieces (`VideoDecoder` using MediaCodec→Surface, `DisplaySurfaceView`, `MainActivity`). The app dials `127.0.0.1:27000`, which `adb reverse` forwards over USB to the host daemon. Built from the command line with the Android SDK + Gradle in a distrobox (no IDE), mirroring the C++ "build in a container, off the CIFS mount" pattern.

**Tech Stack:** Kotlin, Android SDK (compileSdk 34, minSdk 26), Gradle (wrapper) + Android Gradle Plugin, MediaCodec (hardware H.264 decode), `java.net` sockets, JUnit 4 (JVM unit tests), `adb` (USB tunnel + install).

## Global Constraints

- **Build/run environment:** a dedicated Fedora distrobox `droppix-android` (separate from the C++ `droppix-dev`). Install a JDK + Android cmdline-tools SDK there. The repo is on a CIFS mount with NO exec bit and NO symlinks, so: (a) run `gradlew` via `bash gradlew` (never rely on its exec bit), (b) put the Gradle build output and Gradle user home OFF the mount (under the container `$HOME`). Concretely, builds run as:
  `distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew <task> --no-daemon'`
- **Version combo (starting point — adjust only on a real incompatibility, and document what you used):** Gradle 8.7, Android Gradle Plugin 8.5.2, Kotlin 1.9.24, compileSdk 34, build-tools 34.0.0, minSdk 26, targetSdk 34, JDK 17. If `java-17-openjdk-devel` is unavailable on Fedora 44, install `java-21-openjdk-devel` and bump AGP to 8.6.1 / Gradle to 8.9.
- **Wire protocol = the host's (frozen).** See `docs/superpowers/specs/2026-06-23-droppix-wire-protocol.md`. Message = `[u32 big-endian length][u8 type][body]`. Types Hello=1, Config=2, Video=3, Ping=4, Pong=5, Bye=6. HELLO body = u32 version(=1), u32 width, u32 height, u32 density. CONFIG body = u32 width, u32 height, u32 fps, u32 extradata_len, extradata. VIDEO body = u64 pts_us, u8 keyframe, H.264 Annex-B bytes. All integers big-endian. **kProtocolVersion = 1.**
- **SPS/PPS are IN-BAND** (host uses x264 repeat-headers); CONFIG.extradata is typically empty. MediaCodec must be configured from a `MediaFormat` with width/height and will pick up SPS/PPS from the in-band stream — do NOT depend on CONFIG.extradata being non-empty.
- **Package/app id:** `com.droppix.app`. App name "droppix".
- **Transport for Phase 1b:** USB only — the app dials `127.0.0.1:27000`; `adb reverse tcp:27000 tcp:27000` forwards it to the host. WiFi/discovery is a later phase.
- **Out of scope for 1b:** input back-channel (Phase 2), stylus (Phase 3), WiFi/mDNS/pairing (Phase 4), audio. The app is display-only; it sends HELLO and reads video.

---

## File Structure

```
android/
  settings.gradle.kts
  build.gradle.kts                 # root: plugin versions, off-mount buildDir
  gradle.properties
  gradlew  gradlew.bat
  gradle/wrapper/gradle-wrapper.properties  gradle/wrapper/gradle-wrapper.jar
  app/
    build.gradle.kts
    src/main/AndroidManifest.xml
    src/main/res/values/strings.xml
    src/main/res/layout/activity_main.xml
    src/main/java/com/droppix/app/
      protocol/Protocol.kt         # MsgType, framing, MessageParser, payload codecs (pure JVM)
      net/TransportClient.kt       # socket connect, handshake, video callbacks (pure JVM)
      decode/VideoDecoder.kt       # MediaCodec H.264 -> Surface (Android)
      ui/DisplaySurfaceView.kt     # SurfaceView (Android)
      ui/MainActivity.kt           # wires connection + decoder + surface (Android)
    src/test/java/com/droppix/app/
      protocol/ProtocolTest.kt     # JUnit, byte-exact vs host wire format
      net/TransportClientTest.kt   # JUnit loopback server
```

Pure-JVM units (`protocol`, `net`) carry real JUnit tests. Android units (`decode`, `ui`) gate on a successful APK build; their behavior is verified on the device (operator, Task 6).

---

### Task 1: Android dev container, SDK, Gradle scaffold, minimal app builds an APK

**Files:**
- Create: `android/settings.gradle.kts`, `android/build.gradle.kts`, `android/gradle.properties`
- Create: `android/app/build.gradle.kts`, `android/app/src/main/AndroidManifest.xml`, `android/app/src/main/res/values/strings.xml`
- Create: `android/app/src/test/java/com/droppix/app/SmokeTest.kt`
- Create (generated): `android/gradlew`, `android/gradle/wrapper/gradle-wrapper.properties`, `android/gradle/wrapper/gradle-wrapper.jar`
- Create: `scripts/android-container.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: a Gradle Android project that `assembleDebug` builds into an APK and `test` runs JUnit. Later tasks add Kotlin sources under `app/src/main/java/com/droppix/app/` and tests under `app/src/test/java/com/droppix/app/`.

- [ ] **Step 1: Create the Android container + SDK setup script**

Create `scripts/android-container.sh`:

```bash
#!/usr/bin/env bash
# Create the droppix-android Fedora distrobox and install JDK + Android SDK.
set -euo pipefail
NAME=droppix-android
IMAGE=registry.fedoraproject.org/fedora:44
SDK=/home/Spinjitsudoomyt/android-sdk

if ! distrobox list | grep -q "\b${NAME}\b"; then
  distrobox create --name "${NAME}" --image "${IMAGE}" --yes
fi

distrobox enter "${NAME}" -- bash -lc '
  set -e
  sudo dnf install -y java-17-openjdk-devel unzip wget which 2>/dev/null || \
  sudo dnf install -y java-21-openjdk-devel unzip wget which
  SDK=/home/Spinjitsudoomyt/android-sdk
  if [ ! -d "$SDK/cmdline-tools/latest" ]; then
    mkdir -p "$SDK/cmdline-tools"
    cd /tmp
    wget -q https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip -O cmdline.zip
    unzip -q -o cmdline.zip -d "$SDK/cmdline-tools"
    mv "$SDK/cmdline-tools/cmdline-tools" "$SDK/cmdline-tools/latest"
  fi
  yes | "$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" --licenses >/dev/null
  "$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" \
    "platform-tools" "platforms;android-34" "build-tools;34.0.0"
'
echo "droppix-android ready. SDK at ${SDK}"
```

Run it (this downloads ~1 GB; be patient):

```bash
chmod +x scripts/android-container.sh
bash scripts/android-container.sh
```
Expected: container created, JDK + SDK (platform-34, build-tools 34.0.0, platform-tools) installed.

- [ ] **Step 2: Write Gradle project files**

`android/settings.gradle.kts`:

```kotlin
pluginManagement {
    repositories { google(); mavenCentral(); gradlePluginPortal() }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories { google(); mavenCentral() }
}
rootProject.name = "droppix"
include(":app")
```

`android/build.gradle.kts` (root) — declares plugins and relocates the build dir off the CIFS mount:

```kotlin
plugins {
    id("com.android.application") version "8.5.2" apply false
    id("org.jetbrains.kotlin.android") version "1.9.24" apply false
}

// Repo is on a CIFS mount (no exec/symlink) — build off-mount.
val offMount = file("/home/Spinjitsudoomyt/droppix-android-build")
rootProject.layout.buildDirectory.set(offMount.resolve("root"))
subprojects {
    layout.buildDirectory.set(offMount.resolve(name))
}
```

`android/gradle.properties`:

```properties
org.gradle.jvmargs=-Xmx2048m
android.useAndroidX=true
kotlin.code.style=official
org.gradle.caching=true
```

- [ ] **Step 3: Write the app module build file + manifest + resources**

`android/app/build.gradle.kts`:

```kotlin
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.droppix.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.droppix.app"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1"
    }
    buildTypes {
        release { isMinifyEnabled = false }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    testImplementation("junit:junit:4.13.2")
}
```

`android/app/src/main/AndroidManifest.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.INTERNET" />

    <application
        android:allowBackup="true"
        android:label="@string/app_name"
        android:supportsRtl="true"
        android:theme="@style/Theme.AppCompat.NoActionBar">
        <activity
            android:name=".ui.MainActivity"
            android:exported="true"
            android:screenOrientation="landscape"
            android:configChanges="orientation|screenSize|keyboardHidden">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

`android/app/src/main/res/values/strings.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="app_name">droppix</string>
</resources>
```

- [ ] **Step 4: Add a temporary placeholder MainActivity so the manifest resolves**

The manifest references `.ui.MainActivity`; create a minimal stub so Task 1 builds (Task 5 replaces it). `android/app/src/main/java/com/droppix/app/ui/MainActivity.kt`:

```kotlin
package com.droppix.app.ui

import android.app.Activity
import android.os.Bundle

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
    }
}
```

- [ ] **Step 5: Write the smoke unit test**

`android/app/src/test/java/com/droppix/app/SmokeTest.kt`:

```kotlin
package com.droppix.app

import org.junit.Assert.assertEquals
import org.junit.Test

class SmokeTest {
    @Test fun toolchainWorks() { assertEquals(4, 2 + 2) }
}
```

- [ ] **Step 6: Generate the Gradle wrapper (pinned to 8.7)**

Inside the container, download a Gradle 8.7 distribution once and generate the wrapper so all later builds use `./gradlew`:

```
distrobox enter droppix-android -- bash -lc '
  set -e
  cd /tmp
  if [ ! -x /tmp/gradle-8.7/bin/gradle ]; then
    wget -q https://services.gradle.org/distributions/gradle-8.7-bin.zip -O g.zip
    unzip -q -o g.zip -d /tmp
  fi
  cd "/var/mnt/nas/Projects/Spacedesk for linux/android"
  /tmp/gradle-8.7/bin/gradle wrapper --gradle-version 8.7 --distribution-type bin
'
```
Then record the wrapper script as executable in git (CIFS can't hold the bit):
```bash
git update-index --chmod=+x android/gradlew 2>/dev/null || true
```

- [ ] **Step 7: Build the APK and run the unit test**

```
distrobox enter droppix-android -- bash -lc '
  export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk
  export JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java))))
  export GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle
  cd "/var/mnt/nas/Projects/Spacedesk for linux/android"
  bash gradlew --no-daemon assembleDebug test
'
```
Expected: `BUILD SUCCESSFUL`; an APK at (off-mount) `/home/Spinjitsudoomyt/droppix-android-build/app/outputs/apk/debug/app-debug.apk`; the `SmokeTest` passes. **If the build fails on a Gradle/AGP/Kotlin/JDK version mismatch, adjust to a compatible set (see Global Constraints fallback) and document the versions used in the report.**

- [ ] **Step 8: Add a .gitignore for the SDK/local files and commit**

Create `android/.gitignore`:

```
.gradle/
local.properties
*.iml
.idea/
```

Commit:
```bash
git add android/ scripts/android-container.sh
git commit -m "build(android): scaffold Gradle project, SDK container, minimal APK + smoke test"
```

---

### Task 2: Kotlin wire protocol (mirror of the host) — TDD

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/protocol/Protocol.kt`
- Create: `android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum class MsgType(val code: Int) { HELLO(1), CONFIG(2), VIDEO(3), PING(4), PONG(5), BYE(6) }` with `fun fromCode(c: Int): MsgType?`
  - `object Protocol` with: `const val VERSION = 1`; `fun encodeMessage(type: MsgType, body: ByteArray): ByteArray`; `fun encodeHello(version:Int,width:Int,height:Int,density:Int): ByteArray`; `data class Config(val width:Int,val height:Int,val fps:Int,val extradata:ByteArray)`; `fun decodeConfig(body: ByteArray): Config?`; `data class Video(val ptsUs:Long, val keyframe:Boolean, val nal:ByteArray)`; `fun decodeVideo(body: ByteArray): Video?`
  - `class MessageParser { fun feed(data: ByteArray, n: Int); fun next(): ParsedMessage? }` and `data class ParsedMessage(val type: MsgType, val body: ByteArray)`

- [ ] **Step 1: Write the failing tests**

`android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt`:

```kotlin
package com.droppix.app.protocol

import org.junit.Assert.*
import org.junit.Test

class ProtocolTest {
    @Test fun encodeMessageMatchesHostWireFormat() {
        // Must match the C++ host test byte-for-byte:
        // length = 1 (type) + 2 (body) = 3, big-endian; type=VIDEO(3).
        val m = Protocol.encodeMessage(MsgType.VIDEO, byteArrayOf(0xAA.toByte(), 0xBB.toByte()))
        assertArrayEquals(
            byteArrayOf(0, 0, 0, 3, 3, 0xAA.toByte(), 0xBB.toByte()), m)
    }

    @Test fun encodeHelloLayout() {
        val b = Protocol.encodeHello(1, 1920, 1080, 320)
        assertEquals(16, b.size)
        // version=1, width=1920(0x780), height=1080(0x438), density=320(0x140)
        assertArrayEquals(byteArrayOf(0,0,0,1), b.copyOfRange(0,4))
        assertArrayEquals(byteArrayOf(0,0,0x07,0x80.toByte()), b.copyOfRange(4,8))
        assertArrayEquals(byteArrayOf(0,0,0x04,0x38), b.copyOfRange(8,12))
        assertArrayEquals(byteArrayOf(0,0,0x01,0x40), b.copyOfRange(12,16))
    }

    @Test fun decodeConfigRoundTrip() {
        // Build a CONFIG body the way the host does: w,h,fps,edlen,extradata.
        val ed = byteArrayOf(0x67, 0x42, 0x00)
        val body = beU32(1920) + beU32(1080) + beU32(30) + beU32(ed.size) + ed
        val c = Protocol.decodeConfig(body)!!
        assertEquals(1920, c.width); assertEquals(1080, c.height); assertEquals(30, c.fps)
        assertArrayEquals(ed, c.extradata)
    }

    @Test fun decodeVideoRoundTrip() {
        val nal = byteArrayOf(0,0,0,1, 0x65, 0x11)
        val body = beU64(123456L) + byteArrayOf(1) + nal  // pts, keyframe=1, nal
        val v = Protocol.decodeVideo(body)!!
        assertEquals(123456L, v.ptsUs); assertTrue(v.keyframe)
        assertArrayEquals(nal, v.nal)
    }

    @Test fun parserReassemblesAcrossPartialFeeds() {
        val m = Protocol.encodeMessage(MsgType.PING, byteArrayOf(1, 2, 3))
        val p = MessageParser()
        p.feed(m.copyOfRange(0, 3), 3)
        assertNull(p.next())
        p.feed(m.copyOfRange(3, m.size), m.size - 3)
        val msg = p.next()!!
        assertEquals(MsgType.PING, msg.type)
        assertArrayEquals(byteArrayOf(1, 2, 3), msg.body)
        assertNull(p.next())
    }

    // big-endian helpers for the tests
    private fun beU32(x: Int) = byteArrayOf(
        (x ushr 24).toByte(), (x ushr 16).toByte(), (x ushr 8).toByte(), x.toByte())
    private fun beU64(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }
}
```

- [ ] **Step 2: Run tests to verify they fail**

```
distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew --no-daemon test'
```
Expected: compile failure — `Protocol`/`MessageParser` unresolved.

- [ ] **Step 3: Write Protocol.kt**

`android/app/src/main/java/com/droppix/app/protocol/Protocol.kt`:

```kotlin
package com.droppix.app.protocol

enum class MsgType(val code: Int) {
    HELLO(1), CONFIG(2), VIDEO(3), PING(4), PONG(5), BYE(6);
    companion object {
        fun fromCode(c: Int): MsgType? = entries.firstOrNull { it.code == c }
    }
}

data class ParsedMessage(val type: MsgType, val body: ByteArray)

object Protocol {
    const val VERSION = 1

    private fun putU32(out: ArrayList<Byte>, x: Int) {
        out.add((x ushr 24).toByte()); out.add((x ushr 16).toByte())
        out.add((x ushr 8).toByte()); out.add(x.toByte())
    }
    private fun getU32(b: ByteArray, o: Int): Int =
        ((b[o].toInt() and 0xFF) shl 24) or ((b[o+1].toInt() and 0xFF) shl 16) or
        ((b[o+2].toInt() and 0xFF) shl 8) or (b[o+3].toInt() and 0xFF)
    private fun getU64(b: ByteArray, o: Int): Long {
        var x = 0L
        for (i in 0 until 8) x = (x shl 8) or (b[o + i].toLong() and 0xFF)
        return x
    }

    fun encodeMessage(type: MsgType, body: ByteArray): ByteArray {
        val out = ArrayList<Byte>(5 + body.size)
        putU32(out, 1 + body.size)            // length covers type + body
        out.add(type.code.toByte())
        for (b in body) out.add(b)
        return out.toByteArray()
    }

    fun encodeHello(version: Int, width: Int, height: Int, density: Int): ByteArray {
        val out = ArrayList<Byte>(16)
        putU32(out, version); putU32(out, width); putU32(out, height); putU32(out, density)
        return out.toByteArray()
    }

    data class Config(val width: Int, val height: Int, val fps: Int, val extradata: ByteArray)
    fun decodeConfig(body: ByteArray): Config? {
        if (body.size < 16) return null
        val w = getU32(body, 0); val h = getU32(body, 4); val fps = getU32(body, 8)
        val n = getU32(body, 12)
        if (body.size != 16 + n) return null
        return Config(w, h, fps, body.copyOfRange(16, body.size))
    }

    data class Video(val ptsUs: Long, val keyframe: Boolean, val nal: ByteArray)
    fun decodeVideo(body: ByteArray): Video? {
        if (body.size < 9) return null
        val pts = getU64(body, 0)
        val key = body[8].toInt() != 0
        return Video(pts, key, body.copyOfRange(9, body.size))
    }
}

class MessageParser {
    private var buf = ByteArray(0)
    private var pos = 0
    private val maxMessage = 64 * 1024 * 1024  // mirror host sanity cap

    fun feed(data: ByteArray, n: Int) {
        buf = buf.copyOf(buf.size).plus(data.copyOfRange(0, n))
    }

    fun next(): ParsedMessage? {
        while (true) {
            if (buf.size - pos < 4) return null
            val len = ((buf[pos].toInt() and 0xFF) shl 24) or
                      ((buf[pos+1].toInt() and 0xFF) shl 16) or
                      ((buf[pos+2].toInt() and 0xFF) shl 8) or
                      (buf[pos+3].toInt() and 0xFF)
            if (len < 1 || len > maxMessage) { pos += 4; continue }
            if (buf.size - pos < 4 + len) return null
            val type = MsgType.fromCode(buf[pos + 4].toInt() and 0xFF)
            val body = buf.copyOfRange(pos + 5, pos + 4 + len)
            pos += 4 + len
            if (pos > 65536) { buf = buf.copyOfRange(pos, buf.size); pos = 0 }
            if (type == null) continue   // unknown type: skip
            return ParsedMessage(type, body)
        }
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Same gradle `test` command as Step 2. Expected: all `ProtocolTest` + `SmokeTest` pass.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/protocol/Protocol.kt android/app/src/test/java/com/droppix/app/protocol/ProtocolTest.kt
git commit -m "feat(android): Kotlin wire protocol mirroring the host (byte-exact)"
```

---

### Task 3: TransportClient (java.net) — TDD with a loopback server

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/net/TransportClient.kt`
- Create: `android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt`

**Interfaces:**
- Consumes: `Protocol`, `MessageParser`, `MsgType` (Task 2).
- Produces:
  - `interface StreamListener { fun onConfig(config: Protocol.Config); fun onVideo(video: Protocol.Video) }`
  - `class TransportClient` with `fun run(host: String, port: Int, width: Int, height: Int, density: Int, listener: StreamListener, isRunning: () -> Boolean)` — connects, sends HELLO, then reads messages, dispatching CONFIG/VIDEO to the listener, answering PING with PONG, until `isRunning()` is false or the socket closes. Pure `java.net` (works in JVM unit tests).

- [ ] **Step 1: Write the failing test**

`android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt`:

```kotlin
package com.droppix.app.net

import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import org.junit.Assert.*
import org.junit.Test
import java.io.DataInputStream
import java.net.ServerSocket
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class TransportClientTest {
    @Test fun handshakeReceivesConfigAndVideo() {
        val server = ServerSocket(0)
        val port = server.localPort

        // Fake host: accept, read HELLO, send CONFIG + one VIDEO.
        val serverThread = thread {
            server.use {
                val sock = it.accept()
                val input = DataInputStream(sock.getInputStream())
                // read HELLO frame: u32 len, then len bytes
                val len = input.readInt()
                val frame = ByteArray(len); input.readFully(frame)
                assertEquals(MsgType.HELLO.code, frame[0].toInt())  // type byte
                val out = sock.getOutputStream()
                out.write(Protocol.encodeMessage(MsgType.CONFIG,
                    beU32(1920) + beU32(1080) + beU32(30) + beU32(0)))  // empty extradata
                out.write(Protocol.encodeMessage(MsgType.VIDEO,
                    beU64(1000L) + byteArrayOf(1) + byteArrayOf(0,0,0,1,0x65)))
                out.flush()
                Thread.sleep(200)
                sock.close()
            }
        }

        var gotConfig: Protocol.Config? = null
        var gotVideo: Protocol.Video? = null
        val latch = CountDownLatch(2)
        val listener = object : StreamListener {
            override fun onConfig(config: Protocol.Config) { gotConfig = config; latch.countDown() }
            override fun onVideo(video: Protocol.Video) { gotVideo = video; latch.countDown() }
        }

        val client = TransportClient()
        val clientThread = thread {
            client.run("127.0.0.1", port, 1920, 1080, 320, listener) { true }
        }

        assertTrue("did not receive config+video", latch.await(3, TimeUnit.SECONDS))
        assertEquals(1920, gotConfig!!.width)
        assertEquals(1000L, gotVideo!!.ptsUs)
        assertTrue(gotVideo!!.keyframe)

        serverThread.join(1000); clientThread.join(1000)
    }

    private fun beU32(x: Int) = byteArrayOf(
        (x ushr 24).toByte(), (x ushr 16).toByte(), (x ushr 8).toByte(), x.toByte())
    private fun beU64(x: Long) = ByteArray(8) { i -> (x ushr (56 - i * 8)).toByte() }
}
```

- [ ] **Step 2: Run to verify failure** (gradle `test`) — expect unresolved `TransportClient`/`StreamListener`.

- [ ] **Step 3: Write TransportClient.kt**

`android/app/src/main/java/com/droppix/app/net/TransportClient.kt`:

```kotlin
package com.droppix.app.net

import com.droppix.app.protocol.MessageParser
import com.droppix.app.protocol.MsgType
import com.droppix.app.protocol.Protocol
import java.net.InetSocketAddress
import java.net.Socket

interface StreamListener {
    fun onConfig(config: Protocol.Config)
    fun onVideo(video: Protocol.Video)
}

class TransportClient {
    fun run(host: String, port: Int, width: Int, height: Int, density: Int,
            listener: StreamListener, isRunning: () -> Boolean) {
        val socket = Socket()
        socket.tcpNoDelay = true
        socket.connect(InetSocketAddress(host, port), 5000)
        socket.soTimeout = 1000  // periodic wakeups so isRunning() is checked

        val out = socket.getOutputStream()
        val input = socket.getInputStream()

        out.write(Protocol.encodeMessage(MsgType.HELLO,
            Protocol.encodeHello(Protocol.VERSION, width, height, density)))
        out.flush()

        val parser = MessageParser()
        val chunk = ByteArray(65536)
        try {
            while (isRunning()) {
                val n = try { input.read(chunk) } catch (e: java.net.SocketTimeoutException) { 0 }
                if (n > 0) {
                    parser.feed(chunk, n)
                    var msg = parser.next()
                    while (msg != null) {
                        when (msg.type) {
                            MsgType.CONFIG -> Protocol.decodeConfig(msg.body)?.let(listener::onConfig)
                            MsgType.VIDEO -> Protocol.decodeVideo(msg.body)?.let(listener::onVideo)
                            MsgType.PING -> { out.write(Protocol.encodeMessage(MsgType.PONG, msg.body)); out.flush() }
                            MsgType.BYE -> return
                            else -> { /* ignore */ }
                        }
                        msg = parser.next()
                    }
                } else if (n < 0) {
                    return  // peer closed
                }
            }
        } finally {
            try { socket.close() } catch (_: Exception) {}
        }
    }
}
```

- [ ] **Step 4: Run tests to verify they pass** (gradle `test`). Expected: `TransportClientTest` + prior tests pass.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/net/TransportClient.kt android/app/src/test/java/com/droppix/app/net/TransportClientTest.kt
git commit -m "feat(android): TransportClient — connect, handshake, video callbacks"
```

---

### Task 4: VideoDecoder (MediaCodec) + DisplaySurfaceView (APK-build gate)

**Files:**
- Create: `android/app/src/main/java/com/droppix/app/decode/VideoDecoder.kt`
- Create: `android/app/src/main/java/com/droppix/app/ui/DisplaySurfaceView.kt`

**Interfaces:**
- Consumes: nothing from earlier tasks directly (takes a `Surface` + dimensions).
- Produces:
  - `class VideoDecoder(surface: Surface, width: Int, height: Int)` with `fun submit(nal: ByteArray, ptsUs: Long)` and `fun release()`.
  - `class DisplaySurfaceView(context: Context) : SurfaceView` exposing `fun awaitSurface(cb: (Surface) -> Unit)` that calls back when the surface is ready (and is a valid View usable in a layout).

- [ ] **Step 1: Write VideoDecoder.kt**

`android/app/src/main/java/com/droppix/app/decode/VideoDecoder.kt`:

```kotlin
package com.droppix.app.decode

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Build
import android.view.Surface

// Hardware H.264 decode straight onto a Surface. SPS/PPS arrive in-band, so we
// configure with only width/height and let the codec sync on the first IDR.
class VideoDecoder(surface: Surface, width: Int, height: Int) {
    private val codec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
    private val info = MediaCodec.BufferInfo()

    init {
        val fmt = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height)
        if (Build.VERSION.SDK_INT >= 30) {
            fmt.setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
        }
        codec.configure(fmt, surface, null, 0)
        codec.start()
    }

    fun submit(nal: ByteArray, ptsUs: Long) {
        val inIndex = codec.dequeueInputBuffer(10_000)
        if (inIndex >= 0) {
            val buf = codec.getInputBuffer(inIndex)!!
            buf.clear()
            buf.put(nal)
            codec.queueInputBuffer(inIndex, 0, nal.size, ptsUs, 0)
        }
        var outIndex = codec.dequeueOutputBuffer(info, 0)
        while (outIndex >= 0) {
            codec.releaseOutputBuffer(outIndex, true)  // render to the surface
            outIndex = codec.dequeueOutputBuffer(info, 0)
        }
    }

    fun release() {
        try { codec.stop() } catch (_: Exception) {}
        codec.release()
    }
}
```

- [ ] **Step 2: Write DisplaySurfaceView.kt**

`android/app/src/main/java/com/droppix/app/ui/DisplaySurfaceView.kt`:

```kotlin
package com.droppix.app.ui

import android.content.Context
import android.util.AttributeSet
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

class DisplaySurfaceView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    private var surfaceCb: ((Surface) -> Unit)? = null

    init { holder.addCallback(this) }

    fun awaitSurface(cb: (Surface) -> Unit) {
        val s = holder.surface
        if (s != null && s.isValid) cb(s) else surfaceCb = cb
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceCb?.let { it(holder.surface) }
        surfaceCb = null
    }
    override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) {}
    override fun surfaceDestroyed(holder: SurfaceHolder) {}
}
```

- [ ] **Step 3: Build to verify both compile into the APK**

```
distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew --no-daemon assembleDebug test'
```
Expected: `BUILD SUCCESSFUL`, APK produced, prior unit tests still pass. (MediaCodec/SurfaceView are Android-runtime types — there is no JVM unit test here; the APK build is the gate. Behavior is verified on the device in Task 6.)

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/java/com/droppix/app/decode/VideoDecoder.kt android/app/src/main/java/com/droppix/app/ui/DisplaySurfaceView.kt
git commit -m "feat(android): MediaCodec H.264 decoder + display SurfaceView"
```

---

### Task 5: MainActivity wiring (APK-build gate)

**Files:**
- Create: `android/app/src/main/res/layout/activity_main.xml`
- Modify: `android/app/src/main/java/com/droppix/app/ui/MainActivity.kt` (replace the Task 1 stub)

**Interfaces:**
- Consumes: `DisplaySurfaceView`, `VideoDecoder` (Task 4), `TransportClient`, `StreamListener`, `Protocol.Config`, `Protocol.Video` (Tasks 2–3).
- Produces: a launchable Activity that, on resume, connects on a background thread to `127.0.0.1:27000`, creates a `VideoDecoder` when CONFIG arrives, and feeds VIDEO packets to it; tears down on pause.

- [ ] **Step 1: Write the layout**

`android/app/src/main/res/layout/activity_main.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<FrameLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:background="#000000">

    <com.droppix.app.ui.DisplaySurfaceView
        android:id="@+id/surface"
        android:layout_width="match_parent"
        android:layout_height="match_parent" />
</FrameLayout>
```

- [ ] **Step 2: Write MainActivity.kt**

Replace `android/app/src/main/java/com/droppix/app/ui/MainActivity.kt`:

```kotlin
package com.droppix.app.ui

import android.app.Activity
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.WindowManager
import com.droppix.app.R
import com.droppix.app.decode.VideoDecoder
import com.droppix.app.net.StreamListener
import com.droppix.app.net.TransportClient
import com.droppix.app.protocol.Protocol
import kotlin.concurrent.thread

class MainActivity : Activity() {
    private companion object { const val TAG = "droppix"; const val HOST = "127.0.0.1"; const val PORT = 27000 }

    @Volatile private var running = false
    private var netThread: Thread? = null
    private var decoder: VideoDecoder? = null
    private lateinit var surfaceView: DisplaySurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(R.layout.activity_main)
        surfaceView = findViewById(R.id.surface)
    }

    override fun onResume() {
        super.onResume()
        running = true
        surfaceView.awaitSurface { surface -> startStreaming(surface) }
    }

    private fun startStreaming(surface: Surface) {
        netThread = thread(name = "droppix-net") {
            val client = TransportClient()
            val listener = object : StreamListener {
                override fun onConfig(config: Protocol.Config) {
                    Log.i(TAG, "CONFIG ${config.width}x${config.height}@${config.fps}")
                    runOnUiThread {
                        surfaceView.holder.setFixedSize(config.width, config.height)
                    }
                    decoder?.release()
                    decoder = VideoDecoder(surface, config.width, config.height)
                }
                override fun onVideo(video: Protocol.Video) {
                    decoder?.submit(video.nal, video.ptsUs)
                }
            }
            try {
                client.run(HOST, PORT, 1920, 1080, resources.displayMetrics.densityDpi,
                    listener) { running }
            } catch (e: Exception) {
                Log.w(TAG, "stream ended: ${e.message}")
            }
        }
    }

    override fun onPause() {
        super.onPause()
        running = false
        netThread?.join(1500)
        netThread = null
        decoder?.release()
        decoder = null
    }
}
```

- [ ] **Step 3: Build the release-candidate debug APK**

```
distrobox enter droppix-android -- bash -lc 'export ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle; cd "/var/mnt/nas/Projects/Spacedesk for linux/android"; bash gradlew --no-daemon assembleDebug test'
```
Expected: `BUILD SUCCESSFUL`; APK at `/home/Spinjitsudoomyt/droppix-android-build/app/outputs/apk/debug/app-debug.apk`; all unit tests pass.

- [ ] **Step 4: Commit**

```bash
git add android/app/src/main/res/layout/activity_main.xml android/app/src/main/java/com/droppix/app/ui/MainActivity.kt
git commit -m "feat(android): MainActivity wires transport -> decoder -> surface"
```

---

### Task 6: Operator end-to-end — install on the tablet and see the picture (hardware gate)

This step is performed by the human operator (needs the physical tablet, USB authorization, and the GUI). It is not automatable.

- [ ] **Step 1: Authorize the tablet.** Plug in the tablet; on the host run `adb devices`. If it shows `unauthorized`, tap **Allow USB debugging** on the tablet (check "always allow"). Re-run `adb devices` until it shows `device`.

- [ ] **Step 2: Install the APK** (from the host, using the off-mount APK path):
```bash
adb install -r /home/Spinjitsudoomyt/droppix-android-build/app/outputs/apk/debug/app-debug.apk
```

- [ ] **Step 3: Set up the USB tunnel** so the tablet's `localhost:27000` reaches the host daemon:
```bash
adb reverse tcp:27000 tcp:27000
```

- [ ] **Step 4: Start the host streamer** (test pattern first — no evdi/sudo needed — to isolate the app):
```bash
/home/Spinjitsudoomyt/droppix-build/droppix_stream --test-pattern --port 27000 --fps 30 --width 1280 --height 720
```

- [ ] **Step 5: Launch the droppix app** on the tablet. Expected: the animated test pattern appears on the tablet, confirming connect → MediaCodec decode → display works over USB.

- [ ] **Step 6: Now the real extended monitor.** Stop the test-pattern streamer; start the evdi one (needs sudo):
```bash
sudo /home/Spinjitsudoomyt/droppix-build/droppix_stream --port 27000 --fps 30 --bitrate 8000
```
In KDE Display settings, enable/arrange the new **droppix** monitor and drag a window onto it. Expected: **the window appears on the tablet** — a working USB extended display.

- [ ] **Step 7: Record findings** in `docs/superpowers/specs/2026-06-23-phase1b-device-findings.md`: latency feel, fps, resolution/scaling, any decode artifacts or reconnect issues. This informs Phase 2 (input back-channel) and the VAAPI encoder follow-up.

```bash
git add docs/superpowers/specs/2026-06-23-phase1b-device-findings.md
git commit -m "docs: phase 1b device findings (tablet receives the stream)"
```

---

## Self-Review

**1. Spec coverage (Phase 1b scope):** The design spec's Android side = `TransportClient` (Task 3), `Decoder` MediaCodec→Surface (Task 4), `DisplaySurfaceView` (Task 4), `ConnectionUi`/Activity (Task 5). HELLO/CONFIG/VIDEO/PING/PONG/BYE protocol mirrored in Kotlin (Task 2), byte-exact against the host. USB transport via `adb reverse` (Tasks 5–6). `InputCapture` is correctly out of scope (Phase 2). The frozen wire protocol + in-band-SPS/PPS constraint from `droppix-wire-protocol.md` is honored (Task 4 configures MediaCodec without relying on CONFIG.extradata).

**2. Placeholder scan:** No TBD/TODO. Every code step has complete code. The Task 1 `MainActivity` stub is explicitly a placeholder created in Task 1 and fully replaced in Task 5 (called out in both). The version-combo "adjust if incompatible" note is a real, bounded fallback (with specific alternative versions), not a placeholder.

**3. Type consistency:** `MsgType` codes match the host (1–6). `Protocol.encodeMessage/encodeHello/decodeConfig/decodeVideo`, `Protocol.Config`, `Protocol.Video`, `MessageParser`/`ParsedMessage` are defined in Task 2 and used with identical signatures in Task 3 (`TransportClient`) and Task 5. `StreamListener.onConfig(Protocol.Config)`/`onVideo(Protocol.Video)` consistent between Task 3 and Task 5. `VideoDecoder(Surface,Int,Int)` + `submit(ByteArray,Long)` + `release()` and `DisplaySurfaceView.awaitSurface((Surface)->Unit)` defined in Task 4 and consumed in Task 5. Byte-order helpers match the host's big-endian layout (verified against the Phase 1a protocol bytes in Task 2's tests).

**Known build-matrix caveat (by design):** the exact Gradle/AGP/Kotlin/JDK versions are pinned as a known-good starting point; the Android toolchain version matrix and Fedora's JDK package availability can require small adjustments — the build is the gate and Task 1 instructs documenting any version changes. The canonical project path used in every build command is `/var/mnt/nas/Projects/Spacedesk for linux/android`.
