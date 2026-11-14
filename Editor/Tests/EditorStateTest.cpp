#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectArchive.hpp>
#include <LuxEngine/Scene/Scene.hpp>
#include <LuxUI/Widgets.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
namespace fs = std::filesystem;
using namespace LuxEditor;

void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

LuxUI::String Q(const fs::path& path) {
    return LuxUI::String::fromUtf8(PathUtf8(path));
}

struct ProjectFixture {
    LuxUI::TemporaryDir directory;
    EditorProject project;
    fs::path secondary;
    fs::path external;

    ProjectFixture() {
        Check(directory.isValid(), "temporary project directory exists");
        const auto root = PathFromUtf8(directory.path().toStdString());
        std::string error;
        auto created =
            EditorProject::Create(root / "Project", "Audit", ProjectTemplate::Empty, error);
        Check(created.has_value(), error.c_str());
        project = std::move(*created);
        secondary = project.GetSceneFile().parent_path() / "Secondary.lux";
        external = root / "Outside.lux";
        LuxEngine::Scene scene;
        scene.CreateEntity("Secondary");
        Check(scene.Serialize(PathUtf8(secondary)) && scene.Serialize(PathUtf8(external)),
              "secondary and external scene fixtures save");
    }

    std::unique_ptr<EditorWindow> Open(LuxUI::Application& app) {
        LuxUI::Settings().clear();
        LuxUI::Settings().setValue("lastProject", Q(project.GetFile()));
        auto window = std::make_unique<EditorWindow>();
        window->show();
        app.processEvents();
        return window;
    }
};

void OpenViaMenu(EditorWindow& window, const fs::path& scene) {
    LuxUI::Timer chooser;
    bool configured = false, accepted = false, timedOut = false, inputAvailable = false;
    const auto start = std::chrono::steady_clock::now();
    LuxUI::Object::connect(&chooser, &LuxUI::Timer::timeout, &window, [&] {
        auto* dialog = qobject_cast<LuxUI::FileDialog*>(LuxUI::Application::activeModalWidget());
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
            timedOut = true;
            if (auto* modal = qobject_cast<LuxUI::Dialog*>(LuxUI::Application::activeModalWidget()))
                modal->reject();
            return;
        }
        if (!dialog)
            return;
        if (!configured) {
            configured = true;
            dialog->setDirectory(Q(scene.parent_path()));
            auto* filename = dialog->findChild<LuxUI::LineEdit*>(LUX_UI_TEXT("fileNameEdit"));
            inputAvailable = filename != nullptr;
            if (!filename) {
                dialog->reject();
                return;
            }
            LuxUI::Object::connect(dialog, &LuxUI::FileDialog::accepted, &window,
                                   [&] { accepted = true; });
            filename->setFocus();
            filename->setText(Q(scene));
            return;
        }
        const auto selected = dialog->selectedFiles();
        if (selected.size() == 1 && LuxUI::FileInfo(selected.front()).canonicalFilePath() ==
                                        LuxUI::FileInfo(Q(scene)).canonicalFilePath())
            LuxUI::MetaObject::invokeMethod(dialog, "accept", LuxUI::Flags::DirectConnection);
    });
    chooser.start(5);
    auto* action = window.findChild<LuxUI::Action*>("openScene");
    Check(action, "scene open action exists");
    action->trigger();
    Check(!timedOut, "scene chooser did not accept the existing fixture file within ten seconds");
    Check(inputAvailable, "non-native scene chooser exposes its filename input");
    Check(configured && accepted, "scene chooser accepted the exact requested file");
}

