// 弹窗（`menu` / `help`）共用的几何类型与常量。
//
// 这里刻意只放**逻辑像素**：Qt 6 在 Windows 上已经是 Per-Monitor DPI Aware V2，
// 所以 QML 里的坐标天然就是缩放过的，不需要再手动按 `dpi / 96` 缩放；
// 本文件里的数字都是 96 DPI 下的逻辑取值（`menu_model` 与 `help_model`
// 里的 `Metrics` 用的就是这些常量）。
#pragma once

#include <QRect>
#include <QSize>

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

/// 弹窗窗口的**物理**尺寸是不是已经过期（不再等于「逻辑尺寸 × 屏幕缩放」）。
///
/// 为什么需要这个判断：五个弹窗窗口在启动时预热一次（`PopupHost::preload()`），
/// 之后就一直留着复用；而它们平时是**隐藏的、还摆在屏幕之外**的。显示器缩放变了
/// 之后（用户改了缩放、显示器换到另一块 DPI 上、显示器重新接入……），这种窗口
/// 收不到 `WM_DPICHANGED`，Qt 也就不会刷新它的缩放 —— 于是弹出时只有正确尺寸的
/// 一半。2026-10-05 真机实测：守护进程在 100% 缩放下启动、用户把系统缩放改成 200%
/// 之后，那五张卡片仍然停在「逻辑数字当作物理像素」的大小上（`apps` 卡片
/// 800×1244 物理像素，本该是 1600×1244），用户看到的启动器只有一半大。
/// 所以每次**弹出前**拿它跟「这块屏的缩放」对一遍，不一致就把窗口丢掉重建
/// （`PopupHost::discardPopupWindows()`）。
///
/// `physical` 是 `GetWindowRect` 给的物理像素尺寸；`slack` 用来容忍无边框窗口的
/// 边框 / DWM 阴影带来的几像素差别。尺寸为空（问不到窗口）时返回 false —— 宁可
/// 不重建，也不要因为读不到尺寸就把预热好的窗口白白丢掉。
bool popupPixelSizeIsStale(const QSize &physical, int logicalWidth, int logicalHeight,
                           qreal screenDpr, int slack = 8);

} // namespace flowkeyd::app
