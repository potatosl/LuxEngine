#include <LuxEditor/App/EditorWindow.hpp>
#include <LuxEditor/Project/PathEncoding.hpp>
#include <LuxEngine/Scene/Entity.hpp>
#include <LuxEngine/Scripting/ScriptRuntime.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <limits>
#include <type_traits>

namespace LuxEditor {
void EditorWindow::BuildScriptDetails(QVBoxLayout* layout) {
    auto* group = new QGroupBox(QStringLiteral("C# Script"), inspectorBody_);
    auto* form = new QFormLayout(group);
    scriptAttached_ = new QCheckBox(QStringLiteral("스크립트 컴포넌트"), group);
    scriptAttached_->setObjectName(QStringLiteral("scriptAttached"));
    scriptEnabled_ = new QCheckBox(QStringLiteral("실행"), group);
    scriptEnabled_->setObjectName(QStringLiteral("scriptEnabled"));
    scriptType_ = new QLineEdit(group);
    scriptType_->setObjectName(QStringLiteral("scriptType"));
    scriptType_->setMaxLength(512);
    auto* inspect = new QPushButton(QStringLiteral("어셈블리에서 필드 읽기…"), group);
    inspect->setObjectName(QStringLiteral("scriptInspectAssembly"));
    scriptFieldsPanel_ = new QWidget(group);
    scriptFieldsForm_ = new QFormLayout(scriptFieldsPanel_);
    scriptFieldsForm_->setContentsMargins(0, 0, 0, 0);
    form->addRow(scriptAttached_);
    form->addRow(QStringLiteral("클래스"), scriptType_);
    form->addRow(scriptEnabled_);
    form->addRow(inspect);
    form->addRow(scriptFieldsPanel_);
    layout->addWidget(group);
    connect(scriptAttached_, &QCheckBox::toggled, this, [this](bool attached) {
        if (refreshingInspector_)
            return;
        RecordEdit(QStringLiteral("C# 컴포넌트 변경"), [this, attached] {
            auto entity = document_.GetSelected();
            if (!entity)
                return;
            if (attached && !entity.HasComponent<LuxEngine::ScriptComponent>())
                entity.AddComponent<LuxEngine::ScriptComponent>();
            else if (!attached && entity.HasComponent<LuxEngine::ScriptComponent>())
                entity.RemoveComponent<LuxEngine::ScriptComponent>();
            document_.MarkDirty();
        });
        RefreshInspector();
    });
    connect(scriptEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (refreshingInspector_)
            return;
        RecordEdit(QStringLiteral("C# 실행 설정"), [this, enabled] {
            if (auto* script =
                    document_.GetSelected().TryGetComponent<LuxEngine::ScriptComponent>()) {
                script->enabled = enabled;
                document_.MarkDirty();
            }
        });
    });
    connect(scriptType_, &QLineEdit::editingFinished, this, [this] {
        if (refreshingInspector_)
            return;
        const auto type = scriptType_->text().trimmed().toUtf8().toStdString();
        RecordEdit(QStringLiteral("C# 클래스 변경"), [this, type] {
            if (auto* script =
                    document_.GetSelected().TryGetComponent<LuxEngine::ScriptComponent>()) {
                if (script->typeName != type)
                    script->fields.clear();
                script->typeName = type;
                document_.MarkDirty();
            }
        });
        QTimer::singleShot(0, this, [this] { RefreshInspector(); });
    });
    connect(inspect, &QPushButton::clicked, this, [this] { InspectScriptFields(); });
}

