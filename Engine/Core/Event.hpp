#pragma once

#include "Engine/Core/Core.hpp"
#include <string>
#include <functional>

namespace LuxEngine {

    enum class EventType {
        None = 0,
        WindowClose, WindowResize, WindowFocus, WindowLostFocus,
        KeyPressed, KeyReleased,
        MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
    };

    enum EventCategory {
        None = 0,
        EventCategoryApplication = 1 << 0,
        EventCategoryInput       = 1 << 1,
        EventCategoryKeyboard    = 1 << 2,
        EventCategoryMouse       = 1 << 3,
        EventCategoryMouseButton = 1 << 4
    };

    class Event {
    public:
        virtual ~Event() = default;

        bool Handled = false;

        virtual EventType GetEventType() const = 0;
        virtual const char* GetName() const = 0;
        virtual int GetCategoryFlags() const = 0;

        bool IsInCategory(EventCategory category) const {
            return (GetCategoryFlags() & category) != 0;
        }
    };

    class EventDispatcher {
    public:
        EventDispatcher(Event& event) : event_(event) {}

        template<typename T, typename F>
        bool Dispatch(const F& func) {
            if (event_.GetEventType() == T::GetStaticType()) {
                event_.Handled |= func(static_cast<T&>(event_));
                return true;
            }
            return false;
        }

    private:
        Event& event_;
    };

    class WindowCloseEvent : public Event {
    public:
        WindowCloseEvent() = default;

        static EventType GetStaticType() { return EventType::WindowClose; }
        EventType GetEventType() const override { return GetStaticType(); }
        const char* GetName() const override { return "WindowClose"; }
        int GetCategoryFlags() const override { return EventCategoryApplication; }
    };

    class WindowResizeEvent : public Event {
    public:
        WindowResizeEvent(uint32 width, uint32 height)
            : width_(width), height_(height) {}

        uint32 GetWidth() const { return width_; }
        uint32 GetHeight() const { return height_; }

        static EventType GetStaticType() { return EventType::WindowResize; }
        EventType GetEventType() const override { return GetStaticType(); }
        const char* GetName() const override { return "WindowResize"; }
        int GetCategoryFlags() const override { return EventCategoryApplication; }

    private:
        uint32 width_;
        uint32 height_;
    };

    class KeyPressedEvent : public Event {
    public:
        KeyPressedEvent(int32 keycode, bool isRepeat = false)
            : keycode_(keycode), isRepeat_(isRepeat) {}

        int32 GetKeyCode() const { return keycode_; }
        bool IsRepeat() const { return isRepeat_; }

        static EventType GetStaticType() { return EventType::KeyPressed; }
        EventType GetEventType() const override { return GetStaticType(); }
        const char* GetName() const override { return "KeyPressed"; }
        int GetCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

    private:
        int32 keycode_;
        bool isRepeat_;
    };

    class KeyReleasedEvent : public Event {
    public:
        KeyReleasedEvent(int32 keycode) : keycode_(keycode) {}

        int32 GetKeyCode() const { return keycode_; }

        static EventType GetStaticType() { return EventType::KeyReleased; }
        EventType GetEventType() const override { return GetStaticType(); }
        const char* GetName() const override { return "KeyReleased"; }
        int GetCategoryFlags() const override { return EventCategoryInput | EventCategoryKeyboard; }

    private:
        int32 keycode_;
    };

    class MouseMovedEvent : public Event {
    public:
        MouseMovedEvent(float x, float y) : mouseX_(x), mouseY_(y) {}

        float GetX() const { return mouseX_; }
        float GetY() const { return mouseY_; }

        static EventType GetStaticType() { return EventType::MouseMoved; }
        EventType GetEventType() const override { return GetStaticType(); }
        const char* GetName() const override { return "MouseMoved"; }
        int GetCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse; }

    private:
        float mouseX_;
        float mouseY_;
    };

    class MouseScrolledEvent : public Event {
    public:
        MouseScrolledEvent(float xOffset, float yOffset)
            : xOffset_(xOffset), yOffset_(yOffset) {}

        float GetXOffset() const { return xOffset_; }
        float GetYOffset() const { return yOffset_; }

        static EventType GetStaticType() { return EventType::MouseScrolled; }
        EventType GetEventType() const override { return GetStaticType(); }
        const char* GetName() const override { return "MouseScrolled"; }
        int GetCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse; }

    private:
        float xOffset_;
        float yOffset_;
    };

    class MouseButtonPressedEvent : public Event {
    public:
        MouseButtonPressedEvent(int32 button) : button_(button) {}

        int32 GetMouseButton() const { return button_; }

        static EventType GetStaticType() { return EventType::MouseButtonPressed; }
        EventType GetEventType() const override { return GetStaticType(); }
        const char* GetName() const override { return "MouseButtonPressed"; }
        int GetCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton; }

    private:
        int32 button_;
    };

    class MouseButtonReleasedEvent : public Event {
    public:
        MouseButtonReleasedEvent(int32 button) : button_(button) {}

        int32 GetMouseButton() const { return button_; }

        static EventType GetStaticType() { return EventType::MouseButtonReleased; }
        EventType GetEventType() const override { return GetStaticType(); }
        const char* GetName() const override { return "MouseButtonReleased"; }
        int GetCategoryFlags() const override { return EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton; }

    private:
        int32 button_;
    };

} // namespace LuxEngine
