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

  // give the Android app a moment to receive the accessory-attached intent, launch, and
  // start reading/echoing before we measure — else the first bulk transfer races it.
  { struct timespec ready = {2, 0}; nanosleep(&ready, NULL); }
  printf("starting echo/throughput test...\n");

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