void EditorWindow::InspectScriptFields() {
    const auto entity = document_.GetSelected();
    const auto* script = entity.TryGetComponent<LuxEngine::ScriptComponent>();
    if (!script || script->typeName.empty()) {
        ShowError(QStringLiteral("C# 필드"),
                  QStringLiteral("먼저 스크립트 클래스 이름을 지정하세요."));
        return;
    }
    const auto tab = document_.GetActiveTabId();
    const auto id = entity.GetId();
    const auto lifetime = document_.GetScene().Lifetime();
    const auto typeName = script->typeName;
    const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("게임 스크립트 어셈블리"),
                                                   {}, QStringLiteral(".NET Assembly (*.dll)"));
    if (file.isEmpty())
        return;
    if (lifetime.expired() || document_.GetActiveTabId() != tab ||
        document_.GetSelectedId(tab) != id) {
        Log(QStringLiteral("선택이 변경되어 C# 필드 읽기를 취소했습니다."), true);
        return;
    }
    script = document_.GetScene().TryGetComponent<LuxEngine::ScriptComponent>(id);
    if (!script || script->typeName != typeName) {
        Log(QStringLiteral("스크립트가 변경되어 필드 읽기를 취소했습니다."), true);
        return;
    }
    LuxEngine::ScriptRuntime runtime;
    LuxEngine::ScriptRuntimeConfig config;
    config.assemblyPath = PathFromUtf8(file.toUtf8().toStdString());
    std::vector<LuxEngine::ScriptFieldDescription> fields;
    if (!runtime.Initialize(document_.GetScene(), config) ||
        !runtime.DescribeFields(typeName, fields)) {
        ShowError(QStringLiteral("C# 필드"), QString::fromUtf8(runtime.GetLastError().c_str()));
        return;
    }
    LuxEngine::ScriptFieldMap values;
    for (auto& description : fields) {
        const auto old = script->fields.find(description.name);
        values.emplace(description.name,
                       old != script->fields.end() &&
                               old->second.index() == description.defaultValue.index()
                           ? old->second
                           : description.defaultValue);
    }
    RecordEdit(QStringLiteral("C# 필드 불러오기"), [this, id, values = std::move(values)] {
        document_.GetScene().GetComponent<LuxEngine::ScriptComponent>(id).fields = values;
        document_.MarkDirty();
    });
    RefreshInspector();
}

void EditorWindow::SetScriptField(const std::string& name,
                                  const LuxEngine::ScriptFieldValue& value) {
    if (refreshingInspector_)
        return;
    const auto entity = document_.GetSelected();
    if (!entity || !entity.HasComponent<LuxEngine::ScriptComponent>())
        return;
    const auto id = entity.GetId();
    RecordEdit(QStringLiteral("C# 필드 변경"), [this, id, name, value] {
        auto& fields = document_.GetScene().GetComponent<LuxEngine::ScriptComponent>(id).fields;
        const auto found = fields.find(name);
        if (found != fields.end() && found->second.index() == value.index()) {
            found->second = value;
            document_.MarkDirty();
        }
    });
}

