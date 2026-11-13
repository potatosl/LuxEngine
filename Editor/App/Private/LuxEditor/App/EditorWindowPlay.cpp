#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryFile>
#include <QVariant>

namespace LuxEditor {
namespace {
QString PlayerExecutable() {
#ifdef _WIN32
    const QString name = QStringLiteral("LuxPlayer.exe");
#else
    const QString name = QStringLiteral("LuxPlayer");
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}
} // namespace

void EditorWindow::RunSceneInWindow() {
    const QString executable = PlayerExecutable();
    if (!QFileInfo::exists(executable)) {
        ShowError(QStringLiteral("게임 실행"),
                  QStringLiteral("LuxPlayer를 찾을 수 없습니다: %1").arg(executable));
        return;
    }
    StopSceneWindow();
    // Run what is on screen, including unsaved edits, without touching the user's scene file.
    QTemporaryFile temporary(QDir::tempPath() + QStringLiteral("/LuxEditor-play-XXXXXX.lux"));
    if (!temporary.open()) {
        ShowError(QStringLiteral("게임 실행"),
                  QStringLiteral("실행용 씬 파일을 만들 수 없습니다."));
        return;
    }
    playerScenePath_ = temporary.fileName();
    temporary.setAutoRemove(false);
    temporary.close();
    std::string error;
    if (!LuxEngine::SceneManager::Save(document_.GetScene(),
                                       std::filesystem::path(playerScenePath_.toStdU16String()),
                                       PathFromUtf8(document_.GetPath()), error)) {
        QFile::remove(playerScenePath_);
        playerScenePath_.clear();
        ShowError(QStringLiteral("게임 실행"),
                  QStringLiteral("실행용 장면 파일을 저장하지 못했습니다."));
        return;
    }
    QStringList arguments{playerScenePath_, QStringLiteral("--title"),
                          QStringLiteral("LuxEngine - %1").arg(windowTitle())};
    if (canvas_) {
        double centerX = 0, centerY = 0, halfHeight = 0;
        canvas_->GetView(centerX, centerY, halfHeight);
        arguments << QStringLiteral("--view") << QString::number(centerX, 'g', 9)
                  << QString::number(centerY, 'g', 9) << QString::number(halfHeight, 'g', 9);
    }
    playerProcess_ = new QProcess(this);
    playerProcess_->setProcessChannelMode(QProcess::MergedChannels);
    auto* process = playerProcess_;
    const auto scenePath = playerScenePath_;
    const auto consume = [this, process](bool flush) {
        auto buffer =
            process->property("outputBuffer").toByteArray() + process->readAllStandardOutput();
        for (;;) {
            const auto newline = buffer.indexOf('\n');
            if (newline < 0 && !flush && buffer.size() < 65536)
                break;
            if (buffer.isEmpty())
                break;
            const auto count = newline < 0 ? buffer.size() : newline;
            const auto line = QString::fromUtf8(buffer.left(count)).trimmed();
            buffer.remove(0, count + (newline >= 0 ? 1 : 0));
            if (line.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("warning"), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
                line.contains(QStringLiteral("VUID-")))
                Log(QStringLiteral("[게임] %1").arg(line), true);
        }
        process->setProperty("outputBuffer", buffer);
    };
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, consume] {
        if (playerProcess_ == process)
            consume(false);
    });
    connect(playerProcess_, &QProcess::finished, this,
            [this, process, scenePath, consume](int exitCode, QProcess::ExitStatus status) {
                if (playerProcess_ != process)
                    return;
                consume(true);
                if (status == QProcess::CrashExit)
                    Log(QStringLiteral("게임 창이 비정상 종료되었습니다."), true);
                else if (exitCode != 0)
                    Log(QStringLiteral("게임 창이 오류 코드 %1로 종료되었습니다.").arg(exitCode),
                        true);
                process->deleteLater();
                playerProcess_ = nullptr;
                QFile::remove(scenePath);
                playerScenePath_.clear();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, scenePath](QProcess::ProcessError error) {
                if (playerProcess_ != process)
                    return;
                if (error == QProcess::FailedToStart) {
                    Log(QStringLiteral("LuxPlayer를 시작하지 못했습니다."), true);
                    playerProcess_ = nullptr;
                    process->deleteLater();
                    QFile::remove(scenePath);
                    playerScenePath_.clear();
                }
            });
    playerProcess_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    playerProcess_->start(executable, arguments);
}

void EditorWindow::StopSceneWindow() {
    if (!playerProcess_)
        return;
    QProcess* process = playerProcess_;
    playerProcess_ = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        if (!process->waitForFinished(1500)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
    process->deleteLater();
    QFile::remove(playerScenePath_);
    playerScenePath_.clear();
}
} // namespace LuxEditor
