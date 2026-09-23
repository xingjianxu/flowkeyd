#include "core/desktop_badge.h"

#include <QtGlobal>

#include <algorithm>

namespace flowkeyd::core {

QString desktopBadgeText(int number)
{
    if (number <= 0) {
        return QString();
    }
    if (number > 9) {
        return QStringLiteral("9+");
    }
    return QString::number(number);
}

int desktopBadgeFontPixels(int iconSize, int characters)
{
    if (characters <= 0 || iconSize <= 0) {
        return 0;
    }
    // 一位数（`0.62`）与两位数（`0.42`，也就是 `9+`）各一个比例：
    // 16 px 的图标上分别是 10 与 7 像素，实测都还认得出。
    const double ratio = characters >= 2 ? 0.42 : 0.62;
    return std::max(6, qRound(iconSize * ratio));
}

} // namespace flowkeyd::core
