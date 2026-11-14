#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Debug/DebugPanel.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/GameViewport.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Core/Log.hpp>
#include <LuxEngine/Platform/Input.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <cctype>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace LuxEditor {

bool EditorWindow::RegisterTool(LuxUI::Command command) {
    const auto id = command.id;
    const auto title = command.title;
    if (!commands_.Register(std::move(command)))
        return false;
    LuxUI::Action* action = nullptr;
    try {
        if (customToolsMenu_) {
            action = AddCommand(customToolsMenu_, id, title, [this, id] {
                if (!commands_.Execute(id))
                    Log(LUX_UI_TEXT("현재 상태에서는 이 도구를 실행할 수 없습니다."));
            });
        }
    } catch (...) {
        commands_.Remove(id);
        delete action;
        throw;
    }
    return true;
}

void EditorWindow::BuildDevelopmentWorkspace() {
    customToolsMenu_ = menuBar()->addMenu(LUX_UI_TEXT("도구"));
    customToolsMenu_->setObjectName(LUX_UI_TEXT("customToolsMenu"));
    connect(this, &LuxUI::MainWindow::CommandFailed, this,
            [this](const LuxUI::String& message) { Log(message, true); });
    developmentConsole_ = std::make_unique<LuxUI::ConsolePanel>(this);
    consoleOutput_->setParent(this);
    consoleOutput_->hide();
    docks_[3]->setWidget(developmentConsole_->View());
    profiler_ = std::make_unique<LuxUI::ProfilerPanel>(this);
    profilerDock_ = AddPanel("performanceProfilerDock", "성능 프로파일러", profiler_->View(),
                             LuxUI::Flags::BottomDockWidgetArea);
    tabifyDockWidget(docks_[7], profilerDock_);
    profilerDock_->hide();
    RegisterTool({"play", "현재 씬 플레이 (F5)", [this] { StartPreview(); },
                  [this] { return !previewActive_; }});
    RegisterTool({"pause", "플레이 일시정지/계속", [this] { TogglePreviewPause(); },
                  [this] { return previewActive_; }});
    RegisterTool({"step", "한 프레임 실행", [this] { StepPreview(); }, {}});
    RegisterTool(
        {"stop", "플레이 종료", [this] { StopPreview(); }, [this] { return previewActive_; }});
    RegisterTool({"frame", "선택 오브젝트에 초점", [this] { canvas3D_->FocusSelection(); }, {}});
    RegisterTool({"profiler",
                  "프로파일러 표시",
                  [this] {
                      profilerDock_->show();
                      profilerDock_->raise();
                  },
                  {}});
    RegisterTool({"scripts", "플레이용 C# 어셈블리 선택…", [this] { SelectPlayAssembly(); },
                  [this] { return !previewActive_; }});
    RegisterTool({"reload-tools", "프로젝트 도구 불러오기…", [this] { LoadProjectTools(); },
                  [this] { return !previewActive_; }});
    developmentConsole_->SetCommandHandler([this](std::string command) {
        if (command == "help" || command == "tools") {
            for (const auto& [id, title] : commands_.Commands())
                developmentConsole_->Append(LuxUI::Severity::Info, "도구", id + " — " + title);
            return;
        }
        try {
            if (!commands_.Execute(command))
                developmentConsole_->Append(LuxUI::Severity::Warning, "콘솔",
                                            "사용할 수 없는 명령입니다: " + command);
        } catch (const std::exception& error) {
            Log(LuxUI::String::fromUtf8(error.what()), true);
        }
    });
    pauseOnErrorAction_ = customToolsMenu_->addAction(LUX_UI_TEXT("실행 오류 발생 시 일시정지"));
    pauseOnErrorAction_->setObjectName(LUX_UI_TEXT("pauseOnRuntimeError"));
    pauseOnErrorAction_->setCheckable(true);
    pauseOnErrorAction_->setChecked(true);
    BuildAssetBrowserCommands();
    for (const auto& entry : LuxEngine::Log::ReadAfter(0)) {
        lastEngineLog_ = entry.sequence;
        const auto severity = entry.level >= LuxEngine::LogLevel::Error  ? LuxUI::Severity::Error
                              : entry.level == LuxEngine::LogLevel::Warn ? LuxUI::Severity::Warning
                                                                         : LuxUI::Severity::Info;
        developmentConsole_->Append(severity, "엔진", entry.message);
    }
    gameCanvas_->installEventFilter(this);
    developmentTimer_.setInterval(100);
    connect(&developmentTimer_, &LuxUI::Timer::timeout, this,
            [this] { PollDevelopmentWorkspace(); });
    developmentTimer_.start();
}

