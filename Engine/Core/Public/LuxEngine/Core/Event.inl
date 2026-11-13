#pragma once

namespace LuxEngine {
template <typename T, typename F> bool EventDispatcher::Dispatch(const F& func) {
    if (event_.GetEventType() == T::GetStaticType()) {
        event_.Handled |= func(static_cast<T&>(event_));
        return true;
    }
    return false;
}
} // namespace LuxEngine
