#include "TakScreens.h"

#include <cstdio>

#include "../ui/ToyboxFormat.h"

namespace takui {

namespace {

using tak::Color;
using tak::PieceKind;

// The header band with the offset rule under it, as the other games wear it.
void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

constexpr int16_t kPanelHeight = 188;

fui::Color ink(const Color color) { return color == Color::White ? fui::Color::White : fui::Color::Black; }

const char* colorName(const Color color) { return color == Color::White ? "WHITE" : "BLACK"; }

// One stone, centred on (cx, cy), at a size set by the cell it stands in.
// White is paper with an ink outline and Black is solid ink, so the two read
// apart on a panel with no colour and on a dithered square alike.
void drawPiece(toybox::Screen& screen, const int16_t cx, const int16_t cy, const int16_t cell, const Color color,
               const PieceKind kind) {
  const fui::Color body = ink(color);
  const fui::Color mark = color == Color::White ? fui::Color::Black : fui::Color::White;
  const int16_t weight = cell >= 60 ? 3 : 2;
  if (kind == PieceKind::Cap) {
    const int16_t radius = static_cast<int16_t>(cell * 3 / 10);
    toybox::disc(screen, cx, cy, radius, fui::Color::Black);
    toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - weight), body);
    const int16_t inner = static_cast<int16_t>(radius / 2);
    toybox::disc(screen, cx, cy, inner, mark);
    toybox::disc(screen, cx, cy, static_cast<int16_t>(inner - weight), body);
    return;
  }
  const int16_t halfW = kind == PieceKind::Wall ? static_cast<int16_t>(cell / 9) : static_cast<int16_t>(cell * 26 / 100);
  const int16_t halfH = kind == PieceKind::Wall ? static_cast<int16_t>(cell * 34 / 100) : halfW;
  const fui::Rect box = fui::makeRect(static_cast<int16_t>(cx - halfW), static_cast<int16_t>(cy - halfH),
                                      static_cast<int16_t>(halfW * 2), static_cast<int16_t>(halfH * 2));
  screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
  const fui::Rect inside = fui::makeRect(static_cast<int16_t>(box.x + weight), static_cast<int16_t>(box.y + weight),
                                         static_cast<int16_t>(box.width - weight * 2),
                                         static_cast<int16_t>(box.height - weight * 2));
  screen.target().fill(inside, fui::Paint::solid(body));
}

// A square's whole contents: the top stone, and beside it a column with one
// bar per stone in the stack, bottom to top, in the owner's colour. That is how
// a stack reads at a glance: who is on top, how tall, and what is buried.
void drawStack(toybox::Screen& screen, const fui::Rect& box, const tak::Cell& cell) {
  if (cell.height == 0) return;
  const int16_t size = box.width;
  const bool stacked = cell.height > 1;
  const int16_t stripW = static_cast<int16_t>(size / 8 > 6 ? size / 8 : 6);
  const int16_t cx = static_cast<int16_t>(box.x + (stacked ? (size - stripW - 4) / 2 : size / 2));
  const int16_t cy = static_cast<int16_t>(box.y + size / 2);
  const int top = cell.height - 1;
  drawPiece(screen, cx, cy, size, tak::colorAt(cell, top), cell.topKind);
  if (!stacked) return;

  const int16_t bar = static_cast<int16_t>(size / 12 > 4 ? size / 12 : 4);
  const int16_t pitch = static_cast<int16_t>(bar + 1);
  const int fits = (size - 8) / pitch;
  const int shown = cell.height < fits ? cell.height : fits;
  const int16_t x = static_cast<int16_t>(box.right() - stripW - 3);
  int16_t y = static_cast<int16_t>(box.bottom() - 4 - bar);
  for (int level = 0; level < shown; ++level) {
    const fui::Rect rect = fui::makeRect(x, y, stripW, bar);
    screen.target().fill(rect, fui::Paint::solid(fui::Color::Black));
    if (tak::colorAt(cell, level) == Color::White) {
      screen.target().fill(fui::makeRect(static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1),
                                         static_cast<int16_t>(stripW - 2), static_cast<int16_t>(bar - 2)),
                           fui::Paint::solid(fui::Color::White));
    }
    y = static_cast<int16_t>(y - pitch);
  }
}

void text(toybox::Screen& screen, const fui::Rect& where, const char* string, const fui::FontId font,
          const fui::TextAlign align, const uint8_t maxLines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.maxLines = maxLines;
  screen.target().text(where, string, style);
}

