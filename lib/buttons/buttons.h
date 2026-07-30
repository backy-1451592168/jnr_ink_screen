// 三键扫描：模式 / 执行 / 系统
#pragma once

#include <Arduino.h>

namespace buttons {

enum class Event : uint8_t {
  None = 0,
  ModeDouble,     // 双击：绿↔青切换（防误触；单击无效）
  ActionShort,    // 单击立刻上报（未绑定 sync；已绑定忽略）
  ActionDouble,   // 双击：本地重刷缓存（防误触）
  ActionLong,
  ActionCancel,   // 忙时再按执行键（下载阶段可取消）
  SystemReconfig, // 长按 3s
  SystemFactory,  // 超长按 8s
};

void begin();
// 主循环调用；busy=true 时执行键短按变为取消
Event poll(bool busy);

// 系统键按住阶段（供主循环灯提示；不产生事件）：
// 0=未按，1=按住未满 3s，2=已满 3s（松手即重配），3=已满 6s（即将出厂）
uint8_t systemHoldLevel();

}  // namespace buttons
