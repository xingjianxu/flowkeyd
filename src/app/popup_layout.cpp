// 弹窗共用的几何工具（纯算术，见 popup_layout.h）。
#include "app/popup_layout.h"

#include <algorithm>

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

} // namespace flowkeyd::app
