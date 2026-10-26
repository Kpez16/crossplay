#pragma once

// Game Boy / Game Boy Color player (branch gb-nes-emulator): plays one ROM from
// the SD card through the FastEPD video scan. Launched by EmuRomListActivity.
//
// Two tasks, like InkDeck: an emulator task on core 0 produces packed frames at
// the game's own rate; a scan task on core 1 owns the panel and drives it as fast
// as it will go. They meet in two packed-frame buffers (see PlayState).
//
// No controls or saves yet: any tap or Back ends the run, and finish() returns
// to the ROM list. A failure shows its reason on a page first.

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "../../activities/Activity.h"

class GbPlayerActivity final : public Activity {
 public:
  GbPlayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string romPath)
      : Activity("GbPlayer", renderer, mappedInput), romPath(std::move(romPath)) {}
  ~GbPlayerActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Full CPU speed and no idle sleep while a game is on. Without skipLoopDelay the
  // main loop drops the CPU to 80 MHz after three quiet seconds, and a game nobody
  // is touching is exactly that: measured 14 fps instead of ~60.
  bool skipLoopDelay() override { return stage == Stage::Launch || stage == Stage::Playing; }
  bool preventAutoSleep() override { return stage == Stage::Launch || stage == Stage::Playing; }

 private:
  enum class Stage : uint8_t { Launch, Playing, Done };
  const std::string romPath;
  Stage stage = Stage::Launch;
  bool launchPageShown = false;
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> taskFinished{false};
  char report[200] = "";  // written before taskFinished is set, read after

  struct PlayState;
  PlayState* ps = nullptr;

  bool prepare();  // main thread: ROM, buffers, core. False leaves `report` set.
  void releaseAll();
  static void scanEntry(void* self);
  static void emuEntry(void* self);
  void scanTask();
  void emuTask();
};
