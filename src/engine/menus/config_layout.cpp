/**
 * @file    engine/menus/config_layout.cpp
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include "engine/menus/config_layout.h"

#include <algorithm>
#include <format>

namespace bd::engine {

namespace {

// The stock footer band this menu's prompts sit in.
constexpr int kFooterIconY = 639, kFooterIconSize = 64;
constexpr int kFooterLabelY = 654, kFooterFontW = 27, kFooterFontH = 30;

// Header line above each of the three columns.
constexpr int kHeaderY = 110, kHeaderFontW = 18, kHeaderFontH = 22;
constexpr int kHeaderPri = 4;

// A titled block on the keybind screen: a black panel behind it, the title in
// the band across its top, and a rule closing that band. The panel sits behind
// the rows the list draws, the title and its rule in front of it.
constexpr int kSectionPanelPri = 4;
constexpr int kSectionTitlePri = 3;
constexpr int kSectionTitleH = 30;
constexpr int kSectionFontW = 19, kSectionFontH = 25;
// Margin a panel keeps around the cells inside it, and the row label's own
// inset, which the titles line up with.
constexpr int kSectionPad = 8;
constexpr int kSectionLabelX = 22;
constexpr int kKeybindCompatRows = 4;
constexpr int kKeybindCompatRowY =
    kKeybindGridY + (kKeybindTopRows + 2) * kKeybindRowH;
// Panels: the top pair closes just under its last row, and the compat panel
// opens far enough below them to read as its own block.
constexpr int kSectionTopY = 98;
constexpr int kSectionTopH =
    kKeybindGridY + kKeybindTopRows * kKeybindRowH + 2 - kSectionTopY;
constexpr int kSectionCompatY = 436;
constexpr int kSectionCompatH = kKeybindCompatRowY +
                                kKeybindCompatRows * kKeybindRowH + 4 -
                                kSectionCompatY;
// Between the two columns of the compat block, which has no panel gap to
// separate them.
constexpr int kKeybindColSplitX =
    kKeybindGridX + kKeybindCellW + (kKeybindColStride - kKeybindCellW) / 2;

// Longest list a settings page may carry before its rows run past the
// row description line.
constexpr size_t kMaxSettingsRows = 12;

// One line of a detail panel's metadata block, and the thin rule closing it.
AnimeMessage PanelLine(int y, const char *var) {
  return {.frameStart = "start",
          .posRef = "pos",
          .offsetX = 10,
          .offsetY = y,
          .fontW = 16,
          .fontH = 20,
          .alphaRef = "LabelColor.a",
          .colorR = "LabelColor.r",
          .colorG = "LabelColor.g",
          .colorB = "LabelColor.b",
          .contentVar = var};
}

void PanelDivider(CsvBuilder &b, int y) {
  b.blank().comment("divider").comment(kColsFrame).frame(
      AnimeFrameRel{.frameStart = "start",
                    .posRef = "pos",
                    .offsetX = 10,
                    .offsetY = y,
                    .w = 380,
                    .h = 1,
                    .priority = 3,
                    .alphaRef = "alpha-127"});
}


// One key slot: the vars driving its box, text and cap art, and where the
// pair sits. The caps are cells of the key sheet through per-item element
// vars (see PromptGlyph for why a uv. name cannot carry them). A bind with a
// modifier prefix draws two caps side by side, gated by pairVar, in place of
// the solo cap capVar gates.
constexpr struct {
  const char *comment;
  const char *wndVar, *wndDefault, *textVar, *colorVar;
  const char *capVar, *uvVar, *pairVar, *modUvVar;
  int x, w;
} kKeybindSlots[] = {
    {"key button", "KeyWnd", "BTN01_OF", "Key", "KeyCol", "KeyCap", "KeyUv",
     "KeyPair", "KeyModUv", 280, 124},
    {"alternate key button", "KeyWnd2", "NOWINDOW", "Key2", "KeyCol2",
     "KeyCap2", "KeyUv2", "KeyPair2", "KeyModUv2", 412, 124},
};

// The cap ink band is 38x40 inside its cell, so the 26-tall key box holds it
// at 21x22 with a 2px margin. The modifier cells ink a 56-wide band, which the
// same scale puts at 31 wide.
constexpr int kKeyCapW = 21;
constexpr int kKeyCapH = 22;
constexpr int kKeyCapY = 4;
constexpr int kKeyModCapW = 31;
constexpr int kKeyPairGap = 2;

// Where a row's box stops with one key bound and with two.
static_assert(kKeybindSlots[0].x + kKeybindSlots[0].w + 8 == kKeybindRowSoloW);
static_assert(kKeybindSlots[1].x + kKeybindSlots[1].w + 8 == kKeybindRowPairW);

struct PadAnchor {
  int x, y, w;
};

constexpr PadAnchor kPadGeneralAnchors[PadLayoutTemplate::kGeneralSlots] = {
    {126, 190, 277}, {228, 435, 114}, {330, 557, 233}, {636, 120, 246},
    {77, 312, 266},  {879, 190, 266}, {934, 465, 195}, {656, 553, 114},
    {858, 553, 233}, {934, 282, 233}, {934, 374, 195}};

constexpr PadAnchor kPadMechatAnchors[PadLayoutTemplate::kMechatSlots] = {
    {96, 207, 231}, {96, 385, 231}, {947, 207, 231}, {947, 331, 231},
    {947, 448, 231}};

constexpr int kPadAimSlot = 1;
constexpr int kPadLabelH = 42;
constexpr int kPadAimTallH = 74;
constexpr int kPadLineH = 30;
constexpr int kPadLabelInsetY = 9;
constexpr int kPadFontW = 21, kPadFontH = 27;
constexpr int kPadBoxPri = 1, kPadLabelPri = 0, kPadArtPri = 2;
constexpr int kPadBandY = 98, kPadBandH = 519, kPadBandPri = 4;
constexpr int kPadScreenW = 1280;
constexpr int kPadArtX = 128, kPadArtY = -154, kPadArtSize = 1024;
constexpr int kPadBadgeX = 126, kPadBadgeY = 122;
constexpr int kPadBadgeW = 138, kPadBadgeH = 43;
constexpr int kPadBadgeLabelX = 151, kPadBadgeLabelY = 131;
} // namespace

SectionTemplate::SectionTemplate() : RowTemplate(110, 121, 150, kSectionRowH) {
  name.set("Section");
  wndType.set("BTN01_OF");
}

// Always drawn, unlike the content rows which fade with their list.
AnimeWindow SectionTemplate::rowWindow() const {
  AnimeWindow w = RowTemplate::rowWindow();
  w.frameStart = "1";
  return w;
}

AnimeMessage SectionTemplate::rowLabel() const {
  AnimeMessage m = RowTemplate::rowLabel();
  m.offsetX = 75;
  m.priority = 1;
  m.font = "meiryo";
  m.posType = "center";
  return m;
}

ListItemTemplate::ListItemTemplate(const char *defaultName, int rowWidth,
                                   int checkX)
    : ToggleRowTemplate(110, 121, rowWidth, 45), checkX_(checkX) {
  name.set(defaultName);
  wndType.set("BTN01_OF");
  chkOn.set(1.0);
}

// The two states of the checkbox: the same cell size out of mark_config, one
// lit and one dimmed.
void ListItemTemplate::buildValue(CsvBuilder &b) {
  constexpr UVRect kMarkUv[] = {{0.5f, 0.25f, 1.0f, 0.5f},
                                {0.0f, 0.25f, 0.5f, 0.5f}};
  static_assert(std::size(kMarkUv) == std::size(kCheckStates));

  b.comment(kColsTex);
  for (size_t i = 0; i < std::size(kCheckStates); ++i)
    b.tex(AnimeTex{.frameStart = kCheckStates[i].gate,
                   .posRef = "pos",
                   .offsetX = checkX_,
                   .offsetY = 8,
                   .w = 28,
                   .h = 28,
                   .priority = 2,
                   .uv = kMarkUv[i],
                   .file = kMarkConfigTex,
                   .tag = kCheckStates[i].tag});
}

void DlcDetailTemplate::build(CsvBuilder &b) {
  b.comment("variable definitions").vars(start, alpha).blank();
  for (const auto &line : descLines)
    b.var(line);
  b.blank()
      .vars(labelColor, valueColor)
      .blank()
      .comment(kColsPos)
      .pos(pos_)
      .blank()
      .comment("metadata")
      .comment(kColsMessage);
  for (int i = 0; i < kDescLines; ++i)
    b.message(PanelLine(10 + 20 * i, descLines[i].name()));
  PanelDivider(b, 75);
}

AnimeMessage NodataTemplate::rowLabel() const {
  AnimeMessage m = RowTemplate::rowLabel();
  m.offsetX = 100;
  m.fontW = 24;
  m.fontH = 30;
  m.offsetY = CenterY(m.fontH);
  m.colorR = m.colorG = m.colorB = "150";
  return m;
}

void DetailTemplate::build(CsvBuilder &b) {
  const StringV *const fields[] = {&author, &version, &created, &desc};

  b.comment("variable definitions")
      .vars(start, alpha, hasPreview, previewFile)
      .blank()
      .vars(author, version, created, desc)
      .blank()
      .vars(labelColor, valueColor)
      .blank()
      .comment(kColsPos)
      .pos(pos_)
      .blank()
      .comment("preview image")
      .comment(kColsTex)
      .tex(AnimeTex{.frameStart = hasPreview.name(),
                    .posRef = "pos",
                    .offsetX = 10,
                    .offsetY = 10,
                    .w = 380,
                    .h = 200,
                    .priority = 2,
                    .uv = {0.0f, 0.0f, 1.0f, 1.0f},
                    .file = previewFile.name()})
      .blank()
      .comment("metadata")
      .comment(kColsMessage);
  for (size_t i = 0; i < std::size(fields); ++i)
    b.message(PanelLine(10 + 25 * static_cast<int>(i), fields[i]->name()));
  PanelDivider(b, 110);
}

void PadLayoutTemplate::build(CsvBuilder &b) {
  const auto box = [&](const FloatV &vis, const PadAnchor &a, int h) {
    b.window(AnimeWindow{.frameStart = vis.name(),
                         .x = a.x,
                         .y = a.y,
                         .w = a.w,
                         .h = h,
                         .priority = kPadBoxPri,
                         .wndTypeVar = nullptr,
                         .wndTypeLiteral = "FRAME01",
                         .alpha = 255});
  };
  const auto text = [&](const FloatV &vis, int cx, int y, const char *var) {
    b.message(AnimeMessageAbs{.frameStart = vis.name(),
                              .x = cx,
                              .y = y,
                              .fontW = kPadFontW,
                              .fontH = kPadFontH,
                              .priority = kPadLabelPri,
                              .contentVar = var,
                              .posType = "center"});
  };

  b.comment("variable definitions")
      .vars(start, alpha, generalVis, mechatVis, aimShortVis, aimTallVis,
            artGeneralVis, artMechatVis, artMechatAltVis, typeName);
  for (const auto &l : label)
    b.var(l);
  b.blank();

  b.comment("backdrop band")
      .comment(kColsFrame)
      .frame(AnimeFrame{.frameStart = start.name(),
                        .x = 0,
                        .y = kPadBandY,
                        .w = kPadScreenW,
                        .h = kPadBandH,
                        .priority = kPadBandPri,
                        .alphaRef = "alpha-200",
                        .r = 0,
                        .g = 0,
                        .b = 0})
      .blank();

  const struct {
    const FloatV *vis;
    const char *file;
  } kArt[] = {{&artGeneralVis, kPadGeneralTex},
              {&artMechatVis, kPadMechatTex},
              {&artMechatAltVis, kPadMechatAltTex}};

  b.comment("controller art and page arrows").comment(kColsTex);
  for (const auto &art : kArt)
    b.tex(AnimeTexAbs{.frameStart = art.vis->name(),
                      .x = kPadArtX,
                      .y = kPadArtY,
                      .w = kPadArtSize,
                      .h = kPadArtSize,
                      .priority = kPadArtPri,
                      .file = art.file});
  b.tex(AnimeTexAbs{.frameStart = start.name(),
                    .x = kPadArrowRightX,
                    .y = kPadArrowY,
                    .w = kPadArrowSize,
                    .h = kPadArrowSize,
                    .priority = kPadLabelPri,
                    .file = kPadArrowTex})
      .tex(AnimeTexAbs{.frameStart = start.name(),
                       .x = kPadArrowLeftX + kPadArrowSize,
                       .y = kPadArrowY,
                       .w = -kPadArrowSize,
                       .h = kPadArrowSize,
                       .priority = kPadLabelPri,
                       .file = kPadArrowTex})
      .blank();

  b.comment("type badge").comment(kColsWindow);
  b.window(AnimeWindow{.frameStart = start.name(),
                       .x = kPadBadgeX,
                       .y = kPadBadgeY,
                       .w = kPadBadgeW,
                       .h = kPadBadgeH,
                       .priority = kPadBoxPri,
                       .wndTypeVar = nullptr,
                       .wndTypeLiteral = "BTN01_ON",
                       .alpha = 255});
  b.comment(kColsMessage)
      .message(AnimeMessageAbs{.frameStart = start.name(),
                               .x = kPadBadgeLabelX,
                               .y = kPadBadgeLabelY,
                               .fontW = kPadFontW,
                               .fontH = kPadFontH,
                               .priority = kPadLabelPri,
                               .contentVar = typeName.name()})
      .blank();

  b.comment("general diagram").comment(kColsWindow);
  for (const auto &a : kPadGeneralAnchors)
    box(generalVis, a, kPadLabelH);
  b.comment(kColsMessage);
  for (int i = 0; i < kGeneralSlots; ++i) {
    const PadAnchor &a = kPadGeneralAnchors[i];
    text(generalVis, a.x + a.w / 2, a.y + kPadLabelInsetY, label[i].name());
  }
  b.blank();

  b.comment("mecha shooting diagram").comment(kColsWindow);
  for (int i = 0; i < kMechatSlots; ++i) {
    if (i == kPadAimSlot)
      continue;
    box(mechatVis, kPadMechatAnchors[i], kPadLabelH);
  }
  box(aimShortVis, kPadMechatAnchors[kPadAimSlot], kPadLabelH);
  box(aimTallVis, kPadMechatAnchors[kPadAimSlot], kPadAimTallH);
  b.comment(kColsMessage);
  for (int i = 0; i < kMechatSlots; ++i) {
    const PadAnchor &a = kPadMechatAnchors[i];
    text(mechatVis, a.x + a.w / 2, a.y + kPadLabelInsetY, label[i].name());
  }
  const PadAnchor &aim = kPadMechatAnchors[kPadAimSlot];
  text(aimTallVis, aim.x + aim.w / 2, aim.y + kPadLabelInsetY + kPadLineH,
       label[kInvertSlot].name());
}

void SettingItemTemplate::declareVars(CsvBuilder &b) {
    b.vars(FloatV{"RowW", 700.0}, FloatV{"Dim", 255.0},
        ColorV{"LblCol", 255, 255, 255, 255}, FloatV{"RowVis", 1.0},
        FloatV{"HdrVis", -1.0}, StringV{"Hdr", ""});
    for (const auto &opt : kSettingOptVars)
        b.vars(FloatV{opt.vis, -1.0}, StringV{opt.name, ""},
            StringV{opt.wnd, "BTN01_OF"}, FloatV{opt.dim, 255.0});
    b.vars(FloatV{"SldVis", -1.0}, FloatV{"SldFill", 0.0},
        FloatV{"SldRate", 0.0}, FloatV{ "SldThumbTravel", 0.0 }, StringV{"SldVal", ""});
}

// The highlight stops at the row's last value element rather than running the
// full cell width, which RefreshSettingsVisuals drives through RowW. A section
// title carries no highlight at all, which RowVis takes away: the engine owns
// WndType and would light the cell the cursor passed over.
AnimeWindow SettingItemTemplate::rowWindow() const {
  AnimeWindow w = RowTemplate::rowWindow();
  w.frameStart = "RowVis";
  w.relWExpr = "RowW";
  return w;
}

AnimeMessage SettingItemTemplate::rowLabel() const {
  AnimeMessage m = RowTemplate::rowLabel();
  m.frameStart = "RowVis";
  m.offsetX = 24;
  m.fontW = 18;
  m.fontH = 24;
  m.offsetY = CenterY(m.fontH);
  m.priority = 1;
  m.alphaRef = "Dim";
  m.colorR = "LblCol.r";
  m.colorG = "LblCol.g";
  m.colorB = "LblCol.b";
  return m;
}

void SettingItemTemplate::buildValue(CsvBuilder &b) {
  b.comment("camp-style divider line under the row")
      .frame(AnimeFrameRel{.frameStart = "RowVis",
                           .posRef = "pos",
                           .offsetX = 4,
                           .offsetY = kSettingRowH - 1,
                           .w = kRowContentW,
                           .h = 1,
                           .priority = 4,
                           .alphaRef = "alpha-191"})
      .blank();

  b.comment("section title, in place of the row on a header slot")
      .message(AnimeMessage{.frameStart = "HdrVis",
                            .posRef = "pos",
                            .offsetX = 6,
                            .offsetY = kSettingRowH - kSectionFontH - 2,
                            .fontW = kSectionFontW,
                            .fontH = kSectionFontH,
                            .priority = 1,
                            .alphaRef = "alpha",
                            .colorR = "255",
                            .colorG = "255",
                            .colorB = "255",
                            .contentVar = "Hdr"})
      .frame(AnimeFrameRel{.frameStart = "HdrVis",
                           .posRef = "pos",
                           .offsetX = 4,
                           .offsetY = kSettingRowH - 1,
                           .w = kRowContentW,
                           .h = 2,
                           .priority = 4,
                           .alphaRef = "alpha-127"})
      .blank();

  b.comment("value buttons (discrete settings / action rows)");
  for (int k = 0; k < kMaxOpts; ++k) {
    const auto &opt = kSettingOptVars[k];
    const int bx = kBtnX0 + k * kBtnStride;
    b.window(AnimeWindow{.frameStart = opt.vis,
                         .posRef = "pos",
                         .wndTypeVar = opt.wnd,
                         .alpha = 128,
                         .offsetX = bx,
                         .offsetY = kBtnY,
                         .relW = kBtnW,
                         .relH = kBtnH});
    b.message(AnimeMessage{.frameStart = opt.vis,
                           .posRef = "pos",
                           .offsetX = bx + kBtnW / 2,
                           .offsetY = 5,
                           .fontW = 17,
                           .fontH = 22,
                           .priority = 1,
                           .alphaRef = opt.dim,
                           .colorR = "255",
                           .colorG = "255",
                           .colorB = "255",
                           .contentVar = opt.name,
                           .font = "meiryo",
                           .posType = "center"});
  }
  b.blank();

  b.comment("slider: background window + camp bar track + rate-scaled fill + thumb + value text")
      .window(AnimeWindow{.frameStart = "SldVis",
                          .posRef = "pos",
                          .wndTypeLiteral = "BTN01_ON",
                          .alpha = 128,
                          .offsetX = kSldX,
                          .offsetY = kSldY,
                          .relW = kSldW,
                          .relH = kSldH })
      .tex(AnimeTex{.frameStart = "SldVis",
                    .posRef = "pos",
                    .offsetX = kTrackX,
                    .offsetY = kSldY,
                    .w = kTrackW,
                    .h = kSldH,
                    .priority = 2,
                    .alpha = 255,
                    .uv = {0, 0, 0, 0},
                    .file = kBarConfig01Tex })
      .tex(AnimeTex{.frameStart = "SldVis",
                    .posRef = "pos",
                    .offsetX = kTrackX + 2,
                    .offsetY = kSldY,
                    .h = kSldH,
                    .priority = 1,
                    .alpha = 255,
                    .uv = {0, 0, 0, 0},
                    .file = kBarConfig02Tex,
                    .wExpr = "SldFill",
                    .u1Expr = "SldRate" })
      .tex(AnimeTexAbs{.frameStart = "SldVis",
                       .x = 0,
                       .y = 0,
                       .w = 32,
                       .h = 32,
                       .priority = 0,
                       .alpha = 255,
                       .uv = {0, 0, 0, 0},
                       .file = kSlideConfigTex,
                       .xExpr = "SldThumbTravel+708",
                       .yExpr = "pos.y+6" })
      .message(AnimeMessage{.frameStart = "SldVis",
                            .posRef = "pos",
                            .offsetX = kSldX + kSldW + 18,
                            .offsetY = 6,
                            .fontW = 17,
                            .fontH = 22,
                            .priority = 1,
                            .alphaRef = "Dim",
                            .colorR = "255",
                            .colorG = "255",
                            .colorB = "255",
                            .contentVar = "SldVal" });
}

int SettingItemTemplate::RowWidth(bool slider, int optCount) {
  if (slider)
    return kRowContentW;
  optCount = std::max(optCount, 1);
  const int w = kBtnX0 + (optCount - 1) * kBtnStride + kBtnW + 18;
  return std::min(w, kRowContentW + 6);
}

int SettingItemTemplate::ButtonAt(f32 x, int optCount) {
  for (int k = 0; k < std::min(optCount, kMaxOpts); ++k) {
    const f32 bx = static_cast<f32>(kBtnX0 + k * kBtnStride);
    if (x >= bx && x <= bx + static_cast<f32>(kBtnW))
      return k;
  }
  return -1;
}

bool SettingItemTemplate::SliderFractionAt(f32 x, double& fraction) {
    const f32 trackStart = static_cast<f32>(kTrackX + 2);
    const f32 trackEnd = trackStart + static_cast<f32>(kSliderWidth);
    if (x < trackStart || x > trackEnd)
        return false;
    const double f =
        (static_cast<double>(x) - trackStart) / static_cast<double>(kSliderWidth);
    fraction = std::clamp(f, 0.0, 1.0);
    return true;
}

void KeybindItemTemplate::declareVars(CsvBuilder &b) {
  b.vars(FloatV{"Dim", 255.0}, FloatV{"RowW", double(kKeybindCellW)},
         FloatV{"RowVis", 1.0});
  for (const auto &slot : kKeybindSlots) {
    b.vars(StringV{slot.textVar, ""}, StringV{slot.wndVar, slot.wndDefault},
           ColorV{slot.colorVar, 255, 255, 255});
    b.vars(FloatV{slot.capVar, -1.0}, FloatV{slot.pairVar, -1.0});
    b.pos(AnimePos{slot.uvVar, 0, 0, 0, 0});
    b.pos(AnimePos{slot.modUvVar, 0, 0, 0, 0});
  }
}

// The box closes after the row's last key rather than running the cell out,
// the same thing the settings rows do with their own RowW. It also carries a
// gate of its own: the grid's spacer cells have no row to draw, and WndType is
// the engine's own var to write, so taking that from it would fight the cursor
// frame for the rows that do.
AnimeWindow KeybindItemTemplate::rowWindow() const {
  AnimeWindow w = RowTemplate::rowWindow();
  w.frameStart = "RowVis";
  w.relWExpr = "RowW";
  w.relH = 30;
  return w;
}

AnimeMessage KeybindItemTemplate::rowLabel() const {
  AnimeMessage m = RowTemplate::rowLabel();
  m.offsetX = 22;
  m.fontW = 16;
  m.fontH = 21;
  m.offsetY = CenterY(m.fontH);
  m.priority = 1;
  m.alphaRef = "Dim";
  return m;
}

void KeybindItemTemplate::buildValue(CsvBuilder &b) {
  for (const auto &slot : kKeybindSlots) {
    const std::string r = std::format("{}.r", slot.colorVar);
    const std::string g = std::format("{}.g", slot.colorVar);
    const std::string bl = std::format("{}.b", slot.colorVar);
    const std::string u0 = std::format("{}.x", slot.uvVar);
    const std::string v0 = std::format("{}.y", slot.uvVar);
    const std::string u1 = std::format("{}.w", slot.uvVar);
    const std::string v1 = std::format("{}.h", slot.uvVar);
    const std::string m0 = std::format("{}.x", slot.modUvVar);
    const std::string n0 = std::format("{}.y", slot.modUvVar);
    const std::string m1 = std::format("{}.w", slot.modUvVar);
    const std::string n1 = std::format("{}.h", slot.modUvVar);
    // A bare cap centers alone, a modifier pair centers as a unit.
    const int capX = slot.x + slot.w / 2 - kKeyCapW / 2;
    const int modX =
        slot.x + (slot.w - kKeyModCapW - kKeyPairGap - kKeyCapW) / 2;
    const int pairKeyX = modX + kKeyModCapW + kKeyPairGap;
    b.comment(slot.comment)
        .window(AnimeWindow{.frameStart = "start",
                            .posRef = "pos",
                            .wndTypeVar = slot.wndVar,
                            .alpha = 128,
                            .offsetX = slot.x,
                            .offsetY = 2,
                            .relW = slot.w,
                            .relH = 26})
        // The centered text carries only what a cap cannot: the capture
        // ellipsis, the unbound word, and a bind no cap art covers.
        .message(AnimeMessage{.frameStart = "start",
                              .posRef = "pos",
                              .offsetX = slot.x + slot.w / 2,
                              .offsetY = 5,
                              .fontW = 15,
                              .fontH = 20,
                              .priority = 1,
                              .alphaRef = "Dim",
                              .colorR = r.c_str(),
                              .colorG = g.c_str(),
                              .colorB = bl.c_str(),
                              .contentVar = slot.textVar,
                              .font = "meiryo",
                              .posType = "center"})
        .tex(AnimeTex{.frameStart = slot.capVar,
                      .posRef = "pos",
                      .offsetX = capX,
                      .offsetY = kKeyCapY,
                      .w = kKeyCapW,
                      .h = kKeyCapH,
                      .priority = 1,
                      .file = kHelpSheetTex,
                      .u0Expr = u0.c_str(),
                      .v0Expr = v0.c_str(),
                      .u1Expr = u1.c_str(),
                      .v1Expr = v1.c_str(),
                      .alphaExpr = "Dim"})
        .tex(AnimeTex{.frameStart = slot.pairVar,
                      .posRef = "pos",
                      .offsetX = modX,
                      .offsetY = kKeyCapY,
                      .w = kKeyModCapW,
                      .h = kKeyCapH,
                      .priority = 1,
                      .file = kHelpSheetTex,
                      .u0Expr = m0.c_str(),
                      .v0Expr = n0.c_str(),
                      .u1Expr = m1.c_str(),
                      .v1Expr = n1.c_str(),
                      .alphaExpr = "Dim"})
        .tex(AnimeTex{.frameStart = slot.pairVar,
                      .posRef = "pos",
                      .offsetX = pairKeyX,
                      .offsetY = kKeyCapY,
                      .w = kKeyCapW,
                      .h = kKeyCapH,
                      .priority = 1,
                      .file = kHelpSheetTex,
                      .u0Expr = u0.c_str(),
                      .v0Expr = v0.c_str(),
                      .u1Expr = u1.c_str(),
                      .v1Expr = v1.c_str(),
                      .alphaExpr = "Dim"});
  }
}

ConfigLayout::ConfigLayout() {
  for (int p = 0; p < kSettingsSectionCount; ++p)
    settingsList[p] = PageList(kSettingsListNames[p], "l_modmgr_setting.csv");
}

void ConfigLayout::SetSectionCount(int count) {
  sectionCount_ = std::clamp(count, 1, kSectionCount);
  sectionMenu.rows = sectionCount_;
  sectionMenu.defaultItem = sectionCount_;
  sectionMenu.h =
      sectionCount_ * kSectionRowH + (sectionCount_ - 1) * kSectionRowGap;
}

void ConfigLayout::SetListCount(AnimeMenuWidget &list, size_t count,
                                const char *tpl) {
  if (count == 0) {
    list.h = 65;
    list.rows = 1;
    list.defaultItem = 1;
    list.itemOnType = "NOFRAME03";
    list.itemOffType = "NOFRAME03";
    list.templateCSV = "l_modmgr_nodata.csv";
    return;
  }

  const int rows = static_cast<int>(std::min(count, kMaxVisibleRows));
  list.h = rows * 45 + 20;
  list.rows = rows;
  list.defaultItem = 0;
  list.itemOnType = "FRAME01";
  list.itemOffType = "BTN01_OF";
  list.templateCSV = tpl;
}

void ConfigLayout::SetModCount(size_t count) {
  SetListCount(modList, count, "l_modmgr_info.csv");
}

void ConfigLayout::SetDLCCount(size_t count) {
  SetListCount(dlcList, count, "l_modmgr_dlcinfo.csv");
}

// The catalog is never empty (the title's XDBF ships 43), so unlike the mod and
// DLC lists there is no no-data branch. The rows are a settings page's, not a
// mod list's, so the list runs to the same depth a settings page does.
void ConfigLayout::SetAchievementCount(size_t count) {
  const int rows = static_cast<int>(std::min(count, kMaxSettingsRows));
  achvList.h = rows * achvList.itemH + 20;
  achvList.rows = rows;
  achvList.defaultItem = 0;
}

void ConfigLayout::SetSettingsCounts(
    const size_t (&pageCounts)[kSettingsSectionCount], size_t keybinds) {
  for (int p = 0; p < kSettingsSectionCount; ++p) {
    const int n = static_cast<int>(std::min(pageCounts[p], kMaxSettingsRows));
    settingsList[p].h = n * kSettingRowH + 20;
    settingsList[p].rows = n;
    // Entry data is added per row at discovery, so a page longer than the
    // window scrolls the way the mod list does.
    settingsList[p].defaultItem = 0;
  }

  // The grid carries the binds plus the spacer slots of the band between the
  // sections (see KeybindSlotToIndex), and the cursor bound runs through the
  // last bind's slot.
  const int kbSlots = KeybindIndexToSlot(static_cast<int>(keybinds) - 1) + 1;
  const int kbRows = (kbSlots + 1) / 2;
  // Exactly the rows, with nothing over: the engine spreads the extent across
  // the cell count, so any slack here widens the pitch the section panels are
  // measured against.
  keybindList.h = kbRows * kKeybindRowH;
  keybindList.rows = kbRows;
  keybindList.defaultItem = kbSlots;
}

void ConfigLayout::build(CsvBuilder &b) {
  // Column headers, one per pane. Names are pushed every frame by
  // PopulateNames rather than baked, which a CSV cannot do without routing the
  // text through the ASCII CSV path.
  const struct {
    int x;
    const char *var;
  } kHeaders[] = {{105, hdrSections.name()},
                  {325, hdrMods.name()},
                  {815, hdrDetails.name()}};

  // The two rules bracketing the content area.
  constexpr int kRuleY[] = {95, 617};

  // The keybind screen's three blocks: the two grid columns and the compat
  // band under them. Each is a panel with a title over it, and the titles line
  // up with the row labels inside.
  const struct {
    int x, y, w, h, titleX;
    const char *titleVar;
  } kKeybindSections[] = {
      {kKeybindGridX - kSectionPad, kSectionTopY,
       kKeybindCellW + 2 * kSectionPad, kSectionTopH,
       kKeybindGridX + kSectionLabelX, hdrActions.name()},
      {kKeybindGridX + kKeybindColStride - kSectionPad, kSectionTopY,
       kKeybindCellW + 2 * kSectionPad, kSectionTopH,
       kKeybindGridX + kKeybindColStride + kSectionLabelX,
       hdrMovement.name()},
      {kKeybindGridX - kSectionPad, kSectionCompatY,
       kKeybindColStride + kKeybindCellW + 2 * kSectionPad, kSectionCompatH,
       kKeybindGridX + kSectionLabelX, hdrCompat.name()},
  };

  // One footer prompt: its icon cell in res\cmn_help_menue and the two vars
  // driving its visibility and label. B is always shown, so it gates on the
  // layout's own 'start'.
  const struct {
    const char *visVar, *labelVar;
    int iconX, labelX;
    const HelpUv &uv;
  } kFooter[] = {
      {ftrAVis.name(), ftrA.name(), 115, 160, kFooterGlyphs[0].uv},
      {"start", ftrB.name(), 335, 381, kFooterGlyphs[1].uv},
      {ftrXVis.name(), ftrX.name(), 555, 601, kFooterGlyphs[2].uv},
      {ftrYVis.name(), ftrY.name(), 755, 801, kFooterGlyphs[3].uv},
      {ftrBackVis.name(), ftrBack.name(), 955, 1004, kFooterGlyphs[4].uv},
  };

  // Centered on the 1280-wide screen, clear of the tenth list row above and,
  // on its second line, of the footer divider at 612.
  const auto rowDesc = [](int y, const char *var) {
    return AnimeMessageAbs{.frameStart = "start",
                           .x = 640,
                           .y = y,
                           .fontW = 15,
                           .fontH = 19,
                           .priority = 4,
                           .alpha = 200,
                           .r = 190,
                           .g = 190,
                           .b = 190,
                           .contentVar = var,
                           .posType = "center"};
  };

  // Each content list, in the order the CSV declares them. The sidebar closes
  // the block it shares with no other list, hence the second gap.
  const struct {
    const char *comment;
    const AnimeMenuWidget *menu;
    int gap;
  } kLists[] = {
      {"section sidebar", &sectionMenu, 2},
      {"mod list", &modList, 1},
      {"achievement list", &achvList, 1},
      {"dlc list", &dlcList, 1},
  };

  b.panel(cfgIcon).blank();
  b.panel(detail).blank();
  b.panel(dlcDetail).blank();
  b.panel(pad).blank();

  b.comment("variable definitions").vars(start, alpha);
  Declare(b, title, hdrSections, hdrMods, hdrDetails, ftrA, ftrB, ftrX, ftrY,
          ftrBack, ftrAVis, ftrXVis, ftrYVis, ftrBackVis, restartVis,
          restartNote, rowDesc0, rowDesc1, hdrActions, hdrMovement, hdrCompat,
          kbHint, kbChromeVis);
  b.blank();

  if (standalone_)
    b.comment(kColsWindow)
        .window(AnimeWindow{.frameStart = "start",
                            .x = -100,
                            .y = -100,
                            .w = 2000,
                            .h = 2000,
                            .priority = 5,
                            .wndTypeVar = nullptr,
                            .wndTypeLiteral = "BTN01_OF",
                            .alpha = 255,
                            .r = 40,
                            .g = 0,
                            .b = 145})
        .blank();

  b.comment(kColsMessage);
  for (const auto &h : kHeaders)
    b.message(AnimeMessageAbs{.frameStart = "start",
                              .x = h.x,
                              .y = kHeaderY,
                              .fontW = kHeaderFontW,
                              .fontH = kHeaderFontH,
                              .priority = kHeaderPri,
                              .contentVar = h.var});

  // Keybind screen chrome, hidden everywhere else. Sectioned the way the game
  // sections its own tables: a black panel behind each block, as its key
  // config draws behind the controller diagram, and a title closed by a rule
  // over a boxed cell per row, as the camp skill table does.
  b.blank().comment("keybind screen sections").comment(kColsFrame);
  for (const auto &s : kKeybindSections)
    b.frame(AnimeFrame{.frameStart = kbChromeVis.name(),
                       .x = s.x,
                       .y = s.y,
                       .w = s.w,
                       .h = s.h,
                       .priority = kSectionPanelPri,
                       .alphaRef = "alpha-200",
                       .r = 0,
                       .g = 0,
                       .b = 0,
                       .tag = "#panel"});
  for (const auto &s : kKeybindSections)
    b.frame(AnimeFrame{.frameStart = kbChromeVis.name(),
                       .x = s.x + kSectionPad,
                       .y = s.y + kSectionTitleH,
                       .w = s.w - 2 * kSectionPad,
                       .h = 2,
                       .priority = kSectionTitlePri,
                       .alphaRef = "alpha-127",
                       .tag = "#title rule"});
  // The compat block runs both columns, so it needs the divider the two
  // panels above it get from the gap between them.
  b.frame(AnimeFrame{.frameStart = kbChromeVis.name(),
                     .x = kKeybindColSplitX,
                     .y = kKeybindCompatRowY,
                     .w = 1,
                     .h = kKeybindCompatRows * kKeybindRowH,
                     .priority = kSectionTitlePri,
                     .alphaRef = "alpha-127",
                     .tag = "#column rule"});

  b.blank().comment(kColsMessage);
  for (const auto &s : kKeybindSections)
    b.message(AnimeMessageAbs{.frameStart = kbChromeVis.name(),
                              .x = s.titleX,
                              .y = s.y + 3,
                              .fontW = kSectionFontW,
                              .fontH = kSectionFontH,
                              .priority = kSectionTitlePri,
                              .contentVar = s.titleVar});
  // Beside the last title rather than over the grid: the band it used to sit
  // in carries a section title now.
  b.message(AnimeMessageAbs{.frameStart = kbChromeVis.name(),
                            .x = kKeybindSections[2].x + kKeybindSections[2].w -
                                 kSectionLabelX,
                            .y = kKeybindSections[2].y + 10,
                            .fontW = 15,
                            .fontH = 19,
                            .alpha = 200,
                            .r = 190,
                            .g = 190,
                            .b = 190,
                            .contentVar = kbHint.name(),
                            .posType = "right"});
  b.blank().blank();

  for (const auto &l : kLists) {
    b.comment(l.comment).comment(kColsMenu).menu(*l.menu);
    for (int i = 0; i < l.gap; ++i)
      b.blank();
  }

  b.comment("settings lists (one per page)").comment(kColsMenu);
  for (const auto &list : settingsList)
    b.menu(list);
  b.menu(keybindList).blank();

  // The two rules close the header and prompt bands. In camp both bands are
  // the game's own and already carry theirs.
  if (standalone_) {
    b.comment(kColsFrame);
    for (int y : kRuleY)
      b.frame(AnimeFrame{.frameStart = "start",
                         .x = 0,
                         .y = y,
                         .w = 1280,
                         .h = 2,
                         .priority = 4,
                         .alphaRef = "alpha-127",
                         .tag = "#line"});
  }
  b.blank();

  b.comment(kColsMessage)
      // Where the game rests its own screen title, in L_cfg01 and at the end
      // of M_top_cfg's fly-in alike: clear of the icon at 115,35.
      .message(AnimeMessageAbs{.frameStart = "start",
                               .x = 180,
                               .y = 45,
                               .fontW = 43,
                               .fontH = 48,
                               .priority = 0,
                               .contentVar = title.name()})
      .message(AnimeMessageAbs{.frameStart = restartVis.name(),
                               .x = 1010,
                               .y = 588,
                               .fontW = 15,
                               .fontH = 19,
                               .priority = 4,
                               .alpha = 200,
                               .r = 255,
                               .g = 220,
                               .b = 0,
                               .contentVar = restartNote.name()})
      .message(rowDesc(568, rowDesc0.name()))
      .message(rowDesc(590, rowDesc1.name()))
      .blank()
      .setVal("cfg_icon.pos", "115,35,64,64,0")
      .blank();

  // Both surfaces draw the same five prompts. The camp screen's own band
  // (d2anime\L_ftr.csv) has slots for two, so CampSettings blanks them and
  // this takes over the band it already ruled off.
  b.blank().comment("footer cap rects, written by ConfigMenu");
  for (const PromptGlyph &g : kFooterGlyphs)
    b.pos(AnimePos{g.posVar, 0, 0, 0, 0});
  b.blank().comment("footer button icons").comment(kColsTex);
  for (const auto &f : kFooter)
      b.tex(AnimeTexAbs{.frameStart = f.visVar,
                        .x = f.iconX,
                        .y = kFooterIconY,
                        .w = kFooterIconSize,
                        .h = kFooterIconSize,
                        .priority = 0,
                        .alpha = 255,
                        .uv = {},
                        .file = kHelpSheetTex,
                        .helpUv = &f.uv });
  b.blank();

  b.comment("footer labels").comment(kColsMessage);
  for (const auto &f : kFooter)
    b.message(AnimeMessageAbs{.frameStart = f.visVar,
                              .x = f.labelX,
                              .y = kFooterLabelY,
                              .fontW = kFooterFontW,
                              .fontH = kFooterFontH,
                              .contentVar = f.labelVar});
  b.blank();

  // start=-1 hides panels until a row is selected.
  b.set(detail, "start", "-1");
  b.set(dlcDetail, "start", "-1");
  b.set(pad, "start", "-1");

  // SelMesWinTask vars, read by SelMesWinConfig_LoadStrings("RBDEL").
  b.blank()
      .comment("sysmes confirmation dialog variables")
      .vars(rbdelSQ1, rbdelSQ2, rbdelSQ3, rbdelSA1, rbdelSA2, rbdelFS, rbdelLN)
      .pos(rbdelPS_)
      .pos(rbdelOFS_)
      .vars(rbdelWCL, rbdelECL, rbdelFCL);
}

} // namespace bd::engine
