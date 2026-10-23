# 已踩过的坑（AGENTS.md 第 10 节）

> 每条都是「现象 → 原因 → 做法」。**只留判据，不留实测过程**；具体数字只在它本身就是判据时才留
> （如钩子 300 ms 预算、shell 菜单每条 22 逻辑像素）。要复现/重新量测时看 `tmp/` 那套一次性工具。

## 环境与工具

* **`qt_add_executable(... WIN32 ...)` 意味着 GUI 子系统 ⇒ 没有控制台**，`--check` 什么都打印不出来
  → `main()` 第一件事 `AttachConsole(ATTACH_PARENT_PROCESS)`（失败忽略），自己用 `WriteConsoleW` 写
  （重定向时走 UTF-8 `WriteFile`），**行尾 `\r\n`**。不要用 `qDebug` 当日志（没有控制台时走
  `OutputDebugString`）。
* **`QSystemTrayIcon` 在 QtWidgets 里**，不是 QtGui → 用它就必须 `QApplication`。
* **QML 窗口默认会让"关掉最后一个窗口"退出应用** → `setQuitOnLastWindowClosed(false)`。
* **FluentWinUI3 是 Qt Quick Controls 的样式，不是 Widgets 样式**（`QQuickStyle::setStyle` 要在加载
  任何 QML 之前）。
* **FluentWinUI3 不支持一批控件**（`Dial`、`Drawer`、`HorizontalHeaderView`、`SplitView`、`StackView`、
  `SwipeDelegate`、`SwipeView`、`TreeViewDelegate`、`Tumbler`、`VerticalHeaderView`）——它们会**静默回退
  到 Fusion**。布局不要用它们。
* **无边框圆角弹窗**：`flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint` +
  `color: "transparent"` + 内部一个 `Rectangle { radius: … }`。只设 `FramelessWindowHint` 而 `color`
  不是 transparent 就看不到圆角。
* **Qt 6 在 Windows 上默认就是 Per-Monitor DPI Aware V2**；不要在 `QApplication` 之前创建窗口。
* **Qt 会自动给主线程一个 STA**（OLE/拖放）。别把音频初始化放到主线程。
* **字体引擎是平台插件的构造参数，只能在 `QApplication` 之前选**：本机高缩放下 Qt Quick 界面发虚 →
  `main()` 第一行 `qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype")`。不要放到 main 之后、更
  不要在 QML 里设。
* **`PostThreadMessage` 在目标线程还没消息队列时静默失败** → 钩子线程先 `PeekMessage`。
* **PS 5.1 的 `Set-Content -Encoding UTF8` 写出 BOM**，而 Lua 词法分析器在 BOM 上报错 → 加载器先剥
  BOM。反向：**没有 BOM 的 `.ps1` 会被 PS 5.1 按 GBK 解码**，中文变乱码、甚至当成引号让 here-string
  提前结束、或者**静默吞掉下一行**（表现是"不报错、只是少做一步"）→ 写文件用
  `[System.IO.File]::WriteAllText($p, $text, [System.Text.UTF8Encoding]::new($true))`；**`tmp/` 下的
  一次性脚本一律纯 ASCII**。
* **`.cmd`/`.bat` 必须纯 ASCII**；**命令行上不要直接拼中文**。
* **`$args` 与 `$Pid` 是 PowerShell 自动变量**，不要当参数名/变量名。
* **`Start-Process -PassThru` 拿不到退出码**（`$p.ExitCode` 空）；`cmd.exe` 也不等 GUI 子系统进程 →
  验 CLI 输出用 `Start-Process -FilePath .\flowkeyd.exe -ArgumentList '--help' -NoNewWindow -Wait
  -PassThru -RedirectStandardOutput out.txt`。
* **PowerShell 的数组 splatting 不能转发命名参数** → 用哈希表 splatting。
* **`$ErrorActionPreference = 'Stop'` 下原生命令往 stderr 写一个字就被包成终止性异常** → 调 cmake/git/gh
  时用 helper 临时切回 `Continue`、只看退出码。
* **PowerShell 接原生命令的 UTF-8 stdout 会按控制台代码页（本机 GBK）解码** → 按字节重定向到文件再看；
  **`Tee-Object` 写出的是 UTF-16**。
* **`Get-Content` 不带 `-Encoding UTF8` 会把无 BOM 的 UTF-8 数错行数** → 用
  `[System.IO.File]::ReadAllLines`。
* **守护进程一直开着日志文件**，`ReadAllLines` 报"正由另一进程使用" → 用 `FileShare.ReadWrite` 的
  `FileStream`（`Get-Content` 默认就是 ReadWrite）。
* **核对文档/示例改动用 `git diff --no-index`**（比肉眼看可靠）。
* **`tmp/` 里有上次跑的旧日志** → 先看时间戳或先删掉。
* **`irm` 会把响应体开头的 BOM 留成真实的 `U+FEFF`** → `Invoke-Expression` 在 `param()` 上报
  `InvalidLeftHandSide`；也**不能** `curl | powershell -Command -`（GBK 解码中文）。所以 `install.ps1`
  只能「纯 ASCII + 无 BOM」。带参数跑用 `curl -o` 落盘 + `powershell -File`，或
  `&([scriptblock]::Create((irm <url>))) -Version …`。
* **`$PSScriptRoot` 在 `param()` 的默认值里是空的**（PS 5.1）→ 默认值写空串，进函数体再算。
* **`[ordered]@{}` 的整数键不能用 `$d[1234]` 取**：索引器把整数当**位置**、返回 `$null`（不报错）→
  用普通 `@{}`，或把键写成字符串。
* **`-f` 格式串里的 `{{`/`}}` 是转义大括号**：拿它拼 C++ 的 `{0x1234, 0x0023}` 会被解析坏 → 花括号用
  字符串相加拼。
* **提交进仓库的 `.ps1` 带中文注释时必须写 UTF-8 BOM**（否则 PS 5.1 按 GBK 解码、中文注释里的字节被当
  引号/括号，`UnexpectedToken` 报错行号还可能落在别处）；文件工具默认无 BOM，写完要转一次。

## 配置 / Lua / 核心逻辑

* **Lua 头文件里没有 `extern "C"` 保护** → C++ 里直接 `#include <lua.h>` 会 mangling、链接报一屏
  `undefined reference`。统一用 `src/lua/lua_include.h`。
* **`lua_next` 遍历中调 `lua_tolstring` 会把数字键就地转成字符串** → 下一次 `lua_next` 收到不存在的键、
  `PANIC: invalid key to 'next'`（abort）。只对 `LUA_TSTRING` 的键取字符串，数字键用 `lua_tointeger`。
* **嵌套遍历时内层压栈会打乱外层的 `lua_next` 游标** → 进循环前记 `loopBase = lua_gettop(L)`，每轮结束
  `lua_settop(L, loopBase + 1)`。
* **`lua_next` 的"复制表"惯用法里不能再多弹一次**：`pushvalue(-2); insert(-2); settable(dst);` 之后栈上
  正好剩下 key。
* **Lua C API 的栈不自动扩容**：`readTableList` 把每个条目都留在栈上，条目多了直接写到数组之外（现象是
  `--check` 以 `0xC0000374` heap corruption 崩掉，而且到 `lua_close()` 才报错）→ 压条目之前先
  `lua_checkstack(L, 条目数 + LUA_MINSTACK)`。诊断手法：临时给 `lua_static` 加 `LUA_USE_APICHECK`。
* **`-Werror` 下两个 Win32 小坑**：MinGW 的 `SendInput` 第二参是 `LPINPUT`（非 const）要 `const_cast`；
  `GetProcAddress` 的 `FARPROC` → 具体函数指针会被 `-Wcast-function-type` 报错，用 `std::memcpy` 绕开。
