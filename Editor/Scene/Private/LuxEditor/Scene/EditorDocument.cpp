#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/SceneAuthoringIO.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

namespace LuxEditor {
namespace {
std::string AnchoredPath(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos)
        return {};
    const auto input = PathFromUtf8(path);
    if (input.is_absolute())
        return path;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(input, error);
    return error ? path : PathUtf8(absolute);
}
std::string SourcePath(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos)
        return {};
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(PathFromUtf8(path), error);
    return error ? path : GenericPathUtf8(canonical);
}
bool SameFile(const std::string& first, const std::string& second) {
    if (first.empty() || second.empty() || first.find('\0') != std::string::npos ||
        second.find('\0') != std::string::npos)
        return false;
    const auto a = PathFromUtf8(first), b = PathFromUtf8(second);
    std::error_code error;
    if (std::filesystem::equivalent(a, b, error) && !error)
        return true;
    error.clear();
    const auto normalizedA = std::filesystem::weakly_canonical(a, error);
    if (error)
        return false;
    const auto normalizedB = std::filesystem::weakly_canonical(b, error);
    return !error && normalizedA == normalizedB;
}

std::shared_ptr<LuxEngine::Scene> ReadScene(const std::string& path, std::string& error,
                                            std::string& warning) {
    error.clear();
    warning.clear();
    if (path.empty() || path.find('\0') != std::string::npos) {
        error = "씬 파일 경로가 비어 있습니다.";
        return {};
    }
    if (auto composed = LuxEngine::SceneManager().Load(PathFromUtf8(path), error))
        return composed;
    std::string authoredError;
    auto authored = ReadAuthoredScene(PathFromUtf8(path), authoredError);
    if (!authored)
        return {};
    warning = "참조 씬을 불러오지 못해 파일에 저장된 편집 내용을 열었습니다: " + error;
    error.clear();
    return authored;
}
} // namespace

EditorDocument::EditorDocument() {
    NewTab();
}

EditorDocument::TabId EditorDocument::NextId() {
    if (nextId_ == std::numeric_limits<TabId>::max())
        throw std::overflow_error("씬 문서 ID를 더 생성할 수 없습니다.");
    return nextId_++;
}

EditorDocument::TabState* EditorDocument::Find(TabId tab) noexcept {
    return const_cast<TabState*>(std::as_const(*this).Find(tab));
}

const EditorDocument::TabState* EditorDocument::Find(TabId tab) const noexcept {
    const auto found = std::find_if(tabs_.begin(), tabs_.end(),
                                    [tab](const TabState& state) { return state.id == tab; });
    return found == tabs_.end() ? nullptr : &*found;
}

bool EditorDocument::ConflictsWithOpenTab(const std::string& path, TabId except) const {
    return std::any_of(tabs_.begin(), tabs_.end(), [&](const TabState& tab) {
        return tab.id != except && SameFile(path, tab.path);
    });
}

void EditorDocument::NewScene() {
    auto replacement = std::make_shared<LuxEngine::Scene>();
    auto& active = Active();
    active.scene = std::move(replacement);
    active.selectedId.reset();
    active.path.clear();
    active.dirty = false;
    active.loadWarning.clear();
    error_.clear();
}

bool EditorDocument::Open(const std::string& path) {
    error_.clear();
    if (ConflictsWithOpenTab(path, GetActiveTabId())) {
        error_ = "다른 탭에서 열려 있는 씬입니다.";
        return false;
    }
    std::string nextPath = AnchoredPath(path);
    std::string warning;
    auto replacement = ReadScene(nextPath, error_, warning);
    if (!replacement)
        return false;
    auto& active = Active();
    active.scene = std::move(replacement);
    active.selectedId.reset();
    active.path.swap(nextPath);
    active.dirty = false;
    active.loadWarning = std::move(warning);
    return true;
}

