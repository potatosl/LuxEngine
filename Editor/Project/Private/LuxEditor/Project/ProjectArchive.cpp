#include <LuxEditor/Project/ErrorEncoding.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectArchive.hpp>
#include <LuxEditor/Project/ProjectFileOperations.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>
#include <zip.h>

namespace LuxEditor {
namespace {
constexpr uint64_t MaxArchiveBytes = 1024ull * 1024ull * 1024ull;    // 1 GiB total extracted size
constexpr uint64_t MaxSingleEntryBytes = 256ull * 1024ull * 1024ull; // 256 MiB per single file
constexpr size_t MaxEntries = 10000;

bool FitsArchiveSizeLimits(uint64_t size, uint64_t total) {
    return size <= MaxSingleEntryBytes && total <= MaxArchiveBytes &&
           size <= MaxArchiveBytes - total;
}

// Validates that a string is strictly valid UTF-8 and contains no control characters.
bool IsValidUtf8Filename(std::string_view str) {
    size_t i = 0;
    while (i < str.size()) {
        const auto c = static_cast<unsigned char>(str[i]);
        if (c == 0 || (c < 32 && c != '\t') || c == 127) // Disallow null, control chars, DEL
            return false;
        if (c < 0x80) {
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= str.size())
                return false;
            const auto c1 = static_cast<unsigned char>(str[i + 1]);
            if ((c1 & 0xC0) != 0x80 || c < 0xC2)
                return false;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= str.size())
                return false;
            const auto c1 = static_cast<unsigned char>(str[i + 1]);
            const auto c2 = static_cast<unsigned char>(str[i + 2]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80)
                return false;
            if (c == 0xE0 && c1 < 0xA0)
                return false;
            if (c == 0xED && c1 >= 0xA0) // UTF-16 surrogates disallowed
                return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (c > 0xF4) // RFC 3629: UTF-8 code points above U+10FFFF (0xF4 0x8F 0xBF 0xBF) are
                          // strictly invalid
                return false;
            if (i + 3 >= str.size())
                return false;
            const auto c1 = static_cast<unsigned char>(str[i + 1]);
            const auto c2 = static_cast<unsigned char>(str[i + 2]);
            const auto c3 = static_cast<unsigned char>(str[i + 3]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
                return false;
            if (c == 0xF0 && c1 < 0x90) // Overlong encoding for U+0000..U+FFFF
                return false;
            if (c == 0xF4 && c1 > 0x8F) // Code points beyond U+10FFFF
                return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

// Converts a path component to lowercase for case-insensitive duplicate and collision detection on
// Windows.
std::string ToLowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

bool IsReparsePointOrSymlink(const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::is_symlink(p, ec))
        return true;
#if defined(_WIN32)
    const DWORD attrs = GetFileAttributesW(p.wstring().c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        return true;
#endif
    return false;
}

std::filesystem::path TemporarySibling(const std::filesystem::path& target) {
    auto path = target;
    path +=
        ".lux-tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return path;
}

bool SafeArchivePath(const std::filesystem::path& relative, bool isDirectory = false) {
    const auto name = GenericPathUtf8(relative);
    if (relative.empty() || relative.is_absolute() || relative.has_root_directory() ||
        relative.has_root_name() || name.starts_with('/') || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos || name.find('\0') != std::string::npos ||
        name.find("//") != std::string::npos || !IsValidUtf8Filename(name))
        return false;

    // Reject reserved characters on Windows: < > " | ? *
    if (name.find_first_of("<>\"|?*") != std::string::npos)
        return false;

    // A non-directory entry cannot end with '/'
    if (!isDirectory && name.back() == '/')
        return false;

    // Case-insensitive check for VCS and engine private directories (.git, .lux, .GIT, .Lux, etc.)
    const auto firstPartLower = ToLowerAscii(GenericPathUtf8(*relative.begin()));
    if (firstPartLower == ".git" || firstPartLower == ".lux")
        return false;

    size_t partIndex = 0;
    const size_t totalParts = std::distance(relative.begin(), relative.end());
    for (const auto& part : relative) {
        ++partIndex;
        const auto p = GenericPathUtf8(part);
        if (p.empty()) {
            // In C++ std::filesystem::path, a trailing slash produces an empty final component.
            // This is valid ONLY for directory entries where the original name ended with '/'.
            if (isDirectory && partIndex == totalParts && name.back() == '/' && totalParts > 1)
                continue;
            return false;
        }
        if (p == ".." || p == ".")
            return false;

        const auto partLower = ToLowerAscii(p);
        if (partLower == ".git" || partLower == ".lux")
            return false;

        // Windows trailing dot or trailing space vulnerability:
        // "file. " or "file." creates issues or bypasses filters on Windows NTFS/FAT.
        if (p.back() == '.' || p.back() == ' ')
            return false;

        // Check Windows DOS device reserved names (CON, PRN, AUX, NUL, COM1-9, LPT1-9, CONIN$,
        // CONOUT$)
        std::string stem = p;
        const auto dot = stem.find('.');
        if (dot != std::string::npos)
            stem = stem.substr(0, dot);
        std::string upperStem;
        upperStem.reserve(stem.size());
        for (char c : stem)
            upperStem.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

        if (upperStem == "CON" || upperStem == "PRN" || upperStem == "AUX" || upperStem == "NUL" ||
            upperStem == "CONIN$" || upperStem == "CONOUT$")
            return false;
        if (upperStem.size() == 4 &&
            (upperStem.starts_with("COM") || upperStem.starts_with("LPT")) && upperStem[3] >= '1' &&
            upperStem[3] <= '9')
            return false;
    }
    return true;
}

struct ValidatedZipEntry {
    zip_uint64_t index = 0;
    std::string name;
    std::filesystem::path relative;
    bool isDirectory = false;
    zip_uint64_t uncompressedSize = 0;
    zip_uint64_t compressedSize = 0;
};

bool ExtractArchive(const std::filesystem::path& archive, const std::filesystem::path& staging,
                    std::string& error) {
    int zipError = 0;
    zip_t* zip = zip_open(PathUtf8(archive).c_str(), ZIP_RDONLY, &zipError);
    if (!zip) {
        error = "프로젝트 압축 파일을 열 수 없습니다.";
        return false;
    }
    const auto count = zip_get_num_entries(zip, 0);
    if (count <= 0 || count > static_cast<zip_int64_t>(MaxEntries)) {
        error = "압축 파일의 파일 수가 올바르지 않습니다.";
        zip_close(zip);
        return false;
    }

    // === PASS 1: Strict Verification Pass ===
    // Verify all headers, names, paths, sizes, symlinks, duplicate entries, and file/directory
    // collisions BEFORE writing any file to disk.
    std::vector<ValidatedZipEntry> entries;
    entries.reserve(static_cast<size_t>(count));
    std::set<std::string> exactNames;
    std::set<std::string> caseFoldedNames;
    std::set<std::string> caseFoldedDirs;
    std::set<std::string> caseFoldedFiles;
    uint64_t totalUncompressed = 0;
    bool foundProjectFile = false;

    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index) {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(zip, index, ZIP_FL_ENC_GUESS, &stat) != 0 || !stat.name) {
            error = "압축 파일의 항목을 읽을 수 없습니다.";
            break;
        }

        const std::string name(stat.name);
        if (!IsValidUtf8Filename(name)) {
            error = "압축 파일 내 파일명이 유효한 UTF-8 문자열이 아닙니다: " + name;
            break;
        }

        const bool directory = !name.empty() && name.back() == '/';
        std::filesystem::path relative;
        try {
            relative = PathFromUtf8(name);
        } catch (...) {
            error = "압축 파일 내 파일명의 인코딩을 변환할 수 없습니다: " + name;
            break;
        }

        // Disallow symlinks: check Unix symlink attributes and Windows reparse/link attributes
        zip_uint8_t opsys = 0;
        zip_uint32_t attributes = 0;
        const bool hasAttributes =
            (zip_file_get_external_attributes(zip, index, 0, &opsys, &attributes) == 0);
        const bool isUnixSymlink =
            hasAttributes && (opsys == ZIP_OPSYS_UNIX && ((attributes >> 16) & 0170000) == 0120000);
        const bool isWindowsReparse =
            hasAttributes && (opsys == ZIP_OPSYS_WINDOWS_NTFS &&
                              (attributes & 0x400) != 0); // FILE_ATTRIBUTE_REPARSE_POINT

        if (isUnixSymlink || isWindowsReparse) {
            error = "압축 파일 내 심볼릭 링크 또는 리파스 포인트 항목은 보안상 허용되지 않습니다.";
            break;
        }

        if (!SafeArchivePath(relative, directory)) {
            error =
                "압축 파일의 경로 또는 파일명이 올바르지 않거나 보안 규칙을 위반했습니다: " + name;
            break;
        }

        // Exact duplicate entry check
        if (!exactNames.insert(name).second) {
            error = "압축 파일 내 중복된 파일 경로가 존재합니다: " + name;
            break;
        }

        // Case-folded duplicate check for Windows case-insensitive filesystem
        const std::string foldedName = ToLowerAscii(name);
        if (!caseFoldedNames.insert(foldedName).second) {
            error = "압축 파일 내 대소문자만 다른 중복 파일 경로가 존재합니다: " + name;
            break;
        }

        // File-Directory collision check (e.g. entry "foo" is a file, but "foo/bar" is a file
        // inside it)
        const std::string cleanFolded = directory && foldedName.back() == '/'
                                            ? foldedName.substr(0, foldedName.size() - 1)
                                            : foldedName;
        if (directory) {
            if (caseFoldedFiles.contains(cleanFolded)) {
                error = "압축 파일 내 파일과 디렉터리 경로 충돌이 발견되었습니다: " + name;
                break;
            }
            caseFoldedDirs.insert(cleanFolded);
        } else {
            if (caseFoldedDirs.contains(cleanFolded)) {
                error = "압축 파일 내 디렉터리와 파일 경로 충돌이 발견되었습니다: " + name;
                break;
            }
            // Check all parent directory prefixes of this file against existing files
            std::string parentCheck = cleanFolded;
            while (true) {
                const auto slash = parentCheck.rfind('/');
                if (slash == std::string::npos)
                    break;
                parentCheck = parentCheck.substr(0, slash);
                if (caseFoldedFiles.contains(parentCheck)) {
                    error = "압축 파일 내 상위 파일과 하위 디렉터리 경로 충돌이 발견되었습니다: " +
                            name;
                    break;
                }
                caseFoldedDirs.insert(parentCheck);
            }
            if (!error.empty())
                break;
            caseFoldedFiles.insert(cleanFolded);
        }

        // Check single entry and cumulative size limits (prevent Zip Bomb and integer overflow)
        if (!FitsArchiveSizeLimits(stat.size, totalUncompressed)) {
            error = "압축 파일의 용량이 허용된 최대 크기를 초과했습니다 (Zip Bomb 방지).";
            break;
        }
        totalUncompressed += stat.size;

        if (!directory && relative == EditorProject::FileName)
            foundProjectFile = true;

        ValidatedZipEntry entry;
        entry.index = index;
        entry.name = name;
        entry.relative = relative;
        entry.isDirectory = directory;
        entry.uncompressedSize = stat.size;
        entry.compressedSize = stat.comp_size;
        entries.push_back(std::move(entry));
    }

    if (!error.empty()) {
        zip_close(zip);
        return false;
    }

    if (!foundProjectFile) {
        error =
            "프로젝트 파일(" + std::string(EditorProject::FileName) + ")이 압축 파일에 없습니다.";
        zip_close(zip);
        return false;
    }

    // === PASS 2: Safe Extraction Pass ===
    // Extract entries to staging directory and verify strict filesystem boundaries.
    const auto stagingNorm = staging.lexically_normal();
    if (IsReparsePointOrSymlink(stagingNorm)) {
        error = "압축 해제 대상 기본 폴더가 보안상 허용되지 않는 심볼릭 링크 또는 "
                "접점(Junction)입니다.";
        zip_close(zip);
        return false;
    }
    uint64_t actualExtractedTotal = 0;

    for (const auto& entry : entries) {
        const auto target = (staging / entry.relative).lexically_normal();
        auto [itStaging, itTarget] =
            std::mismatch(stagingNorm.begin(), stagingNorm.end(), target.begin(), target.end());
        if (itStaging != stagingNorm.end()) {
            error = "압축 파일의 경로가 허용된 범위를 벗어났습니다.";
            break;
        }

        // Boundary check: ensure target and all parent components in staging are not symlinks or
        // junctions
        if (IsReparsePointOrSymlink(target)) {
            error = "압축 해제 대상 경로에 보안상 허용되지 않는 심볼릭 링크 또는 접점(Junction)이 "
                    "존재합니다.";
            break;
        }

        std::error_code ec;
        auto cur = target.parent_path();
        while (cur != stagingNorm && cur.has_relative_path()) {
            if (IsReparsePointOrSymlink(cur)) {
                error = "압축 해제 경로에 보안상 허용되지 않는 심볼릭 링크 또는 접점(Junction)이 "
                        "감지되었습니다.";
                break;
            }
            cur = cur.parent_path();
        }
        if (!error.empty())
            break;

        std::filesystem::create_directories(entry.isDirectory ? target : target.parent_path(), ec);
        if (ec) {
            error = "압축 파일의 폴더를 만들 수 없습니다: " + ErrorMessageUtf8(ec);
            break;
        }
        if (IsReparsePointOrSymlink(entry.isDirectory ? target : target.parent_path())) {
            error = "압축 해제 경로에 보안상 허용되지 않는 심볼릭 링크 또는 접점(Junction)이 "
                    "감지되었습니다.";
            break;
        }
        if (entry.isDirectory)
            continue;

        if (IsReparsePointOrSymlink(target)) {
            error = "압축 해제 대상 파일 경로에 심볼릭 링크 또는 접점(Junction)이 감지되었습니다.";
            break;
        }

        zip_file_t* input = zip_fopen_index(zip, entry.index, 0);
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!input || !output) {
            if (input)
                zip_fclose(input);
            error = "압축 파일의 내용을 읽거나 쓸 수 없습니다.";
            break;
        }

        std::array<char, 65536> buffer{};
        uint64_t readTotal = 0;
        while (true) {
            const zip_int64_t amount = zip_fread(input, buffer.data(), buffer.size());
            if (amount < 0 ||
                (amount > 0 &&
                 (static_cast<uint64_t>(amount) > MaxArchiveBytes - actualExtractedTotal ||
                  static_cast<uint64_t>(amount) > entry.uncompressedSize - readTotal))) {
                error = "압축 파일의 내용이 손상되었거나 지정된 크기 제한을 넘었습니다.";
                break;
            }
            if (amount == 0)
                break;
            output.write(buffer.data(), amount);
            if (!output) {
                error = "압축 파일의 내용을 디스크에 저장할 수 없습니다 (디스크 공간 부족 등).";
                break;
            }
            readTotal += static_cast<uint64_t>(amount);
            actualExtractedTotal += static_cast<uint64_t>(amount);
        }
        zip_fclose(input);

        if (readTotal != entry.uncompressedSize && error.empty())
            error = "압축 파일의 실제 추출 크기가 헤더에 명시된 크기와 일치하지 않습니다.";
        if (!error.empty())
            break;
    }

