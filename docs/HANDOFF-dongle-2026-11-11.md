# Handoff: dongle and split stability (2026-11-11, resolved)

## Status

The keyboard is working and the dongle is on the normal firmware. Each half
now drops very rarely; the last test had one drop in several minutes of
typing. Long-term stability isn't confirmed yet, so watch the drop counts on
the dongle panel.

## What was wrong (three bugs)

1. **The dongle's BLE clock (main cause).** The nrfmicro has no 32 kHz
   crystal, so ZMK's board default ran the BLE sleep clock from the
   calibrated RC oscillator (500 ppm, drifts with temperature). As the
   central device timing two 7.5 ms links, it lost sync with the halves.
   Both halves dropped every 5–10 s with supervision timeouts (0x08), even
   at −49 dBm. Reconnects then failed in runs (0x3e, "Security failed
   err 9"). Resetting the halves didn't help.
   **Fix (`5967a65`):** `CONFIG_CLOCK_CONTROL_NRF_K32SRC_SYNTH=y` plus
   `_50PPM=y` in `sofled_dongle.conf`, which derives the clock from the
   32 MHz crystal. Drops fell from about 15 in 75 s to 1.

2. **ZMK bug: a dropped half never came back.** `start_scanning()` in
   `app/src/split/bluetooth/central.c` left `is_scanning` set when
   `bt_le_scan_start()` failed. That happens with -EPERM whenever one half
   fails to connect while the other is still connecting. Every later
   reconnect attempt was skipped until the dongle rebooted.
   **Fix:** `seanuleh/zmk@sofled-split-fixes` clears the flag and retries
   after 200 ms. `config/west.yml` now points at that branch.

3. **The dongle advertised to BLE hosts non-stop.** No host was ever bonded,
   so it never stopped. The same branch adds
   `CONFIG_ZMK_BLE_HOST_ADVERTISING`, set to `n` on the dongle (USB-only), so
   the radio serves only the halves.

Commit `3ed6e07` (LVGL heap 8192) had already fixed the boot hang.

## Dongle link panel

```
L ok  0     side (L/R, "?" until you press a key on that half after a dongle boot)
R ok  2     "ok" connected / "--" down, then drops since the dongle booted
```

- Restarting the dongle resets both counts to 0.
- Restarting a half adds 1 to that half's count.
- Failed connection attempts (0x3e) aren't counted.
- RSSI polling is behind `CONFIG_SOFLED_LINK_RSSI` (off).
- The old third row (last drop reason) was removed.

## If drops come back

- **Get logs.** Flash `sofled_dongle-usb-logging.uf2` and read
  `/dev/cu.usbmodem*` (macOS) or `/dev/ttyACM*` (Linux) at 115200. Keep only
  one reader open at a time. Drops are logged as `split link L/R dropped,
  reason 0x..`.
- **Don't judge typing quality on the logging build.** Its debug output
  (~15 lines per key over the same USB as HID) caused garbled typing with no
  drops, and its log loses lines under load.
- **Placement and noise.** The desk is a busy 2.4 GHz environment (Wi-Fi,
  ESP32s). The next cheap step is a USB extension cable, so the dongle has
  line of sight to both halves and sits away from the Mac's USB 3 ports.
- **The right half's signal is about 30 dB weaker than the left's** (−80s vs
  −50s dBm), even on USB power. If the right is the one dropping, suspect its
  antenna area first: the controller's orientation, or the battery, plate or
  display near it.

## Maintenance

To pick up upstream ZMK changes, rebase `sofled-split-fixes` onto
`zmkfirmware/zmk` main and push it to `seanuleh/zmk`. The scan fix is a good
candidate to send upstream as a PR.
