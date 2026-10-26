#include "GbPlayerActivity.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../components/UITheme.h"
#include "fontIds.h"

#if FREEINK_DEVICE_LILYGO
#include <FastEPD.h>

#include "GameCore.h"
#include "GameFrameTiming.h"
#include "GameViewport.h"

// Defined in freeink-sdk LgfxEpdDriver.cpp.
namespace freeink {
bool lgfxEpdSuspend();
bool lgfxEpdResume();
}  // namespace freeink
#endif

#if FREEINK_DEVICE_LILYGO
namespace {
// Portrait, the way InkDeck holds the board by default. If the picture comes up
// upside down on a device held the CrossPlay way, this is the one number to flip
// (1 <-> 3).
constexpr uint8_t kRotation = 3;
constexpr int kInset = 8;  // LilygoLayout::INSET: the canvas sits inside the bezel
constexpr size_t kMaxRomBytes = 4U * 1024U * 1024U;
constexpr uint32_t kEmuStack = 5120;

// Internal RAM if there is a block for it, PSRAM otherwise. CrossPlay itself holds
// most of the internal heap (see the memory notes in docs), so this is a ranking of
// who gets the little that is left, not a promise.
uint8_t* allocInternalFirst(size_t bytes, const char* what) {
  auto* p = static_cast<uint8_t*>(heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const bool internal = p != nullptr;
  if (!p) p = static_cast<uint8_t*>(heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  LOG_INF("GBSPIKE", "%-14s %6u bytes in %s", what, unsigned(bytes), p ? (internal ? "internal" : "PSRAM") : "NO MEMORY");
  return p;
}
uint8_t* allocPsramFirst(size_t bytes, const char* what) {
  auto* p = static_cast<uint8_t*>(heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const bool psram = p != nullptr;
  if (!p) p = static_cast<uint8_t*>(heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  LOG_INF("GBSPIKE", "%-14s %6u bytes in %s", what, unsigned(bytes), p ? (psram ? "PSRAM" : "internal") : "NO MEMORY");
  return p;
}
void logHeap(const char* where) {
  LOG_INF("GBSPIKE", "%s: internal free %u, largest block %u; PSRAM free %u", where,
          unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}
}  // namespace

// Everything the two tasks share. Lives on the heap for the length of one game.
//
// The two packed-frame slots: the scan task shows `cur`; the emulator task only
// ever writes the other one, then publishes it as `latest`. The scan task adopts
// `latest` between scans. A frame the scanner never adopted is withdrawn and
// overwritten (dropped), so the game keeps its own speed however slow the panel is,
// and a slot is never written while it is being scanned.
struct GbPlayerActivity::PlayState {
  uint8_t* rom = nullptr;
  uint8_t* history = nullptr;
  uint8_t* slot[2] = {nullptr, nullptr};
  uint8_t* work = nullptr;
  bool coreOpen = false;

  uint8_t* history2 = nullptr;  // rows >= historySplit, when no single internal block fits
  int historySplit = 0;

  SemaphoreHandle_t mtx = nullptr;
  SemaphoreHandle_t go = nullptr;     // released by prepare(): scan task may start
  SemaphoreHandle_t emuGo = nullptr;  // released by the scan task: emulator may start
  std::atomic<bool> prepareOk{false};
  int cur = 0;
  int latest = -1;
  TaskHandle_t emuHandle = nullptr;

  std::atomic<bool> emuDone{false};
  std::atomic<bool> emuFailed{false};
  std::atomic<uint32_t> emuFrames{0};
  std::atomic<uint32_t> emuUs{0};
  std::atomic<uint32_t> packUs{0};
  std::atomic<uint32_t> emuStackLeft{0};
  char error[96] = "";
};
#else
struct GbPlayerActivity::PlayState {};
#endif

void GbPlayerActivity::onEnter() {
  Activity::onEnter();
  stage = Stage::Launch;
  launchPageShown = false;
  stopRequested = false;
  taskFinished = false;
  report[0] = '\0';
  requestUpdate();
}

void GbPlayerActivity::onExit() {
  // Never leave a task running over a destroyed activity.
  if (ps && stage == Stage::Playing) {
    stopRequested = true;
    for (int i = 0; i < 400 && !taskFinished; ++i) delay(20);
  }
  releaseAll();
  Activity::onExit();
}

#if FREEINK_DEVICE_LILYGO

void GbPlayerActivity::releaseAll() {
  if (!ps) return;
  if (ps->coreOpen) inkGameClose();
  free(ps->history);
  free(ps->history2);
  free(ps->slot[0]);
  free(ps->slot[1]);
  free(ps->work);
  free(ps->rom);
  if (ps->mtx) vSemaphoreDelete(ps->mtx);
  if (ps->go) vSemaphoreDelete(ps->go);
  if (ps->emuGo) vSemaphoreDelete(ps->emuGo);
  delete ps;
  ps = nullptr;
}

// Main thread. Everything that needs internal RAM is asked for here, first, before
// the tasks' stacks and the core's own tables fragment what is left.
bool GbPlayerActivity::prepare() {
  using namespace GameViewport;
  report[0] = '\0';  // ps and the two tasks already exist; see loop()

  logHeap("start (stacks already taken)");
  // The video history is the hot buffer: the scan reads and writes all of it every
  // frame. It wants internal RAM, but no single internal block is big enough
  // (largest ~31 KB, history 34.5 KB), so it is allocated in up to two parts: as
  // many rows as the biggest block holds, then the rest wherever it fits.
  const int groups = W / SCALE;          // rect.h / rowRepeat source rows
  const size_t rowState = H / 2;         // rect.w / 2 state bytes per source row
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  int firstGroups = largest > 1024 ? int((largest - 1024) / rowState) : 0;
  if (firstGroups > groups) firstGroups = groups;
  if (firstGroups < 1) firstGroups = 1;
  ps->history = allocInternalFirst(size_t(firstGroups) * rowState, "history part A");
  if (firstGroups < groups) {
    ps->history2 = allocInternalFirst(size_t(groups - firstGroups) * rowState, "history part B");
    ps->historySplit = firstGroups;
  }
  // The sequentially-read packed frames and the work frame go to PSRAM.
  ps->slot[0] = allocPsramFirst(PACKED_BYTES, "packed slot 0");
  ps->slot[1] = allocPsramFirst(PACKED_BYTES, "packed slot 1");
  ps->work = allocPsramFirst(SOURCE_W * SOURCE_H, "work frame");
  if (!ps->history || (firstGroups < groups && !ps->history2) || !ps->slot[0] || !ps->slot[1] || !ps->work) {
    snprintf(report, sizeof(report), "Not enough memory for the frame buffers.");
    return false;
  }

  HalFile f;
  if (!Storage.openFileForRead("GBSPIKE", romPath, f)) {
    snprintf(report, sizeof(report), "Cannot open %s.", romPath.c_str());
    return false;
  }
  const size_t bytes = f.size();
  if (bytes < 32768 || bytes > kMaxRomBytes) {
    snprintf(report, sizeof(report), "ROM size %u is outside 32 KB..4 MB.", unsigned(bytes));
    f.close();
    return false;
  }
  ps->rom = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ps->rom) {
    snprintf(report, sizeof(report), "Not enough PSRAM for the ROM.");
    f.close();
    return false;
  }
  size_t got = 0;
  while (got < bytes) {
    const int n = f.read(ps->rom + got, std::min<size_t>(4096, bytes - got));
    if (n <= 0) break;
    got += n;
  }
  f.close();
  if (got != bytes) {
    snprintf(report, sizeof(report), "Short read from the SD card.");
    return false;
  }
  if (!inkGameOpen(ps->rom, bytes)) {
    snprintf(report, sizeof(report), "Core refused the ROM: %s", inkGameError());
    return false;
  }
  ps->coreOpen = true;
  logHeap("core open");
  return true;
}

void GbPlayerActivity::scanEntry(void* self) {
  auto* a = static_cast<GbPlayerActivity*>(self);
  a->scanTask();
  a->taskFinished = true;
  vTaskDelete(nullptr);
}

void GbPlayerActivity::emuEntry(void* self) {
  auto* a = static_cast<GbPlayerActivity*>(self);
  a->emuTask();
  vTaskDelete(nullptr);
}

// Core 0. Real-time producer: one core frame every 16.743 ms, packed into the slot
// the scanner is not showing.
void GbPlayerActivity::emuTask() {
  PlayState& s = *ps;
  xSemaphoreTake(s.emuGo, portMAX_DELAY);  // the scan task has cleaned the screen
  int64_t next = esp_timer_get_time();
  while (!stopRequested) {
    const int64_t t0 = esp_timer_get_time();
    if (!inkGameFrame(0, s.work)) {
      snprintf(s.error, sizeof(s.error), "Core stopped: %s", inkGameError());
      s.emuFailed = true;
      break;
    }
    const int64_t t1 = esp_timer_get_time();

    int p;
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    p = 1 - s.cur;
    if (s.latest == p) s.latest = -1;  // withdraw a frame the scanner never took
    xSemaphoreGive(s.mtx);
    GameViewport::pack(s.slot[p], s.work, kRotation);
    const int64_t t2 = esp_timer_get_time();
    xSemaphoreTake(s.mtx, portMAX_DELAY);
    s.latest = p;
    xSemaphoreGive(s.mtx);

    s.emuUs += uint32_t(t1 - t0);
    s.packUs += uint32_t(t2 - t1);
    ++s.emuFrames;

    next += GameFrameTiming::PERIOD_US;
    const int64_t now = esp_timer_get_time();
    const int64_t remaining = next - now;
    if (remaining < -100000) {
      next = now;  // far behind: rebase instead of replaying
      vTaskDelay(1);
    } else if (remaining > 1000) {
      vTaskDelay(pdMS_TO_TICKS(remaining / 1000));
    } else {
      vTaskDelay(1);
    }
  }
  s.emuStackLeft = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
  s.emuDone = true;
}

// Core 1. Owns the panel from handoff to handback.
void GbPlayerActivity::scanTask() {
  using namespace GameViewport;
  PlayState& s = *ps;
  FASTEPD* panel = nullptr;
  bool suspended = false;

  auto fail = [&](const char* why) { snprintf(report, sizeof(report), "%s", why); };

  xSemaphoreTake(s.go, portMAX_DELAY);  // prepare() is done (or has failed)
  if (!s.prepareOk || stopRequested) goto done;

  if (!freeink::lgfxEpdSuspend()) {
    fail("Could not release the panel from CrossPlay.");
    goto done;
  }
  suspended = true;
  panel = new (std::nothrow) FASTEPD();
  if (!panel) {
    fail("Out of memory for FastEPD.");
    goto done;
  }
  {
    int rc = panel->initPanel(BB_PANEL_EPDIY_V7, 20000000);
    if (rc == BBEP_SUCCESS) rc = panel->setPanelSize(960, 540);
    if (rc == BBEP_SUCCESS) rc = panel->setMode(BB_MODE_1BPP);
    if (rc != BBEP_SUCCESS) {
      snprintf(report, sizeof(report), "FastEPD init failed rc=%d", rc);
      goto done;
    }
    panel->setVideoPulses(6, false);
    panel->setVideoScanTail(false);
    panel->setPasses(4);
    panel->setVideoHistoryTail(s.history2, s.historySplit);

    const BB_RECT rect = {nativeX(kRotation) + kInset, nativeY(kRotation) + kInset, H, W};

    // Warm the game up here, before the emulator task exists, so the first picture
    // is the title screen and not the black power-on frame.
    for (int i = 0; i < 6; ++i) {
      if (!inkGameFrame(0, s.work)) {
        snprintf(report, sizeof(report), "Core stopped: %s", inkGameError());
        goto done;
      }
    }
    pack(s.slot[0], s.work, kRotation);
    s.cur = 0;

    // Clean start: white screen, then the first frame blitted into the panel's own
    // buffer and driven with the slow waveform inside the game rectangle.
    panel->fillScreen(BBEP_WHITE);
    panel->fullUpdate(CLEAR_SLOW, true);
    uint8_t* buf = panel->currentBuffer();
    memset(buf, 0xff, 960 * 540 / 8);
    for (int row = 0; row < SOURCE_W; ++row)
      for (int rep = 0; rep < SCALE; ++rep)
        memcpy(buf + (rect.y + row * SCALE + rep) * (960 / 8) + rect.x / 8, s.slot[0] + row * ROW_BYTES, ROW_BYTES);
    panel->fullUpdate(CLEAR_SLOW, true, const_cast<BB_RECT*>(&rect));

    // Now the real-time producer (its task already exists, waiting).
    xSemaphoreGive(s.emuGo);

    bool reset = true;
    uint32_t scans = 0, repeats = 0;
    uint32_t scanUsTotal = 0;
    uint32_t lastEmuFrames = 0, lastEmuUs = 0, lastPackUs = 0;
    int64_t started = esp_timer_get_time();
    int64_t lastLog = started;
    int scanRc = BBEP_SUCCESS;
    while (!stopRequested && !s.emuFailed) {
      bool fresh = false;
      xSemaphoreTake(s.mtx, portMAX_DELAY);
      if (s.latest >= 0 && s.latest != s.cur) {
        s.cur = s.latest;
        s.latest = -1;
        fresh = true;
      }
      const int showing = s.cur;
      xSemaphoreGive(s.mtx);
      if (!fresh) ++repeats;

      const int64_t t0 = esp_timer_get_time();
      scanRc = panel->videoScan(rect, s.slot[showing], s.history, reset, SCALE, kRotation == 3);
      reset = false;
      const int64_t t1 = esp_timer_get_time();
      scanUsTotal += uint32_t(t1 - t0);
      ++scans;
      if (scanRc != BBEP_SUCCESS) {
        snprintf(report, sizeof(report), "videoScan failed rc=%d after %u scans", scanRc, unsigned(scans));
        break;
      }
      if (t1 - lastLog >= 5000000) {
        const double secs = (t1 - lastLog) / 1e6;
        const uint32_t ef = s.emuFrames, eu = s.emuUs, pu = s.packUs;
        const uint32_t df = ef - lastEmuFrames;
        LOG_INF("GBSPIKE", "scans %.1f/s (%.0f%% repeats), scan %.2f ms | emu %.1f fps, core %.2f ms, pack %.2f ms",
                scans / secs, scans ? 100.0 * repeats / scans : 0.0, scans ? scanUsTotal / (1000.0 * scans) : 0.0,
                df / secs, df ? (eu - lastEmuUs) / (1000.0 * df) : 0.0, df ? (pu - lastPackUs) / (1000.0 * df) : 0.0);
        lastEmuFrames = ef;
        lastEmuUs = eu;
        lastPackUs = pu;
        scans = repeats = scanUsTotal = 0;
        lastLog = t1;
      }
      vTaskDelay(1);  // idle task and watchdog
    }
    const double total = (esp_timer_get_time() - started) / 1e6;
    if (s.emuFailed) {
      snprintf(report, sizeof(report), "%s", s.error);
    } else if (scanRc == BBEP_SUCCESS && report[0] == '\0') {
      // A clean run leaves `report` empty: loop() returns to the ROM list on that.
      LOG_INF("GBSPIKE", "Played %.1f s: %u emulator frames (%.1f fps).", total, unsigned(s.emuFrames.load()),
              s.emuFrames.load() / total);
    }
  }

done:
  // The producer must be gone before the core is closed. It may still be waiting for
  // its start signal, so release that too.
  stopRequested = true;
  xSemaphoreGive(s.emuGo);
  for (int i = 0; i < 100 && !s.emuDone; ++i) vTaskDelay(pdMS_TO_TICKS(10));
  if (panel) panel->setVideoHistoryTail(nullptr, 0);
  if (panel) {
    panel->videoNeutral();
    panel->einkPower(0);
    panel->deInit();
    delete panel;
    panel = nullptr;
  }
  if (suspended && !freeink::lgfxEpdResume()) {
    char before[sizeof(report)];
    snprintf(before, sizeof(before), "%s", report);
    snprintf(report, sizeof(report), "%s (CrossPlay could not retake the panel)", before);
  }
  LOG_INF("GBSPIKE", "stack left: scan task %u bytes, emulator task %u bytes",
          unsigned(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)), unsigned(s.emuStackLeft.load()));
  if (report[0]) LOG_ERR("GBSPIKE", "%s", report);
}

#else  // not a T5 build

void GbPlayerActivity::releaseAll() {}
bool GbPlayerActivity::prepare() {
  snprintf(report, sizeof(report), "T5 only");
  return false;
}
void GbPlayerActivity::scanEntry(void*) {}
void GbPlayerActivity::emuEntry(void*) {}
void GbPlayerActivity::scanTask() {}
void GbPlayerActivity::emuTask() {}

#endif

void GbPlayerActivity::loop() {
  if (stage == Stage::Playing) {
    int tx = 0, ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(tx, ty)) {
      stopRequested = true;
    }
    if (taskFinished) {
      releaseAll();
      renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
      if (report[0]) {  // a failure: say why before leaving
        stage = Stage::Done;
        requestUpdate();
      } else {
        finish();  // back to the ROM list
      }
    }
    return;
  }
  if (stage == Stage::Launch) {
    if (!launchPageShown) return;  // render() has not handed the page to the panel yet
    renderer.waitRefreshComplete();
    delay(300);
    stopRequested = false;
    taskFinished = false;
    report[0] = '\0';
#if FREEINK_DEVICE_LILYGO
    // Stacks first. They need contiguous internal blocks and are the one thing here
    // that cannot fall back to PSRAM, so they are taken before anything else can
    // fragment the heap. Both tasks then wait: the scan task for prepare(), the
    // emulator task for the cleaned screen.
    ps = new (std::nothrow) PlayState();
    if (ps) {
      ps->mtx = xSemaphoreCreateMutex();
      ps->go = xSemaphoreCreateBinary();
      ps->emuGo = xSemaphoreCreateBinary();
    }
    logHeap("before stacks");
    TaskHandle_t scanHandle = nullptr;
    const bool tasksOk = ps && ps->mtx && ps->go && ps->emuGo &&
                         xTaskCreatePinnedToCore(&GbPlayerActivity::scanEntry, "gbscan", 8192, this, 3, &scanHandle, 1) == pdPASS &&
                         xTaskCreatePinnedToCore(&GbPlayerActivity::emuEntry, "gbemu", kEmuStack, this, 2, &ps->emuHandle, 0) == pdPASS;
    if (!tasksOk) {
      snprintf(report, sizeof(report), "Could not start the game tasks (internal RAM).");
      if (scanHandle) {  // scan task exists and is waiting: let it unwind
        ps->prepareOk = false;
        stopRequested = true;
        xSemaphoreGive(ps->go);
        stage = Stage::Playing;  // loop() releases everything when it reports finished
        return;
      }
      releaseAll();
      stage = Stage::Done;
      requestUpdate();
      return;
    }
    const bool ok = prepare();
    ps->prepareOk = ok;
    if (!ok) stopRequested = true;
    xSemaphoreGive(ps->go);
#else
    snprintf(report, sizeof(report), "T5 only");
    stage = Stage::Done;
    requestUpdate();
    return;
#endif
    stage = Stage::Playing;
    return;
  }
  // Done: only reached with a failure page showing.
  int tx = 0, ty = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(tx, ty)) {
    finish();
  }
}

void GbPlayerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, "Game Boy");

  const int bodyY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bodyH = sh - bodyY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const char* text = "";
  if (stage == Stage::Launch) text = "Loading. The screen will clear and the game will start.";
  if (stage == Stage::Done) text = report[0] ? report : "Done.";
  UITheme::drawCenteredWrappedText(renderer, Rect{0, bodyY, sw, bodyH}, UI_12_FONT_ID, text, 6);

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
  if (stage == Stage::Launch) launchPageShown = true;
}
