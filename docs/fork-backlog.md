# Fork backlog (Kpez16/crossplay)

Work this fork owes itself, on top of what upstream CrossPlay tracks in
[open-items.md](open-items.md). That file is upstream's and merges on every
sync, so fork items live here instead. Delete an item when it is done.

Started 2026-11-13, after the EEGO Reader A4 first booted CrossPlay.

## 1. EEGO A4 display tweaks

The A4 runs (branch `feature/eego-a4`): it boots, reads the SD card, opens
books and plays games. These are what is left to make it look right.

- **Screens laid out for an 800 px tall panel.** The A4 is 552 x 768 in
  portrait, so anything pinned to 800 loses its bottom 32 px, and anything
  sized from 480 underfills the 552 width. Known sites:
  - Knucklebones: `src/apps_local/knucklebones/KnucklebonesScreens.cpp:81`
    (`kCapsuleTop = 800 - ...`)
  - Toy Battle: `src/apps_local/toybattle/ToyBattleScreens.cpp:34`,
    `ToyBattleMenus.cpp:403`
  - xkcd reader: `src/apps_local/xkcd/XkcdActivity.cpp:54`
    (`kReaderViewportH = 800 - 44`), `XkcdCore.h:63` (`kPanelWidth = 480`)
  - Connections: `src/apps_local/connections/ConnectionsScreens.h:49`
    (tile width from 480)

  Fix by deriving from `renderer.getScreenWidth()/getScreenHeight()`. Check
  with `pio run -e simulator_eego_a4`, which sizes the simulator at 768x552
  (size only: the A4's bezel insets and grayscale path are not simulated).
- **Display and touch fixes already made upstream in Free-Ink/freeink-sdk**,
  not yet in our SDK (`kpez16/freeink-sdk` `eego-a4`, based on `839fecf`):
  - `249b934` screen graying while idle on USB
  - `8ff2cec` keep the panel powered between Gray3 page turns
  - `513b9fe` use the stock power settings for all e-ink sessions
  - `1132622` raise the home tab bar 10 px
  - touch calibration fitted from hardware taps (profile range
    12..779 / 28..553, ours is 0..767 / 0..551), plus `567f448`,
    `3c2078b`, `56b4ee1`
  - `ec420cc` frontlight brightness constant across warmth

  These arrive with item 2's SDK rebase rather than one by one.
- **Bezel insets.** The panel has rounded corners; the A4 profile's insets are
  marked "tune on hardware". Check text and status icons at the corners in all
  four orientations.
- **Ghosting and gray levels on real glass**, the open item in the SDK's
  `docs/eego-a4-support.md`: long FAST-only menu navigation and AA page turns.
- **Battery percentage (not display, but found here).** Our A4 has the CW2015
  gauge, so GPIO6 is now SD power and is driven LOW. Our SDK still reads the
  battery on the GPIO10 ADC, which upstream found only works with GPIO6 HIGH.
  Check whether the percentage is plausible; the real fix is upstream's CW2015
  support, again via item 2.
- Add anything else seen on the device here.

## 2. Pull in the latest CrossPoint upstream features

Where things stand: `lilygo-on-upstream` is `ma-r-s/crossplay` trunk (synced
2026-10-16) plus the fork's stack, and carries CrossPoint **1.6.5**. CrossPoint
`develop` is at **2.0.0**, 169 commits later (checked 2026-11-13). Features
since then include:

- reader: improved EPUB rendering (#3891), static GIFs (#2299), Image
  Grayscale toggle (#4160), line spacing as a percentage of font size (#4174)
- dictionary: recursive lookup (#4063), vocabulary history (#4294)
- library and files: skip special root folders (#4373), Show File Extensions
  (#4450), OPDS browser moved to File Transfer (#4216), SD usage in About
  (#4331)
- devices: **LilyGO T5S3 expander button mapped to Down (#4182)**, BOOX Picco
  (#3947), Bluetooth quick toggle (#4187)
- plugins: catalog, events and web UI work (#4197-#4319)
- fonts: NotoSerif moved to an optional SD font (#4022), Japanese fonts in
  Manage Fonts (#4405)

Steps and the decision to make first:

1. **Merge the A4 work first.** `feature/eego-a4` into `lilygo-on-upstream`,
   and SDK `eego-a4` into `lilygot5-on-upstream`, so the sync starts from one
   line.
2. **Decide the route.** (a) Sync with `ma-r-s/crossplay` as the runbook
   describes; it only gets CrossPoint features once Mario merges them, and
   CrossPlay's trunk may not be on 2.0.0 yet. (b) Take CrossPoint `develop`
   directly; this skips CrossPlay's games and fixes, and upstream's
   squash-merges make history merges conflict heavily (see the runbook), so it
   means cherry-picking features. Check where `ma-r-s/crossplay` is first.
3. **Rebase the SDK onto Free-Ink/freeink-sdk upstream.** Keep the T5 battery
   work (BQ27220 provisioning, `isGaugeProvisioned()`); drop our adapted A4 SD
   power fix in favour of upstream's `ed7c5c3` plus its CW2015 gauge support.
   Expect conflicts in `BoardConfig.h` and `BatteryMonitor`.
4. Rebuild `gh_release_lilygo_t5s3` and `gh_release_eego_a4`, run
   `scripts_local/check-fork.sh --tests --committed`, then the on-device
   checklists for both boards (in the project runbook).
