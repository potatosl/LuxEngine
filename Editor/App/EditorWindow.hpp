#pragma once
#include "Project/EditorProject.hpp"
#include "Scene/EditorDocument.hpp"
#include "Viewport/EditorTool.hpp"
#include <QMainWindow>
#include <QTimer>
#include <array>
#include <filesystem>
#include <functional>
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
class QTreeWidget;
class QUndoStack;
class QWidget;
class QSlider;
class QPushButton;
class QProcess;

namespace LuxEngine {
class SceneViewport2D;
class SceneViewport3D;
class DebugPanel;
struct TransformComponent;
class EditorWindow final : public QMainWindow {
public:
    explicit EditorWindow(QWidget* parent = nullptr);
    ~EditorWindow() override;

private:
    void BuildMenus();
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
    bool OpenProjectScene(const std::filesystem::path& file);
    void RecoverAutosaveIfNeeded();
    void AutoSaveScene();
    void NewProject();
    void OpenProjectDialog();
    void OpenProjectFile(const std::filesystem::path& file);
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
    void PaintTile(uint32 id, uint32 x, uint32 y, uint8_t tile);
    void DeleteEntity();
    void SelectEntity(std::optional<uint32> id);
    void OnNameChanged();
    void OnTransformChanged();
    void OnSpriteChanged();
    void OnColorChanged();
    void OnMeshChanged();
    void OnTilemapChanged();
    void OnPhysicsChanged();
    void OnAnimationChanged();
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
    void ApplyViewportTransform(uint32 id, const TransformComponent& value);
    void BeginViewportEdit();
    void EndViewportEdit();
    void SetTool(EditorTool tool);
    void Log(const QString& message, bool error = false);
    void ShowError(const QString& title, const QString& message);
    void RunWithProgress(const QString& message, const std::function<void()>& task);
    void CopyEntity();
    void PasteEntity();
    void RecordEdit(const QString& name, const std::function<void()>& edit);
    QByteArray SnapshotScene();
    void RestoreScene(const QByteArray& scene, std::optional<uint32> selected);
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    EditorDocument document_;
    std::optional<EditorProject> project_;
    RecentProjects recentProjects_;
    QTimer autosaveTimer_;
    QTimer previewTimer_;
    QUndoStack* undo_ = nullptr;
    QTabWidget* viewportTabs_ = nullptr;
    SceneViewport2D* canvas_ = nullptr;
    SceneViewport2D* overviewCanvas_ = nullptr;
    SceneViewport3D* canvas3D_ = nullptr;
    QListWidget* hierarchy_ = nullptr;
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
    std::vector<uint32> hierarchyIds_;
    std::vector<std::filesystem::path> projectFilePaths_;
    bool refreshingHierarchy_ = false;
    bool refreshingInspector_ = false;
    bool restoringHistory_ = false;
    EditorTool tool_ = EditorTool::Select;
    QByteArray viewportEditBefore_;
    QByteArray timelineBefore_;
    QByteArray previewBefore_;
    std::optional<uint32> viewportEditSelected_;
    std::optional<uint32> previewSelected_;
    uint64_t previewFrame_ = 0;
    bool previewActive_ = false;
    bool previewPaused_ = false;
    bool previewWasDirty_ = false;
    bool debugDockHiddenForSize_ = false;
    QString theme_ = QStringLiteral("dark");
    double uiScale_ = 1.0;
    QProcess* playerProcess_ = nullptr;
    QString playerScenePath_;
};
} // namespace LuxEngine
