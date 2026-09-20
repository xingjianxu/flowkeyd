// 单实例：按配置路径散列出一个互斥体名。
//
// 同名互斥体只允许一个持有者；提权重启时父进程可能还握着它，
// 所以启动方要重试几次（见 AGENTS.md 第 6 节的 `--elevated` 重试）。
#pragma once

#include "platform/win/ffi.h"

#include <QString>

#include <cstdint>
#include <optional>

namespace flowkeyd::platform::win {

/// FNV-1a 64 位（纯函数，可单测）。
std::uint64_t fnv1a64(const QByteArray &bytes);

/// `Local\flowkeyd-<配置路径散列>`。
///
/// 路径先做大小写归一（Windows 路径不区分大小写）与分隔符归一，
/// 这样 `C:\a\config.lua` 与 `c:/a/config.lua` 会命中同一个实例。
QString instanceKey(const QString &configPath);

/// 一个已持有的命名互斥体。
class SingleInstance
{
public:
    SingleInstance() = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance &) = delete;
    SingleInstance &operator=(const SingleInstance &) = delete;
    SingleInstance(SingleInstance &&other) noexcept;
    SingleInstance &operator=(SingleInstance &&other) noexcept;

    /// 尝试获得 `key`。失败时返回 `nullopt` 并填 `error`。
    /// 已经有别的实例持有时把 `*alreadyRunning` 置为 true 并照常返回一个对象
    /// （调用方据此拒绝启动，但互斥体的所有权仍然由我们拿着）。
    static std::optional<SingleInstance> acquire(const QString &key,
                                                 bool *alreadyRunning,
                                                 QString *error);

    bool alreadyRunning() const { return m_alreadyRunning; }
    HANDLE handle() const { return m_handle; }

private:
    HANDLE m_handle = nullptr;
    bool m_alreadyRunning = false;
};

} // namespace flowkeyd::platform::win
