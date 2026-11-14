#pragma once
#include <LuxEditor/Build/ProjectBuild.hpp>
#include <LuxEditor/Project/EditorProject.hpp>
#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Scene/EditorSnapshot.hpp>
#include <LuxEditor/Scene/SelectionTransform.hpp>
#include <LuxEditor/Viewport/EditorTool.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>
#include <LuxEngine/Scripting/ScriptRuntime.hpp>
#include <LuxUI/Framework.hpp>
#include <LuxUI/Panels.hpp>
#include <LuxUI/Widgets.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <vector>

namespace LuxEngine {
struct TransformComponent;
class ModelAsset;
} // namespace LuxEngine

namespace LuxEditor {
class SceneViewport2D;
class SceneViewport3D;
class GameViewport;
class DebugPanel;
class SceneEditCommand;
struct ModelingMesh;
struct ModelingSelection;

class EditorWindow final : public LuxUI::MainWindow {
public:
    explicit EditorWindow(LuxUI::Widget* parent = nullptr);
    ~EditorWindow() override;

    EditorDocument& GetDocument() noexcept;
    bool RegisterTool(LuxUI::Command command);
    bool LoadProjectToolsFromFile(const std::filesystem::path& path, std::string& error);
    bool ImportAssetFiles(const std::vector<std::filesystem::path>& files, std::string& error);

    const EditorDocument& GetDocument() const noexcept;
    LuxEngine::Scene* GetRuntimeScene() noexcept;
    const LuxEngine::Scene* GetRuntimeScene() const noexcept;

    const LuxUI::String& LastLogMessage() const noexcept;

    void ExecuteRecordedEdit(const LuxUI::String& name, const std::function<void()>& edit);

    EditorDocument::TabId NewSceneDocument();
    bool OpenSceneDocument(const std::filesystem::path& path, bool asynchronous = false);
    bool SwitchSceneDocument(EditorDocument::TabId id, bool animate = true);
    bool CloseSceneDocument(EditorDocument::TabId id, bool discardDirty = false);
    bool MoveSelectionToScene(EditorDocument::TabId destination);

