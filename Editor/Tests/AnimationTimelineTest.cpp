#include <LuxEditor/Animation/AnimationTimeline.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxUI/Widgets.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void Mouse(LuxUI::Widget* widget, LuxUI::Event::Type type, LuxUI::PointF point,
           LuxUI::Flags::MouseButton button, LuxUI::Flags::MouseButtons buttons) {
    LuxUI::MouseEvent event(type, point, point, button, buttons, LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(widget, &event);
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application app(argc, argv);
    try {
        LuxEngine::Scene scene;
        auto entity = scene.CreateEntity("Timeline");
        auto& animation = entity.AddComponent<LuxEngine::AnimationComponent>();
        animation.clip.duration = 2;
        animation.clip.loop = false;
        animation.clip.AddKeyframe({0, {}, {}, {1, 1, 1}});
        animation.clip.AddKeyframe({1, {2, 1, 0}, {}, {1, 1, 1}});
        animation.clip.AddKeyframe({2, {4, 2, 0}, {}, {1, 1, 1}});
        int selected = -1;
        float moved = -1;
        LuxEditor::AnimationTimeline timeline(
            nullptr,
            {[entity] { return entity; }, [&] { return selected; }, [] { return 30; },
             [&](int key) { selected = key; }, [&](int, float time) { moved = time; }, [] {}, [] {},
             [&](double time) { animation.currentTime = static_cast<float>(time); }, [] {}});
        LuxUI::Palette palette;
        palette.setColor(LuxUI::Palette::Window, LuxUI::Color("#242424"));
        palette.setColor(LuxUI::Palette::WindowText, LuxUI::Color("#dedede"));
        palette.setColor(LuxUI::Palette::Base,
                         LuxUI::Flags::white); // Regression: style Base remains light.
        palette.setColor(LuxUI::Palette::Highlight, LuxUI::Color("#3269a8"));
        timeline.setPalette(palette);
        timeline.resize(1000, 240);
        timeline.show();
        app.processEvents();
        const auto image = timeline.grab().toImage();
        Check(image.pixelColor(800, 230).lightness() < 90,
              "Dark animation timeline used the style's white Base color");
        const double span = timeline.width() - 174 - 18;
        const LuxUI::PointF start(174 + span / 2, 86);
        const LuxUI::PointF end(174 + span * .75, 86);
        Mouse(&timeline, LuxUI::Event::MouseButtonPress, start, LuxUI::Flags::LeftButton,
              LuxUI::Flags::LeftButton);
        Mouse(&timeline, LuxUI::Event::MouseMove, end, LuxUI::Flags::NoButton,
              LuxUI::Flags::LeftButton);
        Mouse(&timeline, LuxUI::Event::MouseButtonRelease, end, LuxUI::Flags::LeftButton,
              LuxUI::Flags::NoButton);
        Check(selected == 1 && std::abs(moved - 1.5f) < .0001f,
              "Timeline did not select and drag a key to its snapped time");
        Check(!timeline.isSliderDown(), "Timeline did not end its editing gesture");
        timeline.SetCurveMode(true);
        app.processEvents();
        Check(!timeline.grab().isNull(), "Position curve view did not render");
        std::cout << "Animation timeline contrast, selection and drag checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
