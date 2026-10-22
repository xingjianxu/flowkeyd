#include "app/app_icon.h"

#include "core/desktop_badge.h"

#include <QColor>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>

namespace flowkeyd::app {

namespace {

/// 与 `applicationIcon()` 用的帧列表一致（见那里的说明）：每个尺寸**单独渲染**，
/// 而不是缩放一张大位图，所以 16 px 下的数字也是清晰的。
constexpr int kIconSizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};

/// 画一帧「第几号虚拟桌面」：蓝色渐变圆角方块 + 白色粗体数字。
///
/// 配色取自仓库根目录的 `logo.svg`（渐变 `#00d2ff` → `#3a7bd5`，圆角 `rx = 56/256`），
/// 项目所有者 2026-09 在“蓝底白字 / 深蓝底青字 / logo + 数字角标”三种画法里选了
/// 第一种：16 px 的托盘图标上也要一眼看得出是几号。
QPixmap renderDesktopBadge(int size, const QString &text)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient gradient(0, 0, size, size);
    gradient.setColorAt(0.0, QColor(0x00, 0xd2, 0xff));
    gradient.setColorAt(1.0, QColor(0x3a, 0x7b, 0xd5));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    const qreal radius = size * 56.0 / 256.0;
    painter.drawRoundedRect(QRectF(0, 0, size, size), radius, radius);

    // 字按**字形墨迹**居中（而不是包含行距的方框）：数字没有下伸部，按方框居中
    // 会明显偏高。这里只有数字与 `+`，不需要中文字形（弹窗那套微软雅黑是另一件事）。
    QFont font(QStringLiteral("Segoe UI"));
    font.setBold(true);
    font.setPixelSize(core::desktopBadgeFontPixels(size, text.size()));
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QFontMetricsF metrics(font);
    const QRectF ink = metrics.boundingRect(text);
    painter.drawText(QPointF(size / 2.0 - (ink.left() + ink.width() / 2.0),
                             size / 2.0 - (ink.top() + ink.height() / 2.0)),
                     text);
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

QIcon desktopIcon(int number)
{
    const QString text = core::desktopBadgeText(number);
    if (text.isEmpty()) {
        // 读不到当前桌面（锁屏、非交互会话、版本表对不上）：让调用方退回应用图标。
        return QIcon();
    }
    QIcon icon;
    for (const int size : kIconSizes) {
        icon.addPixmap(renderDesktopBadge(size, text));
    }
    return icon;
}

} // namespace flowkeyd::app