    size_t PendingSceneLoadCount() const noexcept;

private:
    bool eventFilter(LuxUI::Object*, LuxUI::Event*) override;
    void BuildPhysics3DDetails(LuxUI::VBoxLayout* layout);
    void RefreshPhysics3DDetails();
    void BuildDevelopmentWorkspace();
    void BuildModelingWorkspace();
    void RefreshModelingWorkspace();
    void CommitModelingEdit(const ModelingMesh& mesh, const ModelingSelection& selection);
    void ExtrudeModelingSelection();
    void MergeModelingSelection();
    void BakeModelingSelection();
    void PollDevelopmentWorkspace();
    void LoadProjectTools();
    void SelectPlayAssembly();
    void BuildAssetBrowserCommands();
    void ImportAssetsDialog();
    void CreateAssetFolder();
    void BuildMenus();
    void BuildSceneMenus();
    void RefreshSceneTabs();
    void ResetSceneWorkspace();
    void PollSceneLoads();
    void LoadSceneAsync(const std::filesystem::path& path, bool preload);
    void DuplicateScene();
    void DeleteSceneFile();
    void SetStartupScene();
    void EditSceneSettings();
    void AddSceneReference(LuxEngine::SceneReferenceMode mode);
    void RefreshSceneReferences();
    void RelinkSceneReference();
    void ConfigureSceneStreaming();
    void TickSceneStreaming();
    void GroupSelection();
    void UngroupSelection();
    void ReparentSelection(const std::vector<LuxEngine::uint32>& children,
                           LuxEngine::uint32 parent);
    void ChooseMoveDestination();
    bool SaveAllScenes(bool currentProjectOnly = false);
    bool RequestCloseScene(EditorDocument::TabId id);
    void RememberSceneBaseline(bool clean = true);
    void UpdateSceneDirty(EditorDocument::TabId id, const EditorSnapshot& snapshot);
    EditorSnapshot SnapshotScene(EditorDocument::TabId id);
    void RestoreSceneAt(EditorDocument::TabId id, const EditorSnapshot& snapshot,
                        std::optional<LuxEngine::uint32> selected, bool activate = true);
    void InvalidateSceneHistory(EditorDocument::TabId id);
    void PushSceneCommand(SceneEditCommand* command);
    void AnimateSceneTransition(const LuxUI::Pixmap& previous);
    void BuildLayout();
    void BuildWorkspaceChrome();
    void SetWorkspace(bool animation);
    void ToggleDrawer(size_t index);
    void RefreshAssetFolders();
    void FilterDetails(const LuxUI::String& text);
    void BuildAnimationPanel();
    void BuildModelAnimationWorkspace(LuxUI::VBoxLayout* workspace);
    void RefreshModelAnimationWorkspace();
    void SeekModelAnimationTime(double time);
    void SetModelAnimationMode(bool enabled);
    void StopAnimationPlayback();
    void RefreshAnimationWorkspace();
    void SetAnimationTool(EditorTool tool);
    void OnAnimationKeyChanged(const LuxUI::Widget* field);
    void MoveAnimationKey(int index, float time);
    void ApplyTheme();
    void RestoreWorkspaceLayout();
    void SaveWorkspaceLayout();
    void ResetWorkspaceLayout();
    void OpenCommandPalette();
    void ConfigureShortcuts();
    void ExportEditorSettings();
    void ImportEditorSettings();
    void RefreshAll();
    void RefreshHierarchy();
    void RefreshInspector();
    void RefreshProjectExplorer();
    void BuildPrefabMenus(LuxUI::Menu* parent);
    void SaveSelectedPrefab();
    void AddPrefabAsset(const std::filesystem::path& path);
    void EditSelectedPrefab(int operation);
    void UpdateChrome();
    void UpdateRecentMenu();
    void ApplyProjectPreferences();
    enum class SceneOpenResult { Opened, Recovered, Cancelled, Failed };
    SceneOpenResult LoadSceneDocument(const std::filesystem::path& file,
                                      const EditorProject* recoveryProject,
                                      EditorDocument* destination = nullptr);
    bool OpenProjectScene(const std::filesystem::path& file);
    void RecoverAutosaveIfNeeded();
    void AutoSaveScene();
    void NewProject();
    void OpenProjectDialog();
    void OpenProjectFile(const std::filesystem::path& file,
                         const std::filesystem::path& preferredScene = {});
    void EditProjectSettings();
    void CloneCurrentProject();
    void ImportProjectDialog();
    void ExportCurrentProject();
    void BackupCurrentProject();
    void RestoreCurrentProject();
    void RescanProjectResources();
    bool ConfirmDiscardChanges();
    bool SaveScene(bool choosePath);
    void AddEntity();
    void AddMeshEntity();
    void AddBasicModelMenu(LuxUI::Menu* menu);
    void AddBasicModel(const std::string& id);
    void AddModelAsset(const std::filesystem::path& path);
    void ChooseModelAsset();
    void AddCameraEntity(bool perspective = true);
    void BuildCameraDetails(LuxUI::VBoxLayout* layout);
    void RefreshCameraDetails();
    void OnCameraChanged(const LuxUI::Widget* field);
    void AlignCameraToSceneView();
    void ViewThroughSelectedCamera();
    void AddTilemap();
    void PaintTile(LuxEngine::uint32 id, LuxEngine::uint32 x, LuxEngine::uint32 y, uint8_t tile);
    void DeleteEntity();
    void SelectEntity(std::optional<LuxEngine::uint32> id, bool syncHierarchy = true);
    void OnNameChanged();
    void OnTransformChanged(int row, int axis);
    void OnSpriteChanged();
    void OnColorChanged(int channel);
    void OnMeshChanged(const LuxUI::Widget* field);
    void OnTilemapChanged(const LuxUI::Widget* field);
    void OnPhysicsChanged(const LuxUI::Widget* field);
    void OnAnimationChanged(const LuxUI::Widget* field);
    void AddKeyframe();
    void RemoveKeyframe();
    void ScrubAnimation(int value);
    void SeekAnimationTime(double time);
    void EndTimelineEdit();
    void StartPreview();
    void StopPreview();
    void TogglePreviewPause();
    void StepPreview();
    void TickPreview();
    // Saves the current scene and runs it in a separate LuxPlayer window.
    void RunSceneInWindow();
    void StopSceneWindow();
    // Builds the open project with CMake (Debug/Release), optionally running or packaging it.
    enum class BuildFollowUp { None, Run, Package };
    void StartBuild(BuildConfiguration configuration, bool clean, BuildFollowUp followUp);
    void RunNextBuildStep();
    void FinishBuild(bool success);
    void CancelBuild();
    void HandleBuildOutput(const LuxUI::ByteArray& data);
    void ChooseEnginePackage();
    void PackageBuild();
    void ApplyViewportTransform(LuxEngine::uint32 id, const LuxEngine::TransformComponent& value);
    void BeginViewportEdit();
    void CancelViewportEdit();
    void SyncHierarchySelection();
    void RefreshSelection(bool hierarchy = true);
    void EndViewportEdit();
    void SetTool(EditorTool tool);
    void Log(const LuxUI::String& message, bool error = false);
    void ShowError(const LuxUI::String& title, const LuxUI::String& message);
    void RunWithProgress(const LuxUI::String& message, const std::function<void()>& task);
    void CopyEntity();
    void PasteEntity();
    void RecordEdit(const LuxUI::String& name, const std::function<void()>& edit);
    void BuildScriptDetails(LuxUI::VBoxLayout* layout);
    void RefreshScriptDetails();
    void InspectScriptFields();
    void SetScriptField(const std::string& name, const LuxEngine::ScriptFieldValue& value);
    EditorSnapshot SnapshotScene();
    void RestoreScene(const EditorSnapshot& snapshot, std::optional<LuxEngine::uint32> selected);
    void closeEvent(LuxUI::CloseEvent* event) override;
    void resizeEvent(LuxUI::ResizeEvent* event) override;
    void dragEnterEvent(LuxUI::DragEnterEvent* event) override;
    void dropEvent(LuxUI::DropEvent* event) override;

