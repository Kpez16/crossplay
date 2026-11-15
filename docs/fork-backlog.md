# Fork backlog (Kpez16/crossplay)

This branch, `crosspoint-main`, is CrossPoint `develop` plus a small set of
fork changes. The fork's goal: CrossPoint's reader and device support
(including the EEGO Reader A4 and the LilyGo T5 S3), with the fork's own apps
instead of CrossPlay's. Fork changes in upstream files carry a `FORK CHANGE`
comment so a sync can find them. Delete an item here when it is done.

Started 2026-11-13; rewritten 2026-11-14 for the move from CrossPlay to
CrossPoint.

## Fork changes on this branch

- `.gitmodules`: SDK from `kpez16/freeink-sdk` branch `crosspoint-main`, which
  is upstream `Free-Ink/freeink-sdk` at the commit CrossPoint pins plus, for the
  LilyGo T5 S3 only:
  - no power latch on GPIO2 (it is the RTC interrupt line);
  - BQ27220 gauge data-memory provisioning with LilyGo's 1500 mAh profile, run
    from the first SoC reads of a boot (at most 3 attempts);
  - `freeink::I2cBusLock` enabled from boot.
- `platformio.ini`: `[fork] version`; `lilygo_t5s3` and
  `lilygo_t5s3-gh_release` envs; all `eego_a4*` envs use the fork version.
- `src/network/OtaUpdater.cpp`: updates come from `Kpez16/crossplay` releases.
- `.github/workflows/ci.yml`: also builds on pushes to `crosspoint-main`, and
  builds `lilygo_t5s3`. GitHub disables workflows on forks until they are
  enabled in the repository's Actions tab.

## 1. Stage 1: CrossPoint 2.0 on both devices (this branch)

- Build `eego_a4-gh_release` and `lilygo_t5s3-gh_release`, flash both.
- On-device checks: boots and draws, buttons, touch corners, SD mounts (A4
  boot log: `[SD] EEGO A4: CW2015 present ...`), battery percentage plausible
  (A4: CW2015 gauge; T5: `[BATT] BQ27220 data-memory provisioning attempt
  1/3: ok`, and `needsProvisioning=0` on the next boot), sleep and wake,
  frontlight, Wi-Fi.
- T5: USB Drive is not enabled (`FREEINK_CAP_USB_MSC`); turn it on and test
  separately.
- **Releases do not work through `release.yml` yet.** It checks the tag
  against the first `version =` line in `platformio.ini` (upstream's 2.0.0, not
  `[fork]`), has no T5 entry, and expects exactly eight assets, so it fails on
  any fork release. Until it is adapted, publish releases by hand: tag exactly
  `0.2.0` (no `v`; the device builds the asset name from the raw tag) and attach
  `crosspoint-0.2.0-eego_a4.bin` and `crosspoint-0.2.0-lilygo.bin` (board names
  from `FirmwareBoardTag.cpp`).

## 2. Stage 2: the fork's own apps

Bring over CrossPlay's launcher and UI kit as the fork's app framework, then
Tak. CrossPlay's other apps are not carried over.

- From `lilygo-on-upstream`: `src/apps_local/Shelf*`, `src/apps_local/ui/`
  (Toybox, with its fonts), `src/apps_local/tak/`.
- CrossPlay hooked these into about ten CrossPoint files (Home screen, sleep
  screen, theme, settings, web server, renderer); each hook must be redone
  against CrossPoint 2.0, and kept as small as possible.
- Toybox uses two FreeInkUI additions that only CrossPlay's SDK had:
  `FreeInkApp::setContentMarginAbsolute()` and `header(props, Rect)`. Either
  add them to the fork's SDK or keep them inside Toybox.
- The FreeInkUI screens need a 16 KB render-task stack
  (`CROSSPOINT_RENDER_TASK_STACK` in CrossPlay; CrossPoint has no such flag).
- Layouts must derive from the screen size: the A4 is 552 x 768 in portrait,
  the T5 is 960 x 540.
- The emulator work (`gb-nes-emulator`) needs `lgfxEpdSuspend()` /
  `lgfxEpdResume()` from SDK commit `1ed40c9` on `lilygot5-on-upstream`; add it
  back with its caller. Before then: require callers to hold the render lock,
  and check that a second `Bus_EPD::init()` in M5GFX 0.2.24 does not leak.

## 3. Stage 3: simulator

Bring over the simulator targets for the A4 (768x552) and the T5 (960x540),
including the panel-size patches in `scripts_local/sim_catchup.py`, adapted
to whatever simulator setup CrossPoint `develop` uses.

## 4. T5 battery and I2C

- BQ27220 provisioning runs on the render task (from the battery icon) and
  can block drawing for several seconds when the gauge needs it. Better shape:
  give `BatteryGaugeConfig` an optional data-memory table, set it only on the
  T5, and write it through upstream's step-by-step `loadDesignCapacity()`
  loader in the main loop.
- `freeink::I2cBusLock` on the T5 covers the battery, RTC, frontlight and IMU
  helpers only. GT911 touch reads and `BoardT5S3::ScopedI2CLock` (PCA9535
  expander, TPS65185 PMIC) are not serialized with it. If I2C errors show up
  while touching the screen, make `ScopedI2CLock` take `I2cBusLock` and add it
  to the GT911 helpers.

## 5. A4 display, after stage 1 is on the device

Compared with the old `lilygo-on-upstream` branch, CrossPoint's SDK already
includes upstream's A4 fixes (touch calibration from hardware taps, Gray3
page-turn power, idle graying on USB, tab bar offset, frontlight warmth,
CW2015 battery, SD power on GPIO6). What is left:

- Bezel: check the A4 profile's insets `{24, 8, 16, 8}` and `cornerRadius 24`
  (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`, `EEGO_A4`)
  against the glass in all four orientations.
- Ghosting and gray levels on real glass during long FAST-only navigation and
  AA page turns (open item in the SDK's `docs/eego-a4-support.md`).
- Anything else seen on the device.

## Keeping up with upstream

- Firmware: merge or rebase `crosspoint-main` onto CrossPoint `develop`; fork
  changes are marked `FORK CHANGE`.
- SDK: rebase `kpez16/freeink-sdk` `crosspoint-main` onto the commit the new
  CrossPoint pins.
