#include "app/app_icons.h"

#include "platform/win/apps.h"
#include "platform/win/logging.h"

#include <QMutexLocker>
#include <QQuickTextureFactory>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <utility>
#include <vector>

namespace flowkeyd::app {

namespace win = flowkeyd::platform::win;

namespace {

/// 没有 `sourceSize` 时的兜底边长（设备像素）：`AppListModel::iconSize()` 的
/// 40 逻辑像素按 1× 算。QML 正常都会给 `sourceSize`，所以这条只是防御。
constexpr int kFallbackIconSize = 40;

/// 一次图标请求的应答。
///
/// 像素由工作线程填进来、然后 `finished()` —— 这正是 Qt 官方
/// `asyncimageprovider` 例子的形状（把响应对象丢给线程池，在那边填 `QImage`
/// 再发信号）。
class AppIconResponse : public QQuickImageResponse
{
public:
    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    /// **只能从工作线程调用**（紧接着就 `finished()`）。
    void setImage(QImage image) { m_image = std::move(image); }

private:
    QImage m_image;
};

} // namespace

// ---------------------------------------------------------------------------
// 工作线程：一条常驻的 STA 线程 + 一个队列
// ---------------------------------------------------------------------------

class AppIconProvider::Worker : public QThread
{
public:
    Worker() = default;

    ~Worker() override
    {
        {
            QMutexLocker locker(&m_mutex);
            m_stopping = true;
            // 队列里还没做的响应也收尾掉：`QQuickImageResponse` 要等
            // `finished()` 才会被 QML 那边回收，留着不发就是泄漏（退出路径上
            // 最多只有几个）。
            for (const Job &job : m_jobs) {
                job.response->setImage(QImage());
                emit job.response->finished();
            }
            m_jobs.clear();
        }
        m_ready.wakeAll();
        wait();
    }

    /// 排一个取图标的活儿。可以从任意线程调用。
    void enqueue(const QString &path, int size, AppIconResponse *response)
    {
        QMutexLocker locker(&m_mutex);
        m_jobs.push_back(Job{path, size, response});
        m_ready.wakeOne();
    }

protected:
    void run() override
    {
        // 这条线程常驻，所以 COM 只初始化一次（STA：shell 的图像工厂要它）。
        win::apps::StaThread apartment;
        if (!apartment.ok()) {
            win::logWarn(QStringLiteral("app icons: %1").arg(apartment.error()));
            return;
        }
        for (;;) {
            std::vector<Job> jobs;
            {
                QMutexLocker locker(&m_mutex);
                while (m_jobs.empty() && !m_stopping) {
                    m_ready.wait(&m_mutex);
                }
                if (m_stopping && m_jobs.empty()) {
                    return;
                }
                jobs.swap(m_jobs);
            }
            for (const Job &job : jobs) {
                // 缓存命中时连 shell 都不用问（滚动回去、第二次弹出都走这里）。
                const QString cacheKey = job.path + QChar(0x1f) + QString::number(job.size);
                QImage image;
                {
                    QMutexLocker locker(&m_mutex);
                    const auto found = m_cache.constFind(cacheKey);
                    if (found != m_cache.constEnd()) {
                        image = found.value();
                    }
                }
                if (image.isNull()) {
                    const win::apps::ShellIcon icon = win::apps::shellIcon(job.path, job.size);
                    if (icon.ok) {
                        // `ShellIcon::pixels` 就是 `QImage::Format_ARGB32` 的字节布局
                        // （32 位 BGRA、直通 alpha、自顶向下），拷一份让 QImage 自己
                        // 持有数据。
                        image = QImage(reinterpret_cast<const uchar *>(icon.pixels.data()),
                                       icon.width, icon.height, icon.width * 4,
                                       QImage::Format_ARGB32)
                                    .copy();
                    } else {
                        // 失败的条目也缓存（空图）：否则每次滚动都会重新问一遍
                        // shell、还会把同一条警告写进日志几百遍。
                        win::logDebug(QStringLiteral("app icons: %1: %2")
                                          .arg(job.path, icon.error));
                    }
                    QMutexLocker locker(&m_mutex);
                    m_cache.insert(cacheKey, image);
                }
                job.response->setImage(image);
                emit job.response->finished();
            }
        }
    }

private:
    struct Job
    {
        QString path;
        int size = 0;
        AppIconResponse *response = nullptr;
    };

    mutable QMutex m_mutex;
    QWaitCondition m_ready;
    std::vector<Job> m_jobs;
    QHash<QString, QImage> m_cache;
    bool m_stopping = false;
};

// ---------------------------------------------------------------------------
// 提供者
// ---------------------------------------------------------------------------

AppIconProvider::AppIconProvider()
{
    // 工作线程没有 parent：`QThread` 必须由我们自己停掉再销毁（见析构）。
    m_worker = new Worker;
    m_worker->start();
}

AppIconProvider::~AppIconProvider()
{
    // `Worker` 的析构会先唤醒它的等待、把队列里没做完的响应收尾，再 `wait()`。
    delete m_worker;
    m_worker = nullptr;
}

void AppIconProvider::publish(const QVector<QPair<QString, QString>> &entries)
{
    QMutexLocker locker(&m_mutex);
    for (const QPair<QString, QString> &entry : entries) {
        if (!entry.first.isEmpty() && !entry.second.isEmpty()) {
            m_paths.insert(entry.first, entry.second);
        }
    }
}

QQuickImageResponse *AppIconProvider::requestImageResponse(const QString &id,
                                                          const QSize &requestedSize)
{
    auto *response = new AppIconResponse;
    QString path;
    {
        QMutexLocker locker(&m_mutex);
        path = m_paths.value(id);
    }
    if (path.isEmpty()) {
        // 表里没有这个键（多半是预热用的假数据，或者列表刚被换掉）：立刻收尾，
        // `Image` 会画成空的。不要把它当成错误写日志 —— 预热每渲染一帧都会问一次。
        response->setImage(QImage());
        emit response->finished();
        return response;
    }
    m_worker->enqueue(path, requestedIconSize(requestedSize), response);
    return response;
}

int AppIconProvider::requestedIconSize(const QSize &requestedSize)
{
    int size = std::max(requestedSize.width(), requestedSize.height());
    if (size <= 0) {
        size = kFallbackIconSize;
    }
    // shell 拿很大的图会明显变慢（256 那一档要解码整张 PNG），而卡片上一次
    // 只画 40 逻辑像素：夹在 512 以内足够任何 DPI，也挡住了 QML 传错尺寸。
    return std::clamp(size, 8, 512);
}

} // namespace flowkeyd::app