void EditorWindow::PollDevelopmentWorkspace() {
    for (const auto& entry : LuxEngine::Log::ReadAfter(lastEngineLog_)) {
        lastEngineLog_ = entry.sequence;
        const auto severity = entry.level >= LuxEngine::LogLevel::Error  ? LuxUI::Severity::Error
                              : entry.level == LuxEngine::LogLevel::Warn ? LuxUI::Severity::Warning
                                                                         : LuxUI::Severity::Info;
        developmentConsole_->Append(severity, "엔진", entry.message);
        if (entry.level >= LuxEngine::LogLevel::Error && previewActive_ && !previewPaused_ &&
            pauseOnErrorAction_->isChecked())
            TogglePreviewPause();
    }
    LuxUI::FrameSample sample;
    sample.updateMs = lastUpdateMs_;
    if (canvas3D_) {
        const auto renderer = viewportTabs_->currentWidget() == gameCanvas_
                                  ? gameCanvas_->GetRenderer()
                                  : canvas3D_->GetRenderer();
        if (renderer) {
            sample.renderMs = renderer->LastRenderMs();
            if (const auto* device = renderer->GetGpuDevice()) {
                const auto stats = device->GetFrameStats();
                sample.gpuMs = stats.gpuTimeMs;
                sample.drawCalls = stats.drawCalls;
                sample.gpuBytes = device->GetMemoryStats().allocationBytes;
            }
        }
    }
    profiler_->Sample(sample);
}

void EditorWindow::SelectPlayAssembly() {
    const auto path = LuxUI::FileDialog::getOpenFileName(
        this, LUX_UI_TEXT("플레이용 C# 어셈블리"),
        project_ ? LuxUI::String::fromUtf8(PathUtf8(project_->GetFile().parent_path()))
                 : LuxUI::String{},
        LUX_UI_TEXT(".NET Assembly (*.dll)"));
    if (!path.isEmpty()) {
        playAssembly_ = PathFromUtf8(path.toUtf8().toStdString());
        playAssemblyProject_ = project_ ? project_->GetFile() : std::filesystem::path{};
    }
}

void EditorWindow::LoadProjectTools() {
    if (!project_ || previewActive_)
        return;
    const auto path = LuxUI::FileDialog::getOpenFileName(
        this, LUX_UI_TEXT("프로젝트 제작 도구"),
        LuxUI::String::fromUtf8(PathUtf8(project_->GetFile().parent_path())),
        LUX_UI_TEXT("Lux 도구 (*.json)"));
    if (path.isEmpty())
        return;
    std::string error;
    if (!LoadProjectToolsFromFile(PathFromUtf8(path.toUtf8().toStdString()), error))
        ShowError(LUX_UI_TEXT("프로젝트 도구"), LuxUI::String::fromUtf8(error));
}

