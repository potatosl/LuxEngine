#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Project/ProjectDialog.hpp>
#include <LuxUI/Widgets.hpp>

#include <array>
#include <cstdio>
#include <sstream>

namespace LuxEditor {
namespace {
LuxUI::String Q(const std::string& value) {
    return LuxUI::String::fromUtf8(value);
}

LuxUI::String Q(const std::filesystem::path& value) {
    return Q(PathUtf8(value));
}

std::string Utf8(const LuxUI::String& value) {
    return value.toUtf8().toStdString();
}

std::filesystem::path SuggestedPath(const std::string& name) {
    return PathFromUtf8(Utf8(
               LuxUI::StandardPaths::writableLocation(LuxUI::StandardPaths::DocumentsLocation))) /
           PathFromUtf8(name);
}

LuxUI::DialogButtonBox* Buttons(LuxUI::Dialog& dialog, LuxUI::VBoxLayout* layout) {
    auto* buttons = new LuxUI::DialogButtonBox(
        LuxUI::DialogButtonBox::Ok | LuxUI::DialogButtonBox::Cancel, &dialog);
    LuxUI::Object::connect(buttons, &LuxUI::DialogButtonBox::accepted, &dialog,
                           &LuxUI::Dialog::accept);
    LuxUI::Object::connect(buttons, &LuxUI::DialogButtonBox::rejected, &dialog,
                           &LuxUI::Dialog::reject);
    layout->addWidget(buttons);
    return buttons;
}
} // namespace

std::optional<ProjectCreationRequest> ShowNewProjectDialog(LuxUI::Widget* parent) {
    LuxUI::Dialog dialog(parent);
    dialog.setWindowTitle(LUX_UI_TEXT("새 프로젝트"));
    dialog.resize(560, 280);
    auto* layout = new LuxUI::VBoxLayout(&dialog);
    auto* form = new LuxUI::FormLayout;
    auto* name = new LuxUI::LineEdit(LUX_UI_TEXT("새 프로젝트"), &dialog);
    auto* path = new LuxUI::LineEdit(Q(SuggestedPath("NewLuxProject")), &dialog);
    auto* browse = new LuxUI::PushButton(LUX_UI_TEXT("찾아보기..."), &dialog);
    auto* row = new LuxUI::HBoxLayout;
    row->addWidget(path);
    row->addWidget(browse);
    form->addRow(LUX_UI_TEXT("프로젝트 이름"), name);
    form->addRow(LUX_UI_TEXT("프로젝트 폴더"), row);
    auto* templates = new LuxUI::ComboBox(&dialog);
    for (const char* label :
         {"빈 프로젝트", "플랫폼 게임", "탑다운 게임", "RPG", "퍼즐 게임", "3D PBR"})
        templates->addItem(LuxUI::String::fromUtf8(label));
    form->addRow(LUX_UI_TEXT("템플릿"), templates);
    layout->addLayout(form);
    auto* description = new LuxUI::Label(LUX_UI_TEXT("빈 장면과 Assets 폴더를 만듭니다."), &dialog);
    layout->addWidget(description);
    LuxUI::Object::connect(templates, &LuxUI::ComboBox::currentIndexChanged, &dialog,
                           [description](int index) {
                               static const char* descriptions[] = {
                                   "빈 장면과 Assets 폴더를 만듭니다.",
                                   "바닥과 플레이어가 있는 2D 시작 장면을 만듭니다.",
                                   "지면과 플레이어가 있는 2D 시작 장면을 만듭니다.",
                                   "마을과 주인공, NPC가 있는 2D 시작 장면을 만듭니다.",
                                   "보드와 블록이 있는 2D 시작 장면을 만듭니다.",
                                   "PBR 재질·환경 반사·그림자를 사용하는 3D 장면을 만듭니다."};
                               description->setText(LuxUI::String::fromUtf8(descriptions[index]));
                           });
    LuxUI::Object::connect(browse, &LuxUI::PushButton::clicked, &dialog, [path, name, &dialog] {
        const LuxUI::String directory = LuxUI::FileDialog::getExistingDirectory(
            &dialog, LUX_UI_TEXT("프로젝트 상위 폴더 선택"),
            LuxUI::StandardPaths::writableLocation(LuxUI::StandardPaths::DocumentsLocation));
        if (!directory.isEmpty())
            path->setText(directory + LuxUI::Latin1Char('/') + name->text());
    });
    Buttons(dialog, layout);
    if (dialog.exec() != LuxUI::Dialog::Accepted)
        return std::nullopt;
    return ProjectCreationRequest{PathFromUtf8(Utf8(path->text())), Utf8(name->text()),
                                  static_cast<ProjectTemplate>(templates->currentIndex())};
}

std::optional<ProjectSettingsRequest> ShowProjectSettingsDialog(LuxUI::Widget* parent,
                                                                const ProjectSettings& current) {
    LuxUI::Dialog dialog(parent);
    dialog.setWindowTitle(LUX_UI_TEXT("프로젝트 설정"));
    dialog.resize(600, 590);
    auto* layout = new LuxUI::VBoxLayout(&dialog);
    auto* form = new LuxUI::FormLayout;
    auto* name = new LuxUI::LineEdit(Q(current.name), &dialog);
    auto* version = new LuxUI::LineEdit(Q(current.version), &dialog);
    form->addRow(LUX_UI_TEXT("프로젝트 이름"), name);
    auto* versionRow = new LuxUI::HBoxLayout;
    versionRow->addWidget(version);
    for (size_t part = 0; part < 3; ++part) {
        auto* button = new LuxUI::PushButton(
            LuxUI::String::fromUtf8(std::array<const char*, 3>{"주 +1", "부 +1", "수정 +1"}[part]),
            &dialog);
        versionRow->addWidget(button);
        LuxUI::Object::connect(button, &LuxUI::PushButton::clicked, &dialog, [version, part] {
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
            version->setText(LUX_UI_TEXT("%1.%2.%3").arg(values[0]).arg(values[1]).arg(values[2]));
        });
    }
    form->addRow(LUX_UI_TEXT("버전 (주.부.수정)"), versionRow);
    auto* icon = new LuxUI::LineEdit(Q(GenericPathUtf8(current.icon)), &dialog);
    auto* iconRow = new LuxUI::HBoxLayout;
    iconRow->addWidget(icon);
    auto* browse = new LuxUI::PushButton(LUX_UI_TEXT("찾아보기..."), &dialog);
    iconRow->addWidget(browse);
    form->addRow(LUX_UI_TEXT("아이콘"), iconRow);
    LuxUI::Object::connect(browse, &LuxUI::PushButton::clicked, &dialog, [&dialog, icon] {
        const LuxUI::String file = LuxUI::FileDialog::getOpenFileName(
            &dialog, LUX_UI_TEXT("프로젝트 아이콘 선택"), {},
            LUX_UI_TEXT("이미지 (*.png *.jpg *.jpeg *.ico *.icns)"));
        if (!file.isEmpty())
            icon->setText(file);
    });
    auto* autosave = new LuxUI::SpinBox(&dialog);
    autosave->setRange(0, 3600);
    autosave->setValue(static_cast<int>(current.autosaveSeconds));
    form->addRow(LUX_UI_TEXT("자동 저장 간격 (초)"), autosave);
    auto* backup = new LuxUI::SpinBox(&dialog);
    backup->setRange(1, 100);
    backup->setValue(static_cast<int>(current.backupLimit));
    form->addRow(LUX_UI_TEXT("보관할 백업 개수"), backup);
    std::string dependencies;
    for (const auto& item : current.dependencies)
        dependencies += item.package + (item.version.empty() ? "" : " " + item.version) + "\n";
    auto* dependencyText = new LuxUI::TextEdit(Q(dependencies), &dialog);
    dependencyText->setMinimumHeight(90);
    form->addRow(LUX_UI_TEXT("CMake 종속성"), dependencyText);
    layout->addLayout(form);
    Buttons(dialog, layout);
    if (dialog.exec() != LuxUI::Dialog::Accepted)
        return std::nullopt;
    ProjectSettingsRequest request;
    request.settings = current;
    request.settings.name = Utf8(name->text());
    request.settings.version = Utf8(version->text());
    request.settings.autosaveSeconds = static_cast<uint32_t>(autosave->value());
    request.settings.backupLimit = static_cast<uint32_t>(backup->value());
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
            LuxUI::MessageBox::warning(
                parent, LUX_UI_TEXT("프로젝트 설정"),
                LUX_UI_TEXT("종속성은 한 줄에 패키지 이름과 버전만 입력해 주세요."));
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

std::optional<std::filesystem::path> ShowProjectDestinationDialog(LuxUI::Widget* parent,
                                                                  const char* title,
                                                                  const std::string& suggested) {
    bool accepted = false;
    const LuxUI::String value = LuxUI::InputDialog::getText(
        parent, LuxUI::String::fromUtf8(title),
        LUX_UI_TEXT("새 프로젝트 폴더의 전체 경로를 입력해 주세요."), LuxUI::LineEdit::Normal,
        Q(SuggestedPath(suggested)), &accepted);
    return accepted ? std::optional(PathFromUtf8(Utf8(value))) : std::nullopt;
}
} // namespace LuxEditor
