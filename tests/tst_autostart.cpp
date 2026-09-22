// 开机自启的计划任务：纯逻辑（XML 渲染 / 解析、输出解码、路径比较）。
//
// 真正的 `schtasks /Create` 与 `/Query` 需要管理员 + 会改系统状态，所以**不进单测**：
// 这里只盯那些能离线断言的纯函数。
#include <QtTest>

#include "platform/win/autostart.h"

using namespace flowkeyd;

namespace {

QByteArray utf16le(const QString &text)
{
    QByteArray out;
    out.append(static_cast<char>(0xFF));
    out.append(static_cast<char>(0xFE));
    for (const QChar ch : text) {
        const ushort unit = ch.unicode();
        out.append(static_cast<char>(unit & 0xFF));
        out.append(static_cast<char>((unit >> 8) & 0xFF));
    }
    return out;
}

} // namespace

class TestAutostart : public QObject
{
    Q_OBJECT

private slots:
    void taskNameIsStable();
    void currentExecutablePathIsNonEmpty();
    void buildTaskXmlUsesALogonTrigger();
    void buildTaskXmlHonoursTheDelay();
    void buildTaskXmlEscapesPaths();
    void taskXmlCommandRoundTrips();
    void taskXmlCommandWithoutCommand();
    void decodeTaskOutputHandlesUtf16();
    void decodeTaskOutputHandlesUtf8();
    void decodeTaskOutputFallsBackToLocal8Bit();
    void sameExecutablePathNormalizes();
};

void TestAutostart::taskNameIsStable()
{
    QCOMPARE(platform::win::autostartTaskName(), QStringLiteral("flowkeyd"));
}

void TestAutostart::currentExecutablePathIsNonEmpty()
{
    const QString path = platform::win::currentExecutablePath();
    QVERIFY(!path.isEmpty());
    QVERIFY(path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive));
    QVERIFY(QFileInfo::exists(path));
}

void TestAutostart::buildTaskXmlUsesALogonTrigger()
{
    platform::win::AutostartSpec spec;
    spec.executable = QStringLiteral("D:\\prj\\flowkeyd\\build\\dist-release\\flowkeyd.exe");
    spec.workingDirectory = QStringLiteral("D:\\prj\\flowkeyd\\build\\dist-release");
    spec.userId = QStringLiteral("WKS-HW\\xingjian");

    const QString xml = platform::win::buildTaskXml(spec);
    QVERIFY(xml.contains(QStringLiteral("<LogonTrigger>")));
    QVERIFY(xml.contains(QStringLiteral("<LogonType>InteractiveToken</LogonType>")));
    QVERIFY(xml.contains(QStringLiteral("<RunLevel>HighestAvailable</RunLevel>")));
    QVERIFY(xml.contains(QStringLiteral("<MultipleInstancesPolicy>IgnoreNew")));
    QVERIFY(xml.contains(QStringLiteral("<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>")));
    QVERIFY(xml.contains(QStringLiteral("<DisallowStartIfOnBatteries>false")));
    QVERIFY(xml.contains(QStringLiteral("<StopIfGoingOnBatteries>false")));
    QVERIFY(xml.contains(QStringLiteral("<RestartOnFailure>")));
    QVERIFY(xml.contains(QStringLiteral("<Command>D:\\prj\\flowkeyd\\build\\dist-release\\flowkeyd.exe</Command>")));
    QVERIFY(xml.contains(QStringLiteral("<WorkingDirectory>D:\\prj\\flowkeyd\\build\\dist-release</WorkingDirectory>")));
    QVERIFY(xml.contains(QStringLiteral("<UserId>WKS-HW\\xingjian</UserId>")));
}

void TestAutostart::buildTaskXmlHonoursTheDelay()
{
    platform::win::AutostartSpec spec;
    spec.executable = QStringLiteral("C:\\tools\\flowkeyd.exe");
    spec.userId = QStringLiteral("u");
    spec.logonDelaySeconds = 30;
    QVERIFY(platform::win::buildTaskXml(spec).contains(QStringLiteral("<Delay>PT30S</Delay>")));

    // 负数要夹成 0，否则会渲染出 `PT-5S` 这种 schtasks 不认的东西。
    spec.logonDelaySeconds = -5;
    const QString xml = platform::win::buildTaskXml(spec);
    QVERIFY(xml.contains(QStringLiteral("<Delay>PT0S</Delay>")));
    QVERIFY(!xml.contains(QStringLiteral("PT-5S")));
}

