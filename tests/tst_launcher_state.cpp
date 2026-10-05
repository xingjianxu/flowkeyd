// 程序启动器持久状态（`core/launcher_state`）的纯逻辑测试：JSON 解析 / 序列化
// 的容错、「最近使用」的顺序与上限、「固定」的切换，以及状态文件的路径。
//
// 真正的读写（`QFile`）在 `app::PopupHost` 里，不在这里测；`launcher.json`
// 与用户无关地放在配置文件旁边，所以路径那条也要钉住。
#include <QtTest>

#include <QDir>
#include <QFileInfo>

#include "core/launcher_state.h"

using namespace flowkeyd;

class TestLauncherState : public QObject
{
    Q_OBJECT

private slots:
    void stateFileSitsNextToTheConfig();
    void missingOrEmptyFileIsAnEmptyState();
    void parseKeepsOrderAndDropsJunk();
    void parseRejectsMalformedJsonButStillReturnsEmpty();
    void recentIsMovedToTheFrontAndCapped();
    void pinTogglesAtTheEnd();
    void serializedStateRoundTrips();
};

void TestLauncherState::stateFileSitsNextToTheConfig()
{
    const QString path = QDir::toNativeSeparators(
        core::launcherStatePath(QStringLiteral("C:/Users/me/.config/flowkeyd/config.lua")));
    QCOMPARE(QDir::fromNativeSeparators(path),
             QStringLiteral("C:/Users/me/.config/flowkeyd/launcher.json"));

    // 相对路径按当前目录解析（与 `--config` 的行为一致）。
    const QString relative = core::launcherStatePath(QStringLiteral("smoke.lua"));
    QCOMPARE(QFileInfo(relative).fileName(), QStringLiteral("launcher.json"));
    QVERIFY(QFileInfo(relative).isAbsolute());
}

void TestLauncherState::missingOrEmptyFileIsAnEmptyState()
{
    QString error = QStringLiteral("unset");
    const core::LauncherState empty = core::parseLauncherState(QByteArray(), &error);
    QVERIFY(empty.pinned.isEmpty());
    QVERIFY(empty.recent.isEmpty());
    // 空文件 = 第一次运行，不是错误。
    QVERIFY(error.isEmpty());

    const core::LauncherState blank =
        core::parseLauncherState(QByteArrayLiteral("\n  \n"));
    QVERIFY(blank.pinned.isEmpty());
    QVERIFY(blank.recent.isEmpty());
}

void TestLauncherState::parseKeepsOrderAndDropsJunk()
{
    const QByteArray json = R"({
      "version": 1,
      "pinned": ["aa", "bb", "aa", 7, "", "cc"],
      "recent": ["cc", "bb"],
      "somethingElse": true
    })";
    const core::LauncherState state = core::parseLauncherState(json);
    // 去重（保留第一次出现的位置）、丢掉非字符串与空串、未知字段忽略。
    QCOMPARE(state.pinned, QStringList({QStringLiteral("aa"), QStringLiteral("bb"),
                                        QStringLiteral("cc")}));
    QCOMPARE(state.recent, QStringList({QStringLiteral("cc"), QStringLiteral("bb")}));

    // 字段类型不对（`pinned` 不是数组）：那一部分当空，不整体报错。
    const core::LauncherState weird =
        core::parseLauncherState(QByteArrayLiteral(R"({"pinned": "aa", "recent": [1,2]})"));
    QVERIFY(weird.pinned.isEmpty());
    QVERIFY(weird.recent.isEmpty());
}

void TestLauncherState::parseRejectsMalformedJsonButStillReturnsEmpty()
{
    QString error;
    const core::LauncherState state =
        core::parseLauncherState(QByteArrayLiteral("{ this is not json"), &error);
    QVERIFY(state.pinned.isEmpty());
    QVERIFY(state.recent.isEmpty());
    QVERIFY(!error.isEmpty());

    // 合法的 JSON、但不是对象。
    QString arrayError;
    const core::LauncherState array =
        core::parseLauncherState(QByteArrayLiteral("[1, 2]"), &arrayError);
    QVERIFY(!arrayError.isEmpty());
}

void TestLauncherState::recentIsMovedToTheFrontAndCapped()
{
    QStringList recent;
    for (int i = 0; i < core::kRecentLimit + 3; ++i) {
        recent = core::touchRecent(recent, QStringLiteral("k%1").arg(i));
    }
    QCOMPARE(recent.size(), qsizetype(core::kRecentLimit));
    // 最新的一条在最前面，最旧的那几条被挤掉了。
    QCOMPARE(recent.first(), QStringLiteral("k%1").arg(core::kRecentLimit + 2));
    QVERIFY(!recent.contains(QStringLiteral("k0")));

    // 已经在里面的会被提前，不会重复。
    const QStringList once = core::touchRecent(recent, recent.at(5));
    QCOMPARE(once.size(), qsizetype(core::kRecentLimit));
    QCOMPARE(once.first(), recent.at(5));
    QCOMPARE(once.count(recent.at(5)), qsizetype(1));

    // 空键是空操作（没有稳定身份的行不该进「最近使用」）。
    QCOMPARE(core::touchRecent(recent, QString()), recent);
}

void TestLauncherState::pinTogglesAtTheEnd()
{
    QStringList pinned;
    pinned = core::togglePinned(pinned, QStringLiteral("aa"));
    pinned = core::togglePinned(pinned, QStringLiteral("bb"));
    QCOMPARE(pinned, QStringList({QStringLiteral("aa"), QStringLiteral("bb")}));

    // 再切一次就是取消固定（保留其余项的顺序）。
    QCOMPARE(core::togglePinned(pinned, QStringLiteral("aa")),
             QStringList({QStringLiteral("bb")}));

    // 空键是空操作。
    QCOMPARE(core::togglePinned(pinned, QString()), pinned);
}

void TestLauncherState::serializedStateRoundTrips()
{
    core::LauncherState state;
    state.pinned = {QStringLiteral("00ff"), QStringLiteral("beef")};
    state.recent = {QStringLiteral("beef"), QStringLiteral("cafe")};

    const QByteArray json = core::serializeLauncherState(state);
    // 人能看懂（缩进过的、带版本号），而不是一行压缩的。
    QVERIFY(json.contains('\n'));
    QVERIFY(json.contains("version"));

    const core::LauncherState parsed = core::parseLauncherState(json);
    QCOMPARE(parsed.pinned, state.pinned);
    QCOMPARE(parsed.recent, state.recent);
}

QTEST_MAIN(TestLauncherState)
#include "tst_launcher_state.moc"
