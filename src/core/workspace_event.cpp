#include "core/workspace_event.h"

namespace flowkeyd::core {

const char *workspaceEventName(WorkspaceEvent::Kind kind)
{
    switch (kind) {
    case WorkspaceEvent::Kind::Startup:
        return "startup";
    case WorkspaceEvent::Kind::WindowAppeared:
        return "window appeared";
    case WorkspaceEvent::Kind::WindowDestroyed:
        return "window destroyed";
    case WorkspaceEvent::Kind::WindowMinimized:
        return "window minimized";
    case WorkspaceEvent::Kind::WindowRestored:
        return "window restored";
    case WorkspaceEvent::Kind::WindowMoved:
        return "window moved";
    case WorkspaceEvent::Kind::FocusChanged:
        return "focus changed";
    case WorkspaceEvent::Kind::MonitorsChanged:
        return "monitors changed";
    }
    return "unknown";
}

void ExpectedWorkspaceEvents::expect(WorkspaceEvent::Kind kind,
                                    WindowId window,
                                    std::uint64_t nowMs,
                                    std::uint64_t lifetimeMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    pruneLocked(nowMs);
    m_expiry[Key{kind, window}] = nowMs + lifetimeMs;
}

bool ExpectedWorkspaceEvents::consume(WorkspaceEvent::Kind kind,
                                     WindowId window,
                                     std::uint64_t nowMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    pruneLocked(nowMs);
    const auto it = m_expiry.find(Key{kind, window});
    if (it == m_expiry.end()) {
        return false;
    }
    // 吞一次就够了：同一次显隐只会产生一条 SHOW（M0 实测）。
    m_expiry.erase(it);
    return true;
}

std::size_t ExpectedWorkspaceEvents::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_expiry.size();
}

void ExpectedWorkspaceEvents::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_expiry.clear();
}

void ExpectedWorkspaceEvents::pruneLocked(std::uint64_t nowMs)
{
    for (auto it = m_expiry.begin(); it != m_expiry.end();) {
        if (it->second <= nowMs) {
            it = m_expiry.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace flowkeyd::core
