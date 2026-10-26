#pragma once

// Throwaway spike for the GB/GBC/NES emulator work (branch gb-nes-emulator).
//
// Question it answers: can CrossPlay's LovyanGFX panel driver give up the
// ESP32-S3 LCD peripheral, can the emulator's FastEPD driver take it, draw, and
// let go, and does CrossPlay come back with a working screen afterwards?
//
// Registered temporarily in Shelf.cpp (kApps). Remove the row and this
// directory once emu/ has a real entry point.

#include <memory>

#include "../../activities/Activity.h"

class EmuSpikeActivity final : public Activity {
 public:
  EmuSpikeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("EmuSpike", renderer, mappedInput) {}
  ~EmuSpikeActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Stage : uint8_t { Intro, Running, Done };
  Stage stage = Stage::Intro;
  int round = 0;
  // Set by render() once the "Running" page has been handed to the panel, so the
  // handoff never starts while a refresh from this activity is still in flight.
  bool runningPageShown = false;
  char report[160] = "";

  // Runs one suspend / FastEPD draw / release / resume cycle. Returns true if
  // every step reported success; fills `report` either way.
  bool runHandoff();
};
