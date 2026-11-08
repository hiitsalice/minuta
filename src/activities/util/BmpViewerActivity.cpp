#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr int MAX_IMAGE_DISPLAY_WIDTH = 400;
constexpr int MAX_IMAGE_DISPLAY_HEIGHT = 720;

struct ImageFit {
  int x;
  int y;
  int width;
  int height;
};

ImageFit fitImageToScreen(const int sourceWidth, const int sourceHeight, const int screenWidth,
                          const int screenHeight) {
  if (sourceWidth <= 0 || sourceHeight <= 0 || screenWidth <= 0 || screenHeight <= 0) {
    return {0, 0, 0, 0};
  }

  float scale = 1.0f;
  scale = std::min(scale, static_cast<float>(MAX_IMAGE_DISPLAY_WIDTH) / sourceWidth);
  scale = std::min(scale, static_cast<float>(MAX_IMAGE_DISPLAY_HEIGHT) / sourceHeight);

  const int width = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
  const int height = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));

  return {(screenWidth - width) / 2, (screenHeight - height) / 2, width, height};
}
}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        if (FsHelpers::hasBmpExtension(fname) || FsHelpers::hasPngExtension(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(siblingImages);

  for (size_t i = 0; i < siblingImages.size(); ++i) {
    if (siblingImages[i] == fileName) {
      currentImageIndex = static_cast<int>(i);
      break;
    }
  }
}

bool BmpViewerActivity::renderPng() {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(filePath, dimensions)) return false;
  if (dimensions.width <= 0 || dimensions.height <= 0) return false;

  const ImageFit fit =
      fitImageToScreen(dimensions.width, dimensions.height, renderer.getScreenWidth(), renderer.getScreenHeight());
  if (fit.width <= 0 || fit.height <= 0) return false;

  RenderConfig config{fit.x, fit.y, fit.width, fit.height};

  PngToFramebufferConverter converter;
  return converter.decodeToFramebuffer(filePath, renderer, config);
}

namespace {
// File name without folder or extension, shown as the header on the image screen.
std::string imageHeaderTitle(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.erase(dot);
  return name;
}

void drawImageHeader(GfxRenderer& renderer, const char* title) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding - 6, renderer.getScreenWidth(), metrics.headerHeight}, title);
}
}  // namespace

void BmpViewerActivity::onEnter() {
  Activity::onEnter();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Prev / Next availability for the error screens (same rule as the image screens).
  const bool canPrev = siblingImages.size() > 1 && currentImageIndex > 0;
  const bool canNext = siblingImages.size() > 1 && currentImageIndex != -1 &&
                       currentImageIndex < static_cast<int>(siblingImages.size()) - 1;
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  if (FsHelpers::hasPngExtension(filePath)) {
    renderer.clearScreen();
    const bool hasPrevious = siblingImages.size() > 1 && currentImageIndex > 0;
    const bool hasNext = siblingImages.size() > 1 && currentImageIndex != -1 &&
                         currentImageIndex < static_cast<int>(siblingImages.size()) - 1;
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", hasPrevious ? "Prev" : "", hasNext ? "Next" : "");
    if (renderPng()) {
      drawImageHeader(renderer, imageHeaderTitle(filePath).c_str());
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      drawImageHeader(renderer, "Image Viewer");
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    return;
  }

  HalFile file;
  // 1. Open the BMP file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      const ImageFit fit = fitImageToScreen(bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
      const int x = fit.x;
      const int y = fit.y;
      const int displayWidth = fit.width;
      const int displayHeight = fit.height;

      // 4. Prepare Rendering
      bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                      currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", (hasPrevious ? "Prev" : ""), (hasNext ? "Next" : ""));

      GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      // Assuming drawBitmap defaults to 0,0 crop if omitted, or pass explicitly: drawBitmap(bitmap, x, y, pageWidth,
      // pageHeight, 0, 0)
      renderer.drawBitmap(bitmap, x, y, displayWidth, displayHeight, 0, 0);

      drawImageHeader(renderer, imageHeaderTitle(filePath).c_str());
      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      // Single pass for non-grayscale images

      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      drawImageHeader(renderer, "Image Viewer");
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_BMP_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", canPrev ? "Prev" : "", canNext ? "Next" : "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }

    file.close();
  } else {
    // Handle file open error
    renderer.clearScreen();
    drawImageHeader(renderer, "Image Viewer");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", canPrev ? "Prev" : "", canNext ? "Next" : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  auto openSibling = [this](const int delta) {
    if (currentImageIndex < 0) {
      return false;
    }
    const int nextIndex = currentImageIndex + delta;
    if (siblingImages.size() <= 1 || nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) {
      return false;
    }
    currentImageIndex = nextIndex;
    std::string dirPath = FsHelpers::extractFolderPath(filePath);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + siblingImages[currentImageIndex];
    onEnter();
    return true;
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openSibling(1);
    return;
  }
}
