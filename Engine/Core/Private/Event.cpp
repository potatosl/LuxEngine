#include <LuxEngine/Core/Event.hpp>

namespace LuxEngine {
Event::~Event() = default;

bool Event::IsInCategory(EventCategory category) const {
    return (GetCategoryFlags() & category) != 0;
}

EventDispatcher::EventDispatcher(Event& event) : event_(event) {}

EventType WindowCloseEvent::GetStaticType() {
    return EventType::WindowClose;
}

EventType WindowCloseEvent::GetEventType() const {
    return GetStaticType();
}

const char* WindowCloseEvent::GetName() const {
    return "WindowClose";
}

int WindowCloseEvent::GetCategoryFlags() const {
    return EventCategoryApplication;
}

WindowResizeEvent::WindowResizeEvent(uint32 width, uint32 height)
    : width_(width), height_(height) {}

uint32 WindowResizeEvent::GetWidth() const {
    return width_;
}

uint32 WindowResizeEvent::GetHeight() const {
    return height_;
}

EventType WindowResizeEvent::GetStaticType() {
    return EventType::WindowResize;
}

EventType WindowResizeEvent::GetEventType() const {
    return GetStaticType();
}

const char* WindowResizeEvent::GetName() const {
    return "WindowResize";
}

int WindowResizeEvent::GetCategoryFlags() const {
    return EventCategoryApplication;
}

KeyPressedEvent::KeyPressedEvent(int32 keycode, bool isRepeat)
    : keycode_(keycode), isRepeat_(isRepeat) {}

int32 KeyPressedEvent::GetKeyCode() const {
    return keycode_;
}

bool KeyPressedEvent::IsRepeat() const {
    return isRepeat_;
}

EventType KeyPressedEvent::GetStaticType() {
    return EventType::KeyPressed;
}

EventType KeyPressedEvent::GetEventType() const {
    return GetStaticType();
}

const char* KeyPressedEvent::GetName() const {
    return "KeyPressed";
}

int KeyPressedEvent::GetCategoryFlags() const {
    return EventCategoryInput | EventCategoryKeyboard;
}

KeyReleasedEvent::KeyReleasedEvent(int32 keycode) : keycode_(keycode) {}

int32 KeyReleasedEvent::GetKeyCode() const {
    return keycode_;
}

EventType KeyReleasedEvent::GetStaticType() {
    return EventType::KeyReleased;
}

EventType KeyReleasedEvent::GetEventType() const {
    return GetStaticType();
}

const char* KeyReleasedEvent::GetName() const {
    return "KeyReleased";
}

int KeyReleasedEvent::GetCategoryFlags() const {
    return EventCategoryInput | EventCategoryKeyboard;
}

MouseMovedEvent::MouseMovedEvent(float x, float y) : mouseX_(x), mouseY_(y) {}

float MouseMovedEvent::GetX() const {
    return mouseX_;
}

float MouseMovedEvent::GetY() const {
    return mouseY_;
}

EventType MouseMovedEvent::GetStaticType() {
    return EventType::MouseMoved;
}

EventType MouseMovedEvent::GetEventType() const {
    return GetStaticType();
}

const char* MouseMovedEvent::GetName() const {
    return "MouseMoved";
}

int MouseMovedEvent::GetCategoryFlags() const {
    return EventCategoryInput | EventCategoryMouse;
}

MouseScrolledEvent::MouseScrolledEvent(float xOffset, float yOffset)
    : xOffset_(xOffset), yOffset_(yOffset) {}

float MouseScrolledEvent::GetXOffset() const {
    return xOffset_;
}

float MouseScrolledEvent::GetYOffset() const {
    return yOffset_;
}

EventType MouseScrolledEvent::GetStaticType() {
    return EventType::MouseScrolled;
}

EventType MouseScrolledEvent::GetEventType() const {
    return GetStaticType();
}

const char* MouseScrolledEvent::GetName() const {
    return "MouseScrolled";
}

int MouseScrolledEvent::GetCategoryFlags() const {
    return EventCategoryInput | EventCategoryMouse;
}

MouseButtonPressedEvent::MouseButtonPressedEvent(int32 button) : button_(button) {}

int32 MouseButtonPressedEvent::GetMouseButton() const {
    return button_;
}

EventType MouseButtonPressedEvent::GetStaticType() {
    return EventType::MouseButtonPressed;
}

EventType MouseButtonPressedEvent::GetEventType() const {
    return GetStaticType();
}

const char* MouseButtonPressedEvent::GetName() const {
    return "MouseButtonPressed";
}

int MouseButtonPressedEvent::GetCategoryFlags() const {
    return EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton;
}

MouseButtonReleasedEvent::MouseButtonReleasedEvent(int32 button) : button_(button) {}

int32 MouseButtonReleasedEvent::GetMouseButton() const {
    return button_;
}

EventType MouseButtonReleasedEvent::GetStaticType() {
    return EventType::MouseButtonReleased;
}

EventType MouseButtonReleasedEvent::GetEventType() const {
    return GetStaticType();
}

const char* MouseButtonReleasedEvent::GetName() const {
    return "MouseButtonReleased";
}

int MouseButtonReleasedEvent::GetCategoryFlags() const {
    return EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton;
}

WindowCloseEvent::WindowCloseEvent() = default;
} // namespace LuxEngine
