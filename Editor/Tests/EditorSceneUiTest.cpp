#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Build/BuildOutput.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEditor/Viewport/ViewportCamera3D.hpp>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
using namespace LuxEngine;
using namespace LuxEditor;
using TabId = EditorDocument::TabId;

void Check(bool condition, const char* description) {
    if (!condition)
        throw std::runtime_error(description);
}

bool Near(float a, float b, float epsilon = 1e-4f) {
    return std::abs(a - b) < epsilon;
}

void EqualMatrix(const Mat4& a, const Mat4& b, const char* description) {
    for (size_t index = 0; index < 16; ++index)
        Check(Near(a.elements[index], b.elements[index]), description);
}

std::string Data(Scene& scene) {
    std::string result;
    Check(scene.SerializeToString(result), "UI fixture serializes");
    return result;
}

void Pump(int milliseconds = 0) {
    QElapsedTimer timer;
    timer.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        if (milliseconds)
            QThread::msleep(1);
    } while (timer.elapsed() < milliseconds);
}

template <typename Predicate> void Wait(Predicate condition, const char* description) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 5000)
        Pump(2);
    Check(condition(), description);
}

QAction* Action(EditorWindow& window, const char* name) {
    auto* result = window.findChild<QAction*>(QString::fromLatin1(name));
    Check(result != nullptr, "required scene menu action exists");
    return result;
}

QUndoStack* History(EditorWindow& window) {
    auto* result = window.findChild<QUndoStack*>();
    Check(result != nullptr, "global scene history exists");
    return result;
}

QTreeWidget* Outliner(EditorWindow& window) {
    auto* result = window.findChild<QTreeWidget*>(QStringLiteral("sceneOutliner"));
    Check(result != nullptr, "scene outliner exists");
    return result;
}

QTreeWidgetItem* Find(QTreeWidget& tree, uint32 id) {
    for (QTreeWidgetItemIterator item(&tree); *item; ++item)
        if ((*item)->data(0, Qt::UserRole).toUInt() == id)
            return *item;
    throw std::runtime_error("entity is missing from the visible scene hierarchy");
}

struct Fixture {
    QTemporaryDir files;
    std::unique_ptr<EditorWindow> window;

    Fixture() {
        Check(files.isValid(), "temporary fixture directory exists");
        QSettings().clear();
        QSettings().setValue(QStringLiteral("sceneTransitions"), false);
        window = std::make_unique<EditorWindow>();
        window->show();
        Pump();
    }

    ~Fixture() {
        for (const auto& tab : window->GetDocument().ListTabs())
            window->GetDocument().SetDirty(tab.id, false);
        window->close();
        Pump();
    }

    std::filesystem::path Path(const char* name) const {
        return PathFromUtf8(files.path().toUtf8().toStdString()) / name;
    }

    TabId Open(Scene& scene, const char* name) {
        const auto file = Path(name);
        Check(scene.Serialize(PathUtf8(file)) && window->OpenSceneDocument(file),
              "saved scene opens through the editor UI path");
        return window->GetDocument().GetActiveTabId();
    }
};

