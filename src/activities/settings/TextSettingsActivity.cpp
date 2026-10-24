#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "TextSettingsPreview.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// One name per row, in list order.
constexpr StrId ROW_NAME_IDS[] = {StrId::STR_FONT,         StrId::STR_SIZE,         StrId::STR_LINE_SPACING,
                                  StrId::STR_EXTRA_SPACING, StrId::STR_ALIGNMENT,    StrId::STR_SCREEN_MARGIN,
                                  StrId::STR_FOCUS_READING, StrId::STR_HYPHENATION,  StrId::STR_EMBEDDED_STYLE,
                                  StrId::STR_TEXT_AA};

int findCurrentFontIndex(const SdCardFontRegistry* registry, const char* sdFontFamilyName, uint8_t fontFamily) {
  if (sdFontFamilyName[0] != '\0' && registry) {
    const auto& families = registry->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == sdFontFamilyName) {
        return CrossPointSettings::BUILTIN_FONT_COUNT + i;
      }
    }
  }

  return fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? fontFamily : 0;
}

constexpr StrId LINE_SPACING_IDS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId ALIGNMENT_IDS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE};
constexpr int MARGIN_MIN = CrossPointSettings::SCREEN_MARGIN_MIN;
constexpr int MARGIN_MAX = CrossPointSettings::SCREEN_MARGIN_MAX;
constexpr int MARGIN_STEP = CrossPointSettings::SCREEN_MARGIN_STEP;

// Size names shown in the list; any other point size falls back to "N pt".
void sizeLabel(const uint8_t pt, char* out, const size_t len, const bool youngSerif) {
  switch (pt) {
    case 12:
      snprintf(out, len, "Tiny");
      break;
    case 14:
      snprintf(out, len, "Small");
      break;
    case 16:
      snprintf(out, len, "Medium");
      break;
    case 18:
      snprintf(out, len, "Large");
      break;
    case 20:
      snprintf(out, len, youngSerif ? "Magnuta" : "20 pt");
      break;
    default:
      snprintf(out, len, "%u pt", pt);
      break;
  }
}
}  // namespace

TextSettingsActivity::TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SdCardFontRegistry* registry, Tab initialTab)
    : UiListActivity("TextSettings", renderer, mappedInput), registry_(registry), initialTab_(initialTab) {}

const char* TextSettingsActivity::headerTitle() const { return tr(STR_TEXT_SETTINGS); }

void TextSettingsActivity::onEnter() {
  UiListActivity::onEnter();

  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));
  fonts_.push_back({I18N.get(StrId::STR_YOUNG_SERIF), true, static_cast<uint8_t>(CrossPointSettings::YOUNGSERIF)});
  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  rebuildSizeList();
  currentFamilyIndex_ = findCurrentFontIndex(registry_, SETTINGS.sdFontFamilyName, SETTINGS.fontFamily);

  for (int i = 0; i < ROW_COUNT; i++) {
    rowItems_[i].label = I18N.get(ROW_NAME_IDS[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }

  int startRow = 0;
  switch (initialTab_) {
    case Tab::Size:
      startRow = static_cast<int>(Row::Size);
      break;
    case Tab::Layout:
      startRow = static_cast<int>(Row::LineSpacing);
      break;
    case Tab::Style:
      startRow = static_cast<int>(Row::FocusReading);
      break;
    default:
      break;
  }
  nav.selected = startRow;
  nav.followOnBuild = true;
  requestUpdate();
}

// The selectable sizes belong to the active family, so this runs on entry and again after every
// family change. The highlight is resolved by snapping, because the stored size can sit outside
// this family's set (family deleted while selected, card swapped).
void TextSettingsActivity::rebuildSizeList() {
  const std::vector<uint8_t> points = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);
  const uint8_t selectedPt = snapToNearestPointSize(points, SETTINGS.fontPointSize);

  sizes_.clear();
  sizes_.reserve(points.size());
  currentSizeIndex_ = 0;
  for (const uint8_t pt : points) {
    char label[12];
    sizeLabel(pt, label, sizeof(label), SETTINGS.sdFontFamilyName[0] == '\0');
    if (pt == selectedPt) currentSizeIndex_ = static_cast<int>(sizes_.size());
    sizes_.push_back({label, pt});
  }
}

