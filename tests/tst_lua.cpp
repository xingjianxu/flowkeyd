// Lua 配置层测试：DSL 注入、两种写法、每个构造器、错误信息格式与 BOM。
//
// 测试点对齐 oskeyd 的 `src/lua.rs`（见 AGENTS.md 第 9 节的阶段 2）。
#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/action.h"
#include "core/config.h"
#include "core/keys.h"
#include "lua/lua_config.h"

using namespace flowkeyd;

namespace {

/// 走完「Lua 求值 + 校验编译」，成功时返回编译结果。
std::optional<core::Compiled> parse(const QString &script)
{
    core::EvalResult evaluated;
    const core::Evaluator evaluator = lua::makeLuaEvaluator();
    if (auto error = evaluator(script, QStringLiteral("test.lua"), &evaluated);
        error.has_value()) {
        qWarning("unexpected parse error: %s", qPrintable(error->toString()));
        return std::nullopt;
    }
    core::Compiled out;
    if (auto error = core::compile(evaluated.config, QStringLiteral("test.lua"),
                                   evaluated.errors, evaluated.warnings, &out);
        error.has_value()) {
        qWarning("unexpected validation error: %s", qPrintable(error->toString()));
        return std::nullopt;
    }
    return out;
}

/// 期望失败，返回错误渲染文本（成功时返回空串）。
QString failure(const QString &script)
{
    core::EvalResult evaluated;
    const core::Evaluator evaluator = lua::makeLuaEvaluator();
    if (auto error = evaluator(script, QStringLiteral("test.lua"), &evaluated);
        error.has_value()) {
        return error->toString();
    }
    core::Compiled out;
    if (auto error = core::compile(evaluated.config, QStringLiteral("test.lua"),
                                   evaluated.errors, evaluated.warnings, &out);
        error.has_value()) {
        return error->toString();
    }
    return QString();
}

} // namespace

class TestLua : public QObject
{
    Q_OBJECT

private slots:
    void imperativeAndDeclarativeStylesAgree();
    void loopsCanGenerateBindings();
    void helpersBuildTheSameTablesAsTheRawForm();
    void runWithoutArgsAndWithEmptyArgs();
    void menuAndPowerHelpersBuildTheExpectedTables();
    void helpHelperTakesAnOptionalTitle();
    void shorthandStringsStillWork();
    void chordListsAndAliasesWork();
    void inlineFunctionsAreRejectedWithAHint();
    void syntaxErrorsPointAtTheLine();
    void runtimeErrorsPointAtTheLine();
    void registrationArgumentMustBeATable();
    void helperArgumentsAreTypeChecked();
    void unknownFieldsAreReportedWithTheEntryName();
    void functionsInsideAnActionListAreRejectedToo();
    void namedKeysInsideAListAreRejectedInsteadOfIgnored();
    void returnedTableIsChecked();
    void emptyActionListIsAnErrorNotASilentNoop();
    void emptyTableInAMapPositionExplainsItself();
    void repeatedSettingsWarnInsteadOfFailing();
    void luaIsActuallyLua();
    void longStringsKeepWindowsPathsIntact();
    void bomIsStrippedBeforeLuaSeesIt();
    void legacyTomlIsRejected();
};

void TestLua::imperativeAndDeclarativeStylesAgree()
{
    const auto imperative = parse(R"(
        settings{ swallow = false }
        hotkey{ name = "t", keys = "Ctrl+Alt+T", action = run("wt.exe", { "-w", "0" }) }
        remap{ from = "CapsLock", to = "Esc" }
    )");
    QVERIFY(imperative.has_value());
    const auto declarative = parse(R"(
        return {
          settings = { swallow = false },
          hotkeys = { { name = "t", keys = "Ctrl+Alt+T", action = { type = "run", program = "wt.exe", args = { "-w", "0" } } } },
          remaps = { { from = "CapsLock", to = "Esc" } },
        }
    )");
    QVERIFY(declarative.has_value());

    QCOMPARE(imperative->bindings.size(), std::size_t(1));
    QCOMPARE(declarative->bindings.size(), std::size_t(1));
    QCOMPARE(imperative->bindings[0].name, declarative->bindings[0].name);
    QCOMPARE(imperative->bindings[0].press[0].summary(), declarative->bindings[0].press[0].summary());
    QCOMPARE(imperative->remaps.size(), std::size_t(1));
    QVERIFY(!imperative->bindings[0].swallow);
    QVERIFY(!declarative->bindings[0].swallow);
}

