// 弹窗共用的几何工具（纯算术，见 popup_layout.h）。
#include "app/popup_layout.h"

#include <algorithm>
#include <cmath>

namespace flowkeyd::app {

PopupPoint centrePopup(const PopupRect &work, const PopupRect &bounds, int width, int height)
{
    PopupPoint point;
    point.x = work.x + (work.width - width) / 2;
    point.y = work.y + (work.height - height) / 2;
    // 夹进屏幕：卡片放不下时贴左上角（与 `std::clamp` 不同，
    // 这里的上界可能小于下界，所以要自己取 max）。
    point.x = std::clamp(point.x, bounds.x, std::max(bounds.x, bounds.x + bounds.width - width));
    point.y = std::clamp(point.y, bounds.y, std::max(bounds.y, bounds.y + bounds.height - height));
    return point;
}

bool popupPixelSizeIsStale(const QSize &physical, int logicalWidth, int logicalHeight,
                           qreal screenDpr, int slack)
{
    if (physical.isEmpty() || logicalWidth <= 0 || logicalHeight <= 0) {
        return false;
    }
    // 屏幕报 0 / 负数（理论上不会）时按 1:1 处理，至少能把「停在 1:1 上」的那种
    // 情形认出来。
    const qreal dpr = screenDpr > 0.0 ? screenDpr : 1.0;
    const int expectedWidth = static_cast<int>(std::lround(logicalWidth * dpr));
    const int expectedHeight = static_cast<int>(std::lround(logicalHeight * dpr));
    return std::abs(physical.width() - expectedWidth) > slack
        || std::abs(physical.height() - expectedHeight) > slack;
}

} // namespace flowkeyd::app
