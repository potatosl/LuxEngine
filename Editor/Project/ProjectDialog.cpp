#include "ProjectDialog.hpp"
#include "PathEncoding.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QVBoxLayout>
#include <array>
#include <cstdio>
#include <sstream>

namespace LuxEditor {
namespace {
QString Q(const std::string& value) {
    return QString::fromUtf8(value);
}
QString Q(const std::filesystem::path& value) {
    return Q(PathUtf8(value));
}
std::string Utf8(const QString& value) {
    return value.toUtf8().toStdString();
}
std::filesystem::path SuggestedPath(const std::string& name) {
    return PathFromUtf8(Utf8(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))) /
           PathFromUtf8(name);
}
QDialogButtonBox* Buttons(QDialog& dialog, QVBoxLayout* layout) {
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    return buttons;
}
} // namespace

std::optional<ProjectCreationRequest> ShowNewProjectDialog(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("새 프로젝트"));
    dialog.resize(560, 280);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(QStringLiteral("새 프로젝트"), &dialog);
    auto* path = new QLineEdit(Q(SuggestedPath("NewLuxProject")), &dialog);
    auto* browse = new QPushButton(QStringLiteral("찾아보기..."), &dialog);
    auto* row = new QHBoxLayout;
    row->addWidget(path);
    row->addWidget(browse);
    form->addRow(QStringLiteral("프로젝트 이름"), name);
    form->addRow(QStringLiteral("프로젝트 폴더"), row);
    auto* templates = new QComboBox(&dialog);
    for (const char* label : {"빈 프로젝트", "플랫폼 게임", "탑다운 게임", "RPG", "퍼즐 게임"})
        templates->addItem(QString::fromUtf8(label));
    form->addRow(QStringLiteral("템플릿"), templates);
    layout->addLayout(form);
    auto* description = new QLabel(QStringLiteral("빈 장면과 Assets 폴더를 만듭니다."), &dialog);
    layout->addWidget(description);
    QObject::connect(templates, &QComboBox::currentIndexChanged, &dialog, [description](int index) {
        static const char* descriptions[] = {"빈 장면과 Assets 폴더를 만듭니다.",
                                             "바닥과 플레이어가 있는 2D 시작 장면을 만듭니다.",
                                             "지면과 플레이어가 있는 2D 시작 장면을 만듭니다.",
                                             "마을과 주인공, NPC가 있는 2D 시작 장면을 만듭니다.",
                                             "보드와 블록이 있는 2D 시작 장면을 만듭니다."};
        description->setText(QString::fromUtf8(descriptions[index]));
    });
    QObject::connect(browse, &QPushButton::clicked, &dialog, [path, name, &dialog] {
        const QString directory = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("프로젝트 상위 폴더 선택"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        if (!directory.isEmpty())
            path->setText(directory + QLatin1Char('/') + name->text());
    });
    Buttons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return ProjectCreationRequest{PathFromUtf8(Utf8(path->text())), Utf8(name->text()),
                                  static_cast<ProjectTemplate>(templates->currentIndex())};
}

