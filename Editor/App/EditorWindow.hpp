#pragma once
#include "Build/ProjectBuild.hpp"
#include "Engine/Scene/SceneManager.hpp"
#include "Project/EditorProject.hpp"
#include "Scene/EditorDocument.hpp"
#include "Scene/EditorSnapshot.hpp"
#include "Viewport/EditorTool.hpp"
#include <QMainWindow>
#include <QTimer>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <vector>

class QAction;
class QAbstractButton;
class QCheckBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QSpinBox;
class QResizeEvent;
class QComboBox;
class QTabWidget;
class QTabBar;
class QTreeWidget;
class QUndoStack;
class QWidget;
class QSlider;
class QPushButton;
class QProcess;
class QPixmap;
class QProgressBar;

namespace LuxEngine {
struct TransformComponent;
}

namespace LuxEditor {
class SceneViewport2D;
class SceneViewport3D;
class DebugPanel;
class SceneEditCommand;
class EditorWindow final : public QMainWindow {
public:
    explicit EditorWindow(QWidget* parent = nullptr);
    ~EditorWindow() override;

    EditorDocument& GetDocument() noexcept {
        return document_;
    }
    const EditorDocument& GetDocument() const noexcept {
        return document_;
    }
    const QString& LastLogMessage() const noexcept {
        return lastLogMessage_;
    }
    void ExecuteRecordedEdit(const QString& name, const std::function<void()>& edit) {
        RecordEdit(name, edit);
        RefreshAll();
    }
    EditorDocument::TabId NewSceneDocument();
    bool OpenSceneDocument(const std::filesystem::path& path, bool asynchronous = false);
    bool SwitchSceneDocument(EditorDocument::TabId id, bool animate = true);
    bool CloseSceneDocument(EditorDocument::TabId id, bool discardDirty = false);
    bool MoveSelectionToScene(EditorDocument::TabId destination);
    size_t PendingSceneLoadCount() const noexcept {
        return pendingSceneLoads_.size();
    }

private:
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
    void AnimateSceneTransition(const QPixmap& previous);
    void BuildLayout();
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
    void AddTilemap();
    void PaintTile(LuxEngine::uint32 id, LuxEngine::uint32 x, LuxEngine::uint32 y, uint8_t tile);
    void DeleteEntity();
    void SelectEntity(std::optional<LuxEngine::uint32> id, bool syncHierarchy = true);
    void OnNameChanged();
    void OnTransformChanged(int row, int axis);
    void OnSpriteChanged();
    void OnColorChanged(int channel);
    void OnMeshChanged(const QWidget* field);
    void OnTilemapChanged(const QWidget* field);
    void OnPhysicsChanged(const QWidget* field);
    void OnAnimationChanged(const QWidget* field);
    void AddKeyframe();
    void RemoveKeyframe();
    void ScrubAnimation(int value);
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
    void HandleBuildOutput(const QByteArray& data);
    void ChooseEnginePackage();
    void PackageBuild();
    void ApplyViewportTransform(LuxEngine::uint32 id, const LuxEngine::TransformComponent& value);
    void BeginViewportEdit();
    void EndViewportEdit();
    void SetTool(EditorTool tool);
    void Log(const QString& message, bool error = false);
    void ShowError(const QString& title, const QString& message);
    void RunWithProgress(const QString& message, const std::function<void()>& task);
    void CopyEntity();
    void PasteEntity();
    void RecordEdit(const QString& name, const std::function<void()>& edit);
    EditorSnapshot SnapshotScene();
    void RestoreScene(const EditorSnapshot& snapshot, std::optional<LuxEngine::uint32> selected);
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    EditorDocument document_;
    std::optional<EditorProject> project_;
    RecentProjects recentProjects_;
    QTimer autosaveTimer_;
    QTimer previewTimer_;
    QTimer sceneLoadTimer_;
    QTimer sceneStreamingTimer_;
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
    QTabBar* sceneTabs_ = nullptr;
    QProgressBar* sceneLoadProgress_ = nullptr;
    std::map<EditorDocument::TabId, EditorSnapshot> cleanScenes_;
    std::map<EditorDocument::TabId, std::optional<EditorProject>> sceneProjects_;
    const std::chrono::steady_clock::time_point openedAt_ = std::chrono::steady_clock::now();
    std::map<EditorDocument::TabId, std::chrono::steady_clock::time_point> autosavedAt_;
    QUndoStack* undo_ = nullptr;
    QTabWidget* viewportTabs_ = nullptr;
    SceneViewport2D* canvas_ = nullptr;
    SceneViewport2D* overviewCanvas_ = nullptr;
    SceneViewport3D* canvas3D_ = nullptr;
    QTreeWidget* hierarchy_ = nullptr;
    QTreeWidget* groupTree_ = nullptr;
    int groupFilter_ = 0;
    QLineEdit* hierarchySearch_ = nullptr;
    QListWidget* projectFiles_ = nullptr;
    QLabel* assetPreview_ = nullptr;
    QLabel* assetDetails_ = nullptr;
    QWidget* assetPreviewPanel_ = nullptr;
    QComboBox* assetFilter_ = nullptr;
    QLineEdit* projectSearch_ = nullptr;
    QLabel* projectLabel_ = nullptr;
    QLabel* toolHint_ = nullptr;
    QLabel* selectionLabel_ = nullptr;
    QWidget* inspectorBody_ = nullptr;
    QWidget* sceneReferencePanel_ = nullptr;
    QLineEdit* sceneReferencePath_ = nullptr;
    QLabel* sceneReferenceState_ = nullptr;
    QLineEdit* name_ = nullptr;
    QCheckBox* spriteEnabled_ = nullptr;
    QCheckBox* meshEnabled_ = nullptr;
    QCheckBox* tilemapEnabled_ = nullptr;
    QSpinBox* tilemapWidth_ = nullptr;
    QSpinBox* tilemapHeight_ = nullptr;
    QDoubleSpinBox* tilemapSize_ = nullptr;
    QComboBox* tilePalette_ = nullptr;
    QCheckBox* bodyEnabled_ = nullptr;
    QComboBox* bodyType_ = nullptr;
    QDoubleSpinBox* gravityScale_ = nullptr;
    QCheckBox* colliderEnabled_ = nullptr;
    QCheckBox* triggerEnabled_ = nullptr;
    std::array<QDoubleSpinBox*, 2> colliderSize_{};
    std::array<QDoubleSpinBox*, 2> colliderOffset_{};
    QCheckBox* animationEnabled_ = nullptr;
    QCheckBox* animationLoop_ = nullptr;
    QDoubleSpinBox* animationDuration_ = nullptr;
    QSlider* animationTimeline_ = nullptr;
    QListWidget* keyframeList_ = nullptr;
    uint8_t tileId_ = 1;
    QDoubleSpinBox* metallic_ = nullptr;
    QDoubleSpinBox* roughness_ = nullptr;
    std::array<std::array<QDoubleSpinBox*, 3>, 3> transform_{};
    std::array<QDoubleSpinBox*, 4> color_{};
    QPlainTextEdit* consoleOutput_ = nullptr;
    QPlainTextEdit* issuesOutput_ = nullptr;
    DebugPanel* debugPanel_ = nullptr;
    std::array<QDockWidget*, 8> docks_{};
    QAction* previewStartAction_ = nullptr;
    QAction* previewPauseAction_ = nullptr;
    QAction* previewStepAction_ = nullptr;
    QAction* previewStopAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* colliderAction_ = nullptr;
    QAction* breakOnCollisionAction_ = nullptr;
    QByteArray defaultLayout_;
    QAction* snapAction_ = nullptr;
    std::array<QAbstractButton*, 6> toolButtons_{};
    std::array<QAction*, 6> toolActions_{};
    QAction* projectSettingsAction_ = nullptr;
    QAction* cloneAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* backupAction_ = nullptr;
    QAction* restoreAction_ = nullptr;
    QAction* rescanAction_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    std::vector<QAction*> shortcutActions_;
    std::vector<LuxEngine::uint32> hierarchyIds_;
    std::vector<std::filesystem::path> projectFilePaths_;
    bool refreshingHierarchy_ = false;
    bool refreshingInspector_ = false;
    bool restoringHistory_ = false;
    EditorTool tool_ = EditorTool::Select;
    EditorSnapshot viewportEditBefore_;
    EditorSnapshot timelineBefore_;
    EditorSnapshot previewBefore_;
    std::optional<LuxEngine::uint32> viewportEditSelected_;
    std::optional<LuxEngine::uint32> previewSelected_;
    uint64_t previewFrame_ = 0;
    bool previewActive_ = false;
    bool previewPaused_ = false;
    bool previewWasDirty_ = false;
    bool debugDockHiddenForSize_ = false;
    QString theme_ = QStringLiteral("dark");
    double uiScale_ = 1.0;
    QProcess* playerProcess_ = nullptr;
    QProcess* buildProcess_ = nullptr;
    QProgressBar* buildProgress_ = nullptr;
    std::optional<BuildPlan> buildPlan_;
    size_t buildStep_ = 0;
    BuildFollowUp buildFollowUp_ = BuildFollowUp::None;
    std::vector<BuildDiagnostic> buildDiagnostics_;
    QByteArray buildLineBuffer_;
    QString playerScenePath_;
    QString lastLogMessage_;
};
} // namespace LuxEditor
