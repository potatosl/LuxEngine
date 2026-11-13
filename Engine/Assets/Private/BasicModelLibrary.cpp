#include <LuxEngine/Assets/BasicModelLibrary.hpp>
#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <cerrno>
#include <stdio.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace LuxEngine {
namespace {
namespace fs = std::filesystem;
constexpr std::array<BasicModelDescriptor, 10> Models{{
    {"shape-cube", "큐브", "shape-cube.fbx"},
    {"shape-cylinder", "원기둥", "shape-cylinder.fbx"},
    {"floor-square", "평면", "floor-square.fbx"},
    {"shape-cube-rounded", "둥근 큐브", "shape-cube-rounded.fbx"},
    {"shape-slope", "경사면", "shape-slope.fbx"},
    {"shape-triangular-prism", "삼각기둥", "shape-triangular-prism.fbx"},
    {"shape-hexagon", "육각기둥", "shape-hexagon.fbx"},
    {"shape-cube-half", "반 높이 큐브", "shape-cube-half.fbx"},
    {"shape-cylinder-detailed", "고해상도 원기둥", "shape-cylinder-detailed.fbx"},
    {"shape-cylinder-half", "반쪽 원기둥", "shape-cylinder-half.fbx"},
}};
constexpr std::uintmax_t MaximumPackageBytes = 128 * 1024 * 1024;
std::mutex InstallMutex;
std::atomic<unsigned long long> Sequence{0};

fs::path Utf8Path(const std::string& value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

std::vector<fs::path> PackageFiles() {
    std::vector<fs::path> files;
    for (const auto& model : Models)
        files.emplace_back(model.fileName);
    files.emplace_back("Textures/colormap.png");
    files.emplace_back("License.txt");
    files.emplace_back("Provenance.json");
    return files;
}

bool RegularPackageFile(const fs::path& directory, const fs::path& relative) {
    auto current = directory;
    for (const auto& part : relative) {
        current /= part;
        const auto status = fs::symlink_status(current);
        if (fs::is_symlink(status))
            return false;
    }
    return fs::is_regular_file(current);
}

bool CompletePackage(const fs::path& directory) {
    try {
        if (!fs::is_directory(directory))
            return false;
        for (const auto& relative : PackageFiles())
            if (!RegularPackageFile(directory, relative) ||
                fs::file_size(directory / relative) == 0)
                return false;
        return true;
    } catch (const fs::filesystem_error&) {
        return false;
    }
}

struct PackageFile {
    fs::path relative;
    std::vector<char> bytes;
};

std::vector<PackageFile> ReadPackage(const fs::path& source) {
    std::uintmax_t remaining = MaximumPackageBytes;
    std::vector<PackageFile> files;
    for (const auto& relative : PackageFiles()) {
        if (!RegularPackageFile(source, relative))
            throw std::runtime_error("기본 모델 패키지에 파일이 없거나 링크가 포함되어 있습니다.");
        const auto size = fs::file_size(source / relative);
        if (size == 0 || size > remaining)
            throw std::runtime_error("기본 모델 패키지의 크기가 올바르지 않습니다.");
        remaining -= size;
        PackageFile file{relative, std::vector<char>(static_cast<size_t>(size))};
        std::ifstream input(source / relative, std::ios::binary);
        if (!input || !input.read(file.bytes.data(), static_cast<std::streamsize>(size)) ||
            input.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("기본 모델 패키지를 읽을 수 없습니다.");
        files.push_back(std::move(file));
    }
    return files;
}

bool SamePackage(const fs::path& directory, const std::vector<PackageFile>& files) {
    try {
        if (fs::is_symlink(fs::symlink_status(directory)) || !fs::is_directory(directory))
            return false;
        for (const auto& file : files) {
            if (!RegularPackageFile(directory, file.relative) ||
                fs::file_size(directory / file.relative) != file.bytes.size())
                return false;
            std::ifstream input(directory / file.relative, std::ios::binary);
            std::vector<char> bytes(file.bytes.size());
            if (!input || !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
                input.peek() != std::char_traits<char>::eof() || bytes != file.bytes)
                return false;
        }
        return true;
    } catch (const fs::filesystem_error&) {
        return false;
    }
}

fs::path DestinationParent(const fs::path& root) {
    auto current = root;
    for (const char* part : {"Assets", "Models", "Primitives"}) {
        current /= part;
        std::error_code error;
        auto status = fs::symlink_status(current, error);
        if (!fs::exists(status)) {
            fs::create_directory(current);
            status = fs::symlink_status(current);
        }
        if (fs::is_symlink(status) || !fs::is_directory(status))
            throw std::runtime_error("기본 모델 설치 경로에는 디렉터리 링크를 사용할 수 없습니다.");
    }
    return current;
}

enum class PublishResult { Complete, Exists, Failed };

PublishResult PublishDirectory(const fs::path& source, const fs::path& target) {
    // Exclusive rename publishes a complete package without replacing a directory created by
    // another process between the existence check and publication.
#if defined(_WIN32)
    if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        return PublishResult::Complete;
    const auto error = GetLastError();
    return error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? PublishResult::Exists
                                                                       : PublishResult::Failed;
#elif defined(__APPLE__)
    if (renamex_np(source.c_str(), target.c_str(), RENAME_EXCL) == 0)
        return PublishResult::Complete;
    return errno == EEXIST || errno == ENOTEMPTY ? PublishResult::Exists : PublishResult::Failed;
#else
    if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, target.c_str(),
                RENAME_NOREPLACE) == 0)
        return PublishResult::Complete;
    return errno == EEXIST || errno == ENOTEMPTY ? PublishResult::Exists : PublishResult::Failed;
#endif
}

struct StagingDirectory {
    fs::path path;