    zip_close(zip);
    if (!error.empty())
        return false;

    if (!std::filesystem::is_regular_file(staging / EditorProject::FileName)) {
        error = "프로젝트 파일이 압축 파일에 정상적으로 추출되지 않았습니다.";
        return false;
    }
    return true;
}

bool StageArchive(const std::filesystem::path& archive, const std::filesystem::path& staging,
                  std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(staging.parent_path(), ec);
    bool created = false;
    if (!ec)
        created = std::filesystem::create_directory(staging, ec);
    if (!created || ec || !ExtractArchive(archive, staging, error)) {
        if (error.empty())
            error = "프로젝트 임시 폴더를 만들 수 없습니다.";
        if (created)
            std::filesystem::remove_all(staging, ec);
        return false;
    }
    if (!EditorProject::Open(staging / EditorProject::FileName, error)) {
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    // Older packages omitted empty directories. Restore the standard project layout too.
    for (const auto* directory : {"Assets", "Scenes"}) {
        std::filesystem::create_directories(staging / directory, ec);
        if (ec) {
            error = "프로젝트 기본 폴더를 만들 수 없습니다: " + std::string(directory);
            std::filesystem::remove_all(staging, ec);
            return false;
        }
    }
    return true;
}
} // namespace

bool ExportProject(const EditorProject& project, const std::filesystem::path& archive,
                   std::string& error) {
    error.clear();
    const auto root = project.GetFile().parent_path();
    std::error_code ec;
    if (!archive.parent_path().empty())
        std::filesystem::create_directories(archive.parent_path(), ec);
    if (ec) {
        error = "내보낼 폴더를 만들 수 없습니다.";
        return false;
    }
    const auto temporary = TemporarySibling(archive);
    int zipError = 0;
    zip_t* zip = zip_open(PathUtf8(temporary).c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    if (!zip) {
        error = "압축 파일을 만들 수 없습니다.";
        return false;
    }
    uint64_t total = 0;
    size_t entries = 0;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && iterator != end; iterator.increment(ec)) {
        const auto relative = iterator->path().lexically_relative(root);
        if (relative.empty() || *relative.begin() == ".lux" || *relative.begin() == ".git" ||
            iterator->path() == archive || iterator->path() == temporary) {
            if (iterator->is_directory(ec))
                iterator.disable_recursion_pending();
            continue;
        }
        if (IsReparsePointOrSymlink(iterator->path())) {
            error = "심볼릭 링크 또는 접점(Junction)이 있는 프로젝트는 보안상 내보낼 수 없습니다.";
            break;
        }
        const bool directory = iterator->is_directory(ec);
        if (!directory && !iterator->is_regular_file(ec))
            continue;
        if (ec || ++entries > MaxEntries || !SafeArchivePath(relative)) {
            error = "프로젝트 파일 수 또는 크기가 제한을 넘었습니다.";
            break;
        }
        if (directory) {
            if (zip_dir_add(zip, GenericPathUtf8(relative).c_str(), ZIP_FL_ENC_UTF_8) < 0) {
                error = "프로젝트 폴더를 압축하지 못했습니다: " + GenericPathUtf8(relative);
                break;
            }
            continue;
        }
        const uint64_t size = iterator->file_size(ec);
        if (ec) {
            error = "프로젝트 파일 크기를 읽지 못했습니다.";
            break;
        }
        if (!FitsArchiveSizeLimits(size, total)) {
            error = "프로젝트 파일의 크기가 제한을 넘었습니다 (파일당 256 MiB, 전체 1 GiB): " +
                    GenericPathUtf8(relative);
            break;
        }
        const std::string sourcePath = PathUtf8(iterator->path());
        zip_source_t* source = zip_source_file(zip, sourcePath.c_str(), 0, -1);
        if (!source ||
            zip_file_add(zip, GenericPathUtf8(relative).c_str(), source, ZIP_FL_ENC_UTF_8) < 0) {
            if (source)
                zip_source_free(source);
            error = "프로젝트 파일을 압축하지 못했습니다.";
            break;
        }
        total += size;
    }
    if (ec && error.empty())
        error = "프로젝트 파일을 읽지 못했습니다.";
    if (error.empty()) {
        if (zip_close(zip) != 0) {
            error = "압축 파일을 완성하지 못했습니다.";
            zip_discard(zip);
        }
    } else {
        zip_discard(zip);
    }
    if (!error.empty()) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    RenameProjectPath(temporary, archive, ec);
    if (ec && std::filesystem::is_regular_file(archive)) {
        const auto previous = TemporarySibling(archive);
        std::error_code moveError;
        RenameProjectPath(archive, previous, moveError);
        if (!moveError) {
            ec.clear();
            RenameProjectPath(temporary, archive, ec);
            if (ec) {
                std::error_code rollback;
                RenameProjectPath(previous, archive, rollback);
                if (rollback)
                    error =
                        "압축 파일 교체에 실패했습니다. 이전 파일은 임시 위치에 보존했습니다: " +
                        PathUtf8(previous);
            } else {
                std::filesystem::remove(previous, moveError);
            }
        }
    }
    if (ec) {
        if (error.empty())
            error = "압축 파일을 최종 위치로 옮기지 못했습니다.";
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

std::optional<EditorProject> ImportProject(const std::filesystem::path& archive,
                                           const std::filesystem::path& destination,
                                           std::string& error) {
    error.clear();
    std::error_code ec;
    if (std::filesystem::exists(destination, ec)) {
        error = "가져올 프로젝트 폴더가 이미 있습니다.";
        return std::nullopt;
    }
    const auto staging = TemporarySibling(destination);
    if (!StageArchive(archive, staging, error))
        return std::nullopt;
    RenameProjectPath(staging, destination, ec);
    if (ec) {
        error = "프로젝트 폴더를 최종 위치로 옮기지 못했습니다.";
        std::filesystem::remove_all(staging, ec);
        return std::nullopt;
    }
    return EditorProject::Open(destination / EditorProject::FileName, error);
}

std::optional<EditorProject> CloneProject(const EditorProject& project,
                                          const std::filesystem::path& destination,
                                          const std::string& name, std::string& error) {
    auto temporary = TemporarySibling(destination);
    temporary += ".luxpack";
    if (!ExportProject(project, temporary, error))
        return std::nullopt;
    auto copy = ImportProject(temporary, destination, error);
    std::error_code ec;
    std::filesystem::remove(temporary, ec);
    if (!copy)
        return std::nullopt;
    if (!copy->Rename(name, error)) {
        std::filesystem::remove_all(destination, ec);
        return std::nullopt;
    }
    return copy;
}

bool CreateProjectBackup(const EditorProject& project, std::filesystem::path& backup,
                         std::string& error) {
    error.clear();
    const auto directory = project.GetFile().parent_path() / ".lux" / "backups";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = "백업 폴더를 만들 수 없습니다.";
        return false;
    }
    backup =
        directory /
        ("backup-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
         ".luxbackup");
    if (!ExportProject(project, backup, error))
        return false;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> backups;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".luxbackup") {
            const auto modified = entry.last_write_time(ec);
            if (!ec)
                backups.emplace_back(modified, entry.path());
        }
        if (ec)
            break;
    }
    if (ec) {
        error = "백업 목록을 읽을 수 없습니다.";
        return false;
    }
    // Prefixes (backup-/pre-restore-) do not encode age. Always retain the backup just created,
    // including when the filesystem clock has coarse resolution or the system clock changed.
    std::sort(backups.begin(), backups.end(), [&backup](const auto& a, const auto& b) {
        if (a.second == backup || b.second == backup)
            return a.second != backup && b.second == backup;
        return a < b;
    });
    while (backups.size() > project.GetSettings().backupLimit && !backups.empty()) {
        std::filesystem::remove(backups.front().second, ec);
        if (ec) {
            error = "오래된 백업을 정리할 수 없습니다.";
            return false;
        }
        backups.erase(backups.begin());
    }
    return true;
}

