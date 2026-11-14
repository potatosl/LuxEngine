#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Debug/DebugPanel.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/SceneHierarchy.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/GameViewport.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <LuxUI/AssetList.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>

namespace LuxEditor {
namespace {
LuxUI::Widget* Panel(LuxUI::Widget* parent, LuxUI::VBoxLayout*& layout, int margin = 6) {
    auto* panel = new LuxUI::Widget(parent);
    layout = new LuxUI::VBoxLayout(panel);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(5);
    return panel;
}

LuxUI::Label* Caption(const LuxUI::String& text, LuxUI::Widget* parent) {
    auto* label = new LuxUI::Label(text, parent);
    label->setObjectName(LUX_UI_TEXT("sectionCaption"));
    return label;
}

LuxUI::DoubleSpinBox* Number(LuxUI::Widget* parent, double low, double high, double value,
                             double step) {
    auto* control = new LuxUI::DoubleSpinBox(parent);
    control->setRange(low, high);
    control->setDecimals(3);
    control->setSingleStep(step);
    control->setValue(value);
    control->setButtonSymbols(LuxUI::AbstractSpinBox::NoButtons);
    control->setKeyboardTracking(false);
    control->setMinimumWidth(56);
    control->setSizePolicy(LuxUI::SizePolicy::Expanding, LuxUI::SizePolicy::Fixed);
    control->setToolTip(control->text());
    LuxUI::Object::connect(control, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), control,
                           [control] { control->setToolTip(control->text()); });
    return control;
}
} // namespace