bool EditorWindow::LoadProjectToolsFromFile(const std::filesystem::path& file, std::string& error) {
    error.clear();
    if (!project_ || previewActive_) {
        error = "프로젝트 편집 모드에서 도구를 불러와 주세요.";
        return false;
    }
    std::vector<std::string> added;
    try {
        if (std::filesystem::file_size(file) > 262144)
            throw std::runtime_error("도구 파일이 너무 큽니다.");
        std::ifstream stream(file);
        const auto data = nlohmann::json::parse(stream);
        if (data.at("version") != 1 || !data.at("tools").is_array() || data.at("tools").size() > 64)
            throw std::runtime_error("도구 파일의 형식이 올바르지 않습니다.");
        std::set<std::string> ids;
        for (const auto& [id, title] : commands_.Commands())
            ids.insert(id);
        std::vector<LuxUI::Command> prepared;
        const auto projectFile = project_->GetFile();
        for (const auto& tool : data.at("tools")) {
            const auto id = tool.at("id").get<std::string>();
            const auto title = tool.at("title").get<std::string>();
            const auto count = tool.value("count", 1u);
            const auto spacing = tool.value("spacing", 2.f);
            if (tool.at("action") != "create_boxes" || id.empty() || id.size() > 128 ||
                !std::all_of(id.begin(), id.end(),
                             [](unsigned char c) {
                                 return std::isalnum(c) || c == '.' || c == '_' || c == '-';
                             }) ||
                title.empty() || title.size() > 256 || count == 0 || count > 256 ||
                !std::isfinite(spacing) || spacing < .02f || spacing > 100)
                throw std::runtime_error("지원하지 않는 도구 동작 또는 범위입니다.");
            const auto commandId = "project." + id;
            if (!ids.insert(commandId).second)
                throw std::runtime_error("이미 등록됐거나 중복된 도구 ID입니다: " + id);
            prepared.push_back(
                {commandId, title,
                 [this, count, spacing, title] {
                     RecordEdit(LuxUI::String::fromUtf8(title), [this, count, spacing] {
                         for (unsigned i = 0; i < count; ++i) {
                             auto entity =
                                 document_.GetScene().CreateEntity("Box " + std::to_string(i + 1));
                             entity.GetComponent<LuxEngine::TransformComponent>().position = {
                                 i * spacing, 3, 0};
                             entity.AddComponent<LuxEngine::MeshRendererComponent>();
                             entity.AddComponent<LuxEngine::BoxCollider3DComponent>();
                             entity.AddComponent<LuxEngine::Rigidbody3DComponent>();
                         }
                         document_.MarkDirty();
                     });
                     RefreshAll();
                 },
                 [this, projectFile] {
                     return !previewActive_ && project_ && project_->GetFile() == projectFile;
                 }});
        }
        added.reserve(prepared.size());
        projectToolIds_.reserve(projectToolIds_.size() + prepared.size());
        for (auto& command : prepared) {
            const auto id = command.id;
            if (!RegisterTool(std::move(command)))
                throw std::runtime_error("도구를 등록하지 못했습니다.");
            added.push_back(id);
        }
        projectToolIds_.insert(projectToolIds_.end(), added.begin(), added.end());
        return true;
    } catch (const std::exception& failure) {
        for (const auto& id : added) {
            commands_.Remove(id);
            delete findChild<LuxUI::Action*>(LuxUI::String::fromUtf8(id));
        }
        error = failure.what();
        return false;
    }
}

void EditorWindow::BuildAssetBrowserCommands() {
    projectFiles_->setContextMenuPolicy(LuxUI::Flags::ActionsContextMenu);
    const auto add = [this](const LuxUI::String& text, const char* id, auto callback) {
        auto* action = new LuxUI::Action(text, projectFiles_);
        action->setObjectName(LuxUI::String::fromLatin1(id));
        projectFiles_->addAction(action);
        connect(action, &LuxUI::Action::triggered, this, callback);
    };
    add(LUX_UI_TEXT("에셋 가져오기…"), "importAssets", [this] { ImportAssetsDialog(); });
    add(LUX_UI_TEXT("새 폴더…"), "createAssetFolder", [this] { CreateAssetFolder(); });
    add(LUX_UI_TEXT("에셋 재검색"), "refreshAssets", [this] { RefreshProjectExplorer(); });
}

