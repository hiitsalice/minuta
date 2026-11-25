#include "QrUtils.h"

#include <Utf8.h>
#include <qrcode.h>

#include <algorithm>
#include <memory>

#include "Logging.h"

#ifndef SIMULATOR
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#endif

namespace {

constexpr int8_t QR_NO_STACK = -100;

#ifndef SIMULATOR
struct QrJob {
  QRCode* qrcode;
  uint8_t* modules;
  int version;
  const char* payload;
  int8_t result;
  std::atomic<bool> done;
};

void qrBuildTask(void* arg) {
  auto* job = static_cast<QrJob*>(arg);
  job->result = qrcode_initText(job->qrcode, job->modules, job->version, ECC_LOW, job->payload);
  job->done.store(true);  // last access to the job: the caller may free it right after this
  vTaskDelete(nullptr);
}
#endif

// The QR library keeps several large work buffers on the stack (about 7 KB at version 30 and 11 KB at version 40),
// which does not fit in the 8 KB render task. Big codes are therefore built on a short-lived task with its own stack.
int8_t buildQr(QRCode* qrcode, uint8_t* modules, const int version, const char* payload) {
#ifndef SIMULATOR
  if (version >= 30) {
    QrJob job{qrcode, modules, version, payload, QR_NO_STACK, {false}};
    const uint32_t stackBytes = version >= 40 ? 16384 : 12288;
    if (xTaskCreate(qrBuildTask, "QrBuild", stackBytes, &job, 1, nullptr) != pdPASS) {
      LOG_ERR("QR", "Not enough free memory for the QR build stack (%u bytes)", static_cast<unsigned>(stackBytes));
      return QR_NO_STACK;
    }
    while (!job.done.load()) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    return job.result;
  }
#endif
  return qrcode_initText(qrcode, modules, version, ECC_LOW, payload);
}

}  // namespace

void QrUtils::drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload) {
  // Dynamically calculate the QR code version based on text length
  // Version 4 holds ~114 bytes, Version 10 ~395, Version 20 ~1066, up to 40
  // qrcode.h max version is 40.
  // Formula: approx version = size / 26 + 1 (very rough estimate, better to find best fit)
  size_t len = textPayload.length();

  // Truncate to max QR capacity at a UTF-8 safe boundary to avoid splitting multi-byte sequences
  static constexpr size_t MAX_QR_CAPACITY = 2953;  // Version 40, ECC_LOW, byte mode
  std::string truncated;
  const char* payload = textPayload.c_str();
  if (len > MAX_QR_CAPACITY) {
    len = utf8SafeTruncateBuffer(textPayload.c_str(), static_cast<int>(MAX_QR_CAPACITY));
    truncated = textPayload.substr(0, len);
    payload = truncated.c_str();
  }

  int version = 4;
  if (len > 114) version = 10;
  if (len > 395) version = 20;
  if (len > 1066) version = 30;
  if (len > 2110) version = 40;

  // Make sure we have a large enough buffer on the heap to avoid blowing the stack
  uint32_t bufferSize = qrcode_getBufferSize(version);
  auto qrcodeBytes = std::make_unique<uint8_t[]>(bufferSize);

  QRCode qrcode;
  // Initialize the QR code. We use ECC_LOW for max capacity.
  int8_t res = buildQr(&qrcode, qrcodeBytes.get(), version, payload);
  if (res == QR_NO_STACK) {
    // Not enough free memory for the big work stack: fall back to a smaller code (version 20, ~850 bytes of text).
    static constexpr size_t SAFE_CAPACITY = 858;
    version = 20;
    const std::string safe(payload, utf8SafeTruncateBuffer(payload, static_cast<int>(SAFE_CAPACITY)));
    res = qrcode_initText(&qrcode, qrcodeBytes.get(), version, ECC_LOW, safe.c_str());
  }

  if (res == 0) {
    // Determine the optimal pixel size.
    const int maxDim = std::min(bounds.width, bounds.height);

    int px = maxDim / qrcode.size;
    if (px < 1) px = 1;

    // Calculate centering X and Y
    const int qrDisplaySize = qrcode.size * px;
    const int xOff = bounds.x + (bounds.width - qrDisplaySize) / 2;
    const int yOff = bounds.y + (bounds.height - qrDisplaySize) / 2;

    // Draw the QR Code
    for (uint8_t cy = 0; cy < qrcode.size; cy++) {
      for (uint8_t cx = 0; cx < qrcode.size; cx++) {
        if (qrcode_getModule(&qrcode, cx, cy)) {
          renderer.fillRect(xOff + px * cx, yOff + px * cy, px, px, true);
        }
      }
    }
  } else {
    // If it fails (e.g. text too large), log an error
    LOG_ERR("QR", "Text too large for QR Code version %d", version);
  }
}
