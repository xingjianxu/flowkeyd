// 通过 Core Audio API（`IAudioEndpointVolume`）控制系统音量与静音。
//
// **本模块风险最高**：COM 接口是手写声明的，下面的 vtable 布局按 Windows SDK
// 头文件的声明顺序排列，而 vtable 布局写错不是返回错误码，而是崩溃。
// 每次调用都检查 HRESULT，因此布局错会大声失败而不是悄悄破坏进程。
// 布局照抄 `../oskeyd/src/win/audio.rs`（那边的 vtable 已经过实测）。
//
// `CoCreateInstance(CLSID_MMDeviceEnumerator)` 避免了静态链接 `mmdevapi.lib`。
//
// **单元模型**：Core Audio 要 **MTA**，而虚拟桌面（shell 接口）要 STA。
// 所以音频在动作工作线程上自己进 MTA 单元，绝不在 Qt 主线程上初始化
// （主线程被 Qt 弄成 STA 给 OLE/拖放用，见 AGENTS.md 第 7 节第 13 条）。
#pragma once

#include "core/action.h"

#include <QString>

#include <cstdint>
#include <optional>

namespace flowkeyd::platform::win::audio {

/// 纯计算：当前标量音量（0..1）在给定 op 下的目标值。
///
/// `set` 用 `level / 100`（钳到 0..100），`up`/`down` 用 `step`（默认 2，
/// 最小 1）`/ 100` 并在 [0, 1] 上钳位；其余 op 原样返回 `current`。
/// 独立出来是为了能单测步进与边界，不必真的碰音频设备。
float nextVolumeScalar(float current,
                       core::VolumeOp op,
                       std::optional<std::uint8_t> level,
                       std::optional<std::uint8_t> step);

/// 应用一个 `volume` 动作。成功时 `detail` 是 `"42%"` / `"muted"` 等。
bool apply(core::VolumeOp op,
           std::optional<std::uint8_t> level,
           std::optional<std::uint8_t> step,
           QString *detail,
           QString *error);

/// 以百分比读取当前主音量（`--selftest` 与交互式单测用）。
bool getPercent(int *out, QString *error = nullptr);

/// 默认渲染端点是否静音？
bool isMuted(bool *out, QString *error = nullptr);

} // namespace flowkeyd::platform::win::audio
