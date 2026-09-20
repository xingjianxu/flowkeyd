// 按键注入与物理按键状态。
//
// 注入有两条后端：`user32!SendInput`（总是可用）与
// `win32u!NtUserSendInput`（运行时解析成功才用）。
//
// flowkeyd 合成的每个事件都在 `dwExtraInfo` 里带上
// [`kSyntheticTag`]（`"FLOW"`）；钩子正是靠它识别并忽略自己产生的输入，
// 包括 `SendInput` 还在执行时触发的重入回调（不变量 2）。
#pragma once

#include "core/keys.h"
#include "platform/win/ffi.h"

#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>
#include <vector>

namespace flowkeyd::platform::win {

/// `"FLOW"` —— 标记由 flowkeyd 合成的输入。
inline constexpr ULONG_PTR kSyntheticTag = 0x464C4F57;

enum class BackendPreference { Auto, User32, NtUser };

/// 解析 `auto` / `user32` / `ntuser`（`documented`/`win32u`/`undocumented` 作别名）。
std::optional<BackendPreference> parseBackendPreference(const QString &name);

/// 记录配置的偏好；在启动时、任何发送之前调用一次。
void configureInput(BackendPreference preference);

/// 实际生效的后端名（`user32!SendInput` / `win32u!NtUserSendInput`）。
QString inputBackendName();

/// `sizeof(INPUT)`；`--list`/自检与单测用。
int inputSize();

/// 构造一个按下或松开 `vk` 的键盘 `INPUT`（内部会翻译小键盘 Enter 的伪码）。
INPUT keyInput(core::Vk vk, bool down);

/// 构造一个把一个 UTF-16 码元作为文本注入的键盘 `INPUT`。
INPUT unicodeInput(char16_t unit, bool down);

/// 批量注入一组输入事件，优先使用配置的后端。
bool sendInputs(const std::vector<INPUT> &inputs, QString *error = nullptr);

/// 执行一段编译好的发送脚本。
///
/// 从低级钩子回调调用时 `allowSleep` 必须为 false：会睡觉的钩子就是死掉的钩子
/// （Windows 会静默移除超过 `LowLevelHooksTimeout` 的钩子）。
bool sendOps(const std::vector<core::SendOp> &ops, bool allowSleep, QString *error = nullptr);
bool sendOps(const QVector<core::SendOp> &ops, bool allowSleep, QString *error = nullptr);

/// 按下并松开一个按键。
bool tapKey(core::Vk vk, QString *error = nullptr);

/// 合成一次 Ctrl+C，使前台应用把选中内容放进剪贴板，然后等待 `waitMs`。
///
/// `{selection}` 模板用它。与 `send` 一样会先松开用户按住的修饰键
/// （否则会变成 Ctrl+Alt+C），并在松开 Win/Alt 时做菜单遮断（不变量 10）。
bool copySelection(std::uint64_t waitMs, QString *error = nullptr);

/// 物理按键状态（优先走未公开的 `NtUserGetAsyncKeyState`）。
bool isKeyDown(core::Vk vk);

/// CapsLock 当前是否处于锁定状态。
bool capsLockOn();

/// 用户此刻物理按住的修饰键（按 Ctrl/Alt/Shift/Win 顺序）。
core::Modifiers modifiersDown();

/// 纯函数：给定按住的修饰键，返回松开它们所需的注入序列。
///
/// 松开 Win/Alt 之前会插一次未分配按键，遮断“单独按下”的判断（不变量 10）。
/// 独立出来是为了能单测顺序（不会真的注入）。
std::vector<core::SendOp> modifierReleasePlan(const std::vector<core::Vk> &held);

/// 纯函数：`restore` 的注入序列（最新松开的先按，与 AutoHotkey 一致）。
std::vector<core::SendOp> modifierRestorePlan(const std::vector<core::Vk> &released);

/// 在一个作用域内松开物理按住的修饰键，随后再按回去。
///
/// 这正是让 `Ctrl+Alt+T` -> `send:^{c}` 发出单纯的 Ctrl+C 而不是
/// Ctrl+Alt+Ctrl+C 的原因。对 Win/Alt 而言，这一次松开本身就可能制造出
/// 引擎的 `maskMenuKeyUp` 想防止的场景，因此每次松开 Win/Alt 之前先注入
/// 一次未分配按键（不变量 10）。
class ModifierGuard
{
public:
    ModifierGuard() = default;
    ~ModifierGuard();

    ModifierGuard(const ModifierGuard &) = delete;
    ModifierGuard &operator=(const ModifierGuard &) = delete;
    ModifierGuard(ModifierGuard &&other) noexcept;
    ModifierGuard &operator=(ModifierGuard &&other) noexcept;

    /// 松开用户当前按住的每一个修饰键。
    static ModifierGuard release();

    /// 什么都不做（动作关闭了修饰键释放时使用）。
    static ModifierGuard none();

    /// 重新按下这些修饰键，最新松开的先按（与 AutoHotkey 顺序一致）。
    void restore();

    bool isActive() const { return m_active; }
    const std::vector<core::Vk> &releasedKeys() const { return m_released; }

private:
    void restoreInner();

    std::vector<core::Vk> m_released;
    bool m_active = false;
};

} // namespace flowkeyd::platform::win
