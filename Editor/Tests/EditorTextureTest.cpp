#include "App/EditorWindow.hpp"
#include "Project/EditorProject.hpp"
#include "Project/PathEncoding.hpp"
#include "Scene/EditorDocument.hpp"
#include "Viewport/EngineSceneRenderer.hpp"
#include "Viewport/SceneViewport2D.hpp"
#include "Viewport/SceneViewport3D.hpp"
#include "Viewport/SpriteImageCache.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMimeData>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
using namespace LuxEngine;
using namespace LuxEditor;
namespace fs = std::filesystem;

void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Fixture {
    QTemporaryDir temporary;
    fs::path root;
    fs::path scene;

    Fixture() {
        Check(temporary.isValid(), "texture fixture temporary directory");
        const auto base = PathFromUtf8(temporary.path().toUtf8().toStdString());
        std::string error;
        auto project = EditorProject::Create(base / "project", "Texture", ProjectTemplate::Empty,
                                              error);
        Check(project.has_value(), ("texture fixture project: " + error).c_str());
        root = project->GetFile().parent_path();
        scene = project->GetSceneFile();
        QImage image(4, 4, QImage::Format_RGBA8888);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                image.setPixelColor(x, y, y < 2 ? (x < 2 ? QColor(255, 0, 0) : QColor(0, 255, 0))
                                                 : (x < 2 ? QColor(0, 0, 255)
                                                          : QColor(255, 255, 255, 0)));
        Check(image.save(QString::fromUtf8(PathUtf8(root / PathFromUtf8("Assets/한글.png")))),
              "texture fixture image");
        image.fill(QColor(20, 200, 60));
        Check(image.save(QString::fromUtf8(PathUtf8(root / "Assets/solid.png"))),
              "solid texture fixture image");
    }

    void Populate(EditorDocument& document) const {
        Check(document.Open(PathUtf8(scene)), "open texture project scene");
        auto entity = document.CreateEntity("Texture");
        entity.GetComponent<TransformComponent>().scale = {2, 2, 1};
        entity.AddComponent<SpriteRendererComponent>().texturePath = "Assets/한글.png";
    }
};

void TestDocument() {
    Fixture files;
    EditorDocument document;
    files.Populate(document);
    const std::string root = document.GetScene().GetAssetRoot();
    Check(fs::equivalent(PathFromUtf8(root), files.root), "document infers project asset root");
    const auto id = document.GetSelected().GetId();
    Check(document.Save(), "textured document saves");
    Check(document.Open(PathUtf8(files.scene)), "textured document reopens");
    document.Select(id);
    Check(document.GetSelected().GetComponent<SpriteRendererComponent>().texturePath ==
              "Assets/한글.png", "texture path survives document reopen");
    std::string snapshot;
    Check(document.GetScene().SerializeToString(snapshot), "texture undo snapshot");
    document.GetSelected().GetComponent<SpriteRendererComponent>().texturePath.clear();
    Check(document.ReplaceSceneFromData(snapshot, id), "texture undo restores scene");
    Check(document.GetScene().GetAssetRoot() == root &&
              document.GetSelected().GetComponent<SpriteRendererComponent>().texturePath ==
                  "Assets/한글.png", "texture undo preserves root and path");

    const auto autosave = files.root.parent_path() / "autosave.lux";
    Check(document.GetScene().Serialize(PathUtf8(autosave)), "external autosave snapshot");
    Check(document.ReplaceScene(PathUtf8(autosave), id) &&
              document.GetScene().GetAssetRoot() == root,
          "file-backed history preserves the document asset root");
    Check(document.Recover(PathUtf8(files.scene), PathUtf8(autosave)) &&
              document.GetScene().GetAssetRoot() == root,
          "recovery preserves the original scene asset root");

    const auto sameRoot = files.root / "Scenes/Sub/Copy.lux";
    fs::create_directories(sameRoot.parent_path());
    Check(document.SaveAs(PathUtf8(sameRoot)), "save as within the project root");
    const auto external = files.root.parent_path() / "External.lux";
    std::ofstream(external) << "previous";
    document.MarkDirty();
    Check(!document.SaveAs(PathUtf8(external)) && document.IsDirty() &&
              document.GetPath() == PathUtf8(sameRoot) &&
              document.GetLastError().find("다른 에셋 루트") != std::string::npos,
          "save as outside the texture root fails with an explanation");
    std::string previous;
    std::ifstream(external) >> previous;
    Check(previous == "previous", "rejected save as preserves the destination file");
    document.NewScene();
    Check(document.GetScene().GetAssetRoot() == root, "new project scene preserves asset root");
}

