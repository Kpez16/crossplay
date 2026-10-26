#pragma once

// Spike 2 for the GB/GBC/NES emulator work (branch gb-nes-emulator): play the
// first .gb/.gbc under /roms on the SD card through the FastEPD video scan.
//
// Two tasks, like InkDeck: an emulator task on core 0 produces packed frames at
// the game's own rate; a scan task on core 1 owns the panel and drives it as fast
// as it will go. They meet in two packed-frame buffers (see PlayState).
//
// No ROM list, no controls, no saves yet: any tap or Back ends the run and
// returns to the shelf. Temporary, like EmuSpikeActivity.

#include <atomic>
#include <memory>

#include "../../activities/Activity.h"

class GbSpikeActivity final : public Activity {
 public:
  GbSpikeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GbSpike", renderer, mappedInput) {}
  ~GbSpikeActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

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
  enum class Stage : uint8_t { Intro, Launch, Playing, Done };
  Stage stage = Stage::Intro;
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
