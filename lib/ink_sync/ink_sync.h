// 小程序推送模式：HTTP sync → 整帧下载 → CRC → 刷屏 → ack
#pragma once

#include <Arduino.h>

namespace ink_sync {

enum class Result : uint8_t {
  OkNoUpdate = 0,  // sync_ok
  OkUpdated,       // 已刷屏并 ack
  Cancelled,
  Failed,
};

// 置位后整帧下载循环会中断（执行键取消）
void requestCancel();
void clearCancel();
bool isBusy();

// 下载/防抖期间周期性回调（主程序用于紫灯呼吸）；传 nullptr 清除
using ActivityHook = void (*)();
void setActivityHook(ActivityHook fn);

// 执行一次完整 sync（主 apiBase，失败再备）
Result runOnce();

// 用本地 /last.bin 重刷；force=true 时忽略最小刷屏间隔（开机恢复用）
Result refreshLocal(bool force = false);

}  // namespace ink_sync
