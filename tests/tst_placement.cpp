// `window_rule` 纯逻辑的单测：显示器排序/选择、摆放几何、匹配与摘要。
//
// 这里**不碰**任何 Win32 / 桌面：`platform/win/monitor` 只负责把
// `MONITORINFOEXW` 转成 `core::MonitorDescription` 并调用
// `SetWindowPlacement`，几何判断全部在 `core/placement`。
#include <QtTest>

#include "core/config.h"
#include "core/placement.h"

using namespace flowkeyd;

namespace {

core::MonitorDescription monitor(const QString &device,
                                 int x,
                                 int y,
                                 int width,
                                 int height,
                                 bool primary = false)
{
    core::MonitorDescription description;
    description.device = device;
    description.bounds = core::Rect{x, y, width, height};
    // 假任务栏 40 px：工作区比显示器矮一点，好让“最大化 = 工作区”这条断言有区分度。
    description.work = core::Rect{x, y, width, height - 40};
    description.primary = primary;
    return description;
}

core::WindowRule rule(const QString &process, std::optional<QString> title = std::nullopt)
{
    core::WindowRule value;
    value.name = process;
    value.process = process;
    value.title = title;
    return value;
}

} // namespace

class TestPlacement : public QObject
{
    Q_OBJECT

private slots:
    void sortedMonitorsOrdersLeftThenTop();
    void selectMonitorByIndex();
    void selectMonitorByPrimaryAndDevice();
    void selectMonitorReturnsNulloptWhenMissing();
    void normalizeDeviceNameStripsThePrefix();
    void newMonitorDevicesDetectsReconnects();
    void placementRectMaximizesToTheWorkArea();
    void placementRectCentersAndKeepsTheSize();
    void placementRectHonoursPositionAndSize();
    void placementRectClampsIntoTheWorkArea();
    void placementRectAlignsTopLeftWhenLargerThanTheWorkArea();
    void windowMatchesRuleRequiresEveryCondition();
    void windowRuleSummaryIsReadable();
};

void TestPlacement::sortedMonitorsOrdersLeftThenTop()
{
    // 故意打乱顺序：右边那块先给，再给左下、左上。
    std::vector<core::MonitorDescription> monitors{
        monitor(QStringLiteral("\\\\.\\DISPLAY3"), 1920, 0, 1920, 1080),
        monitor(QStringLiteral("\\\\.\\DISPLAY2"), 0, 1080, 1920, 1080),
        monitor(QStringLiteral("\\\\.\\DISPLAY1"), 0, 0, 1920, 1080, true),
    };
    const std::vector<core::MonitorDescription> sorted = core::sortedMonitors(monitors);
    QCOMPARE(sorted.size(), std::size_t(3));
    QCOMPARE(sorted[0].device, QStringLiteral("\\\\.\\DISPLAY1"));
    QCOMPARE(sorted[1].device, QStringLiteral("\\\\.\\DISPLAY2"));
    QCOMPARE(sorted[2].device, QStringLiteral("\\\\.\\DISPLAY3"));
}

void TestPlacement::selectMonitorByIndex()
{
    const std::vector<core::MonitorDescription> sorted = core::sortedMonitors({
        monitor(QStringLiteral("\\\\.\\DISPLAY2"), 1920, 0, 1920, 1080),
        monitor(QStringLiteral("\\\\.\\DISPLAY1"), 0, 0, 1920, 1080, true),
    });
    core::MonitorRef ref;
    ref.kind = core::MonitorRef::Kind::Index;
    ref.index = 1;
    QCOMPARE(core::selectMonitor(sorted, ref), std::optional<std::size_t>(0));
    ref.index = 2;
    QCOMPARE(core::selectMonitor(sorted, ref), std::optional<std::size_t>(1));
    ref.index = 0;
    QCOMPARE(core::selectMonitor(sorted, ref), std::nullopt);
    ref.index = 3;
    QCOMPARE(core::selectMonitor(sorted, ref), std::nullopt);
}

