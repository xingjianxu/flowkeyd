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

/// `Local\flowkeyd-<散列>-quit`：那个实例的「干净退出」请求事件。
///
/// 之所以另开一个事件而不是复用互斥体：互斥体只能表达「有人在跑」，
/// 没法从外面通知持有者退出（见 AGENTS.md 第 10 节）。
QString quitEventName(const QString &key);

/// 守护进程侧：创建退出请求事件（自动重置）。失败时返回 `nullptr` 并填 `error`。
///
/// 事件用**手工构造的安全描述符**创建：默认描述符的对象带着创建者的完整性级别
/// （提权的守护进程是 High），而 `SetEvent` 要的 `EVENT_MODIFY_STATE` 属于写权限，
/// 「no write up」会让非提权的 `--quit` 直接吃访问被拒。所以这里给对象打上
/// **Low** 完整性标签，任何级别的调用者都能置位它。
HANDLE createQuitEvent(const QString &key, QString *error);

/// 客户端侧：请 `key` 对应的实例退出。
///
/// 返回 false 表示调用本身失败（`error` 里是英文原因）；返回 true 时看 `*running`，
/// 它是 false 就说明没有实例在跑（不算错误）。
bool requestQuit(const QString &key, bool *running, QString *error);

/// 客户端侧：退出事件对象还在不在。守护进程握着它，所以「对象消失」就等于
/// 「那个实例已经退干净了」——`--quit` 用它来等进程真正收尾。
bool quitEventExists(const QString &key);

/// 客户端侧：`key` 对应的实例现在是不是已经在跑。
///
/// 只查命名互斥体在不在（`OpenMutexW(SYNCHRONIZE)`，**不获取所有权**），所以
/// 不会影响那个正在运行的实例。它刻意设计成可以在**提权之前**调用：
/// 双重启时先在 UAC 之前就告诉用户「已经有一个实例在运行」，用户确认后退出，
/// 免得白白弹一次提权。互斥体不存在（或打不开）时返回 false。
bool instanceRunning(const QString &key);

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
