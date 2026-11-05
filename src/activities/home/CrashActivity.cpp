#include "CrashActivity.h"

#include <algorithm>
#include <sstream>
#include <string>

#include <GfxRenderer.h>
#include <HalSystem.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

void CrashActivity::onEnter() {
  title_ = tr(STR_CRASH_TITLE);
  rows_.clear();
  boldMasks_.clear();
  items_.clear();

  const std::string reportText = HalSystem::getPanicInfo(true);

  rows_.emplace_back("Minuta restarted after an unexpected error.");

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int textWidth = std::max(
      1, static_cast<int>(renderer.getScreenWidth()) -
             2 * static_cast<int>(metrics.contentSidePadding) - 8);

  std::istringstream report(reportText);
  std::string line;

  while (std::getline(report, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty()) {
      continue;
    }

    const auto wrapped = renderer.wrappedText(
        UI_10_FONT_ID, line.c_str(), textWidth, 128);

    if (wrapped.empty()) {
      rows_.push_back(line);
    } else {
      for (const auto& wrappedLine : wrapped) {
        rows_.push_back(wrappedLine);
      }
    }
  }

  // ListItem labels and bold masks point into these vectors, so build
  // them only after rows_ is final and reserve before storing pointers.
  items_.reserve(rows_.size());
  boldMasks_.reserve(rows_.size());

  for (const auto& row : rows_) {
    const char* boldPrefix = nullptr;

    if (row.rfind("Minuta restarted after an unexpected error.", 0) == 0) {
      boldPrefix = "Minuta restarted after an unexpected error.";
    } else if (row.rfind("Firmware version:", 0) == 0) {
      boldPrefix = "Firmware version:";
    } else if (row.rfind("Panic reason:", 0) == 0) {
      boldPrefix = "Panic reason:";
    } else if (row.rfind("Last logs:", 0) == 0) {
      boldPrefix = "Last logs:";
    } else if (row.rfind("Stack memory:", 0) == 0) {
      boldPrefix = "Stack memory:";
    }

    boldMasks_.emplace_back(row.size(), '0');
    auto& mask = boldMasks_.back();

    if (boldPrefix) {
      const size_t prefixLength = std::string(boldPrefix).size();
      std::fill(mask.begin(), mask.begin() + prefixLength, '1');
    }

    fui::ListItem item{};
    item.label = row.c_str();
    item.boldMask = boldPrefix ? mask.c_str() : nullptr;
    items_.push_back(item);
  }

  UiListActivity::onEnter();
}

void CrashActivity::buildScreen(UiScreen& screen) {
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  refreshSharedUiThemeTokens(uiTarget);

  const auto& metrics = UITheme::getInstance().getMetrics();

  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight - 3),
      0,
      static_cast<int16_t>(metrics.buttonHintsHeight),
      0
  });

  screen.spacer(static_cast<int16_t>(
      metrics.verticalSpacing > 6 ? metrics.verticalSpacing - 6 : 0));

  fui::ListProps props;
  props.items = items_.data();
  props.count = static_cast<uint16_t>(items_.size());
  props.action = ACTION_ROW;
  props.valueInset = 3;
  props.rowHeight = static_cast<int16_t>(metrics.listRowHeight);
  props.rowGap = 0;
  props.scrollIndicator = true;
  props.labelText = screen.theme().smallText;
  props.labelText.align = fui::TextAlign::Left;
  props.labelText.maxLines = 1;
  props.labelText.lineGap = 6;

  syncListViewport(screen, props);

  // View-only: keep scrolling, but never show a selected-row highlight.
  props.selectedIndex = -1;
  screen.list(props);
}

void CrashActivity::activateIndex(int) {
  // Diagnostic rows are informational and cannot be activated.
}

void CrashActivity::scrollPages(const int direction) {
  const int count = listCount();
  const int pageSize = std::max(1, nav.pageRows());
  const int maxTop = std::max(0, count - pageSize);

  int newTop = nav.top + direction * pageSize;
  newTop = std::clamp(newTop, 0, maxTop);

  if (newTop == nav.top) return;

  {
    RenderLock lock(*this);
    nav.top = newTop;
    nav.selected = newTop;
  }

  requestUpdate();
}

void CrashActivity::navigateButtons() {
  navigator_.onNext([this] { scrollPages(1); });
  navigator_.onPrevious([this] { scrollPages(-1); });
}

void CrashActivity::drawFooter() {
  const int pageSize = std::max(1, nav.pageRows());
  const bool canScrollUp = nav.top > 0;
  const bool canScrollDown =
      static_cast<int>(nav.top) + pageSize < listCount();

  const auto labels = mappedInput.mapLabels(
      tr(STR_BACK), "",
      canScrollUp ? "Prev" : "",
      canScrollDown ? "Next" : "");

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                      -1, drawButtonHintDivider() && listFillsArea());
}
