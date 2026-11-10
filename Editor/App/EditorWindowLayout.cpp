#include "Debug/DebugPanel.hpp"
#include "EditorWindow.hpp"
#include "Engine/ECS/ECS.hpp"
#include "Project/PathEncoding.hpp"
#include "Scene/SceneHierarchy.hpp"
#include "Ui/IconFactory.hpp"
#include "Viewport/EngineSceneRenderer.hpp"
#include "Viewport/SceneViewport2D.hpp"
#include "Viewport/SceneViewport3D.hpp"
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
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
    control->setMinimumWidth(82);
    control->setMaximumWidth(110);
    control->setToolTip(control->text());
    QObject::connect(control, qOverload<double>(&QDoubleSpinBox::valueChanged), control,
                     [control] { control->setToolTip(control->text()); });
    return control;
}
} // namespace

void EditorWindow::BuildLayout() {
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

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
    sceneBarLayout->addWidget(Caption(QStringLiteral("LAYOUT"), sceneBar));
    auto* viewChoice = new QComboBox(sceneBar);
    viewChoice->addItems({QStringLiteral("3D 원근 화면"), QStringLiteral("2D 평면 화면")});
    viewChoice->setMinimumWidth(128);
    sceneBarLayout->addWidget(viewChoice);
    sceneBarLayout->addStretch();
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
    canvas_->setObjectName(QStringLiteral("sceneViewport2D"));
    viewportTabs_->addTab(canvas3D_, QStringLiteral("3D 장면"));
    viewportTabs_->addTab(canvas_, QStringLiteral("2D 장면"));
    sceneLayout->addWidget(viewportTabs_, 1);
    setCentralWidget(sceneFrame);
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
    hierarchyLayout->addWidget(groupTree_);
    hierarchyLayout->addWidget(Caption(QStringLiteral("OUTLINER"), hierarchyPanel));
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
    connect(groupTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        groupFilter_ = item ? groupTree_->indexOfTopLevelItem(item) : 0;
        RefreshHierarchy();
    });
    connect(hierarchySearch_, &QLineEdit::textChanged, this, [this] { RefreshHierarchy(); });
    connect(hierarchy_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!refreshingHierarchy_)
            SelectEntity(
                item ? std::optional<LuxEngine::uint32>(item->data(0, Qt::UserRole).toUInt())
                     : std::nullopt,
                false);
    });
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
    assetHeader->addWidget(Caption(QStringLiteral("ASSETS"), assetsPanel));
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
    projectFiles_ = new QListWidget(assetSplit);
    projectFiles_->setObjectName(QStringLiteral("assetGrid"));
    projectFiles_->setViewMode(QListView::IconMode);
    projectFiles_->setIconSize(QSize(94, 94));
    projectFiles_->setGridSize(QSize(120, 118));
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
    assetSplit->setStretchFactor(0, 4);
    assetSplit->setStretchFactor(1, 1);
    assetsLayout->addWidget(assetSplit, 1);
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
    inspectorScroll->setMinimumWidth(265);
    inspectorScroll->setWidgetResizable(true);
    auto* inspector = new QWidget(inspectorScroll);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(7, 7, 7, 7);
    inspectorLayout->setSpacing(5);
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
    sceneReferencePanel_ = new QGroupBox(QStringLiteral("SCENE REFERENCE"), inspectorBody_);
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
    auto* transformGroup = new QGroupBox(QStringLiteral("TRANSFORM"), inspectorBody_);
    auto* grid = new QGridLayout(transformGroup);
    grid->setContentsMargins(6, 8, 6, 6);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(3);
    const char* rows[] = {"위치", "회전 °", "크기"};
    for (int row = 0; row < 3; ++row) {
        const int base = row * 3;
        grid->addWidget(Caption(QString::fromUtf8(rows[row]), transformGroup), base, 0, 1, 4);
        for (int axis = 0; axis < 3; ++axis) {
            transform_[row][axis] =
                Number(transformGroup, -100000, 100000, row == 2 ? 1 : 0, row == 1 ? 1 : .1);
            const int axisRow = base + (axis == 2 ? 2 : 1);
            const int axisCol = axis == 1 ? 2 : 0;
            grid->addWidget(new QLabel(QString::fromLatin1("XYZ" + axis, 1), transformGroup),
                            axisRow, axisCol);
            grid->addWidget(transform_[row][axis], axisRow, axisCol + 1);
            connect(transform_[row][axis], qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [this, row, axis] { OnTransformChanged(row, axis); });
        }
    }
    body->addWidget(transformGroup);
    auto* spriteGroup = new QGroupBox(QStringLiteral("SPRITE RENDERER"), inspectorBody_);
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
    auto* meshGroup = new QGroupBox(QStringLiteral("MESH RENDERER"), inspectorBody_);
    auto* meshLayout = new QFormLayout(meshGroup);
    meshEnabled_ = new QCheckBox(QStringLiteral("메시 사용"), meshGroup);
    meshEnabled_->setObjectName(QStringLiteral("editorMesh"));
    meshLayout->addRow(meshEnabled_);
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
    auto* tileGroup = new QGroupBox(QStringLiteral("TILEMAP"), inspectorBody_);
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
    auto* physicsGroup = new QGroupBox(QStringLiteral("2D PHYSICS"), inspectorBody_);
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

    // 개발 중: 스프라이트 시트·본 애니메이션은 엔진 구현 후 타임라인에 연결합니다.
    auto* animationGroup = new QGroupBox(QStringLiteral("TRANSFORM ANIMATION"), inspectorBody_);
    auto* animationLayout = new QVBoxLayout(animationGroup);
    animationEnabled_ = new QCheckBox(QStringLiteral("애니메이션 사용"), animationGroup);
    animationEnabled_->setObjectName(QStringLiteral("editorAnimation"));
    animationLoop_ = new QCheckBox(QStringLiteral("반복 재생"), animationGroup);
    animationDuration_ = Number(animationGroup, .01, 3600, 1, .1);
    auto* durationRow = new QFormLayout;
    durationRow->addRow(QStringLiteral("길이 (초)"), animationDuration_);
    animationTimeline_ = new QSlider(Qt::Horizontal, animationGroup);
    animationTimeline_->setObjectName(QStringLiteral("animationTimeline"));
    animationTimeline_->setRange(0, 1000);
    animationTimeline_->setToolTip(QStringLiteral("시간을 이동해 변환 애니메이션을 확인합니다."));
    keyframeList_ = new QListWidget(animationGroup);
    keyframeList_->setMaximumHeight(90);
    auto* keyButtons = new QHBoxLayout;
    auto* addKey = new QPushButton(QStringLiteral("현재 변환 키 추가"), animationGroup);
    auto* removeKey = new QPushButton(QStringLiteral("선택 키 삭제"), animationGroup);
    keyButtons->addWidget(addKey);
    keyButtons->addWidget(removeKey);
    animationLayout->addWidget(animationEnabled_);
    animationLayout->addWidget(animationLoop_);
    animationLayout->addLayout(durationRow);
    animationLayout->addWidget(animationTimeline_);
    animationLayout->addWidget(keyframeList_);
    animationLayout->addLayout(keyButtons);
    body->addWidget(animationGroup);
    connect(animationEnabled_, &QCheckBox::toggled, this,
            [this] { OnAnimationChanged(animationEnabled_); });
    connect(animationLoop_, &QCheckBox::toggled, this,
            [this] { OnAnimationChanged(animationLoop_); });
    connect(animationDuration_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { OnAnimationChanged(animationDuration_); });
    connect(addKey, &QPushButton::clicked, this, [this] { AddKeyframe(); });
    connect(removeKey, &QPushButton::clicked, this, [this] { RemoveKeyframe(); });
    connect(animationTimeline_, &QSlider::sliderPressed, this,
            [this] { timelineBefore_ = SnapshotScene(); });
    connect(animationTimeline_, &QSlider::actionTriggered, this, [this] {
        if (animationTimeline_->isSliderDown() || previewActive_)
            return;
        timelineBefore_ = SnapshotScene();
        QTimer::singleShot(0, this, [this] { EndTimelineEdit(); });
    });
    connect(animationTimeline_, &QSlider::valueChanged, this,
            [this](int value) { ScrubAnimation(value); });
    connect(animationTimeline_, &QSlider::sliderReleased, this, [this] { EndTimelineEdit(); });
    connect(name_, &QLineEdit::editingFinished, this, [this] { OnNameChanged(); });
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
    propertiesLayout->addWidget(Caption(QStringLiteral("TOOL PROPERTIES"), toolProperties));
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
         {QStringLiteral("Asset Browser"), assetsPanel},
         {QStringLiteral("Object Properties"), inspectorScroll},
         {QStringLiteral("Console"), consoleOutput_},
         {QStringLiteral("Problems"), issuesOutput_},
         {QStringLiteral("Tool Properties"), toolsPanel},
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
    splitDockWidget(docks_[6], docks_[1], Qt::Horizontal);
    tabifyDockWidget(docks_[3], docks_[4]);
    tabifyDockWidget(docks_[3], docks_[7]);
    docks_[3]->hide();
    docks_[4]->hide();
    resizeDocks({docks_[5], docks_[0]}, {215, 320}, Qt::Horizontal);
    resizeDocks({docks_[6], docks_[1]}, {440, 480}, Qt::Horizontal);
    resizeDocks({docks_[6]}, {235}, Qt::Vertical);

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
    toolbar->addWidget(Caption(QStringLiteral("FILE"), toolbar));
    iconAction(EditorIcon::NewFile, QStringLiteral("새 프로젝트"), [this] { NewProject(); });
    iconAction(EditorIcon::Open, QStringLiteral("프로젝트 열기"), [this] { OpenProjectDialog(); });
    iconAction(EditorIcon::Save, QStringLiteral("장면 저장"), [this] { SaveScene(false); });
    toolbar->addSeparator();
    toolbar->addWidget(Caption(QStringLiteral("CREATE"), toolbar));
    iconAction(EditorIcon::Sprite, QStringLiteral("2D 엔티티 추가"), [this] { AddEntity(); });
    iconAction(EditorIcon::Mesh, QStringLiteral("3D 메시 엔티티 추가"),
               [this] { AddMeshEntity(); });
    iconAction(EditorIcon::Grid, QStringLiteral("타일맵 추가"), [this] { AddTilemap(); });
    toolbar->addSeparator();
    toolbar->addWidget(Caption(QStringLiteral("TRANSFORM"), toolbar));
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
    toolbar->addWidget(Caption(QStringLiteral("DEBUG"), toolbar));
    previewStartAction_ =
        iconAction(EditorIcon::Play, QStringLiteral("장면 실행 (F5)"), [this] { StartPreview(); });
    previewPauseAction_ = iconAction(EditorIcon::Pause, QStringLiteral("일시정지/재개 (F6)"),
                                     [this] { TogglePreviewPause(); });
    previewStepAction_ = iconAction(EditorIcon::Step, QStringLiteral("한 프레임 실행 (F7)"),
                                    [this] { StepPreview(); });
    previewStopAction_ = iconAction(EditorIcon::Stop, QStringLiteral("실행 중지 (Shift+F5)"),
                                    [this] { StopPreview(); });
    toolbar->addSeparator();
    toolbar->addWidget(Caption(QStringLiteral("VIEW"), toolbar));
    auto* toolbarView = new QComboBox(toolbar);
    toolbarView->addItems({QStringLiteral("3D 원근"), QStringLiteral("2D 평면")});
    toolbar->addWidget(toolbarView);
    connect(toolbarView, &QComboBox::currentIndexChanged, viewportTabs_,
            &QTabWidget::setCurrentIndex);
    connect(viewportTabs_, &QTabWidget::currentChanged, toolbarView, &QComboBox::setCurrentIndex);
    auto* toolbarFocus = iconAction(EditorIcon::Focus, QStringLiteral("선택 항목에 초점"),
                                    [focus] { focus->click(); });
    toolbarFocus->setToolTip(QStringLiteral("선택 항목에 초점 (F)"));
    toolbar->addSeparator();
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

    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("GRID 1u"), this));
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("SNAP ON"), this));
    defaultLayout_ = saveState(3);
    previewPauseAction_->setEnabled(false);
    previewStepAction_->setEnabled(false);
    previewStopAction_->setEnabled(false);
    ApplyTheme();
    RestoreWorkspaceLayout();
}