void TestSaveAsRootTransition() {
    Fixture first;
    Fixture second;
    QImage source(2, 2, QImage::Format_RGBA8888);
    source.fill(Qt::red);
    Check(source.save(QString::fromUtf8(PathUtf8(first.root / "Assets/shared.png"))),
          "first project root transition image");
    source.fill(Qt::green);
    Check(source.save(QString::fromUtf8(PathUtf8(second.root / "Assets/shared.png"))),
          "second project root transition image");
    EditorDocument document;
    Check(document.Open(PathUtf8(first.scene)) && document.SaveAs(PathUtf8(second.scene)),
          "untextured scene saves into a different project root");
    Check(fs::equivalent(PathFromUtf8(document.GetScene().GetAssetRoot()), second.root),
          "successful save as switches the document asset root immediately");
    auto entity = document.CreateEntity("Texture added after Save As");
    auto& sprite = entity.AddComponent<SpriteRendererComponent>();
    sprite.texturePath = "Assets/shared.png";
    SpriteImageCache cache;
    const auto before = cache.Get(document.GetScene(), sprite).pixelColor(0, 0);
    Check(before == QColor(Qt::green), "textures added after save as use the destination root");
    Check(document.Save() && document.Open(PathUtf8(second.scene)),
          "moved textured document saves and reopens");
    const auto& reopened = document.GetScene().GetEntities().front()
                               .GetComponent<SpriteRendererComponent>();
    Check(cache.Get(document.GetScene(), reopened).pixelColor(0, 0) == before,
          "texture resolution agrees before and after reopening the destination");

    QSettings().clear();
    QSettings().setValue(QStringLiteral("lastProject"),
                         QString::fromUtf8(PathUtf8(first.root / EditorProject::FileName)));
    QSettings().setValue(QStringLiteral("lastScene"), QString::fromUtf8(PathUtf8(first.scene)));
    EditorWindow window;
    auto* projectSettings = window.findChild<QAction*>(QStringLiteral("projectSettings"));
    auto* undo = window.findChild<QUndoStack*>();
    Check(projectSettings && projectSettings->isEnabled() && undo,
          "root transition starts with an open project");
    window.findChild<QAction*>(QStringLiteral("addEntity"))->trigger();
    window.ExecuteRecordedEdit(QStringLiteral("Texture before deletion"), [&] {
        window.GetDocument().GetSelected().GetComponent<SpriteRendererComponent>().texturePath =
            "Assets/shared.png";
    });
    window.findChild<QAction*>(QStringLiteral("deleteEntity"))->trigger();
    Check(undo->canUndo() && window.GetDocument().GetScene().GetEntityCount() == 0,
          "old project history contains the deleted texture reference");
    const auto destination = second.root / "Scenes/Imported.lux";
    bool accepted = false;
    QTimer accept;
    QObject::connect(&accept, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(QString::fromUtf8(PathUtf8(destination)));
            accepted = QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    accept.start(10);
    timeout.start(5000);
    window.findChild<QAction*>(QStringLiteral("saveSceneAs"))->trigger();
    accept.stop();
    timeout.stop();
    const auto& savedPath = window.GetDocument().GetPath();
    std::cout << "cross-root save as accepted=" << accepted << " actual=" << savedPath
              << " expected=" << PathUtf8(destination) << '\n';
    std::error_code pathError;
    Check(accepted && fs::equivalent(PathFromUtf8(savedPath), destination, pathError),
          "save as selects a scene in the other project");
    Check(!projectSettings->isEnabled() && undo->count() == 0 &&
              fs::equivalent(PathFromUtf8(window.GetDocument().GetScene().GetAssetRoot()),
                              second.root),
          "cross-root save as releases the old project and incompatible undo history");
    QSettings().clear();
}

QImage RenderWidget(QWidget& viewport) {
    QImage image(viewport.size(), QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    viewport.render(&image);
    return image;
}

void TestSoftware() {
    Fixture files;
    EditorDocument document;
    Check(document.Open(PathUtf8(files.scene)), "open software fixture");
    SceneViewport2D viewport(nullptr, document, [](auto) {}, [](auto, const auto&) {},
                             [](auto, auto, auto, auto) {}, [] {}, [] {});
    viewport.resize(400, 300);
    const QImage background = RenderWidget(viewport);
    auto entity = document.CreateEntity("Texture");
    entity.GetComponent<TransformComponent>().scale = {2, 2, 1};
    auto& sprite = entity.AddComponent<SpriteRendererComponent>();
    sprite.texturePath = "Assets/한글.png";
    document.Select(std::nullopt);
    QImage image = RenderWidget(viewport);
    Check(image.pixelColor(164, 114).red() > 240 &&
              image.pixelColor(164, 114).green() < 10,
          "software viewport draws the texture instead of the sprite color");
    Check(image.pixelColor(236, 186) == background.pixelColor(236, 186),
          "software viewport preserves image alpha");
    entity.GetComponent<TransformComponent>().rotation.z = std::numbers::pi_v<float> * .5f;
    image = RenderWidget(viewport);
    Check(image.pixelColor(164, 186).red() > 240 &&
              image.pixelColor(164, 186).green() < 10,
          "software viewport rotates texture pixels with the sprite");
    sprite.color = {.5f, .75f, 1, .5f};
    SpriteImageCache cache;
    const QImage tinted = cache.Get(document.GetScene(), sprite);
    Check(tinted.pixelColor(0, 0) == QColor(128, 0, 0, 128),
          "software texture tint multiplies RGB and alpha");
    Check(cache.Get(document.GetScene(), sprite).pixelColor(3, 3).alpha() == 0,
          "cached tint preserves transparent pixels");
    sprite.texturePath = "Assets/missing.png";
    Check(cache.Get(document.GetScene(), sprite).isNull(), "missing image falls back safely");
    image = RenderWidget(viewport);
    Check(!image.isNull(), "software viewport handles a missing texture");
    sprite.texturePath = "../outside.png";
    Check(cache.Get(document.GetScene(), sprite).isNull(), "software cache rejects root escapes");
}

void TestClipboard() {
    Fixture files;
    EditorDocument saved;
    files.Populate(saved);
    Check(saved.Save(), "clipboard texture scene saves");
    QSettings().clear();
    QSettings().setValue(QStringLiteral("lastScene"), QString::fromUtf8(PathUtf8(files.scene)));
    EditorWindow window;
    auto* hierarchy = window.findChild<QListWidget*>(QStringLiteral("sceneOutliner"));
    Check(hierarchy && hierarchy->count() == 1, "clipboard fixture hierarchy");
    hierarchy->setCurrentRow(0);
    auto* copy = window.findChild<QAction*>(QStringLiteral("copyEntity"));
    auto* paste = window.findChild<QAction*>(QStringLiteral("pasteEntity"));
    auto* undo = window.findChild<QUndoStack*>();
    Check(copy && paste && undo, "clipboard texture controls");
    copy->trigger();
    auto object = QJsonDocument::fromJson(QApplication::clipboard()->mimeData()->data(
                                            QStringLiteral("application/x-lux-entity"))).object();
    Check(object.value(QStringLiteral("sprite")).isArray() &&
              object.value(QStringLiteral("spriteTexturePath")).toString() ==
                  QStringLiteral("Assets/한글.png"),
          "clipboard preserves the legacy color field and texture path");
    paste->trigger();
    Check(window.GetDocument().GetScene().GetEntityCount() == 2 &&
              window.GetDocument().GetSelected().GetComponent<SpriteRendererComponent>().texturePath ==
                  "Assets/한글.png", "paste preserves the texture reference");
    undo->undo();
    undo->redo();
    Check(window.GetDocument().GetSelected().GetComponent<SpriteRendererComponent>().texturePath ==
              "Assets/한글.png", "clipboard undo and redo preserve the texture reference");
    const int count = undo->count();
    object.insert(QStringLiteral("spriteTexturePath"), QStringLiteral("../outside.png"));
    auto* mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-lux-entity"), QJsonDocument(object).toJson());
    QApplication::clipboard()->setMimeData(mime);
    paste->trigger();
    Check(window.GetDocument().GetScene().GetEntityCount() == 2 && undo->count() == count,
          "invalid clipboard texture paths do not mutate the scene or history");
    QSettings().clear();
}

void TestSoftwareReadLockRecovery() {
#ifdef _WIN32
    Fixture files;
    Scene scene;
    scene.SetAssetRoot(PathUtf8(files.root));
    SpriteRendererComponent sprite;
    sprite.texturePath = "Assets/solid.png";
    const auto imagePath = files.root / "Assets/solid.png";
    const auto modified = fs::last_write_time(imagePath);
    const auto size = fs::file_size(imagePath);
    HANDLE locked = CreateFileW(imagePath.c_str(), GENERIC_WRITE,
                                 FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "software texture read-lock fixture opens");
    SpriteImageCache cache;
    const bool blocked = cache.Get(scene, sprite).isNull();
    CloseHandle(locked);
    Check(blocked && fs::last_write_time(imagePath) == modified &&
              fs::file_size(imagePath) == size,
          "temporary read lock causes decode failure without changing texture metadata");
    SpriteImageCache fresh;
    Check(!fresh.Get(scene, sprite).isNull(), "unlocked image can be read without rewriting it");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    Check(!cache.Get(scene, sprite).isNull(),
          "software cache retries a transient failure after the bounded cooldown");
#endif
}

int TestGpu() {
    qunsetenv("LUX_EDITOR_SOFTWARE_VIEWPORT");
    EngineSceneRenderer::SetForceAllowed(true);
    Fixture files;
    EditorDocument document;
    files.Populate(document);
    auto& sprite = document.GetSelected().GetComponent<SpriteRendererComponent>();
    sprite.texturePath = "Assets/solid.png";
    sprite.color = {1, .5f, 1, .5f};
    EngineSceneRenderer renderer;
    if (!renderer.EnsureReady()) {
        std::cout << "SKIP: " << renderer.LastError().toStdString() << '\n';
        return 77;
    }
    QImage image;
    Check(renderer.Render(document.GetScene(), 128, 128, QRectF(QPointF(-2, 2), QPointF(2, -2)),
                            image), "GPU editor renders a texture");
    const auto pixel = image.pixelColor(64, 64);
    Check(pixel.green() > pixel.red() + 20 && pixel.green() > pixel.blue() + 5,
          "GPU editor samples the image and applies tint and alpha");
    document.GetSelected().GetComponent<TransformComponent>().position.z = 6;
    Check(renderer.Render3D(document.GetScene(), 128, 128, {0, 0, 6}, .8, .2, 3, image),
          "3D editor renders a textured sprite at its nonzero Z position");
    const auto pixel3d = image.pixelColor(64, 64);
    Check(pixel3d.green() > pixel3d.red() + 20 && pixel3d.green() > pixel3d.blue() + 5,
          "3D editor samples the sprite texture");
    Check(std::abs(pixel3d.red() - 24) <= 3 && std::abs(pixel3d.green() - 67) <= 3 &&
              std::abs(pixel3d.blue() - 51) <= 3,
          "3D sprite alpha blends its tinted image over the viewport background");
    QImage translucent(2, 2, QImage::Format_RGBA8888);
    translucent.fill(QColor(0, 200, 0, 128));
    Check(translucent.save(QString::fromUtf8(PathUtf8(files.root / "Assets/translucent.png"))),
          "translucent 3D fixture saves");
    translucent.fill(QColor(255, 0, 0));
    Check(translucent.save(QString::fromUtf8(PathUtf8(files.root / "Assets/red.png"))),
          "opaque 3D fixture saves");
    document.NewScene();
    auto front = document.CreateEntity("Front created first");
    front.GetComponent<TransformComponent>().position.z = 1;
    front.GetComponent<TransformComponent>().scale = {3, 3, 1};
    front.AddComponent<SpriteRendererComponent>().texturePath = "Assets/translucent.png";
    auto back = document.CreateEntity("Back created last");
    back.GetComponent<TransformComponent>().scale = {3, 3, 1};
    back.AddComponent<SpriteRendererComponent>().texturePath = "Assets/red.png";
    Check(renderer.Render3D(document.GetScene(), 128, 128, {}, 0, 0, 5, image),
          "overlapping translucent 3D sprites render");
    const auto layered = image.pixelColor(64, 64);
    Check(std::abs(layered.red() - 127) <= 3 && std::abs(layered.green() - 100) <= 3 &&
              layered.blue() < 4,
          "3D translucent sprites compose back to front independently of creation order");
    return 0;
}
}

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LuxEngineTests"));
    QCoreApplication::setApplicationName(QStringLiteral("LuxEditorTextureTests"));
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    try {
        if (argc > 1 && std::string_view(argv[1]) == "gpu")
            return TestGpu();
        qputenv("LUX_EDITOR_SOFTWARE_VIEWPORT", "1");
        TestDocument();
        TestSaveAsRootTransition();
        TestSoftware();
        TestSoftwareReadLockRecovery();
        TestClipboard();
        std::cout << "Editor texture tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Editor texture test failed: " << error.what() << '\n';
        return 1;
    }
}