bool EditorDocument::Recover(const std::string& originalPath, const std::string& autosavePath) {
    error_.clear();
    if (originalPath.empty() || originalPath.find('\0') != std::string::npos ||
        ConflictsWithOpenTab(originalPath, GetActiveTabId())) {
        error_ = "복구할 씬 경로가 올바르지 않거나 다른 탭에서 열려 있습니다.";
        return false;
    }
    std::string warning;
    auto replacement = ReadScene(autosavePath, error_, warning);
    if (!replacement)
        return false;
    std::string nextPath = AnchoredPath(originalPath);
    replacement->SetSourcePath(SourcePath(nextPath));
    auto& active = Active();
    active.scene = std::move(replacement);
    active.selectedId.reset();
    active.path.swap(nextPath);
    active.dirty = true;
    active.loadWarning = std::move(warning);
    return true;
}

bool EditorDocument::Save() {
    return Save(GetActiveTabId());
}

bool EditorDocument::Save(TabId tab) {
    error_.clear();
    auto* state = Find(tab);
    if (!state || state->path.empty()) {
        error_ = "씬을 저장할 경로를 먼저 지정해 주세요.";
        return false;
    }
    if (ConflictsWithOpenTab(state->path, tab)) {
        error_ = "다른 탭에서 사용 중인 씬 경로입니다.";
        return false;
    }
    auto sourcePath = SourcePath(state->path);
    if (!SaveAuthoredScene(*state->scene, PathFromUtf8(state->path), PathFromUtf8(state->path),
                           error_))
        return false;
    state->scene->SetSourcePath(std::move(sourcePath));
    state->dirty = false;
    return true;
}

bool EditorDocument::SaveAs(const std::string& path) {
    error_.clear();
    if (path.empty() || ConflictsWithOpenTab(path, GetActiveTabId())) {
        error_ = "다른 탭에서 사용하지 않는 씬 저장 경로를 지정해 주세요.";
        return false;
    }
    std::string nextPath = AnchoredPath(path);
    auto sourcePath = SourcePath(nextPath);
    auto& active = Active();
    if (!SaveAuthoredScene(*active.scene, PathFromUtf8(nextPath), PathFromUtf8(active.path),
                           error_))
        return false;
    active.path.swap(nextPath);
    active.scene->SetSourcePath(std::move(sourcePath));
    active.dirty = false;
    return true;
}

bool EditorDocument::ReplaceScene(const std::string& serializedPath,
                                  std::optional<LuxEngine::uint32> selectedId) {
    std::string warning;
    auto replacement = ReadScene(serializedPath, error_, warning);
    if (!replacement)
        return false;
    auto& active = Active();
    replacement->SetSourcePath(active.scene->GetSourcePath());
    active.scene = std::move(replacement);
    active.selectedId =
        selectedId && active.scene->IsAlive(*selectedId) ? selectedId : std::nullopt;
    active.dirty = true;
    active.loadWarning = std::move(warning);
    return true;
}

bool EditorDocument::ReplaceSceneFromData(const std::string& serializedData,
                                          std::optional<LuxEngine::uint32> selectedId) {
    return ReplaceSceneFromData(GetActiveTabId(), serializedData, selectedId);
}

bool EditorDocument::ReplaceSceneFromData(TabId tab, const std::string& serializedData,
                                          std::optional<LuxEngine::uint32> selectedId) {
    auto* state = Find(tab);
    if (!state)
        return false;
    auto replacement = std::make_shared<LuxEngine::Scene>();
    if (!replacement->DeserializeFromString(serializedData))
        return false;
    replacement->SetSourcePath(state->scene->GetSourcePath());
    state->scene = std::move(replacement);
    state->selectedId =
        selectedId && state->scene->IsAlive(*selectedId) ? selectedId : std::nullopt;
    state->dirty = true;
    return true;
}