// A caption for the reserve row: a stone in the owner's colour and what is left.
void reserveLine(toybox::Screen& screen, const fui::Rect& row, const Color color, const tak::Reserve& reserve,
                 const bool toMove, const bool you, const int size) {
  const int16_t radius = 9;
  const int16_t cx = static_cast<int16_t>(row.x + radius + 2);
  const int16_t cy = static_cast<int16_t>(row.y + row.height / 2);
  toybox::disc(screen, cx, cy, radius, fui::Color::Black);
  toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 2), ink(color));
  char line[48];
  // A board with no capstones does not mention them.
  char caps[12] = "";
  if (tak::capstonesFor(size) > 0) std::snprintf(caps, sizeof(caps), "  %d CAP", reserve.capstones);
  std::snprintf(line, sizeof(line), "%s%s%s  %d FLAT%s", toMove ? "> " : "", colorName(color), you ? " (YOU)" : "",
                reserve.flats, caps);
  text(screen, fui::makeRect(static_cast<int16_t>(row.x + radius * 2 + 12), row.y,
                             static_cast<int16_t>(row.width - radius * 2 - 12), row.height),
       line, toybox::kTileFont, fui::TextAlign::Left);
}

int flatsOnTop(const tak::Game& game, const Color color) {
  int count = 0;
  for (const tak::Cell& cell : game.board) {
    if (cell.height > 0 && cell.topKind == PieceKind::Flat && tak::colorAt(cell, cell.height - 1) == color) ++count;
  }
  return count;
}

void drawGrid(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell, const int size) {
  const int16_t side = static_cast<int16_t>(cell * size);
  screen.target().stroke(fui::makeRect(static_cast<int16_t>(left - toybox::kBoardFrame),
                                       static_cast<int16_t>(top - toybox::kBoardFrame),
                                       static_cast<int16_t>(side + toybox::kBoardFrame * 2),
                                       static_cast<int16_t>(side + toybox::kBoardFrame * 2)),
                         fui::Paint::solid(fui::Color::Black), toybox::kBoardFrame);
  for (int row = 0; row < size; ++row) {
    for (int col = 0; col < size; ++col) {
      const fui::Rect box = fui::makeRect(static_cast<int16_t>(left + col * cell),
                                          static_cast<int16_t>(top + row * cell), cell, cell);
      if (((row + col) & 1) == 1) screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
      screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), toybox::kHairline);
    }
  }
}

// A fixed position for the front door: both colours, a wall, a capstone and a
// stack, all made of the app's own pieces.
void ornament(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell) {
  drawGrid(screen, left, top, cell, tak::kSize);
  const auto put = [&](const int col, const int row, const char* stones, const PieceKind kind) {
    tak::Cell stack{};
    int n = 0;
    uint64_t bits = 0;
    for (; stones[n] != '\0'; ++n) {
      if (stones[n] == 'B') bits |= (1ULL << n);
    }
    stack.height = static_cast<uint8_t>(n);
    stack.colors = bits;
    stack.topKind = kind;
    drawStack(screen,
              fui::makeRect(static_cast<int16_t>(left + col * cell), static_cast<int16_t>(top + row * cell), cell, cell),
              stack);
  };
  put(0, 0, "W", PieceKind::Flat);
  put(1, 0, "W", PieceKind::Flat);
  put(2, 1, "BW", PieceKind::Cap);
  put(3, 1, "B", PieceKind::Wall);
  put(1, 2, "WB", PieceKind::Flat);
  put(2, 2, "B", PieceKind::Flat);
  put(4, 3, "W", PieceKind::Wall);
  put(3, 4, "BWB", PieceKind::Flat);
  put(0, 4, "B", PieceKind::Cap);
}

}  // namespace

Layout layoutFor(const fui::DeviceContext& device, const int size) {
  Layout layout;
  layout.size = static_cast<int16_t>(size);
  const int16_t first = static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter + toybox::kBoardFrame);
  layout.top = first;
  layout.sideways = device.width > device.height;
  if (!layout.sideways) {
    int16_t room = static_cast<int16_t>(device.width - toybox::kMargin * 2 - toybox::kBoardFrame * 2);
    const int16_t tall = static_cast<int16_t>(device.height - first - toybox::kBoardFrame - toybox::kGutter -
                                              kPanelHeight - toybox::kMargin);
    if (tall < room) room = tall;
    layout.cell = static_cast<int16_t>(room / size);
    const int16_t side = static_cast<int16_t>(layout.cell * size);
    layout.left = static_cast<int16_t>((device.width - side) / 2);
    const int16_t panelTop = static_cast<int16_t>(first + side + toybox::kBoardFrame + toybox::kGutter);
    layout.panel = fui::makeRect(toybox::kMargin, panelTop, static_cast<int16_t>(device.width - toybox::kMargin * 2),
                                 static_cast<int16_t>(device.height - panelTop - toybox::kMargin));
  } else {
    const int16_t room = static_cast<int16_t>(device.height - first - toybox::kBoardFrame - toybox::kMargin);
    layout.cell = static_cast<int16_t>(room / size);
    const int16_t side = static_cast<int16_t>(layout.cell * size);
    layout.left = static_cast<int16_t>(toybox::kMargin + toybox::kBoardFrame);
    const int16_t panelLeft = static_cast<int16_t>(layout.left + side + toybox::kBoardFrame + toybox::kGutter * 2);
    layout.panel = fui::makeRect(panelLeft, first, static_cast<int16_t>(device.width - panelLeft - toybox::kMargin),
                                 static_cast<int16_t>(device.height - first - toybox::kMargin));
  }
  return layout;
}

