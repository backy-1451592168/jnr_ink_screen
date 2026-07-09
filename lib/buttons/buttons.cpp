#include "buttons.h"

#ifndef PIN_BTN_MODE
#define PIN_BTN_MODE 4
#endif
#ifndef PIN_BTN_ACTION
#define PIN_BTN_ACTION 5
#endif
#ifndef PIN_BTN_SYSTEM
#define PIN_BTN_SYSTEM 6
#endif

namespace buttons {

namespace {

constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kShortMaxMs = 800;
constexpr uint32_t kLongMs = 800;
constexpr uint32_t kReconfigMs = 3000;
constexpr uint32_t kFactoryMs = 8000;

struct Btn {
  uint8_t pin;
  bool stableLow;
  bool lastRaw;
  uint32_t lastChange;
  uint32_t pressStart;
  bool pressed;
  bool longFired;  // 系统键已触发过某档
  uint8_t stage;   // 0=无 1=已报3s 2=已报8s
};

Btn g_mode{PIN_BTN_MODE, false, true, 0, 0, false, false, 0};
Btn g_action{PIN_BTN_ACTION, false, true, 0, 0, false, false, 0};
Btn g_system{PIN_BTN_SYSTEM, false, true, 0, 0, false, false, 0};

void setupPin(Btn& b) {
  pinMode(b.pin, INPUT_PULLUP);
  b.lastRaw = digitalRead(b.pin);
  b.stableLow = (b.lastRaw == LOW);
}

// 返回释放时的按住时长；按下中返回 0 且不产生事件（系统键边沿在 poll 里处理）
uint32_t updateRelease(Btn& b) {
  bool raw = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChange = now;
  }
  if (now - b.lastChange < kDebounceMs) return 0;

  if (raw && !b.pressed) {
    b.pressed = true;
    b.pressStart = now;
    b.longFired = false;
    b.stage = 0;
    return 0;
  }
  if (!raw && b.pressed) {
    b.pressed = false;
    return now - b.pressStart;
  }
  return 0;
}

}  // namespace

void begin() {
  setupPin(g_mode);
  setupPin(g_action);
  setupPin(g_system);
}

Event poll(bool busy) {
  // 系统键：松手时按时长判定（避免 3s 重配抢在 8s 出厂之前重启）
  {
    bool raw = digitalRead(g_system.pin) == LOW;
    uint32_t now = millis();
    if (raw != g_system.lastRaw) {
      g_system.lastRaw = raw;
      g_system.lastChange = now;
    }
    if (now - g_system.lastChange >= kDebounceMs) {
      if (raw && !g_system.pressed) {
        g_system.pressed = true;
        g_system.pressStart = now;
      } else if (!raw && g_system.pressed) {
        uint32_t held = now - g_system.pressStart;
        g_system.pressed = false;
        if (held >= kFactoryMs) return Event::SystemFactory;
        if (held >= kReconfigMs) return Event::SystemReconfig;
      }
    }
  }

  uint32_t modeHeld = updateRelease(g_mode);
  if (modeHeld > 0 && modeHeld < kShortMaxMs) {
    return Event::ModeShort;
  }

  // 忙时：按下执行键即取消（边沿）
  if (busy) {
    bool raw = digitalRead(g_action.pin) == LOW;
    uint32_t now = millis();
    if (raw != g_action.lastRaw) {
      g_action.lastRaw = raw;
      g_action.lastChange = now;
    }
    if (now - g_action.lastChange >= kDebounceMs) {
      if (raw && !g_action.pressed) {
        g_action.pressed = true;
        return Event::ActionCancel;
      }
      if (!raw) g_action.pressed = false;
    }
    return Event::None;
  }

  uint32_t actHeld = updateRelease(g_action);
  if (actHeld > 0) {
    if (actHeld >= kLongMs) return Event::ActionLong;
    return Event::ActionShort;
  }

  return Event::None;
}

}  // namespace buttons