std::optional<ProjectSettingsRequest> ShowProjectSettingsDialog(QWidget* parent,
                                                                const ProjectSettings& current) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("프로젝트 설정"));
    dialog.resize(600, 590);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(Q(current.name), &dialog);
    auto* version = new QLineEdit(Q(current.version), &dialog);
    form->addRow(QStringLiteral("프로젝트 이름"), name);
    auto* versionRow = new QHBoxLayout;
    versionRow->addWidget(version);
    for (size_t part = 0; part < 3; ++part) {
        auto* button = new QPushButton(
            QString::fromUtf8(std::array<const char*, 3>{"주 +1", "부 +1", "수정 +1"}[part]),
            &dialog);
        versionRow->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, &dialog, [version, part] {
            unsigned values[3]{};
            char extra = 0;
            const std::string text = Utf8(version->text());
            if (std::sscanf(text.c_str(), "%u.%u.%u%c", &values[0], &values[1], &values[2],
                            &extra) != 3 ||
                values[part] >= 9999)
                return;
            ++values[part];
            for (size_t index = part + 1; index < 3; ++index)
                values[index] = 0;
            version->setText(
                QStringLiteral("%1.%2.%3").arg(values[0]).arg(values[1]).arg(values[2]));
        });
    }
    form->addRow(QStringLiteral("버전 (주.부.수정)"), versionRow);
    auto* icon = new QLineEdit(Q(GenericPathUtf8(current.icon)), &dialog);
    auto* iconRow = new QHBoxLayout;
    iconRow->addWidget(icon);
    auto* browse = new QPushButton(QStringLiteral("찾아보기..."), &dialog);
    iconRow->addWidget(browse);
    form->addRow(QStringLiteral("아이콘"), iconRow);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, icon] {
        const QString file = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("프로젝트 아이콘 선택"), {},
            QStringLiteral("이미지 (*.png *.jpg *.jpeg *.ico *.icns)"));
        if (!file.isEmpty())
            icon->setText(file);
    });
    auto* autosave = new QSpinBox(&dialog);
    autosave->setRange(0, 3600);
    autosave->setValue(static_cast<int>(current.autosaveSeconds));
    form->addRow(QStringLiteral("자동 저장 간격 (초)"), autosave);
    auto* backup = new QSpinBox(&dialog);
    backup->setRange(1, 100);
    backup->setValue(static_cast<int>(current.backupLimit));
    form->addRow(QStringLiteral("보관할 백업 개수"), backup);
    auto* snap = new QCheckBox(QStringLiteral("격자에 맞춤"), &dialog);
    snap->setChecked(current.snapToGrid);
    form->addRow({}, snap);
    std::string dependencies;
    for (const auto& item : current.dependencies)
        dependencies += item.package + (item.version.empty() ? "" : " " + item.version) + "\n";
    auto* dependencyText = new QTextEdit(Q(dependencies), &dialog);
    dependencyText->setMinimumHeight(90);
    form->addRow(QStringLiteral("CMake 종속성"), dependencyText);
    layout->addLayout(form);
    Buttons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    ProjectSettingsRequest request;
    request.settings = current;
    request.settings.name = Utf8(name->text());
    request.settings.version = Utf8(version->text());
    request.settings.autosaveSeconds = static_cast<uint32_t>(autosave->value());
    request.settings.backupLimit = static_cast<uint32_t>(backup->value());
    request.settings.snapToGrid = snap->isChecked();
    request.settings.dependencies.clear();
    std::istringstream lines(Utf8(dependencyText->toPlainText()));
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        ProjectDependency dependency;
        std::string extra;
        if (!(fields >> dependency.package))
            continue;
        fields >> dependency.version;
        if (fields >> extra) {
            QMessageBox::warning(
                parent, QStringLiteral("프로젝트 설정"),
                QStringLiteral("종속성은 한 줄에 패키지 이름과 버전만 입력해 주세요."));
            return std::nullopt;
        }
        request.settings.dependencies.push_back(std::move(dependency));
    }
    const auto iconValue = Utf8(icon->text());
    if (iconValue.empty()) {
        request.settings.icon.clear();
        request.clearIcon = true;
    } else if (iconValue != GenericPathUtf8(current.icon)) {
        request.iconSource = PathFromUtf8(iconValue);
    }
    return request;
}

std::optional<std::filesystem::path>
ShowProjectDestinationDialog(QWidget* parent, const char* title, const std::string& suggested) {
    bool accepted = false;
    const QString value =
        QInputDialog::getText(parent, QString::fromUtf8(title),
                              QStringLiteral("새 프로젝트 폴더의 전체 경로를 입력해 주세요."),
                              QLineEdit::Normal, Q(SuggestedPath(suggested)), &accepted);
    return accepted ? std::optional(PathFromUtf8(Utf8(value))) : std::nullopt;
}
} // namespace LuxEditor
