#include "Common/SampleApplication.hpp"
#include "Engine/EntryPoint.hpp"
#include <SDL3/SDL_timer.h>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/resource.h>

namespace {
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
class CaptureSample final : public LuxSamples::SampleApplication {
public:
    explicit CaptureSample(const std::filesystem::path& file)
        : SampleApplication("한글 캡처 검사", "가나다 · 화면 저장",
                            {.frames = 1, .capture = file}) {}

private:
    void Reset() override {}
    void Step(float) override {}
    void Draw(float) override {
        LuxSamples::Text("가나다", {-1, 2}, .1f, {1, 1, 1, 1});
    }
};
class FileSizeLimit final {
public:
    explicit FileSizeLimit(rlim_t limit) {
        Check(getrlimit(RLIMIT_FSIZE, &previous_) == 0, "read original file-size limit");
        auto next = previous_;
        next.rlim_cur = limit;
        previousSignal_ = std::signal(SIGXFSZ, SIG_IGN);
        if (setrlimit(RLIMIT_FSIZE, &next) != 0) {
            std::signal(SIGXFSZ, previousSignal_);
            throw std::runtime_error("set temporary file-size limit");
        }
    }
    ~FileSizeLimit() {
        setrlimit(RLIMIT_FSIZE, &previous_);
        std::signal(SIGXFSZ, previousSignal_);
    }

private:
    rlimit previous_{};
    void (*previousSignal_)(int) = SIG_DFL;
};
struct Files {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("lux-sample-capture-" + std::to_string(SDL_GetTicksNS()));
    Files() {
        std::filesystem::create_directories(root);
    }
    ~Files() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};
int RunChecks(int, char**) {
    Files files;
    const auto good = files.root / "정상.ppm", limited = files.root / "쓰기 실패.ppm";
    {
        CaptureSample sample(good);
        if (!sample.Initialize())
            return 77;
        Check(sample.Run() == 0, "normal capture succeeds");
    }
    std::ifstream input(good, std::ios::binary);
    std::string signature;
    unsigned width = 0, height = 0, maximum = 0;
    input >> signature >> width >> height >> maximum;
    Check(input && signature == "P6" && maximum == 255, "capture header is valid PPM");
    input.get();
    const auto header = static_cast<std::uintmax_t>(input.tellg());
    const auto size = std::filesystem::file_size(good);
    Check(width && height && size == header + std::uintmax_t(width) * height * 3,
          "successful capture contains every RGB pixel");
    {
        CaptureSample sample(limited);
        Check(sample.Initialize(), "capture failure fixture initializes");
        bool failed = false;
        {
            // Leave only the final buffered tail unwritable; the earlier writes can succeed.
            FileSizeLimit limit(size - 1);
            try {
                sample.Run();
            } catch (const std::runtime_error& error) {
                failed = std::string_view(error.what()) == "화면 캡처를 저장하지 못했습니다.";
            }
        }
        Check(failed, "final buffered write/close failure must not report capture success");
        Check(std::filesystem::file_size(limited) < size, "fixture actually truncated the output");
    }
    std::cout << "정상 한글 경로 캡처와 마지막 버퍼 쓰기 실패 검증을 통과했습니다.\n";
    return 0;
}
} // namespace
LUX_ENGINE_MAIN(RunChecks)
