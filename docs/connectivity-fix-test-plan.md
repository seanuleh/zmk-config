# Split connectivity fix — NEEDS FLASHING AND TESTING

**Status (2026-11-06): built locally, NOT yet flashed or tested on the keyboard.**
Branch: `claude-dude-screen`. Delete this doc (or move its result into the README)
once the test below has been done.

## Symptoms being fixed

- Right half intermittently stops reporting to the dongle
- Laggy key presses; characters from the two halves arriving out of order
- Random capital letters (auto-shift hold firing on a normal tap)

## What changed

| Where | Change | Why |
|---|---|---|
| `widgets/wpm_graph_widget.c`, `widgets/dude_widget.c` | Tick work runs on `zmk_display_work_q()` instead of the system workqueue; connection listeners no longer call LVGL; labels only updated when their value changes; WPM canvas not redrawn while the graph is flat; dude ticks at 1 s while asleep | LVGL was being driven from two threads at once (corruption/hangs), and renders on the system workqueue blocked Bluetooth TX on the halves. Right half was worst because it re-rendered the canvas every second |
| `sofled_left.conf`, `sofled_right.conf` | `ZMK_DISPLAY_DEDICATED_THREAD_PRIORITY=10` | Display thread now ranks below split-BLE notify (prio 5) |
| `sofled_dongle.conf` | Dedicated display work queue (prio 10); `ZMK_SPLIT_BLE_CENTRAL_POSITION_QUEUE_SIZE=16` (was 5) | Dongle OLED flushes were blocking incoming key events and hold-tap timers; a full queue silently dropped presses/releases |
| `sofled_dongle.overlay` | Mock kscan replaces the key matrix; I2C at 400 kHz | Matrix column 0 (P0.29) was the same pin as I2C SDA. 100 kHz made each OLED frame ~45 ms |
| `config/sofled.keymap` | `require-prior-idle-ms = <150>` on `as` / `as_slow`; removed ignored `tapping_term_ms` / `quick_tap_ms` | A key pressed within 150 ms of the previous one is always a tap, so mid-word capitals can't happen |

**Behaviour change to expect:** an auto-shift capital now needs a ~150 ms pause
before the held key. Capitals mid-flow need a brief pause or the Shift key.

## Flash

Flash **all three** (the keymap is compiled into each, and each side's config changed).

1. Build: `scripts/build-local.sh` (or the GitHub Actions artifacts for this branch).
   UF2s end up in `build/{left,right,dongle}/zephyr/zmk.uf2`.
2. Dongle first: double-tap reset, copy `build/dongle/zephyr/zmk.uf2`.
3. Left, then right: same.
4. **Don't** run `settings_reset` up front. Only use it if a half won't reconnect.
   In that case flash `settings_reset` to all three, re-flash the firmware and let them pair.

## Test checklist

- [ ] Both halves connect after power-on; left screen shows the wifi + tick
- [ ] Right screen: WPM graph moves while typing, flat-lines when idle; batt% and the conn tick appear
- [ ] Left screen: the dude animates while typing, goes to "zzz" after 30 s, and wakes on a keypress
- [ ] Fast typing across both halves for a few minutes (e.g. a typing test): no out-of-order characters
- [ ] No stray capitals during normal typing
- [ ] Holding a letter after a pause still gives a capital
- [ ] Right half: leave idle for 10+ minutes, then type. It reconnects promptly with no dropped first keys
- [ ] Dongle OLED still shows layer / output / battery and changes layer with `&mo`
- [ ] Day-to-day use for a couple of days without the right half dropping out

## If problems remain

- **Right half still drops out:** build it with `CONFIG_ZMK_DISPLAY=n` in `sofled_right.conf`.
  If that fixes it, the display path is still to blame. If it doesn't, suspect RF: try
  the dongle on a USB extension near the keyboard, away from USB 3 ports.
- **Dongle OLED blank or garbled:** remove `clock-frequency` from `&i2c0` in
  `sofled_dongle.overlay` (back to 100 kHz).
- **nice!view flicker came back:** labels now update only on change, so this is
  unlikely. Check the widget tick paths first.
- **Ideas not done yet:**
  - `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=y` on the halves would stop LVGL ticking when idle,
    but it would freeze the dude before it reaches "zzz".
  - Restore the default VCOM interval (`serial-vcom-interval` 33).
