// 在线更新的最后一步：把新的 exe 换上、重启，并清理上一次留下的备份。
//
// 为什么能替换一个正在运行的 exe（Windows 上文件被映射成映像时既不能删也不能
// 覆盖，但**可以改名**）：把当前 exe 改名成 `<exe>.old` 之后，目标路径就空出来
// 了，把下载好的新 exe 改到那儿即可（同卷改名是原子的）。这条事实是本项目
// 早就验证过的（AGENTS.md 第 10 节「改名绕路」）。
//
// 顺序是刻意的，每一步都有回滚：
//   1. 删掉上一次可能留下的 `.old`（不删掉，下面的改名会失败）；
//   2. 当前 exe → `.old`；
//   3. `flowkeyd.exe.new` → 当前 exe；
//   4. 启动新实例（命令行的最后加上 `--updated-from <旧版本>`）；
//   5. 等一会儿看它有没有当场退出 —— 缺 DLL 的 exe 会立刻死掉，这时候必须回滚，
//      否则用户就没有能用的程序了。
//
// 备份 `.old` 由**新实例**在启动时删（当前进程的映像就是那个文件，自己删不掉）。
#pragma once

#include <QString>
#include <QStringList>

namespace flowkeyd::platform::win {

/// 替换 exe 并重启需要的全部信息。
struct UpdateInstallPlan
{
    /// 正在运行的 exe（要被替换的那个）。
    QString targetExecutable;
    /// 已经下载并校验过的新 exe（与 `targetExecutable` 同目录、同卷）。
    QString stagedExecutable;
    /// 新实例的命令行参数（不含程序名）。`--updated-from <旧版本>` 由
    /// `applyExecutableUpdate()` 自己追加。
    QStringList restartArgs;
    /// 当前（旧）版本号，写进新实例的 `--updated-from`。
    QString currentVersion;
};

/// 把 staged 换成 target 并重启，等新实例活过启动期后返回 true。
///
/// **调用方必须先把自己的运行时停干净**（卸钩子、停工作线程、关弹窗），
/// 因为这一步成功之后当前进程马上就要退出。
///
/// 失败时尽力回滚到旧 exe，返回 false 并填英文原因（`error` 里带上 Windows
/// 的错误文本或新进程的退出码）。
bool applyExecutableUpdate(const UpdateInstallPlan &plan, QString *error);

/// 删掉上次更新留下的 `<exe>.old`。
///
/// 新实例启动后调；旧进程可能还在退出过程中（它的映像就是那个文件），
/// 那时会失败返回 false —— 调用方过一会儿重试即可。文件本来就不存在也算成功。
bool removeExecutableBackup(const QString &executablePath);

} // namespace flowkeyd::platform::win
