# AOA M0 Validation Spike — Plan

> **For agentic workers:** This is an EXPLORATORY HARDWARE SPIKE, not production TDD. Its "test" is a round-trip on the real Nexus 10, not unit tests. Build the throwaway artifacts, then run the hardware validation (Task 3) with the user. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Prove on the real Nexus 10 that AOA works — the device enters accessory mode, bulk endpoints open, bytes round-trip, and throughput clears ~8 Mbps — before building the real transport (M1–M4).

**Architecture:** A throwaway host libusb program does the AOA control handshake (51/52/53) and an echo/throughput test over the bulk endpoints; a throwaway Android `UsbAccessory` activity echoes bytes back. Both are disposable spike code on the `feat/aoa-usb-transport` branch — M2/M4 replace them with the real transport.

**Tech Stack:** C + libusb-1.0 (host); Kotlin `UsbManager`/`UsbAccessory` (Android).

## Global Constraints

- AOA control requests: **51** = get protocol (`bmRequestType=0xC0`), **52** = send string (`0x40`, `wIndex` = string id 0..5), **53** = ACCESSORY_START (`0x40`, no data). Verbatim from the spec's handshake.
- Identification strings the host sends MUST match the Android `accessory_filter.xml`: **manufacturer=`droppix`, model=`droppix`** (description=`droppix USB`, version=`1.0`, uri=`https://droppix`, serial=`0000`).
- Accessory-mode device: VID `0x18D1`, PID `0x2D00` (accessory) or `0x2D01` (accessory+adb).
- The Nexus 10 enumerates as VID `0x18D1` in MTP mode — the spike targets VID `0x18D1`. (Production M2/M3 will broaden device detection.)
- Spike code is throwaway: host tool under `spike/aoa/`; Android echo activity added to the app on this branch only. Not merged as-is.
- Host tool runs as **root** (`sudo`) for raw USB — no udev rule in the spike (M3 adds that).
- Build C in the `droppix-dev` distrobox; build the APK in `droppix-android`. Commits: `git -c user.name="Claude" -c user.email="noreply@anthropic.com"`.

---

## File Structure

- **Create** `spike/aoa/aoa_spike.c` — host libusb handshake + echo/throughput tool (throwaway).
- **Create** `spike/aoa/README.md` — how to build/run (throwaway).
- **Create** `android/app/src/main/java/com/droppix/app/ui/AoaEchoActivity.kt` — throwaway echo handler.
- **Create** `android/app/src/main/res/xml/accessory_filter.xml` — accessory match.
- **Modify** `android/app/src/main/AndroidManifest.xml` — register the echo activity + accessory intent filter.

---

### Task 1: Host libusb AOA handshake + echo/throughput tool

**Files:**
- Create: `spike/aoa/aoa_spike.c`
- Create: `spike/aoa/README.md`

- [ ] **Step 1: Install libusb in the distrobox**

Run: `distrobox enter droppix-dev -- bash -lc 'pkg-config --exists libusb-1.0 || sudo dnf install -y libusb1-devel'`
Expected: libusb1-devel present (`pkg-config --modversion libusb-1.0` prints a version).

- [ ] **Step 2: Write the host tool**

Create `spike/aoa/aoa_spike.c`:

