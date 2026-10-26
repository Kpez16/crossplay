#include "EmuRomListActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <strings.h>

#include <cctype>

#include "GbPlayerActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* kRomDir = "/roms";
constexpr int kMaxRoms = 200;

std::string lowerExt(const std::string& s) {
  const auto dot = s.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string ext = s.substr(dot);
  for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return ext;
}
}  // namespace

EmuRomListActivity::EmuRomListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, System system)
    : UiListActivity("EmuRomList", renderer, mappedInput), system(system) {}

std::unique_ptr<Activity> EmuRomListActivity::createGb(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<EmuRomListActivity>(renderer, mappedInput, System::GameBoy);
}

std::unique_ptr<Activity> EmuRomListActivity::createGbc(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<EmuRomListActivity>(renderer, mappedInput, System::GameBoyColor);
}

const char* EmuRomListActivity::headerTitle() const {
  return system == System::GameBoy ? "Game Boy" : "Game Boy Color";
}

void EmuRomListActivity::scanRoms() {
  names.clear();
  labels.clear();
  rowItems.clear();
  const char* want = system == System::GameBoy ? ".gb" : ".gbc";
  for (const auto& n : Storage.listFiles(kRomDir, kMaxRoms)) {
    std::string s(n.c_str());
    if (lowerExt(s) != want) continue;
    names.push_back(s);
  }
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    return strcasecmp(a.c_str(), b.c_str()) < 0;
  });
  labels.reserve(names.size());
  for (const auto& s : names) labels.push_back(s.substr(0, s.find_last_of('.')));

  rowItems.reserve(std::max<size_t>(names.size(), 1));
  if (names.empty()) {
    emptyText = std::string("No ") + want + " files in " + kRomDir + " on the SD card";
    fui::ListItem item;
    item.label = emptyText.c_str();
    item.enabled = false;
    rowItems.push_back(item);
  } else {
    for (size_t i = 0; i < labels.size(); ++i) {
      fui::ListItem item;
      item.label = labels[i].c_str();
      item.actionValue = static_cast<int16_t>(i);
      rowItems.push_back(item);
    }
  }
  LOG_INF("EMU", "%s: %u ROM(s)", headerTitle(), static_cast<unsigned>(names.size()));
}

void EmuRomListActivity::onEnter() {
  scanRoms();
  UiListActivity::onEnter();
}

int EmuRomListActivity::listCount() const { return static_cast<int>(rowItems.size()); }

void EmuRomListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void EmuRomListActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (names.empty() || index < 0 || index >= static_cast<int>(names.size())) return;
  nav.selected = index;
  const std::string path = std::string(kRomDir) + "/" + names[index];
  LOG_INF("EMU", "launch %s", path.c_str());
  // The player resets the panel itself and returns here through finish(); nothing
  // to do with its result.
  startActivityForResult(std::make_unique<GbPlayerActivity>(renderer, mappedInput, path),
                         [](const ActivityResult&) {});
}