void TestPlacement::selectMonitorByPrimaryAndDevice()
{
    const std::vector<core::MonitorDescription> sorted = core::sortedMonitors({
        monitor(QStringLiteral("\\\\.\\DISPLAY2"), 1920, 0, 1920, 1080),
        monitor(QStringLiteral("\\\\.\\DISPLAY1"), 0, 0, 1920, 1080, true),
    });

    core::MonitorRef primary;
    primary.kind = core::MonitorRef::Kind::Primary;
    // 排序后 DISPLAY1（x=0）在前，DISPLAY2（x=1920）在后。
    QCOMPARE(core::selectMonitor(sorted, primary), std::optional<std::size_t>(0));

    // 设备名两种写法都要认，大小写无关。
    core::MonitorRef device;
    device.kind = core::MonitorRef::Kind::Device;
    device.device = QStringLiteral("DISPLAY2");
    QCOMPARE(core::selectMonitor(sorted, device), std::optional<std::size_t>(1));
    device.device = QStringLiteral("\\\\.\\display1");
    QCOMPARE(core::selectMonitor(sorted, device), std::optional<std::size_t>(0));
    device.device = QStringLiteral("DISPLAY9");
    QCOMPARE(core::selectMonitor(sorted, device), std::nullopt);
}

void TestPlacement::selectMonitorReturnsNulloptWhenMissing()
{
    const std::vector<core::MonitorDescription> empty;
    core::MonitorRef primary;
    primary.kind = core::MonitorRef::Kind::Primary;
    QCOMPARE(core::selectMonitor(empty, primary), std::nullopt);
}

void TestPlacement::normalizeDeviceNameStripsThePrefix()
{
    QCOMPARE(core::normalizeDeviceName(QStringLiteral("\\\\.\\DISPLAY2")), QStringLiteral("DISPLAY2"));
    QCOMPARE(core::normalizeDeviceName(QStringLiteral("display2")), QStringLiteral("DISPLAY2"));
    QCOMPARE(core::normalizeDeviceName(QStringLiteral("  DISPLAY2  ")), QStringLiteral("DISPLAY2"));
    QCOMPARE(core::normalizeDeviceName(QString()), QString());
}

void TestPlacement::newMonitorDevicesDetectsReconnects()
{
    const QStringList one{QStringLiteral("DISPLAY1")};
    const QStringList two{QStringLiteral("DISPLAY1"), QStringLiteral("\\\\.\\DISPLAY2")};
    // 从无到有：这就是“之前断开的显示器重新接上”。
    QCOMPARE(core::newMonitorDevices(one, two), QStringList{QStringLiteral("DISPLAY2")});
    // 只是分辨率/排列变化（设备名集合不变）不算。
    QCOMPARE(core::newMonitorDevices(one, one), QStringList());
    QCOMPARE(core::newMonitorDevices(two, one), QStringList());
    // 同一块显示器换个写法（全名 / 短名）不算“新设备”。
    QCOMPARE(core::newMonitorDevices(QStringList{QStringLiteral("\\\\.\\DISPLAY1")}, one),
             QStringList());
}