fui::Rect boardRect(const Layout& layout) {
  const int16_t side = static_cast<int16_t>(layout.cell * layout.size);
  return fui::makeRect(layout.left, layout.top, side, side);
}

fui::Rect cellRect(const Layout& layout, const int index) {
  return fui::makeRect(static_cast<int16_t>(layout.left + tak::colOf(index) * layout.cell),
                       static_cast<int16_t>(layout.top + tak::rowOf(index) * layout.cell), layout.cell, layout.cell);
}

bool cellAt(const Layout& layout, const int x, const int y, int& index) {
  if (layout.cell <= 0) return false;
  const int dx = x - layout.left;
  const int dy = y - layout.top;
  const int side = layout.cell * layout.size;
  if (dx < 0 || dy < 0 || dx >= side || dy >= side) return false;
  index = tak::at(dx / layout.cell, dy / layout.cell);
  return true;
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "TAK");

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  int count = 0;
  // No subtitles: a row with one is taller than the list's fixed row height,
  // so the doors stopped fitting and the last of them scrolled out of view.
  // What a saved game is says so on a line of its own, below.
  const auto door = [&](const MenuRow which, const char* label) {
    rows[count].label = label;
    rows[count].actionValue = static_cast<int16_t>(which);
    ++count;
  };
  if (model.hasSave) door(MenuRow::Continue, "CONTINUE");
  door(MenuRow::Play, model.hasSave ? "NEW GAME" : "PLAY");
  door(MenuRow::TwoPlayers, "TWO PLAYERS");
  door(MenuRow::HowTo, "HOW TO PLAY");

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionMenuRow;
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  int16_t listTop = static_cast<int16_t>(content.bottom() - listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  if (model.hasSave && model.saveNote != nullptr) {
    constexpr int16_t kNoteHeight = 24;
    listTop = static_cast<int16_t>(listTop - kNoteHeight - toybox::kGutter / 2);
    text(screen, fui::makeRect(content.x, listTop, content.width, kNoteHeight), model.saveNote, toybox::kTileFont,
         fui::TextAlign::Center);
  }

  // The ornament, centred in what the doors leave, as large as that room and
  // the width allow.
  const int16_t room = static_cast<int16_t>(listTop - content.y - toybox::kGutter * 2);
  int16_t cell = static_cast<int16_t>(room / tak::kSize);
  const int16_t wide = static_cast<int16_t>((content.width - toybox::kBoardFrame * 2) / tak::kSize);
  if (wide < cell) cell = wide;
  if (cell > 96) cell = 96;
  if (cell < 24) return;
  const int16_t side = static_cast<int16_t>(cell * tak::kSize);
  const int16_t top = static_cast<int16_t>(content.y + (room - side) / 2 + toybox::kGutter);
  ornament(screen, static_cast<int16_t>((content.width + content.x * 2 - side) / 2), top, cell);
}

