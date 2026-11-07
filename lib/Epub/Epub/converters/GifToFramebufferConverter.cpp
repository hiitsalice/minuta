#include "GifToFramebufferConverter.h"

#include <AnimatedGIF.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <memory>
#include <new>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"

namespace {

struct GifContext {
  DirectPixelWriter writer;
  const RenderConfig* config{nullptr};
  int screenWidth{0};
  int screenHeight{0};
  int sourceWidth{0};
  int sourceHeight{0};
  int destWidth{0};
  int destHeight{0};
  uint32_t lastYieldMs{0};
  bool drewPixels{false};
  bool invalidPalette{false};
};

void* gifOpen(const char* filename, int32_t* fileSize) {
  auto* file = new (std::nothrow) HalFile();
  if (!file) return nullptr;

  if (!Storage.openFileForRead("GIF", std::string(filename), *file)) {
    delete file;
    return nullptr;
  }

  *fileSize = static_cast<int32_t>(file->size());
  return file;
}

void gifClose(void* handle) {
  auto* file = static_cast<HalFile*>(handle);
  delete file;
}

int32_t gifRead(GIFFILE* gifFile, uint8_t* buffer, int32_t length) {
  auto* file = static_cast<HalFile*>(gifFile->fHandle);
  if (!file || length <= 0) return 0;

  const int bytesRead = file->read(buffer, length);
  if (bytesRead <= 0) return 0;

  gifFile->iPos += bytesRead;
  return bytesRead;
}

int32_t gifSeek(GIFFILE* gifFile, int32_t position) {
  auto* file = static_cast<HalFile*>(gifFile->fHandle);
  if (!file || position < 0 || !file->seek(static_cast<size_t>(position))) return -1;

  gifFile->iPos = position;
  return position;
}

uint8_t rgb565ToGray(uint16_t colour) {
  const uint8_t red = static_cast<uint8_t>((colour >> 11) & 0x1F) * 255 / 31;
  const uint8_t green = static_cast<uint8_t>((colour >> 5) & 0x3F) * 255 / 63;
  const uint8_t blue = static_cast<uint8_t>(colour & 0x1F) * 255 / 31;
  return static_cast<uint8_t>((77u * red + 150u * green + 29u * blue) >> 8);
}

void gifDrawCallback(GIFDRAW* draw) {
  auto* ctx = static_cast<GifContext*>(draw->pUser);
  if (!ctx || !ctx->config || !draw->pPixels || !draw->pPalette) return;

  ImageToFramebufferDecoder::yieldDuringDecode(ctx->lastYieldMs);

  const int sourceY = draw->iY + draw->y;
  if (sourceY < 0 || sourceY >= ctx->sourceHeight) return;

  // Map this source scanline to the destination rows that sample it.
  const int firstDestY = static_cast<int>(
      (static_cast<int64_t>(sourceY) * ctx->destHeight + ctx->sourceHeight - 1) /
      ctx->sourceHeight);
  const int afterDestY = static_cast<int>(
      (static_cast<int64_t>(sourceY + 1) * ctx->destHeight + ctx->sourceHeight - 1) /
      ctx->sourceHeight);

  const int frameLeft = draw->iX;
  const int frameRight = draw->iX + draw->iWidth;
  const int cfgX = ctx->config->x;
  const int cfgY = ctx->config->y;

  for (int destY = firstDestY; destY < afterDestY; ++destY) {
    const int outY = cfgY + destY;
    if (outY < 0 || outY >= ctx->screenHeight) continue;

    ctx->writer.beginRow(outY);

    for (int destX = 0; destX < ctx->destWidth; ++destX) {
      const int sourceX = static_cast<int>(
          static_cast<int64_t>(destX) * ctx->sourceWidth / ctx->destWidth);

      // The first GIF frame may occupy only part of the logical canvas.
      if (sourceX < frameLeft || sourceX >= frameRight) continue;

      const int outX = cfgX + destX;
      if (outX < 0 || outX >= ctx->screenWidth) continue;

      const uint8_t paletteIndex = draw->pPixels[sourceX - frameLeft];

      // Leave transparent pixels untouched so the page background remains visible.
      if (draw->ucHasTransparency && paletteIndex == draw->ucTransparent) continue;

      const uint8_t gray = rgb565ToGray(draw->pPalette[paletteIndex]);
      const uint8_t pixel = ctx->config->useDithering
                                ? applyBayerDither4Level(gray, outX, outY)
                                : std::min<uint8_t>(3, gray / 85);

      ctx->writer.writePixel(outX, pixel);
      ctx->drewPixels = true;
    }
  }
}

struct GifCloseGuard {
  AnimatedGIF* gif;
  ~GifCloseGuard() {
    if (gif) gif->close();
  }
};

}  // namespace

