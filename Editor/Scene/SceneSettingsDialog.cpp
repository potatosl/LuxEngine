#include "SceneSettingsDialog.hpp"
#include "Engine/Assets/EnvironmentMap.hpp"
#include "Project/PathEncoding.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariant>
#include <array>

namespace LuxEditor {
std::optional<LuxEngine::SceneSettings>
ShowSceneSettingsDialog(QWidget* parent, const LuxEngine::SceneSettings& settings,
                        const std::string& sourcePath) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("씬 설정"));
    dialog.setMinimumWidth(360);
    dialog.resize(580, 760);
    auto* outer = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll);
    scroll->setWidget(body);
    outer->addWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    auto group = [&](const QString& title) {
        auto* box = new QGroupBox(title, &dialog);
        layout->addWidget(box);
        return new QFormLayout(box);
    };
    auto number = [&](QFormLayout* form, const QString& label, double value, double low,
                      double high, int decimals = 3) {
        auto* field = new QDoubleSpinBox(&dialog);
        field->setDecimals(decimals);
        field->setRange(low, high);
        field->setValue(value);
        field->setProperty("initialValue", field->value());
        field->setMinimumWidth(150);
        form->addRow(label, field);
        return field;
    };
    auto* environment = group(QStringLiteral("환경"));
    std::array<QDoubleSpinBox*, 4> clear;
    const std::array<float, 4> colors{settings.clearColor.x, settings.clearColor.y,
                                      settings.clearColor.z, settings.clearColor.w};
    const std::array<QString, 4> labels{QStringLiteral("배경 R"), QStringLiteral("배경 G"),
                                        QStringLiteral("배경 B"), QStringLiteral("배경 A")};
    for (size_t index = 0; index < clear.size(); ++index)
        clear[index] = number(environment, labels[index], colors[index], 0, 1);
    auto* rendering = group(QStringLiteral("렌더링"));
    auto* exposure = number(rendering, QStringLiteral("노출 배율"), settings.exposure, .05, 16);
    auto* mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("render3DMode"));
    mode->addItem(QStringLiteral("Legacy"));
    mode->addItem(QStringLiteral("PBR"));
    mode->setCurrentIndex(static_cast<int>(settings.render3D.mode));
    rendering->addRow(QStringLiteral("3D"), mode);
    const auto& r = settings.render3D;
    auto* sunGroup = group(QStringLiteral("방향광"));
    std::array<QDoubleSpinBox*, 3> sunDirection, sunColor, ambientColor;
    const std::array<float, 3> directions{r.sun.direction.x, r.sun.direction.y, r.sun.direction.z};
    const std::array<float, 3> sunColors{r.sun.color.x, r.sun.color.y, r.sun.color.z};
    const std::array<float, 3> ambientColors{r.environmentColor.x, r.environmentColor.y,
                                             r.environmentColor.z};
    for (size_t i = 0; i < 3; ++i) {
        sunDirection[i] =
            number(sunGroup, QStringLiteral("방향 %1").arg(QStringLiteral("XYZ").at(i)),
                   directions[i], -10000, 10000);
        sunColor[i] = number(sunGroup, QStringLiteral("색상 %1").arg(QStringLiteral("RGB").at(i)),
                             sunColors[i], 0, 65504);
    }
    auto* sunIntensity = number(sunGroup, QStringLiteral("광원 강도"), r.sun.intensity, 0, 10000);
    auto* lighting = group(QStringLiteral("환경광·HDR"));
    auto* ibl = new QCheckBox(QStringLiteral("환경 반사 (IBL)"), &dialog);
    ibl->setChecked(r.imageBasedLighting);
    lighting->addRow(ibl);
    auto* skybox = new QCheckBox(QStringLiteral("스카이박스 표시"), &dialog);
    skybox->setChecked(r.showSkybox);
    lighting->addRow(skybox);
    auto* tone = new QCheckBox(QStringLiteral("ACES 톤 매핑"), &dialog);
    tone->setChecked(r.toneMapping);
    lighting->addRow(tone);
    for (size_t i = 0; i < 3; ++i)
        ambientColor[i] =
            number(lighting, QStringLiteral("기본 환경색 %1").arg(QStringLiteral("RGB").at(i)),
                   ambientColors[i], 0, 65504);
    auto* ambientIntensity =
        number(lighting, QStringLiteral("환경 강도"), r.environmentIntensity, 0, 10000);
    auto* rotation =
        number(lighting, QStringLiteral("환경 회전 (도)"), r.environmentRotation, -36000, 36000);
    auto* path = new QLineEdit(QString::fromUtf8(r.environmentPath), &dialog);
    path->setObjectName(QStringLiteral("environmentPath"));
    path->setReadOnly(true);
    path->setPlaceholderText(QStringLiteral("내장 환경 사용"));
    lighting->addRow(QStringLiteral("HDR 이미지"), path);
    const auto resolve = [&](const QString& text) {
        auto file = PathFromUtf8(text.toUtf8().toStdString());
        if (file.is_relative() && !sourcePath.empty())
            file = PathFromUtf8(sourcePath).parent_path() / file;
        return file;
    };
    auto* choose = new QPushButton(QStringLiteral("환경 이미지 선택..."), &dialog);
    lighting->addRow(choose);
    QObject::connect(choose, &QPushButton::clicked, &dialog, [&] {
        const auto file =
            QFileDialog::getOpenFileName(&dialog, QStringLiteral("환경 이미지 선택"), {},
                                         QStringLiteral("Radiance HDR (*.hdr *.HDR)"));
        if (file.isEmpty())
            return;
        std::string error;
        if (!LuxEngine::EnvironmentData::ValidateFile(file.toUtf8().toStdString(), error)) {
            QMessageBox::warning(&dialog, QStringLiteral("환경 이미지 오류"),
                                 QString::fromUtf8(error));
            return;
        }
        path->setText(file);
    });
    auto* clearEnvironment = new QPushButton(QStringLiteral("내장 환경으로 초기화"), &dialog);
    lighting->addRow(clearEnvironment);
    QObject::connect(clearEnvironment, &QPushButton::clicked, path, &QLineEdit::clear);
    auto* reload = new QPushButton(QStringLiteral("환경 다시 불러오기"), &dialog);
    lighting->addRow(reload);
    QObject::connect(reload, &QPushButton::clicked, &dialog, [&] {
        std::string error;
        const auto file =
            path->text().isEmpty() ? std::string{} : GenericPathUtf8(resolve(path->text()));
        if (!LuxEngine::EnvironmentData::Reload(file, error))
            QMessageBox::warning(&dialog, QStringLiteral("환경 이미지 오류"),
                                 QString::fromUtf8(error));
    });
    auto* physics = group(QStringLiteral("물리"));
    auto* overrideGravity = new QCheckBox(QStringLiteral("씬 중력 사용"), &dialog);
    overrideGravity->setChecked(settings.overrideGravity);
    physics->addRow(overrideGravity);
    auto* gx = number(physics, QStringLiteral("2D 중력 X"), settings.gravity.x, -10000, 10000);
    auto* gy = number(physics, QStringLiteral("2D 중력 Y"), settings.gravity.y, -10000, 10000);
    std::array<QDoubleSpinBox*, 3> gravity3D;
    const std::array<float, 3> gravity{settings.gravity3D.x, settings.gravity3D.y,
                                       settings.gravity3D.z};
    for (size_t index = 0; index < gravity3D.size(); ++index)
        gravity3D[index] = number(physics,
                                  QStringLiteral("3D 중력 %1")
                                      .arg(QStringLiteral("XYZ").at(static_cast<qsizetype>(index))),
                                  gravity[index], -10000, 10000);
    auto* step = number(physics, QStringLiteral("고정 시간 간격 (초)"), settings.fixedStep,
                        1.0 / 240.0, .25, 6);
    auto* substeps = new QSpinBox(&dialog);
    substeps->setRange(1, 16);
    substeps->setValue(static_cast<int>(settings.physicsSubsteps));
    physics->addRow(QStringLiteral("물리 하위 단계"), substeps);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    const auto value = [](QDoubleSpinBox* field, float previous) {
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