void buildBoard(toybox::Screen& screen, const BoardModel& model) {
  fui::HeaderProps header;
  header.title = "TAK";
  header.borderEdges = fui::EdgesNone;
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 2, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const tak::Game& game = *model.game;
  const tak::Turn& turn = *model.turn;
  const Layout layout = layoutFor(screen.device(), game.size);

  drawGrid(screen, layout.left, layout.top, layout.cell, game.size);
  screen.frame().hit(boardRect(layout), ActionBoard, 0);

  for (int index = 0; index < tak::kCells; ++index) drawStack(screen, cellRect(layout, index), game.board[index]);

  // What the next tap would do, marked by the same function that will do it.
  const bool hand = tak::holding(turn);
  for (int index = 0; index < tak::kCells; ++index) {
    const tak::Tap tap = tak::tapMeaning(game, turn, index);
    const fui::Rect box = cellRect(layout, index);
    const bool empty = game.board[index].height == 0;
    switch (tap) {
      case tak::Tap::Lift:
        toybox::bracket(screen, box, static_cast<int16_t>(layout.cell / 5), 3);
        break;
      case tak::Tap::Step:
      case tak::Tap::Add:
        if (empty) {
          toybox::disc(screen, static_cast<int16_t>(box.x + box.width / 2), static_cast<int16_t>(box.y + box.height / 2),
                       static_cast<int16_t>(layout.cell / 9 + 2), fui::Color::Black);
        } else {
          toybox::bracket(screen, box, static_cast<int16_t>(layout.cell / 4), 4);
        }
        break;
      default:
        break;
    }
  }

  if (hand) {
    const fui::Rect from = cellRect(layout, turn.from);
    screen.target().stroke(
        fui::makeRect(static_cast<int16_t>(from.x - toybox::kFrame), static_cast<int16_t>(from.y - toybox::kFrame),
                      static_cast<int16_t>(from.width + toybox::kFrame * 2),
                      static_cast<int16_t>(from.height + toybox::kFrame * 2)),
        fui::Paint::solid(fui::Color::Black), toybox::kFrame);
    // How many stones each square on the path has been given so far.
    int cell = turn.from;
    for (int i = 0; i < turn.steps; ++i) {
      cell = tak::cellAfter(game, cell, turn.dir);
      if (cell == tak::kNothing) break;
      const fui::Rect box = cellRect(layout, cell);
      char badge[8];
      std::snprintf(badge, sizeof(badge), "+%d", turn.drops[i]);
      const fui::Rect tag = fui::makeRect(static_cast<int16_t>(box.x + 3), static_cast<int16_t>(box.y + 3), 38, 24);
      screen.target().fill(tag, fui::Paint::solid(fui::Color::Black));
      fui::TextStyle style;
      style.font = toybox::kTileFont;
      style.align = fui::TextAlign::Center;
      style.color = fui::Color::White;
      screen.target().text(tag, badge, style);
    }
  }

  // The panel: status, both reserves, then either the piece chooser or the hand.
  fui::Rect panel = layout.panel;
  const bool vsComputer = model.opponent == tak::Opponent::Computer;
  const bool computerTurn = vsComputer && game.toMove != model.player;
  const char* label;
  char named[24];
  if (game.over) {
    label = "GAME OVER";
  } else if (model.thinking || computerTurn) {
    label = "THINKING";
  } else if (vsComputer) {
    label = "YOUR MOVE";
  } else {
    std::snprintf(named, sizeof(named), "%s TO MOVE", colorName(game.toMove));
    label = named;
  }
  fui::ButtonProps status;
  status.label = label;
  status.action = fui::NO_ACTION;
  status.borderEdges = fui::EdgesNone;
  const fui::Rect capsule = fui::makeRect(panel.x, panel.y, panel.width, toybox::kPillHeight);
  screen.button(status, capsule);
  int16_t y = static_cast<int16_t>(capsule.bottom() + toybox::kGutter);

  for (int seat = 0; seat < 2; ++seat) {
    const Color color = seat == 0 ? Color::White : Color::Black;
    reserveLine(screen, fui::makeRect(panel.x, y, panel.width, 26), color, game.reserve[seat],
                !game.over && game.toMove == color, vsComputer && color == model.player, game.size);
    y = static_cast<int16_t>(y + 28);
  }
  y = static_cast<int16_t>(y + toybox::kGutter - 2);
  const fui::Rect controls = fui::makeRect(panel.x, y, panel.width, toybox::kPillHeight);

  if (game.over || computerTurn || model.thinking) return;

  if (hand) {
    const int16_t half = static_cast<int16_t>((controls.width - toybox::kGutter) / 2);
    char line[24];
    std::snprintf(line, sizeof(line), "IN HAND  %d", tak::inHand(turn));
    text(screen, fui::makeRect(controls.x, controls.y, half, controls.height), line, toybox::kUiFont,
         fui::TextAlign::Left);
    fui::ButtonProps back;
    back.label = "PUT BACK";
    back.action = ActionPutBack;
    screen.button(back, fui::makeRect(static_cast<int16_t>(controls.x + half + toybox::kGutter), controls.y, half,
                                      controls.height));
    return;
  }

  if (game.plyNumber < 2) {
    char line[40];
    std::snprintf(line, sizeof(line), "PLACE A %s FLAT FOR THEM", colorName(tak::colorToPlace(game)));
    text(screen, controls, line, toybox::kUiFont, fui::TextAlign::Left, 2);
    return;
  }

  // FLAT / WALL / CAP, one of which is always chosen.
  static const char* const kNames[3] = {"FLAT", "WALL", "CAP"};
  static const PieceKind kKinds[3] = {PieceKind::Flat, PieceKind::Wall, PieceKind::Cap};
  // A board with no capstone has no CAP button to leave disabled and unexplained.
  const int kinds = tak::capstonesFor(game.size) > 0 ? 3 : 2;
  const int16_t width = static_cast<int16_t>((controls.width - toybox::kGutter * (kinds - 1)) / kinds);
  for (int i = 0; i < kinds; ++i) {
    fui::ButtonProps button;
    button.label = kNames[i];
    button.action = ActionKind;
    button.value = static_cast<int16_t>(i);
    button.enabled = tak::kindAvailable(game, kKinds[i]);
    // The same convention as the setup screen: filled is chosen, outlined is
    // open, and a kind that has run out is dithered rather than missing.
    button.styles = !button.enabled ? toybox::disabledButtonStyles()
                                    : (kKinds[i] == turn.kind ? toybox::invertedStyles() : toybox::rowStyles());
    screen.button(button, fui::makeRect(static_cast<int16_t>(controls.x + i * (width + toybox::kGutter)), controls.y,
                                        width, controls.height));
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const tak::Game& game = *model.game;
  const tak::Verdict verdict = tak::verdictFor(game, model.opponent, model.player);

  const char* title = "A DRAW";
  switch (verdict) {
    case tak::Verdict::YouWon: title = "YOU WIN"; break;
    case tak::Verdict::YouLost: title = "THEY WIN"; break;
    case tak::Verdict::WhiteWon: title = "WHITE WINS"; break;
    case tak::Verdict::BlackWon: title = "BLACK WINS"; break;
    case tak::Verdict::Draw: break;
  }
  toyboxChrome(screen, title);

  fui::ButtonProps done;
  done.label = "DONE";
  done.action = ActionDone;
  screen.button(done, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));
  fui::ButtonProps again;
  again.label = "PLAY AGAIN";
  again.action = ActionAgain;
  screen.button(again, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  char reason[48];
  if (game.winnerByRoad) {
    std::snprintf(reason, sizeof(reason), "A ROAD ACROSS THE BOARD");
  } else {
    std::snprintf(reason, sizeof(reason), "FLATS ON TOP  %d TO %d", flatsOnTop(game, Color::White),
                  flatsOnTop(game, Color::Black));
  }
  const fui::Rect line = fui::makeRect(area.x, area.y, area.width, 34);
  text(screen, line, reason, toybox::kBodyFont, fui::TextAlign::Center);

  // The final position, as large as the room allows.
  const int16_t room = static_cast<int16_t>(area.height - line.height - toybox::kGutter * 2);
  const int size = game.size;
  int16_t cell = static_cast<int16_t>(room / size);
  const int16_t wide = static_cast<int16_t>((area.width - toybox::kBoardFrame * 2) / size);
  if (wide < cell) cell = wide;
  if (cell > 450 / size) cell = static_cast<int16_t>(450 / size);
  if (cell < 24) return;
  const int16_t side = static_cast<int16_t>(cell * size);
  const int16_t left = static_cast<int16_t>(area.x + (area.width - side) / 2);
  const int16_t top = static_cast<int16_t>(line.bottom() + toybox::kGutter + toybox::kBoardFrame);
  drawGrid(screen, left, top, cell, size);
  for (int index = 0; index < tak::kCells; ++index) {
    if (!tak::playable(game, index)) continue;
    drawStack(screen,
              fui::makeRect(static_cast<int16_t>(left + tak::colOf(index) * cell),
                            static_cast<int16_t>(top + tak::rowOf(index) * cell), cell, cell),
              game.board[index]);
  }
}

namespace {

// A captioned row of exclusive choices, as wide as the panel: the caption, then
// one button per choice with the chosen one inverted, then a line saying what the
// choice means. Returns the y the next row starts at.
int16_t choiceRow(toybox::Screen& screen, const fui::Rect& area, const int16_t y, const char* caption,
                  const char* const* names, const int count, const int selected, const fui::ActionId action,
                  const int16_t* values, const char* note) {
  text(screen, fui::makeRect(area.x, y, area.width, 24), caption, toybox::kTileFont, fui::TextAlign::Left);
  const int16_t top = static_cast<int16_t>(y + 26);
  const int16_t width = static_cast<int16_t>((area.width - toybox::kGutter * (count - 1)) / count);
  for (int i = 0; i < count; ++i) {
    fui::ButtonProps button;
    button.label = names[i];
    button.action = action;
    button.value = values[i];
    // Filled is the choice made, outlined is a choice still open.
    button.styles = i == selected ? toybox::invertedStyles() : toybox::rowStyles();
    screen.button(button, fui::makeRect(static_cast<int16_t>(area.x + i * (width + toybox::kGutter)), top, width,
                                        toybox::kPillHeight));
  }
  int16_t next = static_cast<int16_t>(top + toybox::kPillHeight + 6);
  if (note != nullptr) {
    text(screen, fui::makeRect(area.x, next, area.width, 24), note, toybox::kTileFont, fui::TextAlign::Left);
    next = static_cast<int16_t>(next + 26);
  }
  return static_cast<int16_t>(next + toybox::kGutter);
}

}  // namespace

void buildSetup(toybox::Screen& screen, const SetupModel& model) {
  toyboxChrome(screen, "NEW GAME");

  fui::ButtonProps start;
  start.label = "START";
  start.action = ActionStart;
  screen.button(start, screen.takeBottom(toybox::kPillHeight, toybox::kGutter));

  const fui::Rect area = screen.body();
  int16_t y = area.y;

  static const char* const kBoards[2] = {"3 BY 3", "5 BY 5"};
  static const int16_t kBoardValues[2] = {tak::kSmallSize, tak::kSize};
  y = choiceRow(screen, area, y, "BOARD", kBoards, 2, model.size == tak::kSmallSize ? 0 : 1, ActionSize, kBoardValues,
                model.size == tak::kSmallSize ? "10 STONES EACH, NO CAPSTONE" : "21 STONES AND A CAPSTONE EACH");

  if (!model.vsComputer) return;

  static const char* const kSeats[2] = {"WHITE", "BLACK"};
  static const int16_t kSeatValues[2] = {0, 1};
  y = choiceRow(screen, area, y, "PLAY AS", kSeats, 2, model.player == Color::White ? 0 : 1, ActionSeat, kSeatValues,
                "WHITE MOVES FIRST");

  static const char* const kLevels[3] = {"EASY", "MEDIUM", "HARD"};
  static const int16_t kLevelValues[3] = {0, 1, 2};
  static const char* const kLevelNotes[3] = {"A BEGINNER. IT MISSES THINGS", "A STEADY OPPONENT",
                                             "WATCHES YOUR REPLIES. IT TAKES A MOMENT"};
  const int level = static_cast<int>(model.level);
  choiceRow(screen, area, y, "LEVEL", kLevels, 3, level, ActionLevel, kLevelValues, kLevelNotes[level]);
}

int howToPages() { return 8; }

namespace {

// A small board of the app's own pieces for a lesson. Same drawing as the live
// board, so what is taught is what will be seen.
struct Lesson {
  toybox::Screen& screen;
  int16_t left;
  int16_t top;
  int16_t cell;

  fui::Rect box(const int col, const int row) const {
    return fui::makeRect(static_cast<int16_t>(left + col * cell), static_cast<int16_t>(top + row * cell), cell, cell);
  }
  void put(const int col, const int row, const char* stones, const PieceKind kind = PieceKind::Flat) const {
    tak::Cell stack{};
    int n = 0;
    uint64_t bits = 0;
    for (; stones[n] != '\0'; ++n) {
      if (stones[n] == 'B') bits |= (1ULL << n);
    }
    stack.height = static_cast<uint8_t>(n);
    stack.colors = bits;
    stack.topKind = kind;
    drawStack(screen, box(col, row), stack);
  }
  void dot(const int col, const int row) const {
    const fui::Rect b = box(col, row);
    toybox::disc(screen, static_cast<int16_t>(b.x + b.width / 2), static_cast<int16_t>(b.y + b.height / 2),
                 static_cast<int16_t>(cell / 9 + 2), fui::Color::Black);
  }
  void mark(const int col, const int row) const { toybox::bracket(screen, box(col, row), static_cast<int16_t>(cell / 4), 4); }
  // A caption in the square BELOW, so it never sits on the stone it names.
  void label(const int col, const int row, const char* words) const {
    const fui::Rect b = box(col, row);
    text(screen, fui::makeRect(b.x, static_cast<int16_t>(b.bottom() + 6), b.width, 24), words, toybox::kTileFont,
         fui::TextAlign::Center);
  }
};

void drawLesson(toybox::Screen& screen, const int page, const int16_t left, const int16_t top, const int16_t cell) {
  drawGrid(screen, left, top, cell, 3);
  const Lesson lesson{screen, left, top, cell};
  switch (page) {
    case 0:  // a road that bends: top edge to bottom edge, side to side all the way
      lesson.put(0, 0, "W");
      lesson.put(0, 1, "W");
      lesson.put(1, 1, "W");
      lesson.put(1, 2, "W");
      lesson.put(2, 0, "B");
      lesson.put(2, 2, "B");
      break;
    case 1:  // corners do not connect: three white stones in a diagonal, boxed in
      lesson.put(0, 0, "W");
      lesson.put(1, 1, "W");
      lesson.put(2, 2, "W");
      lesson.put(1, 0, "B");
      lesson.put(0, 1, "B");
      lesson.put(2, 1, "B");
      lesson.put(1, 2, "B");
      break;
    case 2:  // the opening swap: each side's first stone is the other's
      lesson.put(0, 0, "B");
      lesson.put(2, 2, "W");
      break;
    case 3:  // the three stones
      lesson.put(0, 1, "W");
      lesson.put(1, 1, "W", PieceKind::Wall);
      lesson.put(2, 1, "W", PieceKind::Cap);
      lesson.label(0, 1, "FLAT");
      lesson.label(1, 1, "WALL");
      lesson.label(2, 1, "CAP");
      break;
    case 4:  // a stack, and where its first stone may go
      lesson.put(0, 1, "BWW");
      lesson.mark(0, 1);
      lesson.dot(1, 1);
      lesson.dot(0, 0);
      lesson.dot(0, 2);
      break;
    case 5:  // three stones dealt along a line: one, then two
      lesson.put(0, 1, "WWW");
      lesson.mark(0, 1);
      lesson.dot(1, 1);
      lesson.dot(2, 1);
      lesson.label(1, 1, "DROP 1");
      lesson.label(2, 1, "DROP 2");
      break;
    case 6:  // a capstone flattening a wall
      lesson.put(0, 1, "W", PieceKind::Cap);
      lesson.put(1, 1, "B", PieceKind::Wall);
      lesson.mark(1, 1);
      lesson.put(2, 1, "B");
      break;
    default:  // a full board: count the flats on top
      lesson.put(0, 0, "W");
      lesson.put(1, 0, "B");
      lesson.put(2, 0, "W");
      lesson.put(0, 1, "B");
      lesson.put(1, 1, "WB", PieceKind::Wall);
      lesson.put(2, 1, "W");
      lesson.put(0, 2, "W");
      lesson.put(1, 2, "B");
      lesson.put(2, 2, "B");
      break;
  }
}

}  // namespace

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  const int pages = howToPages();
  const int page = model.page < 0 ? 0 : (model.page >= pages ? pages - 1 : model.page);

  fui::HeaderProps header;
  header.title = "HOW TO PLAY";
  char progress[toybox::kOfCounterChars];
  std::snprintf(progress, sizeof(progress), "%d OF %d", page + 1, pages);
  header.rightLabel = progress;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  const fui::Rect body = screen.body();
  screen.frame().hit(body, ActionHowToNext, 0);

  // Built from the bottom up: dots, then the tap line, then everything else in
  // what is left. Nothing below is placed at a fixed offset from anything it
  // could collide with.
  constexpr int16_t kDot = 14;
  constexpr int16_t kDotGap = 10;
  const int16_t dotRow = static_cast<int16_t>(pages * kDot + (pages - 1) * kDotGap);
  const int16_t dotX = static_cast<int16_t>(body.x + (body.width - dotRow) / 2);
  const int16_t dotY = static_cast<int16_t>(body.bottom() - kDot);
  for (int i = 0; i < pages; ++i) {
    const fui::Rect dotAt = fui::makeRect(static_cast<int16_t>(dotX + i * (kDot + kDotGap)), dotY, kDot, kDot);
    if (i == page) {
      screen.target().fill(dotAt, fui::Paint::solid(fui::Color::Black), 7);
    } else {
      screen.target().stroke(dotAt, fui::Paint::dither(fui::Color::DarkGray), toybox::kHairline, 7);
    }
  }
  const fui::Rect tapLine = fui::makeRect(body.x, static_cast<int16_t>(dotY - 34), body.width, 22);
  text(screen, tapLine, page + 1 == pages ? "TAP TO FINISH" : "TAP TO CONTINUE", toybox::kTileFont,
       fui::TextAlign::Center);

  static const char* const kTitles[] = {"THE GOAL",  "CORNERS",       "FIRST MOVE", "THE STONES",
                                        "MOVING",    "DEALING STONES", "WALLS AND CAPS", "WINNING"};
  const fui::Rect titleRect = fui::makeRect(body.x, body.y, body.width, 46);
  fui::TextStyle title;
  title.font = toybox::kDisplayFont;
  title.align = fui::TextAlign::Center;
  screen.target().text(toybox::inkCentred(titleRect, toybox::kDisplayCut), kTitles[page], title);

  static const char* const kLines[] = {
      "BUILD AN UNBROKEN ROAD OF YOUR STONES FROM ONE EDGE TO THE OPPOSITE EDGE. IT CAN BEND AND TURN. THE FIRST ROAD "
      "WINS.",
      "STONES CONNECT SIDE TO SIDE. STONES THAT ONLY TOUCH AT A CORNER ARE NOT CONNECTED, SO THIS IS NOT A ROAD.",
      "EACH PLAYER'S FIRST MOVE PLACES ONE OF THEIR OPPONENT'S FLATS. AFTER, PLAYERS MUST PLACE OR MOVE A PIECE OR "
      "STACK.",
      "A FLAT BUILDS ROADS AND CAN BE STACKED ON. A WALL BLOCKS ROADS. A CAPSTONE BUILDS ROADS AND IS NEVER COVERED.",
      "TAP YOUR STACK TO LIFT ITS TOP STONE. TAP IT AGAIN TO LIFT MORE, UP TO THE BOARD'S WIDTH. THEN TAP A "
      "NEIGHBOURING SQUARE TO START MOVING.",
      "MOVE IN A STRAIGHT LINE. DROP AT LEAST ONE STONE ON EVERY SQUARE YOU PASS, AND DEAL OUT THE REST HOWEVER YOU "
      "LIKE. TAP A SQUARE AGAIN TO DROP MORE THERE. THE MOVE ENDS WHEN YOUR HAND IS EMPTY.",
      "NOTHING MOVES ONTO A WALL OR CAPSTONE, EXCEPT A CAPSTONE ALONE, WHICH FLATTENS A WALL. THE 3 BY 3 BOARD HAS NO "
      "CAPSTONES.",
      "A ROAD WINS AT ONCE. IF THE BOARD FILLS OR A PLAYER RUNS OUT OF STONES, THE MOST FLATS ON TOP WINS.",
  };

  // What is left for the lesson and its words, between the title and the tap line.
  const int16_t areaTop = static_cast<int16_t>(titleRect.bottom() + toybox::kGutter);
  const int16_t areaBottom = static_cast<int16_t>(tapLine.y - toybox::kGutter);
  const bool sideways = body.width > body.height;

  // The words are measured, not guessed: the block is exactly as tall as the
  // text wraps to at this width, so there is nothing to overlap and nothing
  // cut off. If a page's words are too long to leave room for a readable
  // diagram, they step down a size rather than crush it.
  fui::TextStyle caption;
  caption.align = fui::TextAlign::Center;
  caption.maxLines = 10;
  const int16_t captionWidth = sideways ? static_cast<int16_t>(body.width / 2 - toybox::kGutter) : body.width;
  int16_t captionHeight = 0;
  const fui::FontId fonts[2] = {toybox::kUiFont, toybox::kTileFont};
  int16_t cell = 0;
  int16_t gridTop = 0;
  int16_t gridLeft = 0;
  fui::Rect captionRect{};
  for (const fui::FontId font : fonts) {
    caption.font = font;
    captionHeight = fui::measureWrappedText(screen.target(), kLines[page], caption, captionWidth).height;
    if (sideways) {
      const int16_t room = static_cast<int16_t>(areaBottom - areaTop);
      cell = static_cast<int16_t>((room - toybox::kBoardFrame * 2) / 3);
      const int16_t wide = static_cast<int16_t>((body.width / 2 - toybox::kBoardFrame * 2) / 3);
      if (wide < cell) cell = wide;
      const int16_t side = static_cast<int16_t>(cell * 3);
      gridLeft = static_cast<int16_t>(body.x + (body.width / 2 - side) / 2);
      gridTop = static_cast<int16_t>(areaTop + (room - side) / 2);
      const int16_t textLeft = static_cast<int16_t>(body.x + body.width / 2 + toybox::kGutter);
      const int16_t textTop = static_cast<int16_t>(areaTop + (room - captionHeight) / 2);
      captionRect = fui::makeRect(textLeft, textTop < areaTop ? areaTop : textTop, captionWidth, captionHeight);
      if (captionHeight <= room) break;
    } else {
      const int16_t room = static_cast<int16_t>(areaBottom - areaTop - captionHeight - toybox::kGutter);
      cell = static_cast<int16_t>((room - toybox::kBoardFrame * 2) / 3);
      const int16_t wide = static_cast<int16_t>((body.width - toybox::kBoardFrame * 2) / 3);
      if (wide < cell) cell = wide;
      const int16_t side = static_cast<int16_t>(cell * 3);
      captionRect = fui::makeRect(body.x, static_cast<int16_t>(areaBottom - captionHeight), body.width, captionHeight);
      gridLeft = static_cast<int16_t>(body.x + (body.width - side) / 2);
      gridTop = static_cast<int16_t>(areaTop + (room - side) / 2 + toybox::kBoardFrame);
      if (cell >= 64) break;
    }
  }
  if (cell > 130) {
    // A tall lesson on a small board: keep it a board, not a wall.
    const int16_t side = 130 * 3;
    gridLeft = static_cast<int16_t>(gridLeft + (cell * 3 - side) / 2);
    gridTop = static_cast<int16_t>(gridTop + (cell * 3 - side) / 2);
    cell = 130;
  }
  screen.target().text(captionRect, kLines[page], caption);
  if (cell >= 24) drawLesson(screen, page, gridLeft, gridTop, cell);
}

}  // namespace takui