void EditorWindow::ImportAssetsDialog() {
    if (!project_ || previewActive_)
        return;
    const auto projectFile = project_->GetFile();
    const auto files = LuxUI::FileDialog::getOpenFileNames(this, LUX_UI_TEXT("에셋 가져오기"));
    if (!project_ || project_->GetFile() != projectFile || files.empty())
        return;
    std::vector<std::filesystem::path> paths;
    for (const auto& file : files)
        paths.push_back(PathFromUtf8(file.toUtf8().toStdString()));
    std::string error;
    if (!ImportAssetFiles(paths, error))
        ShowError(LUX_UI_TEXT("에셋 가져오기"), LuxUI::String::fromUtf8(error));
}

bool EditorWindow::ImportAssetFiles(const std::vector<std::filesystem::path>& files,
                                    std::string& error) {
    namespace fs = std::filesystem;
    error.clear();
    std::vector<fs::path> created;
    try {
        if (!project_ || previewActive_ || files.empty() || files.size() > 256)
            throw std::runtime_error("프로젝트 편집 모드에서 1~256개 파일을 선택해 주세요.");
        const auto root = fs::canonical(project_->GetFile().parent_path());
        const auto destination =
            fs::weakly_canonical(root / (assetFolder_.empty() ? fs::path("Assets") : assetFolder_));
        const auto relative = destination.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("프로젝트 내부의 에셋 폴더를 선택해 주세요.");
        std::set<fs::path> targets;
        std::vector<std::pair<fs::path, fs::path>> plan;
        for (const auto& file : files) {
            if (fs::is_symlink(file) || !fs::is_regular_file(file) ||
                fs::file_size(file) > 512ull * 1024 * 1024)
                throw std::runtime_error("512 MiB 이하의 일반 에셋 파일만 가져올 수 있습니다.");
            const auto target = destination / file.filename();
            if (fs::exists(target) || !targets.insert(target).second)
                throw std::runtime_error("같은 이름의 에셋이 이미 있습니다: " +
                                         PathUtf8(file.filename()));
            plan.emplace_back(file, target);
        }
        created.reserve(plan.size());
        fs::create_directories(destination);
        for (const auto& [file, target] : plan) {
            if (!fs::copy_file(file, target, fs::copy_options::none))
                throw std::runtime_error("에셋 파일을 복사하지 못했습니다.");
            created.push_back(target);
        }
        RefreshProjectExplorer();
        return true;
    } catch (const std::exception& failure) {
        for (const auto& file : created) {
            std::error_code ignored;
            fs::remove(file, ignored);
        }
        error = failure.what();
        return false;
    }
}

void EditorWindow::CreateAssetFolder() {
    namespace fs = std::filesystem;
    if (!project_ || previewActive_)
        return;
    const auto projectFile = project_->GetFile();
    bool accepted = false;
    const auto name =
        LuxUI::InputDialog::getText(this, LUX_UI_TEXT("새 에셋 폴더"), LUX_UI_TEXT("폴더 이름"),
                                    LuxUI::LineEdit::Normal, {}, &accepted)
            .trimmed();
    if (!accepted || name.isEmpty() || !project_ || project_->GetFile() != projectFile)
        return;
    try {
        const auto component = PathFromUtf8(name.toUtf8().toStdString());
        if (component != component.filename() || component == "." || component == ".." ||
            component.has_root_path() || name.size() > 128 || name.contains('/') ||
            name.contains('\\'))
            throw std::runtime_error("경로 구분자를 제외한 폴더 이름을 입력해 주세요.");
        const auto root = fs::canonical(projectFile.parent_path());
        const auto folder = fs::weakly_canonical(
            root / (assetFolder_.empty() ? fs::path("Assets") : assetFolder_) / component);
        const auto relative = folder.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("프로젝트 내부에 폴더를 만들어 주세요.");
        if (!fs::create_directories(folder))
            throw std::runtime_error("같은 이름의 폴더가 이미 있습니다.");
        assetFolder_ = relative;
        RefreshProjectExplorer();
    } catch (const std::exception& failure) {
        ShowError(LUX_UI_TEXT("새 에셋 폴더"), LuxUI::String::fromUtf8(failure.what()));
    }
}