```c
// aoa_spike.c — THROWAWAY: switch an Android (Nexus, VID 0x18d1) into AOA accessory
// mode, then echo/throughput-test the bulk endpoints. Proves M0.
// Build: gcc aoa_spike.c -o aoa_spike $(pkg-config --cflags --libs libusb-1.0)
// Run:   sudo ./aoa_spike
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define AOA_GET_PROTOCOL 51
#define AOA_SEND_STRING  52
#define AOA_START        53
#define GOOGLE_VID 0x18d1
#define ACC_PID0 0x2d00
#define ACC_PID1 0x2d01

static int send_string(libusb_device_handle* h, int idx, const char* s) {
  return libusb_control_transfer(h, 0x40, AOA_SEND_STRING, 0, idx,
                                 (unsigned char*)s, (uint16_t)(strlen(s) + 1), 1000);
}

int main(void) {
  libusb_context* ctx = NULL;
  if (libusb_init(&ctx)) { fprintf(stderr, "libusb_init failed\n"); return 1; }

  // 1) find a non-accessory Android device (VID 0x18d1) and open it.
  libusb_device_handle* h = NULL;
  libusb_device** list;
  ssize_t n = libusb_get_device_list(ctx, &list);
  for (ssize_t i = 0; i < n; i++) {
    struct libusb_device_descriptor d;
    if (libusb_get_device_descriptor(list[i], &d)) continue;
    if (d.idVendor != GOOGLE_VID) continue;
    if (d.idProduct == ACC_PID0 || d.idProduct == ACC_PID1) continue;  // already accessory
    if (libusb_open(list[i], &h) == 0) {
      printf("opened Android %04x:%04x\n", d.idVendor, d.idProduct);
      break;
    }
  }
  libusb_free_device_list(list, 1);
  if (!h) { fprintf(stderr, "no Android (VID 18d1) in non-accessory mode found\n"); return 1; }

  // 2) query AOA protocol version.
  unsigned char buf[2] = {0, 0};
  int r = libusb_control_transfer(h, 0xC0, AOA_GET_PROTOCOL, 0, 0, buf, 2, 1000);
  if (r < 2) { fprintf(stderr, "AOA get-protocol failed (%d)\n", r); return 1; }
  int proto = buf[0] | (buf[1] << 8);
  printf("AOA protocol version = %d\n", proto);
  if (proto < 1) { fprintf(stderr, "device is not AOA-capable\n"); return 1; }

  // 3) send identification strings + ACCESSORY_START.
  send_string(h, 0, "droppix");        // manufacturer
  send_string(h, 1, "droppix");        // model
  send_string(h, 2, "droppix USB");    // description
  send_string(h, 3, "1.0");            // version
  send_string(h, 4, "https://droppix");// uri
  send_string(h, 5, "0000");           // serial
  r = libusb_control_transfer(h, 0x40, AOA_START, 0, 0, NULL, 0, 1000);
  printf("ACCESSORY_START sent (%d); device should re-enumerate as accessory\n", r);
  libusb_close(h);

  // 4) wait for the accessory-mode device to appear (up to ~5s).
  libusb_device_handle* a = NULL;
  for (int tries = 0; tries < 50 && !a; tries++) {
    struct timespec ts = {0, 100 * 1000 * 1000};
    nanosleep(&ts, NULL);
    a = libusb_open_device_with_vid_pid(ctx, GOOGLE_VID, ACC_PID0);
    if (!a) a = libusb_open_device_with_vid_pid(ctx, GOOGLE_VID, ACC_PID1);
  }
  if (!a) { fprintf(stderr, "accessory device did not appear\n"); return 1; }
  printf("accessory device opened\n");

  if (libusb_kernel_driver_active(a, 0) == 1) libusb_detach_kernel_driver(a, 0);
  if (libusb_claim_interface(a, 0)) { fprintf(stderr, "claim_interface failed\n"); return 1; }

  // find the bulk IN/OUT endpoints on interface 0.
  struct libusb_config_descriptor* cfg;
  libusb_get_active_config_descriptor(libusb_get_device(a), &cfg);
  const struct libusb_interface_descriptor* id = &cfg->interface[0].altsetting[0];
  unsigned char ep_in = 0, ep_out = 0;
  for (int e = 0; e < id->bNumEndpoints; e++) {
    const struct libusb_endpoint_descriptor* ep = &id->endpoint[e];
    if ((ep->bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_BULK) {
      if (ep->bEndpointAddress & 0x80) ep_in = ep->bEndpointAddress;
      else ep_out = ep->bEndpointAddress;
    }
  }
  libusb_free_config_descriptor(cfg);
  printf("bulk endpoints: IN=0x%02x OUT=0x%02x\n", ep_in, ep_out);
  if (!ep_in || !ep_out) { fprintf(stderr, "missing bulk endpoints\n"); return 1; }

  // 5) echo + throughput: send 8 MB in 16 KB chunks, read the echo back, verify + time.
  const int CHUNK = 16384, TOTAL = 8 * 1024 * 1024;
  unsigned char* out = malloc(CHUNK), *in = malloc(CHUNK);
  for (int i = 0; i < CHUNK; i++) out[i] = (unsigned char)i;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  int sent = 0, ok = 1;
  while (sent < TOTAL && ok) {
    int tx = 0;
    if (libusb_bulk_transfer(a, ep_out, out, CHUNK, &tx, 3000) || tx != CHUNK) { ok = 0; break; }
    int acc = 0, got = 0;
    while (acc < CHUNK) {
      if (libusb_bulk_transfer(a, ep_in, in + acc, CHUNK - acc, &got, 3000)) { ok = 0; break; }
      acc += got;
    }
    if (!ok || memcmp(out, in, CHUNK) != 0) { fprintf(stderr, "echo mismatch\n"); ok = 0; break; }
    sent += CHUNK;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
  if (ok) {
    double rt_mbps = (sent * 8.0 / 1e6) / secs;   // round-trip
    printf("ECHO OK: %d bytes round-trip in %.2fs => %.1f Mbit/s round-trip "
           "(~%.1f Mbit/s one-way)\n", sent, secs, rt_mbps, rt_mbps / 2);
  } else {
    fprintf(stderr, "echo/throughput FAILED after %d bytes\n", sent);
  }

  libusb_release_interface(a, 0);
  libusb_close(a);
  libusb_exit(ctx);
  return ok ? 0 : 1;
}
```