void MeshAssetEditing() {
    Fixture fixture;
    auto& window = *fixture.window;
    auto& document = window.GetDocument();
    const auto model = fixture.Path("triangle.obj");
    std::ofstream(model) << "v 1 0 0\nv 3 0 0\nv 2 2 0\nvn 0 0 1\nf 1//1 2//1 3//1\n";
    window.ExecuteRecordedEdit(QStringLiteral("메시 생성"), [&] {
        document.CreateEntity("OBJ").AddComponent<MeshRendererComponent>();
    });
    const auto id = document.GetSelected().GetId();
    auto* path = window.findChild<QLineEdit*>(QStringLiteral("meshAssetPath"));
    auto* choose = window.findChild<QPushButton*>(QStringLiteral("chooseMeshAsset"));
    auto* clear = window.findChild<QPushButton*>(QStringLiteral("clearMeshAsset"));
    Check(path && choose && clear && path->text().isEmpty(), "mesh asset inspector controls exist");
    QTimer picker;
    QObject::connect(&picker, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(QString::fromUtf8(PathUtf8(model)));
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    picker.start(5);
    choose->click();
    picker.stop();
    const auto selectedPath = document.GetScene().GetComponent<MeshRendererComponent>(id).meshPath;
    Check(PathFromUtf8(selectedPath) == model && path->text() == QString::fromUtf8(selectedPath),
          "OBJ selection updates the selected scene component and inspector");
    History(window)->undo();
    Check(document.GetScene().GetComponent<MeshRendererComponent>(id).meshPath.empty(),
          "mesh assignment is undoable");
    History(window)->redo();
    Check(document.GetScene().GetComponent<MeshRendererComponent>(id).meshPath == selectedPath,
          "mesh assignment is redoable");
    Action(window, "copyEntity")->trigger();
    Action(window, "pasteEntity")->trigger();
    const auto copiedPath = document.GetSelected().GetComponent<MeshRendererComponent>().meshPath;
    Check(document.GetSelected().GetId() != id &&
              std::filesystem::equivalent(PathFromUtf8(copiedPath), model),
          "single-object clipboard copying preserves the mesh asset");
    clear->click();
    Check(document.GetSelected().GetComponent<MeshRendererComponent>().meshPath.empty(),
          "mesh assignment can be reset to the default cube");
    History(window)->undo();
    Check(document.GetSelected().GetComponent<MeshRendererComponent>().meshPath == copiedPath,
          "resetting a mesh is undoable");
    EditorDocument picking;
    auto object = picking.CreateEntity("Offset OBJ bounds");
    object.AddComponent<MeshRendererComponent>().meshPath = PathUtf8(model);
    picking.Select({});
    SceneViewport3D view(
        nullptr, picking, [&](auto selected) { picking.Select(selected); },
        [](uint32, const TransformComponent&) {}, [] {}, [] {});
    view.resize(800, 600);
    view.show();
    Pump();
    const auto camera = MakeViewportCamera3D(view.size(), {}, .65, -.48, 22);
    const auto point = *ProjectViewportPoint(camera, view.size(), {2, 1, 0});
    QMouseEvent press(QEvent::MouseButtonPress, point, view.mapToGlobal(point.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, view.mapToGlobal(point.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &release);
    Check(picking.GetSelectedId(picking.GetActiveTabId()) == object.GetId(),
          "3D picking uses actual offset OBJ bounds rather than the default unit cube");
}

void BaseColorEditingAndReload() {
    Fixture fixture;
    auto& window = *fixture.window;
    auto& document = window.GetDocument();
    const auto texture = fixture.Path("base-color.PnG");
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(QColor(120, 80, 40));
    Check(image.save(QString::fromUtf8(PathUtf8(texture)), "PNG"), "editor texture fixture writes");
    window.ExecuteRecordedEdit(QStringLiteral("텍스처 대상"), [&] {
        document.CreateEntity("Textured cube").AddComponent<MeshRendererComponent>();
    });
    const auto tab = document.GetActiveTabId();
    const auto id = document.GetSelected().GetId();
    auto* choose = window.findChild<QPushButton*>(QStringLiteral("chooseBaseColorTexture"));
    auto* clear = window.findChild<QPushButton*>(QStringLiteral("clearBaseColorTexture"));
    auto* reload = window.findChild<QPushButton*>(QStringLiteral("reloadMeshAssets"));
    auto* path = window.findChild<QLineEdit*>(QStringLiteral("baseColorTexturePath"));
    Check(choose && clear && reload && path, "base color controls exist");
    QTimer picker;
    QObject::connect(&picker, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(QString::fromUtf8(PathUtf8(texture)));
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    picker.start(5);
    choose->click();
    picker.stop();
    const auto assigned =
        document.GetScene().GetComponent<MeshRendererComponent>(id).material.baseColorTexture;
    Check(std::filesystem::equivalent(PathFromUtf8(assigned), texture) && !path->text().isEmpty(),
          "the inspector assigns a base color map to a default cube");
    History(window)->undo();
    Check(document.GetScene()
              .GetComponent<MeshRendererComponent>(id)
              .material.baseColorTexture.empty(),
          "texture assignment is undoable");
    History(window)->redo();
    Check(document.GetScene().GetComponent<MeshRendererComponent>(id).material.baseColorTexture ==
              assigned,
          "texture assignment is redoable");
    Action(window, "copyEntity")->trigger();
    Action(window, "pasteEntity")->trigger();
    const auto copied =
        document.GetSelected().GetComponent<MeshRendererComponent>().material.baseColorTexture;
    Check(document.GetSelected().GetId() != id &&
              std::filesystem::equivalent(PathFromUtf8(copied), texture),
          "entity clipboard copying preserves base-color textures");
    clear->click();
    Check(document.GetSelected()
              .GetComponent<MeshRendererComponent>()
              .material.baseColorTexture.empty(),
          "a material can return to solid color");
    History(window)->undo();
    const auto before = Data(document.GetScene());
    const auto history = History(window)->index();
    const auto dirty = document.IsDirty();
    reload->click();
    Check(Data(document.GetScene()) == before && History(window)->index() == history &&
              document.IsDirty() == dirty,
          "asset reload does not alter scene data, dirty state or history");
    std::ofstream(texture, std::ios::binary | std::ios::trunc) << "broken image";
    reload->click();
    Check(!window.LastLogMessage().isEmpty() && Data(document.GetScene()) == before &&
              History(window)->index() == history,
          "failed texture reload reports an error without changing editing state");
    Check(image.save(QString::fromUtf8(PathUtf8(texture)), "PNG"), "texture source recovers");
    reload->click();
    const auto oldTab = document.GetActiveTabId();
    const auto oldScene = Data(document.GetScene());
    QTimer switchPicker;
    bool switched = false;
    QObject::connect(&switchPicker, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            if (!switched) {
                window.NewSceneDocument();
                switched = true;
            }
            dialog->selectFile(QString::fromUtf8(PathUtf8(texture)));
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    switchPicker.start(5);
    choose->click();
    switchPicker.stop();
    Check(switched && document.GetScene().GetEntityCount() == 0 &&
              Data(*document.GetScene(oldTab)) == oldScene,
          "a delayed file picker never applies an asset to a different active document");
    (void)tab;
}

void TabsAndGlobalUndo() {
    Fixture fixture;
    auto& window = *fixture.window;
    auto& document = window.GetDocument();
    auto* console = window.findChild<QPlainTextEdit*>(QStringLiteral("editorConsole"));
    Check(console && console->toPlainText().isEmpty() && console->placeholderText().isEmpty(),
          "a fresh editor has no filler console messages");
    const auto first = document.GetActiveTabId();
    uint32 firstEntity = 0;
    window.ExecuteRecordedEdit(QStringLiteral("첫 씬 편집"),
                               [&] { firstEntity = document.CreateEntity("첫 선택").GetId(); });
    const auto firstData = Data(document.GetScene());
    const auto second = window.NewSceneDocument();
    Check(second != first && document.ListTabs().size() == 2 && document.IsDirty(first) &&
              !document.IsDirty(second),
          "new scene tab preserves the previous dirty document");
    uint32 secondEntity = 0;
    window.ExecuteRecordedEdit(QStringLiteral("둘째 씬 편집"),
                               [&] { secondEntity = document.CreateEntity("둘째 선택").GetId(); });
    Check(window.SwitchSceneDocument(first, false) &&
              document.GetSelected().GetId() == firstEntity &&
              Data(document.GetScene()) == firstData,
          "switching restores each tab's authored data and selection");
    Check(!window.SwitchSceneDocument(std::numeric_limits<TabId>::max(), false) &&
              document.GetActiveTabId() == first,
          "invalid scene switch does not mutate the active tab");
    History(window)->undo();
    Check(document.GetActiveTabId() == second && document.GetScene().GetEntityCount() == 0 &&
              !document.IsDirty() && document.IsDirty(first),
          "global undo returns to the command's original scene without touching another scene");
    History(window)->redo();
    Check(document.GetActiveTabId() == second && document.GetSelected().GetId() == secondEntity &&
              document.IsDirty(second),
          "global redo restores the correct tab and selection");
    const auto secondData = Data(document.GetScene());
    Check(!window.CloseSceneDocument(second) && document.GetActiveTabId() == second &&
              Data(document.GetScene()) == secondData,
          "closing a dirty tab without consent is rejected");
    auto* tabs = window.findChild<QTabBar*>(QStringLiteral("sceneDocuments"));
    Check(tabs && tabs->count() == 2 && tabs->tabText(0).endsWith(QLatin1Char('*')) &&
              tabs->tabText(1).endsWith(QLatin1Char('*')),
          "scene tabs display the dirty state of both documents");
    Check(console->toPlainText().isEmpty(), "ordinary editing and tab switching do not spam logs");
    Check(window.CloseSceneDocument(second, true) && document.GetScene(second) == nullptr &&
              document.GetActiveTabId() == first && Data(document.GetScene()) == firstData,
          "explicit discard closes only the chosen scene");
    History(window)->undo();
    Check(document.GetScene(second) == nullptr,
          "discarded tab history cannot recreate a closed document");
}

void CrossSceneMove() {
    Fixture fixture;
    auto& window = *fixture.window;
    auto& document = window.GetDocument();
    Scene source, destination;
    auto parent = source.CreateEntity("원본 부모"), selected = source.CreateEntity("이동할 가지"),
         child = source.CreateEntity("함께 이동할 자식");
    const uint32 selectedId = selected.GetId(), childId = child.GetId();
    parent.GetComponent<TransformComponent>().position = {6, 3, 0};
    parent.GetComponent<TransformComponent>().rotation.z = .4f;
    parent.GetComponent<TransformComponent>().scale = {2, 2, 2};
    selected.GetComponent<TransformComponent>().position = {1, 2, 0};
    child.GetComponent<TransformComponent>().position = {.5f, -.25f, 0};
    selected.AddComponent<SpriteRendererComponent>();
    child.AddComponent<SpriteRendererComponent>();
    Check(source.SetParent(selectedId, parent.GetId(), false) &&
              source.SetParent(childId, selectedId, false),
          "cross-scene subtree fixture is valid");
    const auto selectedWorld = source.GetWorldMatrix(selectedId);
    const auto childWorld = source.GetWorldMatrix(childId);
    destination.CreateEntity("ID 충돌 1");
    destination.CreateEntity("ID 충돌 2");
    const auto sourceTab = fixture.Open(source, "source.lux");
    const auto destinationTab = fixture.Open(destination, "destination.lux");
    const auto sourceBefore = Data(*document.GetScene(sourceTab));
    const auto destinationBefore = Data(*document.GetScene(destinationTab));
    Check(window.SwitchSceneDocument(sourceTab, false), "source tab switches");
    Outliner(window)->setCurrentItem(Find(*Outliner(window), selectedId));
    Check(window.MoveSelectionToScene(destinationTab), "selected subtree moves through UI command");
    const auto imported = document.GetSelected().GetId();
    Check(document.GetActiveTabId() == destinationTab && imported != selectedId &&
              document.GetScene(sourceTab)->GetEntityCount() == 1 &&
              document.GetScene(destinationTab)->GetEntityCount() == 4,
          "move remaps conflicting IDs and includes the complete child subtree");
    const auto descendants = document.GetScene().GetChildren(imported);
    Check(descendants.size() == 1, "moved subtree retains its hierarchy");
    EqualMatrix(document.GetScene().GetWorldMatrix(imported), selectedWorld,
                "selected root retains its source world transform");
    EqualMatrix(document.GetScene().GetWorldMatrix(descendants.front()), childWorld,
                "child retains its source world transform");
    Check(document.IsDirty(sourceTab) && document.IsDirty(destinationTab),
          "moving between scenes dirties both files");
    History(window)->undo();
    Check(document.GetActiveTabId() == sourceTab && document.GetSelected().GetId() == selectedId &&
              Data(*document.GetScene(sourceTab)) == sourceBefore &&
              Data(*document.GetScene(destinationTab)) == destinationBefore &&
              !document.IsDirty(sourceTab) && !document.IsDirty(destinationTab),
          "a single undo atomically restores both scenes and their clean states");
    History(window)->redo();
    Check(document.GetActiveTabId() == destinationTab &&
              document.GetScene(sourceTab)->GetEntityCount() == 1 &&
              document.GetScene(destinationTab)->GetEntityCount() == 4,
          "a single redo reapplies both halves of the move");
    History(window)->undo();
    Check(window.CloseSceneDocument(destinationTab), "clean destination tab may close");
    History(window)->redo();
    Check(document.GetScene(destinationTab) == nullptr && Data(document.GetScene()) == sourceBefore,
          "closing either participant invalidates the entire cross-scene history command");
}

void HierarchyActions() {
    Fixture fixture;
    auto& window = *fixture.window;
    Scene scene;
    auto a = scene.CreateEntity("왼쪽"), b = scene.CreateEntity("오른쪽");
    const auto aId = a.GetId(), bId = b.GetId();
    a.GetComponent<TransformComponent>().position = {-2, 1, 0};
    b.GetComponent<TransformComponent>().position = {3, -1, 0};
    const auto originalA = scene.GetWorldMatrix(aId), originalB = scene.GetWorldMatrix(bId);
    fixture.Open(scene, "groups.lux");
    auto* tree = Outliner(window);
    tree->setCurrentItem(Find(*tree, aId));
    Find(*tree, aId)->setSelected(true);
    Find(*tree, bId)->setSelected(true);
    Action(window, "groupEntities")->trigger();
    auto& document = window.GetDocument();
    const auto group = document.GetSelected().GetId();
    Check(document.GetScene().GetEntityCount() == 3 &&
              document.GetSelected().GetComponent<HierarchyComponent>().group &&
              document.GetScene().GetChildren(group) == std::vector<uint32>{aId, bId} &&
              tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 2,
          "group action creates a real parent visible in the tree");
    EqualMatrix(document.GetScene().GetWorldMatrix(aId), originalA, "grouping retains world A");
    EqualMatrix(document.GetScene().GetWorldMatrix(bId), originalB, "grouping retains world B");
    Action(window, "ungroupEntities")->trigger();
    Check(document.GetScene().GetEntityCount() == 2 && !document.GetScene().GetParent(aId) &&
              !document.GetScene().GetParent(bId) && tree->topLevelItemCount() == 2,
          "ungroup removes only the group and promotes its children");
    History(window)->undo();
    Check(document.GetScene().GetParent(aId) == group && tree->topLevelItemCount() == 1,
          "ungroup is reversible with hierarchy and tree state");
    History(window)->undo();
    Check(document.GetScene().GetEntityCount() == 2 && !document.GetScene().GetParent(aId),
          "grouping is independently undoable");
    History(window)->redo();
    Check(document.GetScene().GetParent(bId) == group, "grouping redo restores both children");
}

void GroupClipboard() {
    Fixture fixture;
    auto& window = *fixture.window;
    Scene scene;
    auto group = scene.CreateEntity("복사할 그룹"), sprite = scene.CreateEntity("그룹 스프라이트"),
         mesh = scene.CreateEntity("그룹 메시");
    const auto groupId = group.GetId(), spriteId = sprite.GetId(), meshId = mesh.GetId();
    group.AddComponent<HierarchyComponent>().group = true;
    group.GetComponent<TransformComponent>().position = {2, -1, 0};
    group.GetComponent<TransformComponent>().rotation.z = .35f;
    sprite.GetComponent<TransformComponent>().position = {1, 2, 0};
    sprite.AddComponent<SpriteRendererComponent>().color = {.2f, .4f, .6f, 1};
    mesh.GetComponent<TransformComponent>().position = {-1, .5f, 2};
    mesh.AddComponent<MeshRendererComponent>().material.metallic = .345678f;
    Check(scene.SetParent(spriteId, groupId, false) && scene.SetParent(meshId, groupId, false),
          "clipboard subtree fixture hierarchy is valid");
    const auto initial = Data(scene);
    const auto originalSprite = scene.GetWorldMatrix(spriteId);
    const auto originalMesh = scene.GetWorldMatrix(meshId);
    const auto originalGroup = scene.GetWorldTransform(groupId);
    fixture.Open(scene, "clipboard-group.lux");
    auto& document = window.GetDocument();
    Outliner(window)->setCurrentItem(Find(*Outliner(window), groupId));
    Action(window, "copyEntity")->trigger();
    Check(QApplication::clipboard()->mimeData()->hasFormat(
              QStringLiteral("application/x-lux-scene-subtree")),
          "group clipboard records the full scene subtree payload");
    Action(window, "pasteEntity")->trigger();
    const auto pasted = document.GetSelected().GetId();
    const auto children = document.GetScene().GetChildren(pasted);
    Check(document.GetScene().GetEntityCount() == 6 && pasted != groupId && children.size() == 2 &&
              document.GetSelected().GetComponent<HierarchyComponent>().group,
          "pasting a group clones all descendants with a new parent ID");
    const auto pastedGroup = document.GetScene().GetWorldTransform(pasted);
    const auto offset = pastedGroup.position - originalGroup.position;
    bool sawSprite = false, sawMesh = false;
    for (const auto id : children) {
        Check(id != spriteId && id != meshId && document.GetScene().GetParent(id) == pasted,
              "clipboard remaps every child instead of linking to original entities");
        const auto world = document.GetScene().GetWorldMatrix(id);
        auto expected = document.GetScene().HasComponent<SpriteRendererComponent>(id)
                            ? originalSprite
                            : originalMesh;
        expected.elements[12] += offset.x;
        expected.elements[13] += offset.y;
        expected.elements[14] += offset.z;
        EqualMatrix(world, expected,
                    "group paste preserves descendant world transforms plus the paste offset");
        if (const auto* color = document.GetScene().TryGetComponent<SpriteRendererComponent>(id))
            sawSprite = color->color.y == .4f;
        if (const auto* material = document.GetScene().TryGetComponent<MeshRendererComponent>(id))
            sawMesh = material->material.metallic == .345678f;
    }
    Check(sawSprite && sawMesh, "clipboard retains different descendant component families");
    History(window)->undo();
    Check(Data(document.GetScene()) == initial && document.GetSelected().GetId() == groupId &&
              !document.IsDirty(),
          "one undo restores the original group and clean selection");
    History(window)->redo();
    Check(document.GetScene().GetEntityCount() == 6 && document.GetSelected().GetId() == pasted,
          "one redo restores the complete pasted group");
    History(window)->undo();
    Outliner(window)->setCurrentItem(Find(*Outliner(window), spriteId));
    Action(window, "copyEntity")->trigger();
    Action(window, "pasteEntity")->trigger();
    const auto isolated = document.GetSelected();
    Check(
        document.GetScene().GetEntityCount() == 4 &&
            isolated.HasComponent<SpriteRendererComponent>() &&
            document.GetScene().GetParent(isolated.GetId()) == 0,
        "copying a child alone produces an independent leaf instead of duplicating its ancestors");
    auto expected = originalSprite;
    expected.elements[12] += offset.x;
    expected.elements[13] += offset.y;
    expected.elements[14] += offset.z;
    EqualMatrix(
        document.GetScene().GetWorldMatrix(isolated.GetId()), expected,
        "copying a child uses its world pose rather than its old parent's local coordinates");
}

void Click(QWidget& view, QPointF point) {
    QMouseEvent press(QEvent::MouseButtonPress, point, view.mapToGlobal(point.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, view.mapToGlobal(point.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &release);
}

void Drag(QWidget& view, QPointF start, QPointF end) {
    QMouseEvent press(QEvent::MouseButtonPress, start, view.mapToGlobal(start.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, end, view.mapToGlobal(end.toPoint()), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.mapToGlobal(end.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &move);
    QApplication::sendEvent(&view, &release);
}

void ViewportAffinePicking() {
    {
        EditorDocument dragDocument;
        auto object = dragDocument.CreateEntity("Move directly");
        object.AddComponent<MeshRendererComponent>();
        int transactions = 0;
        SceneViewport3D dragView(
            nullptr, dragDocument, [&](auto id) { dragDocument.Select(id); },
            [&](uint32 id, const TransformComponent& transform) {
                Check(dragDocument.GetScene().SetWorldTransform(id, transform),
                      "world drag applies");
            },
            [&] { ++transactions; }, [&] { --transactions; });
        dragView.resize(800, 600);
        dragView.show();
        Pump();
        const auto camera = MakeViewportCamera3D(dragView.size(), {}, .65, -.48, 22);
        const auto start = *ProjectViewportPoint(camera, dragView.size(), {});
        const auto finish = start + QPointF(32, 18);
        QMouseEvent press(QEvent::MouseButtonPress, start, dragView.mapToGlobal(start.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, finish, dragView.mapToGlobal(finish.toPoint()),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, finish,
                            dragView.mapToGlobal(finish.toPoint()), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(&dragView, &press);
        QApplication::sendEvent(&dragView, &move);
        QApplication::sendEvent(&dragView, &release);
        const auto world = dragDocument.GetScene().GetWorldTransform(object.GetId());
        const auto projected = ProjectViewportPoint(camera, dragView.size(), world.position);
        Check(projected &&
                  std::hypot(projected->x() - finish.x(), projected->y() - finish.y()) < .1 &&
                  transactions == 0,
              "default selection drags a 3D object with the pointer on the ground plane");
    }
    EditorDocument document;
    auto parent = document.CreateEntity("비균일 부모");
    auto& parentTransform = parent.GetComponent<TransformComponent>();
    parentTransform.position = {1, 0, 0};
    parentTransform.rotation.y = .4f;
    parentTransform.scale = {3, .6f, 2};
    auto child = document.CreateEntity("전단이 있는 자식");
    auto& childTransform = child.GetComponent<TransformComponent>();
    childTransform.position = {1, 1, -1};
    childTransform.rotation = {.3f, 0, .8f};
    childTransform.scale = {-.8f, 1.5f, .7f};
    child.AddComponent<MeshRendererComponent>();
    Check(document.GetScene().SetParent(child.GetId(), parent.GetId(), false),
          "3D picking fixture has a sheared mirrored child");
    const auto model = document.GetScene().GetWorldMatrix(child.GetId());
    const auto point = [](const Mat4& matrix, Vec3 local, bool direction = false) {
        const auto& m = matrix.elements;
        return Vec3{m[0] * local.x + m[4] * local.y + m[8] * local.z + (direction ? 0 : m[12]),
                    m[1] * local.x + m[5] * local.y + m[9] * local.z + (direction ? 0 : m[13]),
                    m[2] * local.x + m[6] * local.y + m[10] * local.z + (direction ? 0 : m[14])};
    };
    const auto worldDirection = point(model, {0, 0, -1}, true);
    const auto worldStep = std::sqrt(worldDirection.Dot(worldDirection));
    const ViewportRay3D ray{point(model, {.4f, .4f, 4}), worldDirection.Normalized()};
    const auto distance = IntersectViewportBox(ray, model);
    Check(distance && Near(static_cast<float>(*distance), 3.5f * worldStep),
          "sheared mirrored bounds return the exact local face in world distance");
    Check(!IntersectViewportBox({point(model, {.6f, .2f, 4}), ray.direction}, model),
          "a ray outside the transformed local box misses instead of selecting an enclosing box");
    const auto inside = IntersectViewportBox({point(model, {}), ray.direction}, model);
    Check(inside && Near(static_cast<float>(*inside), .5f * worldStep),
          "rays beginning inside an affine box return its exit face");
    Check(!IntersectViewportBox(ray, Mat4::Scale({1, 0, 1})) &&
              !IntersectViewportBox({{}, {}}, Mat4::Identity()),
          "singular transforms and zero-length rays are not pickable");
    const auto tiny = IntersectViewportBox({{0, 0, .00004f}, {0, 0, -1}},
                                           Mat4::Scale({.00001f, .00001f, .00001f}));
    Check(tiny && Near(static_cast<float>(*tiny), .000035f, .0000001f),
          "invertible tiny models use a scale-relative determinant tolerance");

    int begin = 0, end = 0, edits = 0, cancelled = 0;
    SceneViewport3D view(
        nullptr, document, [&](std::optional<uint32> id) { document.Select(id); },
        [&](uint32, const TransformComponent&) { ++edits; }, [&] { ++begin; }, [&] { ++end; });
    view.SetCancelEditCallback([&] { ++cancelled; });
    view.resize(900, 650);
    view.show();
    Pump();
    const auto camera = MakeViewportCamera3D(view.size(), {}, .65, -.48, 22);
    Check(camera.GetPosition().y > 0, "default perspective looks at the world from above ground");
    const auto screen = ProjectViewportPoint(camera, view.size(), point(model, {.4f, .4f, .2f}));
    Check(screen.has_value(), "sheared child surface is visible to the default camera");
    document.Select(std::nullopt);
    Click(view, *screen);
    Check(document.GetSelected() && document.GetSelected().GetId() == child.GetId(),
          "default viewport camera and affine picking select the visible sheared child");
    begin = end = edits = cancelled = 0;
    view.SetTool(EditorTool::Select);
    QMouseEvent press(QEvent::MouseButtonPress, *screen, view.mapToGlobal(screen->toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    Check(begin == 1, "drag opens a transaction for a valid parented child");
    parent.GetComponent<TransformComponent>().scale.x = std::numeric_limits<float>::infinity();
    const auto moved = *screen + QPointF(20, 10);
    QMouseEvent move(QEvent::MouseMove, moved, view.mapToGlobal(moved.toPoint()), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, moved, view.mapToGlobal(moved.toPoint()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &move);
    QApplication::sendEvent(&view, &release);
    Check(begin == 1 && end == 0 && cancelled == 1 && edits == 0,
          "an invalidated parent cancels exactly once without committing or throwing through Qt");
}

void ViewportWorldEditing() {
    EditorDocument document;
    auto& scene = document.GetScene();
    auto parent = scene.CreateEntity("전단 부모"), child = scene.CreateEntity("움직일 스프라이트");
    parent.GetComponent<TransformComponent>().position = {1, 1, 0};
    parent.GetComponent<TransformComponent>().rotation.z = ToRadians(90);
    parent.GetComponent<TransformComponent>().scale = {2, 1, 1};
    child.GetComponent<TransformComponent>().position = {1, 0, 0};
    child.GetComponent<TransformComponent>().rotation.z = .35f;
    child.GetComponent<TransformComponent>().scale = {1.4f, .8f, 1};
    child.AddComponent<SpriteRendererComponent>();
    Check(scene.SetParent(child.GetId(), parent.GetId(), false), "viewport affine hierarchy setup");
    document.Select(std::nullopt);
    int balance = 0, movements = 0, paints = 0;
    bool validMovement = true;
    SceneViewport2D viewport(
        nullptr, document, [&](auto id) { document.Select(id); },
        [&](uint32 id, const TransformComponent& world) {
            validMovement = validMovement && scene.SetWorldTransform(id, world);
            ++movements;
        },
        [&](uint32 id, uint32 x, uint32 y, uint8_t tile) {
            if (auto* map = scene.TryGetComponent<TilemapComponent>(id)) {
                map->Set(x, y, tile);
                ++paints;
            }
        },
        [&] { ++balance; }, [&] { --balance; });
    viewport.resize(960, 720);
    viewport.SetSnapToGrid(false);
    viewport.show();
    Pump();
    const auto screen = [&](Vec3 world) {
        return QPointF(viewport.width() * .5 + world.x * 72, viewport.height() * .5 - world.y * 72);
    };
    const auto initial = scene.GetWorldTransform(child.GetId());
    const auto local = child.GetComponent<TransformComponent>();
    Click(viewport, screen(initial.position));
    Check(document.GetSelected() == child, "world-space picking hits the transformed child");
    viewport.SetTool(EditorTool::Select);
    Drag(viewport, screen(initial.position), screen(initial.position) + QPointF(72, -36));
    const auto moved = scene.GetWorldTransform(child.GetId());
    Check(validMovement && movements > 0 && balance == 0 &&
              Near(moved.position.x, initial.position.x + 1) &&
              Near(moved.position.y, initial.position.y + .5f) &&
              child.GetComponent<TransformComponent>().rotation.z == local.rotation.z &&
              child.GetComponent<TransformComponent>().scale.x == local.scale.x,
          "viewport drags in world space while preserving the sheared child's local axes");

    auto mapParent = scene.CreateEntity("타일 부모"), mapEntity = scene.CreateEntity("타일맵");
    mapParent.GetComponent<TransformComponent>().position = {-1, -1, 0};
    mapParent.GetComponent<TransformComponent>().rotation.z = ToRadians(90);
    mapParent.GetComponent<TransformComponent>().scale = {2, 1, 1};
    auto& map = mapEntity.AddComponent<TilemapComponent>();
    map.Resize(3, 2);
    map.tileSize = .5f;
    Check(scene.SetParent(mapEntity.GetId(), mapParent.GetId(), false), "parented tilemap setup");
    const auto cellCenter = [&](float x, float y) {
        const auto matrix = scene.GetWorldMatrix(mapEntity.GetId());
        const auto* m = matrix.elements;
        return screen({m[0] * x + m[4] * y + m[12], m[1] * x + m[5] * y + m[13], 0});
    };
    document.Select(std::nullopt);
    viewport.SetTool(EditorTool::TileBrush);
    viewport.SetTileId(4);
    Click(viewport, cellCenter(.75f, .25f));
    Check(paints == 1 && map.Get(1, 0) == 4 && map.Get(0, 0) == 0 && balance == 0,
          "tile brush maps the world pointer through its transformed parent to the correct cell");
    mapParent.GetComponent<TransformComponent>().position = {-2, -2, 0};
    mapParent.GetComponent<TransformComponent>().rotation.z = .7f;
    mapParent.GetComponent<TransformComponent>().scale = {1.5f, 2, 1};
    viewport.SetTileId(2);
    Click(viewport, cellCenter(1.25f, .75f));
    Check(paints == 2 && map.Get(2, 1) == 2 && map.Get(1, 0) == 4 && balance == 0,
          "painting follows a parent changed after the viewport was created");
    viewport.SetTool(EditorTool::TileErase);
    Click(viewport, cellCenter(.75f, .25f));
    Check(paints == 3 && map.Get(1, 0) == 0 && map.Get(2, 1) == 2 && balance == 0,
          "eraser uses the same inverse-world coordinates and completes its transaction");
}

void AnimationWorkspace() {
    Fixture fixture;
    auto& window = *fixture.window;
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("AnimationWorkspace"));
    auto* enabled = window.findChild<QCheckBox*>(QStringLiteral("editorAnimation"));
    Check(dock && enabled && dock->isAncestorOf(enabled),
          "animation controls belong to their own dock");
    auto entity = window.GetDocument().CreateEntity("Animated");
    window.ExecuteRecordedEdit(QStringLiteral("select"),
                               [&] { window.GetDocument().Select(entity.GetId()); });
    enabled->setChecked(true);
    Check(window.GetDocument().GetSelected().HasComponent<AnimationComponent>(),
          "workspace adds animation to the selected target");
    auto* clip = window.findChild<QLineEdit*>(QStringLiteral("animationClipName"));
    clip->setText(QStringLiteral("Walk"));
    QMetaObject::invokeMethod(clip, "editingFinished", Qt::DirectConnection);
    Check(window.GetDocument().GetSelected().GetComponent<AnimationComponent>().clip.name == "Walk",
          "clip rename updates engine data");
    window.GetDocument().GetSelected().GetComponent<AnimationComponent>().clip.duration = 120;
    window.findChild<QPushButton*>(QStringLiteral("animationNextFrame"))->click();
    Check(window.GetDocument().GetSelected().GetComponent<AnimationComponent>().currentTime > 0,
          "frame step advances animation time");
    Check(Near(window.GetDocument().GetSelected().GetComponent<AnimationComponent>().currentTime,
               1.0f / 30),
          "frame stepping keeps sub-slider precision on long clips");
    window.findChild<QPushButton*>(QStringLiteral("animationPlay"))->click();
    Pump(80);
    window.findChild<QPushButton*>(QStringLiteral("animationStop"))->click();
    Check(window.GetDocument().GetSelected().GetComponent<AnimationComponent>().currentTime == 0,
          "stop returns to the beginning");
    auto* keys = window.findChild<QListWidget*>(QStringLiteral("animationKeys"));
    keys->setCurrentRow(0);
    QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_Delete, Qt::NoModifier);
    QKeyEvent erase(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(keys, &shortcut);
    QApplication::sendEvent(keys, &erase);
    Check(shortcut.isAccepted() && window.GetDocument().GetSelected() &&
              window.GetDocument()
                  .GetSelected()
                  .GetComponent<AnimationComponent>()
                  .clip.keyframes.empty(),
          "Delete in the key list removes a key while preserving its scene object");
}

void AsyncAndDuplicateOpen() {
    Fixture fixture;
    auto& window = *fixture.window;
    auto& document = window.GetDocument();
    Scene first, second, third;
    first.CreateEntity("원본 A");
    second.CreateEntity("원본 B");
    third.CreateEntity("비동기 C");
    const auto a = fixture.Open(first, "async-a.lux");
    const auto b = fixture.Open(second, "async-b.lux");
    window.ExecuteRecordedEdit(QStringLiteral("B 수정"), [&] {
        document.Select(1);
        document.GetScene().GetComponent<TransformComponent>(1).position.x = 3;
    });
    const auto bDirty = Data(document.GetScene());
    Check(window.SwitchSceneDocument(a, false), "A switches before duplicate B open");
    Check(window.OpenSceneDocument(fixture.Path(".") / "async-b.lux") &&
              document.GetActiveTabId() == b && document.ListTabs().size() == 2 &&
              document.IsDirty() && Data(document.GetScene()) == bDirty,
          "opening an alias of an already dirty file activates it without reloading data");
    const auto before = window.LastLogMessage();
    Check(window.OpenSceneDocument(fixture.Path("missing.lux"), true),
          "async failure request queues");
    Wait([&] { return window.LastLogMessage() != before; }, "async file failure is reported");
    Check(document.GetActiveTabId() == b && document.ListTabs().size() == 2 &&
              Data(document.GetScene()) == bDirty,
          "async failure preserves the active tab and all existing edits");

    const auto file = fixture.Path("async-race.lux");
    Check(third.Serialize(PathUtf8(file)), "async race fixture saves");
    Check(window.OpenSceneDocument(file, true),
          "async scene request queues before synchronous open");
    Check(window.OpenSceneDocument(file), "same scene synchronously opens before async completion");
    const auto c = document.GetActiveTabId();
    const auto clean = Data(document.GetScene());
    window.ExecuteRecordedEdit(QStringLiteral("로드 완료 전 수정"), [&] {
        document.Select(1);
        document.GetScene().GetComponent<TransformComponent>(1).position.y = 7;
    });
    const auto edited = Data(document.GetScene());
    Wait([&] { return window.PendingSceneLoadCount() == 0; },
         "late duplicate asynchronous request completes");
    Check(document.GetActiveTabId() == c && document.ListTabs().size() == 3 &&
              Data(document.GetScene()) == edited && document.IsDirty(c),
          "late async completion cannot replace an already edited synchronous scene");
    History(window)->undo();
    Check(document.GetActiveTabId() == c && Data(document.GetScene()) == clean &&
              !document.IsDirty(c),
          "late duplicate completion cannot reset the clean baseline to an edited snapshot");
    Check(Data(*document.GetScene(b)) == bDirty && document.IsDirty(b),
          "async completion and undo leave other dirty scenes unchanged");
    const auto modalFile = fixture.Path("async-during-dialog.lux");
    Check(third.Serialize(PathUtf8(modalFile)) && window.OpenSceneDocument(modalFile, true),
          "background scene queues before a modal property or save dialog opens");
    QDialog dialog(&window);
    dialog.setModal(true);
    dialog.show();
    Wait([&] { return window.PendingSceneLoadCount() == 0; },
         "asynchronous scene finishes while a modal dialog remains open");
    Check(document.FindTabByPath(PathUtf8(modalFile)) && document.GetActiveTabId() == c &&
              QApplication::activeModalWidget() == &dialog,
          "async completion cannot redirect a modal edit or save onto another scene");
    dialog.hide();
    const auto duplicateFile = fixture.Path("async-duplicate-during-dialog.lux");
    Check(third.Serialize(PathUtf8(duplicateFile)) &&
              window.OpenSceneDocument(duplicateFile, true) &&
              window.OpenSceneDocument(duplicateFile) && window.SwitchSceneDocument(c, false),
          "queued scene already has a tab before the original document's dialog opens");
    dialog.show();
    Wait([&] { return window.PendingSceneLoadCount() == 0; },
         "late duplicate request completes while modal dialog remains open");
    Check(document.GetActiveTabId() == c && QApplication::activeModalWidget() == &dialog,
          "late duplicate completion also cannot redirect a modal edit or save");
    dialog.hide();
}

void SettingsPrecisionAndHistoryBound() {
    Fixture fixture;
    auto& window = *fixture.window;
    Scene scene;
    scene.CreateEntity("설정 확인");
    auto settings = scene.GetSettings();
    settings.clearColor = {.1234567f, .4567891f, .7891234f, .9876543f};
    settings.exposure = 1.234567f;
    settings.gravity = {12345.678f, -.1234567f};
    settings.gravity3D = {-.9876543f, 23456.789f, .2468135f};
    settings.fixedStep = 1.0f / 73;
    Check(scene.SetSettings(settings), "precise scene settings fixture is valid");
    fixture.Open(scene, "settings.lux");
    auto& document = window.GetDocument();
    const auto before = Data(document.GetScene());
    const auto editDialog = [&](bool changeExposure) {
        bool handled = false, fieldsFound = false;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            handled = dialog->windowTitle() == QStringLiteral("씬 설정");
            const auto fields = dialog->findChildren<QDoubleSpinBox*>();
            fieldsFound = fields.size() >= 11;
            if (changeExposure && fieldsFound)
                fields[4]->setValue(.7);
            dialog->accept();
        });
        timer.start(0);
        Action(window, "sceneSettings")->trigger();
        timer.stop();
        Check(handled && fieldsFound, "real scene settings dialog is shown and accepted");
    };
    const auto history = History(window)->count();
    editDialog(false);
    Check(Data(document.GetScene()) == before && !document.IsDirty() &&
              History(window)->count() == history,
          "accepting untouched settings preserves exact floats and creates no undo noise");
    editDialog(true);
    const auto actual = document.GetScene().GetSettings();
    Check(actual.exposure == .7f && actual.clearColor.x == settings.clearColor.x &&
              actual.clearColor.w == settings.clearColor.w &&
              actual.gravity.x == settings.gravity.x &&
              actual.gravity3D.y == settings.gravity3D.y && actual.fixedStep == settings.fixedStep,
          "editing exposure preserves precise and out-of-display-range untouched fields");
    History(window)->undo();
    Check(Data(document.GetScene()) == before && !document.IsDirty(),
          "settings undo restores the exact clean state");

    const auto entity = document.GetScene().GetEntities().front().GetId();
    document.Select(entity);
    History(window)->clear();
    for (int edit = 1; edit <= 150; ++edit)
        window.ExecuteRecordedEdit(QStringLiteral("반복 편집"), [&] {
            document.GetScene().GetComponent<TransformComponent>(entity).position.x =
                static_cast<float>(edit);
        });
    Check(History(window)->count() == 128 && History(window)->index() == 128,
          "long editing sessions cap retained scene commands at 128");
    while (History(window)->canUndo())
        History(window)->undo();
    Check(document.GetScene().IsAlive(entity) &&
              document.GetScene().GetComponent<TransformComponent>(entity).position.x == 22 &&
              document.IsDirty(),
          "eviction preserves current data and never falsely marks a partial undo as clean");
    while (History(window)->canRedo())
        History(window)->redo();
    Check(document.GetScene().GetComponent<TransformComponent>(entity).position.x == 150,
          "all retained edits remain redoable after history eviction");
}

void DuplicateRestoresPreviewSource() {
    Fixture fixture;
    auto& window = *fixture.window;
    auto& document = window.GetDocument();
    const auto source = document.GetActiveTabId();
    const auto actor = document.CreateEntity("미리보기 복원 확인").GetId();
    Action(window, "previewStart")->trigger();
    Action(window, "previewPause")->trigger();
    document.GetScene().GetComponent<TransformComponent>(actor).position.y = 9;
    bool accepted = false;
    QTimer picker;
    QObject::connect(&picker, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(QString::fromUtf8(PathUtf8(fixture.Path("duplicate.lux"))));
            accepted = true;
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    picker.start(5);
    Action(window, "duplicateScene")->trigger();
    picker.stop();
    const auto duplicate = document.GetActiveTabId();
    Action(window, "previewStop")->trigger();
    Check(accepted && source != duplicate && document.GetScene(source)->IsAlive(actor) &&
              Near(document.GetScene(source)->GetComponent<TransformComponent>(actor).position.y,
                   0) &&
              Near(document.GetScene(duplicate)->GetComponent<TransformComponent>(actor).position.y,
                   0),
          "duplicating during preview restores the original and copies authored state");
}

struct ProjectSessionFixture {
    QTemporaryDir files;
    EditorProject project;

    ProjectSessionFixture() {
        Check(files.isValid(), "project session fixture directory exists");
        std::string error;
        const auto root = PathFromUtf8(files.path().toUtf8().toStdString());
        auto created =
            EditorProject::Create(root / "Project", "Session", ProjectTemplate::Empty, error);
        Check(created.has_value(), error.c_str());
        project = std::move(*created);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("sceneTransitions"), false);
        QSettings().setValue(QStringLiteral("lastProject"),
                             QString::fromUtf8(PathUtf8(project.GetFile())));
    }
};

void InactiveSessionRecoversAutosave() {
    ProjectSessionFixture fixture;
    const auto secondary = fixture.project.GetSceneFile().parent_path() / "Secondary.lux";
    Scene source;
    source.CreateEntity("저장된 항목");
    Check(source.Serialize(PathUtf8(secondary)), "inactive session scene saves");
    source.CreateEntity("자동 저장된 작업");
    std::string error;
    Check(fixture.project.SaveAutosave(secondary, source, error), "inactive newer autosave saves");
    std::filesystem::last_write_time(secondary, std::filesystem::file_time_type::clock::now() -
                                                    std::chrono::minutes(5));
    QJsonObject record;
    record.insert(QStringLiteral("path"), QString::fromUtf8(PathUtf8(secondary)));
    record.insert(QStringLiteral("project"),
                  QString::fromUtf8(PathUtf8(fixture.project.GetFile())));
    QSettings().setValue(QStringLiteral("openSceneDocuments"),
                         QJsonDocument(QJsonArray{record}).toJson());
    int prompts = 0;
    QTimer recovery;
    QObject::connect(&recovery, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            ++prompts;
            dialog->button(QMessageBox::Yes)->click();
        }
    });
    recovery.start(5);
    EditorWindow window;
    recovery.stop();
    const auto restored = window.GetDocument().FindTabByPath(PathUtf8(secondary));
    Check(prompts == 1 && restored && window.GetDocument().IsDirty(restored) &&
              window.GetDocument().GetScene(restored)->GetEntityCount() == 2 &&
              window.GetDocument().GetActiveTabId() != restored,
          "inactive session autosave is offered once and recovered without stealing focus");
}

void AsyncOwnerUsesLatestStartup() {
    ProjectSessionFixture fixture;
    EditorWindow window;
    Check(Action(window, "projectSettings")->isEnabled(),
          "async startup fixture owns its initial project");
    const auto directory = fixture.project.GetSceneFile().parent_path();
    const auto pending = directory / "Pending.lux", latest = directory / "NewStart.lux";
    Scene scene;
    scene.CreateEntity("시작 씬 소유권 확인");
    Check(scene.Serialize(PathUtf8(pending)) && scene.Serialize(PathUtf8(latest)) &&
              window.OpenSceneDocument(pending, true) && window.OpenSceneDocument(latest),
          "scene load is pending when another scene becomes current");
    Check(std::filesystem::equivalent(PathFromUtf8(window.GetDocument().GetPath()), latest),
          "the synchronous destination is active before changing startup");
    Action(window, "setStartupScene")->trigger();
    std::string startupError;
    const auto updatedProject = EditorProject::Open(fixture.project.GetFile(), startupError);
    Check(updatedProject && std::filesystem::equivalent(updatedProject->GetSceneFile(), latest),
          "the new startup scene is saved before waiting for async completion");
    Wait([&] { return window.PendingSceneLoadCount() == 0; }, "pending project scene finishes");
    const auto tab = window.GetDocument().FindTabByPath(PathUtf8(pending));
    Check(tab && window.SwitchSceneDocument(tab, false), "async scene activates after completion");
    QTimer modal;
    QObject::connect(&modal, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            dialog->accept();
    });
    const auto previousDriver = qgetenv("SDL_VIDEO_DRIVER");
    qputenv("SDL_VIDEO_DRIVER", "dummy");
    modal.start(5);
    Action(window, "runStartupScene")->trigger();
    modal.stop();
    Action(window, "playWindowStop")->trigger();
    if (previousDriver.isNull())
        qunsetenv("SDL_VIDEO_DRIVER");
    else
        qputenv("SDL_VIDEO_DRIVER", previousDriver);
    if (!std::filesystem::equivalent(PathFromUtf8(window.GetDocument().GetPath()), latest))
        std::cerr << "async startup: active=" << window.GetDocument().GetPath()
                  << " expected=" << PathUtf8(latest)
                  << " projectEnabled=" << Action(window, "projectSettings")->isEnabled()
                  << " log=" << window.LastLogMessage().toUtf8().toStdString() << '\n';
    Check(std::filesystem::equivalent(PathFromUtf8(window.GetDocument().GetPath()), latest),
          "async owner preserves a startup scene changed while its load was pending");
    const auto external = PathFromUtf8(fixture.files.path().toUtf8().toStdString()) / "Outside.lux";
    Check(scene.Serialize(PathUtf8(external)) && window.OpenSceneDocument(external, true),
          "external asynchronous scene queues from an owned project");
    Wait([&] { return window.PendingSceneLoadCount() == 0; }, "external async scene finishes");
    const auto externalTab = window.GetDocument().FindTabByPath(PathUtf8(external));
    Check(externalTab && window.SwitchSceneDocument(externalTab, false) &&
              !Action(window, "projectSettings")->isEnabled(),
          "loaded canonical paths prevent external scenes from retaining project ownership");
}

void CaptureMeshScene(const QString& destination, const QString& source) {
    Check(QFileInfo(destination).isAbsolute() && QFileInfo(source).isAbsolute(),
          "scene capture paths must be absolute");
    QSettings().clear();
    EditorWindow window;
    window.resize(1600, 1000);
    Check(window.OpenSceneDocument(PathFromUtf8(source.toUtf8().toStdString())),
          "mesh demonstration scene opens");
    window.show();
    Action(window, "view3D")->trigger();
    Pump(150);
    if (window.GetDocument().GetScene().GetSettings().render3D.mode ==
        LuxEngine::Render3DMode::Pbr) {
        for (int i = 0; i < 60; ++i) {
            for (auto* widget : window.findChildren<QWidget*>())
                if (dynamic_cast<SceneViewport3D*>(widget))
                    widget->repaint();
            Pump(100);
        }
    }
    Check(window.grab().save(destination), "mesh demonstration capture saves");
    bool viewportSaved = false;
    for (auto* widget : window.findChildren<QWidget*>())
        if (auto* viewport = dynamic_cast<SceneViewport3D*>(widget)) {
            const auto file = QFileInfo(destination).absolutePath() + '/' +
                              QFileInfo(destination).completeBaseName() + "-viewport.png";
            viewportSaved = viewport->grab().save(file);
        }
    Check(viewportSaved, "mesh viewport capture saves");
    for (const auto& tab : window.GetDocument().ListTabs())
        window.GetDocument().SetDirty(tab.id, false);
    window.close();
    Pump();
}

void CaptureEditor(const QString& destination) {
    Check(QFileInfo(destination).isAbsolute(), "capture destination must be an absolute path");
    ProjectSessionFixture fixture;
    Scene scene;
    auto settings = scene.GetSettings();
    settings.clearColor = {.105f, .125f, .15f, 1};
    Check(scene.SetSettings(settings), "capture scene settings apply");
    auto world = scene.CreateEntity("월드 / Forest");
    world.AddComponent<HierarchyComponent>().group = true;
    auto environment = scene.CreateEntity("환경 / 3D 구조물");
    environment.AddComponent<HierarchyComponent>().group = true;
    Check(scene.SetParent(environment.GetId(), world.GetId(), false), "capture group parents");
    const auto mesh = [&](const char* name, Vec3 position, Vec3 scale, Vec4 color) {
        auto entity = scene.CreateEntity(name);
        auto& transform = entity.GetComponent<TransformComponent>();
        transform.position = position;
        transform.scale = scale;
        auto& material = entity.AddComponent<MeshRendererComponent>().material;
        material.albedoColor = color;
        material.roughness = .7f;
        Check(scene.SetParent(entity.GetId(), environment.GetId(), false), "capture mesh parents");
        return entity.GetId();
    };
    mesh("지면", {0, -.3f, 0}, {10, .4f, 7}, {.25f, .38f, .23f, 1});
    mesh("돌담 / 서쪽", {-3, .8f, -1.5f}, {.6f, 2, 4}, {.5f, .55f, .56f, 1});
    mesh("돌담 / 북쪽", {0, .8f, -3}, {6, 2, .5f}, {.5f, .55f, .56f, 1});
    const auto selected =
        mesh("보급 상자", {1.5f, .65f, .5f}, {1.4f, 1.5f, 1.4f}, {.62f, .39f, .18f, 1});
    mesh("관측 기둥", {-1, 1.4f, -1}, {.8f, 3, .8f}, {.38f, .58f, .66f, 1});
    auto layer = scene.CreateEntity("게임플레이 / 2D 레이어");
    layer.AddComponent<HierarchyComponent>().group = true;
    Check(scene.SetParent(layer.GetId(), world.GetId(), false), "capture layer parents");
    auto terrain = scene.CreateEntity("지형 타일맵");
    terrain.GetComponent<TransformComponent>().position = {-3, -2, 0};
    auto& tiles = terrain.AddComponent<TilemapComponent>();
    Check(tiles.Resize(12, 3), "capture tilemap dimensions apply");
    tiles.tileSize = .5f;
    for (uint32 y = 0; y < tiles.height; ++y)
        for (uint32 x = 0; x < tiles.width; ++x)
            tiles.Set(x, y, y == 2 ? 1 : 2);
    Check(scene.SetParent(terrain.GetId(), layer.GetId(), false), "capture tilemap parents");
    const auto sprite = [&](const char* name, Vec3 position, Vec3 scale, Vec4 color) {
        auto entity = scene.CreateEntity(name);
        entity.GetComponent<TransformComponent>().position = position;
        entity.GetComponent<TransformComponent>().scale = scale;
        entity.AddComponent<SpriteRendererComponent>().color = color;
        Check(scene.SetParent(entity.GetId(), layer.GetId(), false), "capture sprite parents");
    };
    sprite("플레이어", {0, 0, .1f}, {.65f, 1.2f, 1}, {.4f, .72f, .9f, 1});
    sprite("수풀 / 왼쪽", {-2, -.2f, 0}, {.9f, .7f, 1}, {.3f, .6f, .25f, 1});
    sprite("수풀 / 오른쪽", {2, -.2f, 0}, {1.1f, .8f, 1}, {.35f, .66f, .28f, 1});
    Check(scene.Serialize(PathUtf8(fixture.project.GetSceneFile())), "capture main scene saves");
    Scene interior;
    interior.CreateEntity("실내 시작 지점");
    const auto second = fixture.project.GetSceneFile().parent_path() / "Interior.lux";
    Check(interior.Serialize(PathUtf8(second)), "capture second scene saves");

    EditorWindow window;
    window.resize(1600, 1000);
    window.show();
    Pump(40);
    const auto main = window.GetDocument().GetActiveTabId();
    Check(window.OpenSceneDocument(second) && window.SwitchSceneDocument(main, false),
          "capture displays multiple scene documents");
    auto* tree = Outliner(window);
    tree->expandAll();
    tree->setCurrentItem(Find(*tree, selected));
    Action(window, "view3D")->trigger();
    auto* animationEnabled = window.findChild<QCheckBox*>(QStringLiteral("editorAnimation"));
    animationEnabled->setChecked(true);
    window.findChild<QDockWidget*>(QStringLiteral("AnimationWorkspace"))->raise();
    Pump(80);
    Check(QDir().mkpath(QFileInfo(destination).absolutePath()) &&
              window.grab().save(destination, "PNG"),
          "actual editor window capture saves");
    for (const auto& tab : window.GetDocument().ListTabs())
        window.GetDocument().SetDirty(tab.id, false);
    window.close();
    Pump();
}
} // namespace

int main(int argc, char** argv) {
    qputenv("LUX_EDITOR_SOFTWARE_VIEWPORT", "1");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxSceneUiTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EditorSceneUi"));
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    EngineSceneRenderer::SetForceAllowed(false);
    try {
        const QString koreanOutput = QStringLiteral("한글 빌드 경고: 프로젝트/캐릭터 🙂");
        Check(DecodeBuildOutput(koreanOutput.toUtf8()) == koreanOutput,
              "UTF-8 build output preserves Korean paths and supplementary characters");
        const QString replacementOutput = koreanOutput + QChar::ReplacementCharacter;
        Check(DecodeBuildOutput(replacementOutput.toUtf8()) == replacementOutput,
              "valid UTF-8 replacement characters do not trigger legacy decoding");
#ifdef _WIN32
        // An English ANSI code page cannot encode Hangul. Use fixed non-UTF-8 bytes
        // ("한글" in CP949) and the actual system code page instead of a lossy round trip.
        const QByteArray legacyOutput = QByteArray::fromHex("c7d1b1db");
        Check(DecodeBuildOutput(legacyOutput) == QString::fromLocal8Bit(legacyOutput),
              "legacy Windows tool output follows the active ANSI code page");
#endif
        const auto capture = qEnvironmentVariable("LUX_EDITOR_SCENE_CAPTURE");
        if (!capture.isEmpty()) {
            if (qEnvironmentVariableIntValue("LUX_EDITOR_SCENE_CAPTURE_GPU") == 1) {
                qunsetenv("LUX_EDITOR_SOFTWARE_VIEWPORT");
                EngineSceneRenderer::SetForceAllowed(true);
            }
            const auto source = qEnvironmentVariable("LUX_EDITOR_SCENE_CAPTURE_SOURCE");
            if (source.isEmpty())
                CaptureEditor(capture);
            else
                CaptureMeshScene(capture, source);
            return 0;
        }
        const std::string_view suite = argc > 1 ? argv[1] : "all";
        bool found = suite == "all";
        for (const auto& [name, run] : std::array<std::pair<std::string_view, void (*)()>, 14>{
                 {{"base-color", BaseColorEditingAndReload},
                  {"mesh-assets", MeshAssetEditing},
                  {"animation-workspace", AnimationWorkspace},
                  {"tabs", TabsAndGlobalUndo},
                  {"move", CrossSceneMove},
                  {"hierarchy", HierarchyActions},
                  {"clipboard", GroupClipboard},
                  {"viewport", ViewportWorldEditing},
                  {"viewport3d", ViewportAffinePicking},
                  {"async", AsyncAndDuplicateOpen},
                  {"settings", SettingsPrecisionAndHistoryBound},
                  {"duplicate-preview", DuplicateRestoresPreviewSource},
                  {"session-recovery", InactiveSessionRecoversAutosave},
                  {"async-startup", AsyncOwnerUsesLatestStartup}}})
            if (suite == "all" || suite == name) {
                found = true;
                run();
            }
        Check(found, "unknown scene UI regression suite");
        std::cout
            << "Scene tabs, world editing, async safety and global history UI checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scene UI regression failed: " << error.what() << '\n';
        return 1;
    }
}
