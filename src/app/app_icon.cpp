#include "app/app_icon.h"

#include "core/desktop_badge.h"

#include <algorithm>

#include <QColor>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace flowkeyd::app {

namespace {

/// 与 `applicationIcon()` 用的帧列表一致（见那里的说明）：每个尺寸**单独渲染**，
/// 而不是缩放一张大位图，所以 16 px 下的数字也是清晰的。
constexpr int kIconSizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};

/// 徽标用的字体：**最粗的那一档**（项目所有者 2026-10 要求：白小字压在渐变底上要
/// 够分量）。Windows 上的 `Segoe UI` 字体族里只有 Regular / Bold，Black 是**另一个
/// 字体族**（`Segoe UI Black`），所以先找它；机器上没有时退回 `Segoe UI` +
/// `QFont::Black`（Qt 会拿最接近的 Bold，不会掉到常规体）。
QFont badgeFont(int pixels)
{
    static const bool hasBlackFamily =
        QFontDatabase::families().contains(QStringLiteral("Segoe UI Black"));
    QFont font(hasBlackFamily ? QStringLiteral("Segoe UI Black") : QStringLiteral("Segoe UI"));
    if (!hasBlackFamily) {
        font.setWeight(QFont::Black);
    }
    font.setPixelSize(pixels);
    return font;
}

/// 把 `text` 按**字形墨迹**居中画到 `centre`（而不是按包含行距的方框：数字没有
/// 下伸部，按方框居中会明显偏高）。只有数字、`+` 与 `/`，不需要中文字形
/// （弹窗那套微软雅黑是另一件事）。
void drawBadgeText(QPainter &painter, const QString &text, const QPointF &centre, int pixels)
{
    if (text.isEmpty() || pixels <= 0) {
        return;
    }
    const QFont font = badgeFont(pixels);
    painter.setFont(font);
    // 数字是**红色**（项目所有者 2026-10 要求：比白字更跳）。用的是偏亮的正红
    // （`#ff3b30`）：深绿 / 蓝两种底都是暗色，红得再深一点就会糊进去。
    painter.setPen(QColor(0xff, 0x3b, 0x30));
    const QFontMetricsF metrics(font);
    const QRectF ink = metrics.boundingRect(text);
    painter.drawText(QPointF(centre.x() - (ink.left() + ink.width() / 2.0),
                             centre.y() - (ink.top() + ink.height() / 2.0)),
                     text);
}

