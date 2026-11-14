#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Build/BuildOutput.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/SceneDependencies.hpp>
#include <LuxUI/Widgets.hpp>

namespace LuxEditor {
namespace {
constexpr size_t MaxStoredDiagnostics = 300;
constexpr const char* GameExecutable = "LuxGame";

LuxUI::String Q(const std::filesystem::path& path) {
    return LuxUI::String::fromUtf8(PathUtf8(path));
}

std::filesystem::path FromQ(const LuxUI::String& text) {
    return PathFromUtf8(text.toUtf8().toStdString());
}

LuxUI::String SafeFileName(LuxUI::String name) {
    for (const LuxUI::Char c : LUX_UI_TEXT("/\\:*?\"<>|"))
        name.replace(c, u'_');
    name = name.trimmed();
    return name.isEmpty() ? LUX_UI_TEXT("LuxGame") : name;
}

LuxUI::String SizeText(uint64_t bytes) {
    constexpr double mebi = 1024.0 * 1024.0;
    return bytes >= 1024 * 1024 ? LUX_UI_TEXT("%1 MB").arg(bytes / mebi, 0, 'f', 1)
                                : LUX_UI_TEXT("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
}
} // namespace

void EditorWindow::ChooseEnginePackage() {
    LuxUI::Settings settings;
    const LuxUI::String current = settings.value(LUX_UI_TEXT("build/engineSdk")).toString();
    const LuxUI::String directory = LuxUI::FileDialog::getExistingDirectory(
        this, LUX_UI_TEXT("설치된 LuxEngine 폴더 선택 (lib/cmake/LuxEngine이 들어 있는 폴더)"),
        current);
    if (directory.isEmpty())
        return;
    BuildTools tools = DetectBuildTools(
        FromQ(LuxUI::CoreApplication::applicationDirPath()), [&](const char* name) {
            return std::string(name) == "LUX_ENGINE_DIR" ? directory.toUtf8().toStdString()
                                                         : std::string{};
        });
    if (tools.enginePackage.empty()) {
        ShowError(LUX_UI_TEXT("엔진 SDK"),
                  LUX_UI_TEXT("이 폴더에서 LuxEngineConfig.cmake를 찾을 수 없습니다.\n"
                              "엔진을 `cmake --install`로 설치한 폴더를 선택하세요."));
        return;
    }
    settings.setValue(LUX_UI_TEXT("build/engineSdk"), directory);
    Log(LUX_UI_TEXT("엔진 SDK 경로를 설정했습니다: %1").arg(Q(tools.enginePackage)));
}

void EditorWindow::StartBuild(BuildConfiguration configuration, bool clean,
                              BuildFollowUp followUp) {
    if (!project_) {
        ShowError(LUX_UI_TEXT("빌드"), LUX_UI_TEXT("열려 있는 프로젝트가 없습니다."));
        return;
    }
    if (buildProcess_) {
        Log(LUX_UI_TEXT("이미 빌드가 진행 중입니다."), true);
        return;
    }
    if (!SaveAllScenes(true) || !project_)
        return;

    // The SDK path set in the editor wins over the environment variable.
    const LuxUI::String savedSdk =
        LuxUI::Settings().value(LUX_UI_TEXT("build/engineSdk")).toString();
    BuildTools tools = DetectBuildTools(
        FromQ(LuxUI::CoreApplication::applicationDirPath()), [&](const char* name) {
            if (std::string(name) == "LUX_ENGINE_DIR" && !savedSdk.isEmpty())
                return savedSdk.toUtf8().toStdString();
            return qEnvironmentVariable(name).toUtf8().toStdString();
        });
    std::string error;
    if (!ValidateProjectScenes(*project_, error)) {
        ShowError(LUX_UI_TEXT("씬 의존성 오류"), LuxUI::String::fromUtf8(error));
        return;
    }
    auto plan =
        PlanBuild(project_->GetFile().parent_path(), configuration, tools, GameExecutable, error);
    if (!plan) {
        ShowError(LUX_UI_TEXT("빌드"), LuxUI::String::fromUtf8(error));
        return;
    }
    if (clean && !CleanBuildDirectory(*plan, error)) {
        ShowError(LUX_UI_TEXT("클린 빌드"), LuxUI::String::fromUtf8(error));
        return;
    }
    buildPlan_ = std::move(plan);
    buildStep_ = 0;
    buildFollowUp_ = followUp;
    buildDiagnostics_.clear();
    issuesOutput_->clear();
    if (!buildProgress_) {
        buildProgress_ = new LuxUI::ProgressBar(this);
        buildProgress_->setMaximumWidth(160);
        buildProgress_->setRange(0, 100);
        statusBar()->addPermanentWidget(buildProgress_);
    }
    buildProgress_->setRange(0, 0); // busy until a percentage shows up
    buildProgress_->show();
    Log(LUX_UI_TEXT("%1 빌드를 시작합니다: %2")
            .arg(LuxUI::String::fromLatin1(ToString(configuration)),
                 Q(buildPlan_->projectDirectory)));
    RunNextBuildStep();
}

void EditorWindow::RunNextBuildStep() {
    if (!buildPlan_ || buildStep_ >= buildPlan_->steps.size()) {
        FinishBuild(true);
        return;
    }
    const BuildStep& step = buildPlan_->steps[buildStep_];
    Log(LUX_UI_TEXT("[%1/%2] %3")
            .arg(buildStep_ + 1)
            .arg(buildPlan_->steps.size())
            .arg(LuxUI::String::fromUtf8(step.title)));
    buildLineBuffer_.clear();
    buildProcess_ = new LuxUI::Process(this);
    buildProcess_->setProcessEnvironment(Utf8BuildEnvironment());
    buildProcess_->setProcessChannelMode(LuxUI::Process::MergedChannels);
    buildProcess_->setWorkingDirectory(Q(buildPlan_->projectDirectory));
    connect(buildProcess_, &LuxUI::Process::readyReadStandardOutput, this,
            [this] { HandleBuildOutput(buildProcess_->readAllStandardOutput()); });
    connect(buildProcess_, &LuxUI::Process::errorOccurred, this,
            [this](LuxUI::Process::ProcessError error) {
                if (error == LuxUI::Process::FailedToStart) {
                    Log(LUX_UI_TEXT("빌드 도구를 시작하지 못했습니다."), true);
                    FinishBuild(false);
                }
            });
    connect(buildProcess_, &LuxUI::Process::finished, this,
            [this](int exitCode, LuxUI::Process::ExitStatus status) {
                if (!buildProcess_)
                    return;
                HandleBuildOutput({}); // flush the last partial line
                if (status == LuxUI::Process::CrashExit || exitCode != 0) {
                    Log(LUX_UI_TEXT("단계가 실패했습니다 (종료 코드 %1).").arg(exitCode), true);
                    FinishBuild(false);
                    return;
                }
                buildProcess_->deleteLater();
                buildProcess_ = nullptr;
                ++buildStep_;
                RunNextBuildStep();
            });
    LuxUI::StringList arguments;
    for (const std::string& argument : step.arguments)
        arguments << LuxUI::String::fromUtf8(argument);
    buildProcess_->start(Q(step.program), arguments);
}

void EditorWindow::HandleBuildOutput(const LuxUI::ByteArray& chunk) {
    buildLineBuffer_ += chunk;
    // chunk is empty when flushing: treat the remaining text as a final line.
    const bool flush = chunk.isEmpty();
    while (true) {
        int end = buildLineBuffer_.indexOf('\n');
        if (end < 0) {
            if (!flush || buildLineBuffer_.isEmpty())
                break;
            end = buildLineBuffer_.size();
        }
        const LuxUI::ByteArray rawLine = buildLineBuffer_.left(end);
        buildLineBuffer_.remove(0, end + 1);
        const LuxUI::String line = DecodeBuildOutput(rawLine).trimmed();
        if (line.isEmpty())
            continue;
        const std::string text = line.toUtf8().toStdString();
        if (const auto percent = ParseBuildProgress(text)) {
            buildProgress_->setRange(0, 100);
            buildProgress_->setValue(*percent);
        }
        if (const auto diagnostic = ParseBuildLine(text);
            diagnostic && diagnostic->severity != DiagnosticSeverity::Note &&
            buildDiagnostics_.size() < MaxStoredDiagnostics)
            buildDiagnostics_.push_back(*diagnostic);
        consoleOutput_->appendPlainText(line);
    }
}

void EditorWindow::FinishBuild(bool success) {
    const BuildFollowUp followUp = buildFollowUp_;
    buildFollowUp_ = BuildFollowUp::None;
    if (buildProcess_) {
        buildProcess_->disconnect(this);
        buildProcess_->deleteLater();
        buildProcess_ = nullptr;
    }
    if (buildProgress_)
        buildProgress_->hide();

    size_t errors = 0, warnings = 0;
    for (const BuildDiagnostic& d : buildDiagnostics_) {
        const bool isError = d.severity == DiagnosticSeverity::Error;
        (isError ? errors : warnings) += 1;
        LuxUI::String location = LuxUI::String::fromUtf8(d.file);
        if (d.line > 0)
            location += LUX_UI_TEXT("(%1%2)").arg(d.line).arg(
                d.column > 0 ? LUX_UI_TEXT(",%1").arg(d.column) : LuxUI::String());
        issuesOutput_->appendPlainText(
            LUX_UI_TEXT("%1 %2%3: %4")
                .arg(isError ? LUX_UI_TEXT("오류") : LUX_UI_TEXT("경고"), location,
                     d.code.empty() ? LuxUI::String()
                                    : LUX_UI_TEXT(" [%1]").arg(LuxUI::String::fromUtf8(d.code)),
                     LuxUI::String::fromUtf8(d.message)));
    }
    if (!success) {
        Log(LUX_UI_TEXT("빌드 실패: 오류 %1개, 경고 %2개").arg(errors).arg(warnings), true);
        buildPlan_.reset();
        return;
    }
    Log(LUX_UI_TEXT("빌드 성공: 오류 0개, 경고 %1개").arg(warnings));
    const auto executable = buildPlan_ ? FindBuiltExecutable(*buildPlan_) : std::nullopt;
    if (!executable) {
        ShowError(LUX_UI_TEXT("빌드"),
                  LUX_UI_TEXT("빌드는 끝났지만 실행 파일(%1)을 찾을 수 없습니다.")
                      .arg(LuxUI::String::fromLatin1(GameExecutable)));
        buildPlan_.reset();
        return;
    }
    Log(LUX_UI_TEXT("실행 파일: %1").arg(Q(*executable)));
    if (followUp == BuildFollowUp::Run) {
        if (!LuxUI::Process::startDetached(Q(*executable), {}, Q(executable->parent_path())))
            ShowError(LUX_UI_TEXT("실행"), LUX_UI_TEXT("게임을 실행하지 못했습니다."));
    } else if (followUp == BuildFollowUp::Package) {
        PackageBuild();
    }
    buildPlan_.reset();
}

void EditorWindow::PackageBuild() {
    const auto executable = buildPlan_ ? FindBuiltExecutable(*buildPlan_) : std::nullopt;
    if (!executable || !project_)
        return;
    const LuxUI::String name = SafeFileName(LuxUI::String::fromUtf8(project_->GetName()));
    const LuxUI::String version =
        SafeFileName(LuxUI::String::fromUtf8(project_->GetSettings().version));
    const LuxUI::String suggested = LuxUI::Dir(Q(buildPlan_->projectDirectory))
                                        .filePath(LUX_UI_TEXT("%1-%2-win.zip").arg(name, version));
    const LuxUI::String file = LuxUI::FileDialog::getSaveFileName(
        this, LUX_UI_TEXT("배포 ZIP 저장"), suggested, LUX_UI_TEXT("ZIP 파일 (*.zip)"));
    if (file.isEmpty())
        return;
    std::string error;
    std::optional<PackageResult> result;
    RunWithProgress(LUX_UI_TEXT("배포 패키지를 만드는 중..."), [&] {
        result = CreateDistributionArchive(
            *executable, FromQ(file),
            LUX_UI_TEXT("%1-%2").arg(name, version).toUtf8().toStdString(), error);
    });
    if (!result) {
        ShowError(LUX_UI_TEXT("배포"), LuxUI::String::fromUtf8(error));
        return;
    }
    Log(LUX_UI_TEXT("배포 패키지를 만들었습니다: %1 (파일 %2개, 압축 전 %3)")
            .arg(file)
            .arg(result->fileCount)
            .arg(SizeText(result->uncompressedBytes)));
}

void EditorWindow::CancelBuild() {
    if (!buildProcess_)
        return;
    LuxUI::Process* process = buildProcess_;
    buildProcess_ = nullptr;
    process->disconnect(this);
    if (process->state() != LuxUI::Process::NotRunning) {
        process->kill();
        process->waitForFinished(2000);
    }
    process->deleteLater();
    buildFollowUp_ = BuildFollowUp::None;
    buildPlan_.reset();
    if (buildProgress_)
        buildProgress_->hide();
    Log(LUX_UI_TEXT("빌드를 중지했습니다."), true);
}
} // namespace LuxEditor
