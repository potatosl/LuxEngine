#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEditor/Scene/SceneSettingsDialog.hpp>
#include <LuxEngine/Environment/EnvironmentMap.hpp>
#include <LuxUI/Widgets.hpp>
#include <array>

namespace LuxEditor {
std::optional<LuxEngine::SceneSettings>
ShowSceneSettingsDialog(LuxUI::Widget* parent, const LuxEngine::SceneSettings& settings,
                        const std::string& sourcePath) {
    LuxUI::Dialog dialog(parent);
    dialog.setWindowTitle(LUX_UI_TEXT("씬 설정"));
    dialog.setMinimumWidth(360);
    dialog.resize(580, 760);
    auto* outer = new LuxUI::VBoxLayout(&dialog);
    auto* scroll = new LuxUI::ScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto* body = new LuxUI::Widget(scroll);
    scroll->setWidget(body);
    outer->addWidget(scroll);
    auto* layout = new LuxUI::VBoxLayout(body);
    auto group = [&](const LuxUI::String& title) {
        auto* box = new LuxUI::GroupBox(title, &dialog);
        layout->addWidget(box);
        return new LuxUI::FormLayout(box);
    };
    auto number = [&](LuxUI::FormLayout* form, const LuxUI::String& label, double value, double low,
                      double high, int decimals = 3) {
        auto* field = new LuxUI::DoubleSpinBox(&dialog);
        field->setDecimals(decimals);
        field->setRange(low, high);
        field->setValue(value);
        field->setProperty("initialValue", field->value());
        field->setMinimumWidth(150);
        form->addRow(label, field);
        return field;
    };
    auto* environment = group(LUX_UI_TEXT("환경"));
    std::array<LuxUI::DoubleSpinBox*, 4> clear;
    const std::array<float, 4> colors{settings.clearColor.x, settings.clearColor.y,
                                      settings.clearColor.z, settings.clearColor.w};
    const std::array<LuxUI::String, 4> labels{LUX_UI_TEXT("배경 R"), LUX_UI_TEXT("배경 G"),
                                              LUX_UI_TEXT("배경 B"), LUX_UI_TEXT("배경 A")};
    for (size_t index = 0; index < clear.size(); ++index)
        clear[index] = number(environment, labels[index], colors[index], 0, 1);
    auto* rendering = group(LUX_UI_TEXT("렌더링"));
    auto* exposure = number(rendering, LUX_UI_TEXT("노출 배율"), settings.exposure, .05, 16);
    auto* mode = new LuxUI::ComboBox(&dialog);
    mode->setObjectName(LUX_UI_TEXT("render3DMode"));
    mode->addItem(LUX_UI_TEXT("Legacy"));
    mode->addItem(LUX_UI_TEXT("PBR"));
    mode->setCurrentIndex(static_cast<int>(settings.render3D.mode));
    rendering->addRow(LUX_UI_TEXT("3D"), mode);
    const auto& r = settings.render3D;
    auto* sunGroup = group(LUX_UI_TEXT("방향광"));
    std::array<LuxUI::DoubleSpinBox*, 3> sunDirection, sunColor, ambientColor;
    const std::array<float, 3> directions{r.sun.direction.x, r.sun.direction.y, r.sun.direction.z};
    const std::array<float, 3> sunColors{r.sun.color.x, r.sun.color.y, r.sun.color.z};
    const std::array<float, 3> ambientColors{r.environmentColor.x, r.environmentColor.y,
                                             r.environmentColor.z};
    for (size_t i = 0; i < 3; ++i) {
        sunDirection[i] = number(sunGroup, LUX_UI_TEXT("방향 %1").arg(LUX_UI_TEXT("XYZ").at(i)),
                                 directions[i], -10000, 10000);
        sunColor[i] = number(sunGroup, LUX_UI_TEXT("색상 %1").arg(LUX_UI_TEXT("RGB").at(i)),
                             sunColors[i], 0, 65504);
    }
    auto* sunIntensity = number(sunGroup, LUX_UI_TEXT("광원 강도"), r.sun.intensity, 0, 10000);
    auto* lighting = group(LUX_UI_TEXT("환경광·HDR"));
    auto* ibl = new LuxUI::CheckBox(LUX_UI_TEXT("환경 반사 (IBL)"), &dialog);
    ibl->setChecked(r.imageBasedLighting);
    lighting->addRow(ibl);
    auto* skybox = new LuxUI::CheckBox(LUX_UI_TEXT("스카이박스 표시"), &dialog);
    skybox->setChecked(r.showSkybox);
    lighting->addRow(skybox);
    auto* tone = new LuxUI::CheckBox(LUX_UI_TEXT("ACES 톤 매핑"), &dialog);
    tone->setChecked(r.toneMapping);
    lighting->addRow(tone);
    for (size_t i = 0; i < 3; ++i)
        ambientColor[i] =
            number(lighting, LUX_UI_TEXT("기본 환경색 %1").arg(LUX_UI_TEXT("RGB").at(i)),
                   ambientColors[i], 0, 65504);
    auto* ambientIntensity =
        number(lighting, LUX_UI_TEXT("환경 강도"), r.environmentIntensity, 0, 10000);
    auto* rotation =
        number(lighting, LUX_UI_TEXT("환경 회전 (도)"), r.environmentRotation, -36000, 36000);
    auto* path = new LuxUI::LineEdit(LuxUI::String::fromUtf8(r.environmentPath), &dialog);
    path->setObjectName(LUX_UI_TEXT("environmentPath"));
    path->setReadOnly(true);
    path->setPlaceholderText(LUX_UI_TEXT("내장 환경 사용"));
    lighting->addRow(LUX_UI_TEXT("HDR 이미지"), path);
    const auto resolve = [&](const LuxUI::String& text) {
        auto file = PathFromUtf8(text.toUtf8().toStdString());
        if (file.is_relative() && !sourcePath.empty())
            file = PathFromUtf8(sourcePath).parent_path() / file;
        return file;
    };
    auto* choose = new LuxUI::PushButton(LUX_UI_TEXT("환경 이미지 선택..."), &dialog);
    lighting->addRow(choose);
    LuxUI::Object::connect(choose, &LuxUI::PushButton::clicked, &dialog, [&] {
        const auto file =
            LuxUI::FileDialog::getOpenFileName(&dialog, LUX_UI_TEXT("환경 이미지 선택"), {},
                                               LUX_UI_TEXT("Radiance HDR (*.hdr *.HDR)"));
        if (file.isEmpty())
            return;
        std::string error;
        if (!LuxEngine::EnvironmentData::ValidateFile(file.toUtf8().toStdString(), error)) {
            LuxUI::MessageBox::warning(&dialog, LUX_UI_TEXT("환경 이미지 오류"),
                                       LuxUI::String::fromUtf8(error));
            return;
        }
        path->setText(file);
    });
    auto* clearEnvironment = new LuxUI::PushButton(LUX_UI_TEXT("내장 환경으로 초기화"), &dialog);
    lighting->addRow(clearEnvironment);
    LuxUI::Object::connect(clearEnvironment, &LuxUI::PushButton::clicked, path,
                           &LuxUI::LineEdit::clear);
    auto* reload = new LuxUI::PushButton(LUX_UI_TEXT("환경 다시 불러오기"), &dialog);
    lighting->addRow(reload);
    LuxUI::Object::connect(reload, &LuxUI::PushButton::clicked, &dialog, [&] {
        std::string error;
        const auto file =
            path->text().isEmpty() ? std::string{} : GenericPathUtf8(resolve(path->text()));
        if (!LuxEngine::EnvironmentData::Reload(file, error))
            LuxUI::MessageBox::warning(&dialog, LUX_UI_TEXT("환경 이미지 오류"),
                                       LuxUI::String::fromUtf8(error));
    });
    auto* shadowGroup = group(LUX_UI_TEXT("방향광 그림자"));
    auto* shadows = new LuxUI::CheckBox(LUX_UI_TEXT("그림자 사용"), &dialog);
    shadows->setObjectName(LUX_UI_TEXT("directionalShadows"));
    shadows->setChecked(r.shadows);
    shadowGroup->addRow(shadows);
    auto* resolution = new LuxUI::ComboBox(&dialog);
    for (int size : {1024, 2048, 4096})
        resolution->addItem(LuxUI::String::number(size), size);
    resolution->setCurrentIndex(resolution->findData(static_cast<int>(r.shadowResolution)));
    shadowGroup->addRow(LUX_UI_TEXT("해상도"), resolution);
    auto* range = number(shadowGroup, LUX_UI_TEXT("적용 영역 크기"), r.shadowRange, 1, 10000);
    auto* depthBias =
        number(shadowGroup, LUX_UI_TEXT("깊이 바이어스"), r.shadowDepthBias, 0, .1, 6);
    auto* normalBias =
        number(shadowGroup, LUX_UI_TEXT("법선 바이어스"), r.shadowNormalBias, 0, 10, 4);
    auto* physics = group(LUX_UI_TEXT("물리"));
    auto* overrideGravity = new LuxUI::CheckBox(LUX_UI_TEXT("씬 중력 사용"), &dialog);
    overrideGravity->setChecked(settings.overrideGravity);
    physics->addRow(overrideGravity);
    auto* gx = number(physics, LUX_UI_TEXT("2D 중력 X"), settings.gravity.x, -10000, 10000);
    auto* gy = number(physics, LUX_UI_TEXT("2D 중력 Y"), settings.gravity.y, -10000, 10000);
    std::array<LuxUI::DoubleSpinBox*, 3> gravity3D;
    const std::array<float, 3> gravity{settings.gravity3D.x, settings.gravity3D.y,
                                       settings.gravity3D.z};
    for (size_t index = 0; index < gravity3D.size(); ++index)
        gravity3D[index] = number(
            physics,
            LUX_UI_TEXT("3D 중력 %1").arg(LUX_UI_TEXT("XYZ").at(static_cast<qsizetype>(index))),
            gravity[index], -10000, 10000);
    auto* step = number(physics, LUX_UI_TEXT("고정 시간 간격 (초)"), settings.fixedStep,
                        1.0 / 240.0, .25, 6);
    auto* substeps = new LuxUI::SpinBox(&dialog);
    substeps->setRange(1, 16);
    substeps->setValue(static_cast<int>(settings.physicsSubsteps));
    physics->addRow(LUX_UI_TEXT("물리 하위 단계"), substeps);
    auto* buttons = new LuxUI::DialogButtonBox(
        LuxUI::DialogButtonBox::Ok | LuxUI::DialogButtonBox::Cancel, &dialog);
    outer->addWidget(buttons);
    LuxUI::Object::connect(buttons, &LuxUI::DialogButtonBox::accepted, &dialog,
                           &LuxUI::Dialog::accept);
    LuxUI::Object::connect(buttons, &LuxUI::DialogButtonBox::rejected, &dialog,
                           &LuxUI::Dialog::reject);
    if (dialog.exec() != LuxUI::Dialog::Accepted)
        return std::nullopt;
    const auto value = [](LuxUI::DoubleSpinBox* field, float previous) {
        return field->value() == field->property("initialValue").toDouble()
                   ? previous
                   : static_cast<float>(field->value());
    };
    auto result = settings;
    result.clearColor = {
        value(clear[0], settings.clearColor.x), value(clear[1], settings.clearColor.y),
        value(clear[2], settings.clearColor.z), value(clear[3], settings.clearColor.w)};
    result.exposure = value(exposure, settings.exposure);
    result.render3D.mode = static_cast<LuxEngine::Render3DMode>(mode->currentIndex());
    auto& render = result.render3D;
    render.sun.direction = {value(sunDirection[0], r.sun.direction.x),
                            value(sunDirection[1], r.sun.direction.y),
                            value(sunDirection[2], r.sun.direction.z)};
    render.sun.color = {value(sunColor[0], r.sun.color.x), value(sunColor[1], r.sun.color.y),
                        value(sunColor[2], r.sun.color.z)};
    render.sun.intensity = value(sunIntensity, r.sun.intensity);
    render.environmentColor = {value(ambientColor[0], r.environmentColor.x),
                               value(ambientColor[1], r.environmentColor.y),
                               value(ambientColor[2], r.environmentColor.z)};
    render.environmentIntensity = value(ambientIntensity, r.environmentIntensity);
    render.environmentRotation = value(rotation, r.environmentRotation);
    render.environmentPath = path->text().toUtf8().toStdString();
    render.imageBasedLighting = ibl->isChecked();
    render.showSkybox = skybox->isChecked();
    render.toneMapping = tone->isChecked();
    render.shadows = shadows->isChecked();
    render.shadowResolution = static_cast<LuxEngine::uint32>(resolution->currentData().toUInt());
    render.shadowRange = value(range, r.shadowRange);
    render.shadowDepthBias = value(depthBias, r.shadowDepthBias);
    render.shadowNormalBias = value(normalBias, r.shadowNormalBias);
    result.overrideGravity = overrideGravity->isChecked();
    result.gravity = {value(gx, settings.gravity.x), value(gy, settings.gravity.y)};
    result.gravity3D = {value(gravity3D[0], settings.gravity3D.x),
                        value(gravity3D[1], settings.gravity3D.y),
                        value(gravity3D[2], settings.gravity3D.z)};
    result.fixedStep = value(step, settings.fixedStep);
    result.physicsSubsteps = static_cast<LuxEngine::uint32>(substeps->value());
    return result;
}
} // namespace LuxEditor