- [ ] **Step 3: Write the README**

Create `spike/aoa/README.md`:

```markdown
# AOA M0 spike (throwaway)

Proves Android Open Accessory works on the Nexus 10 before building the real transport.

## Build
    distrobox enter droppix-dev -- bash -lc \
      'cd "<repo>/spike/aoa" && gcc aoa_spike.c -o aoa_spike $(pkg-config --cflags --libs libusb-1.0)'

## Run (needs the spike APK installed on the tablet; see Task 2)
1. Plug in the Nexus (MTP mode is fine).
2. `sudo <repo>/spike/aoa/aoa_spike`
3. On the tablet, accept "Open droppix for this USB accessory?".
4. Read the printed AOA protocol version, endpoint addresses, and echo throughput.

Success = accessory mode entered, endpoints found, ECHO OK, ~>= 8 Mbit/s one-way.
```

- [ ] **Step 4: Build the tool**

Run: `distrobox enter droppix-dev -- bash -lc 'cd "/var/mnt/nas/Spacedesk for linux/spike/aoa" 2>/dev/null || cd "/var/mnt/nas/Projects/Spacedesk for linux/spike/aoa"; gcc aoa_spike.c -o aoa_spike $(pkg-config --cflags --libs libusb-1.0) && echo BUILT'`
Expected: prints `BUILT`; `spike/aoa/aoa_spike` exists.

- [ ] **Step 5: Commit**

```bash
git add spike/aoa/aoa_spike.c spike/aoa/README.md
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "spike(aoa): host libusb AOA handshake + echo/throughput tool"
```

---

### Task 2: Android throwaway echo handler

**Files:**
- Create: `android/app/src/main/res/xml/accessory_filter.xml`
- Create: `android/app/src/main/java/com/droppix/app/ui/AoaEchoActivity.kt`
- Modify: `android/app/src/main/AndroidManifest.xml`

- [ ] **Step 1: Accessory filter**

Create `android/app/src/main/res/xml/accessory_filter.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <usb-accessory manufacturer="droppix" model="droppix" />
</resources>
```

- [ ] **Step 2: Echo activity**

Create `android/app/src/main/java/com/droppix/app/ui/AoaEchoActivity.kt`:

