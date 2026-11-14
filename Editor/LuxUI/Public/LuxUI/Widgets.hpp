#pragma once

// Qt-backed compatibility types live here so editor modules use one owned UI boundary.
// These types deliberately retain Qt semantics; this is not a backend-independent ABI.
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QButtonGroup>
#include <QByteArray>
#include <QByteArrayView>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListView>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointF>
#include <QPolygonF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRectF>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizeF>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QString>
#include <QStringConverter>
#include <QStringDecoder>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>
#include <functional>
#include <string_view>

#define LUX_UI_TEXT(text) QStringLiteral(text)

namespace LuxUI {
using BoxLayout = QBoxLayout;
namespace Flags = Qt;
using AbstractButton = QAbstractButton;
using AbstractItemView = QAbstractItemView;
using AbstractSpinBox = QAbstractSpinBox;
using Action = QAction;
using ActionGroup = QActionGroup;
using Brush = QBrush;
using ButtonGroup = QButtonGroup;
using ByteArray = QByteArray;
using ByteArrayView = QByteArrayView;
using Char = QChar;
using CheckBox = QCheckBox;
using Clipboard = QClipboard;
using CloseEvent = QCloseEvent;
using Color = QColor;
using ComboBox = QComboBox;
using ContextMenuEvent = QContextMenuEvent;
using CoreApplication = QCoreApplication;
using Dialog = QDialog;
using DialogButtonBox = QDialogButtonBox;
using Dir = QDir;
using DockWidget = QDockWidget;
using DoubleSpinBox = QDoubleSpinBox;
using DragEnterEvent = QDragEnterEvent;
using DropEvent = QDropEvent;
using ElapsedTimer = QElapsedTimer;
using Event = QEvent;
using EventLoop = QEventLoop;
using File = QFile;
using FileDialog = QFileDialog;
using FileIconProvider = QFileIconProvider;
using FileInfo = QFileInfo;
using FocusEvent = QFocusEvent;
using Font = QFont;
using FormLayout = QFormLayout;
using GraphicsOpacityEffect = QGraphicsOpacityEffect;
using GridLayout = QGridLayout;
using GroupBox = QGroupBox;
using GuiApplication = QGuiApplication;
using HBoxLayout = QHBoxLayout;
using HeaderView = QHeaderView;
using IODevice = QIODevice;
using Icon = QIcon;
using Image = QImage;
using ImageReader = QImageReader;
using InputDialog = QInputDialog;
using ItemSelectionModel = QItemSelectionModel;
using JsonArray = QJsonArray;
using JsonDocument = QJsonDocument;
using JsonObject = QJsonObject;
using JsonParseError = QJsonParseError;
using JsonValue = QJsonValue;
using KeyEvent = QKeyEvent;
using KeySequence = QKeySequence;
using KeySequenceEdit = QKeySequenceEdit;
using Label = QLabel;
using Latin1Char = QLatin1Char;
using Latin1String = QLatin1String;
using LineEdit = QLineEdit;
using LineF = QLineF;
template <class T> using List = QList<T>;
using ListView = QListView;
using ListWidget = QListWidget;
using ListWidgetItem = QListWidgetItem;
using Menu = QMenu;
using MenuBar = QMenuBar;
using MessageBox = QMessageBox;
using MetaObject = QMetaObject;
using MimeData = QMimeData;
using MouseEvent = QMouseEvent;
using Object = QObject;
using PaintEvent = QPaintEvent;
using Painter = QPainter;
using PainterPath = QPainterPath;
using Palette = QPalette;
using Pen = QPen;
using Pixmap = QPixmap;
using PlainTextEdit = QPlainTextEdit;
using Point = QPoint;
using PointF = QPointF;
using PolygonF = QPolygonF;
using Process = QProcess;
using ProcessEnvironment = QProcessEnvironment;
using ProgressBar = QProgressBar;
using ProgressDialog = QProgressDialog;
using PropertyAnimation = QPropertyAnimation;
using PushButton = QPushButton;
using Rect = QRect;
using RectF = QRectF;
using ResizeEvent = QResizeEvent;
using SaveFile = QSaveFile;
template <class T> using ScopedValueRollback = QScopedValueRollback<T>;
using Screen = QScreen;
using ScrollArea = QScrollArea;
using ScrollBar = QScrollBar;
using Settings = QSettings;
using SignalBlocker = QSignalBlocker;
using Size = QSize;
using SizeF = QSizeF;
using SizePolicy = QSizePolicy;
using Slider = QSlider;
using SpinBox = QSpinBox;
using Splitter = QSplitter;
using StackedWidget = QStackedWidget;
using StandardPaths = QStandardPaths;
using StatusBar = QStatusBar;
using String = QString;
using StringConverter = QStringConverter;
using StringDecoder = QStringDecoder;
using StringList = QStringList;
using Style = QStyle;
using StyleFactory = QStyleFactory;
using TabBar = QTabBar;
using TabWidget = QTabWidget;
using TemporaryDir = QTemporaryDir;
using TemporaryFile = QTemporaryFile;
using TextEdit = QTextEdit;
using Thread = QThread;
using Timer = QTimer;
using ToolBar = QToolBar;
using ToolButton = QToolButton;
using ToolTip = QToolTip;
using Transform = QTransform;
using TreeView = QTreeView;
using TreeWidget = QTreeWidget;
using TreeWidgetItem = QTreeWidgetItem;
using TreeWidgetItemIterator = QTreeWidgetItemIterator;
using UndoCommand = QUndoCommand;
using UndoStack = QUndoStack;
using Url = QUrl;
using VBoxLayout = QVBoxLayout;
using Variant = QVariant;
using WheelEvent = QWheelEvent;
using Widget = QWidget;

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int& argc, char** argv);
    ~Application() override;
    int Run();
    void SetIdentity(std::string_view organization, std::string_view name,
                     std::string_view version);
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Widget* parent = nullptr);
    ~MainWindow() override;
    DockWidget* AddPanel(std::string_view id, std::string_view title, Widget* content,
                         Flags::DockWidgetArea area);
    Action* AddCommand(Menu* menu, std::string_view id, std::string_view title,
                       std::function<void()> execute);

Q_SIGNALS:
    void CommandFailed(const String& message);
};
} // namespace LuxUI