int TextSettingsActivity::previewHeightPx() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int afterHeader = m.topPadding + m.headerHeight - 6 + m.verticalSpacing;
  const int bottomReserved = m.buttonHintsHeight + m.verticalSpacing;
  const int usable = renderer.getScreenHeight() - afterHeader - bottomReserved;
  return usable * m.previewHeightPercent / 100 + 12;
}

// Center the preview between the Anti-aliasing selection row and button hints.
int TextSettingsActivity::previewTopPx() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int listTop = m.topPadding + m.headerHeight - 3 +
                      (m.verticalSpacing > 6 ? m.verticalSpacing - 6 : 0);
  const int antiAliasingBottom = listTop + ROW_COUNT * m.listRowHeight;
  const int hintsTop = renderer.getScreenHeight() - m.buttonHintsHeight;
  return (antiAliasingBottom + hintsTop - previewHeightPx()) / 2 - 6;
}

void TextSettingsActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();
  // The list runs from the same first-row height as Settings down to the caption band above the preview.
  const int captionHeight = renderer.getTextHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  const int listBottom = previewTopPx() - captionHeight;
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3), 0,
                                      static_cast<int16_t>(renderer.getScreenHeight() - listBottom), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  for (int i = 0; i < ROW_COUNT; i++) {
    switch (static_cast<Row>(i)) {
      case Row::Font:
        rowValues_[i] = currentFamilyIndex_ < static_cast<int>(fonts_.size()) ? fonts_[currentFamilyIndex_].name : "";
        break;
      case Row::Size:
        rowValues_[i] = currentSizeIndex_ < static_cast<int>(sizes_.size()) ? sizes_[currentSizeIndex_].name : "";
        break;
      case Row::LineSpacing:
      case Row::ParaSpacing:
      case Row::Alignment:
      case Row::ScreenMargin:
        rowValues_[i] = layoutValueText(i - static_cast<int>(Row::LineSpacing));
        break;
      default:
        rowValues_[i] = styleValueText(i - static_cast<int>(Row::FocusReading));
        break;
    }
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(ROW_COUNT);
  props.action = ACTION_ROW;
  props.valueInset = 3;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.rowGap = 0;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.labelText.lineGap = 6;
  syncListViewport(screen, props);
  screen.list(props);
}

void TextSettingsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_TEXT_SETTINGS));

  textsettings::renderPreview(renderer, previewLayout_, metrics.previewPadding, previewTopPx(), previewHeightPx());
}

void TextSettingsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

}

void TextSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  switch (static_cast<Row>(index)) {
    case Row::Font:
      cycleFont();
      break;
    case Row::Size:
      cycleSize();
      break;
    case Row::LineSpacing:
    case Row::ParaSpacing:
    case Row::Alignment:
    case Row::ScreenMargin:
      confirmLayoutRow(index - static_cast<int>(Row::LineSpacing));
      break;
    case Row::FocusReading:
    case Row::Hyphenation:
    case Row::EmbeddedStyle:
    case Row::AntiAliasing:
      confirmStyleRow(index - static_cast<int>(Row::FocusReading));
      break;
    default:
      break;
  }
}

void TextSettingsActivity::cycleFont() {
  if (fonts_.size() < 2) return;
  const int next = (currentFamilyIndex_ + 1) % static_cast<int>(fonts_.size());
  applyFamily(next);
  // Persist immediately so leaving by the home key or a sleep never loses the change.
  if (currentFamilyIndex_ == next) {
    SETTINGS.saveToFile();
  }
  requestUpdate();
}

void TextSettingsActivity::cycleSize() {
  if (sizes_.size() < 2) return;
  const int next = (currentSizeIndex_ + 1) % static_cast<int>(sizes_.size());
  applySize(next);
  SETTINGS.saveToFile();
  requestUpdate();
}