/// 画一帧数字徽标：圆角渐变底 + **红色**粗体数字。
///
/// 配色取自仓库根目录的 `logo.svg`（蓝渐变 `#00d2ff` → `#3a7bd5`，圆角 `rx = 56/256`），
/// 项目所有者 2026-09 在“蓝底白字 / 深蓝底青字 / logo + 数字角标”三种画法里选了
/// 第一种：16 px 的托盘图标上也要一眼看得出是几号。数字在 2026-10 改成红色
/// （`#ff3b30`）—— 蓝、绿两种底都是暗色，红字比白字跳。
///
/// 工作空间号用**深绿色**同一套渐变（`#12a150` → `#075e35`，2026-10 要求：深一点
/// 红字才拉得开）。两种构图：
///
/// * **一个号**：整块渐变 + 数字居中（蓝 = 虚拟桌面、绿 = 工作空间）；
/// * **两个号**：底色按对角线分成左上绿 / 右下蓝，**左上角画工作空间号、右下角画
///   虚拟桌面号**，中间一条 `/` 方向的白线（左下 → 右上）—— 项目所有者 2026-10
///   说挤成一行 `1/2` 看不清，分开画之后每个号都能大三倍。
QPixmap renderDesktopBadge(int size, const core::TrayBadgeParts &parts)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient desktopGradient(0, 0, size, size);
    desktopGradient.setColorAt(0.0, QColor(0x00, 0xd2, 0xff));
    desktopGradient.setColorAt(1.0, QColor(0x3a, 0x7b, 0xd5));
    QLinearGradient workspaceGradient(0, 0, size, size);
    workspaceGradient.setColorAt(0.0, QColor(0x12, 0xa1, 0x50));
    workspaceGradient.setColorAt(1.0, QColor(0x07, 0x5e, 0x35));

    // 圆角通过裁剪路径实现：对角线两侧各填一条自己的渐变，边界笔直。
    QPainterPath shape;
    const qreal radius = size * 56.0 / 256.0;
    shape.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
    painter.setPen(Qt::NoPen);
    painter.setClipPath(shape);
    if (parts.isSplit()) {
        // 先用虚拟桌面的蓝铺满，再把**左上半个三角**盖成绿（三角顶点 = 左上角、
        // 右上角、左下角，正好是 `/` 线左上侧那一半），最后画那条 `/` 线。
        painter.fillRect(QRectF(0, 0, size, size), desktopGradient);
        QPainterPath topLeft;
        topLeft.moveTo(0, 0);
        topLeft.lineTo(size, 0);
        topLeft.lineTo(0, size);
        topLeft.closeSubpath();
        painter.fillPath(topLeft, workspaceGradient);
        painter.setPen(QPen(Qt::white, std::max(1.0, size / 20.0), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(0, size), QPointF(size, 0));
        painter.setPen(Qt::NoPen);
    } else if (!parts.workspace.isEmpty()) {
        painter.fillRect(QRectF(0, 0, size, size), workspaceGradient);
    } else {
        painter.fillRect(QRectF(0, 0, size, size), desktopGradient);
    }
    painter.setClipping(false);

    if (parts.isSplit()) {
        // 两个号各占一个角，字号比挤成 `1/2` 时大得多；`9+` 这种两位数再缩一档、
        // 并且**再往角里挪一点**，两样都是为了不碰到那条对角线。
        const auto cornerCentre = [size](const QString &text, bool topLeft) {
            const qreal offset = size * (text.size() >= 2 ? 0.23 : 0.27);
            return topLeft ? QPointF(offset, offset) : QPointF(size - offset, size - offset);
        };
        drawBadgeText(painter, parts.workspace, cornerCentre(parts.workspace, true),
                      core::desktopBadgeCornerFontPixels(size, parts.workspace.size()));
        drawBadgeText(painter, parts.desktop, cornerCentre(parts.desktop, false),
                      core::desktopBadgeCornerFontPixels(size, parts.desktop.size()));
    } else {
        const QString text = parts.workspace.isEmpty() ? parts.desktop : parts.workspace;
        drawBadgeText(painter, text, QPointF(size / 2.0, size / 2.0),
                      core::desktopBadgeFontPixels(size, text.size()));
    }
    return pixmap;
}

} // namespace

QIcon applicationIcon()
{
    // 用 PNG 而不是 SVG：PNG 是 QtGui 内建的格式，**不需要 imageformats/qsvg
    // 插件**，也就不需要 Qt6::Svg 与「windeployqt 记得把那个插件拷过来」这两件事
    // ——少一个就会静默变成空白图标（见 AGENTS.md 第 10 节）。
    // 尺寸列表与 `tools/icon_gen`、CMakeLists.txt 里的 `qt_add_resources` 一致；
    // 托盘在不同 DPI 下取的正是这里的某一帧。
    static const char *const kFrames[] = {
        ":/icons/flowkeyd-16.png",  ":/icons/flowkeyd-20.png",  ":/icons/flowkeyd-24.png",
        ":/icons/flowkeyd-32.png",  ":/icons/flowkeyd-40.png",  ":/icons/flowkeyd-48.png",
        ":/icons/flowkeyd-64.png",  ":/icons/flowkeyd-128.png", ":/icons/flowkeyd-256.png",
    };

    QIcon icon;
    for (const char *path : kFrames) {
        const QString name = QLatin1String(path);
        // 存在性检查让「资源没编进来」变成空 QIcon（QIcon::addFile 对不存在的文件
        // 只是记一条失败项），调用方据此回退。
        if (QFile::exists(name)) {
            icon.addFile(name);
        }
    }
    return icon;
}

QIcon trayBadgeIcon(const core::TrayBadge &badge)
{
    const core::TrayBadgeParts parts = core::trayBadgeParts(badge);
    if (parts.isEmpty()) {
        // 读不到当前桌面 / 没有工作空间号：让调用方退回应用图标。
        return QIcon();
    }
    QIcon icon;
    for (const int size : kIconSizes) {
        icon.addPixmap(renderDesktopBadge(size, parts));
    }
    return icon;
}

} // namespace flowkeyd::app
