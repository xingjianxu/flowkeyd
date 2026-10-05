// 程序启动器的图标：一个**异步**的 QML 图片提供者。
//
// 为什么必须异步：从 shell 里取一个图标在真机上是 **3 ms 上下**（159 个开始菜单
// 条目连取一遍约 400 ms，冷启动那一次还要更久）。卡片弹出是热路径，等 400 ms
// 是不能接受的；而 QQuickImageProvider 的同步版本会在**渲染线程**上回调我们，
// 那里既没有初始化 COM 单元，也不能让 shell 跑进去。所以：
//
//   * 模型给每一行一个 `image://flowkeyd-app/<core::appIconKey(启动名)>`；
//   * 提供者收到请求后把活儿排进**自己那条常驻 STA 线程**的队列，立刻返回一个
//     `QQuickImageResponse`；图标取好之后在那边填进去再 `finished()`，
//     QML 的 `Image` 于是自己换上真正的图标（用户看到的是图标逐格“长出来”）；
//   * 取过的图标按「键 + 边长」缓存在那条线程上，所以第二次弹出、滚动回去、
//     卡片隐藏再显示都不用重取。
//
// 键为什么是哈希而不是路径本身：见 `core/app_list.h` 的 `appIconKey()` ——
// `image://` 的 id 是 URL 的一部分，把反斜杠 / 空格 / 中文都塞进去就要双方对
// 「转义了几次」达成一致；换成 16 个十六进制字符之后两边都省心，而且**同一个
// 程序永远是同一个 URL**（重扫、换位置都不会让 QML 的图片缓存认错图标）。
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QPair>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>
#include <QVector>

namespace flowkeyd::app {

/// 程序启动器图标的图片提供者。
///
/// **线程**：`publish()` 从动作线程调，`requestImageResponse()` 从 QML 的图片
/// 加载线程调，像素在那条常驻 STA 线程上取 —— 所有共享状态都用 `m_mutex` 或
/// 「只增不改」的表保护。
class AppIconProvider : public QQuickAsyncImageProvider
{
public:
    /// 图片提供者的注册名（`engine->addImageProvider(kProviderId, …)`）。
    /// 必须与 `core::appIconUrl()` 里的 `image://flowkeyd-app/` 一致。
    static constexpr const char *kProviderId = "flowkeyd-app";

    AppIconProvider();
    ~AppIconProvider() override;

    AppIconProvider(const AppIconProvider &) = delete;
    AppIconProvider &operator=(const AppIconProvider &) = delete;

    /// 登记一批「图标键 → 启动名」。可以从任意线程调用。
    ///
    /// 表是**只增不改**的：同一个键永远对应同一个启动名（键就是启动名的哈希），
    /// 所以列表重扫、条目换位置都不会让已经缓存下来的 URL 指向别的程序。
    void publish(const QVector<QPair<QString, QString>> &entries);

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    /// 取图标的边长（设备像素）。
    ///
    /// QML 那边给 `Image` 设了 `sourceSize`（逻辑边长 × `Screen.devicePixelRatio`），
    /// 所以这里正常都能拿到一个合尺寸的值；拿不到时按 1× 的 40 像素处理
    /// （`AppListModel::iconSize()` 就是 40 —— 两边改动要一起改）。
    static int requestedIconSize(const QSize &requestedSize);

private:
    class Worker;

    QMutex m_mutex;
    /// 图标键 → 启动名。只增不改。
    QHash<QString, QString> m_paths;
    Worker *m_worker = nullptr;
};

} // namespace flowkeyd::app