    LuxUI::CheckBox* body3DEnabled_ = nullptr;
    LuxUI::CheckBox* collider3DEnabled_ = nullptr;
    LuxUI::ComboBox* body3DType_ = nullptr;
    LuxUI::CheckBox* trigger3D_ = nullptr;
    LuxUI::DoubleSpinBox* body3DMass_ = nullptr;
    LuxUI::DoubleSpinBox* body3DGravity_ = nullptr;
    std::array<LuxUI::DoubleSpinBox*, 3> collider3DSize_{};
    std::array<LuxUI::DoubleSpinBox*, 3> collider3DOffset_{};
    LuxUI::CheckBox* scriptAttached_ = nullptr;
    LuxUI::CheckBox* scriptEnabled_ = nullptr;
    LuxUI::LineEdit* scriptType_ = nullptr;
    LuxUI::Widget* scriptFieldsPanel_ = nullptr;
    LuxUI::FormLayout* scriptFieldsForm_ = nullptr;

    EditorDocument document_;
    std::optional<EditorProject> project_;
    RecentProjects recentProjects_;
    LuxUI::Timer autosaveTimer_;
    LuxUI::Timer previewTimer_;
    LuxUI::Timer sceneLoadTimer_;
    LuxUI::Timer sceneStreamingTimer_;
    LuxEngine::SceneManager sceneManager_;

