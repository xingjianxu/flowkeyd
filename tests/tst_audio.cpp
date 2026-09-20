// 音量步进与钳位的纯计算单测。
//
// 刻意**不**调用 `audio::apply()`：那会改变这台机器的真实音量。
// 这里只覆盖 `nextVolumeScalar`（不碰任何设备、不碰 COM）。
#include <QtTest>

#include "core/action.h"
#include "platform/win/audio.h"

using namespace flowkeyd;

class TestAudio : public QObject
{
    Q_OBJECT

private slots:
    void setUsesTheLevelAndClamps();
    void upAndDownUseTheDefaultStep();
    void upAndDownHonourAnExplicitStep();
    void upAndDownClampAtTheEdges();
    void muteOpsLeaveTheScalarAlone();
};

void TestAudio::setUsesTheLevelAndClamps()
{
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.5F, core::VolumeOp::Set, 40, std::nullopt), 0.4F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.5F, core::VolumeOp::Set, 100, std::nullopt), 1.0F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.5F, core::VolumeOp::Set, 0, std::nullopt), 0.0F));
    // 超过 100 的值也要钳到 100，而不是溢出。
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.5F, core::VolumeOp::Set, 250, std::nullopt), 1.0F));
    // 没有 level 时按 0 处理（配置校验会在加载期先报错）。
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.5F, core::VolumeOp::Set, std::nullopt, std::nullopt), 0.0F));
}

void TestAudio::upAndDownUseTheDefaultStep()
{
    // 默认步进 2%。
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.50F, core::VolumeOp::Up, std::nullopt, std::nullopt), 0.52F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.50F, core::VolumeOp::Down, std::nullopt, std::nullopt), 0.48F));
}

void TestAudio::upAndDownHonourAnExplicitStep()
{
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.50F, core::VolumeOp::Up, std::nullopt, 10), 0.60F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.50F, core::VolumeOp::Down, std::nullopt, 10), 0.40F));
    // 步进 0 要当成 1，不能原地不动。
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.50F, core::VolumeOp::Up, std::nullopt, 0), 0.51F));
}

void TestAudio::upAndDownClampAtTheEdges()
{
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(1.0F, core::VolumeOp::Up, std::nullopt, 10), 1.0F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.0F, core::VolumeOp::Down, std::nullopt, 10), 0.0F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.95F, core::VolumeOp::Up, std::nullopt, 50), 1.0F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.05F, core::VolumeOp::Down, std::nullopt, 50), 0.0F));
}

void TestAudio::muteOpsLeaveTheScalarAlone()
{
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.42F, core::VolumeOp::Mute, std::nullopt, std::nullopt), 0.42F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.42F, core::VolumeOp::Unmute, std::nullopt, std::nullopt), 0.42F));
    QVERIFY(qFuzzyCompare(platform::win::audio::nextVolumeScalar(0.42F, core::VolumeOp::Toggle, std::nullopt, std::nullopt), 0.42F));
}

QTEST_MAIN(TestAudio)
#include "tst_audio.moc"
