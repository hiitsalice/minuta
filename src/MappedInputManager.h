#pragma once

#include <HalGPIO.h>

class GfxRenderer;
namespace freeink {
namespace ui {
enum class ScreenEdge : uint8_t;
}
}  // namespace freeink

class MappedInputManager {
 public:
  enum class Button {
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    NavNext,
    NavPrevious,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  enum class SwipeDir { None, Left, Right, Up, Down };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  void update() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  // One-shot threshold event while the button is down; consumes its release.
  bool wasLongPressed(Button button, unsigned long thresholdMs) const;
  bool consumeSuppressedRelease() const;
  void suppressRelease(Button button) const;
  bool isPressed(Button button) const;
  // The ordinary XTEINK X4 has no touchscreen.
  constexpr bool hasTouch() const { return false; }
  constexpr bool wasScreenTapped(int&, int&) const { return false; }
  constexpr bool wasScreenTouchDown(int&, int&) const { return false; }
  constexpr bool wasScreenLongPress(int&, int&) const { return false; }
  constexpr bool isScreenTouchHeld(int&, int&) const { return false; }
  constexpr bool wasScreenTouchReleased() const { return false; }
  constexpr bool wasTapInRect(int, int, int, int) const { return false; }

  enum class RowTouch : uint8_t { None, Down, Tap };
  constexpr RowTouch rowTouch(int&, int, int, int, int = 0, int = INT32_MAX, int = 0) const {
    return RowTouch::None;
  }
  constexpr RowTouch colTouch(int&, int, int, int, int, int, int = 0) const {
    return RowTouch::None;
  }

  constexpr SwipeDir wasSwipe() const { return SwipeDir::None; }
  constexpr bool wasBackGesture() const { return false; }
  constexpr bool wasHomeGesture() const { return false; }
  constexpr bool wasHomeKeyHold() const { return false; }
  constexpr bool wasMenuGesture() const { return false; }
  constexpr bool wasReaderMenuSwipeUp() const { return false; }
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  const GfxRenderer& getRenderer() const { return renderer; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Maps four screen-direction labels onto the two physical front-button roles
  // using the same live-orientation transform as ScreenLeft/Right/Up/Down.
  Labels mapDirectionalLabels(const char* back, const char* confirm, const char* left, const char* right,
                              const char* up, const char* down) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;
  // Returns the raw front button index that was released this frame (or -1 if none).
  int getReleasedFrontButton() const;

  // True when the control axis is flipped relative to the physical buttons: the user opted into
  // orientation-following front buttons AND the screen is *currently rendered* rotated (INVERTED /
  // LANDSCAPE_CCW). Keyed on the live renderer orientation rather than the persisted reader setting,
  // so portrait UI (home, settings) never swaps while the reader and its menus do.
  [[nodiscard]] bool isNavDirectionSwapped() const;

 private:
  HalGPIO& gpio;
  // Logical-to-physical button mapping depends on what the user is actually looking at: when the
  // screen is rendered rotated, the directional buttons must flip to match. The renderer is the only
  // authority on the *live* orientation (the reader rotates it and restores portrait on exit), so we
  // read it here instead of CrossPointSettings.orientation, which is just the persisted reader
  // preference and stays "rotated" even while portrait UI like home/settings is on screen.
  const GfxRenderer& renderer;

  Button mapScreenDirection(Button button) const;
  Labels mapFrontLabels(const char* back, const char* confirm, const char* left, const char* right) const;
  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  void suppressNextRelease(Button button) const;

  mutable uint16_t longPressFiredButtons = 0;
  mutable uint16_t suppressedReleaseButtons = 0;
};