void TestLua::loopsCanGenerateBindings()
{
    const auto c = parse(R"(
        for i = 1, 3 do
          hotkey{ name = "desktop-" .. i, keys = "Win+F" .. i, action = desktop(i) }
        end
    )");
    QVERIFY(c.has_value());
    QCOMPARE(c->bindings.size(), std::size_t(3));
    QCOMPARE(c->bindings[2].name, QStringLiteral("desktop-3"));
    QCOMPARE(c->bindings[2].chords[0].render(), QStringLiteral("Win+F3"));
    QVERIFY(c->bindings[2].press.size() == 1);
    QCOMPARE(c->bindings[2].press[0].kind, core::Action::Kind::Desktop);
    QCOMPARE(c->bindings[2].press[0].desktopSwitch, quint32(3));
}

void TestLua::helpersBuildTheSameTablesAsTheRawForm()
{
    const auto c = parse(R"(
        local n = 4
        hotkey{ keys = "F1", action = {
          send("^{c}", { delay_ms = 5 }),
          type_text("hi\n"),
          open("https://example.com", { show = "maximized" }),
          notify("title", "body"),
          volume("set", { level = 40 }),
          media("play_pause"),
          window("activate", { process = "wezterm", launch = { program = "wezterm.exe", wait_ms = 500 } }),
          clipboard("set", { text = "{selection}" }),
          caps_lock(),
          suspend("off"),
          run("cmd.exe", { "/c", "echo hi" }),
          reload(), quit(), none(),
        } }
        hotkey{ keys = "F2", action = volume("up", { step = 2 }), repeatable = { interval_ms = 60, delay_ms = 250 } }
    )");
    QVERIFY(c.has_value());
    const auto &first = c->bindings[0].press;
    QCOMPARE(first.size(), std::size_t(14));
    QCOMPARE(first[0].summary(), QStringLiteral("send ^{c}"));
    QCOMPARE(first[1].summary(), QStringLiteral("type \"hi\\n\""));
    QVERIFY(first[2].summary().startsWith(QStringLiteral("open https://example.com")));
    QCOMPARE(first[3].summary(), QStringLiteral("notify \"title\""));
    QCOMPARE(first[4].kind, core::Action::Kind::Volume);
    QCOMPARE(first[4].volumeOp, core::VolumeOp::Set);
    QCOMPARE(first[4].level.value_or(0), quint8(40));
    QCOMPARE(first[5].summary(), QStringLiteral("media PlayPause"));
    QVERIFY(first[6].summary().contains(QStringLiteral("launch if missing")));
    QCOMPARE(first[8].kind, core::Action::Kind::CapsLock);
    QCOMPARE(first[11].summary(), QStringLiteral("reload"));
    QCOMPARE(first[13].summary(), QStringLiteral("none"));

    const auto &second = c->bindings[1];
    QVERIFY(second.repeat.has_value());
    QCOMPARE(second.repeat->intervalMs, quint32(60));
    QCOMPARE(second.repeat->delayMs, quint32(250));
    QCOMPARE(second.trigger, core::TriggerMode::Repeat);
}

void TestLua::runWithoutArgsAndWithEmptyArgs()
{
    const QStringList scripts{
        QStringLiteral(R"(hotkey{ keys = "F1", action = run("notepad.exe") })"),
        QStringLiteral(R"(hotkey{ keys = "F1", action = run("notepad.exe", {}) })"),
        QStringLiteral(R"(hotkey{ keys = "F1", action = run("notepad.exe", nil, { show = "hidden" }) })"),
    };
    for (const QString &script : scripts) {
        const auto c = parse(script);
        QVERIFY(c.has_value());
        QCOMPARE(c->bindings[0].press[0].kind, core::Action::Kind::Run);
        QCOMPARE(c->bindings[0].press[0].program, QStringLiteral("notepad.exe"));
        QVERIFY(c->bindings[0].press[0].args.isEmpty());
    }
}

