// 小程序推送模式：HTTP sync → 分包下载 → CRC → 刷屏 → ack
#pragma once

#include <Arduino.h>

namespace ink_sync {

enum class Result : uint8_t {
  OkNoUpdate = 0,  // sync_ok
  OkUpdated,       // 已刷屏并 ack
  Cancelled,
  Failed,
};

// 置位后分包循环会中断（执行键取消）
void requestCancel();
void clearCancel();
bool isBusy();

// 执行一次完整 sync（主 apiBase，失败再备）
Result runOnce();

// 用本地 /last.bin 重刷（受最小刷屏间隔约束）
Result refreshLocal();

}  // namespace ink_sync
