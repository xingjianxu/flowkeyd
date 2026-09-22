#include "app/app_icon.h"

#include <QFile>

namespace flowkeyd::app {

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

} // namespace flowkeyd::app
