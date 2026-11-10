#include "App/EditorWindow.hpp"
#include "Project/PathEncoding.hpp"
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>

namespace LuxEditor {
namespace {
constexpr size_t MaxStoredDiagnostics = 300;
constexpr const char* GameExecutable = "LuxGame";

QString Q(const std::filesystem::path& path) {
    return QString::fromUtf8(PathUtf8(path));
}
std::filesystem::path FromQ(const QString& text) {
    return PathFromUtf8(text.toUtf8().toStdString());
}
QString SafeFileName(QString name) {
    for (const QChar c : QStringLiteral("/\\:*?\"<>|"))
        name.replace(c, u'_');
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("LuxGame") : name;
}
QString SizeText(uint64_t bytes) {
    constexpr double mebi = 1024.0 * 1024.0;
    return bytes >= 1024 * 1024 ? QStringLiteral("%1 MB").arg(bytes / mebi, 0, 'f', 1)
                                : QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
}
} // namespace

void EditorWindow::ChooseEnginePackage() {
    QSettings settings;
    const QString current = settings.value(QStringLiteral("build/engineSdk")).toString();
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("설치된 LuxEngine 폴더 선택 (lib/cmake/LuxEngine이 들어 있는 폴더)"),
        current);
    if (directory.isEmpty())
        return;
    BuildTools tools =
        DetectBuildTools(FromQ(QCoreApplication::applicationDirPath()), [&](const char* name) {
            return std::string(name) == "LUX_ENGINE_DIR" ? directory.toUtf8().toStdString()
                                                         : std::string{};
        });
    if (tools.enginePackage.empty()) {
        ShowError(QStringLiteral("엔진 SDK"),
                  QStringLiteral("이 폴더에서 LuxEngineConfig.cmake를 찾을 수 없습니다.\n"
                                 "엔진을 `cmake --install`로 설치한 폴더를 선택하세요."));
        return;
    }
    settings.setValue(QStringLiteral("build/engineSdk"), directory);
    Log(QStringLiteral("엔진 SDK 경로를 설정했습니다: %1").arg(Q(tools.enginePackage)));
}

void EditorWindow::StartBuild(BuildConfiguration configuration, bool clean,
                              BuildFollowUp followUp) {
    if (!project_) {
        ShowError(QStringLiteral("빌드"), QStringLiteral("열려 있는 프로젝트가 없습니다."));
        return;
    }
    if (buildProcess_) {
        Log(QStringLiteral("이미 빌드가 진행 중입니다."), true);
        return;
    }
    if (document_.IsDirty() && !document_.GetPath().empty() && !SaveScene(false))
        return;

    // The SDK path set in the editor wins over the environment variable.
    const QString savedSdk = QSettings().value(QStringLiteral("build/engineSdk")).toString();
    BuildTools tools =
        DetectBuildTools(FromQ(QCoreApplication::applicationDirPath()), [&](const char* name) {
            if (std::string(name) == "LUX_ENGINE_DIR" && !savedSdk.isEmpty())
                return savedSdk.toUtf8().toStdString();
            return qEnvironmentVariable(name).toUtf8().toStdString();
        });
    std::string error;
    auto plan =
        PlanBuild(project_->GetFile().parent_path(), configuration, tools, GameExecutable, error);
    if (!plan) {
        ShowError(QStringLiteral("빌드"), QString::fromUtf8(error));
        return;
    }
    if (clean && !CleanBuildDirectory(*plan, error)) {
        ShowError(QStringLiteral("클린 빌드"), QString::fromUtf8(error));
        return;
    }
    buildPlan_ = std::move(plan);
    buildStep_ = 0;
    buildFollowUp_ = followUp;
    buildDiagnostics_.clear();
    issuesOutput_->clear();
    if (!buildProgress_) {
        buildProgress_ = new QProgressBar(this);
        buildProgress_->setMaximumWidth(160);
        buildProgress_->setRange(0, 100);
        statusBar()->addPermanentWidget(buildProgress_);
    }
    buildProgress_->setRange(0, 0); // busy until a percentage shows up
    buildProgress_->show();
    Log(QStringLiteral("%1 빌드를 시작합니다: %2")
            .arg(QString::fromLatin1(ToString(configuration)), Q(buildPlan_->projectDirectory)));
    RunNextBuildStep();
}

