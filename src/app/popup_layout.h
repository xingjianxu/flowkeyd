// 弹窗（`menu` / `help`）共用的几何类型与常量。
//
// 这里刻意只放**逻辑像素**：Qt 6 在 Windows 上已经是 Per-Monitor DPI Aware V2，
// 所以 QML 里的坐标天然就是缩放过的。oskeyd 那边要手动按 `dpi / 96` 缩放，
// flowkeyd 不需要；本文件里的数字就是 oskeyd 在 96 DPI 下的取值
// （见 `../oskeyd/src/win/menu.rs` 的 `Metrics` 与 `help.rs` 的同名结构）。
#pragma once

#include <QRect>

namespace flowkeyd::app {

/// 弹窗里的一个矩形（卡片坐标，逻辑像素）。
struct PopupRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(int px, int py) const
    {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    /// 直接给 QML 用：`Q_PROPERTY(QRect …)` 要求 getter 真的返回 `QRect`
    /// （moc 生成的代码会把返回值赋给 `QRect`），所以这里留一个隐式转换。
    operator QRect() const { return QRect(x, y, width, height); }

    friend bool operator==(const PopupRect &a, const PopupRect &b) = default;
};

/// 屏幕坐标里的一个点。
struct PopupPoint
{
    int x = 0;
    int y = 0;

    friend bool operator==(const PopupPoint &a, const PopupPoint &b) = default;
};

/// 弹窗的左上角：先在工作区里居中，再夹进屏幕范围。
///
/// 为什么必须夹一次：本机实测（225% 缩放）Qt 报出的 `availableGeometry()`
/// 比 `geometry()` 还宽（工作区从 x=108 开始、宽 485，而屏幕只有 533 宽），
/// 于是 500 逻辑像素宽的帮助卡片会被居中到屏幕外面去，右边被切掉一大块。
/// 卡片真的比屏幕还宽时贴左上角，至少让用户看得到左边那部分。
PopupPoint centrePopup(const PopupRect &work, const PopupRect &bounds, int width, int height);

} // namespace flowkeyd::app