* **`HHOOK` 不是 `HANDLE`**。**`core::Trigger`/`Phase` 在 `core/engine.h`**，不在 `config.h`。
* **`QObject::moveToThread` 拒绝带 parent 的对象。**
* **`emit other->someSignal()` 在类外编译不过**（信号是 `protected`）→ 公开方法 +
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 把 emit 挪回 GUI 线程。
* **`QCOMPARE` 的宏参数里不能出现"不受括号保护"的逗号**（尖括号不算括号）→ 起 `using` 别名。
* **`QCOMPARE(optional<int>, -1)` 是错的**：`-1` 会隐式构造成 engaged 的 optional，"没有值"是
  `std::nullopt`。
* **`QDir::filePath()` 一律用 `/`** → 输出前统一过 `QDir::toNativeSeparators()`。
* **`QWheelEvent` 合成不出来**：`QCoreApplication::sendEvent` 递进去之后 `isAccepted()` 为 false、列表
  一点不动 → 验滚轮只能 `SetCursorPos` + `SendInput`（且弹窗必须是前台窗口）。
* **`WIN32_LEAN_AND_MEAN` 不包含 `ole2.h`** → `CoInitializeEx`/`CoCreateInstance` 要显式
  `#include <objbase.h>`。MinGW 的 `GUID` 可以直接聚合初始化。
* **MinGW 头文件里没有 `MONITOR_OFF`**（`SC_MONITORPOWER` 与 `SMTO_ABORTIFHUNG` 有）。
* **`hresultMessage()` 以前用 `FormatMessageW` 解析 HRESULT，几乎总是 `error 0x…`** → 用
  `hresultText()`：`E_NOINTERFACE` 等几个常见值有人话名字（对虚拟桌面特别重要：`E_NOINTERFACE` 意味着
  版本表选错了 IID）。
* **`RtlGetVersion` 可以从已加载的 `ntdll.dll` 用 `GetModuleHandleW` + `GetProcAddress` 拿**（不用加
  链接依赖）；`GetVersionEx` 会被应用清单骗。

## 构建 / 测试 / 打包

* **`QTEST_MAIN` 的默认输出在本机拿不到**（重定向、管道、`ctest --output-on-failure` 全空，而
  `-o <file>,txt` 能拿到完整结果）→ `cmake/RunQTest.cmake` 让测试先写文件、再由脚本 `cmake -E cat`
  出来。新增测试不用做别的，`flowkeyd_add_test()` 已封好。
* **把 `scripts\release.ps1` 的输出从 agent 的 bash 用 `>` 重定向到文件时，ninja 的中间一段会丢**
  （父 PowerShell 的 host 子进程与孙子 ninja 共享同一个继承句柄、各自推进文件偏移）：**不是脚本吞输出**
  → 判据看退出码、`build/dist-release` 的文件数、`--version` 的哈希。
* **`qt_add_executable()` 默认给测试套 AUTOMOC**，连 `lua_static` 也不例外 → 显式 `AUTOMOC OFF`/
  `AUTOUIC OFF`。
* **`set_tests_properties(... ENVIRONMENT_MODIFICATION)` 的两个修改项必须写在一个 `"a;b"` 字符串里**。
  测试 exe 的 `PATH` 要有 Qt 与 MinGW 的 `bin`，否则 `ctest` 报 `0xc0000135`（DLL not found）。
* **带 qrc 的静态库，资源初始化会被链接器丢掉** → 在被引用的源文件里调一次 `Q_INIT_RESOURCE(name)`
  （必须在全局命名空间）。另一个坑：`qt_add_resources(target path/to/foo.qrc)` 这种"直接传 .qrc"的写法
  在本机 Qt 6.11 上**什么都没生成**；用 `qt_add_resources(target "name" PREFIX "/x" BASE <dir> FILES …)`。
* **写测试用的假 `Evaluator` 不用真的 Lua**：`core::loadConfig()` 把求值回调当参数。
* **`windeployqt` 只会多拷、不会删**：它按 qmldir 里的 `optional import … auto` 把六个 Quick Controls
  样式全拷了 → 想变小只能"拷完再删"（`cmake/PruneRuntime.cmake`）。**`--style` 这个选项本机 6.11 的
  windeployqt 没有。**
* **发布包精简（从 1378 个文件 / 149.8 MB 降到 212 个 / 64 MB）的四类东西**：
  1. 没用到的 Quick Controls 样式（Imagine/Material/Universal/Windows/NativeStyle，连 QML 目录与 DLL）。
  2. `qmltooling/`、`imageformats/{qgif,qjpeg,qsvg}` + `iconengines/qsvgicon` + `Qt6Svg.dll`、
     `networkinformation/`、`generic/qtuiotouch`、`Qt6Quick3DUtils.dll`、`plugins.qmltypes`。
  3. **`opengl32sw.dll` 与 `D3Dcompiler_47.dll` 必须同进同退**（共约 24 MB）：Qt Quick 在 Windows 上走
     D3D11 RHI（没显卡驱动时 WARP 也能画），Win10+ 的 `System32` 本来就有 `d3dcompiler_47.dll`；少了
     D3D 编译器 Qt 会退回 OpenGL，那时才真需要软件回退。失败现象是"弹窗与日志窗口出不来（托盘与快捷键
     还在）"。
  4. **exe 里的调试符号**（`RelWithDebInfo` 带 `-g`，43 MB → 1.6 MB）：release 用
     `objcopy --strip-all`。
  * **FluentWinUI3 的磁盘 .qml/.png 可以整份删掉（省 848 个文件）**：样式插件把整套资源内嵌在自己的
    qrc 里，模块 qmldir 的 `prefer :/qt-project.org/imports/QtQuick/Controls/FluentWinUI3/` 指向那里。
    验证手法：把 DLL 按 **UTF-16** 解码再查 `Config.qml` 这类名字。**`qmldir` 必须留着**，
    `plugins.qmltypes` 可以删。
  * **Basic 与 Fusion 删不得**（实测 `module "QtQuick.Controls.Basic" is not installed`）：
    `QtQuick.Controls` 的 qmldir 有 `default import …Basic auto`，FluentWinUI3 有
    `import …Fusion auto`。两个合起来约 4 MB，当保险留着。
  * **`tls/` 不能整个删**：加了 `Qt6::Network` 之后 https 靠 `tls/qschannelbackend.dll`。只删
    `qopensslbackend.dll` 与 `qcertonlybackend.dll`。
  * **`objcopy --strip-all` 不是幂等的**（每次都重写 PE 头 COFF 的 `TimeDateStamp`）→ dist-release 那一步
    **不再 strip**（源 exe 已经 strip 过）。
  * **改了精简清单必须重新链接**（prune 挂在 `POST_BUILD`）→ `set_property(TARGET flowkeyd APPEND
    PROPERTY LINK_DEPENDS …/cmake/PruneRuntime.cmake)` 已加。
  * **精简过头的及早发现**：五个弹窗启动时就预热，缺模块会立刻报 `could not load ….qml: module "…" is
    not installed`；`scripts/acceptance.ps1` 有一条哨兵检查盯着这个。**日志窗口不在预热里**。
