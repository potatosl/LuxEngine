#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxEditor/Viewport/SceneViewport2D.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <map>

namespace LuxEditor {
void EditorWindow::BuildWorkspaceChrome() {
    auto* workspaceBar = new QToolBar(QStringLiteral("작업 공간"), this);
    workspaceBar->setObjectName(QStringLiteral("workspaceToolbar"));
    workspaceBar->setMovable(false);
    auto* brand = new QLabel(QStringLiteral("  LUX  "), workspaceBar);
    brand->setObjectName(QStringLiteral("editorBrand"));
    brand->setPixmap(MakeBrandIcon(true).pixmap(30, 30));
    brand->setFixedSize(42, 38);
    brand->setAlignment(Qt::AlignCenter);
    brand->setToolTip(QStringLiteral("LuxEngine"));
    workspaceBar->addWidget(brand);
    workspaceTabs_ = new QTabBar(workspaceBar);
    workspaceTabs_->setObjectName(QStringLiteral("workspaceTabs"));
    workspaceTabs_->addTab(MakeEditorIcon(EditorIcon::Mesh), QStringLiteral("레벨 에디터"));
    workspaceTabs_->addTab(MakeEditorIcon(EditorIcon::Play), QStringLiteral("애니메이션"));
    workspaceTabs_->setExpanding(false);
    workspaceBar->addWidget(workspaceTabs_);
    auto* mainToolbar = findChild<QToolBar*>(QStringLiteral("lux-main-toolbar"));
    insertToolBar(mainToolbar, workspaceBar);
    insertToolBarBreak(mainToolbar);
    connect(workspaceTabs_, &QTabBar::currentChanged, this,
            [this](int index) { SetWorkspace(index == 1); });

    // Animation is a document workspace, not a small console tab competing with the viewport.
    removeDockWidget(animationDock_);
    animationDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    animationDock_->setTitleBarWidget(new QWidget(animationDock_));
    workspaceStack_->addWidget(animationDock_);
    animationDock_->hide();

    auto* detailsLayout = qobject_cast<QVBoxLayout*>(inspectorBody_->layout());
    const auto groups =
        inspectorBody_->findChildren<QGroupBox*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* group : groups) {
        const int index = detailsLayout->indexOf(group);
        auto* section = new QWidget(inspectorBody_);
        section->setObjectName(QStringLiteral("componentSection"));
        section->setProperty("sectionName", group->title());
        auto* sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(0);
        auto* header = new QToolButton(section);
        header->setObjectName(QStringLiteral("componentHeader"));
        header->setText(group->title());
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setArrowType(Qt::DownArrow);
        header->setCheckable(true);
        header->setChecked(true);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sectionLayout->addWidget(header);
        detailsLayout->removeWidget(group);
        group->setProperty("componentTitle", group->title());
        group->setTitle({});
        sectionLayout->addWidget(group);
        detailsLayout->insertWidget(index, section);
        connect(header, &QToolButton::toggled, section, [group, header](bool expanded) {
            group->setVisible(expanded);
            header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        });
    }

    auto* addComponent = new QToolButton(inspectorBody_);
    addComponent->setObjectName(QStringLiteral("addComponent"));
    addComponent->setText(QStringLiteral("+ 컴포넌트 추가"));
    addComponent->setPopupMode(QToolButton::InstantPopup);
    addComponent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* components = new QMenu(addComponent);
    const std::pair<const char*, QCheckBox*> choices[] = {{"Sprite Renderer", spriteEnabled_},
                                                          {"Mesh Renderer", meshEnabled_},
                                                          {"Tilemap", tilemapEnabled_},
                                                          {"Rigidbody 2D", bodyEnabled_},
                                                          {"Box Collider 2D", colliderEnabled_}};
    for (const auto& [name, field] : choices)
        components->addAction(QString::fromLatin1(name), this,
                              [field] { field->setChecked(true); });
    components->addAction(QStringLiteral("Camera"), this,
                          [this] { cameraEnabled_->setChecked(true); });
    components->addAction(QStringLiteral("애니메이션 클립"), this, [this] {
        animationEnabled_->setChecked(true);
        SetWorkspace(true);
    });
    addComponent->setMenu(components);
    detailsLayout->insertWidget(1, addComponent);

    const auto drawer = [this](const QString& text, EditorIcon icon, size_t index,
                               const char* name) {
        auto* button = new QToolButton(this);
        button->setObjectName(QString::fromLatin1(name));
        button->setText(text);
        button->setIcon(MakeEditorIcon(icon));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setCheckable(true);
        statusBar()->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, index] { ToggleDrawer(index); });
        connect(docks_[index], &QDockWidget::visibilityChanged, button, &QToolButton::setChecked);
        return button;
    };
    auto* content = drawer(QStringLiteral("콘텐츠"), EditorIcon::Folder, 1, "contentDrawerButton");
    content->setToolTip(QStringLiteral("콘텐츠 브라우저 (Ctrl+Space)"));
    auto* contentShortcut = new QAction(this);
    contentShortcut->setObjectName(QStringLiteral("toggleContentBrowser"));
    contentShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    addAction(contentShortcut);
    connect(contentShortcut, &QAction::triggered, content, &QToolButton::click);
    drawer(QStringLiteral("출력 로그"), EditorIcon::Console, 3, "outputLogButton");
    drawer(QStringLiteral("문제"), EditorIcon::Bug, 4, "problemsDrawerButton");
    sceneStatus_ = new QLabel(this);
    sceneStatus_->setObjectName(QStringLiteral("sceneStatus"));
    sceneStatus_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(sceneStatus_, 1);
    statusBar()->setSizeGripEnabled(true);
}