void TestLua::menuAndPowerHelpersBuildTheExpectedTables()
{
    const auto c = parse(R"(
        hotkey{ keys = "Win+X", action = menu{
          title = "电源",
          items = {
            { key = "s", label = "睡眠", hint = "Sleep", action = power("sleep") },
            { key = "p", label = "关机", action = power("shutdown") },
          },
        } }
    )");
    QVERIFY(c.has_value());
    const auto &action = c->bindings[0].press[0];
    QCOMPARE(action.kind, core::Action::Kind::Menu);
    QCOMPARE(action.items.size(), std::size_t(2));
    QVERIFY(action.items[0].key.has_value());
    QCOMPARE(*action.items[0].key, QStringLiteral("s"));
    QVERIFY(action.items[1].action != nullptr);
    QCOMPARE(action.items[1].action->kind, core::ActionSpec::Kind::One);
    QCOMPARE(action.items[1].action->action.kind, core::Action::Kind::Power);

    // `menu{}` 自己补上 `type`，但 `menu()` 传非表时要说人话。
    const QString message = failure(R"(hotkey{ keys = "F1", action = menu("power") })");
    QVERIFY(message.contains(QStringLiteral("`menu` expects a table")));

    // 条目里的 Lua 函数会被点名拒绝（和顶层动作一样）。
    const QString fn = failure(
        R"(hotkey{ keys = "F1", action = menu{ items = { { label = "x", action = function() end } } } })");
    QVERIFY(fn.contains(QStringLiteral("cannot be a Lua function")));
    QVERIFY(fn.contains(QStringLiteral("items[1].action")));
}

void TestLua::helpHelperTakesAnOptionalTitle()
{
    const auto plain = parse(R"(hotkey{ keys = "Win+/", action = help() })");
    QVERIFY(plain.has_value());
    QCOMPARE(plain->bindings[0].press[0].kind, core::Action::Kind::Help);
    QVERIFY(!plain->bindings[0].press[0].helpTitle.has_value());
    QCOMPARE(plain->bindings[0].press[0].summary(), QStringLiteral("help"));

    const auto titled = parse(R"(hotkey{ keys = "Win+/", action = help("快捷键") })");
    QVERIFY(titled.has_value());
    QVERIFY(titled->bindings[0].press[0].helpTitle.has_value());
    QCOMPARE(*titled->bindings[0].press[0].helpTitle, QStringLiteral("快捷键"));

    const QString message = failure(R"(hotkey{ keys = "F1", action = help(1) })");
    QVERIFY(message.contains(QStringLiteral("optional title string")));

    const auto shorthand = parse(R"(hotkey{ keys = "Win+/", action = "help" })");
    QVERIFY(shorthand.has_value());
    QCOMPARE(shorthand->bindings[0].press[0].kind, core::Action::Kind::Help);
}

void TestLua::shorthandStringsStillWork()
{
    const auto c = parse(R"(hotkey{ keys = "F1", action = "send:^{c}" })");
    QVERIFY(c.has_value());
    QCOMPARE(c->bindings[0].press[0].summary(), QStringLiteral("send ^{c}"));
}

void TestLua::chordListsAndAliasesWork()
{
    const auto c = parse(
        R"(hotkey{ name = "multi", keys = { "^!1", "^!2" }, press = "send:a", on_release = "send:b" })");
    QVERIFY(c.has_value());
    const auto &b = c->bindings[0];
    QCOMPARE(b.chords.size(), std::size_t(2));
    QCOMPARE(b.chords[0].mods.bits(), core::Modifiers::Ctrl.unioned(core::Modifiers::Alt).bits());
    QCOMPARE(b.press[0].summary(), QStringLiteral("send a"));
    QCOMPARE(b.release[0].summary(), QStringLiteral("send b"));
}