void EditorWindow::BuildLayout() {
    setDockOptions(LuxUI::MainWindow::AllowNestedDocks | LuxUI::MainWindow::AllowTabbedDocks |
                   LuxUI::MainWindow::GroupedDragging);
    setCorner(LuxUI::Flags::BottomLeftCorner, LuxUI::Flags::BottomDockWidgetArea);
    setCorner(LuxUI::Flags::BottomRightCorner, LuxUI::Flags::BottomDockWidgetArea);

    const auto select = [this](std::optional<LuxEngine::uint32> id) { SelectEntity(id); };
    const auto transform = [this](LuxEngine::uint32 id,
                                  const LuxEngine::TransformComponent& value) {
        ApplyViewportTransform(id, value);
    };
    const auto begin = [this] { BeginViewportEdit(); };
    const auto end = [this] { EndViewportEdit(); };
    const auto paintTile = [this](LuxEngine::uint32 id, LuxEngine::uint32 x, LuxEngine::uint32 y,
                                  uint8_t tile) { PaintTile(id, x, y, tile); };

    auto* sceneFrame = new LuxUI::Widget(this);
    auto* sceneLayout = new LuxUI::VBoxLayout(sceneFrame);
    sceneLayout->setContentsMargins(0, 0, 0, 0);
    sceneLayout->setSpacing(0);
    sceneTabs_ = new LuxUI::TabBar(sceneFrame);
    sceneTabs_->setObjectName(LUX_UI_TEXT("sceneDocuments"));
    sceneTabs_->setExpanding(false);
    sceneTabs_->setTabsClosable(true);
    sceneTabs_->setUsesScrollButtons(true);
    sceneTabs_->setElideMode(LuxUI::Flags::ElideMiddle);
    sceneLayout->addWidget(sceneTabs_);
    sceneLoadProgress_ = new LuxUI::ProgressBar(sceneFrame);
    sceneLoadProgress_->setObjectName(LUX_UI_TEXT("sceneLoadProgress"));
    sceneLoadProgress_->setRange(0, 0);
    sceneLoadProgress_->setTextVisible(false);
    sceneLoadProgress_->setFixedHeight(3);
    sceneLoadProgress_->hide();
    sceneLayout->addWidget(sceneLoadProgress_);
    connect(sceneTabs_, &LuxUI::TabBar::currentChanged, this, [this](int index) {
        if (!refreshingSceneTabs_ && index >= 0)
            SwitchSceneDocument(sceneTabs_->tabData(index).toULongLong());
    });
    connect(sceneTabs_, &LuxUI::TabBar::tabCloseRequested, this, [this](int index) {
        if (index >= 0)
            RequestCloseScene(sceneTabs_->tabData(index).toULongLong());
    });
    auto* sceneBar = new LuxUI::Widget(sceneFrame);
    sceneBar->setObjectName(LUX_UI_TEXT("sceneBar"));
    auto* sceneBarLayout = new LuxUI::HBoxLayout(sceneBar);
    sceneBarLayout->setContentsMargins(8, 2, 8, 2);
    sceneBarLayout->setSpacing(8);
    auto* viewChoice = new LuxUI::ComboBox(sceneBar);
    viewChoice->addItems({LUX_UI_TEXT("3D"), LUX_UI_TEXT("2D"), LUX_UI_TEXT("Game")});
    viewChoice->setMinimumWidth(60);
    sceneBarLayout->addWidget(viewChoice);
    auto* projection = new LuxUI::ComboBox(sceneBar);
    projection->setObjectName(LUX_UI_TEXT("viewportProjection"));
    projection->addItems(
        {LUX_UI_TEXT("원근"), LUX_UI_TEXT("정면"), LUX_UI_TEXT("오른쪽"), LUX_UI_TEXT("상단")});
    sceneBarLayout->addWidget(projection);
    connect(projection, &LuxUI::ComboBox::currentIndexChanged, this, [this](int index) {
        canvas3D_->SetViewPreset(static_cast<SceneViewport3D::ViewPreset>(index));
    });
    connect(viewChoice, &LuxUI::ComboBox::currentIndexChanged, projection,
            [projection](int index) { projection->setVisible(index == 0); });
    auto* space = new LuxUI::ComboBox(sceneBar);
    space->setObjectName(LUX_UI_TEXT("transformSpace"));
    space->addItems({LUX_UI_TEXT("월드"), LUX_UI_TEXT("로컬")});
    space->setToolTip(LUX_UI_TEXT("기즈모 좌표계"));
    sceneBarLayout->addWidget(space);
    auto* pivot = new LuxUI::ComboBox(sceneBar);
    pivot->setObjectName(LUX_UI_TEXT("selectionPivot"));
    pivot->addItems({LUX_UI_TEXT("중앙 피벗"), LUX_UI_TEXT("활성 피벗"), LUX_UI_TEXT("개별 피벗")});
    pivot->setToolTip(LUX_UI_TEXT("다중 선택 회전·크기 조절의 기준점"));
    sceneBarLayout->addWidget(pivot);
    const auto cancelGesture = [this] {
        canvas_->CancelInteraction();
        canvas3D_->CancelInteraction();
        overviewCanvas_->CancelInteraction();
        CancelViewportEdit();
    };
    connect(space, &LuxUI::ComboBox::currentIndexChanged, this, [this, cancelGesture](int index) {
        cancelGesture();
        document_.SetTransformSpace(static_cast<TransformSpace>(index));
        RefreshSelection(false);
    });
    connect(pivot, &LuxUI::ComboBox::currentIndexChanged, this, [this, cancelGesture](int index) {
        cancelGesture();
        document_.SetSelectionPivotMode(static_cast<SelectionPivot>(index));
        RefreshSelection(false);
    });
    sceneBarLayout->addStretch();
    auto* gridVisibility = new LuxUI::ToolButton(sceneBar);
    gridVisibility->setObjectName(LUX_UI_TEXT("viewportGridToggle"));
    gridVisibility->setIcon(MakeEditorIcon(EditorIcon::Grid));
    gridVisibility->setToolTip(LUX_UI_TEXT("격자 표시"));
    gridVisibility->setCheckable(true);
    gridVisibility->setChecked(true);
    sceneBarLayout->addWidget(gridVisibility);
    connect(gridVisibility, &LuxUI::ToolButton::toggled, this, [this](bool visible) {
        canvas_->SetGridVisible(visible);
        canvas3D_->SetGridVisible(visible);
        overviewCanvas_->SetGridVisible(visible);
    });
    auto* focus = new LuxUI::ToolButton(sceneBar);
    focus->setIcon(MakeEditorIcon(EditorIcon::Focus));
    focus->setToolTip(LUX_UI_TEXT("선택 항목에 초점 (F)"));
    sceneBarLayout->addWidget(focus);
    connect(focus, &LuxUI::ToolButton::clicked, this, [this] {
        if (viewportTabs_->currentWidget() == canvas3D_)
            canvas3D_->FocusSelection();
        else
            canvas_->FocusSelection();
    });
    sceneLayout->addWidget(sceneBar);
    viewportTabs_ = new LuxUI::TabWidget(sceneFrame);
    viewportTabs_->setObjectName(LUX_UI_TEXT("sceneGameViews"));
    auto sharedRenderer = std::make_shared<EngineSceneRenderer>();
    canvas3D_ = new SceneViewport3D(viewportTabs_, document_, select, transform, begin, end,
                                    sharedRenderer);
    canvas_ = new SceneViewport2D(viewportTabs_, document_, select, transform, paintTile, begin,
                                  end, sharedRenderer);
    canvas_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    canvas3D_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    canvas_->setObjectName(LUX_UI_TEXT("sceneViewport2D"));
    canvas3D_->setObjectName(LUX_UI_TEXT("sceneViewport3D"));
    gameCanvas_ = new GameViewport(viewportTabs_);
    viewportTabs_->addTab(canvas3D_, LUX_UI_TEXT("Scene · 3D"));
    viewportTabs_->addTab(canvas_, LUX_UI_TEXT("Scene · 2D"));
    viewportTabs_->addTab(gameCanvas_, LUX_UI_TEXT("Game"));
    connect(viewportTabs_, &LuxUI::TabWidget::currentChanged, this,
            [space, pivot, gridVisibility, focus](int index) {
                const bool editing = index != 2;
                space->setEnabled(editing);
                pivot->setEnabled(editing);
                gridVisibility->setEnabled(editing);
                focus->setEnabled(editing);
            });
    sceneLayout->addWidget(viewportTabs_, 1);
    workspaceStack_ = new LuxUI::StackedWidget(this);
    workspaceStack_->setObjectName(LUX_UI_TEXT("editorWorkspace"));
    workspaceStack_->addWidget(sceneFrame);
    setCentralWidget(workspaceStack_);
    connect(viewChoice, &LuxUI::ComboBox::currentIndexChanged, viewportTabs_,
            &LuxUI::TabWidget::setCurrentIndex);
    connect(viewportTabs_, &LuxUI::TabWidget::currentChanged, viewChoice,
            &LuxUI::ComboBox::setCurrentIndex);
    connect(viewportTabs_, &LuxUI::TabWidget::currentChanged, this, [this] { UpdateChrome(); });

    LuxUI::VBoxLayout* hierarchyLayout;
    auto* hierarchyPanel = Panel(this, hierarchyLayout);
    groupTree_ = new LuxUI::TreeWidget(hierarchyPanel);
    groupTree_->setHeaderLabels({LUX_UI_TEXT("장면 그룹"), LUX_UI_TEXT("개수")});
    groupTree_->setRootIsDecorated(false);
    groupTree_->setFixedHeight(100);
    for (const LuxUI::String& name :
         {LUX_UI_TEXT("전체"), LUX_UI_TEXT("2D 스프라이트"), LUX_UI_TEXT("3D 메시")}) {
        auto* item = new LuxUI::TreeWidgetItem(groupTree_);
        item->setText(0, name);
        item->setTextAlignment(1, LuxUI::Flags::AlignRight);
    }
    groupTree_->setCurrentItem(groupTree_->topLevelItem(0));
    groupTree_->hide();
    auto* objectFilter = new LuxUI::ComboBox(hierarchyPanel);
    objectFilter->setObjectName(LUX_UI_TEXT("outlinerFilter"));
    objectFilter->addItems(
        {LUX_UI_TEXT("모든 오브젝트"), LUX_UI_TEXT("스프라이트"), LUX_UI_TEXT("메시")});
    connect(objectFilter, &LuxUI::ComboBox::currentIndexChanged, this,
            [this](int index) { groupTree_->setCurrentItem(groupTree_->topLevelItem(index)); });
    hierarchyLayout->addWidget(objectFilter);
    hierarchySearch_ = new LuxUI::LineEdit(hierarchyPanel);
    hierarchySearch_->setPlaceholderText(LUX_UI_TEXT("엔티티 이름 검색"));
    hierarchyLayout->addWidget(hierarchySearch_);
    auto* tree = new SceneHierarchy(hierarchyPanel);
    tree->reparent = [this](const std::vector<LuxEngine::uint32>& children,
                            LuxEngine::uint32 parent) { ReparentSelection(children, parent); };
    hierarchy_ = tree;
    hierarchy_->setObjectName(LUX_UI_TEXT("sceneOutliner"));
    hierarchy_->setContextMenuPolicy(LuxUI::Flags::CustomContextMenu);
    hierarchyLayout->addWidget(hierarchy_, 1);
    auto* entityActions = new LuxUI::HBoxLayout;
    auto* add2D = new LuxUI::ToolButton(hierarchyPanel);
    add2D->setIcon(MakeEditorIcon(EditorIcon::Sprite));
    add2D->setText(LUX_UI_TEXT("2D"));
    add2D->setToolButtonStyle(LuxUI::Flags::ToolButtonTextBesideIcon);
    add2D->setToolTip(LUX_UI_TEXT("2D 엔티티 추가"));
    auto* add3D = new LuxUI::ToolButton(hierarchyPanel);
    add3D->setIcon(MakeEditorIcon(EditorIcon::Mesh));
    add3D->setText(LUX_UI_TEXT("3D"));
    add3D->setToolButtonStyle(LuxUI::Flags::ToolButtonTextBesideIcon);
    add3D->setToolTip(LUX_UI_TEXT("3D 메시 엔티티 추가"));
    auto* remove = new LuxUI::ToolButton(hierarchyPanel);
    remove->setIcon(MakeEditorIcon(EditorIcon::Delete));
    remove->setToolTip(LUX_UI_TEXT("선택 엔티티 삭제 (Delete)"));
    entityActions->addWidget(add2D);
    entityActions->addWidget(add3D);
    entityActions->addStretch();
    entityActions->addWidget(remove);
    hierarchyLayout->addLayout(entityActions);
    add2D->hide();
    add3D->hide();
    remove->hide();
    connect(groupTree_, &LuxUI::TreeWidget::currentItemChanged, this,
            [this](LuxUI::TreeWidgetItem* item) {
                groupFilter_ = item ? groupTree_->indexOfTopLevelItem(item) : 0;
                RefreshHierarchy();
            });
    connect(hierarchySearch_, &LuxUI::LineEdit::textChanged, this, [this] { RefreshHierarchy(); });
    connect(hierarchy_, &LuxUI::TreeWidget::itemSelectionChanged, this,
            &EditorWindow::SyncHierarchySelection);
    connect(hierarchy_, &LuxUI::TreeWidget::currentItemChanged, this,
            [this] { SyncHierarchySelection(); });
    connect(hierarchy_, &LuxUI::TreeWidget::customContextMenuRequested, this,
            [this](const LuxUI::Point& point) {
                LuxUI::Menu menu(this);
                menu.addAction(MakeEditorIcon(EditorIcon::Sprite), LUX_UI_TEXT("2D 엔티티 추가"),
                               this, [this] { AddEntity(); });
                menu.addAction(MakeEditorIcon(EditorIcon::Mesh), LUX_UI_TEXT("3D 메시 엔티티 추가"),
                               this, [this] { AddMeshEntity(); });
                AddBasicModelMenu(&menu);
                menu.addSeparator();
                menu.addAction(LUX_UI_TEXT("복사"), this, [this] { CopyEntity(); });
                menu.addAction(LUX_UI_TEXT("붙여넣기"), this, [this] { PasteEntity(); });
                menu.addAction(LUX_UI_TEXT("선택 삭제"), this, [this] { DeleteEntity(); });
                menu.addSeparator();
                for (const char* id :
                     {"groupEntities", "ungroupEntities", "detachEntity", "moveEntityToScene"})
                    if (auto* action = findChild<LuxUI::Action*>(LuxUI::String::fromLatin1(id)))
                        menu.addAction(action);
                menu.exec(hierarchy_->mapToGlobal(point));
            });
    connect(add2D, &LuxUI::ToolButton::clicked, this, [this] { AddEntity(); });
    connect(add3D, &LuxUI::ToolButton::clicked, this, [this] { AddMeshEntity(); });
    connect(remove, &LuxUI::ToolButton::clicked, this, [this] { DeleteEntity(); });

    LuxUI::VBoxLayout* assetsLayout;
    auto* assetsPanel = Panel(this, assetsLayout, 4);
    auto* assetHeader = new LuxUI::HBoxLayout;
    auto* rescan = new LuxUI::ToolButton(assetsPanel);
    rescan->setIcon(MakeEditorIcon(EditorIcon::Folder));
    rescan->setText(LUX_UI_TEXT("폴더 새로 고침"));
    rescan->setToolTip(LUX_UI_TEXT("프로젝트 콘텐츠 다시 검색"));
    connect(rescan, &LuxUI::ToolButton::clicked, this, [this] { RefreshProjectExplorer(); });
    assetHeader->addWidget(rescan);
    assetBreadcrumb_ = new LuxUI::Label(LUX_UI_TEXT("모든 콘텐츠"), assetsPanel);
    assetBreadcrumb_->setObjectName(LUX_UI_TEXT("assetBreadcrumb"));
    assetHeader->addWidget(assetBreadcrumb_, 1);
    assetHeader->addStretch();
    assetFilter_ = new LuxUI::ComboBox(assetsPanel);
    assetFilter_->setObjectName(LUX_UI_TEXT("assetTypeFilter"));
    assetFilter_->addItems({LUX_UI_TEXT("전체"), LUX_UI_TEXT("장면"), LUX_UI_TEXT("이미지"),
                            LUX_UI_TEXT("오디오"), LUX_UI_TEXT("모델"), LUX_UI_TEXT("기타")});
    assetHeader->addWidget(assetFilter_);
    auto* gridMode = new LuxUI::ToolButton(assetsPanel);
    gridMode->setIcon(MakeEditorIcon(EditorIcon::Grid));
    gridMode->setToolTip(LUX_UI_TEXT("격자로 보기"));
    auto* listMode = new LuxUI::ToolButton(assetsPanel);
    listMode->setIcon(MakeEditorIcon(EditorIcon::List));
    listMode->setToolTip(LUX_UI_TEXT("목록으로 보기"));
    assetHeader->addWidget(gridMode);
    assetHeader->addWidget(listMode);
    assetsLayout->addLayout(assetHeader);
    projectSearch_ = new LuxUI::LineEdit(assetsPanel);
    projectSearch_->setPlaceholderText(LUX_UI_TEXT("에셋 이름 검색"));
    assetsLayout->addWidget(projectSearch_);
    auto* assetSplit = new LuxUI::Splitter(LuxUI::Flags::Horizontal, assetsPanel);
    assetSplit->setObjectName(LUX_UI_TEXT("contentBrowserSplitter"));
    assetFolders_ = new LuxUI::TreeWidget(assetSplit);
    assetFolders_->setObjectName(LUX_UI_TEXT("assetFolders"));
    assetFolders_->setHeaderHidden(true);
    assetFolders_->setMinimumWidth(140);
    assetFolders_->setMaximumWidth(300);
    connect(
        assetFolders_, &LuxUI::TreeWidget::currentItemChanged, this,
        [this](LuxUI::TreeWidgetItem* item) {
            assetFolder_ =
                item ? PathFromUtf8(
                           item->data(0, LuxUI::Flags::UserRole).toString().toUtf8().toStdString())
                     : std::filesystem::path{};
            RefreshProjectExplorer();
        });
    projectFiles_ = new LuxUI::AssetList(assetSplit);
    projectFiles_->setObjectName(LUX_UI_TEXT("assetGrid"));
    projectFiles_->setViewMode(LuxUI::ListView::IconMode);
    projectFiles_->setIconSize(LuxUI::Size(64, 64));
    projectFiles_->setGridSize(LuxUI::Size(112, 100));
    projectFiles_->setResizeMode(LuxUI::ListView::Adjust);
    projectFiles_->setWordWrap(true);
    projectFiles_->setSpacing(3);
    auto* previewPanel = new LuxUI::Widget(assetSplit);
    assetPreviewPanel_ = previewPanel;
    auto* previewLayout = new LuxUI::VBoxLayout(previewPanel);
    previewLayout->setContentsMargins(4, 4, 4, 4);
    previewLayout->addWidget(Caption(LUX_UI_TEXT("PREVIEW"), previewPanel));
    assetPreview_ = new LuxUI::Label(previewPanel);
    assetPreview_->setAlignment(LuxUI::Flags::AlignCenter);
    assetPreview_->setMinimumWidth(90);
    assetPreview_->setSizePolicy(LuxUI::SizePolicy::Ignored, LuxUI::SizePolicy::Ignored);
    assetDetails_ = new LuxUI::Label(previewPanel);
    assetDetails_->setWordWrap(true);
    previewLayout->addWidget(assetPreview_, 1);
    previewLayout->addWidget(assetDetails_);
    assetSplit->addWidget(projectFiles_);
    assetSplit->addWidget(previewPanel);
    assetSplit->setStretchFactor(0, 0);
    assetSplit->setStretchFactor(1, 5);
    assetSplit->setStretchFactor(2, 1);
    assetSplit->setSizes({180, 800, 200});
    assetsLayout->addWidget(assetSplit, 1);
    assetCount_ = new LuxUI::Label(assetsPanel);
    assetCount_->setObjectName(LUX_UI_TEXT("assetCount"));
    assetsLayout->addWidget(assetCount_);
    connect(projectSearch_, &LuxUI::LineEdit::textChanged, this,
            [this] { RefreshProjectExplorer(); });
    connect(assetFilter_, &LuxUI::ComboBox::currentIndexChanged, this,
            [this] { RefreshProjectExplorer(); });
    connect(gridMode, &LuxUI::ToolButton::clicked, this, [this] {
        projectFiles_->setViewMode(LuxUI::ListView::IconMode);
        projectFiles_->setGridSize(LuxUI::Size(120, 118));
    });
    connect(listMode, &LuxUI::ToolButton::clicked, this, [this] {
        projectFiles_->setViewMode(LuxUI::ListView::ListMode);
        projectFiles_->setGridSize(LuxUI::Size());
    });
    connect(projectFiles_, &LuxUI::ListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || static_cast<size_t>(row) >= projectFilePaths_.size()) {
            assetPreview_->clear();
            assetDetails_->clear();
            return;
        }
        const auto& path = projectFilePaths_[static_cast<size_t>(row)];
        LuxUI::Pixmap image(LuxUI::String::fromUtf8(PathUtf8(path)));
        if (!image.isNull()) {
            assetPreview_->setPixmap(
                image.scaled(std::max(1, std::min(220, assetPreview_->width())),
                             std::max(1, std::min(180, assetPreview_->height())),
                             LuxUI::Flags::KeepAspectRatio, LuxUI::Flags::SmoothTransformation));
        } else {
            assetPreview_->setPixmap(
                LuxUI::FileIconProvider()
                    .icon(LuxUI::FileInfo(LuxUI::String::fromUtf8(PathUtf8(path))))
                    .pixmap(64, 64));
        }
        LuxUI::FileInfo info(LuxUI::String::fromUtf8(PathUtf8(path)));
        assetDetails_->setText(
            LUX_UI_TEXT("%1\n%2 KB").arg(info.fileName()).arg((info.size() + 1023) / 1024));
    });
    connect(projectFiles_, &LuxUI::ListWidget::itemDoubleClicked, this, [this] {
        const int row = projectFiles_->currentRow();
        if (row < 0 || static_cast<size_t>(row) >= projectFilePaths_.size())
            return;
        const auto& path = projectFilePaths_[static_cast<size_t>(row)];
        const auto suffix = LuxUI::String::fromUtf8(PathUtf8(path.extension())).toLower();
        if (suffix == LuxUI::Latin1String(".luxprefab"))
            AddPrefabAsset(path);
        else if (suffix == LuxUI::Latin1String(".lux"))
            OpenProjectScene(path);
        else if (suffix == LuxUI::Latin1String(".obj") || suffix == LuxUI::Latin1String(".fbx"))
            AddModelAsset(path);
    });

    auto* inspectorScroll = new LuxUI::ScrollArea(this);
    inspectorScroll->setObjectName(LUX_UI_TEXT("inspectorScroll"));
    inspectorScroll->setMinimumWidth(290);
    inspectorScroll->setWidgetResizable(true);
    auto* inspector = new LuxUI::Widget(inspectorScroll);
    auto* inspectorLayout = new LuxUI::VBoxLayout(inspector);
    inspectorLayout->setContentsMargins(7, 7, 7, 7);
    inspectorLayout->setSpacing(5);
    detailsSearch_ = new LuxUI::LineEdit(inspector);
    detailsSearch_->setObjectName(LUX_UI_TEXT("detailsSearch"));
    detailsSearch_->setPlaceholderText(LUX_UI_TEXT("속성 검색"));
    inspectorLayout->addWidget(detailsSearch_);
    connect(detailsSearch_, &LuxUI::LineEdit::textChanged, this, &EditorWindow::FilterDetails);
    selectionLabel_ = Caption(LUX_UI_TEXT("선택된 엔티티 없음"), inspector);
    inspectorLayout->addWidget(selectionLabel_);
    inspectorBody_ = new LuxUI::Widget(inspector);
    auto* body = new LuxUI::VBoxLayout(inspectorBody_);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(8);
    auto* nameRow = new LuxUI::FormLayout;
    name_ = new LuxUI::LineEdit(inspectorBody_);
    nameRow->addRow(LUX_UI_TEXT("이름"), name_);
    body->addLayout(nameRow);
    sceneReferencePanel_ = new LuxUI::GroupBox(LUX_UI_TEXT("씬 참조"), inspectorBody_);
    sceneReferencePanel_->setObjectName(LUX_UI_TEXT("sceneReferencePanel"));
    auto* referenceLayout = new LuxUI::VBoxLayout(sceneReferencePanel_);
    sceneReferencePath_ = new LuxUI::LineEdit(sceneReferencePanel_);
    sceneReferencePath_->setObjectName(LUX_UI_TEXT("sceneReferencePath"));
    sceneReferencePath_->setReadOnly(true);
    referenceLayout->addWidget(sceneReferencePath_);
    sceneReferenceState_ = new LuxUI::Label(sceneReferencePanel_);
    referenceLayout->addWidget(sceneReferenceState_);
    auto* referenceButtons = new LuxUI::HBoxLayout;
    auto* openReference = new LuxUI::PushButton(LUX_UI_TEXT("열기"), sceneReferencePanel_);
    auto* relinkReference = new LuxUI::PushButton(LUX_UI_TEXT("경로 변경"), sceneReferencePanel_);
    referenceButtons->addWidget(openReference);
    referenceButtons->addWidget(relinkReference);
    referenceLayout->addLayout(referenceButtons);
    connect(openReference, &LuxUI::PushButton::clicked, this, [this] {
        const auto selected = document_.GetSelected();
        if (selected && selected.HasComponent<LuxEngine::SceneReferenceComponent>())
            OpenSceneDocument(
                PathFromUtf8(selected.GetComponent<LuxEngine::SceneReferenceComponent>().path));
    });
    connect(relinkReference, &LuxUI::PushButton::clicked, this, [this] { RelinkSceneReference(); });
    body->addWidget(sceneReferencePanel_);
    auto* transformGroup = new LuxUI::GroupBox(LUX_UI_TEXT("Transform"), inspectorBody_);
    auto* grid = new LuxUI::GridLayout(transformGroup);
    grid->setContentsMargins(6, 8, 6, 6);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(3);
    const char* rows[] = {"위치", "회전 °", "크기"};
    const char* axisColors[] = {"#c76168", "#63a979", "#6294c8"};
    for (int row = 0; row < 3; ++row) {
        grid->addWidget(new LuxUI::Label(LuxUI::String::fromUtf8(rows[row]), transformGroup), row,
                        0);
        for (int axis = 0; axis < 3; ++axis) {
            transform_[row][axis] =
                Number(transformGroup, -100000, 100000, row == 2 ? 1 : 0, row == 1 ? 1 : .1);
            transform_[row][axis]->setToolTip(LUX_UI_TEXT("%1 · %2").arg(
                LuxUI::String::fromUtf8(rows[row]), LuxUI::String::fromLatin1("XYZ" + axis, 1)));
            transform_[row][axis]->setStyleSheet(
                LUX_UI_TEXT("QDoubleSpinBox{border-left:3px solid %1;}")
                    .arg(LuxUI::String::fromLatin1(axisColors[axis])));
            grid->addWidget(transform_[row][axis], row, axis + 1);
            grid->setColumnStretch(axis + 1, 1);
            connect(transform_[row][axis], qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged),
                    this, [this, row, axis] { OnTransformChanged(row, axis); });
        }
    }
    body->addWidget(transformGroup);
    auto* spriteGroup = new LuxUI::GroupBox(LUX_UI_TEXT("Sprite Renderer"), inspectorBody_);
    auto* spriteLayout = new LuxUI::VBoxLayout(spriteGroup);
    spriteEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("스프라이트 사용"), spriteGroup);
    spriteEnabled_->setObjectName(LUX_UI_TEXT("editorSprite"));
    spriteLayout->addWidget(spriteEnabled_);
    auto* colors = new LuxUI::GridLayout;
    for (int channel = 0; channel < 4; ++channel) {
        const int row = channel / 2, col = channel % 2;
        colors->addWidget(
            new LuxUI::Label(LuxUI::String::fromLatin1("RGBA" + channel, 1), spriteGroup), row * 2,
            col);
        color_[channel] = Number(spriteGroup, 0, 1, 1, .05);
        colors->addWidget(color_[channel], row * 2 + 1, col);
        connect(color_[channel], qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
                [this, channel] { OnColorChanged(channel); });
    }
    spriteLayout->addLayout(colors);
    body->addWidget(spriteGroup);
    connect(spriteEnabled_, &LuxUI::CheckBox::toggled, this, [this] { OnSpriteChanged(); });
    auto* meshGroup = new LuxUI::GroupBox(LUX_UI_TEXT("Mesh Renderer"), inspectorBody_);
    auto* meshLayout = new LuxUI::FormLayout(meshGroup);
    meshEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("메시 사용"), meshGroup);
    meshEnabled_->setObjectName(LUX_UI_TEXT("editorMesh"));
    meshLayout->addRow(meshEnabled_);
    meshPath_ = new LuxUI::LineEdit(meshGroup);
    meshPath_->setObjectName(LUX_UI_TEXT("meshAssetPath"));
    meshPath_->setReadOnly(true);
    meshPath_->setPlaceholderText(LUX_UI_TEXT("기본 큐브"));
    meshLayout->addRow(LUX_UI_TEXT("모델"), meshPath_);
    meshPath_->setToolTip(LUX_UI_TEXT("OBJ·FBX 정적 메시를 지원합니다. FBX 재질·스킨·애니메이션 "
                                      "자동 가져오기는 개발 중입니다."));
    auto* meshButtons = new LuxUI::Widget(meshGroup);
    auto* meshButtonsLayout = new LuxUI::HBoxLayout(meshButtons);
    meshButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto* chooseMesh = new LuxUI::PushButton(LUX_UI_TEXT("모델 선택"), meshButtons);
    chooseMesh->setObjectName(LUX_UI_TEXT("chooseMeshAsset"));
    auto* clearMesh = new LuxUI::PushButton(LUX_UI_TEXT("기본 큐브"), meshButtons);
    clearMesh->setObjectName(LUX_UI_TEXT("clearMeshAsset"));
    meshButtonsLayout->addWidget(chooseMesh);
    meshButtonsLayout->addWidget(clearMesh);
    meshLayout->addRow(meshButtons);
    baseColorTexturePath_ = new LuxUI::LineEdit(meshGroup);
    baseColorTexturePath_->setObjectName(LUX_UI_TEXT("baseColorTexturePath"));
    baseColorTexturePath_->setReadOnly(true);
    baseColorTexturePath_->setPlaceholderText(LUX_UI_TEXT("단색 재질"));
    meshLayout->addRow(LUX_UI_TEXT("기본 색상 텍스처"), baseColorTexturePath_);
    auto* textureButtons = new LuxUI::Widget(meshGroup);
    auto* textureButtonsLayout = new LuxUI::HBoxLayout(textureButtons);
    textureButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto* chooseTexture = new LuxUI::PushButton(LUX_UI_TEXT("이미지 선택"), textureButtons);
    chooseTexture->setObjectName(LUX_UI_TEXT("chooseBaseColorTexture"));
    auto* clearTexture = new LuxUI::PushButton(LUX_UI_TEXT("단색 재질"), textureButtons);
    clearTexture->setObjectName(LUX_UI_TEXT("clearBaseColorTexture"));
    textureButtonsLayout->addWidget(chooseTexture);
    textureButtonsLayout->addWidget(clearTexture);
    meshLayout->addRow(textureButtons);
    auto* reloadAssets = new LuxUI::PushButton(LUX_UI_TEXT("메시·텍스처 다시 불러오기"), meshGroup);
    reloadAssets->setObjectName(LUX_UI_TEXT("reloadMeshAssets"));
    meshLayout->addRow(reloadAssets);
    connect(chooseTexture, &LuxUI::PushButton::clicked, this, [this] {
        if (!document_.GetSelected())
            return;
        const auto tab = document_.GetActiveTabId();
        const auto entity = document_.GetSelected().GetId();
        const auto file = LuxUI::FileDialog::getOpenFileName(
            this, LUX_UI_TEXT("기본 색상 텍스처 선택"), baseColorTexturePath_->text(),
            LUX_UI_TEXT("이미지 (*.png *.jpg *.jpeg *.bmp *.tga)"));
        if (file.isEmpty())
            return;
        if (document_.GetActiveTabId() != tab || document_.GetSelectedId(tab) != entity) {
            Log(LUX_UI_TEXT("선택이 변경되어 텍스처 적용을 취소했습니다."), true);
            return;
        }
        const auto path = file.toUtf8().toStdString();
        std::string error;
        if (!LuxEngine::AssetManager::ReloadTexture(path, error)) {
            ShowError(LUX_UI_TEXT("텍스처 불러오기"), LuxUI::String::fromUtf8(error));
            return;
        }
        const auto selected = document_.GetSelected();
        if (selected.HasComponent<LuxEngine::MeshRendererComponent>()) {
            const auto& model = selected.GetComponent<LuxEngine::MeshRendererComponent>().meshPath;
            if (!model.empty()) {
                LuxEngine::RawMeshData data;
                if (!LuxEngine::AssetManager::LoadMesh(document_.GetScene().ResolveAssetPath(model),
                                                       data) ||
                    !data.hasTexCoords) {
                    ShowError(LUX_UI_TEXT("텍스처 불러오기"),
                              LUX_UI_TEXT("모델에 텍스처를 적용할 UV 좌표가 없습니다."));
                    return;
                }
            }
        }
        RecordEdit(LUX_UI_TEXT("기본 색상 텍스처 변경"), [this, path] {
            auto selected = document_.GetSelected();
            if (!selected.HasComponent<LuxEngine::MeshRendererComponent>())
                selected.AddComponent<LuxEngine::MeshRendererComponent>();
            selected.GetComponent<LuxEngine::MeshRendererComponent>().material.baseColorTexture =
                path;
            document_.MarkDirty();
        });
        RefreshAll();
    });
    connect(clearTexture, &LuxUI::PushButton::clicked, this, [this] {
        if (!document_.GetSelected() ||
            !document_.GetSelected().HasComponent<LuxEngine::MeshRendererComponent>())
            return;
        RecordEdit(LUX_UI_TEXT("기본 색상 텍스처 초기화"), [this] {
            document_.GetSelected()
                .GetComponent<LuxEngine::MeshRendererComponent>()
                .material.baseColorTexture.clear();
            document_.MarkDirty();
        });
        RefreshAll();
    });
    connect(reloadAssets, &LuxUI::PushButton::clicked, this, [this] {
        const auto selected = document_.GetSelected();
        if (!selected || !selected.HasComponent<LuxEngine::MeshRendererComponent>())
            return;
        const auto& mesh = selected.GetComponent<LuxEngine::MeshRendererComponent>();
        LuxUI::StringList errors;
        std::string error;
        bool requested = false;
        if (!mesh.meshPath.empty()) {
            requested = true;
            if (!LuxEngine::AssetManager::ReloadMesh(
                    document_.GetScene().ResolveAssetPath(mesh.meshPath), error,
                    !mesh.material.baseColorTexture.empty() ||
                        !mesh.material.normalTexture.empty() ||
                        !mesh.material.metallicTexture.empty() ||
                        !mesh.material.roughnessTexture.empty() ||
                        !mesh.material.occlusionTexture.empty()))
                errors << LuxUI::String::fromUtf8(error);
        }
        for (const auto* path : {&mesh.material.baseColorTexture, &mesh.material.normalTexture,
                                 &mesh.material.metallicTexture, &mesh.material.roughnessTexture,
                                 &mesh.material.occlusionTexture}) {
            if (path->empty())
                continue;
            requested = true;
            if (!LuxEngine::AssetManager::ReloadTexture(
                    document_.GetScene().ResolveAssetPath(*path), error))
                errors << LuxUI::String::fromUtf8(error);
        }
        Log(!errors.isEmpty() ? errors.join('\n')
            : requested       ? LUX_UI_TEXT("자산 파일 검증을 마쳤습니다. 뷰포트에 갱신합니다.")
                              : LUX_UI_TEXT("다시 불러올 자산 파일이 없습니다."),
            !errors.isEmpty());
        RefreshAll();
    });
    connect(chooseMesh, &LuxUI::PushButton::clicked, this, [this] { ChooseModelAsset(); });
    connect(clearMesh, &LuxUI::PushButton::clicked, this, [this] {
        if (!document_.GetSelected() ||
            !document_.GetSelected().HasComponent<LuxEngine::MeshRendererComponent>())
            return;
        RecordEdit(LUX_UI_TEXT("메시 파일 초기화"), [this] {
            document_.GetSelected()
                .GetComponent<LuxEngine::MeshRendererComponent>()
                .meshPath.clear();
            document_.MarkDirty();
        });
        RefreshAll();
    });
    metallic_ = Number(meshGroup, 0, 1, 0, .05);
    roughness_ = Number(meshGroup, 0, 1, 1, .05);
    meshLayout->addRow(LUX_UI_TEXT("금속성"), metallic_);
    meshLayout->addRow(LUX_UI_TEXT("거칠기"), roughness_);
    materialAlphaMode_ = new LuxUI::ComboBox(meshGroup);
    materialAlphaMode_->setObjectName(LUX_UI_TEXT("materialAlphaMode"));
    materialAlphaMode_->addItems(
        {LUX_UI_TEXT("불투명"), LUX_UI_TEXT("컷아웃"), LUX_UI_TEXT("반투명")});
    materialAlphaCutoff_ = Number(meshGroup, 0, 1, .001, .05);
    materialAlphaCutoff_->setObjectName(LUX_UI_TEXT("materialAlphaCutoff"));
    materialDoubleSided_ = new LuxUI::CheckBox(LUX_UI_TEXT("양면"), meshGroup);
    materialDoubleSided_->setObjectName(LUX_UI_TEXT("materialDoubleSided"));
    meshLayout->addRow(LUX_UI_TEXT("표면 모드"), materialAlphaMode_);
    meshLayout->addRow(LUX_UI_TEXT("알파 임계값"), materialAlphaCutoff_);
    meshLayout->addRow(materialDoubleSided_);
    const std::array<LuxUI::String, 4> mapNames{
        LUX_UI_TEXT("노멀 텍스처"), LUX_UI_TEXT("금속성 텍스처"), LUX_UI_TEXT("거칠기 텍스처"),
        LUX_UI_TEXT("AO 텍스처")};
    for (size_t i = 0; i < materialMapPaths_.size(); ++i) {
        auto* field = new LuxUI::LineEdit(meshGroup);
        field->setObjectName(LUX_UI_TEXT("materialMap%1").arg(i));
        materialMapPaths_[i] = field;
        auto* row = new LuxUI::HBoxLayout;
        row->addWidget(field);
        auto* choose = new LuxUI::PushButton(LUX_UI_TEXT("선택"), meshGroup);
        row->addWidget(choose);
        meshLayout->addRow(mapNames[i], row);
        connect(field, &LuxUI::LineEdit::editingFinished, this,
                [this, field] { OnMeshChanged(field); });
        connect(choose, &LuxUI::PushButton::clicked, this, [this, field] {
            const auto file = LuxUI::FileDialog::getOpenFileName(
                this, LUX_UI_TEXT("재질 텍스처 선택"), field->text(),
                LUX_UI_TEXT("이미지 (*.png *.jpg *.jpeg *.bmp *.tga)"));
            if (file.isEmpty())
                return;
            field->setText(file);
            OnMeshChanged(field);
        });
    }
    materialNormalScale_ = Number(meshGroup, 0, 16, 1, .05);
    materialNormalScale_->setObjectName(LUX_UI_TEXT("materialNormalScale"));
    materialOcclusionStrength_ = Number(meshGroup, 0, 1, 1, .05);
    materialOcclusionStrength_->setObjectName(LUX_UI_TEXT("materialOcclusionStrength"));
    materialMipmaps_ = new LuxUI::CheckBox(LUX_UI_TEXT("밉맵"), meshGroup);
    materialMipmaps_->setObjectName(LUX_UI_TEXT("materialMipmaps"));
    materialAddressMode_ = new LuxUI::ComboBox(meshGroup);
    materialAddressMode_->setObjectName(LUX_UI_TEXT("materialAddressMode"));
    materialAddressMode_->addItems({LUX_UI_TEXT("가장자리 고정"), LUX_UI_TEXT("반복")});
    materialAnisotropic_ = new LuxUI::CheckBox(LUX_UI_TEXT("이방성 필터링"), meshGroup);
    materialAnisotropic_->setObjectName(LUX_UI_TEXT("materialAnisotropic"));
    meshLayout->addRow(LUX_UI_TEXT("노멀 강도"), materialNormalScale_);
    meshLayout->addRow(LUX_UI_TEXT("AO 강도"), materialOcclusionStrength_);
    meshLayout->addRow(materialMipmaps_);
    meshLayout->addRow(LUX_UI_TEXT("텍스처 주소"), materialAddressMode_);
    meshLayout->addRow(materialAnisotropic_);
    connect(materialNormalScale_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this] { OnMeshChanged(materialNormalScale_); });
    connect(materialOcclusionStrength_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged),
            this, [this] { OnMeshChanged(materialOcclusionStrength_); });
    connect(materialMipmaps_, &LuxUI::CheckBox::toggled, this,
            [this] { OnMeshChanged(materialMipmaps_); });
    connect(materialAddressMode_, qOverload<int>(&LuxUI::ComboBox::currentIndexChanged), this,
            [this] { OnMeshChanged(materialAddressMode_); });
    connect(materialAnisotropic_, &LuxUI::CheckBox::toggled, this,
            [this] { OnMeshChanged(materialAnisotropic_); });
    connect(materialAlphaMode_, qOverload<int>(&LuxUI::ComboBox::currentIndexChanged), this,
            [this] { OnMeshChanged(materialAlphaMode_); });
    connect(materialAlphaCutoff_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this] { OnMeshChanged(materialAlphaCutoff_); });
    connect(materialDoubleSided_, &LuxUI::CheckBox::toggled, this,
            [this] { OnMeshChanged(materialDoubleSided_); });
    body->addWidget(meshGroup);
    connect(meshEnabled_, &LuxUI::CheckBox::toggled, this, [this] { OnMeshChanged(meshEnabled_); });
    connect(metallic_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this] { OnMeshChanged(metallic_); });
    connect(roughness_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this] { OnMeshChanged(roughness_); });
    auto* tileGroup = new LuxUI::GroupBox(LUX_UI_TEXT("Tilemap"), inspectorBody_);
    auto* tileLayout = new LuxUI::FormLayout(tileGroup);
    tilemapEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("타일맵 사용"), tileGroup);
    tilemapEnabled_->setObjectName(LUX_UI_TEXT("editorTilemap"));
    tileLayout->addRow(tilemapEnabled_);
    tilemapWidth_ = new LuxUI::SpinBox(tileGroup);
    tilemapWidth_->setRange(1, 256);
    tilemapHeight_ = new LuxUI::SpinBox(tileGroup);
    tilemapHeight_->setRange(1, 256);
    tilemapSize_ = Number(tileGroup, .1, 64, 1, .1);
    tileLayout->addRow(LUX_UI_TEXT("가로 칸"), tilemapWidth_);
    tileLayout->addRow(LUX_UI_TEXT("세로 칸"), tilemapHeight_);
    tileLayout->addRow(LUX_UI_TEXT("칸 크기"), tilemapSize_);
    body->addWidget(tileGroup);
    connect(tilemapEnabled_, &LuxUI::CheckBox::toggled, this,
            [this] { OnTilemapChanged(tilemapEnabled_); });
    connect(tilemapWidth_, qOverload<int>(&LuxUI::SpinBox::valueChanged), this,
            [this] { OnTilemapChanged(tilemapWidth_); });
    connect(tilemapHeight_, qOverload<int>(&LuxUI::SpinBox::valueChanged), this,
            [this] { OnTilemapChanged(tilemapHeight_); });
    connect(tilemapSize_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this] { OnTilemapChanged(tilemapSize_); });
    // 개발 중: 엔진에 아직 없는 원형·다각형 충돌체와 조인트는 편집 도구를 노출하지 않습니다.
    auto* physicsGroup = new LuxUI::GroupBox(LUX_UI_TEXT("Physics 2D"), inspectorBody_);
    auto* physicsLayout = new LuxUI::FormLayout(physicsGroup);
    bodyEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("물리 몸체 사용"), physicsGroup);
    bodyEnabled_->setObjectName(LUX_UI_TEXT("editorBody2D"));
    bodyType_ = new LuxUI::ComboBox(physicsGroup);
    bodyType_->addItems({LUX_UI_TEXT("고정"), LUX_UI_TEXT("동적"), LUX_UI_TEXT("키네마틱")});
    gravityScale_ = Number(physicsGroup, 0, 20, 1, .1);
    colliderEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("박스 충돌체 사용"), physicsGroup);
    colliderEnabled_->setObjectName(LUX_UI_TEXT("editorCollider2D"));
    triggerEnabled_ = new LuxUI::CheckBox(LUX_UI_TEXT("트리거"), physicsGroup);
    physicsLayout->addRow(bodyEnabled_);
    physicsLayout->addRow(LUX_UI_TEXT("몸체 유형"), bodyType_);
    physicsLayout->addRow(LUX_UI_TEXT("중력 배율"), gravityScale_);
    physicsLayout->addRow(colliderEnabled_);
    for (int axis = 0; axis < 2; ++axis) {
        colliderSize_[axis] = Number(physicsGroup, .02, 10000, 1, .1);
        colliderOffset_[axis] = Number(physicsGroup, -10000, 10000, 0, .1);
        physicsLayout->addRow(axis == 0 ? LUX_UI_TEXT("폭") : LUX_UI_TEXT("높이"),
                              colliderSize_[axis]);
        physicsLayout->addRow(axis == 0 ? LUX_UI_TEXT("X 오프셋") : LUX_UI_TEXT("Y 오프셋"),
                              colliderOffset_[axis]);
    }
    physicsLayout->addRow(triggerEnabled_);
    body->addWidget(physicsGroup);
    connect(bodyEnabled_, &LuxUI::CheckBox::toggled, this,
            [this] { OnPhysicsChanged(bodyEnabled_); });
    connect(bodyType_, &LuxUI::ComboBox::currentIndexChanged, this,
            [this] { OnPhysicsChanged(bodyType_); });
    connect(gravityScale_, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
            [this] { OnPhysicsChanged(gravityScale_); });
    connect(colliderEnabled_, &LuxUI::CheckBox::toggled, this,
            [this] { OnPhysicsChanged(colliderEnabled_); });
    connect(triggerEnabled_, &LuxUI::CheckBox::toggled, this,
            [this] { OnPhysicsChanged(triggerEnabled_); });
    for (auto* field : colliderSize_)
        connect(field, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
                [this, field] { OnPhysicsChanged(field); });
    for (auto* field : colliderOffset_)
        connect(field, qOverload<double>(&LuxUI::DoubleSpinBox::valueChanged), this,
                [this, field] { OnPhysicsChanged(field); });

    connect(name_, &LuxUI::LineEdit::editingFinished, this, [this] { OnNameChanged(); });
    BuildCameraDetails(body);
    BuildScriptDetails(body);
    BuildPhysics3DDetails(body);
    inspectorLayout->addWidget(inspectorBody_);
    inspectorLayout->addStretch();
    inspectorScroll->setWidget(inspector);
    inspectorScroll->setHorizontalScrollBarPolicy(LuxUI::Flags::ScrollBarAlwaysOff);

    LuxUI::VBoxLayout* toolsLayout;
    auto* toolsPanel = Panel(this, toolsLayout, 0);
    auto* toolsRow = new LuxUI::HBoxLayout;
    toolsRow->setContentsMargins(0, 0, 0, 0);
    toolsRow->setSpacing(0);
    auto* rail = new LuxUI::Widget(toolsPanel);
    rail->setObjectName(LUX_UI_TEXT("toolRail"));
    auto* railLayout = new LuxUI::VBoxLayout(rail);
    railLayout->setContentsMargins(3, 4, 3, 4);
    railLayout->setSpacing(3);
    auto* toolGroup = new LuxUI::ButtonGroup(rail);
    toolGroup->setExclusive(true);
    const std::array<std::pair<EditorIcon, const char*>, 6> modes{
        {{EditorIcon::Select, "선택 (Q)"},
         {EditorIcon::Move, "이동 (W)"},
         {EditorIcon::Rotate, "회전 (E)"},
         {EditorIcon::Scale, "크기 (R)"},
         {EditorIcon::Brush, "타일 칠하기 (B)"},
         {EditorIcon::Eraser, "타일 지우기 (X)"}}};
    for (size_t index = 0; index < modes.size(); ++index) {
        auto* button = new LuxUI::ToolButton(rail);
        button->setIcon(MakeEditorIcon(modes[index].first));
        button->setIconSize(LuxUI::Size(22, 22));
        button->setFixedSize(34, 34);
        button->setToolTip(LuxUI::String::fromUtf8(modes[index].second));
        button->setCheckable(true);
        button->setChecked(index == 0);
        toolButtons_[index] = button;
        toolGroup->addButton(button, static_cast<int>(index));
        railLayout->addWidget(button);
        connect(button, &LuxUI::ToolButton::clicked, this,
                [this, index] { SetTool(static_cast<EditorTool>(index)); });
    }
    railLayout->addStretch();
    auto* focusTool = new LuxUI::ToolButton(rail);
    focusTool->setIcon(MakeEditorIcon(EditorIcon::Focus));
    focusTool->setIconSize(LuxUI::Size(22, 22));
    focusTool->setFixedSize(34, 34);
    focusTool->setToolTip(LUX_UI_TEXT("선택 항목에 초점 (F)"));
    railLayout->addWidget(focusTool);
    connect(focusTool, &LuxUI::ToolButton::clicked, focus, &LuxUI::ToolButton::click);
    toolsRow->addWidget(rail);
    auto* toolProperties = new LuxUI::Widget(toolsPanel);
    auto* propertiesLayout = new LuxUI::VBoxLayout(toolProperties);
    propertiesLayout->setContentsMargins(7, 7, 7, 7);
    propertiesLayout->addWidget(Caption(LUX_UI_TEXT("배치 도구"), toolProperties));
    propertiesLayout->addWidget(new LuxUI::Label(LUX_UI_TEXT("오브젝트 편집"), toolProperties));
    toolHint_ = new LuxUI::Label(LUX_UI_TEXT("선택"), toolProperties);
    toolHint_->setObjectName(LUX_UI_TEXT("toolHelp"));
    toolHint_->setWordWrap(true);
    toolHint_->setAlignment(LuxUI::Flags::AlignTop);
    propertiesLayout->addWidget(toolHint_);
    tilePalette_ = new LuxUI::ComboBox(toolProperties);
    tilePalette_->addItems({LUX_UI_TEXT("풀"), LUX_UI_TEXT("흙"), LUX_UI_TEXT("돌"),
                            LUX_UI_TEXT("물"), LUX_UI_TEXT("모래")});
    tilePalette_->setToolTip(LUX_UI_TEXT("2D 타일 붓 색상"));
    propertiesLayout->addWidget(tilePalette_);
    connect(tilePalette_, &LuxUI::ComboBox::currentIndexChanged, this, [this](int index) {
        tileId_ = static_cast<uint8_t>(index + 1);
        canvas_->SetTileId(tileId_);
    });
    propertiesLayout->addStretch();
    toolsRow->addWidget(toolProperties, 1);
    toolsLayout->addLayout(toolsRow, 1);

    overviewCanvas_ =
        new SceneViewport2D(this, document_, select, transform, paintTile, begin, end);
    overviewCanvas_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    overviewCanvas_->setObjectName(LUX_UI_TEXT("topViewport2D"));
    overviewCanvas_->SetTool(EditorTool::Select);
    consoleOutput_ = new LuxUI::PlainTextEdit(this);
    consoleOutput_->setReadOnly(true);
    consoleOutput_->setObjectName(LUX_UI_TEXT("editorConsole"));
    consoleOutput_->setMaximumBlockCount(2000);
    issuesOutput_ = new LuxUI::PlainTextEdit(this);
    issuesOutput_->setReadOnly(true);
    issuesOutput_->setObjectName(LUX_UI_TEXT("editorProblems"));
    issuesOutput_->setPlaceholderText(LUX_UI_TEXT("오류와 경고가 여기에 표시됩니다."));
    debugPanel_ = new DebugPanel(this, [this](LuxEngine::uint32 id) { SelectEntity(id); });

    const std::array<std::pair<LuxUI::String, LuxUI::Widget*>, 8> panels{
        {{LUX_UI_TEXT("Outliner"), hierarchyPanel},
         {LUX_UI_TEXT("Content Browser"), assetsPanel},
         {LUX_UI_TEXT("Details"), inspectorScroll},
         {LUX_UI_TEXT("Console"), consoleOutput_},
         {LUX_UI_TEXT("Problems"), issuesOutput_},
         {LUX_UI_TEXT("Place Actors"), toolsPanel},
         {LUX_UI_TEXT("Top View"), overviewCanvas_},
         {LUX_UI_TEXT("Debugger"), debugPanel_}}};
    for (size_t index = 0; index < panels.size(); ++index) {
        auto* dock = new LuxUI::DockWidget(panels[index].first, this);
        dock->setObjectName(LUX_UI_TEXT("lux-dock-%1").arg(index));
        dock->setAllowedAreas(LuxUI::Flags::AllDockWidgetAreas);
        dock->setFeatures(LuxUI::DockWidget::DockWidgetMovable |
                          LuxUI::DockWidget::DockWidgetFloatable |
                          LuxUI::DockWidget::DockWidgetClosable);
        dock->setWidget(panels[index].second);
        docks_[index] = dock;
        const auto area = index == 0 || index == 2 ? LuxUI::Flags::RightDockWidgetArea
                          : index == 5             ? LuxUI::Flags::LeftDockWidgetArea
                                                   : LuxUI::Flags::BottomDockWidgetArea;
        addDockWidget(area, dock);
    }
    splitDockWidget(docks_[0], docks_[2], LuxUI::Flags::Vertical);
    for (size_t index : {size_t{3}, size_t{4}, size_t{7}})
        tabifyDockWidget(docks_[1], docks_[index]);
    for (size_t index : {size_t{1}, size_t{3}, size_t{4}, size_t{5}, size_t{6}, size_t{7}})
        docks_[index]->hide();
    resizeDocks({docks_[0], docks_[2]}, {230, 520}, LuxUI::Flags::Vertical);
    resizeDocks({docks_[0]}, {360}, LuxUI::Flags::Horizontal);
    BuildAnimationPanel();

    auto* toolbar = addToolBar(LUX_UI_TEXT("편집 도구"));
    toolbar->setObjectName(LUX_UI_TEXT("lux-main-toolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(LuxUI::Size(20, 20));
    toolbar->setToolButtonStyle(LuxUI::Flags::ToolButtonIconOnly);
    auto iconAction = [this, toolbar](EditorIcon icon, const LuxUI::String& title,
                                      const std::function<void()>& action) {
        LuxUI::Action* command = toolbar->addAction(MakeEditorIcon(icon), title);
        command->setToolTip(title);
        connect(command, &LuxUI::Action::triggered, this, [action] { action(); });
        return command;
    };
    iconAction(EditorIcon::Save, LUX_UI_TEXT("장면 저장"), [this] { SaveScene(false); });
    toolbar->addSeparator();
    auto* create = new LuxUI::ToolButton(toolbar);
    create->setObjectName(LUX_UI_TEXT("placeActorsButton"));
    create->setText(LUX_UI_TEXT("추가"));
    create->setIcon(MakeEditorIcon(EditorIcon::Sprite));
    create->setToolButtonStyle(LuxUI::Flags::ToolButtonTextBesideIcon);
    create->setPopupMode(LuxUI::ToolButton::InstantPopup);
    auto* createMenu = new LuxUI::Menu(create);
    createMenu->addAction(MakeEditorIcon(EditorIcon::Sprite), LUX_UI_TEXT("스프라이트"), this,
                          [this] { AddEntity(); });
    createMenu->addAction(MakeEditorIcon(EditorIcon::Mesh), LUX_UI_TEXT("메시"), this,
                          [this] { AddMeshEntity(); });
    AddBasicModelMenu(createMenu);
    createMenu->addAction(MakeEditorIcon(EditorIcon::Grid), LUX_UI_TEXT("타일맵"), this,
                          [this] { AddTilemap(); });
    createMenu
        ->addAction(MakeEditorIcon(EditorIcon::Focus), LUX_UI_TEXT("카메라"), this,
                    [this] { AddCameraEntity(); })
        ->setObjectName(LUX_UI_TEXT("addCamera"));
    create->setMenu(createMenu);
    toolbar->addWidget(create);
    toolbar->addSeparator();
    auto* actionGroup = new LuxUI::ActionGroup(toolbar);
    actionGroup->setExclusive(true);
    const std::array<std::pair<EditorIcon, int>, 6> toolbarTools{
        {{EditorIcon::Select, LuxUI::Flags::Key_Q},
         {EditorIcon::Move, LuxUI::Flags::Key_W},
         {EditorIcon::Rotate, LuxUI::Flags::Key_E},
         {EditorIcon::Scale, LuxUI::Flags::Key_R},
         {EditorIcon::Brush, LuxUI::Flags::Key_B},
         {EditorIcon::Eraser, LuxUI::Flags::Key_X}}};
    for (size_t index = 0; index < toolbarTools.size(); ++index) {
        LuxUI::Action* action = toolbar->addAction(MakeEditorIcon(toolbarTools[index].first),
                                                   LuxUI::String::fromUtf8(modes[index].second));
        action->setCheckable(true);
        action->setChecked(index == 0);
        action->setShortcut(LuxUI::KeySequence(toolbarTools[index].second));
        actionGroup->addAction(action);
        toolActions_[index] = action;
        connect(action, &LuxUI::Action::triggered, this,
                [this, index] { SetTool(static_cast<EditorTool>(index)); });
    }
    toolbar->addSeparator();
    previewStartAction_ =
        iconAction(EditorIcon::Play, LUX_UI_TEXT("장면 실행 (F5)"), [this] { StartPreview(); });
    previewPauseAction_ = iconAction(EditorIcon::Pause, LUX_UI_TEXT("일시정지/재개 (F6)"),
                                     [this] { TogglePreviewPause(); });
    previewStepAction_ =
        iconAction(EditorIcon::Step, LUX_UI_TEXT("한 프레임 실행 (F7)"), [this] { StepPreview(); });
    previewStopAction_ = iconAction(EditorIcon::Stop, LUX_UI_TEXT("실행 중지 (Shift+F5)"),
                                    [this] { StopPreview(); });
    toolbar->addSeparator();
    toolbar->addSeparator();
    auto* spacer = new LuxUI::Widget(toolbar);
    spacer->setSizePolicy(LuxUI::SizePolicy::Expanding, LuxUI::SizePolicy::Preferred);
    toolbar->addWidget(spacer);
    projectLabel_ = Caption(LUX_UI_TEXT("프로젝트 없음"), toolbar);
    toolbar->addWidget(projectLabel_);
    auto* assetSearch = new LuxUI::LineEdit(toolbar);
    assetSearch->setPlaceholderText(LUX_UI_TEXT("에셋 검색"));
    assetSearch->setMaximumWidth(160);
    toolbar->addWidget(assetSearch);
    connect(assetSearch, &LuxUI::LineEdit::textChanged, projectSearch_, &LuxUI::LineEdit::setText);
    connect(projectSearch_, &LuxUI::LineEdit::textChanged, assetSearch, &LuxUI::LineEdit::setText);
    auto* focusSearch = new LuxUI::Action(this);
    focusSearch->setShortcut(LuxUI::KeySequence(LuxUI::Flags::CTRL | LuxUI::Flags::Key_K));
    addAction(focusSearch);
    connect(focusSearch, &LuxUI::Action::triggered, assetSearch, [assetSearch] {
        assetSearch->setFocus();
        assetSearch->selectAll();
    });

    BuildWorkspaceChrome();
    defaultLayout_ = saveState(5);
    previewPauseAction_->setEnabled(false);
    previewStepAction_->setEnabled(false);
    previewStopAction_->setEnabled(false);
    ApplyTheme();
    RestoreWorkspaceLayout();
}

void EditorWindow::RestoreWorkspaceLayout() {
    LuxUI::Settings settings;
    theme_ = settings.value(LUX_UI_TEXT("theme"), LUX_UI_TEXT("dark")).toString();
    uiScale_ = std::clamp(settings.value(LUX_UI_TEXT("uiScale"), 1.0).toDouble(), .75, 2.0);
    ApplyTheme();
    restoreGeometry(settings.value(LUX_UI_TEXT("geometry")).toByteArray());
    const auto state = settings.value(LUX_UI_TEXT("layout")).toByteArray();
    if (!state.isEmpty() && !restoreState(state, 5))
        restoreState(defaultLayout_, 5);
}

void EditorWindow::SaveWorkspaceLayout() {
    LuxUI::Settings settings;
    settings.setValue(LUX_UI_TEXT("theme"), theme_);
    settings.setValue(LUX_UI_TEXT("uiScale"), uiScale_);
    settings.setValue(LUX_UI_TEXT("geometry"), saveGeometry());
    settings.setValue(LUX_UI_TEXT("layout"), levelLayout_.isEmpty() ? saveState(5) : levelLayout_);
    if (project_)
        settings.setValue(LUX_UI_TEXT("lastProject"),
                          LuxUI::String::fromUtf8(PathUtf8(project_->GetFile())));
    else
        settings.remove(LUX_UI_TEXT("lastProject"));
    settings.setValue(LUX_UI_TEXT("lastScene"), LuxUI::String::fromUtf8(document_.GetPath()));
    LuxUI::JsonArray scenes;
    for (const auto& tab : document_.ListTabs()) {
        if (tab.path.empty())
            continue;
        LuxUI::JsonObject record;
        record.insert(LUX_UI_TEXT("path"), LuxUI::String::fromUtf8(tab.path));
        const auto context = sceneProjects_.find(tab.id);
        if (context != sceneProjects_.end() && context->second)
            record.insert(LUX_UI_TEXT("project"),
                          LuxUI::String::fromUtf8(PathUtf8(context->second->GetFile())));
        scenes.push_back(record);
    }
    settings.setValue(LUX_UI_TEXT("openSceneDocuments"),
                      LuxUI::JsonDocument(scenes).toJson(LuxUI::JsonDocument::Compact));
}

void EditorWindow::ResetWorkspaceLayout() {
    SetWorkspace(false);
    restoreState(defaultLayout_, 5);
    docks_[0]->show();
    docks_[2]->show();
    for (size_t index : {size_t{1}, size_t{3}, size_t{4}, size_t{5}, size_t{6}, size_t{7}})
        docks_[index]->hide();
    statusBar()->showMessage(LUX_UI_TEXT("기본 작업 공간 배치를 복원했습니다."), 4000);
}
} // namespace LuxEditor