void EditorWindow::RefreshScriptDetails() {
    if (!scriptAttached_)
        return;
    const auto entity = document_.GetSelected();
    const auto* script = entity.TryGetComponent<LuxEngine::ScriptComponent>();
    const QSignalBlocker attached(scriptAttached_), enabled(scriptEnabled_), type(scriptType_);
    scriptAttached_->setChecked(script != nullptr);
    scriptEnabled_->setChecked(script && script->enabled);
    scriptEnabled_->setEnabled(script != nullptr);
    scriptType_->setEnabled(script != nullptr);
    scriptType_->setText(script ? QString::fromUtf8(script->typeName.c_str()) : QString{});
    while (scriptFieldsForm_->rowCount())
        scriptFieldsForm_->removeRow(0);
    if (!script)
        return;
    for (const auto& [name, value] : script->fields) {
        QWidget* widget = nullptr;
        std::visit(
            [&](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, bool>) {
                    auto* field = new QCheckBox(scriptFieldsPanel_);
                    field->setChecked(item);
                    connect(field, &QCheckBox::toggled, this,
                            [this, name](bool next) { SetScriptField(name, next); });
                    widget = field;
                } else if constexpr (std::is_same_v<T, LuxEngine::int32>) {
                    auto* field = new QSpinBox(scriptFieldsPanel_);
                    field->setRange(std::numeric_limits<int>::min(),
                                    std::numeric_limits<int>::max());
                    field->setValue(item);
                    field->setKeyboardTracking(false);
                    connect(field, qOverload<int>(&QSpinBox::valueChanged), this,
                            [this, name](int next) {
                                SetScriptField(name, static_cast<LuxEngine::int32>(next));
                            });
                    widget = field;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    auto* field =
                        new QLineEdit(QString::fromUtf8(item.c_str()), scriptFieldsPanel_);
                    field->setMaxLength(16384);
                    connect(field, &QLineEdit::editingFinished, this, [this, name, field] {
                        SetScriptField(name, field->text().toUtf8().toStdString());
                    });
                    widget = field;
                } else if constexpr (std::is_same_v<T, LuxEngine::ScriptEntityReference>) {
                    auto* field = new QComboBox(scriptFieldsPanel_);
                    field->addItem(QStringLiteral("없음"), 0u);
                    for (const auto candidate : document_.GetScene().GetEntities()) {
                        const auto* tag = candidate.TryGetComponent<LuxEngine::TagComponent>();
                        field->addItem(QStringLiteral("%1 (#%2)")
                                           .arg(tag ? QString::fromUtf8(tag->tag.c_str())
                                                    : QStringLiteral("Entity"))
                                           .arg(candidate.GetId()),
                                       candidate.GetId());
                    }
                    int selected = field->findData(item.id);
                    if (selected < 0) {
                        field->addItem(QStringLiteral("없는 엔티티 #%1").arg(item.id), item.id);
                        selected = field->count() - 1;
                    }
                    field->setCurrentIndex(selected);
                    connect(field, &QComboBox::currentIndexChanged, this, [this, name, field] {
                        SetScriptField(
                            name, LuxEngine::ScriptEntityReference{field->currentData().toUInt()});
                    });
                    widget = field;
                } else {
                    auto* row = new QWidget(scriptFieldsPanel_);
                    auto* layout = new QHBoxLayout(row);
                    layout->setContentsMargins(0, 0, 0, 0);
                    const auto add = [&](float current, int axis) {
                        auto* field = new QDoubleSpinBox(row);
                        field->setRange(-std::numeric_limits<float>::max(),
                                        std::numeric_limits<float>::max());
                        field->setDecimals(6);
                        field->setValue(current);
                        field->setKeyboardTracking(false);
                        field->setObjectName(QStringLiteral("scriptField_%1_%2")
                                                 .arg(QString::fromUtf8(name.c_str()))
                                                 .arg(axis));
                        layout->addWidget(field);
                        connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                                [this, name, axis](double next) {
                                    const auto entity = document_.GetSelected();
                                    const auto* script =
                                        entity.TryGetComponent<LuxEngine::ScriptComponent>();
                                    if (!script || !script->fields.contains(name))
                                        return;
                                    T updated = std::get<T>(script->fields.at(name));
                                    if constexpr (std::is_same_v<T, float>)
                                        updated = static_cast<float>(next);
                                    else {
                                        if (axis == 0)
                                            updated.x = static_cast<float>(next);
                                        if (axis == 1)
                                            updated.y = static_cast<float>(next);
                                        if constexpr (!std::is_same_v<T, LuxEngine::Vec2>)
                                            if (axis == 2)
                                                updated.z = static_cast<float>(next);
                                        if constexpr (std::is_same_v<T, LuxEngine::Vec4>)
                                            if (axis == 3)
                                                updated.w = static_cast<float>(next);
                                    }
                                    SetScriptField(name, updated);
                                });
                    };
                    if constexpr (std::is_same_v<T, float>)
                        add(item, 0);
                    else {
                        add(item.x, 0);
                        add(item.y, 1);
                        if constexpr (!std::is_same_v<T, LuxEngine::Vec2>)
                            add(item.z, 2);
                        if constexpr (std::is_same_v<T, LuxEngine::Vec4>)
                            add(item.w, 3);
                    }
                    widget = row;
                }
            },
            value);
        widget->setObjectName(
            QStringLiteral("scriptField_%1").arg(QString::fromUtf8(name.c_str())));
        scriptFieldsForm_->addRow(QString::fromUtf8(name.c_str()), widget);
    }
}
} // namespace LuxEditor