void TestLua::inlineFunctionsAreRejectedWithAHint()
{
    const QString message = failure(R"(hotkey{ keys = "F1", action = function() end })");
    QVERIFY(message.contains(QStringLiteral("cannot be a Lua function")));
    QVERIFY(message.contains(QStringLiteral("README")));

    // 出错的那次调用后面还有别的语句时，报错里会有出错的行号。
    const QString withLine = failure(
        "hotkey{ keys = \"F1\", action = function() end }\nhotkey{ keys = \"F2\" }");
    QVERIFY(withLine.contains(QStringLiteral("test.lua:1")));
}

void TestLua::syntaxErrorsPointAtTheLine()
{
    const QString message = failure("hotkey{ keys = \"F1\",\n  action = \"none\"\n");
    QVERIFY(message.contains(QStringLiteral("test.lua:3")));
    QVERIFY(message.contains(QStringLiteral("syntax error")));
}

void TestLua::runtimeErrorsPointAtTheLine()
{
    const QString message = failure("local t = nil\nreturn t.hotkeys\n");
    QVERIFY(message.contains(QStringLiteral("test.lua:2")));
}

void TestLua::registrationArgumentMustBeATable()
{
    const QString message = failure(R"(hotkey("F1"))");
    QVERIFY(message.contains(QStringLiteral("expects a table")));
    QVERIFY(message.contains(QStringLiteral("string")));
}

void TestLua::helperArgumentsAreTypeChecked()
{
    const QString program = failure(R"(hotkey{ keys = "F1", action = run(42) })");
    QVERIFY(program.contains(QStringLiteral("run() expects a program string")));
    const QString desktop = failure(R"(hotkey{ keys = "F1", action = desktop("one") })");
    QVERIFY(desktop.contains(QStringLiteral("desktop() expects a desktop number")));
}

void TestLua::unknownFieldsAreReportedWithTheEntryName()
{
    const QString message = failure(R"(hotkey{ name = "oops", keys = "F1", typo_field = 1 })");
    QVERIFY(message.contains(QStringLiteral("hotkey #1 (`oops`)")));
    QVERIFY(message.contains(QStringLiteral("typo_field")));
    QVERIFY(message.contains(QStringLiteral("unknown field")));
}

void TestLua::functionsInsideAnActionListAreRejectedToo()
{
    const QString message = failure(R"(hotkey{ keys = "F1", action = { send("a"), function() end } })");
    QVERIFY(message.contains(QStringLiteral("cannot be a Lua function")));
    QVERIFY(message.contains(QStringLiteral("action[2]")));
}

void TestLua::namedKeysInsideAListAreRejectedInsteadOfIgnored()
{
    const QString action = failure(R"(hotkey{ keys = "F1", action = { send("a"), typo = 2 } })");
    QVERIFY(action.contains(QStringLiteral("mixes list entries")));
    QVERIFY(action.contains(QStringLiteral("typo")));

    const QString keys = failure(R"(hotkey{ keys = { "F1", typo = "F2" }, action = "none" })");
    QVERIFY(keys.contains(QStringLiteral("mixes list entries")));

    // 正常的列表与带命名字段的表都不受影响。
    const auto c = parse(R"(hotkey{ keys = { "F1", "F2" }, action = { send("a"), none() } })");
    QVERIFY(c.has_value());
    QCOMPARE(c->bindings.size(), std::size_t(1));
    QCOMPARE(c->bindings[0].press.size(), std::size_t(2));
    const auto settings = parse(R"(settings{ swallow = true })");
    QVERIFY(settings.has_value());
    QVERIFY(settings->settings.swallow);
}

