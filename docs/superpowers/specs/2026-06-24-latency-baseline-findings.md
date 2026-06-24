# Latency Baseline Findings (host-side, evdi 1080p)

**Date:** 2026-06-24
**Source:** operator run of `sudo droppix_stream --port 27000 --fps 30 --bitrate 8000`
(evdi real-monitor source) with the `--stats-json`/`stats:` instrumentation, while
the Nexus 10 app received the stream.

## Measurements

- **Encode time:** avg ~3.7–4.8 ms, peak ~8–10 ms. → The software x264 encoder is
  NOT the latency bottleneck. Plenty of CPU headroom.
- **fps:** swings 0–61 depending on on-screen motion (evdi is damage-driven). The
  capture+encode side sustains up to ~60 fps when the screen is busy, so it keeps up.
- **Frame size:** avg ~17–30 KB, **but peaks of 680–936 KB**.
- **x264 report:** `profile Constrained Baseline, level 4.0`; I-frames ~920 KB at
  **QP ~1.07**, P-frames small; final bitrate **4.8–27 Mbps** despite `--bitrate 8000`.

## Diagnosis

The encoder is effectively running near-lossless (QP ~0.7–1.0) and **does not
respect the target bitrate** because there is **no VBV (video buffering verifier)
constraint** — `bit_rate` alone (ABR) does not cap *instantaneous* per-frame size.
So whenever the whole screen changes (or on each IDR), a single ~900 KB access
unit is produced. Those spikes:
1. take longer to push over the USB/adb link, and
2. overwhelm the Nexus 10's 2012-era H.264 decoder,
which is the felt latency/stutter — NOT encode CPU and NOT the transport floor.

## Fix (planned — the latency tuning)

1. **VBV rate control (primary).** In `SoftwareEncoder::open` set
   `ctx_->rc_max_rate = bit_rate` and `ctx_->rc_buffer_size` to a small buffer
   (~1–2 frames, e.g. `bit_rate / fps * 2`), so libx264 caps instantaneous frame
   size. This bounds per-frame bytes → bounded transport+decode latency and
   eliminates the ~900 KB spikes. Keep `tune=zerolatency`, no B-frames.
2. **Downscale the evdi source 1080p→720p (secondary).** Add a `--scale`/encode-
   resolution option so the 1080p capture is encoded at 720p (libswscale dst
   size); less data per frame and far easier for the old decoder. The Android app
   already adapts via the CONFIG dimensions; the host GUI will expose this once it
   exists.
3. Re-measure on device (overlay RTT/fps/decode + host stats) to confirm the
   ~900 KB peaks are gone and decode lag drops.

## Note

The decoder-side overlay numbers (RTT / fps / decode ms on the tablet) were not
captured in this run (operator ran the CLI directly, not via the app overlay
session). The host-side data alone is conclusive enough to act on the VBV fix.