EditorDocument::TabId EditorDocument::NewTab() {
    auto scene = std::make_shared<LuxEngine::Scene>();
    const auto id = NextId();
    tabs_.push_back({id, std::move(scene), {}, {}, false, {}});
    activeIndex_ = tabs_.size() - 1;
    error_.clear();
    return id;
}

EditorDocument::TabId EditorDocument::OpenTab(const std::string& path) {
    if (const auto existing = FindTabByPath(path)) {
        ActivateTab(existing);
        error_.clear();
        return existing;
    }
    std::string warning;
    auto scene = ReadScene(path, error_, warning);
    if (!scene)
        return 0;
    return AdoptTab(path, std::move(scene), false, std::move(warning));
}

EditorDocument::TabId EditorDocument::AdoptTab(const std::string& path,
                                               std::shared_ptr<LuxEngine::Scene> scene, bool dirty,
                                               std::string warning) {
    if (!scene || path.find('\0') != std::string::npos) {
        error_ = "불러온 씬 또는 파일 경로가 올바르지 않습니다.";
        return 0;
    }
    if (const auto existing = FindTabByPath(path)) {
        ActivateTab(existing);
        error_.clear();
        return existing;
    }
    if (std::any_of(tabs_.begin(), tabs_.end(),
                    [&](const TabState& state) { return state.scene == scene; })) {
        error_ = "다른 탭에서 사용 중인 씬 인스턴스입니다.";
        return 0;
    }
    auto nextPath = AnchoredPath(path);
    if (!nextPath.empty())
        scene->SetSourcePath(SourcePath(nextPath));
    const auto id = NextId();
    tabs_.push_back({id, std::move(scene), {}, std::move(nextPath), dirty, std::move(warning)});
    activeIndex_ = tabs_.size() - 1;
    error_.clear();
    return id;
}

EditorDocument::TabId EditorDocument::DuplicateTab(TabId tab, const std::string& path) {
    error_.clear();
    auto* source = Find(tab);
    std::error_code fileError;
    bool targetAvailable = false;
    if (!path.empty()) {
        const auto status = std::filesystem::symlink_status(PathFromUtf8(path), fileError);
        targetAvailable = status.type() == std::filesystem::file_type::not_found &&
                          (!fileError || fileError == std::errc::no_such_file_or_directory);
    }
    if (!source || !targetAvailable || FindTabByPath(path)) {
        error_ = "씬 복제에는 사용하지 않는 새 파일 경로가 필요합니다.";
        return 0;
    }
    std::string data;
    auto scene = std::make_shared<LuxEngine::Scene>();
    if (!source->scene->SerializeToString(data) || !scene->DeserializeFromString(data) ||
        !SaveAuthoredScene(*scene, PathFromUtf8(path), PathFromUtf8(source->path), error_)) {
        if (error_.empty())
            error_ = "씬을 복제할 수 없습니다.";
        return 0;
    }
    return AdoptTab(path, std::move(scene), false, source->loadWarning);
}

bool EditorDocument::ActivateTab(TabId tab) noexcept {
    for (size_t index = 0; index < tabs_.size(); ++index)
        if (tabs_[index].id == tab) {
            activeIndex_ = index;
            return true;
        }
    return false;
}

bool EditorDocument::CloseTab(TabId tab, bool discardDirty) {
    error_.clear();
    const auto* state = Find(tab);
    if (!state || (state->dirty && !discardDirty)) {
        error_ = state ? "변경 사항이 있는 씬을 저장하거나 변경 취소를 확인해 주세요."
                       : "열려 있지 않은 씬입니다.";
        return false;
    }
    if (tabs_.size() == 1) {
        auto scene = std::make_shared<LuxEngine::Scene>();
        const auto replacementId = NextId();
        tabs_.front() = {replacementId, std::move(scene), {}, {}, false, {}};
        activeIndex_ = 0;
        return true;
    }
    const auto activeId = GetActiveTabId();
    const auto index = static_cast<size_t>(state - tabs_.data());
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    if (activeId == tab)
        activeIndex_ = std::min(index, tabs_.size() - 1);
    else
        ActivateTab(activeId);
    return true;
}

