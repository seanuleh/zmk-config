# Handoff: dongle broken after round-2 flash (2026-11-11)

## Current state

- **Keyboard is not working.** Halves don't pair. The dongle OLED shows the
  *old* built-in screen (`USB` icon, `⚡100%`) after flashing `717140e` three times.
- The `717140e` dongle UF2 contains no battery widget code (verified by
  unpacking it), so the dongle is **not running normally**. The likely cause is
  that the new firmware hangs or crash-loops before its first draw, so the
  SSD1306 keeps showing its last frame.
- **Suspected cause (fixed in `3ed6e07`, not flashed or tested):** switching the
  dongle to a custom status screen dropped `LV_Z_MEM_POOL_SIZE` from 4096
  (ZMK's default only applies to the built-in screen) to 2048. The screen and
  link panel likely overflow it. Now 8192, with the low-prio stack at 1536.
  This is unconfirmed. Logs will tell.

## What changed since the last working build (`a17708e`)

| Commit | Change |
|---|---|
| `717140e` | `CONFIG_ZMK_BLE_EXPERIMENTAL_CONN=y` for all (1M PHY). Dongle: battery reporting off, custom screen (`widgets/dongle_status_screen.c`) + link panel (`widgets/link_widget.c`: HCI Read RSSI every 2 s on the low-prio queue, `BT_CONN_CB_DEFINE` drop counter, side learned from key positions) |
| `3ed6e07` | Dongle LVGL heap 8192, low-prio stack 1536, NULL-check on the label |
| next | This note + `sofled_dongle-usb-logging` CI artifact |

Halves only changed the PHY setting. They should be fine on `717140e`/`3ed6e07`.

## Next steps on the MBA

1. Download the latest CI run's `firmware` zip for this branch.
2. Flash the dongle with **`sofled_dongle-usb-logging.uf2`**: double-tap reset, then copy.
   Only the dongle should be plugged in while flashing.
3. Read the logs:
   `ls /dev/tty.usbmodem*`, then `screen /dev/tty.usbmodemXXXX 115200`
   (or `tio`). Replug or reset to catch boot. Logging delays boot by a few seconds.
4. What to look for:
   - `LVGL` / `lv_` malloc or assert failures: heap theory confirmed.
   - `Controller unresponsive ... 0x1405`: the RSSI HCI read is the culprit.
   - MPU / stack fault naming a thread: raise that stack.
   - Normal boot + `split link L dropped, reason 0x..`: panel works, read the reasons.
5. If the logging build works, flash the normal `sofled_dongle-…uf2` from the same run.
6. If the halves still won't pair once the dongle boots: `settings_reset` on all
   three, then re-flash. Flash one device at a time with only it plugged in.

## Escape hatch

To get a working keyboard back: flash the **`a17708e`** CI artifacts (run
42040398636) to all three. That build worked, apart from the intermittent drops.

## Original problem (still open)

Either half intermittently drops, with both on USB power, which points at the
dongle side or RF. The dongle's `0%`/`100%` battery is a floating BAT pin, so it's
meaningless. TX power is +8 dBm everywhere. See `docs/connectivity-fix-test-plan.md`.
