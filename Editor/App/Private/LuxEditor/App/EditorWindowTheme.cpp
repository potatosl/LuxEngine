#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <QAbstractButton>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>

namespace LuxEditor {
void EditorWindow::ApplyTheme() {
    auto* fusion = findChild<QStyle*>(QStringLiteral("editorFusionStyle"));
    if (!fusion) {
        fusion = QStyleFactory::create(QStringLiteral("Fusion"));
        fusion->setObjectName(QStringLiteral("editorFusionStyle"));
        fusion->setParent(this);
    }
    setStyle(fusion);
    QFont font = QApplication::font();
    font.setPointSizeF(9.0 * uiScale_);
    setFont(font);
    const bool light = theme_ == QLatin1String("light");
    if (auto* brand = findChild<QLabel*>(QStringLiteral("editorBrand")))
        brand->setPixmap(MakeBrandIcon(!light).pixmap(30, 30));
    if (!project_ || project_->GetIcon().empty())
        setWindowIcon(MakeBrandIcon(!light));
    QPalette colors = palette();
    colors.setColor(QPalette::Window, light ? QColor("#eceef1") : QColor("#242424"));
    colors.setColor(QPalette::WindowText, light ? QColor("#24272d") : QColor("#dedede"));
    colors.setColor(QPalette::Base, light ? QColor("#ffffff") : QColor("#191919"));
    colors.setColor(QPalette::AlternateBase, light ? QColor("#f3f4f6") : QColor("#202020"));
    colors.setColor(QPalette::Text, colors.color(QPalette::WindowText));
    colors.setColor(QPalette::Button, colors.color(QPalette::Window));
    colors.setColor(QPalette::ButtonText, colors.color(QPalette::WindowText));
    colors.setColor(QPalette::Highlight, QColor("#3269a8"));
    colors.setColor(QPalette::HighlightedText, Qt::white);
    colors.setColor(QPalette::Disabled, QPalette::Text,
                    light ? QColor("#949aa3") : QColor("#707070"));
    colors.setColor(QPalette::Disabled, QPalette::ButtonText,
                    colors.color(QPalette::Disabled, QPalette::Text));
    setPalette(colors);
    if (auto* bar = findChild<QToolBar*>(QStringLiteral("lux-main-toolbar"))) {
        bar->setIconSize(QSize(20, 20) * uiScale_);
        if (previewStartAction_)
            if (auto* play = bar->widgetForAction(previewStartAction_))
                play->setObjectName(QStringLiteral("playButton"));
    }
    for (auto* number : findChildren<QDoubleSpinBox*>()) {
        number->setMinimumWidth(std::max(number->minimumWidth(), static_cast<int>(56 * uiScale_)));
        number->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    if (projectFiles_) {
        projectFiles_->setIconSize(QSize(64, 64) * uiScale_);
        if (projectFiles_->viewMode() == QListView::IconMode)
            projectFiles_->setGridSize(QSize(112, 100) * uiScale_);
    }
    if (theme_ == QLatin1String("custom")) {
        setStyleSheet(QSettings().value(QStringLiteral("customStyle")).toString());
        return;
    }
    const QString panel = light ? QStringLiteral("#eceef1") : QStringLiteral("#242424");
    const QString chrome = light ? QStringLiteral("#e0e3e8") : QStringLiteral("#191919");
    const QString field = light ? QStringLiteral("#ffffff") : QStringLiteral("#161616");
    const QString line = light ? QStringLiteral("#bfc5ce") : QStringLiteral("#353535");
    const QString text = light ? QStringLiteral("#25282e") : QStringLiteral("#d5d5d5");
    const QString muted = light ? QStringLiteral("#657080") : QStringLiteral("#999999");
    const QString hover = light ? QStringLiteral("#d0d8e2") : QStringLiteral("#353535");
    setStyleSheet(QStringLiteral(R"CSS(
QWidget { font-size:%1pt; color:%2; }
QMainWindow { background:%3; }
QMenuBar, QMenu, QStatusBar, QToolBar { background:%3; }
QMenuBar { padding:2px 4px; }
QMenuBar::item, QMenu::item { padding:5px 12px; }
QMenuBar::item:selected, QMenu::item:selected { background:#3269a8; color:white; }
QToolBar { border:0; border-bottom:1px solid %4; spacing:4px; padding:5px 8px; }
QToolBar#workspaceToolbar { background:%3; padding:0 8px; }
QToolBar::separator { width:1px; background:%4; margin:4px 8px; }
QLabel#editorBrand { color:#65a4e6; font-size:12pt; font-weight:700; }
QLabel { background:transparent; }
QLabel#sectionCaption { color:%5; font-weight:500; }
QLabel#assetCount, QLabel#sceneStatus { color:%5; padding:2px 8px; }
QWidget#sceneBar { background:%3; border-bottom:1px solid %4; }
QDockWidget { background:%6; border:0; }
QDockWidget::title { background:%6; padding:6px 8px; border-bottom:1px solid %4; text-align:left; }
QDockWidget::close-button, QDockWidget::float-button { padding:2px; }
QTabWidget::pane { border:0; }
QTabBar { qproperty-drawBase:0; background:%3; }
QTabBar::tab { background:%3; color:%5; padding:7px 16px; border:0; border-right:1px solid %3; }
QTabBar::tab:selected { background:%6; color:%2; border-top:2px solid #4789ce; padding-top:5px; }
QTabBar::tab:hover:!selected { background:%7; color:%2; }
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox { background:%8; border:1px solid %4; border-radius:3px; padding:3px 5px; min-height:19px; selection-background-color:#3269a8; }
QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus { border-color:#4789ce; }
QLineEdit:disabled, QDoubleSpinBox:disabled, QSpinBox:disabled, QComboBox:disabled { color:%5; }
QTreeView, QListView, QPlainTextEdit, QTextEdit { alternate-background-color:%6; background:%8; border:0; outline:0; selection-background-color:#315b86; }
QTreeView::item { padding:3px 2px; min-height:20px; }
QTreeView::item:selected, QListView::item:selected { background:#315b86; color:white; }
QHeaderView::section { background:%6; color:%5; border:0; border-bottom:1px solid %4; padding:5px; }
QPushButton, QToolButton { background:transparent; border:1px solid transparent; border-radius:3px; padding:4px 7px; }
QPushButton { background:%7; border-color:%4; min-height:20px; }
QPushButton:hover, QToolButton:hover { background:%7; border-color:%4; }
QPushButton:pressed, QToolButton:checked { background:#315b86; border-color:#4789ce; color:white; }
QToolButton#playButton { color:#77c696; background:#263d2d; }
QToolButton#playButton:hover { background:#36583f; }
QToolButton#componentHeader { background:%7; border:0; border-radius:0; padding:7px; font-weight:600; text-align:left; }
QWidget#componentSection { background:%6; }
QToolButton#addComponent { border:1px solid #41658a; background:#2a4057; color:#dbeaff; padding:6px; }
QGroupBox { background:%6; border:0; margin:0; padding:6px 0; }
QGroupBox::title { color:%5; }
QScrollArea { border:0; background:%6; }
QScrollArea>QWidget>QWidget { background:%6; }
QSplitter::handle, QMainWindow::separator { background:%3; width:4px; height:4px; }
QMainWindow::separator:hover { background:#4789ce; }
QScrollBar:vertical { background:transparent; width:10px; margin:0; }
QScrollBar:horizontal { background:transparent; height:10px; margin:0; }
QScrollBar::handle { background:%4; border-radius:4px; min-height:24px; min-width:24px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; width:0; }
QScrollBar::add-page, QScrollBar::sub-page { background:transparent; }
QStatusBar { border-top:1px solid %4; padding:1px 5px; }
QStatusBar::item { border:0; }
QToolTip { background:%3; color:%2; border:1px solid %4; padding:5px; }
)CSS")
                      .arg(9.0 * uiScale_)
                      .arg(text, chrome, line, muted, panel, hover, field));
}
} // namespace LuxEditor
