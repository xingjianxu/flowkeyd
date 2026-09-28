// 剪贴板访问（`CF_UNICODETEXT`），对“另一个进程占着剪贴板”这种常见情况做了重试。
//
// 剪贴板是全局资源：`OpenClipboard` 在别人打开着的时候会失败，而那个窗口可能
// 只有几毫秒。因此这里重试若干次再放弃。
#pragma once

#include <QString>

namespace flowkeyd::platform::win::clipboard {

/// 以文本形式读取剪贴板。剪贴板为空或只有非文本格式时返回空字符串。
bool getText(QString *out, QString *error = nullptr);

/// 用 `text` 替换剪贴板内容。
bool setText(const QString &text, QString *error = nullptr);

/// 把 `text` 追加到当前剪贴板内容之后。
bool appendText(const QString &text, QString *error = nullptr);

/// 清空剪贴板。
bool clear(QString *error = nullptr);

} // namespace flowkeyd::platform::win::clipboard