namespace {
std::optional<LuxEngine::LuxKey> GameKey(int code) {
    if (code >= 'A' && code <= 'Z')
        return static_cast<LuxEngine::LuxKey>(code + 'a' - 'A');
    if (code >= '0' && code <= '9')
        return static_cast<LuxEngine::LuxKey>(code);
    if (code >= LuxUI::Flags::Key_F1 && code <= LuxUI::Flags::Key_F12 &&
        code != LuxUI::Flags::Key_F5)
        return static_cast<LuxEngine::LuxKey>(static_cast<int>(LuxEngine::LuxKey::F1) + code -
                                              LuxUI::Flags::Key_F1);
    switch (code) {
    case LuxUI::Flags::Key_Left:
        return LuxEngine::LuxKey::Left;
    case LuxUI::Flags::Key_Right:
        return LuxEngine::LuxKey::Right;
    case LuxUI::Flags::Key_Up:
        return LuxEngine::LuxKey::Up;
    case LuxUI::Flags::Key_Down:
        return LuxEngine::LuxKey::Down;
    case LuxUI::Flags::Key_Escape:
        return LuxEngine::LuxKey::Escape;
    case LuxUI::Flags::Key_Return:
    case LuxUI::Flags::Key_Enter:
        return LuxEngine::LuxKey::Enter;
    case LuxUI::Flags::Key_Tab:
        return LuxEngine::LuxKey::Tab;
    case LuxUI::Flags::Key_Backspace:
        return LuxEngine::LuxKey::Backspace;
    case LuxUI::Flags::Key_Delete:
        return LuxEngine::LuxKey::Delete;
    case LuxUI::Flags::Key_Space:
        return LuxEngine::LuxKey::Space;
    case LuxUI::Flags::Key_Shift:
        return LuxEngine::LuxKey::LeftShift;
    case LuxUI::Flags::Key_Control:
        return LuxEngine::LuxKey::LeftCtrl;
    case LuxUI::Flags::Key_Alt:
        return LuxEngine::LuxKey::LeftAlt;
    case LuxUI::Flags::Key_Insert:
        return LuxEngine::LuxKey::Insert;
    case LuxUI::Flags::Key_Home:
        return LuxEngine::LuxKey::Home;
    case LuxUI::Flags::Key_End:
        return LuxEngine::LuxKey::End;
    case LuxUI::Flags::Key_PageUp:
        return LuxEngine::LuxKey::PageUp;
    case LuxUI::Flags::Key_PageDown:
        return LuxEngine::LuxKey::PageDown;
    default:
        return std::nullopt;
    }
}
} // namespace

bool EditorWindow::eventFilter(LuxUI::Object* source, LuxUI::Event* event) {
    if (source == gameCanvas_ && previewActive_) {
        if (event->type() == LuxUI::Event::FocusOut)
            LuxEngine::Input::ClearExternalKeys();
        if (!previewPaused_ && gameCanvas_->IsInputEnabled() && gameCanvas_->hasFocus() &&
            gameCanvas_->isVisible() &&
            (event->type() == LuxUI::Event::KeyPress || event->type() == LuxUI::Event::KeyRelease ||
             event->type() == LuxUI::Event::ShortcutOverride)) {
            const auto* key = static_cast<LuxUI::KeyEvent*>(event);
            const auto code = GameKey(key->key());
            if (code) {
                if (event->type() == LuxUI::Event::ShortcutOverride)
                    event->accept();
                else if (!key->isAutoRepeat())
                    LuxEngine::Input::SetExternalKey(*code,
                                                     event->type() == LuxUI::Event::KeyPress);
                return true;
            }
        }
    }
    return LuxUI::MainWindow::eventFilter(source, event);
}
} // namespace LuxEditor