void TestLua::returnedTableIsChecked()
{
    const QString unknown = failure(R"(return { hotkey = {} })");
    QVERIFY(unknown.contains(QStringLiteral("unknown field `hotkey`")));

    const QString mapped = failure(R"(return { hotkeys = { first = { keys = "F1" } } })");
    QVERIFY(mapped.contains(QStringLiteral("must be a list of tables")));

    const QString notTable = failure(R"(return 42)");
    QVERIFY(notTable.contains(QStringLiteral("must return a table")));

    const auto nilReturn = parse(R"(return nil)");
    QVERIFY(nilReturn.has_value());
    QVERIFY(nilReturn->bindings.empty());
    const auto empty = parse(QString());
    QVERIFY(empty.has_value());
    QVERIFY(empty->bindings.empty());
}

void TestLua::emptyActionListIsAnErrorNotASilentNoop()
{
    const QString message = failure(R"(hotkey{ name = "quiet", keys = "F1", action = {} })");
    QVERIFY(message.contains(QStringLiteral("quiet")));
    QVERIFY(message.contains(QStringLiteral("empty action list")));
}

void TestLua::emptyTableInAMapPositionExplainsItself()
{
    const QString message = failure(R"(hotkey{ keys = "F1", action = run("x", nil, { env = {} }) })");
    QVERIFY(message.contains(QStringLiteral("empty")));
    QVERIFY(message.contains(QStringLiteral("named keys")));
}

void TestLua::repeatedSettingsWarnInsteadOfFailing()
{
    const auto c = parse(R"(
        settings{ swallow = false }
        return { settings = { tick_ms = 20 } }
    )");
    QVERIFY(c.has_value());
    QVERIFY(!c->settings.swallow);
    QCOMPARE(c->settings.tickMs, quint32(20));
    bool warned = false;
    for (const QString &warning : c->warnings) {
        if (warning.contains(QStringLiteral("settings")) && warning.contains(QStringLiteral("times"))) {
            warned = true;
        }
    }
    QVERIFY(warned);
}

void TestLua::luaIsActuallyLua()
{
    const auto c = parse(R"(
        local names = { "a", "b" }
        for _, name in ipairs(names) do
          hotkey{ name = name, keys = "Ctrl+Alt+" .. string.upper(name), action = send("{" .. name .. "}") }
        end
    )");
    QVERIFY(c.has_value());
    QCOMPARE(c->bindings.size(), std::size_t(2));
    QCOMPARE(c->bindings[0].chords[0].render(), QStringLiteral("Ctrl+Alt+A"));
}

void TestLua::longStringsKeepWindowsPathsIntact()
{
    const auto c = parse(R"(hotkey{ keys = "F1", action = run([[C:\tools\my app\app.exe]], { "-x" }) })");
    QVERIFY(c.has_value());
    QCOMPARE(c->bindings[0].press[0].program, QStringLiteral("C:\\tools\\my app\\app.exe"));

    // 普通字符串里的 `\t` 是转义序列：`"C:\tools"` 会静默变成 `C:` + 制表符 + `ools`。
    const auto escaped = parse(R"(hotkey{ keys = "F1", action = open("C:\tools") })");
    QVERIFY(escaped.has_value());
    QCOMPARE(escaped->bindings[0].press[0].target.value_or(QString()), QStringLiteral("C:\tools"));
}

void TestLua::bomIsStrippedBeforeLuaSeesIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("config.lua"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    // 记事本与 `Set-Content -Encoding UTF8` 都会写 BOM，而 Lua 不认它。
    file.write("\xEF\xBB\xBF");
    file.write("hotkey{ keys = \"F1\", action = \"none\" }\n");
    file.close();

    core::Compiled out;
    const auto error = core::loadConfig(lua::makeLuaEvaluator(), path, &out);
    QVERIFY2(!error.has_value(), qPrintable(error.has_value() ? error->toString() : QString()));
    QCOMPARE(out.bindings.size(), std::size_t(1));
}

void TestLua::legacyTomlIsRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("config.toml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[[hotkey]]\n");
    file.close();

    core::Compiled out;
    const auto error = core::loadConfig(lua::makeLuaEvaluator(), path, &out);
    QVERIFY(error.has_value());
    QCOMPARE(error->kind, core::ConfigError::Kind::LegacyToml);
}

QTEST_MAIN(TestLua)
#include "tst_lua.moc"
