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
constexpr uint32_t kActionLongMs = 2000;  // 执行键长按：到点即触发，不等松手
constexpr uint32_t kDoubleClickMs = 400;  // 两次短按间隔上限
constexpr uint32_t kReconfigMs = 3000;
constexpr uint32_t kFactoryMs = 8000;

// 短按后等窗口：窗口内再短按报双击；模式键超时丢弃（单击无效），执行键超时报单击
bool g_modeClickPending = false;
uint32_t g_modeClickAt = 0;
bool g_actionClickPending = false;
uint32_t g_actionClickAt = 0;

struct Btn {
  uint8_t pin;
  bool stableLow;
  bool lastRaw;
  uint32_t lastChange;
  uint32_t pressStart;
  bool pressed;
  bool longFired;  // 执行键长按已触发（松手忽略）
  uint8_t stage;   // 保留字段（未用）
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

  // 模式键：仅双击切换；单击超时丢弃（防误触）
  {
    uint32_t modeHeld = updateRelease(g_mode);
    if (modeHeld > 0 && modeHeld < kShortMaxMs) {
      uint32_t now = millis();
      if (g_modeClickPending && (now - g_modeClickAt) <= kDoubleClickMs) {
        g_modeClickPending = false;
        return Event::ModeDouble;
      }
      g_modeClickPending = true;
      g_modeClickAt = now;
    }
  }
  if (g_modeClickPending && (millis() - g_modeClickAt) > kDoubleClickMs) {
    g_modeClickPending = false;
  }

  // 忙时：按下执行键即取消（边沿）；丢弃未决单击
  if (busy) {
    g_actionClickPending = false;
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

  // 执行键：长按 2s 到点即触发（不等松手）；短按/双击仍在松手判定
  {
    bool raw = digitalRead(g_action.pin) == LOW;
    uint32_t now = millis();
    if (raw != g_action.lastRaw) {
      g_action.lastRaw = raw;
      g_action.lastChange = now;
    }
    if (now - g_action.lastChange >= kDebounceMs) {
      if (raw && !g_action.pressed) {
        g_action.pressed = true;
        g_action.pressStart = now;
        g_action.longFired = false;
      } else if (raw && g_action.pressed) {
        if (!g_action.longFired && (now - g_action.pressStart) >= kActionLongMs) {
          g_action.longFired = true;
          g_actionClickPending = false;
          return Event::ActionLong;
        }
      } else if (!raw && g_action.pressed) {
        bool wasLong = g_action.longFired;
        g_action.pressed = false;
        g_action.longFired = false;
        if (!wasLong) {
          // 短按：窗口内第二次 → 双击；否则挂起等超时再报单击
          if (g_actionClickPending && (now - g_actionClickAt) <= kDoubleClickMs) {
            g_actionClickPending = false;
            return Event::ActionDouble;
          }
          g_actionClickPending = true;
          g_actionClickAt = now;
        }
      }
    }
  }

  if (g_actionClickPending && (millis() - g_actionClickAt) > kDoubleClickMs) {
    g_actionClickPending = false;
    return Event::ActionShort;
  }

  return Event::None;
}

}  // namespace buttons