void DropScene(EditorWindow& window, const fs::path& path) {
    LuxUI::MimeData mime;
    mime.setUrls({LuxUI::Url::fromLocalFile(Q(path))});
    LuxUI::DragEnterEvent enter({50, 50}, LuxUI::Flags::CopyAction, &mime, LuxUI::Flags::LeftButton,
                                LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(&window, &enter);
    Check(enter.isAccepted(), "scene drag enters the editor");
    LuxUI::DropEvent drop({50, 50}, LuxUI::Flags::CopyAction, &mime, LuxUI::Flags::LeftButton,
                          LuxUI::Flags::NoModifier);
    LuxUI::Application::sendEvent(&window, &drop);
}

void TestSceneContext(LuxUI::Application& app) {
    for (const int route : {0, 1, 2}) {
        ProjectFixture fixture;
        auto window = fixture.Open(app);
        auto* settings = window->findChild<LuxUI::Action*>("projectSettings");
        Check(settings && settings->isEnabled(), "project starts connected");
        if (route == 0)
            OpenViaMenu(*window, fixture.secondary);
        else if (route == 1) {
            auto* assets = window->findChild<LuxUI::ListWidget*>("assetGrid");
            Check(assets, "asset browser exists");
            const auto items =
                assets->findItems("Scenes/Secondary.lux", LuxUI::Flags::MatchExactly);
            Check(items.size() == 1, "secondary scene appears in asset browser");
            assets->setCurrentItem(items.front());
            assets->itemDoubleClicked(items.front());
        } else
            DropScene(*window, fixture.secondary);
        Check(PathFromUtf8(window->GetDocument().GetPath()) == fixture.secondary &&
                  settings->isEnabled(),
              "all scene open routes retain the owning project");
        // Failed opens keep the current document and project connection.
        LuxUI::Timer dismiss;
        LuxUI::Object::connect(&dismiss, &LuxUI::Timer::timeout, &app, [] {
            if (auto* message =
                    qobject_cast<LuxUI::MessageBox*>(LuxUI::Application::activeModalWidget()))
                message->accept();
        });
        dismiss.start(5);
        DropScene(*window, fixture.secondary.parent_path() / "Missing.lux");
        Check(settings->isEnabled() &&
                  PathFromUtf8(window->GetDocument().GetPath()) == fixture.secondary,
              "failed scene open leaves the current document and project intact");
        if (route == 2)
            DropScene(*window, fixture.external);
        else
            OpenViaMenu(*window, fixture.external);
        Check(PathFromUtf8(window->GetDocument().GetPath()) == fixture.external &&
                  !settings->isEnabled(),
              "external scenes detach from the previous project consistently");
    }
}

void TestRecoveryHistory(LuxUI::Application& app) {
    ProjectFixture fixture;
    LuxEngine::Scene recovered;
    recovered.CreateEntity("Recovered unsaved entity");
    std::string error;
    Check(fixture.project.SaveAutosave(fixture.project.GetSceneFile(), recovered, error),
          error.c_str());
    fs::last_write_time(fixture.project.GetSceneFile(),
                        fs::file_time_type::clock::now() - std::chrono::minutes(5));
    LuxUI::Timer recover;
    LuxUI::Object::connect(&recover, &LuxUI::Timer::timeout, &app, [] {
        if (auto* message =
                qobject_cast<LuxUI::MessageBox*>(LuxUI::Application::activeModalWidget()))
            if (auto* yes = message->button(LuxUI::MessageBox::Yes))
                yes->click();
    });
    recover.start(5);
    auto window = fixture.Open(app);
    recover.stop();
    auto& document = window->GetDocument();
    Check(document.IsDirty() && document.GetScene().GetEntityCount() == 1,
          "autosave recovery is unsaved");
    auto* undo = window->findChild<LuxUI::UndoStack*>();
    window->findChild<LuxUI::Action*>("addEntity")->trigger();
    undo->undo();
    Check(document.IsDirty() && document.GetScene().GetEntityCount() == 1 && !undo->isClean(),
          "undo to a recovered baseline remains dirty");
    undo->redo();
    Check(document.IsDirty() && document.GetScene().GetEntityCount() == 2, "redo remains dirty");
    undo->undo();
    // Closing must still ask to save the recovered changes.
    int prompts = 0;
    LuxUI::Timer cancel;
    LuxUI::Object::connect(&cancel, &LuxUI::Timer::timeout, &app, [&] {
        if (auto* message =
                qobject_cast<LuxUI::MessageBox*>(LuxUI::Application::activeModalWidget())) {
            ++prompts;
            message->button(LuxUI::MessageBox::Cancel)->click();
        }
    });
    cancel.start(5);
    Check(!window->close() && prompts == 1, "close asks to save recovered changes after undo");
    cancel.stop();
    window->findChild<LuxUI::Action*>("saveScene")->trigger();
    LuxEngine::Scene disk;
    Check(!document.IsDirty() && undo->isClean() &&
              disk.Deserialize(PathUtf8(fixture.project.GetSceneFile())) &&
              disk.GetEntityCount() == 1,
          "saving recovered content establishes the clean baseline");
    undo->redo();
    Check(document.IsDirty(), "redo away from the saved baseline is dirty");
    undo->undo();
    Check(!document.IsDirty(), "undo back to the saved baseline is clean");
}

void TestBrokenSceneRecovery(LuxUI::Application& app) {
    for (const bool missing : {false, true})
        for (const bool accept : {false, true}) {
            ProjectFixture fixture;
            const auto sceneFile = fixture.project.GetSceneFile();
            LuxEngine::Scene recovery;
            recovery.CreateEntity("Recovered from broken original");
            std::string error;
            Check(fixture.project.SaveAutosave(sceneFile, recovery, error), error.c_str());
            if (missing)
                fs::remove(sceneFile);
            else
                std::ofstream(sceneFile, std::ios::trunc) << "corrupt scene";
            Check(!EditorProject::Open(fixture.project.GetFile(), error),
                  "strict project validation rejects broken originals");
            Check(EditorProject::OpenForRecovery(fixture.project.GetFile(), error).has_value(),
                  "editor recovery accepts metadata with a valid autosave");
            const auto archive =
                sceneFile.parent_path().parent_path().parent_path() / "Broken.luxpack";
            Check(ExportProject(fixture.project, archive, error),
                  "broken project archive fixture exports");
            const auto imported = archive.parent_path() / "Imported";
            Check(!ImportProject(archive, imported, error) && !fs::exists(imported),
                  "archive import still rejects a broken start scene despite editor recovery "
                  "support");
            RecentProjects recent(archive.parent_path() / "recent.txt");
            Check(recent.Add(fixture.project.GetFile()),
                  "recoverable projects stay accessible from recents");
            recent.Load();
            Check(recent.Get().size() == 1, "recent project reload retains recoverable metadata");
            LuxUI::Timer answer;
            int prompts = 0;
            LuxUI::Object::connect(&answer, &LuxUI::Timer::timeout, &app, [&] {
                if (auto* message =
                        qobject_cast<LuxUI::MessageBox*>(LuxUI::Application::activeModalWidget())) {
                    ++prompts;
                    message->button(accept ? LuxUI::MessageBox::Yes : LuxUI::MessageBox::Cancel)
                        ->click();
                }
            });
            answer.start(5);
            auto window = fixture.Open(app);
            answer.stop();
            Check(prompts == 1, "broken-original recovery prompts once");
            auto* settings = window->findChild<LuxUI::Action*>("projectSettings");
            if (accept) {
                Check(settings->isEnabled() && window->GetDocument().IsDirty() &&
                          window->GetDocument().GetScene().GetEntityCount() == 1,
                      "accepting recovery opens an unsaved recovered project scene");
                Check(!EditorProject::Open(fixture.project.GetFile(), error),
                      "recovery does not overwrite the original before saving");
                window->findChild<LuxUI::Action*>("saveScene")->trigger();
                Check(EditorProject::Open(fixture.project.GetFile(), error).has_value() &&
                          !window->GetDocument().IsDirty(),
                      "explicit save repairs the original scene");
            } else {
                Check(!settings->isEnabled() && window->GetDocument().GetPath().empty(),
                      "cancelling recovery leaves the document and project connection unchanged");
                Check(fs::exists(fixture.project.GetAutosaveFile(sceneFile)),
                      "cancelling keeps the recoverable autosave");
            }
        }
    ProjectFixture fixture;
    std::string error;
    std::ofstream(fixture.project.GetSceneFile(), std::ios::trunc) << "corrupt scene";
    const auto autosave = fixture.project.GetAutosaveFile(fixture.project.GetSceneFile());
    fs::create_directories(autosave.parent_path());
    std::ofstream(autosave, std::ios::trunc) << "corrupt autosave";
    Check(!EditorProject::OpenForRecovery(fixture.project.GetFile(), error),
          "two broken scene copies are rejected");

    ProjectFixture original;
    auto window = original.Open(app);
    window->ExecuteRecordedEdit("Existing unsaved work",
                                [&] { window->GetDocument().CreateEntity("Keep this edit"); });
    std::string before;
    Check(window->GetDocument().GetScene().SerializeToString(before),
          "existing scene snapshot saves");
    auto* history = window->findChild<LuxUI::UndoStack*>();
    const int historyIndex = history->index();
    LuxEngine::Scene candidate;
    candidate.CreateEntity("Recovery candidate");
    Check(original.project.SaveAutosave(original.secondary, candidate, error), error.c_str());
    std::ofstream(original.secondary, std::ios::trunc) << "broken secondary";
    LuxUI::Timer cancel;
    LuxUI::Object::connect(&cancel, &LuxUI::Timer::timeout, &app, [] {
        if (auto* message =
                qobject_cast<LuxUI::MessageBox*>(LuxUI::Application::activeModalWidget())) {
            if (auto* no = message->button(LuxUI::MessageBox::No))
                no->click();
            else
                message->button(LuxUI::MessageBox::Cancel)->click();
        }
    });
    cancel.start(5);
    DropScene(*window, original.secondary);
    cancel.stop();
    std::string after;
    Check(window->GetDocument().GetScene().SerializeToString(after) && before == after &&
              history->index() == historyIndex && window->GetDocument().IsDirty() &&
              PathFromUtf8(window->GetDocument().GetPath()) == original.project.GetSceneFile() &&
              window->findChild<LuxUI::Action*>("projectSettings")->isEnabled(),
          "cancelling recovery preserves the prior document, unsaved edits, history and project");
}

void TestSessionScene(LuxUI::Application& app) {
    ProjectFixture fixture;
    {
        auto window = fixture.Open(app);
        OpenViaMenu(*window, fixture.secondary);
        Check(window->close(), "clean secondary scene window closes");
        // QFileDialog uses Qt separators, while filesystem paths may use native Windows ones.
        // Verify the persisted scene identity rather than its platform-specific spelling.
        const auto stored =
            PathFromUtf8(LuxUI::Settings().value("lastScene").toString().toUtf8().toStdString());
        std::error_code error;
        Check(!stored.empty() && fs::equivalent(stored, fixture.secondary, error) && !error,
              "closing stores the current project scene");
    }
    {
        EditorWindow restored;
        Check(PathFromUtf8(restored.GetDocument().GetPath()) == fixture.secondary &&
                  restored.GetDocument().GetScene().GetEntityCount() == 1 &&
                  restored.findChild<LuxUI::Action*>("projectSettings")->isEnabled(),
              "startup restores both the project and its last edited scene");
    }
    const auto missing = fixture.secondary.parent_path() / "Missing.lux";
    const auto corrupt = fixture.secondary.parent_path() / "Corrupt.lux";
    std::ofstream(corrupt) << "invalid scene";
    for (const auto& candidate : {missing, corrupt, fixture.external}) {
        LuxUI::Settings().setValue("lastProject", Q(fixture.project.GetFile()));
        LuxUI::Settings().setValue("lastScene", Q(candidate));
        EditorWindow restored;
        Check(PathFromUtf8(restored.GetDocument().GetPath()) == fixture.project.GetSceneFile(),
              "missing, damaged and external last scenes fall back to the start scene");
    }
    // A deleted last scene can still have a valid recovery candidate.
    const auto deleted = fixture.secondary.parent_path() / "Deleted.lux";
    LuxEngine::Scene recovered;
    recovered.CreateEntity("Recovered secondary");
    std::string error;
    Check(fixture.project.SaveAutosave(deleted, recovered, error), error.c_str());
    LuxUI::Settings().setValue("lastProject", Q(fixture.project.GetFile()));
    LuxUI::Settings().setValue("lastScene", Q(deleted));
    LuxUI::Timer answer;
    LuxUI::Object::connect(&answer, &LuxUI::Timer::timeout, &app, [] {
        if (auto* message =
                qobject_cast<LuxUI::MessageBox*>(LuxUI::Application::activeModalWidget()))
            message->button(LuxUI::MessageBox::Yes)->click();
    });
    answer.start(5);
    EditorWindow restored;
    answer.stop();
    Check(PathFromUtf8(restored.GetDocument().GetPath()) == deleted &&
              restored.GetDocument().IsDirty() &&
              restored.GetDocument().GetScene().GetEntityCount() == 1,
          "last edited scene recovery preserves project context and the unsaved baseline");
}

void TestReopenCurrentProject(LuxUI::Application& app) {
    ProjectFixture fixture;
    LuxEngine::Scene initial;
    initial.CreateEntity("Initially opened start");
    Check(initial.Serialize(PathUtf8(fixture.project.GetSceneFile())),
          "initial project start scene saves");
    auto window = fixture.Open(app);
    auto& document = window->GetDocument();
    const auto startTab = document.GetActiveTabId();
    Check(window->OpenSceneDocument(fixture.secondary), "secondary project scene opens");
    window->ExecuteRecordedEdit("Inactive unsaved edit",
                                [&] { document.CreateEntity("Discarded secondary edit"); });
    const auto secondaryTab = document.GetActiveTabId();
    Check(window->SwitchSceneDocument(startTab, false) && document.IsDirty(secondaryTab),
          "project reopen starts with an open start scene and inactive dirty scene");
    LuxEngine::Scene updated;
    updated.CreateEntity("Latest start from disk");
    Check(updated.Serialize(PathUtf8(fixture.project.GetSceneFile())),
          "another collaborator updates the project start scene on disk");
    int discardPrompts = 0, errors = 0;
    LuxUI::Timer answer;
    LuxUI::Object::connect(&answer, &LuxUI::Timer::timeout, &app, [&] {
        if (auto* dialog =
                qobject_cast<LuxUI::FileDialog*>(LuxUI::Application::activeModalWidget())) {
            dialog->selectFile(Q(fixture.project.GetFile()));
            LuxUI::MetaObject::invokeMethod(dialog, "accept", LuxUI::Flags::DirectConnection);
        } else if (auto* message =
                       qobject_cast<LuxUI::MessageBox*>(LuxUI::Application::activeModalWidget())) {
            if (auto* no = message->button(LuxUI::MessageBox::No)) {
                ++discardPrompts;
                no->click();
            } else {
                ++errors;
                message->accept();
            }
        }
    });
    answer.start(5);
    auto* action = window->findChild<LuxUI::Action*>("openProject");
    Check(action && action->isEnabled(), "project open action is available");
    action->trigger();
    answer.stop();
    const auto entities = document.GetScene().GetEntities();
    Check(discardPrompts == 1 && errors == 0 && document.ListTabs().size() == 1 &&
              PathFromUtf8(document.GetPath()) == fixture.project.GetSceneFile() &&
              !document.IsDirty() && entities.size() == 1 &&
              entities.front().GetComponent<LuxEngine::TagComponent>().tag ==
                  "Latest start from disk",
          "reopening the current project replaces its old open start scene without a tab conflict");
    auto* history = window->findChild<LuxUI::UndoStack*>();
    Check(history && history->count() == 0 &&
              window->findChild<LuxUI::Action*>("projectSettings")->isEnabled(),
          "successful project reopen clears old scene history and retains project ownership");
}

void TestRestoreOpenedProject(LuxUI::Application& app) {
    ProjectFixture fixture;
    LuxEngine::Scene original;
    original.CreateEntity("Start from backup");
    Check(original.Serialize(PathUtf8(fixture.project.GetSceneFile())),
          "project backup starts with authored content");
    std::string error;
    fs::path backup;
    Check(CreateProjectBackup(fixture.project, backup, error), error.c_str());
    auto window = fixture.Open(app);
    auto& document = window->GetDocument();
    const auto startTab = document.GetActiveTabId();
    window->ExecuteRecordedEdit("Work after backup",
                                [&] { document.CreateEntity("Start edit after backup"); });
    window->findChild<LuxUI::Action*>("saveScene")->trigger();
    Check(!document.IsDirty() && document.GetScene().GetEntityCount() == 2,
          "post-backup start content is saved and still open");
    Check(window->OpenSceneDocument(fixture.secondary), "secondary project scene opens");
    window->ExecuteRecordedEdit("Inactive work after backup",
                                [&] { document.CreateEntity("Unsaved secondary edit"); });
    const auto secondaryTab = document.GetActiveTabId();
    Check(window->SwitchSceneDocument(startTab, false) && document.IsDirty(secondaryTab),
          "backup restore starts with the old start scene open and inactive unsaved work");
    int discardPrompts = 0, restorePrompts = 0, errors = 0;
    LuxUI::Timer answer;
    LuxUI::Object::connect(&answer, &LuxUI::Timer::timeout, &app, [&] {
        if (auto* dialog =
                qobject_cast<LuxUI::FileDialog*>(LuxUI::Application::activeModalWidget())) {
            dialog->selectFile(Q(backup));
            LuxUI::MetaObject::invokeMethod(dialog, "accept", LuxUI::Flags::DirectConnection);
        } else if (auto* message =
                       qobject_cast<LuxUI::MessageBox*>(LuxUI::Application::activeModalWidget())) {
            if (message->text().contains(LUX_UI_TEXT("저장하지 않은"))) {
                ++discardPrompts;
                message->button(LuxUI::MessageBox::No)->click();
            } else if (auto* yes = message->button(LuxUI::MessageBox::Yes)) {
                ++restorePrompts;
                yes->click();
            } else {
                ++errors;
                message->accept();
            }
        }
    });
    answer.start(5);
    auto* action = window->findChild<LuxUI::Action*>("restoreProject");
    Check(action && action->isEnabled(), "project restore action is available");
    action->trigger();
    answer.stop();
    const auto entities = document.GetScene().GetEntities();
    LuxEngine::Scene disk, secondary;
    Check(discardPrompts == 1 && restorePrompts == 1 && errors == 0 &&
              document.ListTabs().size() == 1 && !document.IsDirty() && entities.size() == 1 &&
              entities.front().GetComponent<LuxEngine::TagComponent>().tag == "Start from backup" &&
              PathFromUtf8(document.GetPath()) == fixture.project.GetSceneFile() &&
              disk.Deserialize(PathUtf8(fixture.project.GetSceneFile())) &&
              disk.GetEntityCount() == 1 && secondary.Deserialize(PathUtf8(fixture.secondary)) &&
              secondary.GetEntityCount() == 1,
          "restoring a backup updates both the live start scene and disk after confirmed discard");
    auto* history = window->findChild<LuxUI::UndoStack*>();
    Check(history && history->count() == 0 &&
              window->findChild<LuxUI::Action*>("projectSettings")->isEnabled(),
          "backup restore clears old tabs and history while retaining project ownership");
    window->findChild<LuxUI::Action*>("saveScene")->trigger();
    Check(disk.Deserialize(PathUtf8(fixture.project.GetSceneFile())) && disk.GetEntityCount() == 1,
          "saving after restore cannot overwrite the backup with stale pre-restore content");
}
} // namespace

