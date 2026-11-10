#include "App/EditorWindow.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QListWidget>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUndoStack>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace LuxEditor;
using namespace LuxEngine;
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
QGroupBox* Group(EditorWindow& window, const char* title) {
    for (auto* group : window.findChildren<QGroupBox*>())
        if (group->title() == QString::fromLatin1(title))
            return group;
    throw std::runtime_error("Inspector group not found");
}
void CheckTransform(const TransformComponent& actual, const TransformComponent& expected) {
    const std::array<float, 9> a{actual.position.x, actual.position.y, actual.position.z,
                                 actual.rotation.x, actual.rotation.y, actual.rotation.z,
                                 actual.scale.x,    actual.scale.y,    actual.scale.z};
    const std::array<float, 9> b{expected.position.x, expected.position.y, expected.position.z,
                                 expected.rotation.x, expected.rotation.y, expected.rotation.z,
                                 expected.scale.x,    expected.scale.y,    expected.scale.z};
    Check(a == b, "editing one Transform axis preserves every untouched float exactly");
}
void TestFields(QApplication& app, const QString& directory) {
    Scene scene;
    auto entity = scene.CreateEntity("Inspector fixture");
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.position = {0, 200000, .1234567f};
    transform.rotation = {.1234567f, .456789f, .8765432f};
    transform.scale = {.0001234f, 1.234567f, 7.654321f};
    const auto originalTransform = transform;
    auto& sprite = entity.AddComponent<SpriteRendererComponent>();
    sprite.color = {1.234567f, -.25f, .1234567f, .9876543f};
    auto& mesh = entity.AddComponent<MeshRendererComponent>();
    mesh.material.metallic = .1234567f;
    mesh.material.roughness = 2;
    auto& map = entity.AddComponent<TilemapComponent>();
    map.Resize(8, 8);
    map.tileSize = .01234567f;
    map.Set(0, 0, 3);
    auto& body = entity.AddComponent<Rigidbody2DComponent>();
    body.gravityScale = -1;
    body.velocity = {1.234567f, 2.345678f};
    body.mass = 4.567891f;
    auto& collider = entity.AddComponent<BoxCollider2DComponent>();
    collider.size = {.01234567f, 12345.67f};
    collider.offset = {20000, -.1234567f};
    const auto originalCollider = collider;
    auto& animation = entity.AddComponent<AnimationComponent>();
    animation.clip.duration = 3601.1234f;
    animation.clip.loop = true;
    animation.clip.AddKeyframe({0, {}, {}, {1, 1, 1}});
    const float originalDuration = animation.clip.duration;
    const QString path = directory + QStringLiteral("/inspector.lux");
    Check(scene.Serialize(path.toUtf8().toStdString()), "inspector fixture saves");
    QSettings().clear();
    QSettings().setValue(QStringLiteral("lastScene"), path);
    EditorWindow window;
    window.show();
    app.processEvents();
    auto* outliner = window.findChild<QTreeWidget*>(QStringLiteral("sceneOutliner"));
    Check(outliner && outliner->topLevelItemCount() == 1, "inspector fixture opens");
    outliner->setCurrentItem(outliner->topLevelItem(0));
    auto selected = [&] { return window.GetDocument().GetSelected(); };
    auto* undo = window.findChild<QUndoStack*>();
    Check(undo != nullptr, "undo stack exists");

    const auto transforms = Group(window, "TRANSFORM")->findChildren<QDoubleSpinBox*>();
    Check(transforms.size() == 9, "all Transform fields exist");
    transforms[0]->setValue(2);
    auto expectedTransform = originalTransform;
    expectedTransform.position.x = 2;
    CheckTransform(selected().GetComponent<TransformComponent>(), expectedTransform);
    undo->undo();
    CheckTransform(selected().GetComponent<TransformComponent>(), originalTransform);
    undo->redo();
    CheckTransform(selected().GetComponent<TransformComponent>(), expectedTransform);
    transforms[4]->setValue(30);
    expectedTransform.rotation.y = ToRadians(30);
    CheckTransform(selected().GetComponent<TransformComponent>(), expectedTransform);
    transforms[8]->setValue(3);
    expectedTransform.scale.z = 3;
    CheckTransform(selected().GetComponent<TransformComponent>(), expectedTransform);

    const auto colors = Group(window, "SPRITE RENDERER")->findChildren<QDoubleSpinBox*>();
    colors[2]->setValue(.5);
    const auto color = selected().GetComponent<SpriteRendererComponent>().color;
    Check(color.x == 1.234567f && color.y == -.25f && color.z == .5f && color.w == .9876543f,
          "editing one color channel preserves HDR, negative and precise untouched channels");

    const auto materials = Group(window, "MESH RENDERER")->findChildren<QDoubleSpinBox*>();
    materials[0]->setValue(.4);
    Check(selected().GetComponent<MeshRendererComponent>().material.roughness == 2,
          "metallic edits preserve out-of-range roughness");
    materials[1]->setValue(.7);
    Check(selected().GetComponent<MeshRendererComponent>().material.metallic == .4f,
          "roughness edits preserve metallic");

    auto* tileGroup = Group(window, "TILEMAP");
    const auto dimensions = tileGroup->findChildren<QSpinBox*>();
    dimensions[0]->setValue(9);
    auto tilemap = selected().GetComponent<TilemapComponent>();
    Check(tilemap.width == 9 && tilemap.height == 8 && tilemap.tileSize == .01234567f &&
              tilemap.Get(0, 0) == 3,
          "resizing a tilemap preserves precise sub-minimum tile size and painted cells");
    dimensions[1]->setValue(10);
    tilemap = selected().GetComponent<TilemapComponent>();
    Check(tilemap.width == 9 && tilemap.height == 10 && tilemap.tileSize == .01234567f,
          "height edits preserve other tilemap fields");

    auto* physics = Group(window, "2D PHYSICS");
    const auto physicsFields = physics->findChildren<QDoubleSpinBox*>();
    QCheckBox* trigger = nullptr;
    for (auto* checkbox : physics->findChildren<QCheckBox*>())
        if (checkbox->text() == QStringLiteral("트리거"))
            trigger = checkbox;
    Check(trigger != nullptr && physicsFields.size() == 5, "physics fields exist");
    trigger->setChecked(true);
    auto actualCollider = selected().GetComponent<BoxCollider2DComponent>();
    Check(actualCollider.size.x == originalCollider.size.x &&
              actualCollider.size.y == originalCollider.size.y &&
              actualCollider.offset.x == originalCollider.offset.x &&
              actualCollider.offset.y == originalCollider.offset.y && actualCollider.isTrigger,
          "trigger toggles preserve every precise and out-of-range collider field");
    auto actualBody = selected().GetComponent<Rigidbody2DComponent>();
    Check(actualBody.gravityScale == -1 && actualBody.mass == 4.567891f &&
              actualBody.velocity.x == 1.234567f && actualBody.velocity.y == 2.345678f,
          "collider edits preserve negative gravity and all rigidbody values");
    physicsFields[1]->setValue(2);
    actualCollider = selected().GetComponent<BoxCollider2DComponent>();
    Check(actualCollider.size.x == 2 && actualCollider.size.y == originalCollider.size.y &&
              actualCollider.offset.x == originalCollider.offset.x &&
              actualCollider.offset.y == originalCollider.offset.y,
          "one collider dimension edit preserves other dimensions and offsets");
    window.findChild<QCheckBox*>(QStringLiteral("editorCollider2D"))->setChecked(false);
    Check(!selected().HasComponent<BoxCollider2DComponent>() &&
              selected().GetComponent<Rigidbody2DComponent>().gravityScale == -1,
          "removing a collider preserves the body's negative gravity");
    undo->undo();
    Check(selected().HasComponent<BoxCollider2DComponent>() &&
              selected().GetComponent<Rigidbody2DComponent>().gravityScale == -1,
          "undo restores collider removal without changing gravity");
    undo->redo();
    Check(!selected().HasComponent<BoxCollider2DComponent>() &&
              selected().GetComponent<Rigidbody2DComponent>().gravityScale == -1,
          "redo preserves independent body fields");
    physics->findChild<QComboBox*>()->setCurrentIndex(0);
    Check(selected().GetComponent<Rigidbody2DComponent>().gravityScale == -1,
          "changing body type also preserves negative gravity");

    QCheckBox* loop = nullptr;
    for (auto* checkbox : Group(window, "TRANSFORM ANIMATION")->findChildren<QCheckBox*>())
        if (checkbox->text() == QStringLiteral("반복 재생"))
            loop = checkbox;
    Check(loop != nullptr, "animation loop control exists");
    loop->setChecked(false);
    Check(!selected().GetComponent<AnimationComponent>().clip.loop &&
              selected().GetComponent<AnimationComponent>().clip.duration == originalDuration,
          "loop changes preserve precise animation duration above the UI range");
    undo->undo();
    Check(selected().GetComponent<AnimationComponent>().clip.loop &&
              selected().GetComponent<AnimationComponent>().clip.duration == originalDuration,
          "animation loop undo preserves the exact duration");
    undo->redo();
    Check(selected().GetComponent<AnimationComponent>().clip.duration == originalDuration,
          "animation loop redo preserves the exact duration");
    Check(window.GetDocument().Save(), "edited scene saves");
    EditorDocument reopened;
    Check(reopened.Open(path.toUtf8().toStdString()), "edited scene reopens");
    reopened.Select(selected().GetId());
    CheckTransform(reopened.GetSelected().GetComponent<TransformComponent>(), expectedTransform);
    Check(reopened.GetSelected().GetComponent<AnimationComponent>().clip.duration ==
              originalDuration,
          "save and reopen preserve untouched precision");
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxEngineTests"));
    QCoreApplication::setApplicationName(QStringLiteral("LuxEditorInspectorTests"));
    QTemporaryDir settings, files;
    if (!settings.isValid() || !files.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    try {
        TestFields(app, files.path());
        std::cout << "Inspector field preservation checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
