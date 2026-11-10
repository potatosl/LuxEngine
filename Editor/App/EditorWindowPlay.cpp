#include "App/EditorWindow.hpp"
#include "Viewport/SceneViewport2D.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

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
    playerScenePath_ =
        QDir(QDir::tempPath())
            .filePath(
                QStringLiteral("LuxEditor-play-%1.lux").arg(QCoreApplication::applicationPid()));
    if (!document_.GetScene().Serialize(playerScenePath_.toUtf8().toStdString())) {
        ShowError(QStringLiteral("게임 실행"),
                  QStringLiteral("실행용 장면 파일을 저장하지 못했습니다."));
        return;
    }
    QStringList arguments{playerScenePath_, QStringLiteral("--title"),
                          QStringLiteral("LuxEngine - %1").arg(windowTitle())};
    if (!document_.GetScene().GetAssetRoot().empty())
        arguments << QStringLiteral("--asset-root")
                  << QString::fromUtf8(document_.GetScene().GetAssetRoot());
    if (canvas_) {
        double centerX = 0, centerY = 0, halfHeight = 0;
        canvas_->GetView(centerX, centerY, halfHeight);
        arguments << QStringLiteral("--view") << QString::number(centerX, 'g', 9)
                  << QString::number(centerY, 'g', 9) << QString::number(halfHeight, 'g', 9);
    }
    playerProcess_ = new QProcess(this);
    playerProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(playerProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        const QString output = QString::fromUtf8(playerProcess_->readAllStandardOutput());
        for (const QString& line : output.split(u'\n', Qt::SkipEmptyParts))
            Log(QStringLiteral("[게임] %1").arg(line.trimmed()));
    });
    connect(playerProcess_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (status == QProcess::CrashExit)
                    Log(QStringLiteral("게임 창이 비정상 종료되었습니다."), true);
                else if (exitCode != 0)
                    Log(QStringLiteral("게임 창이 오류 코드 %1로 종료되었습니다.").arg(exitCode),
                        true);
                else
                    Log(QStringLiteral("게임 창을 닫았습니다."));
                playerProcess_->deleteLater();
                playerProcess_ = nullptr;
                QFile::remove(playerScenePath_);
            });
    connect(playerProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            Log(QStringLiteral("LuxPlayer를 시작하지 못했습니다."), true);
    });
    playerProcess_->setWorkingDirectory(QFileInfo(executable).absolutePath());
    playerProcess_->start(executable, arguments);
    Log(QStringLiteral("현재 장면을 별도 창에서 실행합니다. (Esc 닫기, R 다시 불러오기)"));
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
}
} // namespace LuxEditor