void EditorWindow::SetWorkspace(bool animation) {
    if (!workspaceStack_ || !animationDock_)
        return;
    const QSignalBlocker blocker(workspaceTabs_);
    workspaceTabs_->setCurrentIndex(animation ? 1 : 0);
    if ((workspaceStack_->currentIndex() == 1) == animation)
        return;
    CancelViewportEdit();
    canvas_->CancelInteraction();
    canvas3D_->CancelInteraction();
    if (animation) {
        levelLayout_ = saveState(5);
        if (auto* toolbar = findChild<QToolBar*>(QStringLiteral("lux-main-toolbar")))
            toolbar->hide();
        for (auto* dock : docks_)
            dock->hide();
        workspaceStack_->setCurrentWidget(animationDock_);
        animationDock_->show();
    } else {
        StopAnimationPlayback();
        workspaceStack_->setCurrentIndex(0);
        if (!levelLayout_.isEmpty())
            restoreState(levelLayout_, 5);
        levelLayout_.clear();
        if (auto* toolbar = findChild<QToolBar*>(QStringLiteral("lux-main-toolbar")))
            toolbar->show();
    }
}

void EditorWindow::ToggleDrawer(size_t index) {
    if (index >= docks_.size())
        return;
    const bool close = docks_[index]->isVisible();
    for (size_t drawer : {size_t{1}, size_t{3}, size_t{4}, size_t{7}})
        docks_[drawer]->hide();
    if (!close) {
        docks_[index]->show();
        docks_[index]->raise();
        resizeDocks({docks_[index]}, {270}, Qt::Vertical);
    }
}

void EditorWindow::FilterDetails(const QString& text) {
    for (auto* section :
         inspectorBody_->findChildren<QWidget*>(QStringLiteral("componentSection"))) {
        bool matches =
            text.trimmed().isEmpty() ||
            section->property("sectionName").toString().contains(text, Qt::CaseInsensitive);
        for (auto* label : section->findChildren<QLabel*>())
            matches |= label->text().contains(text, Qt::CaseInsensitive);
        for (auto* field : section->findChildren<QCheckBox*>())
            matches |= field->text().contains(text, Qt::CaseInsensitive);
        const auto entity = document_.GetSelected();
        const auto name = section->property("sectionName").toString();
        bool available = static_cast<bool>(entity);
        if (entity) {
            if (name == QLatin1String("Sprite Renderer"))
                available = entity.HasComponent<LuxEngine::SpriteRendererComponent>();
            else if (name == QLatin1String("Mesh Renderer"))
                available = entity.HasComponent<LuxEngine::MeshRendererComponent>();
            else if (name == QLatin1String("Tilemap"))
                available = entity.HasComponent<LuxEngine::TilemapComponent>();
            else if (name == QLatin1String("Physics 2D"))
                available = entity.HasComponent<LuxEngine::Rigidbody2DComponent>() ||
                            entity.HasComponent<LuxEngine::BoxCollider2DComponent>();
            else if (name == QStringLiteral("씬 참조"))
                available = entity.HasComponent<LuxEngine::SceneReferenceComponent>();
            else if (name == QLatin1String("Camera"))
                available = entity.HasComponent<LuxEngine::CameraComponent>();
            else if (name == QLatin1String("Transform"))
                available = entity.HasComponent<LuxEngine::TransformComponent>();
        }
        section->setVisible(matches && available);
    }
}

void EditorWindow::RefreshAssetFolders() {
    if (!assetFolders_)
        return;
    const QSignalBlocker blocker(assetFolders_);
    assetFolders_->clear();
    if (!project_) {
        assetProjectRoot_.clear();
        assetFolder_.clear();
        return;
    }
    const auto root = project_->GetFile().parent_path();
    if (root != assetProjectRoot_) {
        assetProjectRoot_ = root;
        assetFolder_.clear();
    }
    std::map<std::filesystem::path, QTreeWidgetItem*> folders;
    auto* all = new QTreeWidgetItem(assetFolders_, {QStringLiteral("모든 콘텐츠")});
    all->setIcon(0, MakeEditorIcon(EditorIcon::Folder));
    folders.emplace(std::filesystem::path{}, all);
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error),
        end;
    for (; !error && iterator != end && folders.size() < 512; iterator.increment(error)) {
        if (!iterator->is_directory(error))
            continue;
        const auto name = iterator->path().filename().string();
        if (name.starts_with('.') || name == "out" || name == "build" ||
            name.starts_with("cmake-build")) {
            iterator.disable_recursion_pending();
            continue;
        }
        const auto relative = iterator->path().lexically_relative(root);
        const auto parent = folders.find(relative.parent_path());
        auto* item = new QTreeWidgetItem(parent == folders.end() ? all : parent->second,
                                         {QString::fromUtf8(PathUtf8(relative.filename()))});
        item->setData(0, Qt::UserRole, QString::fromUtf8(GenericPathUtf8(relative)));
        item->setIcon(0, MakeEditorIcon(EditorIcon::Folder));
        folders.emplace(relative, item);
    }
    const auto selected = folders.find(assetFolder_);
    if (selected == folders.end())
        assetFolder_.clear();
    assetFolders_->setCurrentItem(selected == folders.end() ? all : selected->second);
    all->setExpanded(true);
    assetBreadcrumb_->setText(assetFolder_.empty()
                                  ? QString::fromUtf8(project_->GetName())
                                  : QString::fromUtf8(GenericPathUtf8(assetFolder_)));
}
} // namespace LuxEditor
