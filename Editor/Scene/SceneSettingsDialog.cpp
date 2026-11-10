#include "SceneSettingsDialog.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariant>
#include <array>

namespace LuxEditor {
std::optional<LuxEngine::SceneSettings>
ShowSceneSettingsDialog(QWidget* parent, const LuxEngine::SceneSettings& settings) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("씬 설정"));
    dialog.setMinimumWidth(360);
    auto* layout = new QVBoxLayout(&dialog);
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
    layout->addWidget(buttons);
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