bool RestoreProjectBackup(const EditorProject& project, const std::filesystem::path& backup,
                          std::string& error) {
    error.clear();
    const auto root = project.GetFile().parent_path();
    const auto staging = TemporarySibling(root);
    if (!StageArchive(backup, staging, error))
        return false;
    std::error_code ec;
    const auto backups = root / ".lux" / "backups";
    const auto safety =
        backups / ("pre-restore-" +
                   std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
                   ".luxbackup");
    if (!ExportProject(project, safety, error)) {
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    if (std::filesystem::is_directory(backups, ec)) {
        std::filesystem::create_directories(staging / ".lux", ec);
        if (!ec)
            std::filesystem::copy(backups, staging / ".lux" / "backups",
                                  std::filesystem::copy_options::recursive, ec);
        if (ec) {
            error = "기존 백업 목록을 보존할 수 없습니다.";
            std::filesystem::remove_all(staging, ec);
            return false;
        }
    }
    const auto previous = TemporarySibling(root);
    RenameProjectPath(root, previous, ec);
    if (ec) {
        error = "현재 프로젝트를 임시 위치로 옮기지 못했습니다.";
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    RenameProjectPath(staging, root, ec);
    if (ec) {
        std::error_code rollback;
        RenameProjectPath(previous, root, rollback);
        if (rollback) {
            error = "백업 복구 및 이전 프로젝트 롤백에 실패했습니다. 이전 프로젝트는 임시 위치에 "
                    "보존되었습니다: " +
                    PathUtf8(previous);
        } else {
            error = "백업 복구에 실패했습니다. 이전 프로젝트를 정상 복원했습니다.";
        }
        std::filesystem::remove_all(staging, rollback);
        return false;
    }
    std::filesystem::remove_all(previous, ec);
    return true;
}

} // namespace LuxEditor