int main(int argc, char** argv) {
    LuxUI::Application::setAttribute(LuxUI::Flags::AA_DontUseNativeDialogs);
    LuxUI::Application app(argc, argv);
    LuxUI::CoreApplication::setOrganizationName("LuxEngineTests");
    LuxUI::CoreApplication::setApplicationName("LuxEditorStateTests");
    LuxUI::TemporaryDir settings;
    if (!settings.isValid())
        return 1;
    LuxUI::Settings::setDefaultFormat(LuxUI::Settings::IniFormat);
    LuxUI::Settings::setPath(LuxUI::Settings::IniFormat, LuxUI::Settings::UserScope,
                             settings.path());
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode == "scene-context")
            TestSceneContext(app);
        else if (mode == "recovery-history")
            TestRecoveryHistory(app);
        else if (mode == "broken-scene-recovery")
            TestBrokenSceneRecovery(app);
        else if (mode == "session-scene")
            TestSessionScene(app);
        else if (mode == "project-reopen")
            TestReopenCurrentProject(app);
        else if (mode == "project-restore")
            TestRestoreOpenedProject(app);
        else
            throw std::runtime_error("Unknown editor state test mode");
        std::cout << "Editor state checks passed: " << mode << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Editor state checks failed: " << error.what() << '\n';
        return 1;
    }
}
