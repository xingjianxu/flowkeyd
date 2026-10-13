#include "core/monitor_ref.h"

#include "core/action.h"

namespace flowkeyd::core {

QString MonitorRef::describe() const
{
    switch (kind) {
    case Kind::Index:
        return QStringLiteral("monitor %1").arg(index);
    case Kind::Primary:
        return QStringLiteral("primary monitor");
    case Kind::Device:
        return QStringLiteral("monitor %1").arg(rustDebug(device));
    case Kind::Cursor:
        return QStringLiteral("the monitor under the cursor");
    case Kind::Last:
        return QStringLiteral("the monitor it was last placed on");
    case Kind::None:
        return QStringLiteral("no monitor");
    }
    return QStringLiteral("monitor");
}

bool monitorRefIsConcrete(const MonitorRef &ref)
{
    return ref.kind == MonitorRef::Kind::Index || ref.kind == MonitorRef::Kind::Primary
        || ref.kind == MonitorRef::Kind::Device;
}

bool monitorRefIsDynamic(const MonitorRef &ref)
{
    return ref.kind == MonitorRef::Kind::Cursor || ref.kind == MonitorRef::Kind::Last;
}

} // namespace flowkeyd::core