// Font switching runs on the main task from loop(), which deliberately holds no RenderLock.
// ensureLoaded() deletes the resident SdCardFont before loading the next one, and the render task
// walks that same object inside the preview's prewarmCache(), so the switch takes the lock.
void TextSettingsActivity::applyFamily(int listIndex) {
  RenderLock lock;
  const auto& font = fonts_[listIndex];
  if (font.isBuiltin) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
    sdFontSystem.ensureLoaded(renderer);  // unloads the previously resident SD font
    currentFamilyIndex_ = listIndex;
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      sdFontSystem.ensureLoaded(renderer);
      currentFamilyIndex_ = listIndex;
    }
  }

  if (currentFamilyIndex_ != listIndex) return;  // switch failed, keep the old size list

  // The new family ships its own point sizes, and ensureLoaded() may have snapped the selection into it.
  rebuildSizeList();
}

// Same RenderLock rationale as applyFamily(): a size change reloads the SD font file.
void TextSettingsActivity::applySize(int listIndex) {
  RenderLock lock;

  currentSizeIndex_ = listIndex;
  SETTINGS.fontPointSize = sizes_[listIndex].pointSize;
  sdFontSystem.ensureLoaded(renderer);
}

void TextSettingsActivity::confirmLayoutRow(int row) {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::ParaSpacing:
      SETTINGS.extraParagraphSpacing = !SETTINGS.extraParagraphSpacing;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case LayoutRow::LineSpacing:
      SETTINGS.lineSpacing = (SETTINGS.lineSpacing + 1) % static_cast<uint8_t>(std::size(LINE_SPACING_IDS));
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case LayoutRow::Alignment:
      SETTINGS.paragraphAlignment = (SETTINGS.paragraphAlignment + 1) % static_cast<uint8_t>(std::size(ALIGNMENT_IDS));
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case LayoutRow::ScreenMargin: {
      const int current = std::clamp<int>(SETTINGS.screenMargin, MARGIN_MIN, MARGIN_MAX);
      if (current + MARGIN_STEP > MARGIN_MAX) {
        SETTINGS.screenMargin = MARGIN_MIN;
      } else {
        SETTINGS.screenMargin = static_cast<uint8_t>(current + MARGIN_STEP);
      }
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    }

    default:
      break;
  }
}

std::string TextSettingsActivity::layoutValueText(int row) const {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::LineSpacing: {
      const uint8_t v = SETTINGS.lineSpacing;
      return v < std::size(LINE_SPACING_IDS) ? I18N.get(LINE_SPACING_IDS[v]) : I18N.get(StrId::STR_NORMAL);
    }
    case LayoutRow::ParaSpacing:
      return SETTINGS.extraParagraphSpacing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case LayoutRow::Alignment: {
      const uint8_t v = SETTINGS.paragraphAlignment;
      return v < std::size(ALIGNMENT_IDS) ? I18N.get(ALIGNMENT_IDS[v]) : I18N.get(StrId::STR_JUSTIFY);
    }
    case LayoutRow::ScreenMargin:
      return std::to_string(SETTINGS.screenMargin);

    default:
      return "";
  }
}

void TextSettingsActivity::confirmStyleRow(int row) {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::FocusReading:
      SETTINGS.focusReadingEnabled = !SETTINGS.focusReadingEnabled;
      break;
    case StyleRow::Hyphenation:
      SETTINGS.hyphenationEnabled = !SETTINGS.hyphenationEnabled;
      break;
    case StyleRow::EmbeddedStyle:
      SETTINGS.embeddedStyle = !SETTINGS.embeddedStyle;
      break;
    case StyleRow::AntiAliasing:
      SETTINGS.textAntiAliasing = !SETTINGS.textAntiAliasing;
      break;

    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

std::string TextSettingsActivity::styleValueText(int row) const {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::FocusReading:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::Hyphenation:
      return SETTINGS.hyphenationEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::EmbeddedStyle:
      return SETTINGS.embeddedStyle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::AntiAliasing:
      return SETTINGS.textAntiAliasing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);

    default:
      return "";
  }
}

// Only Focus Reading shows in the preview (bold prefixes); the other style rows have no distinct preview.
bool TextSettingsActivity::focusedRowHasNoPreview() const {
  const int sel = nav.selected;
  return sel == static_cast<int>(Row::Hyphenation) || sel == static_cast<int>(Row::EmbeddedStyle) ||
         sel == static_cast<int>(Row::AntiAliasing);
}