    struct PendingSceneLoad {
        std::filesystem::path path;
        std::future<LuxEngine::SceneLoadResult> result;
        std::optional<EditorProject> project;
        uint64_t generation = 0;
        EditorDocument::TabId origin = 0;
        bool preload = false;
    };

    std::vector<PendingSceneLoad> pendingSceneLoads_;
    uint64_t sceneLoadGeneration_ = 0;
    bool refreshingSceneTabs_ = false;
    bool sceneTransitions_ = true;
    LuxUI::TabBar* sceneTabs_ = nullptr;
    LuxUI::ProgressBar* sceneLoadProgress_ = nullptr;
    std::map<EditorDocument::TabId, EditorSnapshot> cleanScenes_;
    std::map<EditorDocument::TabId, std::optional<EditorProject>> sceneProjects_;
    const std::chrono::steady_clock::time_point openedAt_ = std::chrono::steady_clock::now();
    std::map<EditorDocument::TabId, std::chrono::steady_clock::time_point> autosavedAt_;
    LuxUI::UndoStack* undo_ = nullptr;
    LuxUI::StackedWidget* workspaceStack_ = nullptr;
    LuxUI::TabBar* workspaceTabs_ = nullptr;
    LuxUI::Label* sceneStatus_ = nullptr;
    LuxUI::TreeWidget* assetFolders_ = nullptr;
    LuxUI::Label* assetBreadcrumb_ = nullptr;
    LuxUI::Label* assetCount_ = nullptr;
    LuxUI::LineEdit* detailsSearch_ = nullptr;
    std::filesystem::path assetFolder_;
    std::filesystem::path assetProjectRoot_;
    LuxUI::ByteArray levelLayout_;
    LuxUI::TabWidget* viewportTabs_ = nullptr;
    SceneViewport2D* canvas_ = nullptr;
    SceneViewport2D* overviewCanvas_ = nullptr;
    SceneViewport3D* canvas3D_ = nullptr;
    SceneViewport3D* modelAnimationPreview_ = nullptr;
    LuxUI::DockWidget* modelingDock_ = nullptr;
    LuxUI::ComboBox* modelingMode_ = nullptr;
    std::shared_ptr<ModelingMesh> modelingMesh_;
    std::shared_ptr<LuxEngine::ModelAsset> modelingSourceAsset_;
    LuxEngine::uint32 modelingEntity_ = 0;
    EditorDocument::TabId modelingTab_ = 0;
    std::weak_ptr<int> modelingLifetime_;
    std::string modelingSourcePath_;
    int modelingSourcePart_ = -1;
    bool refreshingModeling_ = false;
    std::shared_ptr<LuxEngine::ModelAsset> modelAnimationAsset_;
    GameViewport* gameCanvas_ = nullptr;
    LuxUI::TreeWidget* hierarchy_ = nullptr;
    LuxUI::TreeWidget* groupTree_ = nullptr;
    int groupFilter_ = 0;
    LuxUI::LineEdit* hierarchySearch_ = nullptr;
    LuxUI::ListWidget* projectFiles_ = nullptr;
    LuxUI::Label* assetPreview_ = nullptr;
    LuxUI::Label* assetDetails_ = nullptr;
    LuxUI::Widget* assetPreviewPanel_ = nullptr;
    LuxUI::ComboBox* assetFilter_ = nullptr;
    LuxUI::LineEdit* projectSearch_ = nullptr;
    LuxUI::Label* projectLabel_ = nullptr;
    LuxUI::Label* toolHint_ = nullptr;
    LuxUI::Label* selectionLabel_ = nullptr;
    LuxUI::Widget* inspectorBody_ = nullptr;
    LuxUI::Widget* sceneReferencePanel_ = nullptr;
    LuxUI::LineEdit* sceneReferencePath_ = nullptr;
    LuxUI::Label* sceneReferenceState_ = nullptr;
    LuxUI::LineEdit* name_ = nullptr;
    LuxUI::CheckBox* spriteEnabled_ = nullptr;
    LuxUI::CheckBox* meshEnabled_ = nullptr;
    LuxUI::CheckBox* cameraEnabled_ = nullptr;
    LuxUI::CheckBox* cameraPrimary_ = nullptr;
    LuxUI::ComboBox* cameraProjection_ = nullptr;
    LuxUI::DoubleSpinBox* cameraFov_ = nullptr;
    LuxUI::DoubleSpinBox* cameraOrthoSize_ = nullptr;
    LuxUI::DoubleSpinBox* cameraNearClip_ = nullptr;
    LuxUI::DoubleSpinBox* cameraFarClip_ = nullptr;
    LuxUI::LineEdit* meshPath_ = nullptr;
    std::vector<LuxUI::Action*> basicModelActions_;
    LuxUI::LineEdit* baseColorTexturePath_ = nullptr;
    LuxUI::CheckBox* tilemapEnabled_ = nullptr;
    LuxUI::SpinBox* tilemapWidth_ = nullptr;
    LuxUI::SpinBox* tilemapHeight_ = nullptr;
    LuxUI::DoubleSpinBox* tilemapSize_ = nullptr;
    LuxUI::ComboBox* tilePalette_ = nullptr;
    LuxUI::CheckBox* bodyEnabled_ = nullptr;
    LuxUI::ComboBox* bodyType_ = nullptr;
    LuxUI::DoubleSpinBox* gravityScale_ = nullptr;
    LuxUI::CheckBox* colliderEnabled_ = nullptr;
    LuxUI::CheckBox* triggerEnabled_ = nullptr;
    std::array<LuxUI::DoubleSpinBox*, 2> colliderSize_{};
    std::array<LuxUI::DoubleSpinBox*, 2> colliderOffset_{};
    LuxUI::DockWidget* animationDock_ = nullptr;
    LuxUI::Widget* animationPanel_ = nullptr;
    LuxUI::Label* animationTarget_ = nullptr;
    LuxUI::LineEdit* animationName_ = nullptr;
    LuxUI::DoubleSpinBox* animationTime_ = nullptr;
    LuxUI::SpinBox* animationFps_ = nullptr;
    LuxUI::Timer animationTimer_;
    LuxUI::CheckBox* animationEnabled_ = nullptr;
    LuxUI::CheckBox* animationLoop_ = nullptr;
    LuxUI::DoubleSpinBox* animationDuration_ = nullptr;
    LuxUI::Slider* animationTimeline_ = nullptr;
    LuxUI::DoubleSpinBox* animationKeyTime_ = nullptr;
    std::array<std::array<LuxUI::DoubleSpinBox*, 3>, 3> animationKeyValues_{};
    SceneViewport2D* animationPreview2D_ = nullptr;
    SceneViewport3D* animationPreview3D_ = nullptr;
    LuxUI::ListWidget* keyframeList_ = nullptr;
    uint8_t tileId_ = 1;
    LuxUI::DoubleSpinBox* metallic_ = nullptr;
    LuxUI::DoubleSpinBox* roughness_ = nullptr;
    LuxUI::ComboBox* materialAlphaMode_ = nullptr;
    LuxUI::DoubleSpinBox* materialAlphaCutoff_ = nullptr;
    LuxUI::CheckBox* materialDoubleSided_ = nullptr;
    std::array<LuxUI::LineEdit*, 4> materialMapPaths_{};
    LuxUI::DoubleSpinBox* materialNormalScale_ = nullptr;
    LuxUI::DoubleSpinBox* materialOcclusionStrength_ = nullptr;
    LuxUI::CheckBox* materialMipmaps_ = nullptr;
    LuxUI::ComboBox* materialAddressMode_ = nullptr;
    LuxUI::CheckBox* materialAnisotropic_ = nullptr;
    std::array<std::array<LuxUI::DoubleSpinBox*, 3>, 3> transform_{};
    std::array<LuxUI::DoubleSpinBox*, 4> color_{};
    LuxUI::PlainTextEdit* consoleOutput_ = nullptr;
    LuxUI::PlainTextEdit* issuesOutput_ = nullptr;
    DebugPanel* debugPanel_ = nullptr;
    std::array<LuxUI::DockWidget*, 8> docks_{};
    LuxUI::Action* previewStartAction_ = nullptr;
    LuxUI::Action* previewPauseAction_ = nullptr;
    LuxUI::Action* previewStepAction_ = nullptr;
    LuxUI::Action* previewStopAction_ = nullptr;
    LuxUI::Action* undoAction_ = nullptr;
    LuxUI::Action* redoAction_ = nullptr;
    LuxUI::Action* colliderAction_ = nullptr;
    LuxUI::Action* breakOnCollisionAction_ = nullptr;
    LuxUI::ByteArray defaultLayout_;
    std::array<LuxUI::AbstractButton*, 6> toolButtons_{};
    std::array<LuxUI::Action*, 6> toolActions_{};
    LuxUI::Action* projectSettingsAction_ = nullptr;
    LuxUI::Action* cloneAction_ = nullptr;
    LuxUI::Action* exportAction_ = nullptr;
    LuxUI::Action* backupAction_ = nullptr;
    LuxUI::Action* restoreAction_ = nullptr;
    LuxUI::Action* rescanAction_ = nullptr;
    LuxUI::Menu* recentMenu_ = nullptr;
    std::vector<LuxUI::Action*> shortcutActions_;
    std::vector<LuxEngine::uint32> hierarchyIds_;
    std::vector<std::filesystem::path> projectFilePaths_;
    bool refreshingHierarchy_ = false;
    bool refreshingInspector_ = false;
    bool restoringHistory_ = false;
    EditorTool tool_ = EditorTool::Select;
    EditorSnapshot viewportEditBefore_;
    EditorDocument::TabId viewportEditTab_ = 0;
    bool viewportEditWasDirty_ = false;
    std::weak_ptr<int> viewportEditLifetime_;
    SelectionTransformSession selectionTransform_;
    bool transformRejected_ = false;
    EditorSnapshot timelineBefore_;
    std::optional<LuxEngine::uint32> viewportEditSelected_;
    uint64_t previewFrame_ = 0;
    bool previewActive_ = false;
    std::unique_ptr<LuxEngine::Scene> runtimeScene_;
    LuxEngine::ScriptRuntime previewScripts_;
    std::filesystem::path playAssembly_;
    std::filesystem::path playAssemblyProject_;
    std::vector<std::string> projectToolIds_;
    LuxUI::Action* pauseOnErrorAction_ = nullptr;
    std::unique_ptr<LuxUI::ConsolePanel> developmentConsole_;
    std::unique_ptr<LuxUI::ProfilerPanel> profiler_;
    LuxUI::CommandRegistry commands_;
    LuxUI::Menu* customToolsMenu_ = nullptr;
    LuxUI::DockWidget* profilerDock_ = nullptr;
    LuxUI::Timer developmentTimer_;
    uint64_t lastEngineLog_ = 0;
    double lastUpdateMs_ = 0;
    bool previewPaused_ = false;
    bool debugDockHiddenForSize_ = false;
    LuxUI::String theme_ = LUX_UI_TEXT("dark");
    double uiScale_ = 1.0;
    LuxUI::Process* playerProcess_ = nullptr;
    LuxUI::Process* buildProcess_ = nullptr;
    LuxUI::ProgressBar* buildProgress_ = nullptr;
    std::optional<BuildPlan> buildPlan_;
    size_t buildStep_ = 0;
    BuildFollowUp buildFollowUp_ = BuildFollowUp::None;
    std::vector<BuildDiagnostic> buildDiagnostics_;
    LuxUI::ByteArray buildLineBuffer_;
    LuxUI::String playerScenePath_;
    LuxUI::String lastLogMessage_;
};
} // namespace LuxEditor