    ~StagingDirectory() {
        if (!path.empty()) {
            std::error_code error;
            fs::remove_all(path, error);
        }
    }
};

void Result(const fs::path& directory, const BasicModelDescriptor& model,
            BasicModelInstallation& installation) {
    installation.directory = directory;
    installation.modelPath = directory / model.fileName;
    installation.baseColorTexture = directory / "Textures/colormap.png";
}
} // namespace

std::span<const BasicModelDescriptor> BasicModelLibrary::GetModels() noexcept {
    return Models;
}

const BasicModelDescriptor* BasicModelLibrary::FindModel(std::string_view id) noexcept {
    const auto found = std::find_if(Models.begin(), Models.end(),
                                    [&](const auto& model) { return id == model.id; });
    return found == Models.end() ? nullptr : &*found;
}

fs::path BasicModelLibrary::FindLibraryDirectory() {
    std::vector<fs::path> candidates;
    if (const char* base = SDL_GetBasePath()) {
        auto directory = Utf8Path(base).lexically_normal();
        // SDL may return an app bundle's resource or executable directory. Also account for
        // SDK/bin and multi-configuration build/bin/<Config> without embedding an install prefix.
        for (size_t depth = 0; depth < 5; ++depth) {
            candidates.push_back(directory / "Resources/Models/Primitives/KenneyPrototype");
            candidates.push_back(directory / "share/LuxEngine/models/Primitives/KenneyPrototype");
            candidates.push_back(directory / "Models/Primitives/KenneyPrototype");
            const auto parent = directory.parent_path();
            if (parent == directory)
                break;
            directory = parent;
        }
    }
#ifdef LUX_BASIC_MODEL_SOURCE_DIR
    candidates.emplace_back(Utf8Path(LUX_BASIC_MODEL_SOURCE_DIR));
#endif
    for (const auto& directory : candidates)
        if (CompletePackage(directory))
            return fs::weakly_canonical(directory);
    return {};
}

bool BasicModelLibrary::IdentifyInstalledModel(const fs::path& modelPath,
                                               BasicModelInstallation& installation) {
    installation = {};
    try {
        const auto found = std::find_if(Models.begin(), Models.end(), [&](const auto& model) {
            return modelPath.filename() == model.fileName;
        });
        const auto library = FindLibraryDirectory();
        if (found == Models.end() || library.empty() ||
            !SamePackage(modelPath.parent_path(), ReadPackage(library)))
            return false;
        Result(modelPath.parent_path(), *found, installation);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool BasicModelLibrary::InstallIntoProject(std::string_view id, const fs::path& projectRoot,
                                           BasicModelInstallation& installation, std::string& error,
                                           const fs::path& libraryDirectory) {
    installation = {};
    error.clear();
    std::lock_guard lock(InstallMutex);
    try {
        const auto* model = FindModel(id);
        if (!model)
            throw std::runtime_error("지원하지 않는 기본 모델입니다.");
        if (!projectRoot.is_absolute() ||
            projectRoot.native().find(fs::path::value_type{}) != fs::path::string_type::npos ||
            std::find(projectRoot.begin(), projectRoot.end(), fs::path("..")) !=
                projectRoot.end() ||
            !fs::is_directory(projectRoot))
            throw std::runtime_error("기본 모델을 추가할 프로젝트 경로를 확인해 주세요.");
        const auto root = fs::canonical(projectRoot);
        const auto source = libraryDirectory.empty() ? FindLibraryDirectory() : libraryDirectory;
        if (source.empty())
            throw std::runtime_error(
                "기본 모델 패키지를 찾을 수 없습니다. 설치 파일을 확인해 주세요.");
        const auto files = ReadPackage(source);
        const auto parent = DestinationParent(root);
        StagingDirectory staging;
        for (unsigned int attempt = 1; attempt <= 1000; ++attempt) {
            const auto directory =
                parent / (attempt == 1 ? std::string("KenneyPrototype")
                                       : "KenneyPrototype-" + std::to_string(attempt));
            if (fs::exists(fs::symlink_status(directory))) {
                if (SamePackage(directory, files)) {
                    Result(directory, *model, installation);
                    return true;
                }
                continue;
            }
            if (staging.path.empty()) {
                const auto suffix =
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    "-" + std::to_string(++Sequence);
                auto candidate = parent / (".KenneyPrototype-install-" + suffix);
                if (!fs::create_directory(candidate))
                    throw std::runtime_error("기본 모델 임시 설치 경로를 만들 수 없습니다.");
                // Cleanup owns only a directory that this call created exclusively.
                staging.path = std::move(candidate);
                fs::create_directory(staging.path / "Textures");
                for (const auto& file : files) {
                    std::ofstream output(staging.path / file.relative, std::ios::binary);
                    if (!output ||
                        !output.write(file.bytes.data(),
                                      static_cast<std::streamsize>(file.bytes.size())) ||
                        !output.flush())
                        throw std::runtime_error("기본 모델 파일을 저장할 수 없습니다.");
                }
            }
            const auto published = PublishDirectory(staging.path, directory);
            if (published == PublishResult::Exists)
                continue;
            if (published != PublishResult::Complete)
                throw std::runtime_error("기본 모델 패키지를 설치할 수 없습니다.");
            staging.path.clear();
            Result(directory, *model, installation);
            return true;
        }
        throw std::runtime_error("기본 모델 설치 폴더가 너무 많습니다.");
    } catch (const std::exception& failure) {
        error = failure.what();
        installation = {};
        return false;
    }
}

} // namespace LuxEngine
