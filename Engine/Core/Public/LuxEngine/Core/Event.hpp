#pragma once

#include <LuxEngine/Core/Core.hpp>
#include <functional>
#include <string>

namespace LuxEngine {

enum class EventType {
    None = 0,
    WindowClose,
    WindowResize,
    WindowFocus,
    WindowLostFocus,
    KeyPressed,
    KeyReleased,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseMoved,
    MouseScrolled
};

enum EventCategory {
    None = 0,
    EventCategoryApplication = 1 << 0,
    EventCategoryInput = 1 << 1,
    EventCategoryKeyboard = 1 << 2,
    EventCategoryMouse = 1 << 3,
    EventCategoryMouseButton = 1 << 4
};

class Event {
public:
    LUX_API virtual ~Event();

    bool Handled = false;

    virtual EventType GetEventType() const = 0;
    virtual const char* GetName() const = 0;
    virtual int GetCategoryFlags() const = 0;

    LUX_API bool IsInCategory(EventCategory category) const;
};

class EventDispatcher {
public:
    LUX_API EventDispatcher(Event& event);

    template <typename T, typename F> bool Dispatch(const F& func);

private:
    Event& event_;
};

class WindowCloseEvent : public Event {
public:
    LUX_API WindowCloseEvent();

    LUX_API static EventType GetStaticType();

    LUX_API EventType GetEventType() const override;

    LUX_API const char* GetName() const override;

    LUX_API int GetCategoryFlags() const override;
};

class WindowResizeEvent : public Event {
public:
    LUX_API WindowResizeEvent(uint32 width, uint32 height);

    LUX_API uint32 GetWidth() const;

    LUX_API uint32 GetHeight() const;

    LUX_API static EventType GetStaticType();

    LUX_API EventType GetEventType() const override;

    LUX_API const char* GetName() const override;

    LUX_API int GetCategoryFlags() const override;

private:
    uint32 width_;
    uint32 height_;
};

class KeyPressedEvent : public Event {
public:
    LUX_API KeyPressedEvent(int32 keycode, bool isRepeat = false);

    LUX_API int32 GetKeyCode() const;

    LUX_API bool IsRepeat() const;

    LUX_API static EventType GetStaticType();

    LUX_API EventType GetEventType() const override;

    LUX_API const char* GetName() const override;

    LUX_API int GetCategoryFlags() const override;

private:
    int32 keycode_;
    bool isRepeat_;
};

class KeyReleasedEvent : public Event {
public:
    LUX_API KeyReleasedEvent(int32 keycode);

    LUX_API int32 GetKeyCode() const;

    LUX_API static EventType GetStaticType();

    LUX_API EventType GetEventType() const override;

    LUX_API const char* GetName() const override;

    LUX_API int GetCategoryFlags() const override;

private:
    int32 keycode_;
};

class MouseMovedEvent : public Event {
public:
    LUX_API MouseMovedEvent(float x, float y);

    LUX_API float GetX() const;

    LUX_API float GetY() const;

    LUX_API static EventType GetStaticType();

    LUX_API EventType GetEventType() const override;

    LUX_API const char* GetName() const override;

    LUX_API int GetCategoryFlags() const override;

private:
    float mouseX_;
    float mouseY_;
};

class MouseScrolledEvent : public Event {
public:
    LUX_API MouseScrolledEvent(float xOffset, float yOffset);

    LUX_API float GetXOffset() const;

    LUX_API float GetYOffset() const;

    LUX_API static EventType GetStaticType();

    LUX_API EventType GetEventType() const override;

    LUX_API const char* GetName() const override;

    LUX_API int GetCategoryFlags() const override;

private:
    float xOffset_;
    float yOffset_;
};

class MouseButtonPressedEvent : public Event {
public:
    LUX_API MouseButtonPressedEvent(int32 button);

    LUX_API int32 GetMouseButton() const;

    LUX_API static EventType GetStaticType();

    LUX_API EventType GetEventType() const override;

    LUX_API const char* GetName() const override;

    LUX_API int GetCategoryFlags() const override;

private:
    int32 button_;
};

class MouseButtonReleasedEvent : public Event {
public:
    LUX_API MouseButtonReleasedEvent(int32 button);

    LUX_API int32 GetMouseButton() const;

    LUX_API static EventType GetStaticType();

    LUX_API EventType GetEventType() const override;

    LUX_API const char* GetName() const override;

    LUX_API int GetCategoryFlags() const override;

private:
    int32 button_;
};

} // namespace LuxEngine

#include <LuxEngine/Core/Event.inl>