void EditorWindow::ApplyTheme() {
    QFont font = QApplication::font();
    font.setPointSizeF(9.0 * uiScale_);
    setFont(font);
    if (auto* bar = findChild<QToolBar*>(QStringLiteral("lux-main-toolbar")))
        bar->setIconSize(QSize(static_cast<int>(20 * uiScale_), static_cast<int>(20 * uiScale_)));
    for (QAbstractButton* button : toolButtons_)
        if (auto* tool = qobject_cast<QToolButton*>(button)) {
            tool->setIconSize(
                QSize(static_cast<int>(22 * uiScale_), static_cast<int>(22 * uiScale_)));
            tool->setFixedSize(static_cast<int>(34 * uiScale_), static_cast<int>(34 * uiScale_));
        }
    for (auto* number : findChildren<QDoubleSpinBox*>()) {
        number->setMinimumWidth(static_cast<int>(82 * uiScale_));
        number->setMaximumWidth(static_cast<int>(110 * uiScale_));
    }
    if (projectFiles_) {
        projectFiles_->setIconSize(
            QSize(static_cast<int>(94 * uiScale_), static_cast<int>(94 * uiScale_)));
        if (projectFiles_->viewMode() == QListView::IconMode)
            projectFiles_->setGridSize(
                QSize(static_cast<int>(120 * uiScale_), static_cast<int>(118 * uiScale_)));
    }
    QString sheet;
    if (theme_ == QLatin1String("dark")) {
        sheet = QStringLiteral(
            "QWidget{background:#292a2d;color:#d6d7d9;font-size:%1pt;}"
            "QMainWindow,QMenuBar,QMenu,QToolBar,QStatusBar{background:#252629;color:#d6d7d9;}"
            "QMenuBar{border-bottom:1px solid #4a4c50;}"
            "QMenuBar::item:selected,QMenu::item:selected{background:#45556b;}"
            "QToolBar{spacing:2px;border-bottom:1px solid #54565b;padding:2px;}"
            "QToolBar::separator{background:#55575b;width:1px;margin:3px 5px;}"
            "QDockWidget{border:1px solid #4a4c50;}"
            "QDockWidget::title{background:#333538;color:#e1e2e4;padding:4px 6px;text-align:left;"
            "border-bottom:1px solid #505257;}"
            "QWidget#sceneBar{background:#333538;border-bottom:1px solid #515359;}"
            "QWidget#toolRail{background:#202124;border-right:1px solid #4d4f54;}"
            "QLabel#sectionCaption{color:#b4c1cd;font-weight:600;letter-spacing:1px;}"
            "QLabel#toolHelp{color:#9da5ad;line-height:1.3;}"
            "QLineEdit,QListWidget,QTreeWidget,QPlainTextEdit,QTextEdit,QDoubleSpinBox,QSpinBox,"
            "QComboBox{"
            "background:#202124;color:#e1e1e2;border:1px solid "
            "#50535a;padding:2px;selection-background-color:#a86c27;}"
            "QTreeWidget::item,QListWidget::item{min-height:18px;}"
            "QListWidget::item:selected,QTreeWidget::item:selected{background:#9d682b;color:#fff;}"
            "QPushButton,QToolButton{background:#383a3e;color:#dedfe1;border:1px solid "
            "#585b61;padding:3px;}"
            "QPushButton:hover,QToolButton:hover{background:#4b4f56;}"
            "QPushButton:checked,QToolButton:checked{background:#9d682b;color:#fff;border-color:#"
            "d8953d;}"
            "QGroupBox{border:1px solid #4f5156;margin-top:12px;padding-top:7px;font-weight:600;}"
            "QGroupBox::title{subcontrol-origin:margin;left:6px;color:#a8bdd0;}"
            "QScrollArea,QScrollArea>QWidget>QWidget{background:#292a2d;}"
            "QScrollBar:vertical{background:#252629;width:11px;}"
            "QScrollBar::handle:vertical{background:#555961;min-height:20px;}"
            "QSplitter::handle{background:#17181a;}"
            "QStatusBar{border-top:1px solid #53555a;}"
            "QStatusBar QLabel{padding:0 8px;border-left:1px solid #53555a;}");
    } else if (theme_ == QLatin1String("light")) {
        sheet = QStringLiteral(
            "QWidget{background:#e2e4e7;color:#2a2d32;font-size:%1pt;}"
            "QMainWindow,QMenuBar,QMenu,QToolBar,QStatusBar{background:#d1d4d9;}"
            "QDockWidget::title{background:#c2c7ce;padding:4px;border-bottom:1px solid #9299a4;}"
            "QLineEdit,QListWidget,QTreeWidget,QPlainTextEdit,QDoubleSpinBox,QComboBox{"
            "background:#fff;color:#2a2d32;border:1px solid #9ba2aa;padding:2px;}"
            "QPushButton,QToolButton{background:#d8dce1;border:1px solid #9ba2aa;padding:3px;}"
            "QPushButton:checked,QToolButton:checked{background:#d99a49;}"
            "QGroupBox{border:1px solid #aeb4bc;margin-top:12px;padding-top:7px;}");
    } else {
        sheet = QSettings().value(QStringLiteral("customStyle")).toString();
    }
    if (theme_ != QLatin1String("custom"))
        sheet = sheet.arg(9.0 * uiScale_);
    setStyleSheet(sheet);
}

void EditorWindow::RestoreWorkspaceLayout() {
    QSettings settings;
    theme_ = settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString();
    uiScale_ = std::clamp(settings.value(QStringLiteral("uiScale"), 1.0).toDouble(), .75, 2.0);
    ApplyTheme();
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    const auto state = settings.value(QStringLiteral("layout")).toByteArray();
    if (!state.isEmpty() && !restoreState(state, 3))
        restoreState(defaultLayout_, 3);
}
void EditorWindow::SaveWorkspaceLayout() {
    QSettings settings;
    settings.setValue(QStringLiteral("theme"), theme_);
    settings.setValue(QStringLiteral("uiScale"), uiScale_);
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("layout"), saveState(3));
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
    restoreState(defaultLayout_, 3);
    for (auto* dock : docks_)
        dock->show();
    docks_[3]->hide();
    docks_[4]->hide();
    statusBar()->showMessage(QStringLiteral("기본 작업 공간 배치를 복원했습니다."), 4000);
}
} // namespace LuxEditor
