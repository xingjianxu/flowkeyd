// 冒烟测试：证明 vendor/lua 真的被编进来并且能跑。
#include <QtTest>

#include "lua/lua_include.h"

class TestSmoke : public QObject
{
    Q_OBJECT

private slots:
    void reportsTheVendoredLuaVersion();
    void runsAScript();
};

void TestSmoke::reportsTheVendoredLuaVersion()
{
    QCOMPARE(QString::fromLatin1(LUA_RELEASE), QStringLiteral("Lua 5.5.1"));
    QCOMPARE(LUA_VERSION_NUM, 505);
}

void TestSmoke::runsAScript()
{
    lua_State *state = luaL_newstate();
    QVERIFY(state != nullptr);
    luaL_openlibs(state);
    const char *script = "result = 0\nfor i = 1, 10 do result = result + i end\n";
    QCOMPARE(luaL_loadstring(state, script), LUA_OK);
    QCOMPARE(lua_pcall(state, 0, 0, 0), LUA_OK);
    lua_getglobal(state, "result");
    QCOMPARE(lua_tointeger(state, -1), 55);
    lua_pop(state, 1);
    lua_close(state);
}

QTEST_MAIN(TestSmoke)
#include "tst_smoke.moc"
