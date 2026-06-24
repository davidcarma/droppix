# Phase 1b — Device Findings

**Date:** 2026-06-23
**Result:** ✅ SUCCESS — the Android app receives the host stream over USB and displays it. Both the test pattern and the real evdi extended monitor work end-to-end on the physical device.

## Device

- **Nexus 10**, Android **5.1.1**, **API 22**, ABI `armeabi-v7a`.
- Required lowering the app's `minSdk` from 26 → 21 and dropping the unused
  AndroidX/appcompat dependency (a plain framework fullscreen theme is used).
  The Kotlin code needed no changes (MediaCodec is API 16+, `KEY_LOW_LATENCY`
  was already guarded by `SDK_INT >= 30`).

## What was verified

- ✅ `adb install` of the debug APK succeeds after the minSdk fix.
- ✅ `adb reverse tcp:27000 tcp:27000` + `droppix_stream --test-pattern` →
  the **animated test pattern appears on the tablet**, confirming connect →
  MediaCodec decode → SurfaceView display over the USB tunnel.
- ✅ `sudo droppix_stream` (evdi source) + dragging a window onto the new
  **droppix** monitor in KDE → **the window appears on the tablet**. A working
  USB extended display at 1080p, decoded fine by the Nexus 10's H.264 decoder.
- ✅ The client reconnect loop handled the test-pattern → evdi streamer switch
  without needing to relaunch the app.

## Latency

It works, but end-to-end latency is noticeable and worth reducing. Suspected
contributors (to be quantified next): the Nexus 10's 2012-era H.264 decoder has
no low-latency mode pre-API-30 and tends to buffer frames; software x264 encoding
of 1080p adds per-frame CPU time. Next step (Phase 1c) is latency instrumentation
— host encode-time/fps/frame-size stats + an on-screen tablet overlay (RTT via
PING/PONG, received fps, decode submit→output lag) — to measure each stage before
tuning (downscale / encoder knobs / VAAPI).

## Scope confirmed working

Display-only USB extended monitor. Input back-channel (Phase 2), stylus
(Phase 3), WiFi (Phase 4), and audio remain future phases.
