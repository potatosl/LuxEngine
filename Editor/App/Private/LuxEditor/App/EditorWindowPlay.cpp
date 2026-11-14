#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxUI/Widgets.hpp>

namespace LuxEditor {
namespace {
LuxUI::String PlayerExecutable() {
#ifdef _WIN32
    const LuxUI::String name = LUX_UI_TEXT("LuxPlayer.exe");
#else
    const LuxUI::String name = LUX_UI_TEXT("LuxPlayer");
#endif
    return LuxUI::Dir(LuxUI::CoreApplication::applicationDirPath()).filePath(name);
}
} // namespace

void EditorWindow::RunSceneInWindow() {
    const LuxUI::String executable = PlayerExecutable();
    if (!LuxUI::FileInfo::exists(executable)) {
        ShowError(LUX_UI_TEXT("게임 실행"),
                  LUX_UI_TEXT("LuxPlayer를 찾을 수 없습니다: %1").arg(executable));
        return;
    }
    StopSceneWindow();
    // Run what is on screen, including unsaved edits, without touching the user's scene file.
    LuxUI::TemporaryFile temporary(LuxUI::Dir::tempPath() +
                                   LUX_UI_TEXT("/LuxEditor-play-XXXXXX.lux"));
    if (!temporary.open()) {
        ShowError(LUX_UI_TEXT("게임 실행"), LUX_UI_TEXT("실행용 씬 파일을 만들 수 없습니다."));
        return;
    }
    playerScenePath_ = temporary.fileName();
    temporary.setAutoRemove(false);
    temporary.close();
    std::string error;
    if (!LuxEngine::SceneManager::Save(document_.GetScene(),
                                       std::filesystem::path(playerScenePath_.toStdU16String()),
                                       PathFromUtf8(document_.GetPath()), error)) {
        LuxUI::File::remove(playerScenePath_);
        playerScenePath_.clear();
        ShowError(LUX_UI_TEXT("게임 실행"), LUX_UI_TEXT("실행용 장면 파일을 저장하지 못했습니다."));
        return;
    }
    LuxUI::StringList arguments{playerScenePath_, LUX_UI_TEXT("--title"),
                                LUX_UI_TEXT("LuxEngine - %1").arg(windowTitle())};
    if (canvas_) {
        double centerX = 0, centerY = 0, halfHeight = 0;
        canvas_->GetView(centerX, centerY, halfHeight);
        arguments << LUX_UI_TEXT("--view") << LuxUI::String::number(centerX, 'g', 9)
                  << LuxUI::String::number(centerY, 'g', 9)
                  << LuxUI::String::number(halfHeight, 'g', 9);
    }
    playerProcess_ = new LuxUI::Process(this);
    playerProcess_->setProcessChannelMode(LuxUI::Process::MergedChannels);
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
            const auto line = LuxUI::String::fromUtf8(buffer.left(count)).trimmed();
            buffer.remove(0, count + (newline >= 0 ? 1 : 0));
            if (line.contains(LUX_UI_TEXT("error"), LuxUI::Flags::CaseInsensitive) ||
                line.contains(LUX_UI_TEXT("warning"), LuxUI::Flags::CaseInsensitive) ||
                line.contains(LUX_UI_TEXT("failed"), LuxUI::Flags::CaseInsensitive) ||
                line.contains(LUX_UI_TEXT("VUID-")))
                Log(LUX_UI_TEXT("[게임] %1").arg(line), true);
        }
        process->setProperty("outputBuffer", buffer);
    };
    connect(process, &LuxUI::Process::readyReadStandardOutput, this, [this, process, consume] {
        if (playerProcess_ == process)
            consume(false);
    });
    connect(playerProcess_, &LuxUI::Process::finished, this,
            [this, process, scenePath, consume](int exitCode, LuxUI::Process::ExitStatus status) {
                if (playerProcess_ != process)
                    return;
                consume(true);
                if (status == LuxUI::Process::CrashExit)
                    Log(LUX_UI_TEXT("게임 창이 비정상 종료되었습니다."), true);
                else if (exitCode != 0)
                    Log(LUX_UI_TEXT("게임 창이 오류 코드 %1로 종료되었습니다.").arg(exitCode),
                        true);
                process->deleteLater();
                playerProcess_ = nullptr;
                LuxUI::File::remove(scenePath);
                playerScenePath_.clear();
            });
    connect(process, &LuxUI::Process::errorOccurred, this,
            [this, process, scenePath](LuxUI::Process::ProcessError error) {
                if (playerProcess_ != process)
                    return;
                if (error == LuxUI::Process::FailedToStart) {
                    Log(LUX_UI_TEXT("LuxPlayer를 시작하지 못했습니다."), true);
                    playerProcess_ = nullptr;
                    process->deleteLater();
                    LuxUI::File::remove(scenePath);
                    playerScenePath_.clear();
                }
            });
    playerProcess_->setWorkingDirectory(LuxUI::FileInfo(executable).absolutePath());
    playerProcess_->start(executable, arguments);
}

void EditorWindow::StopSceneWindow() {
    if (!playerProcess_)
        return;
    LuxUI::Process* process = playerProcess_;
    playerProcess_ = nullptr;
    process->disconnect(this);
    if (process->state() != LuxUI::Process::NotRunning) {
        process->terminate();
        if (!process->waitForFinished(1500)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
    process->deleteLater();
    LuxUI::File::remove(playerScenePath_);
    playerScenePath_.clear();
}
} // namespace LuxEditor