```kotlin
package com.droppix.app.ui

import android.app.Activity
import android.content.Context
import android.hardware.usb.UsbAccessory
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.util.Log
import java.io.FileInputStream
import java.io.FileOutputStream
import kotlin.concurrent.thread

/** THROWAWAY M0 spike: open the USB accessory and echo every byte back so the host
 *  tool can measure AOA round-trip + throughput. Replaced by the real transport in M4. */
class AoaEchoActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val mgr = getSystemService(Context.USB_SERVICE) as UsbManager
        val acc: UsbAccessory? =
            intent.getParcelableExtra(UsbManager.EXTRA_ACCESSORY) ?: mgr.accessoryList?.firstOrNull()
        if (acc == null) { Log.w("aoa-echo", "no accessory"); finish(); return }
        val pfd = mgr.openAccessory(acc)
        if (pfd == null) { Log.w("aoa-echo", "openAccessory returned null"); finish(); return }
        Log.i("aoa-echo", "accessory opened; echoing")
        thread(name = "aoa-echo") {
            val fis = FileInputStream(pfd.fileDescriptor)
            val fos = FileOutputStream(pfd.fileDescriptor)
            val buf = ByteArray(16384)
            try {
                while (true) {
                    val n = fis.read(buf)
                    if (n < 0) break
                    fos.write(buf, 0, n); fos.flush()
                }
            } catch (e: Exception) {
                Log.w("aoa-echo", "echo ended: ${e.message}")
            } finally {
                try { pfd.close() } catch (_: Exception) {}
            }
        }
    }
}
```

- [ ] **Step 3: Register in the manifest**

In `android/app/src/main/AndroidManifest.xml`, add this `<activity>` inside `<application>` (after the `StreamActivity` entry):

```xml
        <activity
            android:name=".ui.AoaEchoActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="android.hardware.usb.action.USB_ACCESSORY_ATTACHED" />
            </intent-filter>
            <meta-data
                android:name="android.hardware.usb.action.USB_ACCESSORY_ATTACHED"
                android:resource="@xml/accessory_filter" />
        </activity>
```

- [ ] **Step 4: Build + install the spike APK**

Run: `distrobox enter droppix-android -- bash -lc 'cd "/var/mnt/nas/Projects/Spacedesk for linux/android" && ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk JAVA_HOME=$(dirname $(dirname $(readlink -f $(which java)))) GRADLE_USER_HOME=/home/Spinjitsudoomyt/.droppix-gradle bash gradlew --no-daemon assembleDebug'`
Expected: BUILD SUCCESSFUL. Then install the debug APK on the Nexus (`adb install -r` the produced `app-debug.apk`, or transfer + tap) — noting this is a debug build for the spike.

- [ ] **Step 5: Commit**

```bash
git add android/app/src/main/res/xml/accessory_filter.xml android/app/src/main/java/com/droppix/app/ui/AoaEchoActivity.kt android/app/src/main/AndroidManifest.xml
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "spike(aoa): throwaway UsbAccessory echo handler"
```

---

### Task 3: Hardware validation run (with the user)

**Files:** none (validation only).

- [ ] **Step 1:** Confirm the spike APK is installed on the Nexus and the `aoa_spike` tool is built.
- [ ] **Step 2:** Plug the Nexus into the PC (MTP mode is fine; Developer Options may be on or off — AOA needs neither).
- [ ] **Step 3:** Run `sudo "/var/mnt/nas/Projects/Spacedesk for linux/spike/aoa/aoa_spike"`.
- [ ] **Step 4:** On the Nexus, accept **"Open droppix for this USB accessory?"** (tick "use by default"). It launches `AoaEchoActivity`.
- [ ] **Step 5: Record the go/no-go result** from the tool's output:
  - AOA protocol version printed (≥ 1)? → device is AOA-capable.
  - "accessory device opened" + bulk IN/OUT endpoints found? → accessory mode + endpoints work.
  - "ECHO OK" with throughput **≥ ~8 Mbit/s one-way**? → the pipe carries the video bitrate.
- [ ] **Step 6: Decision.** All green → **GO**: proceed to plan M1–M4. Any red (no accessory mode / no endpoints / echo fails / throughput too low) → **NO-GO or investigate**: capture the exact failure and diagnose before committing to the full transport.

---

## Notes for the implementer

- This is a spike: there are no unit tests. The deliverable of Task 3 is a **recorded observation** (works / doesn't, with numbers), not passing assertions.
- Everything here is throwaway on the `feat/aoa-usb-transport` branch. M2 replaces `aoa_spike.c`'s handshake logic with the real `AoaChannel`; M4 replaces `AoaEchoActivity` with the real `UsbAccessory` transport entry. Do not merge the spike as-is.
- If the Nexus never shows the accessory dialog, the identification strings vs `accessory_filter.xml` mismatch is the first thing to check (both must be manufacturer=`droppix`, model=`droppix`).
