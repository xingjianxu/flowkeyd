#include "core/desktop_badge.h"

#include <QtGlobal>

#include <algorithm>

namespace flowkeyd::core {

QString trayBadgeModeName(TrayBadgeMode mode)
{
    switch (mode) {
    case TrayBadgeMode::Both:
        return QStringLiteral("both");
    case TrayBadgeMode::Desktop:
        return QStringLiteral("desktop");
    case TrayBadgeMode::Workspace:
        return QStringLiteral("workspace");
    }
    return QStringLiteral("both");
}

std::optional<TrayBadgeMode> trayBadgeModeFromName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("both")) {
        return TrayBadgeMode::Both;
    }
    if (key == QLatin1String("desktop")) {
        return TrayBadgeMode::Desktop;
    }
    if (key == QLatin1String("workspace")) {
        return TrayBadgeMode::Workspace;
    }
    return std::nullopt;
}

QStringList trayBadgeModeNames()
{
    return {trayBadgeModeName(TrayBadgeMode::Both), trayBadgeModeName(TrayBadgeMode::Desktop),
            trayBadgeModeName(TrayBadgeMode::Workspace)};
}

TrayBadgeParts trayBadgeParts(const TrayBadge &badge)
{
    TrayBadgeParts parts;
    switch (badge.mode) {
    case TrayBadgeMode::Both:
        parts.workspace = desktopBadgeText(badge.workspace);
        parts.desktop = desktopBadgeText(badge.desktop);
        break;
    case TrayBadgeMode::Desktop:
        parts.desktop = desktopBadgeText(badge.desktop);
        break;
    case TrayBadgeMode::Workspace:
        parts.workspace = desktopBadgeText(badge.workspace);
        break;
    }
    return parts;
}

QString trayBadgeText(const TrayBadge &badge)
{
    const TrayBadgeParts parts = trayBadgeParts(badge);
    if (parts.isSplit()) {
        return parts.workspace + QLatin1Char('/') + parts.desktop;
    }
    return parts.workspace.isEmpty() ? parts.desktop : parts.workspace;
}

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
    // 一位数（`0.62`）、两位数（`0.42`，也就是 `9+`）、三个字符（`0.32`，
    // `1/2` 这种写法）各一个比例：16 px 的图标上分别是 10、7、6 像素。
    double ratio = 0.62;
    if (characters >= 3) {
        ratio = 0.32;
    } else if (characters == 2) {
        ratio = 0.42;
    }
    return std::max(6, qRound(iconSize * ratio));
}

int desktopBadgeCornerFontPixels(int iconSize, int characters)
{
    if (characters <= 0 || iconSize <= 0) {
        return 0;
    }
    // 一个号占一个角：一位数能用半个图标那么大的字（16 px 上是 8 px），
    // 两位的 `9+` 再降一档（`0.30`）。
    const double ratio = characters >= 2 ? 0.30 : 0.50;
    return std::max(6, qRound(iconSize * ratio));
}

} // namespace flowkeyd::core
