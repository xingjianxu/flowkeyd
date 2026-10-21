// 托盘图标上的数字徽标：**画哪几个数字**、**画多大字号**（纯逻辑，可单测）。
//
// 图标本身（圆角渐变底 + 红色粗体数字）画在 `app/app_icon.cpp`，用的是 QtGui；
// 这里只决定**画什么字**、**每一位数字怎么压缩**，所以能在没有桌面会话时被 Qt Test
// 直接验证（与 `core/log_tail`、`core/version` 同一个理由）。
//
// 它服务于一个很小的目标：托盘通知区域里一眼看出现在是第几号虚拟桌面
// （项目所有者 2026-09 要求）与第几个工作空间（2026-10 要求：两个号各占一个角 ——
// 左上工作空间号、右下虚拟桌面号）。见 AGENTS.md 第 2 节第 20 条。
#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

#include <optional>

namespace flowkeyd::core {

/// 托盘徽标上显示哪几个数字（配置项 `settings.tray_badge`）。
///
/// * `Both`（默认）：两个号各占一个角 —— 左上工作空间号（绿底）、右下虚拟桌面号
///   （蓝底），中间一条 `/` 方向的白线；
/// * `Desktop`：只显示虚拟桌面号，蓝底居中（本特性出现之前的样子）；
/// * `Workspace`：只显示工作空间号，绿底居中。
///
/// 工作空间没有被启用 / 查不出来时（`TrayBadge::workspace == 0`）`Both` 只剩虚拟
/// 桌面号（不画对角线），`Workspace` 则无字可画（调用方退回应用图标）。
enum class TrayBadgeMode { Both, Desktop, Workspace };

QString trayBadgeModeName(TrayBadgeMode mode);
std::optional<TrayBadgeMode> trayBadgeModeFromName(const QString &name);
QStringList trayBadgeModeNames();

/// 托盘徽标的全部输入：动作工作线程每 500 ms 算一份，整份交给 GUI 线程去换图标。
///
/// 两个号都是 **1 起**；`0` 一律表示「不知道 / 用不上」（读不到当前桌面、工作空间
/// 功能关着、光标在哪块屏都问不出来……）。`*Count` 只用于悬停提示。
struct TrayBadge
{
    TrayBadgeMode mode = TrayBadgeMode::Both;
    int workspace = 0;
    int workspaceCount = 0;
    int desktop = 0;
    int desktopCount = 0;

    friend bool operator==(const TrayBadge &, const TrayBadge &) = default;
};

/// 徽标文字的组成部分（空串 = 这一位不画）。
///
/// 分开给是为了让画图那一层知道要不要画成**两个角**：两部分都有时左上角是工作空间
/// 号（绿底）、右下角是虚拟桌面号（蓝底），中间一条 `/` 方向的白线；只有一部分时
/// 整块用它自己的颜色、那个号画在正中间。
struct TrayBadgeParts
{
    QString workspace;
    QString desktop;

    bool isEmpty() const { return workspace.isEmpty() && desktop.isEmpty(); }
    /// 两部分都有 = 画成左上 / 右下两个角，中间一条 `/` 方向的对角线。
    bool isSplit() const { return !workspace.isEmpty() && !desktop.isEmpty(); }
};

TrayBadgeParts trayBadgeParts(const TrayBadge &badge);

/// 徽标上要写的完整文字：`1/2`、`3`、`9+`……空串表示**没有可画的**（调用方退回
/// 应用图标，而不是画个 `0`）。
///
/// 图标上是分开画的（`1` 与 `2` 各占一个角），这个拼好的字符串是它的**文字形式**：
/// 日志、测试、以及任何需要把徽标说成一句话的地方用它。
QString trayBadgeText(const TrayBadge &badge);

/// 徽标上要画的字符。
///
/// * `number` 在 `1..9` 时就是它本身（`3`）；
/// * `number >= 10` 时是 `9+`（项目所有者 2026-09 拍板：两位数在 16 逻辑像素的
///   托盘图标上已经挤成一团，宁可表达“还有更多”——所以 10 号桌面之后看不出
///   具体是第几张，这是刻意的取舍；工作空间号沿用同一条规则）；
/// * `number <= 0` 返回**空串**，表示“读不到当前桌面 / 没有工作空间号”。调用方
///   据此退回应用图标，而不是画个 `0`。
QString desktopBadgeText(int number);

/// 在这个图标尺寸（逻辑像素边长）上画 `characters` 个字符所需的字体像素大小。
///
/// 一位数占满画面的大半，位数越多字号收得越狠；结果不小于 6 像素，免得在高 DPI
/// 的小尺寸上糊成一团。`characters <= 0` 时返回 0（没有字要画）。
///
/// **两个号分画在左上 / 右下时不用这个函数**（那样每个号只占一个角，能用大得多的
/// 字），用 `desktopBadgeCornerFontPixels()`。
int desktopBadgeFontPixels(int iconSize, int characters);

/// 徽标上「左上工作空间号 + 右下虚拟桌面号」那种构图里**每个号**的字号。
///
/// 一个号只占一个角，所以比把 `1/2` 挤成三个字符时大得多（项目所有者 2026-10
/// 要求：两个号都显示时原来那个字号看不清）；`9+` 这种两位数再降一档。结果不小于
/// 6 像素，`characters <= 0` 时返回 0。
int desktopBadgeCornerFontPixels(int iconSize, int characters);

} // namespace flowkeyd::core

Q_DECLARE_METATYPE(flowkeyd::core::TrayBadge)
