#pragma once

// Games > GB / GBC: the ROM picker. Lists the matching files in /roms on the SD
// card (CrossPlay's own list UI), and a tap or Select starts GbPlayerActivity on
// the chosen one. Back returns to the shelf.

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class EmuRomListActivity final : public UiListActivity {
 public:
  enum class System : uint8_t { GameBoy, GameBoyColor };

  EmuRomListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, System system);

  static std::unique_ptr<Activity> createGb(GfxRenderer& renderer, MappedInputManager& mappedInput);
  static std::unique_ptr<Activity> createGbc(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void scanRoms();

  const System system;
  std::vector<std::string> names;                 // file names, in /roms
  std::vector<std::string> labels;                // names without the extension
  std::vector<freeink::ui::ListItem> rowItems;    // points into `labels`
  std::string emptyText;
};