void EditorWindow::RunNextBuildStep() {
    if (!buildPlan_ || buildStep_ >= buildPlan_->steps.size()) {
        FinishBuild(true);
        return;
    }
    const BuildStep& step = buildPlan_->steps[buildStep_];
    Log(QStringLiteral("[%1/%2] %3")
            .arg(buildStep_ + 1)
            .arg(buildPlan_->steps.size())
            .arg(QString::fromUtf8(step.title)));
    buildLineBuffer_.clear();
    buildProcess_ = new QProcess(this);
    buildProcess_->setProcessChannelMode(QProcess::MergedChannels);
    buildProcess_->setWorkingDirectory(Q(buildPlan_->projectDirectory));
    connect(buildProcess_, &QProcess::readyReadStandardOutput, this,
            [this] { HandleBuildOutput(buildProcess_->readAllStandardOutput()); });
    connect(buildProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            Log(QStringLiteral("빌드 도구를 시작하지 못했습니다."), true);
            FinishBuild(false);
        }
    });
    connect(buildProcess_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (!buildProcess_)
                    return;
                HandleBuildOutput({}); // flush the last partial line
                if (status == QProcess::CrashExit || exitCode != 0) {
                    Log(QStringLiteral("단계가 실패했습니다 (종료 코드 %1).").arg(exitCode), true);
                    FinishBuild(false);
                    return;
                }
                buildProcess_->deleteLater();
                buildProcess_ = nullptr;
                ++buildStep_;
                RunNextBuildStep();
            });
    QStringList arguments;
    for (const std::string& argument : step.arguments)
        arguments << QString::fromUtf8(argument);
    buildProcess_->start(Q(step.program), arguments);
}

void EditorWindow::HandleBuildOutput(const QByteArray& chunk) {
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
        const QByteArray rawLine = buildLineBuffer_.left(end);
        buildLineBuffer_.remove(0, end + 1);
        const QString utf8Line = QString::fromUtf8(rawLine);
        const QString line =
            (utf8Line.contains(QChar::ReplacementCharacter) ? QString::fromLocal8Bit(rawLine)
                                                            : utf8Line)
                .trimmed();
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
        QString location = QString::fromUtf8(d.file);
        if (d.line > 0)
            location += QStringLiteral("(%1%2)").arg(d.line).arg(
                d.column > 0 ? QStringLiteral(",%1").arg(d.column) : QString());
        issuesOutput_->appendPlainText(
            QStringLiteral("%1 %2%3: %4")
                .arg(isError ? QStringLiteral("오류") : QStringLiteral("경고"), location,
                     d.code.empty() ? QString()
                                    : QStringLiteral(" [%1]").arg(QString::fromUtf8(d.code)),
                     QString::fromUtf8(d.message)));
    }
    if (!success) {
        Log(QStringLiteral("빌드 실패: 오류 %1개, 경고 %2개").arg(errors).arg(warnings), true);
        buildPlan_.reset();
        return;
    }
    Log(QStringLiteral("빌드 성공: 오류 0개, 경고 %1개").arg(warnings));
    const auto executable = buildPlan_ ? FindBuiltExecutable(*buildPlan_) : std::nullopt;
    if (!executable) {
        ShowError(QStringLiteral("빌드"),
                  QStringLiteral("빌드는 끝났지만 실행 파일(%1)을 찾을 수 없습니다.")
                      .arg(QString::fromLatin1(GameExecutable)));
        buildPlan_.reset();
        return;
    }
    Log(QStringLiteral("실행 파일: %1").arg(Q(*executable)));
    if (followUp == BuildFollowUp::Run) {
        if (!QProcess::startDetached(Q(*executable), {}, Q(executable->parent_path())))
            ShowError(QStringLiteral("실행"), QStringLiteral("게임을 실행하지 못했습니다."));
    } else if (followUp == BuildFollowUp::Package) {
        PackageBuild();
    }
    buildPlan_.reset();
}

void EditorWindow::PackageBuild() {
    const auto executable = buildPlan_ ? FindBuiltExecutable(*buildPlan_) : std::nullopt;
    if (!executable || !project_)
        return;
    const QString name = SafeFileName(QString::fromUtf8(project_->GetName()));
    const QString version = SafeFileName(QString::fromUtf8(project_->GetSettings().version));
    const QString suggested = QDir(Q(buildPlan_->projectDirectory))
                                  .filePath(QStringLiteral("%1-%2-win.zip").arg(name, version));
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("배포 ZIP 저장"), suggested, QStringLiteral("ZIP 파일 (*.zip)"));
    if (file.isEmpty())
        return;
    std::string error;
    std::optional<PackageResult> result;
    RunWithProgress(QStringLiteral("배포 패키지를 만드는 중..."), [&] {
        result = CreateDistributionArchive(
            *executable, FromQ(file),
            QStringLiteral("%1-%2").arg(name, version).toUtf8().toStdString(), error);
    });
    if (!result) {
        ShowError(QStringLiteral("배포"), QString::fromUtf8(error));
        return;
    }
    Log(QStringLiteral("배포 패키지를 만들었습니다: %1 (파일 %2개, 압축 전 %3)")
            .arg(file)
            .arg(result->fileCount)
            .arg(SizeText(result->uncompressedBytes)));
}

void EditorWindow::CancelBuild() {
    if (!buildProcess_)
        return;
    QProcess* process = buildProcess_;
    buildProcess_ = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(2000);
    }
    process->deleteLater();
    buildFollowUp_ = BuildFollowUp::None;
    buildPlan_.reset();
    if (buildProgress_)
        buildProgress_->hide();
    Log(QStringLiteral("빌드를 중지했습니다."), true);
}
} // namespace LuxEditor