* **`Compress-Archive` / `ZipFile::CreateFromDirectory` 在 PS 5.1 里把 zip 条目名的目录分隔符写成 `\`**
  （Windows 能解，unzip/tar/WSL 会解出一堆名字里带反斜杠的文件）→ 发布脚本的 `New-ZipPackage` 自己用
  `ZipArchive` 逐条写。（`ZipArchive`/`ZipArchiveMode` 在 `System.IO.Compression`，`ZipFile` 在
  `System.IO.Compression.FileSystem`，**两个都要 `Add-Type`**。）
* **发布时"哪个文件进哪个包"用白名单 + 兜底报错**（`scripts/release.ps1` 的 `$SlimFiles` /
  `$DependencyPatterns`）：`dist` 里出现两边都不认识的文件就直接失败。
* **发布说明要自己写，不要指望 `gh --generate-notes`**：仓库直接往 master 提交（不开 PR），
  `--generate-notes` 除了一行 `**Full Changelog**` 什么都给不出来。**正文只有 changelog**；用
  `--notes-file` 交出去；`-Clobber` 重发同一个 tag 时也顺手 `gh release edit` 刷新正文（它**没有**
  `--generate-notes`，整份正文必须自己写全）。
* **PS 5.1 按控制台代码页（GBK）解码原生命令 stdout**：`git log` 里中文提交信息的 UTF-8 字节会被解成
  乱码，而且 **GBK 双字节序列会把 `"\n"` 当尾字节吃掉** —— 表现是 `git log` 少一条提交、两条粘成一行
  （发布说明直接少列一个改动）→ 脚本开头 `[Console]::OutputEncoding = UTF8`（没有控制台时这个赋值会抛，
  包在 try/catch 里）。
* **tar/zip 里的 `README.txt` 用带 BOM 的 UTF-8 写，`.sha256` 不带 BOM**（`sha256sum -c` 认逐字节内容）。

## QML / 界面

* **QML 的 model 角色名不能叫 `text`/`highlighted`/`hovered`**（标准控件自己有这些属性，`required
  property` 撞名就声明不了）→ 用 `line`/`level`、`rowSelected`、`rowArmed`、`rowKey`、
  `windowTitle`/`windowProcess`。委托里高亮写 `highlighted: rowItem.rowSelected`。
* **delegate 里访问外层组件的 id 会报 `Unqualified access`** → 文件开头
  `pragma ComponentBehavior: Bound`。JS 数组模型的 delegate 要写 `required property var modelData`。
* **用 `qmllint -I C:\Qt\6.11.2\mingw_64\qml <file>.qml` 提前查问题**（**不要加 `--bare`**）。QML 类型的
  类名带 id 后缀（`TextField_QMLTYPE_1544`），遍历 item 树找控件要 `contains("TextField")` 而不是相等
  比较。
* **`import QtQuick.Controls.FluentWinUI3` 里确实能用 `Label`。**
* **本机 225% 缩放下 Qt 报出的 `availableGeometry()` 比 `geometry()` 还宽**（工作区从 x=108 开始、宽
  485，屏幕只有 533）→ 居中之后再按屏幕 `geometry()` 夹一次（`app::centrePopup()`，纯函数、有单测）。
* **用 DPI 不感知的 PowerShell 进程 `GetWindowRect` + `PrintWindow` 会拿到错结果**（坐标被虚拟化成逻辑
  像素）→ 让 Qt 自己抓（`QScreen::grabWindow(window->winId())`），或先 `SetProcessDPIAware()`。
* **弹窗缩放过期：预热好的卡片在显示器缩放变过之后只剩一半大。** 根因：五个弹窗窗口启动预热一次就一直
  复用，而它们平时是**隐藏 + 摆在屏幕之外的**；缩放从 100% 改成 200% 时这种窗口收不到 `WM_DPICHANGED`
  （它们不属于任何显示器），Qt 也就不刷新 `devicePixelRatio` —— 窗口的**物理**尺寸停在"逻辑数字"上
  （宽度只有应有的一半）。**注意 `GetDpiForWindow` 照样报正确值**，所以只有真实物理尺寸能看出来。
  修法两层，都在 `PopupHost`：
  1. **屏幕配置一变就丢掉窗口缓存重建**（`discardPopupWindows()` + 重新 `preload()`）：接
     `QGuiApplication::screenAdded`/`screenRemoved` 与每个 `QScreen` 的 `geometryChanged`/
     `availableGeometryChanged`/`logicalDotsPerInchChanged`/`physicalDotsPerInchChanged`，500 ms 去抖
     （一次显示器切换会连着发好几个信号，建一整套五个卡片要几百毫秒）。
  2. **每次弹出前量一遍真实尺寸**：`GetWindowRect` 与「逻辑尺寸 × 这块屏的缩放」不符就丢掉重建。判断是
     纯函数 `app::popupPixelSizeIsStale()`（`tst_menu_model` 盯着），容差几像素是为了无边框窗口的边框 /
     DWM 阴影；拿不到尺寸时不重建。

  重建时**模型必须留着**（启动器的固定 / 最近使用就存在 `AppListModel` 里，`ensureXxxWindow()` 里那几个
  「模型只建一次」的判断就是为它加的；`discardPopupWindows()` 里也要先 `restoreSwitchInputMode()`）。
  回归守卫两处：`tmp/preview/` 的 `checkStalePopupScale()`（锁屏也能跑）与 `scripts/acceptance.ps1` 里
  那一条。
* **`ItemDelegate` 的内边距覆盖不掉**（FluentWinUI3 的 `ItemDelegate.qml` 用绑定给每个实例定内边距）→
  **不要和样式的内边距较劲**：内容区里的东西全部锚在 `contentItem` 上。
* **`QQuickTextInput` 故意忽略 `↑`/`↓`**，所以它们会冒到父项的 `Keys`；而 `Keys` 默认
  `Keys.BeforeItem`，意味着挂在 `TextField` 上的 `Keys.onPressed` 在 `TextInput` 自己的键盘处理之前跑。
* **`Keys.onPressed` 的 `event.key` 与 `event.modifiers` 都是 `int`** → C++ 侧接
  `handleKey(int key, int modifiers)`；改 `Q_INVOKABLE` 签名时要同步改 `tst_app_list_model` 里
  `indexOfMethod("handleKey(int,int)")`（旧签名会直接变 -1）。
* **筛选回写用 `onTextChanged` + 等值判断，不要 `onTextEdited`**（后者在输入法提交/粘贴/拖选时不一定
  发）：`if (field.text !== model.filter)`。
* **`ListView.positionViewAtIndex(..., Contain)` 不能用来"把选中行带进视野"**（它只看列表自己的矩形、
  不看 `topMargin`/`bottomMargin`）；`positionViewAtBeginning()` 更坑（被 `qBound` 夹到**最大位置**）→
  自己算 `HelpModel::scrollTargetY(...)`（纯算术、有单测），QML 只把结果写回 `listView.contentY`。
* **列表刚建好时 `contentY` 会被摆到一个"保持滚动比例"的位置**，而且发生在收到 `selectedChanged`
  **之后**；但**不要**挂在 `onContentHeightChanged` 上（换视图时靠 `onSelectedChanged` /
  `onHeightChanged` / 模型 reset 就够）。
* **`ListView.contentHeight` 在滚动中是"估算值"，会抖**（混合多种行高时每滚一点就变几像素）→
  `onContentHeightChanged` 里不要摆选中行（会把滚轮/拖滑块刚滚出来的位置拽回第 0 行，现象是"有滚动条
  但滚不动"）。行高全都一样时不抖。
* **两个弹窗都是 `WindowStaysOnTopHint`，会互相遮住**；`grabWindow` 抓的是屏幕那块区域。
* **`QWindow::setProperty("visible", …)` 是隐藏/显示一个 QML `Window` 的最省事办法**（窗口不销毁、可
  复用）。**`Keys.onPressed` 只在窗口是活动窗口时才把事件交给有焦点的 item。**
* **进程内预览里 `console.log` 不一定看得见** → 用"写一个能从 C++ 读回来的属性"调试。
* **`GetWindowRect` + `SetCursorPos` 必须同一种像素**（脚本开头先 `SetProcessDPIAware()`）。弹窗几何靠
  **宽度反推缩放**（menu 卡片 300、help 卡片 500 逻辑像素）比猜 DPI 稳。
* **「同一行有多种形状」的列表**（启动器的表头/网格行/整行按钮/分组列表行）：在**一个** `ListView` 的
  委托里按 `rowKind` 把几个**内联组件**（`component HeaderRow: Label { … }` 等）都实例化出来、用
  `visible:` 开关。**不用 `Loader`**：`pragma ComponentBehavior: Bound` 下 `Loader` 装出来的 item 拿不到
  委托自己的属性。四种形状都建出来也不贵（`ListView` 只给看得见的十来行建委托）。内联组件可以直接用
  文件根对象的 id。
* **内联组件的属性名不能撞基类自己的**：`component HeaderRow: Label` 里想接文字就得叫 `label`（`text`
  是 `Label` 自己的）；模型的角色名也不能叫 `highlighted`/`text`。
* **`Space` 会和标准 `TextField` 抢**（`Keys.BeforeItem`）→ **只在筛选串为空时截走它**（返回
  `handled: false` 放行），否则用户打不出 `visual studio`。
* **`ListView` 的行高与卡片高度是两回事**：卡片高度**恒定** = `maxRows × cellHeight` + 上下占位
  （`AppListModel::relayout()`），行高是每种行形状自己的事（表头 26 / 格子 88 / 整行按钮 40 / 分组列表
  44）—— 不要把「各行高度之和」当卡片高度（那样只固定一两条时卡片会缩成 248 高）。模型侧断言
  `cardHeight()`，外面用 `WindowRect` 看真实高度。
* **`Q_PROPERTY` 写自定义结构体是静默失效的**：`Q_PROPERTY(PopupRect filterRect …)` 时 QML 那边
  `filterRect.x` 全是 `undefined`（筛选框、底部提示、整个网格全堆在左上角，**没有一条 QML 警告**）→
  属性类型必须写成 `QRect`（`PopupRect` 有隐式转换）。`qmllint` 与单测都发现不了，只能抓图看。

### 图形化配置界面

* **`Row`（定位器）+ 子项锚点当 `ItemDelegate.contentItem` 会直接崩**（SIGSEGV、一条警告都不打：`Row`
  的 `implicitWidth` 又要拿回委托自己的宽度去算，绕回了布局引擎）→ **凡是给标准控件做内容区的地方一律
  用布局，不在定位器里给子项加锚点**（`EntryRow`、`ActionEditor`、`ActionListEditor`、`AppOverlay`、
  `ChordsEditor` 共用一套写法）；需要垂直居中用 `RowLayout` 的 `Layout.alignment: Qt.AlignVCenter`。
  诊断：把可疑的那一小段拆成单独的小 QML + `qml.exe` 跑一遍，看退出码（`139` = 崩）。
* **给弹层塞数据必须在切 `overlayKind` 之前**：`overlayKind` 一变，`Loader` 就同步把弹层建出来并跑
  `onLoaded` 里的赋值；顺序反了弹层拿到上一次（首次是空）的条目，界面看起来完全正常，但「确定」写回去
  的是一个空条目，而且静默。所以只留一个入口 `showOverlay()`，顺序写在里面。
* **`Component.onCompleted` 里读不到还没挂上来的东西**：下拉框一开始用
  `Component.onCompleted: currentIndex = options.indexOf(value)`，如果控件是在 `editor`/数据挂上来之前
  建好的（弹层里、`Loader` 装出来的动作编辑器里），`options` 还是空数组 → `currentIndex` 被定死在 0，
  **界面显示的取值与真实值不一样**（`trigger = "release"` 显示成「按下时」），一声不响。改成
  `currentIndex:` 绑定。
* **不可见的那一段的绑定也会被求值**：`run.args` 是字符串列表、`open.args` 是**一个字符串**，编辑
  `open` 动作时 `run` 那段虽然 `visible: false`，`text: linesOf(action.args)` 照样跑 →
  `"…".join is not a function`。同名但类型不同的字段，取值辅助函数都要先 `Array.isArray()`。
* **信号处理器的隐式参数已弃用**：`onPicked: root.action.op = value` 能工作但打
  `Parameter "value" is not declared` 警告，还与同名属性（`BoolRow.value`）撞名 → 写
  `onPicked: function (value) { … }`（单表达式处理器 `onEdited: f(value)` 必须改）。
* **验证这套界面不需要编译 C++**：`tmp/` 里放一份「假模型」（`QtObject`，属性/方法名与
  `app::ConfigEditor` 一样）+ 一个把 `ConfigWindow` 包一层的 `Preview.qml`，`qml.exe Preview.qml` 就能
  跑起整套界面（**记得 `QT_ASSUME_STDERR_HAS_CONSOLE=1`**：GUI 子系统的 `qml.exe` 不打控制台，不设它
  看到的是"什么都没输出"）。配一个定时器逐页切、逐弹层开、`contentItem.grabToImage()` 抓图存 PNG
  （**图要人眼看过**）。`src/qml` 改了文件就拷一份过去；目录里再放一个 `qmldir`（一行
  `singleton ConfigText 1.0 ConfigText.qml`）才认得单例。想验**模块装载**那条路就把
  `build/<preset>/Flowkeyd` 整个拷到临时目录、删掉 qmldir 里 `prefer :/qt/qml/…` 那一行、用 `-I` 指
  过去。
* **只想验某一个表单组件**（例如 `ActionEditor` 的某个动作分支或 `LaunchFieldsEditor`）时不必造假模型：
  `editor: null` 就够了（代码里到处写着 `root.editor ? … : []`）—— 把每种动作各建一个 `ActionEditor`、
  让它跑两秒，再看 stderr 里有没有 `TypeError` / `is not a function` / 绑定警告（`console.log` 也走
  stderr，所以要 `QT_ASSUME_STDERR_HAS_CONSOLE=1`）。
* **两个 `launch` 写法别只给一个写界面**：`window` 动作的 `launch` 与 `app{}` 的 `launch` 字段一样，但
  `ActionEditor.qml` 一度是手写的几个格子（只有 `program`/`args`/`cwd`/`wait_ms`），于是
  `show`/`shell`/`env` 在界面里**改不了**（能原样保留、看不见也改不动，一声不响）→ 两者共用
  `LaunchFieldsEditor`（`showWaitMs: false`）。**同一份字段块只在一个组件里实现**（`WindowRuleFields` /
  `HotkeyFields` 同理）。
* **`remap{}` 的 `name` / `enabled` 一度没写进 README**（配置认、界面认、`--list` 也打，只有 README 的
  `remap{}` 一节漏了）。对照三处清单（工作约定第 13 条）时别只查"代码里有没有"，要按"每一节自己的字段
  表"逐行核对 —— 这两个名字在 `hotkey{}` 的表格与示例配置里都出现过，grep 字段名是对不出来的。

## Windows 领域坑

* **开始菜单「所有应用」= `shell:AppsFolder`**（`Get-StartApps` 也是枚举它）。两个
  `Start Menu\Programs` 目录里**只有 `.lnk`**：商店/UWP 应用根本没有 `.lnk`（它们注册在包清单里），
  `.msc` 系统工具其实有 `.lnk`、但目标是 `.msc` 而不是 `.exe`。所以「目标必须是 `.exe`」那道旧过滤会
  **静默丢掉**计算器、记事本、终端、服务、事件查看器、计算机管理……
* **控制面板的「所有控制面板项」= `SHGetKnownFolderIDList(FOLDERID_ControlPanelFolder)`**
  （`{82A74AEB-AEB4-465C-A014-D097EE346D63}`）：真机解析出来就是
  `::{26EE0668-A00A-44D7-9371-BEB064C98683}\0`。**别拿 `{26EE0668-…}` 本身去枚举**（那是 8 个分类的
  分类视图）。另外「网络连接」是 delegate folder，不在枚举结果里，要单独按解析名补。
* **控制面板项的启动名必须验证回程**：`SIGDN_DESKTOPABSOLUTEPARSING` 有时给的是带**本地化显示名**的
  路径（真机「字体」→ `::{26EE0668-…}\0\字体`），`SHParseDisplayName` 解不回去 → 采用前先自己解析一次
  （失败退回 `SIGDN_FILESYSPATH`，两个都不行就丢掉）。还有条目把解析名当显示名给回来，也丢掉。
* **`ms-settings:` 页面在 shell 里没有名字也没有图标**：`SHParseDisplayName` 解析得了（启动照常交给
  shell），但 `SIGDN_NORMALDISPLAY` 返回的就是 URI 本身、`GetImage` 也拿不到像样的图标 → 名字自带一份
  中英对照、图标借「设置」应用那一张（`core::appIconLaunchName()`）。
* **往启动器的目录里加来源时别忘了「同名去重」**：三份来源之间真的会撞名（真机「Windows 工具」）→
  `core::appendLauncherEntries()`（开始菜单那份在前，所以保留它）。
* **AppsFolder 条目的属性要自己读**：`SIGDN_NORMALDISPLAY` + `PKEY_AppUserModel_ID` /
  `PKEY_Link_TargetParsingPath` / `PKEY_Link_Arguments`。AUMID 有三种形状：`<包家族名>!<AppId>`（UWP）、
  显式 AUMID（`Chrome`、`MSEdge`）、**目标路径当 AUMID**（scoop / Qt 那些没设过 AppUserModelID 的）；
  `.msc` 那些是 `Microsoft.AutoGenerated.{GUID}`。目标可能是 `{已知文件夹 GUID}\相对\路径.exe` 这种
  shell 写法 → **只能看末尾扩展名**，不能拿去 `QFileInfo`。**用 `propkey.h` 里的 `PKEY_*` 要引 MinGW 的
  `libuuid`**，所以 GUID/PID 手写在 `apps.cpp` 里。
* **`shell:AppsFolder\<AUMID>` 一个字符串能干三件事**（真机逐个验过）：启动
  （`ShellExecuteExW` 把解析名当 `lpFile`，或 `SHParseDisplayName` + `SEE_MASK_IDLIST`）、取图标
  （`SHCreateItemFromParsingName` + `GetImage`）、当右键菜单的输入（UWP / `.msc` / `AutoGenerated` /
  `::{…}` 都给出「打开 …」这类正常菜单）。
* **名字变成显示名之后就是中文了，目录顺序不再等于显示顺序**：`core::prepareAppEntries()` 按码点排，
  中文全落在拉丁名字后面；给用户看的顺序是 `core::appSortText()`（拼音）。
* **图标键（= 固定/最近使用的身份）从「`.lnk` 路径」换成「启动名」之后，`launcher.json` 会整份失配**
  （只是丢掉固定/最近，不报错）。而且只靠目标路径当 AUMID 的那些程序（scoop 工具、Qt 多版本工具）在换
  版本/换路径后会变成一条新条目 —— 已写进 README 的已知限制。
* **验收脚本里那个「单字母筛选串只命中 1..8 个程序」的前提很紧**：条目涨到约 200 之后真机上 26 个字母
  里可能一个都不满足（单字母命中量都在十几到上百）→ 凑不到时脚本打诊断并**跳过**那 3 条数字键检查。
* **`SetTimer(nullptr, id, …)` 会忽略 `id` 并返回一个新的定时器 id**（`WM_TIMER` 的 `wParam` 就是它）
  —— 这曾让 `Engine::tick()` 从未被调用（长按重复一直是坏的）→ 把返回值存进成员再比较，`KillTimer`
  也用返回值。
* **`SetForegroundWindow` 除非持有前台锁否则被拒**（递进式绕行 + `AttachThreadInput` 配平）。**而且它在
  "目标已经是 shell 的前台窗口，只是不在当前虚拟桌面上"时是空操作**（返回 TRUE 却什么都不发生）——
  被 `MoveViewToDesktop` 搬走的窗口就是这个状态，只能先显式 `SwitchDesktop`。
* **`GetForegroundWindow()` 不等于"用户看得见的窗口"**：窗口被搬到别的虚拟桌面之后 shell 还把它当前台
  窗口（而公开的 `IsWindowOnCurrentVirtualDesktop` 返回 FALSE）。
* **`IVirtualDesktopManagerInternal::MoveViewToDesktop` 在 vtable 下标 4**（三种布局一致），需要先用
  `IApplicationViewCollection::GetViewForHwnd`（IID/SID 都是 `{1841C6D7-4F9D-42C0-AF41-8747538F10E5}`，
  下标 6）把 `HWND` 换成 `IApplicationView*`。**已公开的 `IVirtualDesktopManager::MoveWindowToDesktop`
  拒绝移动别的进程的窗口。**
* **`MoveViewToDesktop` 返回 S_OK 不等于窗口真的换了桌面**：用**已公开**的
  `IVirtualDesktopManager::GetWindowDesktopId`（`CLSID_VirtualDesktopManager` `{AA509086-…}`，IID
  `{A5CD92FF-…}`，下标 4）验证；`IsWindowOnCurrentVirtualDesktop`（下标 3）也能用，但 shell 是**异步**
  移动的，要轮询等它生效。
* **`IVirtualDesktop::GetID` 在 vtable 下标 4**（先 `IsViewVisible`（3）），用来把窗口的
  `GetWindowDesktopId` 对到内部枚举的桌面上；**拿"有且只有一个匹配"当自检**。
* **`IVirtualDesktopPinnedApps` 的 IID 自 Win10 起就没变过**（SID
  `{B5A399E7-1C87-46B8-88E9-FC5747B171BD}`、IID `{4CE81583-1E4C-4632-A621-07A53543148F}`，vtable
  `3 IsAppIdPinned / 4 PinAppID / 5 UnpinAppID / 6 IsViewPinned / 7 PinView / 8 UnpinView`），所以
  **不进版本表**。`PinView` 返回 S_OK 本身证明不了什么，要用 `IsViewPinned` 从外面确认状态变了。
* **虚拟桌面接口表的 `build.revision` 是"起始版本"不是上限**（取"生效版本不高于当前系统的最后一条"）；
  修订号用 `RegGetValueW` 从注册表读 `UBR`。
* **"另一个桌面上的窗口"是被 DWM cloak 掉的**，但**不能单独用**：`cloaked != 0` +
  `IsWindowOnCurrentVirtualDesktop == FALSE` → 在别的虚拟桌面上（**要留**）；`cloaked != 0` + `== TRUE`
  → shell 藏在当前桌面上的假窗口（**排掉**）。两个 `DWM_CLOAKED_SHELL(2)` 在数值上分不开，只能问一句
  虚拟桌面。
* **`WS_EX_TOPMOST` 只能靠 `SetWindowPos` 设，不能靠 `SetWindowLongPtr`**（后者会返回 TRUE 却什么都没
  发生）。
* **新建窗口后立刻置顶可能无效**（shell 还没登记它：`GetWindowDesktopId` 给全零 GUID、
  `IsWindowOnCurrentVirtualDesktop` 报 FALSE）→ 先对它做一次窗口操作（`applyPlacement`）再置顶。产品
  路径不会碰到。因此 `desktop::isWindowOnCurrentDesktop` 把"不属于任何虚拟桌面"当成 `std::nullopt`
  （不知道）。
* **有属主的窗口（对话框/工具提示/弹出菜单）永远不是用户想要的那个窗口**。"主窗口"判据还必须带上
  **非 `WS_EX_TOOLWINDOW`** 与**有标题**：按 `process` 匹配会一次命中一堆内部窗口（`Non Client Input
  Sink Window`、无标题的 `NotepadTextBox`）。`WS_EX_APPWINDOW` 那一条照任务栏/Alt+Tab 的规则来（"无属主
  **或**带 `WS_EX_APPWINDOW`"）。
* **`core::windowProcessMatches()` 里的空 needle 是「不限制」、返回 true** → 任何拿它做名单匹配的地方
  都**不能**把空串当普通条目：远程桌面名单里混进一个 `""` 会让**所有**前台窗口都算远程桌面、快捷键整片
  失效。修法两层：加载时拒绝空条目（`settings.remote_desktop.processes[%1]`），运行时的
  `isRemoteDesktopProcess()` 也 `continue` 跳过。
* **`SetWindowPlacement` 是跨显示器摆放的关键**：`IsZoomed` 就先 `SW_RESTORE`，然后写
  `WINDOWPLACEMENT.rcNormalPosition`（屏幕坐标）并设 `showCmd`，最后对非最大化的情况再补一次
  `SetWindowPos`（`SWP_NOACTIVATE`）。最小化的窗口只更新"还原位置"。它不会抢焦点。
* **`DWMWA_TRANSITIONS_FORCEDISABLED` 读不回来**，只能"设 TRUE → `ShowWindow` → 设回 FALSE"
  （RAII `TransitionGuard`）；DWM 在过渡**开始**时读它。
* **`MonitorFromWindow()` 对最小化的窗口不可靠**：它的 "几何" 是 `-32000`，`MONITOR_DEFAULTTONEAREST`
  于是会落到一块与它无关的屏上（本机实测：窗口停在第二块屏上最小化，却被算成第一块）。要判"它在
  哪块屏"就先用 `GetWindowPlacement().rcNormalPosition` 的中心点问 `MonitorFromPoint()`（现在
  `win::monitor::indexForWindow()` 就是这么做的，窗口轮转与 `move_left/right_monitor` 都靠它）。
* **终端窗口不能靠标题找**（`process = "wezterm"`，窗口属于 `wezterm-gui.exe`）。
* **`run`/`window.launch` 走 `CreateProcess`、不查 `App Paths`、也不能用 `DETACHED_PROCESS`**（会静默
  杀死控制台子进程，用 `CREATE_NO_WINDOW`）。
* **启动的子进程默认不再继承管理员令牌**（第 35 条）三个真机踩过的点：
  * **`CreateProcessAsUser` 用不了**：要 `SeAssignPrimaryTokenPrivilege`，提权管理员令牌里没这条
    （`AdjustTokenPrivileges` 报 1300、`CreateProcessAsUser` 报 1314）。`CreateProcessWithTokenW` 只要
    `SeImpersonatePrivilege`（提权管理员默认就有），代价是经过 Secondary Logon 服务。
  * **不能同时传 `lpApplicationName` 与 `lpFile`**：那样子进程的 `argv[0]` 变成 exe 路径、**原来第一个
    参数被吞掉**（`--shell-open <路径>` 会变成 `argv[1] = <路径>`……看起来"什么都没发生"）→ 只传
    `lpApplicationName = nullptr` + 完整命令行。
  * **`CreateProcessWithTokenW` 会丢掉我们的环境块**（代理进程里 `PATH` 没了，于是子进程找不到
    `flowkeyd.exe` 旁边的 Qt DLL，报「找不到 Qt6Core.dll」）→ 必须显式给 `lpEnvironment`
    （`buildEnvironmentBlock({})` + `CREATE_UNICODE_ENVIRONMENT`）。`runCommand()` 因此**总是**显式传
    环境块。
* **代理进程 = 我们自己的 exe**，所以从**别的可执行文件**（例如 `tst_interactive.exe`）里调用时要显式
  告诉它用哪个 exe（`runShellHelperAsUser(request, helperExecutable)`）；默认 `currentExecutablePath()`。
  不提供这个口的后果曾经是：测试进程把测试自己又启动了一遍，在那条路径上它是 MTA、没控制台，于是弹一个
  「找不到 Qt6Test.dll」。
* **从提权的进程里 `ShellExecuteW` 开出来的东西也是提权的**（子进程 `whoami /groups` 仍报
  `S-1-16-12288`）。这就是 `open` / 程序启动器必须绕一次代理进程的原因。
* **中文错误文案不可断言**（`FormatMessageW` 是本地化的）—— 这也是日志与校验信息保持英文的实际原因。
* **`IShellItemImageFactory::GetImage()` 在 `.lnk` 上很慢**（每个图标约 3 ms，首次冷启动可达 148 ms）
  → 只能在"按需 + 缓存 + 异步"的路径上用。它给回来的 `HBITMAP` 是 32 位**直通 alpha（不是预乘）**、
  **自顶向下**的 BGRA（实测有 `RGB > alpha` 的像素，预乘的数据不可能出现），对应
  `QImage::Format_ARGB32`，`GetDIBits` 时必须给**负的** `biHeight`。
* **`CreateIconIndirect(hbmMask = nullptr)` 会失败**（即使 hbmColor 是 32 位带 alpha）：文档说 mask
  "被忽略"，但它仍然要一个有效的位图句柄 → 本项目直接读 DIB 像素（代价是 `flowkeyd_platform` 多静态链
  一个 `gdi32`）。
* **快捷方式的名字不能用 shell 那两套**：`SHGFI_DISPLAYNAME` 对一半的 `.lnk` 返回空串或被截断的值
  （真机 `iSCSI Initiator` → `iSCSI`、两个 ODBC 都变成 `ODBC`），`IShellLink::GetDescription` 是冗长
  提示语。**用文件名去掉 `.lnk`**。
* **`IShellLink::GetPath` 可能返回 `%windir%\system32\...` 这种没展开的路径** → 去重前先
  `ExpandEnvironmentStringsW`。
* **输入法（IMM32/TSF）的转换模式：别的应用不受影响，但本进程的几个窗口互相看得见**（在一个窗口里按
  `Shift` 切中文，另一个窗口读到的也带 `NATIVE` 位）。各窗口报出的其余标志位不一致 —— **只有 `NATIVE`
  （0x1）那一位有意义，还原时整份照抄**。从另一个进程去 `ImmGetContext` 拿不到上下文（连
  `AttachThreadInput` 也救不回来），只能在进程内验证。
* **`windows.h` 的 `DELETE` 宏与 `core/keys.h` 的 `Vk DELETE` 撞名**：把任何会拉进 `windows.h` 的头
  加进被 `main.cpp` 包含的头里就会炸（`expected unqualified-id before numeric constant`）→ 平台头只
  放在 `.cpp` 里。
* **`.arg(标题, 句柄)` 会把句柄当成字段宽度**（Qt 6 有 `arg(const QString &, int, QChar)` 重载）：一条
  日志会变成 132 KB → 两个参数的 `.arg()` 里只要有一个不是 `QString`，就先转成字符串（本项目用
  `handleText(hwnd)`）。
* **`QDate::fromString(text, "yy-MM-dd")` 的两位年份是启发式的**（`26` 解成 **1926**）→ 解析自己的输出
  必须自己拼年份。
* **Windows 上"正在运行的 exe"可以改名、不可以删/覆盖**（在线更新靠这个把旧 exe 改成 `<exe>.old`；`.old`
  只能由**新进程**删；`MoveFileExW` 不能跨卷）。运行中的 exe 被锁时 release 全量构建会卡在链接
  （`cannot open output file flowkeyd.exe: Permission denied`）→ 常规解法 `--quit`；实在停不掉时可以把
  被锁的 exe `Move-Item` 成 `*.locked`（内核映像按区域映射），但那之后常驻跑的是旧构建，要告知用户。
* **提权实例被强杀会留下幽灵托盘图标**（explorer 不会马上发现进程没了）→ 优先 `--quit`。
* **`--quit` 的事件名按"传进去的配置路径字符串"命名** → 停一个开发实例必须用与启动时**同一个拼写**
  （脚本里一律传绝对路径）。
* **完整性级别的 "no write up"**：提权守护进程建的事件默认带 High 标签，非提权的 `--quit` 会吃
  `ERROR_ACCESS_DENIED` → 用 SDDL `D:(A;;GA;;;<用户 SID>)S:(ML;;NW;;;LW)` 把强制标签压到 **Low**（互斥
  体同理，为了让非提权进程能 `OpenMutexW`）。
* **`--check` 不带 `--config` 读的是真实配置**（`%USERPROFILE%\.config\flowkeyd\`）→ 验收/接管脚本里
  一律显式写 `--config`。
* **`QCoreApplication::applicationDirPath()` 在没有 `QApplication` 时会警告并返回空串** —— 它在
  `core::configPathCandidates()` / `legacyTomlCandidates()` 里被调用，而这两条都在 `QApplication` 之前
  跑，于是**文档里写的"exe 同目录"候选实际是失效的**（`exeDir` 是空串，直接跳过）。已知小缺陷、**没改**；
  要改就得给 core 一个不依赖 Qt 实例的 exe 目录来源（例如 `core::setExeDirectory()`）。
* **`SetWinEventHook` 连子窗口一起报，`EnumWindows` 不给子窗口**：Chrome 的
  `Chrome_RenderWidgetHostHWND`（标题就是 `Chrome Legacy Window`）无属主、非 `WS_EX_TOOLWINDOW`、
  有标题、有尺寸，光看那几项会把它当成一个**程序窗口**（`window()` 匹配会挑中它，窗口切换器也会
  列出来）。判据里必须加 **`WS_CHILD` → 不是主窗口**（`TopLevelWindowFacts.child`，`isMainWindow()`
  第一句就拒掉）。这一条同时管住 `window()` 匹配与窗口切换器。

## 原生 shell 右键菜单（`platform/win/shell_menu`）

* **`TrackPopupMenuEx` 弹的是 shell 生成的菜单，所以菜单消息必须转给 `IContextMenu2/3`**：`HMENU` 里可能
  有子菜单与自绘条目，系统会把 `WM_INITMENUPOPUP`/`WM_DRAWITEM`/`WM_MEASUREITEM`/`WM_MENUCHAR` 发给
  **菜单拥有窗口**（我们的卡片，一个 Qt 窗口）。不转发的话子菜单展开是空的、图标不画、字母加速键不
  生效 → 菜单开着的那一小段时间用 `SetWindowLongPtrW(GWLP_WNDPROC)` 临时换掉它的窗口过程（弹菜单这个
  调用是阻塞的，一条线程同时只有一份 `thread_local` 就够；**别拿 `GWLP_USERDATA` 存自己那份 router**）。
* **命令号是「偏移」，不是绝对 id**：`QueryContextMenu(hmenu, 0, idCmdFirst=1, idCmdLast=0x7FFF, …)` 之后
  `TrackPopupMenuEx(… TPM_RETURNCMD | TPM_NONOTIFY)` 返回的号减掉 `idCmdFirst` 才是
  `CMINVOKECOMMANDINFOEX::lpVerb`（`MAKEINTRESOURCEA(offset)` + `CMIC_MASK_UNICODE` 再填一份
  `lpVerbW`）。返回 0 = 取消。
* **想要「以管理员身份运行」就得加 `CMF_EXTENDEDVERBS`**（资源管理器里 Shift+右键才多出来的那批）。
* **坐标必须是物理像素**（QML 给的 `mapToGlobal` 是逻辑像素）→ 平台层用 `GetCursorPos` 自己取。
* **菜单的拥有窗口必须是前台窗口**（否则"点菜单外面就关掉"失效）：调用前 `SetForegroundWindow(owner)`，
  回来之后补 `PostMessageW(owner, WM_NULL, 0, 0)`。
* **不要在 shell 菜单外面套 DPI 上下文。** 照「宿主 shell 菜单」的建议把线程临时降级成
  `DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED` 会让菜单**高一倍**：shell 给 owner-draw 条目量尺寸用的是
  **拥有窗口的 DPI**（PMv2、192），那个上下文又把整张菜单按 96→192 再缩一遍。本进程（Qt 6）本来就是
  Per-Monitor V2，**win32k 对 PMv2 线程弹的菜单按显示器逐个缩放**，什么都不用做。测量手法见
  `tmp/dpimenu/`（锁屏也能量：向菜单窗口 `PostMessage(WM_KEYDOWN, VK_ESCAPE)` 收掉它）。回归守卫：
  `tmp/preview/` 的 `checkAppsMenu()` 用 `MN_GETHMENU`(0x01E1) 拿回 `HMENU`、逐条 `GetMenuItemRect`
  断言每条约 22 逻辑像素（44 物理，与资源管理器一致）。
* **"属性"这类命令开出来的对话框以 `hwndOwner`（我们的卡片）为属主** → 收卡片必须发生在
  `InvokeCommand` **之前**（`showItemMenu()` 的 `beforeInvoke` 回调就是干这个的），否则
  `setProperty("visible", false)` 会把对话框一起藏掉。
* **`IContextMenu::InvokeCommand` 里的"属性"/"打开方式"在调用方线程上跑模态循环**（`PropertySheet`），
  所以这个调用可能阻塞 GUI 线程到用户关掉对话框为止；弹菜单本身也是阻塞的。这期间 Qt 的事件队列不跑
  （Win32 消息照样派发），别指望那时还有定时器 / 队列回调生效。
* **锁屏 / 会话没真正接收输入时，注入测试全是假的**：`SendInput` 会返回成功（`GetLastError` 也是陈旧
  的），但鼠标不会动、窗口拿不到前台、`CopyFromScreen` 报"句柄无效"。判别方法（不需要人看屏幕）：
  `GetForegroundWindow() == 0`，或者注入一次 1 px 的 `MOUSEEVENTF_MOVE` 再 `GetCursorPos` 看它有没有
  真的动。

## 测试 / 验证手法

* **验收脚本突然"奇慢"（十几分钟才 100 多项）先查前台是不是 `msrdc.exe`**：WSLg 的 RDP 客户端窗口
  一旦当前台，验收实例就把整个会话当成远程桌面（决策 26 的放行）—— **所有注入的按键都被放行 /
  忽略**，每个等待都只能超时。实测：同一份 release 二进制，被它干扰时 134/206 花了 30 分钟且
  没跑完（0 失败），`Start-Process` 重拉一次不到 3 分钟就跑完 206/0。另外**把验收脚本用
  `Start-Process` 拉成独立进程**，不要把它的 stdout 直接挂在 agent 的 shell 上：那个 shell 一超时，
  父进程被杀而子进程还在跑，看起来就像"卡住了"。
* **"物理按键"可以自动化，但必须先加测试后门**：钩子照规矩丢弃一切 `LLKHF_INJECTED` 事件 →
  `FLOWKEYD_ACCEPT_INJECTED=1` 抬升那道过滤，而且**只改钩子给 `event.injected` 赋值的那一步**。启用时
  打一条警告。
* **"只有显示器配置变了才会出现"的弹窗问题可以在进程内复现**：把那个（隐藏的）弹窗窗口 `SetWindowPos`
  成「逻辑尺寸当作物理像素」的大小，再正常弹出一次 —— 产品会发现尺寸不对、丢掉重建（`tmp/preview/` 的
  `checkStalePopupScale()`）。断言时**给旧窗口打个 `setProperty` 标记**：标记不在了才能证明窗口真的换了
  一个。
* **`$form.Activate()` 会静默失败**（前台锁只允许"当前就在前台的那个进程"抢焦点）→ 验收脚本照抄守护
  进程自己的 `raiseWindow`（`AttachThreadInput` → `SetForegroundWindow` → 解挂），真抢不到焦点时要
  **明确报一条失败**。弹窗复用之后 `window->isActive()` 可能是陈旧的 `true` → 再问一句
  `GetForegroundWindow() == hwnd`。
* **`Win+F16` 在本机是外壳快捷键**（会拉出"滑动以关机"）→ 选测试和弦之前先用一个不装钩子的小脚本探一下
  它会不会动外壳。本项目用 `Win+S`（失败代价只是弹个搜索框）。
* **注入鼠标的脚本里，凡是用绝对坐标就必须先把光标放到一个确定的起点**（多显示器下尤其如此）。
* **要验证弹窗链路又不想真的按键盘**：做一个**临时预览程序**（`tmp/preview/`：自己的 `CMakeLists.txt` +
  `main.cpp`，`file(GLOB)` 拉进 `src/core`、`src/platform/win`、模型与 `popup_host.cpp`，再用
  `qt_add_qml_module` 注册同样的模块）：直接调 `PopupHost::requestMenu/requestHelp`，用
  `QCoreApplication::sendEvent(window, &QKeyEvent(...))` 模拟键盘，再用 `QScreen::grabWindow` 截图。
  **这条路径能验证除"真实按键"之外的一切**，锁屏也能跑。注意预览工具自己会报一堆 `ItemDelegate.qml` 的
  `TypeError`（用官方 Qt 跑也一样），那是**预览工具的环境问题**，产品日志里是 0 条。
* **这套预览工具是验"分段 + 多种行形状"界面的主力**：`checkApps()` 会真的扫开始菜单、真的让异步图标
  回来、用 `QTest::keyClick` 走 `Space`/`End`/`Enter`/`Esc`，**断言模型状态与窗口几何**
  （`pinnedCount()` / `allMode()` / `cardHeight()` / 标题里的个数），截图只是给人眼看的那一眼。它跑在
  进程内，锁屏 / RDP 不接收输入时照样能跑。方法变了就直接改它（它是 `tmp/` 里的一次性工具）。
* **"一闪而过"的 bug 怎么查**：用 `Graphics.CopyFromScreen` 抓弹窗那块的屏幕区域
  （`QQuickWindow::grabWindow()` 看不到合成器的中间帧），每帧算 FNV 指纹，~120 fps，打印"与上一帧不同"
  的帧号；把帧存成 PNG 用 `imgdiff` 打印"不同像素数 + 包围盒"。注入 `MOUSEEVENTF_WHEEL` 之后**再注入
  1 px 的 `MOUSEEVENTF_MOVE`**（真鼠标滚轮几乎总会带一点位移），而且**弹窗必须是前台窗口**。
* **「滚轮真的滚了没有」从外面只有一个可观察量：像素**：`[FlowInject]::RegionHash(x,y,w,h)`（FNV-1a）
  只抓列表左侧一条竖带、前后比对（避开右边滚动条与光标那一列）。锁屏时 `CopyFromScreen` 会抛、
  `RegionHash` 吃掉异常返回 0（前后必然相等 → 报失败，那种会话本来就不该跑验收）。
* **锁屏时"看渲染"只剩一条路**：`QScreen::grabWindow()`/`CopyFromScreen` 报"句柄无效"（黑图），但
  **`QQuickWindow::grabWindow()`（进程内渲染）照常可用**，`QTest::keyClick` 也照常可用。注意锁屏时委托
  是逐帧惰性创建、推不推帧说不准 → "到底建了多少个格"这种断言不要写死。
* **从外面看见托盘图标变了**：`SetCursorPos` + 一次**相对**的 `mouse_event(MOUSEEVENTF_MOVE)` 把光标推到
  屏幕边缘，自动隐藏的任务栏才会滑出来；然后 `CopyFromScreen` 整条任务栏、像素差求包围盒。新起的实例
  图标会落进「隐藏的图标」溢出弹窗 → 要看真实效果就重启常驻实例。
* **本机任务栏是自动隐藏的竖条**（`Shell_TrayWnd` 是 `0,0,96,2160`）→ 抓图坐标要对齐这个。
* **需要真实桌面的验证要做成"默认 skip 的交互式单测"**（`tst_interactive` +
  `FLOWKEYD_ALLOW_INTERACTIVE_TESTS=1`）：比写一个只跑一次的临时程序好，能反复验证，而且不会在 CI 里碰
  用户桌面。
* **本机的记事本变成了带标签页、单实例、带会话恢复的 Store 应用**（`WM_CLOSE` 会弹确认框、给文件参数
  只是加标签页、标题可能停在别的文件上）→ "窗口后端"的断言改用**测试进程自己的**顶层窗口
  （`tests/tst_interactive.cpp` 的 `TestWindow`：自己 `RegisterClassExW` + `CreateWindowExW`，标题可控、
  进程独占、`WM_CLOSE` 就是 `DestroyWindow`；等消息用只抽自己窗口消息的 `pump()`）。记事本只剩两个用途：
  覆盖"启动一个真程序 + 按标题找到它的窗口"，以及需要一个真能 Ctrl+C 的编辑器。
* **本地假 GitHub 是验证在线更新最好用的手法**（`tmp/e2e-local.ps1` + `tmp/e2e-http-server.ps1`，临时改
  `latestReleaseApiUrl()` 指向它，测完就删）：用裸 `TcpListener`（`HttpListener` 要 URL ACL）发一份
  `releases/latest` 的 JSON 和一个装着自家新 exe 的 slim zip，让一个从 `build/dist-release` 拷出来的
  沙箱实例自己走完「检查 → 下载 → sha256 → 解压 → 换名 → 重启」。沙箱 exe 要用**带临时钩子**的那份
  （否则没人点菜单它不会自己检查），zip 里的"新版本"要用**不带钩子**的那份（否则无限循环）。同样的手法
  也能验 `install.ps1`。
* **`--quit` 成功与否别只看退出码，也别只看那一瞬间的进程表**（常驻已经开始关闭时 `Get-Process` 仍
  可能看得到它）→ 可靠判据是 stdio 里那句 `flowkeyd: <path> exited` 加稍后为空。收尾脚本要**轮询**等它
  真的消失，还有残留就 abort（不构建也不重启）。
* **`Start-Process` 起的后台进程会把继承的 stdout/stderr 一直握着**，让调用方一直等 → 起后台服务就把
  两个流都重定向到文件。
* **发布后怎么核对**：`gh release view <tag> --json tagName,isDraft,isPrerelease,assets --jq …` 看四个
  资产都在 `uploaded` 状态；正文**别用 PowerShell 的 `>` 抓**（UTF-8 stdout 先按 GBK 解码、再写成
  **UTF-16**，中文变成 `�?`，看起来像"发布页正文是乱码"——其实远端是好的，用 HTTP 取
  `api.github.com/repos/<owner>/<repo>/releases/tags/<tag>` 的 `body` 才是逐字节正确的）。判断"发布
  成功"靠脚本退出码 + 上面那 4 个资产。
* **`--check` 之类的输出中文经 agent 的 bash 抓回来是乱码**（控制台代码页）→ 验收脚本把窗口标题与前台
  窗口的类名/码点写进 `-WorkDir\diag.txt`（UTF-8、无 BOM）当诊断，要读结果就用 read 工具读那个文件。
* **窗口切换器的行为只能靠验收脚本验证**（`PopupHost` 要真 QML 窗口，单测碰不到）：用**注入的「轻碰
  Win」+ 窗口标题前缀**这套组合从外面看。两个容易踩的点：（1）触发发生在 **Win 键松开时**，所以"第二次
  轻碰"进来时卡片已经开着；（2）遮断标记要是没生效，外壳会弹开始菜单、卡片会在 300 ms 内自己关掉 ——
  所以"卡片还在前台"要**再等半秒**再断言。
