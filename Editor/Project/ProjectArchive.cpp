#include "ProjectArchive.hpp"
#include "PathEncoding.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <memory>
#include <set>
#include <vector>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

namespace LuxEngine {
namespace {
constexpr uint64_t MaxArchiveBytes = 1024ull * 1024ull * 1024ull;
constexpr size_t MaxEntries = 10000;

wxString W(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return wxString::FromUTF8(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::filesystem::path TemporarySibling(const std::filesystem::path& target) {
    auto path = target;
    path +=
        ".lux-tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return path;
}

bool SafeArchivePath(const std::filesystem::path& relative) {
    const auto name = GenericPathUtf8(relative);
    if (relative.empty() || relative.is_absolute() || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos || name.find('\0') != std::string::npos ||
        name.find("//") != std::string::npos)
        return false;
    if (*relative.begin() == ".git" || *relative.begin() == ".lux")
        return false;
    for (const auto& part : relative)
        if (part == ".." || part == ".")
            return false;
    return true;
}

bool CopyStream(wxInputStream& input, wxOutputStream& output, uint64_t& total) {
    std::array<char, 65536> buffer{};
    while (input.IsOk() && !input.Eof()) {
        input.Read(buffer.data(), buffer.size());
        const auto count = input.LastRead();
        if (count == 0)
            break;
        if (total > MaxArchiveBytes - count)
            return false;
        output.Write(buffer.data(), count);
        if (output.LastWrite() != count)
            return false;
        total += count;
    }
    return input.Eof() || input.IsOk();
}

bool ExtractArchive(const std::filesystem::path& archive, const std::filesystem::path& staging,
                    std::string& error) {
    wxFFileInputStream file(W(archive));
    if (!file.IsOk()) {
        error = "프로젝트 압축 파일을 열 수 없습니다.";
        return false;
    }
    wxZipInputStream zip(file, wxConvUTF8);
    std::set<std::string> names;
    uint64_t total = 0;
    size_t entries = 0;
    while (auto entry = std::unique_ptr<wxZipEntry>(zip.GetNextEntry())) {
        const std::string name = entry->GetName(wxPATH_UNIX).ToStdString(wxConvUTF8);
        const auto relative = PathFromUtf8(name);
        if (++entries > MaxEntries || !SafeArchivePath(relative) || !names.insert(name).second ||
            (entry->GetSize() != wxInvalidOffset &&
             static_cast<uint64_t>(entry->GetSize()) > MaxArchiveBytes)) {
            error = "압축 파일의 경로 또는 크기가 올바르지 않습니다.";
            return false;
        }
        const auto target = staging / relative;
        std::error_code ec;
        if (entry->IsDir()) {
            std::filesystem::create_directories(target, ec);
            if (ec) {
                error = "압축 파일의 폴더를 만들 수 없습니다.";
                return false;
            }
            continue;
        }
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            error = "압축 파일의 폴더를 만들 수 없습니다.";
            return false;
        }
        wxFFileOutputStream output(W(target));
        if (!output.IsOk() || !CopyStream(zip, output, total)) {
            error = "압축 파일의 내용을 읽을 수 없거나 크기 제한을 넘었습니다.";
            return false;
        }
    }
    if (zip.GetLastError() != wxSTREAM_EOF && zip.GetLastError() != wxSTREAM_NO_ERROR) {
        error = "압축 파일이 손상되었습니다.";
        return false;
    }
    if (entries == 0 || !std::filesystem::is_regular_file(staging / EditorProject::FileName)) {
        error = "프로젝트 파일이 압축 파일에 없습니다.";
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
    wxFFileOutputStream file(W(temporary));
    if (!file.IsOk()) {
        error = "압축 파일을 만들 수 없습니다.";
        return false;
    }
    wxZipOutputStream zip(file);
    uint64_t total = 0;
    size_t entries = 0;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && iterator != end; iterator.increment(ec)) {
        const auto relative = iterator->path().lexically_relative(root);
        if (relative.empty() || *relative.begin() == ".lux" ||
            *relative.begin() == ".git" || iterator->path() == archive ||
            iterator->path() == temporary) {
            if (iterator->is_directory(ec))
                iterator.disable_recursion_pending();
            continue;
        }
        if (iterator->is_symlink(ec)) {
            error = "심볼릭 링크가 있는 프로젝트는 내보낼 수 없습니다.";
            break;
        }
        if (!iterator->is_regular_file(ec))
            continue;
        if (++entries > MaxEntries || !SafeArchivePath(relative) ||
            iterator->file_size(ec) > MaxArchiveBytes - total) {
            error = "프로젝트 파일 수 또는 크기가 제한을 넘었습니다.";
            break;
        }
        wxFFileInputStream input(W(iterator->path()));
        if (!input.IsOk() || !zip.PutNextEntry(wxString::FromUTF8(GenericPathUtf8(relative))) ||
            !CopyStream(input, zip, total)) {
            error = "프로젝트 파일을 압축하지 못했습니다.";
            break;
        }
    }
    if (ec && error.empty())
        error = "프로젝트 파일을 읽지 못했습니다.";
    if (!zip.Close() && error.empty())
        error = "압축 파일을 완성하지 못했습니다.";
    file.Close();
    if (!error.empty()) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    if (!wxRenameFile(W(temporary), W(archive), true)) {
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
    std::filesystem::rename(staging, destination, ec);
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
    std::vector<std::filesystem::path> backups;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".luxbackup")
            backups.push_back(entry.path());
    if (ec) {
        error = "백업 목록을 읽을 수 없습니다.";
        return false;
    }
    std::sort(backups.begin(), backups.end());
    while (backups.size() > project.GetSettings().backupLimit && !backups.empty()) {
        std::filesystem::remove(backups.front(), ec);
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
    std::filesystem::rename(root, previous, ec);
    if (ec) {
        error = "현재 프로젝트를 임시 위치로 옮기지 못했습니다.";
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    std::filesystem::rename(staging, root, ec);
    if (ec) {
        std::error_code rollback;
        std::filesystem::rename(previous, root, rollback);
        error = "백업 복구에 실패했습니다. 이전 프로젝트를 복원했습니다.";
        std::filesystem::remove_all(staging, rollback);
        return false;
    }
    std::filesystem::remove_all(previous, ec);
    return true;
}

} // namespace LuxEngine
