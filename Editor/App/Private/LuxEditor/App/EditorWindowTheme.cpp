#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Ui/IconFactory.hpp>
#include <LuxUI/Widgets.hpp>
#include <algorithm>

namespace LuxEditor {
void EditorWindow::ApplyTheme() {
    auto* fusion = findChild<LuxUI::Style*>(LUX_UI_TEXT("editorFusionStyle"));
    if (!fusion) {
        fusion = LuxUI::StyleFactory::create(LUX_UI_TEXT("Fusion"));
        fusion->setObjectName(LUX_UI_TEXT("editorFusionStyle"));
        fusion->setParent(this);
    }
    setStyle(fusion);
    LuxUI::Font font = LuxUI::Application::font();
    font.setPointSizeF(9.0 * uiScale_);
    setFont(font);
    const bool light = theme_ == LuxUI::Latin1String("light");
    if (auto* brand = findChild<LuxUI::Label*>(LUX_UI_TEXT("editorBrand")))
        brand->setPixmap(MakeBrandIcon(!light).pixmap(30, 30));
    if (!project_ || project_->GetIcon().empty())
        setWindowIcon(MakeBrandIcon(!light));
    LuxUI::Palette colors = palette();
    colors.setColor(LuxUI::Palette::Window,
                    light ? LuxUI::Color("#eceef1") : LuxUI::Color("#242424"));
    colors.setColor(LuxUI::Palette::WindowText,
                    light ? LuxUI::Color("#24272d") : LuxUI::Color("#dedede"));
    colors.setColor(LuxUI::Palette::Base,
                    light ? LuxUI::Color("#ffffff") : LuxUI::Color("#191919"));
    colors.setColor(LuxUI::Palette::AlternateBase,
                    light ? LuxUI::Color("#f3f4f6") : LuxUI::Color("#202020"));
    colors.setColor(LuxUI::Palette::Text, colors.color(LuxUI::Palette::WindowText));
    colors.setColor(LuxUI::Palette::Button, colors.color(LuxUI::Palette::Window));
    colors.setColor(LuxUI::Palette::ButtonText, colors.color(LuxUI::Palette::WindowText));
    colors.setColor(LuxUI::Palette::Highlight, LuxUI::Color("#3269a8"));
    colors.setColor(LuxUI::Palette::HighlightedText, LuxUI::Flags::white);
    colors.setColor(LuxUI::Palette::Disabled, LuxUI::Palette::Text,
                    light ? LuxUI::Color("#949aa3") : LuxUI::Color("#707070"));
    colors.setColor(LuxUI::Palette::Disabled, LuxUI::Palette::ButtonText,
                    colors.color(LuxUI::Palette::Disabled, LuxUI::Palette::Text));
    setPalette(colors);
    if (auto* bar = findChild<LuxUI::ToolBar*>(LUX_UI_TEXT("lux-main-toolbar"))) {
        bar->setIconSize(LuxUI::Size(20, 20) * uiScale_);
        if (previewStartAction_)
            if (auto* play = bar->widgetForAction(previewStartAction_))
                play->setObjectName(LUX_UI_TEXT("playButton"));
    }
    for (auto* number : findChildren<LuxUI::DoubleSpinBox*>()) {
        number->setMinimumWidth(std::max(number->minimumWidth(), static_cast<int>(56 * uiScale_)));
        number->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    if (projectFiles_) {
        projectFiles_->setIconSize(LuxUI::Size(64, 64) * uiScale_);
        if (projectFiles_->viewMode() == LuxUI::ListView::IconMode)
            projectFiles_->setGridSize(LuxUI::Size(112, 100) * uiScale_);
    }
    if (theme_ == LuxUI::Latin1String("custom")) {
        setStyleSheet(LuxUI::Settings().value(LUX_UI_TEXT("customStyle")).toString());
        return;
    }
    const LuxUI::String panel = light ? LUX_UI_TEXT("#eceef1") : LUX_UI_TEXT("#242424");
    const LuxUI::String chrome = light ? LUX_UI_TEXT("#e0e3e8") : LUX_UI_TEXT("#191919");
    const LuxUI::String field = light ? LUX_UI_TEXT("#ffffff") : LUX_UI_TEXT("#161616");
    const LuxUI::String line = light ? LUX_UI_TEXT("#bfc5ce") : LUX_UI_TEXT("#353535");
    const LuxUI::String text = light ? LUX_UI_TEXT("#25282e") : LUX_UI_TEXT("#d5d5d5");
    const LuxUI::String muted = light ? LUX_UI_TEXT("#657080") : LUX_UI_TEXT("#999999");
    const LuxUI::String hover = light ? LUX_UI_TEXT("#d0d8e2") : LUX_UI_TEXT("#353535");
    setStyleSheet(LUX_UI_TEXT(R"CSS(
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