bool GifToFramebufferConverter::supportsFormat(const std::string& extension) {
  return extension == ".gif";
}

bool GifToFramebufferConverter::getDimensionsStatic(const std::string& imagePath,
                                                     ImageDimensions& out) {
  HalFile file;
  if (!Storage.openFileForRead("GIF", imagePath, file)) return false;

  uint8_t header[10];
  const int bytesRead = file.read(header, sizeof(header));
  file.close();

  if (bytesRead != static_cast<int>(sizeof(header))) return false;
  if (header[0] != 'G' || header[1] != 'I' || header[2] != 'F' ||
      !((header[3] == '8' && header[4] == '7' && header[5] == 'a') ||
        (header[3] == '8' && header[4] == '9' && header[5] == 'a'))) {
    return false;
  }

  const uint32_t width = static_cast<uint32_t>(header[6]) |
                         (static_cast<uint32_t>(header[7]) << 8);
  const uint32_t height = static_cast<uint32_t>(header[8]) |
                          (static_cast<uint32_t>(header[9]) << 8);

  return validateAndStoreDimensions(width, height, out, "GIF");
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath,
                                                     GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  LOG_DBG("GIF", "Decoding first GIF frame: %s", imagePath.c_str());

  const size_t decoderSize = sizeof(AnimatedGIF);
  constexpr size_t GIF_HEAP_HEADROOM = 16 * 1024;
  const size_t minFreeHeap = decoderSize + GIF_HEAP_HEADROOM;
  const size_t freeHeap = ESP.getFreeHeap();

  if (freeHeap < minFreeHeap) {
    LOG_ERR("GIF", "Not enough heap for GIF decoder (%u free, need %u; decoder %u)",
            static_cast<unsigned>(freeHeap),
            static_cast<unsigned>(minFreeHeap),
            static_cast<unsigned>(decoderSize));
    return false;
  }

  std::unique_ptr<AnimatedGIF> gif(new (std::nothrow) AnimatedGIF());
  if (!gif) {
    LOG_ERR("GIF", "Failed to allocate GIF decoder");
    return false;
  }

  GifCloseGuard cleanup{gif.get()};

  GifContext ctx;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();
  ctx.writer.init(renderer);

  gif->begin(GIF_PALETTE_RGB565_LE);
  gif->setDrawType(GIF_DRAW_RAW);

  if (!gif->open(imagePath.c_str(), gifOpen, gifClose, gifRead, gifSeek,
                  gifDrawCallback)) {
    LOG_ERR("GIF", "Failed to open GIF (err=%d): %s",
            gif->getLastError(), imagePath.c_str());
    return false;
  }

  ImageDimensions dimensions;
  if (!validateAndStoreDimensions(gif->getCanvasWidth(), gif->getCanvasHeight(),
                                 dimensions, "GIF")) {
    return false;
  }

  ctx.sourceWidth = dimensions.width;
  ctx.sourceHeight = dimensions.height;

  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    ctx.destWidth = config.maxWidth;
    ctx.destHeight = config.maxHeight;
  } else {
    float scale = 1.0f;
    if (config.maxWidth > 0 && ctx.sourceWidth > config.maxWidth) {
      scale = std::min(scale, static_cast<float>(config.maxWidth) / ctx.sourceWidth);
    }
    if (config.maxHeight > 0 && ctx.sourceHeight > config.maxHeight) {
      scale = std::min(scale, static_cast<float>(config.maxHeight) / ctx.sourceHeight);
    }
    ctx.destWidth = std::max(1, static_cast<int>(ctx.sourceWidth * scale + 0.5f));
    ctx.destHeight = std::max(1, static_cast<int>(ctx.sourceHeight * scale + 0.5f));
  }

  if (ctx.destWidth <= 0 || ctx.destHeight <= 0) return false;

  LOG_DBG("GIF", "GIF %dx%d -> %dx%d; rendering first frame only",
          ctx.sourceWidth, ctx.sourceHeight, ctx.destWidth, ctx.destHeight);

  ctx.lastYieldMs = millis();

  // A single playFrame() call deliberately avoids animation playback.
  gif->playFrame(false, nullptr, &ctx);

  if (gif->getLastError() != GIF_SUCCESS) {
    LOG_ERR("GIF", "GIF first-frame decode failed (err=%d)", gif->getLastError());
    return false;
  }

  if (!ctx.drewPixels) {
    LOG_ERR("GIF", "GIF first frame produced no visible pixels: %s", imagePath.c_str());
    return false;
  }

  return true;
}