void TestAutostart::buildTaskXmlEscapesPaths()
{
    platform::win::AutostartSpec spec;
    spec.executable = QStringLiteral("C:\\a & b\\<x>\\flowkeyd.exe");
    spec.workingDirectory = QStringLiteral("C:\\a & b\\<x>");
    spec.userId = QStringLiteral("u");
    const QString xml = platform::win::buildTaskXml(spec);
    QVERIFY(xml.contains(QStringLiteral("<Command>C:\\a &amp; b\\&lt;x&gt;\\flowkeyd.exe</Command>")));
    // 转义之后的 XML 仍然能被解析回原路径。
    QCOMPARE(platform::win::taskXmlCommand(xml),
            std::optional<QString>(spec.executable));
}

void TestAutostart::taskXmlCommandRoundTrips()
{
    platform::win::AutostartSpec spec;
    spec.executable = QStringLiteral("D:\\tools\\flowkeyd.exe");
    spec.workingDirectory = QStringLiteral("D:\\tools");
    spec.userId = QStringLiteral("WKS-HW\\xingjian");
    const auto parsed = platform::win::taskXmlCommand(platform::win::buildTaskXml(spec));
    QCOMPARE(parsed, std::optional<QString>(spec.executable));
}

void TestAutostart::taskXmlCommandWithoutCommand()
{
    // 任务不存在时 `schtasks /Query` 打的是错误文本，里面没有 <Command>。
    QCOMPARE(platform::win::taskXmlCommand(QStringLiteral("ERROR: The system cannot find the file specified.")),
            std::nullopt);
    QCOMPARE(platform::win::taskXmlCommand(QStringLiteral("<Task><Actions/></Task>")),
            std::nullopt);
}

void TestAutostart::decodeTaskOutputHandlesUtf16()
{
    const QString text = QStringLiteral("<?xml version=\"1.0\"?><Exec><Command>C:\\工具\\flowkeyd.exe</Command></Exec>");
    QCOMPARE(platform::win::decodeTaskOutput(utf16le(text)), text);
}

void TestAutostart::decodeTaskOutputHandlesUtf8()
{
    const QString text = QStringLiteral("<Command>C:\\工具\\flowkeyd.exe</Command>");
    // 无 BOM 的 UTF-8：schtasks /Query /XML 重定向到管道时就是这个形状。
    QCOMPARE(platform::win::decodeTaskOutput(text.toUtf8()), text);
    // 有 BOM 的 UTF-8。
    QByteArray withBom = QByteArray("\xEF\xBB\xBF", 3);
    withBom += text.toUtf8();
    QCOMPARE(platform::win::decodeTaskOutput(withBom), text);
}

void TestAutostart::decodeTaskOutputFallsBackToLocal8Bit()
{
    // 既没有 BOM 也不是合法 UTF-8：退回本地代码页，而不是返回一串替换字符。
    QByteArray bytes("\xBE\xCD\xB2\xBF", 4); // 不是合法 UTF-8 序列
    const QString text = platform::win::decodeTaskOutput(bytes);
    QVERIFY(!text.isEmpty());
    QVERIFY(!text.contains(QChar::ReplacementCharacter));
}

void TestAutostart::sameExecutablePathNormalizes()
{
    using platform::win::sameExecutablePath;
    QVERIFY(sameExecutablePath(QStringLiteral("D:\\tools\\flowkeyd.exe"),
                               QStringLiteral("d:/TOOLS/flowkeyd.exe")));
    QVERIFY(sameExecutablePath(QStringLiteral("D:\\tools\\.\\flowkeyd.exe"),
                               QStringLiteral("D:/tools/flowkeyd.exe")));
    QVERIFY(!sameExecutablePath(QStringLiteral("D:\\tools\\flowkeyd.exe"),
                                QStringLiteral("D:\\other\\flowkeyd.exe")));
    QVERIFY(!sameExecutablePath(QString(), QStringLiteral("D:\\tools\\flowkeyd.exe")));
    QVERIFY(!sameExecutablePath(QStringLiteral("D:\\tools\\flowkeyd.exe"), QString()));
}

QTEST_MAIN(TestAutostart)
#include "tst_autostart.moc"
