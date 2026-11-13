#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Debug/DebugPanel.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/SceneHierarchy.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/ECS/ECS.hpp>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace LuxEditor {
namespace {
QWidget* Panel(QWidget* parent, QVBoxLayout*& layout, int margin = 6) {
    auto* panel = new QWidget(parent);
    layout = new QVBoxLayout(panel);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(5);
    return panel;
}

QLabel* Caption(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionCaption"));
    return label;
}

QDoubleSpinBox* Number(QWidget* parent, double low, double high, double value, double step) {
    auto* control = new QDoubleSpinBox(parent);
    control->setRange(low, high);
    control->setDecimals(3);
    control->setSingleStep(step);
    control->setValue(value);
    control->setButtonSymbols(QAbstractSpinBox::NoButtons);
    control->setKeyboardTracking(false);
    control->setMinimumWidth(56);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    control->setToolTip(control->text());
    QObject::connect(control, qOverload<double>(&QDoubleSpinBox::valueChanged), control,
                     [control] { control->setToolTip(control->text()); });
    return control;
}
} // namespace

void EditorWindow::BuildLayout() {
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    const auto select = [this](std::optional<LuxEngine::uint32> id) { SelectEntity(id); };
    const auto transform = [this](LuxEngine::uint32 id,
                                  const LuxEngine::TransformComponent& value) {
        ApplyViewportTransform(id, value);
    };
    const auto begin = [this] { BeginViewportEdit(); };
    const auto end = [this] { EndViewportEdit(); };
    const auto paintTile = [this](LuxEngine::uint32 id, LuxEngine::uint32 x, LuxEngine::uint32 y,
                                  uint8_t tile) { PaintTile(id, x, y, tile); };

    auto* sceneFrame = new QWidget(this);
    auto* sceneLayout = new QVBoxLayout(sceneFrame);
    sceneLayout->setContentsMargins(0, 0, 0, 0);
    sceneLayout->setSpacing(0);
    sceneTabs_ = new QTabBar(sceneFrame);
    sceneTabs_->setObjectName(QStringLiteral("sceneDocuments"));
    sceneTabs_->setExpanding(false);
    sceneTabs_->setTabsClosable(true);
    sceneTabs_->setUsesScrollButtons(true);
    sceneTabs_->setElideMode(Qt::ElideMiddle);
    sceneLayout->addWidget(sceneTabs_);
    sceneLoadProgress_ = new QProgressBar(sceneFrame);
    sceneLoadProgress_->setObjectName(QStringLiteral("sceneLoadProgress"));
    sceneLoadProgress_->setRange(0, 0);
    sceneLoadProgress_->setTextVisible(false);
    sceneLoadProgress_->setFixedHeight(3);
    sceneLoadProgress_->hide();
    sceneLayout->addWidget(sceneLoadProgress_);
    connect(sceneTabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (!refreshingSceneTabs_ && index >= 0)
            SwitchSceneDocument(sceneTabs_->tabData(index).toULongLong());
    });
    connect(sceneTabs_, &QTabBar::tabCloseRequested, this, [this](int index) {
        if (index >= 0)
            RequestCloseScene(sceneTabs_->tabData(index).toULongLong());
    });
    auto* sceneBar = new QWidget(sceneFrame);
    sceneBar->setObjectName(QStringLiteral("sceneBar"));
    auto* sceneBarLayout = new QHBoxLayout(sceneBar);
    sceneBarLayout->setContentsMargins(8, 2, 8, 2);
    sceneBarLayout->setSpacing(8);
    auto* viewChoice = new QComboBox(sceneBar);
    viewChoice->addItems({QStringLiteral("3D"), QStringLiteral("2D")});
    viewChoice->setMinimumWidth(60);
    sceneBarLayout->addWidget(viewChoice);
    auto* projection = new QComboBox(sceneBar);
    projection->setObjectName(QStringLiteral("viewportProjection"));
    projection->addItems({QStringLiteral("원근"), QStringLiteral("정면"), QStringLiteral("오른쪽"),
                          QStringLiteral("상단")});
    sceneBarLayout->addWidget(projection);
    connect(projection, &QComboBox::currentIndexChanged, this, [this](int index) {
        canvas3D_->SetViewPreset(static_cast<SceneViewport3D::ViewPreset>(index));
    });
    connect(viewChoice, &QComboBox::currentIndexChanged, projection,
            [projection](int index) { projection->setVisible(index == 0); });
    auto* space = new QComboBox(sceneBar);
    space->setObjectName(QStringLiteral("transformSpace"));
    space->addItems({QStringLiteral("월드"), QStringLiteral("로컬")});
    space->setToolTip(QStringLiteral("기즈모 좌표계"));
    sceneBarLayout->addWidget(space);
    auto* pivot = new QComboBox(sceneBar);
    pivot->setObjectName(QStringLiteral("selectionPivot"));
    pivot->addItems(
        {QStringLiteral("중앙 피벗"), QStringLiteral("활성 피벗"), QStringLiteral("개별 피벗")});
    pivot->setToolTip(QStringLiteral("다중 선택 회전·크기 조절의 기준점"));
    sceneBarLayout->addWidget(pivot);
    const auto cancelGesture = [this] {
        canvas_->CancelInteraction();
        canvas3D_->CancelInteraction();
        overviewCanvas_->CancelInteraction();
        CancelViewportEdit();
    };
    connect(space, &QComboBox::currentIndexChanged, this, [this, cancelGesture](int index) {
        cancelGesture();
        document_.SetTransformSpace(static_cast<TransformSpace>(index));
        RefreshSelection(false);
    });
    connect(pivot, &QComboBox::currentIndexChanged, this, [this, cancelGesture](int index) {
        cancelGesture();
        document_.SetSelectionPivotMode(static_cast<SelectionPivot>(index));
        RefreshSelection(false);
    });
    sceneBarLayout->addStretch();
    auto* gridVisibility = new QToolButton(sceneBar);
    gridVisibility->setObjectName(QStringLiteral("viewportGridToggle"));
    gridVisibility->setIcon(MakeEditorIcon(EditorIcon::Grid));
    gridVisibility->setToolTip(QStringLiteral("격자 표시"));
    gridVisibility->setCheckable(true);
    gridVisibility->setChecked(true);
    sceneBarLayout->addWidget(gridVisibility);
    connect(gridVisibility, &QToolButton::toggled, this, [this](bool visible) {
        canvas_->SetGridVisible(visible);
        canvas3D_->SetGridVisible(visible);
        overviewCanvas_->SetGridVisible(visible);
    });
    auto* focus = new QToolButton(sceneBar);
    focus->setIcon(MakeEditorIcon(EditorIcon::Focus));
    focus->setToolTip(QStringLiteral("선택 항목에 초점 (F)"));
    sceneBarLayout->addWidget(focus);
    connect(focus, &QToolButton::clicked, this, [this] {
        if (viewportTabs_->currentWidget() == canvas3D_)
            canvas3D_->FocusSelection();
        else
            canvas_->FocusSelection();
    });
    sceneLayout->addWidget(sceneBar);
    viewportTabs_ = new QTabWidget(sceneFrame);
    viewportTabs_->tabBar()->hide();
    auto sharedRenderer = std::make_shared<EngineSceneRenderer>();
    canvas3D_ = new SceneViewport3D(viewportTabs_, document_, select, transform, begin, end,
                                    sharedRenderer);
    canvas_ = new SceneViewport2D(viewportTabs_, document_, select, transform, paintTile, begin,
                                  end, sharedRenderer);
    canvas_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    canvas3D_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    canvas_->setObjectName(QStringLiteral("sceneViewport2D"));
    canvas3D_->setObjectName(QStringLiteral("sceneViewport3D"));
    viewportTabs_->addTab(canvas3D_, QStringLiteral("3D 장면"));
    viewportTabs_->addTab(canvas_, QStringLiteral("2D 장면"));
    sceneLayout->addWidget(viewportTabs_, 1);
    workspaceStack_ = new QStackedWidget(this);
    workspaceStack_->setObjectName(QStringLiteral("editorWorkspace"));
    workspaceStack_->addWidget(sceneFrame);
    setCentralWidget(workspaceStack_);
    connect(viewChoice, &QComboBox::currentIndexChanged, viewportTabs_,
            &QTabWidget::setCurrentIndex);
    connect(viewportTabs_, &QTabWidget::currentChanged, viewChoice, &QComboBox::setCurrentIndex);
    connect(viewportTabs_, &QTabWidget::currentChanged, this, [this] { UpdateChrome(); });

    QVBoxLayout* hierarchyLayout;
    auto* hierarchyPanel = Panel(this, hierarchyLayout);
    groupTree_ = new QTreeWidget(hierarchyPanel);
    groupTree_->setHeaderLabels({QStringLiteral("장면 그룹"), QStringLiteral("개수")});
    groupTree_->setRootIsDecorated(false);
    groupTree_->setFixedHeight(100);
    for (const QString& name :
         {QStringLiteral("전체"), QStringLiteral("2D 스프라이트"), QStringLiteral("3D 메시")}) {
        auto* item = new QTreeWidgetItem(groupTree_);
        item->setText(0, name);
        item->setTextAlignment(1, Qt::AlignRight);
    }
    groupTree_->setCurrentItem(groupTree_->topLevelItem(0));
    groupTree_->hide();
    auto* objectFilter = new QComboBox(hierarchyPanel);
    objectFilter->setObjectName(QStringLiteral("outlinerFilter"));
    objectFilter->addItems(
        {QStringLiteral("모든 오브젝트"), QStringLiteral("스프라이트"), QStringLiteral("메시")});
    connect(objectFilter, &QComboBox::currentIndexChanged, this,
            [this](int index) { groupTree_->setCurrentItem(groupTree_->topLevelItem(index)); });
    hierarchyLayout->addWidget(objectFilter);
    hierarchySearch_ = new QLineEdit(hierarchyPanel);
    hierarchySearch_->setPlaceholderText(QStringLiteral("엔티티 이름 검색"));
    hierarchyLayout->addWidget(hierarchySearch_);
    auto* tree = new SceneHierarchy(hierarchyPanel);
    tree->reparent = [this](const std::vector<LuxEngine::uint32>& children,
                            LuxEngine::uint32 parent) { ReparentSelection(children, parent); };
    hierarchy_ = tree;
    hierarchy_->setObjectName(QStringLiteral("sceneOutliner"));
    hierarchy_->setContextMenuPolicy(Qt::CustomContextMenu);
    hierarchyLayout->addWidget(hierarchy_, 1);
    auto* entityActions = new QHBoxLayout;
    auto* add2D = new QToolButton(hierarchyPanel);
    add2D->setIcon(MakeEditorIcon(EditorIcon::Sprite));
    add2D->setText(QStringLiteral("2D"));
    add2D->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add2D->setToolTip(QStringLiteral("2D 엔티티 추가"));
    auto* add3D = new QToolButton(hierarchyPanel);
    add3D->setIcon(MakeEditorIcon(EditorIcon::Mesh));
    add3D->setText(QStringLiteral("3D"));
    add3D->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add3D->setToolTip(QStringLiteral("3D 메시 엔티티 추가"));
    auto* remove = new QToolButton(hierarchyPanel);
    remove->setIcon(MakeEditorIcon(EditorIcon::Delete));
    remove->setToolTip(QStringLiteral("선택 엔티티 삭제 (Delete)"));
    entityActions->addWidget(add2D);
    entityActions->addWidget(add3D);
    entityActions->addStretch();
    entityActions->addWidget(remove);
    hierarchyLayout->addLayout(entityActions);
    add2D->hide();
    add3D->hide();
    remove->hide();
    connect(groupTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        groupFilter_ = item ? groupTree_->indexOfTopLevelItem(item) : 0;
        RefreshHierarchy();
    });
    connect(hierarchySearch_, &QLineEdit::textChanged, this, [this] { RefreshHierarchy(); });
    connect(hierarchy_, &QTreeWidget::itemSelectionChanged, this,
            &EditorWindow::SyncHierarchySelection);
    connect(hierarchy_, &QTreeWidget::currentItemChanged, this,
            [this] { SyncHierarchySelection(); });
    connect(
        hierarchy_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& point) {
            QMenu menu(this);
            menu.addAction(MakeEditorIcon(EditorIcon::Sprite), QStringLiteral("2D 엔티티 추가"),
                           this, [this] { AddEntity(); });
            menu.addAction(MakeEditorIcon(EditorIcon::Mesh), QStringLiteral("3D 메시 엔티티 추가"),
                           this, [this] { AddMeshEntity(); });
            menu.addSeparator();
            menu.addAction(QStringLiteral("복사"), this, [this] { CopyEntity(); });
            menu.addAction(QStringLiteral("붙여넣기"), this, [this] { PasteEntity(); });
            menu.addAction(QStringLiteral("선택 삭제"), this, [this] { DeleteEntity(); });
            menu.addSeparator();
            for (const char* id :
                 {"groupEntities", "ungroupEntities", "detachEntity", "moveEntityToScene"})
                if (auto* action = findChild<QAction*>(QString::fromLatin1(id)))
                    menu.addAction(action);
            menu.exec(hierarchy_->mapToGlobal(point));
        });
    connect(add2D, &QToolButton::clicked, this, [this] { AddEntity(); });
    connect(add3D, &QToolButton::clicked, this, [this] { AddMeshEntity(); });
    connect(remove, &QToolButton::clicked, this, [this] { DeleteEntity(); });

    QVBoxLayout* assetsLayout;
    auto* assetsPanel = Panel(this, assetsLayout, 4);
    auto* assetHeader = new QHBoxLayout;
    auto* rescan = new QToolButton(assetsPanel);
    rescan->setIcon(MakeEditorIcon(EditorIcon::Folder));
    rescan->setText(QStringLiteral("폴더 새로 고침"));
    rescan->setToolTip(QStringLiteral("프로젝트 콘텐츠 다시 검색"));
    connect(rescan, &QToolButton::clicked, this, [this] { RefreshProjectExplorer(); });
    assetHeader->addWidget(rescan);
    assetBreadcrumb_ = new QLabel(QStringLiteral("모든 콘텐츠"), assetsPanel);
    assetBreadcrumb_->setObjectName(QStringLiteral("assetBreadcrumb"));
    assetHeader->addWidget(assetBreadcrumb_, 1);
    assetHeader->addStretch();
    assetFilter_ = new QComboBox(assetsPanel);
    assetFilter_->addItems({QStringLiteral("전체"), QStringLiteral("장면"),
                            QStringLiteral("이미지"), QStringLiteral("오디오"),
                            QStringLiteral("기타")});
    assetHeader->addWidget(assetFilter_);
    auto* gridMode = new QToolButton(assetsPanel);
    gridMode->setIcon(MakeEditorIcon(EditorIcon::Grid));
    gridMode->setToolTip(QStringLiteral("격자로 보기"));
    auto* listMode = new QToolButton(assetsPanel);
    listMode->setIcon(MakeEditorIcon(EditorIcon::List));
    listMode->setToolTip(QStringLiteral("목록으로 보기"));
    assetHeader->addWidget(gridMode);
    assetHeader->addWidget(listMode);
    assetsLayout->addLayout(assetHeader);
    projectSearch_ = new QLineEdit(assetsPanel);
    projectSearch_->setPlaceholderText(QStringLiteral("에셋 이름 검색"));
    assetsLayout->addWidget(projectSearch_);
    auto* assetSplit = new QSplitter(Qt::Horizontal, assetsPanel);
    assetSplit->setObjectName(QStringLiteral("contentBrowserSplitter"));
    assetFolders_ = new QTreeWidget(assetSplit);
    assetFolders_->setObjectName(QStringLiteral("assetFolders"));
    assetFolders_->setHeaderHidden(true);
    assetFolders_->setMinimumWidth(140);
    assetFolders_->setMaximumWidth(300);
    connect(assetFolders_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        assetFolder_ =
            item ? PathFromUtf8(item->data(0, Qt::UserRole).toString().toUtf8().toStdString())
                 : std::filesystem::path{};
        RefreshProjectExplorer();
    });
    projectFiles_ = new QListWidget(assetSplit);
    projectFiles_->setObjectName(QStringLiteral("assetGrid"));
    projectFiles_->setViewMode(QListView::IconMode);
    projectFiles_->setIconSize(QSize(64, 64));
    projectFiles_->setGridSize(QSize(112, 100));
    projectFiles_->setResizeMode(QListView::Adjust);
    projectFiles_->setWordWrap(true);
    projectFiles_->setSpacing(3);
    auto* previewPanel = new QWidget(assetSplit);
    assetPreviewPanel_ = previewPanel;
    auto* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(4, 4, 4, 4);
    previewLayout->addWidget(Caption(QStringLiteral("PREVIEW"), previewPanel));
    assetPreview_ = new QLabel(previewPanel);
    assetPreview_->setAlignment(Qt::AlignCenter);
    assetPreview_->setMinimumWidth(90);
    assetPreview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    assetDetails_ = new QLabel(previewPanel);
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
    assetCount_ = new QLabel(assetsPanel);
    assetCount_->setObjectName(QStringLiteral("assetCount"));
    assetsLayout->addWidget(assetCount_);
    connect(projectSearch_, &QLineEdit::textChanged, this, [this] { RefreshProjectExplorer(); });
    connect(assetFilter_, &QComboBox::currentIndexChanged, this,
            [this] { RefreshProjectExplorer(); });
    connect(gridMode, &QToolButton::clicked, this, [this] {
        projectFiles_->setViewMode(QListView::IconMode);
        projectFiles_->setGridSize(QSize(120, 118));
    });
    connect(listMode, &QToolButton::clicked, this, [this] {
        projectFiles_->setViewMode(QListView::ListMode);
        projectFiles_->setGridSize(QSize());
    });
    connect(projectFiles_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || static_cast<size_t>(row) >= projectFilePaths_.size()) {
            assetPreview_->clear();
            assetDetails_->clear();
            return;
        }
        const auto& path = projectFilePaths_[static_cast<size_t>(row)];
        QPixmap image(QString::fromUtf8(PathUtf8(path)));
        if (!image.isNull()) {
            assetPreview_->setPixmap(
                image.scaled(std::max(1, std::min(220, assetPreview_->width())),
                             std::max(1, std::min(180, assetPreview_->height())),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            assetPreview_->setPixmap(QFileIconProvider()
                                         .icon(QFileInfo(QString::fromUtf8(PathUtf8(path))))
                                         .pixmap(64, 64));
        }
        QFileInfo info(QString::fromUtf8(PathUtf8(path)));
        assetDetails_->setText(
            QStringLiteral("%1\n%2 KB").arg(info.fileName()).arg((info.size() + 1023) / 1024));
    });
    connect(projectFiles_, &QListWidget::itemDoubleClicked, this, [this] {
        const int row = projectFiles_->currentRow();
        if (row >= 0 && static_cast<size_t>(row) < projectFilePaths_.size() &&
            projectFilePaths_[static_cast<size_t>(row)].extension() == ".lux")
            OpenProjectScene(projectFilePaths_[static_cast<size_t>(row)]);
    });

    auto* inspectorScroll = new QScrollArea(this);
    inspectorScroll->setObjectName(QStringLiteral("inspectorScroll"));
    inspectorScroll->setMinimumWidth(290);
    inspectorScroll->setWidgetResizable(true);
    auto* inspector = new QWidget(inspectorScroll);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(7, 7, 7, 7);
    inspectorLayout->setSpacing(5);
    detailsSearch_ = new QLineEdit(inspector);
    detailsSearch_->setObjectName(QStringLiteral("detailsSearch"));
    detailsSearch_->setPlaceholderText(QStringLiteral("속성 검색"));
    inspectorLayout->addWidget(detailsSearch_);
    connect(detailsSearch_, &QLineEdit::textChanged, this, &EditorWindow::FilterDetails);
    selectionLabel_ = Caption(QStringLiteral("선택된 엔티티 없음"), inspector);
    inspectorLayout->addWidget(selectionLabel_);
    inspectorBody_ = new QWidget(inspector);
    auto* body = new QVBoxLayout(inspectorBody_);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(8);
    auto* nameRow = new QFormLayout;
    name_ = new QLineEdit(inspectorBody_);
    nameRow->addRow(QStringLiteral("이름"), name_);
    body->addLayout(nameRow);
    sceneReferencePanel_ = new QGroupBox(QStringLiteral("씬 참조"), inspectorBody_);
    sceneReferencePanel_->setObjectName(QStringLiteral("sceneReferencePanel"));
    auto* referenceLayout = new QVBoxLayout(sceneReferencePanel_);
    sceneReferencePath_ = new QLineEdit(sceneReferencePanel_);
    sceneReferencePath_->setObjectName(QStringLiteral("sceneReferencePath"));
    sceneReferencePath_->setReadOnly(true);
    referenceLayout->addWidget(sceneReferencePath_);
    sceneReferenceState_ = new QLabel(sceneReferencePanel_);
    referenceLayout->addWidget(sceneReferenceState_);
    auto* referenceButtons = new QHBoxLayout;
    auto* openReference = new QPushButton(QStringLiteral("열기"), sceneReferencePanel_);
    auto* relinkReference = new QPushButton(QStringLiteral("경로 변경"), sceneReferencePanel_);
    referenceButtons->addWidget(openReference);
    referenceButtons->addWidget(relinkReference);
    referenceLayout->addLayout(referenceButtons);
    connect(openReference, &QPushButton::clicked, this, [this] {
        const auto selected = document_.GetSelected();
        if (selected && selected.HasComponent<LuxEngine::SceneReferenceComponent>())
            OpenSceneDocument(
                PathFromUtf8(selected.GetComponent<LuxEngine::SceneReferenceComponent>().path));
    });
    connect(relinkReference, &QPushButton::clicked, this, [this] { RelinkSceneReference(); });
    body->addWidget(sceneReferencePanel_);
    auto* transformGroup = new QGroupBox(QStringLiteral("Transform"), inspectorBody_);
    auto* grid = new QGridLayout(transformGroup);
    grid->setContentsMargins(6, 8, 6, 6);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(3);
    const char* rows[] = {"위치", "회전 °", "크기"};
    const char* axisColors[] = {"#c76168", "#63a979", "#6294c8"};
    for (int row = 0; row < 3; ++row) {
        grid->addWidget(new QLabel(QString::fromUtf8(rows[row]), transformGroup), row, 0);
        for (int axis = 0; axis < 3; ++axis) {
            transform_[row][axis] =
                Number(transformGroup, -100000, 100000, row == 2 ? 1 : 0, row == 1 ? 1 : .1);
            transform_[row][axis]->setToolTip(QStringLiteral("%1 · %2").arg(
                QString::fromUtf8(rows[row]), QString::fromLatin1("XYZ" + axis, 1)));
            transform_[row][axis]->setStyleSheet(
                QStringLiteral("QDoubleSpinBox{border-left:3px solid %1;}")
                    .arg(QString::fromLatin1(axisColors[axis])));
            grid->addWidget(transform_[row][axis], row, axis + 1);
            grid->setColumnStretch(axis + 1, 1);
            connect(transform_[row][axis], qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [this, row, axis] { OnTransformChanged(row, axis); });
        }
    }
    body->addWidget(transformGroup);
    auto* spriteGroup = new QGroupBox(QStringLiteral("Sprite Renderer"), inspectorBody_);
    auto* spriteLayout = new QVBoxLayout(spriteGroup);
    spriteEnabled_ = new QCheckBox(QStringLiteral("스프라이트 사용"), spriteGroup);
    spriteEnabled_->setObjectName(QStringLiteral("editorSprite"));
    spriteLayout->addWidget(spriteEnabled_);
    auto* colors = new QGridLayout;
    for (int channel = 0; channel < 4; ++channel) {
        const int row = channel / 2, col = channel % 2;
        colors->addWidget(new QLabel(QString::fromLatin1("RGBA" + channel, 1), spriteGroup),
                          row * 2, col);
        color_[channel] = Number(spriteGroup, 0, 1, 1, .05);
        colors->addWidget(color_[channel], row * 2 + 1, col);
        connect(color_[channel], qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, channel] { OnColorChanged(channel); });
    }
    spriteLayout->addLayout(colors);
    body->addWidget(spriteGroup);
    connect(spriteEnabled_, &QCheckBox::toggled, this, [this] { OnSpriteChanged(); });
    auto* meshGroup = new QGroupBox(QStringLiteral("Mesh Renderer"), inspectorBody_);
    auto* meshLayout = new QFormLayout(meshGroup);
    meshEnabled_ = new QCheckBox(QStringLiteral("메시 사용"), meshGroup);
    meshEnabled_->setObjectName(QStringLiteral("editorMesh"));
    meshLayout->addRow(meshEnabled_);
    meshPath_ = new QLineEdit(meshGroup);
    meshPath_->setObjectName(QStringLiteral("meshAssetPath"));
    meshPath_->setReadOnly(true);
    meshPath_->setPlaceholderText(QStringLiteral("기본 큐브"));
    meshLayout->addRow(QStringLiteral("OBJ 메시"), meshPath_);
    auto* meshButtons = new QWidget(meshGroup);
    auto* meshButtonsLayout = new QHBoxLayout(meshButtons);
    meshButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto* chooseMesh = new QPushButton(QStringLiteral("OBJ 선택"), meshButtons);
    chooseMesh->setObjectName(QStringLiteral("chooseMeshAsset"));
    auto* clearMesh = new QPushButton(QStringLiteral("기본 큐브"), meshButtons);
    clearMesh->setObjectName(QStringLiteral("clearMeshAsset"));
    meshButtonsLayout->addWidget(chooseMesh);
    meshButtonsLayout->addWidget(clearMesh);
    meshLayout->addRow(meshButtons);
    baseColorTexturePath_ = new QLineEdit(meshGroup);
    baseColorTexturePath_->setObjectName(QStringLiteral("baseColorTexturePath"));
    baseColorTexturePath_->setReadOnly(true);
    baseColorTexturePath_->setPlaceholderText(QStringLiteral("단색 재질"));
    meshLayout->addRow(QStringLiteral("기본 색상 텍스처"), baseColorTexturePath_);
    auto* textureButtons = new QWidget(meshGroup);
    auto* textureButtonsLayout = new QHBoxLayout(textureButtons);
    textureButtonsLayout->setContentsMargins(0, 0, 0, 0);
    auto* chooseTexture = new QPushButton(QStringLiteral("이미지 선택"), textureButtons);
    chooseTexture->setObjectName(QStringLiteral("chooseBaseColorTexture"));
    auto* clearTexture = new QPushButton(QStringLiteral("단색 재질"), textureButtons);
    clearTexture->setObjectName(QStringLiteral("clearBaseColorTexture"));
    textureButtonsLayout->addWidget(chooseTexture);
    textureButtonsLayout->addWidget(clearTexture);
    meshLayout->addRow(textureButtons);
    auto* reloadAssets = new QPushButton(QStringLiteral("메시·텍스처 다시 불러오기"), meshGroup);
    reloadAssets->setObjectName(QStringLiteral("reloadMeshAssets"));
    meshLayout->addRow(reloadAssets);
    connect(chooseTexture, &QPushButton::clicked, this, [this] {
        if (previewActive_ || !document_.GetSelected())
            return;
        const auto tab = document_.GetActiveTabId();
        const auto entity = document_.GetSelected().GetId();
        const auto file = QFileDialog::getOpenFileName(
            this, QStringLiteral("기본 색상 텍스처 선택"), baseColorTexturePath_->text(),
            QStringLiteral("이미지 (*.png *.jpg *.jpeg *.bmp *.tga)"));
        if (file.isEmpty())
            return;
        if (document_.GetActiveTabId() != tab || document_.GetSelectedId(tab) != entity) {
            Log(QStringLiteral("선택이 변경되어 텍스처 적용을 취소했습니다."), true);
            return;
        }
        const auto path = file.toUtf8().toStdString();
        std::string error;
        if (!LuxEngine::AssetManager::ReloadTexture(path, error)) {
            ShowError(QStringLiteral("텍스처 불러오기"), QString::fromUtf8(error));
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
                    ShowError(QStringLiteral("텍스처 불러오기"),
                              QStringLiteral("모델에 텍스처를 적용할 UV 좌표가 없습니다."));
                    return;
                }
            }
        }
        RecordEdit(QStringLiteral("기본 색상 텍스처 변경"), [this, path] {
            auto selected = document_.GetSelected();
            if (!selected.HasComponent<LuxEngine::MeshRendererComponent>())
                selected.AddComponent<LuxEngine::MeshRendererComponent>();
            selected.GetComponent<LuxEngine::MeshRendererComponent>().material.baseColorTexture =
                path;
            document_.MarkDirty();
        });
        RefreshAll();
    });
    connect(clearTexture, &QPushButton::clicked, this, [this] {
        if (previewActive_ || !document_.GetSelected() ||
            !document_.GetSelected().HasComponent<LuxEngine::MeshRendererComponent>())
            return;
        RecordEdit(QStringLiteral("기본 색상 텍스처 초기화"), [this] {
            document_.GetSelected()
                .GetComponent<LuxEngine::MeshRendererComponent>()
                .material.baseColorTexture.clear();
            document_.MarkDirty();
        });
        RefreshAll();
    });
    connect(reloadAssets, &QPushButton::clicked, this, [this] {
        const auto selected = document_.GetSelected();
        if (previewActive_ || !selected ||
            !selected.HasComponent<LuxEngine::MeshRendererComponent>())
            return;
        const auto& mesh = selected.GetComponent<LuxEngine::MeshRendererComponent>();
        QStringList errors;
        std::string error;
        bool requested = false;
        if (!mesh.meshPath.empty()) {
            requested = true;
            if (!LuxEngine::AssetManager::ReloadMesh(
                    document_.GetScene().ResolveAssetPath(mesh.meshPath), error,
                    !mesh.material.baseColorTexture.empty()))
                errors << QString::fromUtf8(error);
        }
        if (!mesh.material.baseColorTexture.empty()) {
            requested = true;
            if (!LuxEngine::AssetManager::ReloadTexture(
                    document_.GetScene().ResolveAssetPath(mesh.material.baseColorTexture), error))
                errors << QString::fromUtf8(error);
        }
        Log(!errors.isEmpty() ? errors.join('\n')
            : requested       ? QStringLiteral("자산 파일 검증을 마쳤습니다. 뷰포트에 갱신합니다.")
                              : QStringLiteral("다시 불러올 자산 파일이 없습니다."),
            !errors.isEmpty());
        RefreshAll();
    });
    connect(chooseMesh, &QPushButton::clicked, this, [this] {
        if (previewActive_ || !document_.GetSelected())
            return;
        const auto tab = document_.GetActiveTabId();
        const auto entity = document_.GetSelected().GetId();
        const auto file =
            QFileDialog::getOpenFileName(this, QStringLiteral("OBJ 메시 선택"), meshPath_->text(),
                                         QStringLiteral("Wavefront OBJ (*.obj);;모든 파일 (*)"));
        if (file.isEmpty())
            return;
        if (document_.GetActiveTabId() != tab || document_.GetSelectedId(tab) != entity) {
            Log(QStringLiteral("선택이 변경되어 메시 적용을 취소했습니다."), true);
            return;
        }
        LuxEngine::RawMeshData data;
        const auto path = file.toUtf8().toStdString();
        if (!LuxEngine::AssetManager::LoadMesh(path, data, true)) {
            ShowError(QStringLiteral("메시 불러오기"),
                      QStringLiteral("OBJ 메시 파일을 읽을 수 없습니다."));
            return;
        }
        const auto selected = document_.GetSelected();
        if (selected.HasComponent<LuxEngine::MeshRendererComponent>() &&
            !selected.GetComponent<LuxEngine::MeshRendererComponent>()
                 .material.baseColorTexture.empty() &&
            !data.hasTexCoords) {
            ShowError(QStringLiteral("메시 불러오기"),
                      QStringLiteral("텍스처를 유지하려면 UV 좌표가 있는 OBJ가 필요합니다."));
            return;
        }
        RecordEdit(QStringLiteral("메시 파일 변경"), [this, path] {
            auto selected = document_.GetSelected();
            if (!selected.HasComponent<LuxEngine::MeshRendererComponent>())
                selected.AddComponent<LuxEngine::MeshRendererComponent>();
            selected.GetComponent<LuxEngine::MeshRendererComponent>().meshPath = path;
            document_.MarkDirty();
        });
        RefreshAll();
    });
    connect(clearMesh, &QPushButton::clicked, this, [this] {
        if (previewActive_ || !document_.GetSelected() ||
            !document_.GetSelected().HasComponent<LuxEngine::MeshRendererComponent>())
            return;
        RecordEdit(QStringLiteral("메시 파일 초기화"), [this] {
            document_.GetSelected()
                .GetComponent<LuxEngine::MeshRendererComponent>()
                .meshPath.clear();
            document_.MarkDirty();
        });
        RefreshAll();
    });
    metallic_ = Number(meshGroup, 0, 1, 0, .05);
    roughness_ = Number(meshGroup, 0, 1, 1, .05);
    meshLayout->addRow(QStringLiteral("금속성"), metallic_);
    meshLayout->addRow(QStringLiteral("거칠기"), roughness_);
    body->addWidget(meshGroup);
    connect(meshEnabled_, &QCheckBox::toggled, this, [this] { OnMeshChanged(meshEnabled_); });
    connect(metallic_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { OnMeshChanged(metallic_); });
    connect(roughness_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { OnMeshChanged(roughness_); });
    auto* tileGroup = new QGroupBox(QStringLiteral("Tilemap"), inspectorBody_);
    auto* tileLayout = new QFormLayout(tileGroup);
    tilemapEnabled_ = new QCheckBox(QStringLiteral("타일맵 사용"), tileGroup);
    tilemapEnabled_->setObjectName(QStringLiteral("editorTilemap"));
    tileLayout->addRow(tilemapEnabled_);
    tilemapWidth_ = new QSpinBox(tileGroup);
    tilemapWidth_->setRange(1, 256);
    tilemapHeight_ = new QSpinBox(tileGroup);
    tilemapHeight_->setRange(1, 256);
    tilemapSize_ = Number(tileGroup, .1, 64, 1, .1);
    tileLayout->addRow(QStringLiteral("가로 칸"), tilemapWidth_);
    tileLayout->addRow(QStringLiteral("세로 칸"), tilemapHeight_);
    tileLayout->addRow(QStringLiteral("칸 크기"), tilemapSize_);
    body->addWidget(tileGroup);
    connect(tilemapEnabled_, &QCheckBox::toggled, this,
            [this] { OnTilemapChanged(tilemapEnabled_); });
    connect(tilemapWidth_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this] { OnTilemapChanged(tilemapWidth_); });
    connect(tilemapHeight_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this] { OnTilemapChanged(tilemapHeight_); });
    connect(tilemapSize_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { OnTilemapChanged(tilemapSize_); });
    // 개발 중: 엔진에 아직 없는 원형·다각형 충돌체와 조인트는 편집 도구를 노출하지 않습니다.
    auto* physicsGroup = new QGroupBox(QStringLiteral("Physics 2D"), inspectorBody_);
    auto* physicsLayout = new QFormLayout(physicsGroup);
    bodyEnabled_ = new QCheckBox(QStringLiteral("물리 몸체 사용"), physicsGroup);
    bodyEnabled_->setObjectName(QStringLiteral("editorBody2D"));
    bodyType_ = new QComboBox(physicsGroup);
    bodyType_->addItems(
        {QStringLiteral("고정"), QStringLiteral("동적"), QStringLiteral("키네마틱")});
    gravityScale_ = Number(physicsGroup, 0, 20, 1, .1);
    colliderEnabled_ = new QCheckBox(QStringLiteral("박스 충돌체 사용"), physicsGroup);
    colliderEnabled_->setObjectName(QStringLiteral("editorCollider2D"));
    triggerEnabled_ = new QCheckBox(QStringLiteral("트리거"), physicsGroup);
    physicsLayout->addRow(bodyEnabled_);
    physicsLayout->addRow(QStringLiteral("몸체 유형"), bodyType_);
    physicsLayout->addRow(QStringLiteral("중력 배율"), gravityScale_);
    physicsLayout->addRow(colliderEnabled_);
    for (int axis = 0; axis < 2; ++axis) {
        colliderSize_[axis] = Number(physicsGroup, .02, 10000, 1, .1);
        colliderOffset_[axis] = Number(physicsGroup, -10000, 10000, 0, .1);
        physicsLayout->addRow(axis == 0 ? QStringLiteral("폭") : QStringLiteral("높이"),
                              colliderSize_[axis]);
        physicsLayout->addRow(axis == 0 ? QStringLiteral("X 오프셋") : QStringLiteral("Y 오프셋"),
                              colliderOffset_[axis]);
    }
    physicsLayout->addRow(triggerEnabled_);
    body->addWidget(physicsGroup);
    connect(bodyEnabled_, &QCheckBox::toggled, this, [this] { OnPhysicsChanged(bodyEnabled_); });
    connect(bodyType_, &QComboBox::currentIndexChanged, this,
            [this] { OnPhysicsChanged(bodyType_); });
    connect(gravityScale_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { OnPhysicsChanged(gravityScale_); });
    connect(colliderEnabled_, &QCheckBox::toggled, this,
            [this] { OnPhysicsChanged(colliderEnabled_); });
    connect(triggerEnabled_, &QCheckBox::toggled, this,
            [this] { OnPhysicsChanged(triggerEnabled_); });
    for (auto* field : colliderSize_)
        connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, field] { OnPhysicsChanged(field); });
    for (auto* field : colliderOffset_)
        connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, field] { OnPhysicsChanged(field); });

    connect(name_, &QLineEdit::editingFinished, this, [this] { OnNameChanged(); });
    BuildCameraDetails(body);
    inspectorLayout->addWidget(inspectorBody_);
    inspectorLayout->addStretch();
    inspectorScroll->setWidget(inspector);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout* toolsLayout;
    auto* toolsPanel = Panel(this, toolsLayout, 0);
    auto* toolsRow = new QHBoxLayout;
    toolsRow->setContentsMargins(0, 0, 0, 0);
    toolsRow->setSpacing(0);
    auto* rail = new QWidget(toolsPanel);
    rail->setObjectName(QStringLiteral("toolRail"));
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(3, 4, 3, 4);
    railLayout->setSpacing(3);
    auto* toolGroup = new QButtonGroup(rail);
    toolGroup->setExclusive(true);
    const std::array<std::pair<EditorIcon, const char*>, 6> modes{
        {{EditorIcon::Select, "선택 (Q)"},
         {EditorIcon::Move, "이동 (W)"},
         {EditorIcon::Rotate, "회전 (E)"},
         {EditorIcon::Scale, "크기 (R)"},
         {EditorIcon::Brush, "타일 칠하기 (B)"},
         {EditorIcon::Eraser, "타일 지우기 (X)"}}};
    for (size_t index = 0; index < modes.size(); ++index) {
        auto* button = new QToolButton(rail);
        button->setIcon(MakeEditorIcon(modes[index].first));
        button->setIconSize(QSize(22, 22));
        button->setFixedSize(34, 34);
        button->setToolTip(QString::fromUtf8(modes[index].second));
        button->setCheckable(true);
        button->setChecked(index == 0);
        toolButtons_[index] = button;
        toolGroup->addButton(button, static_cast<int>(index));
        railLayout->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, index] { SetTool(static_cast<EditorTool>(index)); });
    }
    railLayout->addStretch();
    auto* focusTool = new QToolButton(rail);
    focusTool->setIcon(MakeEditorIcon(EditorIcon::Focus));
    focusTool->setIconSize(QSize(22, 22));
    focusTool->setFixedSize(34, 34);
    focusTool->setToolTip(QStringLiteral("선택 항목에 초점 (F)"));
    railLayout->addWidget(focusTool);
    connect(focusTool, &QToolButton::clicked, focus, &QToolButton::click);
    toolsRow->addWidget(rail);
    auto* toolProperties = new QWidget(toolsPanel);
    auto* propertiesLayout = new QVBoxLayout(toolProperties);
    propertiesLayout->setContentsMargins(7, 7, 7, 7);
    propertiesLayout->addWidget(Caption(QStringLiteral("배치 도구"), toolProperties));
    propertiesLayout->addWidget(new QLabel(QStringLiteral("오브젝트 편집"), toolProperties));
    toolHint_ = new QLabel(QStringLiteral("선택"), toolProperties);
    toolHint_->setObjectName(QStringLiteral("toolHelp"));
    toolHint_->setWordWrap(true);
    toolHint_->setAlignment(Qt::AlignTop);
    propertiesLayout->addWidget(toolHint_);
    tilePalette_ = new QComboBox(toolProperties);
    tilePalette_->addItems({QStringLiteral("풀"), QStringLiteral("흙"), QStringLiteral("돌"),
                            QStringLiteral("물"), QStringLiteral("모래")});
    tilePalette_->setToolTip(QStringLiteral("2D 타일 붓 색상"));
    propertiesLayout->addWidget(tilePalette_);
    connect(tilePalette_, &QComboBox::currentIndexChanged, this, [this](int index) {
        tileId_ = static_cast<uint8_t>(index + 1);
        canvas_->SetTileId(tileId_);
    });
    propertiesLayout->addStretch();
    toolsRow->addWidget(toolProperties, 1);
    toolsLayout->addLayout(toolsRow, 1);

    overviewCanvas_ =
        new SceneViewport2D(this, document_, select, transform, paintTile, begin, end);
    overviewCanvas_->SetCancelEditCallback([this] { CancelViewportEdit(); });
    overviewCanvas_->setObjectName(QStringLiteral("topViewport2D"));
    overviewCanvas_->SetTool(EditorTool::Select);
    consoleOutput_ = new QPlainTextEdit(this);
    consoleOutput_->setReadOnly(true);
    consoleOutput_->setObjectName(QStringLiteral("editorConsole"));
    consoleOutput_->setMaximumBlockCount(2000);
    issuesOutput_ = new QPlainTextEdit(this);
    issuesOutput_->setReadOnly(true);
    issuesOutput_->setObjectName(QStringLiteral("editorProblems"));
    issuesOutput_->setPlaceholderText(QStringLiteral("오류와 경고가 여기에 표시됩니다."));
    debugPanel_ = new DebugPanel(this, [this](LuxEngine::uint32 id) { SelectEntity(id); });

    const std::array<std::pair<QString, QWidget*>, 8> panels{
        {{QStringLiteral("Outliner"), hierarchyPanel},
         {QStringLiteral("Content Browser"), assetsPanel},
         {QStringLiteral("Details"), inspectorScroll},
         {QStringLiteral("Console"), consoleOutput_},
         {QStringLiteral("Problems"), issuesOutput_},
         {QStringLiteral("Place Actors"), toolsPanel},
         {QStringLiteral("Top View"), overviewCanvas_},
         {QStringLiteral("Debugger"), debugPanel_}}};
    for (size_t index = 0; index < panels.size(); ++index) {
        auto* dock = new QDockWidget(panels[index].first, this);
        dock->setObjectName(QStringLiteral("lux-dock-%1").arg(index));
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                          QDockWidget::DockWidgetClosable);
        dock->setWidget(panels[index].second);
        docks_[index] = dock;
        const auto area = index == 0 || index == 2 ? Qt::RightDockWidgetArea
                          : index == 5             ? Qt::LeftDockWidgetArea
                                                   : Qt::BottomDockWidgetArea;
        addDockWidget(area, dock);
    }
    splitDockWidget(docks_[0], docks_[2], Qt::Vertical);
    for (size_t index : {size_t{3}, size_t{4}, size_t{7}})
        tabifyDockWidget(docks_[1], docks_[index]);
    for (size_t index : {size_t{1}, size_t{3}, size_t{4}, size_t{5}, size_t{6}, size_t{7}})
        docks_[index]->hide();
    resizeDocks({docks_[0], docks_[2]}, {230, 520}, Qt::Vertical);
    resizeDocks({docks_[0]}, {360}, Qt::Horizontal);
    BuildAnimationPanel();

    auto* toolbar = addToolBar(QStringLiteral("편집 도구"));
    toolbar->setObjectName(QStringLiteral("lux-main-toolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto iconAction = [this, toolbar](EditorIcon icon, const QString& title,
                                      const std::function<void()>& action) {
        QAction* command = toolbar->addAction(MakeEditorIcon(icon), title);
        command->setToolTip(title);
        connect(command, &QAction::triggered, this, [action] { action(); });
        return command;
    };
    iconAction(EditorIcon::Save, QStringLiteral("장면 저장"), [this] { SaveScene(false); });
    toolbar->addSeparator();
    auto* create = new QToolButton(toolbar);
    create->setObjectName(QStringLiteral("placeActorsButton"));
    create->setText(QStringLiteral("추가"));
    create->setIcon(MakeEditorIcon(EditorIcon::Sprite));
    create->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    create->setPopupMode(QToolButton::InstantPopup);
    auto* createMenu = new QMenu(create);
    createMenu->addAction(MakeEditorIcon(EditorIcon::Sprite), QStringLiteral("스프라이트"), this,
                          [this] { AddEntity(); });
    createMenu->addAction(MakeEditorIcon(EditorIcon::Mesh), QStringLiteral("메시"), this,
                          [this] { AddMeshEntity(); });
    createMenu->addAction(MakeEditorIcon(EditorIcon::Grid), QStringLiteral("타일맵"), this,
                          [this] { AddTilemap(); });
    createMenu
        ->addAction(MakeEditorIcon(EditorIcon::Focus), QStringLiteral("카메라"), this,
                    [this] { AddCameraEntity(); })
        ->setObjectName(QStringLiteral("addCamera"));
    create->setMenu(createMenu);
    toolbar->addWidget(create);
    toolbar->addSeparator();
    auto* actionGroup = new QActionGroup(toolbar);
    actionGroup->setExclusive(true);
    const std::array<std::pair<EditorIcon, int>, 6> toolbarTools{{{EditorIcon::Select, Qt::Key_Q},
                                                                  {EditorIcon::Move, Qt::Key_W},
                                                                  {EditorIcon::Rotate, Qt::Key_E},
                                                                  {EditorIcon::Scale, Qt::Key_R},
                                                                  {EditorIcon::Brush, Qt::Key_B},
                                                                  {EditorIcon::Eraser, Qt::Key_X}}};
    for (size_t index = 0; index < toolbarTools.size(); ++index) {
        QAction* action = toolbar->addAction(MakeEditorIcon(toolbarTools[index].first),
                                             QString::fromUtf8(modes[index].second));
        action->setCheckable(true);
        action->setChecked(index == 0);
        action->setShortcut(QKeySequence(toolbarTools[index].second));
        actionGroup->addAction(action);
        toolActions_[index] = action;
        connect(action, &QAction::triggered, this,
                [this, index] { SetTool(static_cast<EditorTool>(index)); });
    }
    toolbar->addSeparator();
    previewStartAction_ =
        iconAction(EditorIcon::Play, QStringLiteral("장면 실행 (F5)"), [this] { StartPreview(); });
    previewPauseAction_ = iconAction(EditorIcon::Pause, QStringLiteral("일시정지/재개 (F6)"),
                                     [this] { TogglePreviewPause(); });
    previewStepAction_ = iconAction(EditorIcon::Step, QStringLiteral("한 프레임 실행 (F7)"),
                                    [this] { StepPreview(); });
    previewStopAction_ = iconAction(EditorIcon::Stop, QStringLiteral("실행 중지 (Shift+F5)"),
                                    [this] { StopPreview(); });
    toolbar->addSeparator();
    toolbar->addSeparator();
    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    projectLabel_ = Caption(QStringLiteral("프로젝트 없음"), toolbar);
    toolbar->addWidget(projectLabel_);
    auto* assetSearch = new QLineEdit(toolbar);
    assetSearch->setPlaceholderText(QStringLiteral("에셋 검색"));
    assetSearch->setMaximumWidth(160);
    toolbar->addWidget(assetSearch);
    connect(assetSearch, &QLineEdit::textChanged, projectSearch_, &QLineEdit::setText);
    connect(projectSearch_, &QLineEdit::textChanged, assetSearch, &QLineEdit::setText);
    auto* focusSearch = new QAction(this);
    focusSearch->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    addAction(focusSearch);
    connect(focusSearch, &QAction::triggered, assetSearch, [assetSearch] {
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
    QSettings settings;
    theme_ = settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString();
    uiScale_ = std::clamp(settings.value(QStringLiteral("uiScale"), 1.0).toDouble(), .75, 2.0);
    ApplyTheme();
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    const auto state = settings.value(QStringLiteral("layout")).toByteArray();
    if (!state.isEmpty() && !restoreState(state, 5))
        restoreState(defaultLayout_, 5);
}

void EditorWindow::SaveWorkspaceLayout() {
    QSettings settings;
    settings.setValue(QStringLiteral("theme"), theme_);
    settings.setValue(QStringLiteral("uiScale"), uiScale_);
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("layout"),
                      levelLayout_.isEmpty() ? saveState(5) : levelLayout_);
    if (project_)
        settings.setValue(QStringLiteral("lastProject"),
                          QString::fromUtf8(PathUtf8(project_->GetFile())));
    else
        settings.remove(QStringLiteral("lastProject"));
    settings.setValue(QStringLiteral("lastScene"), QString::fromUtf8(document_.GetPath()));
    QJsonArray scenes;
    for (const auto& tab : document_.ListTabs()) {
        if (tab.path.empty())
            continue;
        QJsonObject record;
        record.insert(QStringLiteral("path"), QString::fromUtf8(tab.path));
        const auto context = sceneProjects_.find(tab.id);
        if (context != sceneProjects_.end() && context->second)
            record.insert(QStringLiteral("project"),
                          QString::fromUtf8(PathUtf8(context->second->GetFile())));
        scenes.push_back(record);
    }
    settings.setValue(QStringLiteral("openSceneDocuments"),
                      QJsonDocument(scenes).toJson(QJsonDocument::Compact));
}

void EditorWindow::ResetWorkspaceLayout() {
    SetWorkspace(false);
    restoreState(defaultLayout_, 5);
    docks_[0]->show();
    docks_[2]->show();
    for (size_t index : {size_t{1}, size_t{3}, size_t{4}, size_t{5}, size_t{6}, size_t{7}})
        docks_[index]->hide();
    statusBar()->showMessage(QStringLiteral("기본 작업 공간 배치를 복원했습니다."), 4000);
}
} // namespace LuxEditor
