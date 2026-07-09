// 三键扫描：模式 / 执行 / 系统
#pragma once

#include <Arduino.h>

namespace buttons {

enum class Event : uint8_t {
  None = 0,
  ModeShort,
  ActionShort,
  ActionLong,
  ActionCancel,   // 忙时再按执行键
  SystemReconfig, // 长按 3s
  SystemFactory,  // 超长按 8s
};

void begin();
// 主循环调用；busy=true 时执行键短按变为取消
Event poll(bool busy);

}  // namespace buttons