void EditorDocument::ResetTabs() {
    auto scene = std::make_shared<LuxEngine::Scene>();
    const auto id = NextId();
    std::vector<TabState> next;
    next.push_back({id, std::move(scene), {}, {}, false, {}});
    tabs_.swap(next);
    activeIndex_ = 0;
    error_.clear();
}

std::vector<EditorDocument::TabInfo> EditorDocument::ListTabs() const {
    std::vector<TabInfo> result;
    result.reserve(tabs_.size());
    for (const auto& tab : tabs_)
        result.push_back({tab.id, tab.path, tab.dirty, GetSelectedId(tab.id)});
    return result;
}

EditorDocument::TabId EditorDocument::GetActiveTabId() const noexcept {
    return Active().id;
}

EditorDocument::TabId EditorDocument::FindTabByPath(const std::string& path) const {
    if (path.empty() || path.find('\0') != std::string::npos)
        return 0;
    const auto requested = PathFromUtf8(AnchoredPath(path));
    for (const auto& tab : tabs_)
        if (!tab.path.empty() && requested == PathFromUtf8(AnchoredPath(tab.path)))
            return tab.id;
    for (const auto& tab : tabs_)
        if (SameFile(path, tab.path))
            return tab.id;
    return 0;
}

LuxEngine::Scene* EditorDocument::GetScene(TabId tab) noexcept {
    const auto* state = Find(tab);
    return state ? state->scene.get() : nullptr;
}

const LuxEngine::Scene* EditorDocument::GetScene(TabId tab) const noexcept {
    const auto* state = Find(tab);
    return state ? state->scene.get() : nullptr;
}

std::string EditorDocument::GetPath(TabId tab) const {
    const auto* state = Find(tab);
    return state ? state->path : std::string{};
}

const std::string& EditorDocument::GetLoadWarning(TabId tab) const noexcept {
    static const std::string empty;
    const auto* state = Find(tab);
    return state ? state->loadWarning : empty;
}

bool EditorDocument::IsDirty(TabId tab) const noexcept {
    const auto* state = Find(tab);
    return state && state->dirty;
}

bool EditorDocument::SetDirty(TabId tab, bool dirty) noexcept {
    auto* state = Find(tab);
    if (!state)
        return false;
    state->dirty = dirty;
    return true;
}

std::optional<LuxEngine::uint32> EditorDocument::GetSelectedId(TabId tab) const noexcept {
    const auto* state = Find(tab);
    return state && state->selectedId && state->scene->IsAlive(*state->selectedId)
               ? state->selectedId
               : std::nullopt;
}

void EditorDocument::Select(TabId tab, std::optional<LuxEngine::uint32> id) {
    if (auto* state = Find(tab))
        state->selectedId = id && state->scene->IsAlive(*id) ? id : std::nullopt;
}

void EditorDocument::Select(std::optional<LuxEngine::uint32> id) {
    Select(GetActiveTabId(), id);
}

LuxEngine::Entity EditorDocument::GetSelected() const {
    const auto selectedId = GetSelectedId(GetActiveTabId());
    return selectedId ? LuxEngine::Entity{*selectedId, Active().scene.get()} : LuxEngine::Entity{};
}

LuxEngine::Entity EditorDocument::CreateEntity(const std::string& name) {
    auto& active = Active();
    auto entity = active.scene->CreateEntity(name);
    active.selectedId = entity.GetId();
    active.dirty = true;
    return entity;
}

bool EditorDocument::DeleteSelected() {
    const auto selected = GetSelected();
    if (!selected)
        return false;
    auto& active = Active();
    active.scene->DestroyEntity(selected);
    active.selectedId.reset();
    active.dirty = true;
    return true;
}
} // namespace LuxEditor
