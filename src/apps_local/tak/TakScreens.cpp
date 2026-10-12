#include "TakScreens.h"

#include <cstdio>

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
                 const bool toMove) {
  const int16_t radius = 9;
  const int16_t cx = static_cast<int16_t>(row.x + radius + 2);
  const int16_t cy = static_cast<int16_t>(row.y + row.height / 2);
  toybox::disc(screen, cx, cy, radius, fui::Color::Black);
  toybox::disc(screen, cx, cy, static_cast<int16_t>(radius - 2), ink(color));
  char line[40];
  std::snprintf(line, sizeof(line), "%s%s  %d FLAT  %d CAP", toMove ? "> " : "", colorName(color), reserve.flats,
                reserve.capstones);
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

void drawGrid(toybox::Screen& screen, const int16_t left, const int16_t top, const int16_t cell) {
  const int16_t side = static_cast<int16_t>(cell * tak::kSize);
  screen.target().stroke(fui::makeRect(static_cast<int16_t>(left - toybox::kBoardFrame),
                                       static_cast<int16_t>(top - toybox::kBoardFrame),
                                       static_cast<int16_t>(side + toybox::kBoardFrame * 2),
                                       static_cast<int16_t>(side + toybox::kBoardFrame * 2)),
                         fui::Paint::solid(fui::Color::Black), toybox::kBoardFrame);
  for (int row = 0; row < tak::kSize; ++row) {
    for (int col = 0; col < tak::kSize; ++col) {
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
  drawGrid(screen, left, top, cell);
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

Layout layoutFor(const fui::DeviceContext& device) {
  Layout layout;
  const int16_t first = static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter + toybox::kBoardFrame);
  layout.top = first;
  layout.sideways = device.width > device.height;
  if (!layout.sideways) {
    int16_t room = static_cast<int16_t>(device.width - toybox::kMargin * 2 - toybox::kBoardFrame * 2);
    const int16_t tall = static_cast<int16_t>(device.height - first - toybox::kBoardFrame - toybox::kGutter -
                                              kPanelHeight - toybox::kMargin);
    if (tall < room) room = tall;
    layout.cell = static_cast<int16_t>(room / tak::kSize);
    const int16_t side = static_cast<int16_t>(layout.cell * tak::kSize);
    layout.left = static_cast<int16_t>((device.width - side) / 2);
    const int16_t panelTop = static_cast<int16_t>(first + side + toybox::kBoardFrame + toybox::kGutter);
    layout.panel = fui::makeRect(toybox::kMargin, panelTop, static_cast<int16_t>(device.width - toybox::kMargin * 2),
                                 static_cast<int16_t>(device.height - panelTop - toybox::kMargin));
  } else {
    const int16_t room = static_cast<int16_t>(device.height - first - toybox::kBoardFrame - toybox::kMargin);
    layout.cell = static_cast<int16_t>(room / tak::kSize);
    const int16_t side = static_cast<int16_t>(layout.cell * tak::kSize);
    layout.left = static_cast<int16_t>(toybox::kMargin + toybox::kBoardFrame);
    const int16_t panelLeft = static_cast<int16_t>(layout.left + side + toybox::kBoardFrame + toybox::kGutter * 2);
    layout.panel = fui::makeRect(panelLeft, first, static_cast<int16_t>(device.width - panelLeft - toybox::kMargin),
                                 static_cast<int16_t>(device.height - first - toybox::kMargin));
  }
  return layout;
}

fui::Rect boardRect(const Layout& layout) {
  const int16_t side = static_cast<int16_t>(layout.cell * tak::kSize);
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
  const int side = layout.cell * tak::kSize;
  if (dx < 0 || dy < 0 || dx >= side || dy >= side) return false;
  index = tak::at(dx / layout.cell, dy / layout.cell);
  return true;
}

void buildMenu(toybox::Screen& screen, const MenuModel& model) {
  toyboxChrome(screen, "TAK");

  fui::ListItem rows[static_cast<int>(MenuRow::Count)] = {};
  rows[static_cast<int>(MenuRow::Play)].label = "PLAY";
  rows[static_cast<int>(MenuRow::Play)].subtitle = "AGAINST THE DEVICE";
  rows[static_cast<int>(MenuRow::Play)].actionValue = static_cast<int16_t>(MenuRow::Play);
  rows[static_cast<int>(MenuRow::TwoPlayers)].label = "TWO PLAYERS";
  rows[static_cast<int>(MenuRow::TwoPlayers)].subtitle = "PASS THE DEVICE";
  rows[static_cast<int>(MenuRow::TwoPlayers)].actionValue = static_cast<int16_t>(MenuRow::TwoPlayers);

  const int selected = model.selected < 0 ? 0 : model.selected;
  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(MenuRow::Count);
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = ActionMenuRow;
  const int count = static_cast<int>(MenuRow::Count);
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const int16_t listTop = static_cast<int16_t>(content.bottom() - listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

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
  const Layout layout = layoutFor(screen.device());

  drawGrid(screen, layout.left, layout.top, layout.cell);
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
      cell = tak::cellAfter(cell, turn.dir);
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
  const bool computerTurn = model.opponent == tak::Opponent::Computer && game.toMove == Color::Black;
  const char* label;
  char named[24];
  if (game.over) {
    label = "GAME OVER";
  } else if (model.thinking || computerTurn) {
    label = "THINKING";
  } else if (model.opponent == tak::Opponent::Computer) {
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
                !game.over && game.toMove == color);
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
  const int16_t width = static_cast<int16_t>((controls.width - toybox::kGutter * 2) / 3);
  for (int i = 0; i < 3; ++i) {
    fui::ButtonProps button;
    button.label = kNames[i];
    button.action = ActionKind;
    button.value = static_cast<int16_t>(i);
    button.enabled = tak::kindAvailable(game, kKinds[i]);
    button.state = kKinds[i] == turn.kind ? fui::StateSelected : fui::StateNormal;
    screen.button(button, fui::makeRect(static_cast<int16_t>(controls.x + i * (width + toybox::kGutter)), controls.y,
                                        width, controls.height));
  }
}

void buildResult(toybox::Screen& screen, const ResultModel& model) {
  const tak::Game& game = *model.game;
  const tak::Verdict verdict = tak::verdictFor(game, model.opponent);

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
  int16_t cell = static_cast<int16_t>(room / tak::kSize);
  const int16_t wide = static_cast<int16_t>((area.width - toybox::kBoardFrame * 2) / tak::kSize);
  if (wide < cell) cell = wide;
  if (cell > 90) cell = 90;
  if (cell < 24) return;
  const int16_t side = static_cast<int16_t>(cell * tak::kSize);
  const int16_t left = static_cast<int16_t>(area.x + (area.width - side) / 2);
  const int16_t top = static_cast<int16_t>(line.bottom() + toybox::kGutter + toybox::kBoardFrame);
  drawGrid(screen, left, top, cell);
  for (int index = 0; index < tak::kCells; ++index) {
    drawStack(screen,
              fui::makeRect(static_cast<int16_t>(left + tak::colOf(index) * cell),
                            static_cast<int16_t>(top + tak::rowOf(index) * cell), cell, cell),
              game.board[index]);
  }
}

}  // namespace takui
