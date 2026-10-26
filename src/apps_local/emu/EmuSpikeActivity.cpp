#include "EmuSpikeActivity.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "fontIds.h"

#if FREEINK_DEVICE_LILYGO
#include <FastEPD.h>

// Defined in freeink-sdk LgfxEpdDriver.cpp.
namespace freeink {
bool lgfxEpdSuspend();
bool lgfxEpdResume();
}  // namespace freeink
#endif

std::unique_ptr<Activity> EmuSpikeActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<EmuSpikeActivity>(renderer, mappedInput);
}

void EmuSpikeActivity::onEnter() {
  Activity::onEnter();
  stage = Stage::Intro;
  round = 0;
  report[0] = '\0';
  requestUpdate();
}

bool EmuSpikeActivity::runHandoff() {
#if FREEINK_DEVICE_LILYGO
  if (!freeink::lgfxEpdSuspend()) {
    snprintf(report, sizeof(report), "suspend failed");
    return false;
  }
  bool ok = false;
  {
    FASTEPD* panel = new (std::nothrow) FASTEPD();
    if (!panel) {
      snprintf(report, sizeof(report), "out of memory for FastEPD");
    } else {
      int rc = panel->initPanel(BB_PANEL_EPDIY_V7, 20000000);
      if (rc == BBEP_SUCCESS) rc = panel->setPanelSize(960, 540);
      if (rc == BBEP_SUCCESS) rc = panel->setMode(BB_MODE_1BPP);
      if (rc != BBEP_SUCCESS) {
        snprintf(report, sizeof(report), "FastEPD init failed rc=%d", rc);
      } else {
        panel->fillScreen(BBEP_WHITE);
        panel->drawRect(0, 0, 960, 540, BBEP_BLACK);
        panel->drawRect(8, 8, 944, 524, BBEP_BLACK);
        // Eight black bars, widening, so a bus fault shows as a broken edge.
        for (int i = 0; i < 8; ++i) panel->fillRect(60 + i * 100, 100, 10 + i * 10, 340, BBEP_BLACK);
        rc = panel->fullUpdate(CLEAR_SLOW, false);
        if (rc != BBEP_SUCCESS) {
          snprintf(report, sizeof(report), "FastEPD update failed rc=%d", rc);
        } else {
          delay(3000);
          ok = true;
        }
      }
      panel->deInit();
      delete panel;
    }
  }
  const bool back = freeink::lgfxEpdResume();
  if (!back) {
    snprintf(report, sizeof(report), "resume failed (FastEPD ok=%d)", ok ? 1 : 0);
    return false;
  }
  if (ok) snprintf(report, sizeof(report), "round %d: handoff and return OK", round);
  return ok;
#else
  snprintf(report, sizeof(report), "T5 only");
  return false;
#endif
}

void EmuSpikeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (stage == Stage::Running) {
    if (!runningPageShown) return;  // render() has not drawn it yet
    renderer.waitRefreshComplete();
    delay(300);  // let the panel's last scan settle before the bus is released
    runningPageShown = false;
    ++round;
    const bool ok = runHandoff();
    LOG_INF("EMUSPIKE", "%s: %s", ok ? "PASS" : "FAIL", report);
    stage = Stage::Done;
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    requestUpdate();
    return;
  }
  if (stage == Stage::Done || stage == Stage::Intro) {
    // Any tap runs another round (to check that repeat cycles work).
    int tapX = 0;
    int tapY = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tapX, tapY)) {
      stage = Stage::Running;
      runningPageShown = false;
      requestUpdate();
    }
  }
}

void EmuSpikeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, "Emulator panel test");

  const int bodyY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bodyH = sh - bodyY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const char* text = "Tap anywhere to hand the screen to the emulator driver for three seconds and back. Swipe from the left edge to leave.";
  if (stage == Stage::Running) text = "Running. The screen will flash, show bars, then return here.";
  if (stage == Stage::Done) text = report;
  UITheme::drawCenteredWrappedText(renderer, Rect{0, bodyY, sw, bodyH}, UI_12_FONT_ID, text, 5);

  const auto labels = mappedInput.mapLabels("Back", "Run", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
  if (stage == Stage::Running) runningPageShown = true;
}