void TestPlacement::placementRectMaximizesToTheWorkArea()
{
    const core::MonitorDescription target =
        monitor(QStringLiteral("\\\\.\\DISPLAY2"), 1920, 0, 1920, 1080);
    const core::Rect rect = core::placementRect(core::Rect{0, 0, 800, 600}, target, true,
                                                std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    // 最大化 = 工作区，而不是整块显示器（任务栏要留出来）。
    QCOMPARE(rect.x, 1920);
    QCOMPARE(rect.y, 0);
    QCOMPARE(rect.width, 1920);
    QCOMPARE(rect.height, 1040);
}

void TestPlacement::placementRectCentersAndKeepsTheSize()
{
    const core::MonitorDescription target =
        monitor(QStringLiteral("\\\\.\\DISPLAY2"), 1920, 0, 1920, 1080);
    const core::Rect rect = core::placementRect(core::Rect{0, 0, 800, 600}, target, false,
                                                std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    QCOMPARE(rect.width, 800);
    QCOMPARE(rect.height, 600);
    QCOMPARE(rect.x, 1920 + (1920 - 800) / 2);
    QCOMPARE(rect.y, (1040 - 600) / 2);
}

void TestPlacement::placementRectHonoursPositionAndSize()
{
    const core::MonitorDescription target =
        monitor(QStringLiteral("\\\\.\\DISPLAY2"), 1920, 0, 1920, 1080);
    // x/y 是相对目标显示器**工作区左上角**的偏移。
    const core::Rect rect = core::placementRect(core::Rect{0, 0, 800, 600}, target, false,
                                                std::optional<std::int32_t>(10),
                                                std::optional<std::int32_t>(20),
                                                std::optional<std::uint32_t>(1280),
                                                std::optional<std::uint32_t>(720));
    QCOMPARE(rect.x, 1930);
    QCOMPARE(rect.y, 20);
    QCOMPARE(rect.width, 1280);
    QCOMPARE(rect.height, 720);
}

void TestPlacement::placementRectClampsIntoTheWorkArea()
{
    const core::MonitorDescription target =
        monitor(QStringLiteral("\\\\.\\DISPLAY2"), 1920, 0, 1920, 1080);
    // 偏移远出屏幕时被夹回工作区右下角，窗口不会跑丢。
    const core::Rect rect = core::placementRect(core::Rect{0, 0, 800, 600}, target, false,
                                                std::optional<std::int32_t>(5000),
                                                std::optional<std::int32_t>(5000),
                                                std::nullopt, std::nullopt);
    QCOMPARE(rect.x, 1920 + 1920 - 800);
    QCOMPARE(rect.y, 1040 - 600);
}

void TestPlacement::placementRectAlignsTopLeftWhenLargerThanTheWorkArea()
{
    const core::MonitorDescription target =
        monitor(QStringLiteral("\\\\.\\DISPLAY2"), 1920, 0, 1280, 720);
    const core::Rect rect = core::placementRect(core::Rect{0, 0, 4000, 3000}, target, false,
                                                std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    // 比工作区还大的窗口对齐左上角，保证标题栏可见。
    QCOMPARE(rect.x, 1920);
    QCOMPARE(rect.y, 0);
    QCOMPARE(rect.width, 4000);
    QCOMPARE(rect.height, 3000);
}

void TestPlacement::windowMatchesRuleRequiresEveryCondition()
{
    const core::WindowRule byProcess = rule(QStringLiteral("wezterm"));
    QVERIFY(core::windowMatchesRule(byProcess, QStringLiteral("anything"),
                                    std::optional<QString>(QStringLiteral("wezterm-gui.exe"))));
    QVERIFY(!core::windowMatchesRule(byProcess, QStringLiteral("anything"),
                                     std::optional<QString>(QStringLiteral("chrome.exe"))));
    // 拿不到进程名时不能假定匹配。
    QVERIFY(!core::windowMatchesRule(byProcess, QStringLiteral("anything"), std::nullopt));

    const core::WindowRule both =
        rule(QStringLiteral("code"), std::optional<QString>(QStringLiteral("project")));
    QVERIFY(core::windowMatchesRule(both, QStringLiteral("Project - Visual Studio Code"),
                                    std::optional<QString>(QStringLiteral("code.exe"))));
    QVERIFY(!core::windowMatchesRule(both, QStringLiteral("Untitled - Visual Studio Code"),
                                     std::optional<QString>(QStringLiteral("code.exe"))));
    QVERIFY(!core::windowMatchesRule(both, QStringLiteral("Project - Notepad"),
                                     std::optional<QString>(QStringLiteral("notepad.exe"))));

    // 没有任何匹配条件的规则不匹配任何窗口（`compile()` 会拒绝这种配置）。
    core::WindowRule empty;
    QVERIFY(!core::windowMatchesRule(empty, QStringLiteral("x"),
                                     std::optional<QString>(QStringLiteral("x.exe"))));
}

void TestPlacement::windowRuleSummaryIsReadable()
{
    core::WindowRule value;
    value.name = QStringLiteral("wezterm");
    value.process = QStringLiteral("wezterm");
    value.desktop = 2;
    core::MonitorRef ref;
    ref.kind = core::MonitorRef::Kind::Index;
    ref.index = 2;
    value.monitor = ref;
    value.applyGeometry = true;
    value.maximize = true;
    QCOMPARE(value.summary(),
             QStringLiteral("process \"wezterm\", desktop 2, monitor 2, maximize"));

    core::WindowRule positioned;
    positioned.process = QStringLiteral("code");
    positioned.applyGeometry = true;
    positioned.maximize = false;
    positioned.x = 0;
    positioned.y = 10;
    positioned.width = 1280;
    positioned.height = 800;
    QCOMPARE(positioned.summary(),
             QStringLiteral("process \"code\", size 1280x800, at 0,10"));
}

QTEST_MAIN(TestPlacement)
#include "tst_placement.moc"
