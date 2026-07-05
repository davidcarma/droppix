# AOA M0 spike (throwaway)

Proves Android Open Accessory works on the Nexus 10 before building the real transport.

## Build
    distrobox enter droppix-dev -- bash -lc \
      'cd "<repo>/spike/aoa" && gcc aoa_spike.c -o aoa_spike $(pkg-config --cflags --libs libusb-1.0)'

## Run (needs the spike APK installed on the tablet)
1. Plug in the Nexus (MTP mode is fine).
2. `sudo <repo>/spike/aoa/aoa_spike`
3. On the tablet, accept "Open droppix for this USB accessory?".
4. Read the printed AOA protocol version, endpoint addresses, and echo throughput.

Success = accessory mode entered, endpoints found, ECHO OK, ~>= 8 Mbit/s one-way.
