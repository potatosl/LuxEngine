#include <LuxEditor/Scene/SelectionTransform.hpp>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <unordered_set>

namespace LuxEditor {
namespace {
using namespace LuxEngine;

bool SameLifetime(const Scene& scene, const std::weak_ptr<int>& expected) {
    const auto current = scene.Lifetime();
    return !current.owner_before(expected) && !expected.owner_before(current);
}

bool Finite(const Vec3& vector) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

bool Finite(const TransformComponent& value) {
    return Finite(value.position) && Finite(value.rotation) && Finite(value.scale);
}

bool Same(const Vec3& first, const Vec3& second) {
    return first.x == second.x && first.y == second.y && first.z == second.z;
}

bool Same(const TransformComponent& first, const TransformComponent& second) {
    return Same(first.position, second.position) && Same(first.rotation, second.rotation) &&
           Same(first.scale, second.scale);
}

bool Same(const Mat4& first, const Mat4& second) {
    return std::equal(std::begin(first.elements), std::end(first.elements),
                      std::begin(second.elements));
}

Mat4 Rotation(Vec3 value) {
    return Mat4::RotateZ(value.z) * Mat4::RotateY(value.y) * Mat4::RotateX(value.x);
}

Mat4 TransposedRotation(const Mat4& rotation) {
    Mat4 result;
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            result.elements[column * 4 + row] = rotation.elements[row * 4 + column];
    return result;
}

Vec3 TransformPoint(const Mat4& matrix, Vec3 point) {
    const auto* m = matrix.elements;
    return {m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
            m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
            m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
}

bool Inverse(const Mat4& matrix, Mat4& result) {
    const auto* m = matrix.elements;
    const double a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6],
                 i = m[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        return false;
    const double scale = 1 / determinant;
    auto* out = result.elements;
    out[0] = static_cast<float>((e * i - f * h) * scale);
    out[4] = static_cast<float>((c * h - b * i) * scale);
    out[8] = static_cast<float>((b * f - c * e) * scale);
    out[1] = static_cast<float>((f * g - d * i) * scale);
    out[5] = static_cast<float>((a * i - c * g) * scale);
    out[9] = static_cast<float>((c * d - a * f) * scale);
    out[2] = static_cast<float>((d * h - e * g) * scale);
    out[6] = static_cast<float>((b * g - a * h) * scale);
    out[10] = static_cast<float>((a * e - b * d) * scale);
    const auto position = TransformPoint(result, {m[12], m[13], m[14]});
    out[12] = -position.x;
    out[13] = -position.y;
    out[14] = -position.z;
    return std::all_of(std::begin(result.elements), std::end(result.elements),
                       [](float value) { return std::isfinite(value); });
}

bool Decompose(const Mat4& matrix, TransformComponent& value) {
    const auto* m = matrix.elements;
    value.position = {m[12], m[13], m[14]};
    Vec3 x{m[0], m[1], m[2]}, y{m[4], m[5], m[6]}, z{m[8], m[9], m[10]};
    value.scale = {std::sqrt(x.Dot(x)), std::sqrt(y.Dot(y)), std::sqrt(z.Dot(z))};
    if (!Finite(value.scale) || value.scale.x < 1e-7f || value.scale.y < 1e-7f ||
        value.scale.z < 1e-7f)
        return false;
    if (x.Dot(y.Cross(z)) < 0)
        value.scale.x = -value.scale.x;
    x = x * (1 / value.scale.x);
    y = y * (1 / value.scale.y);
    z = z * (1 / value.scale.z);
    // TRS cannot encode shear. Never silently change an authored transform to approximate it.
    if (std::abs(x.Dot(y)) > 1e-4f || std::abs(x.Dot(z)) > 1e-4f || std::abs(y.Dot(z)) > 1e-4f)
        return false;
    value.rotation.y = std::asin(std::clamp(-x.z, -1.0f, 1.0f));
    if (std::abs(std::cos(value.rotation.y)) > 1e-5f) {
        value.rotation.x = std::atan2(y.z, z.z);
        value.rotation.z = std::atan2(x.y, x.x);
    } else {
        value.rotation.x = 0;
        value.rotation.z = std::atan2(-y.x, y.y);
    }
    return Finite(value);
}

bool ScaleRatio(float initial, float target, float& ratio) {
    if (initial == target) {
        ratio = 1;
        return true;
    }
    if (std::abs(initial) < 1e-7f)
        return false;
    ratio = target / initial;
    return std::isfinite(ratio) && std::abs(ratio) >= 1e-7f;
}
} // namespace

LuxEngine::TransformComponent GetSelectionTransformPivot(const EditorDocument& document) {
    auto result = LuxEngine::TransformComponent{};
    const auto& scene = document.GetScene();
    const auto tryWorld = [&](LuxEngine::uint32 id) {
        try {
            if (!scene.HasComponent<LuxEngine::TransformComponent>(id))
                return false;
            const auto candidate = scene.GetWorldTransform(id);
            if (!Finite(candidate))
                return false;
            result = candidate;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    const auto active = document.GetSelected();
    if (!active || !tryWorld(active.GetId())) {
        const auto ids = document.GetSelection();
        for (auto iterator = ids.rbegin(); iterator != ids.rend(); ++iterator)
            if (tryWorld(*iterator))
                break;
    }
    result.position = GetSelectionPivot(document);
    return result;
}

LuxEngine::Vec3 GetSelectionPivot(const EditorDocument& document) {
    const auto& scene = document.GetScene();
    const auto tryPosition = [&](LuxEngine::uint32 id, LuxEngine::Vec3& result) {
        try {
            if (!scene.HasComponent<LuxEngine::TransformComponent>(id))
                return false;
            const auto candidate = scene.GetWorldTransform(id).position;
            if (!Finite(candidate))
                return false;
            result = candidate;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    const auto ids = document.GetSelection();
    if (document.GetSelectionPivotMode() != SelectionPivot::Median) {
        LuxEngine::Vec3 position;
        const auto active = document.GetSelected();
        if (active && tryPosition(active.GetId(), position))
            return position;
        for (auto iterator = ids.rbegin(); iterator != ids.rend(); ++iterator)
            if (tryPosition(*iterator, position))
                return position;
        return {};
    }
    // Double accumulation keeps a large selection's pivot finite near large world coordinates.
    double x = 0, y = 0, z = 0;
    size_t count = 0;
    for (const auto id : ids) {
        LuxEngine::Vec3 value;
        if (!tryPosition(id, value))
            continue;
        x += value.x;
        y += value.y;
        z += value.z;
        ++count;
    }
    if (!count)
        return {};
    return {static_cast<float>(x / count), static_cast<float>(y / count),
            static_cast<float>(z / count)};
}

bool SelectionTransformSession::Begin(EditorDocument& document) {
    try {
        End();
        auto& scene = document.GetScene();
        const auto active = document.GetSelected();
        if (!active || !active.HasComponent<LuxEngine::TransformComponent>())
            return false;
        const auto initial = scene.GetWorldTransform(active.GetId());
        if (!Finite(initial))
            return false;
        const auto ids = document.GetSelection();
        const std::unordered_set<LuxEngine::uint32> selected(ids.begin(), ids.end());
        std::vector<Entry> next;
        for (const auto id : ids) {
            if (!scene.HasComponent<LuxEngine::TransformComponent>(id))
                continue;
            bool covered = false;
            auto parent = scene.GetParent(id);
            size_t depth = 0;
            for (auto ancestor = parent; ancestor && depth <= ids.size() + scene.GetEntityCount();
                 ancestor = scene.GetParent(ancestor), ++depth) {
                if (selected.contains(ancestor) &&
                    scene.HasComponent<LuxEngine::TransformComponent>(ancestor)) {
                    covered = true;
                    break;
                }
            }
            if (!covered)
                next.push_back({id, parent, scene.GetComponent<LuxEngine::TransformComponent>(id),
                                scene.GetWorldTransform(id), scene.GetWorldMatrix(id),
                                parent ? scene.GetWorldMatrix(parent) : LuxEngine::Mat4{}});
        }
        if (next.empty())
            return false;
        for (const auto& entry : next) {
            if (!Finite(entry.local) || !Finite(entry.world) ||
                !std::all_of(std::begin(entry.matrix.elements), std::end(entry.matrix.elements),
                             [](float value) { return std::isfinite(value); }) ||
                !std::all_of(std::begin(entry.parentMatrix.elements),
                             std::end(entry.parentMatrix.elements),
                             [](float value) { return std::isfinite(value); }))
                return false;
        }
        scene_ = &scene;
        lifetime_ = scene.Lifetime();
        activeId_ = active.GetId();
        initial_ = initial;
        pivot_ = GetSelectionPivot(document);
        space_ = document.GetTransformSpace();
        pivotMode_ = document.GetSelectionPivotMode();
        entries_ = std::move(next);
        return true;
    } catch (const std::exception&) {
        End();
        return false;
    }
}

bool SelectionTransformSession::Apply(LuxEngine::Scene& scene, LuxEngine::uint32 activeId,
                                      const LuxEngine::TransformComponent& target) {
    try {
        using namespace LuxEngine;
        if (!IsActive() || scene_ != &scene)
            return false;
        if (!SameLifetime(scene, lifetime_)) {
            End();
            return false;
        }
        if (activeId != activeId_ || !Finite(target) || !scene.IsAlive(activeId_))
            return false;
        Vec3 ratio;
        if (!ScaleRatio(initial_.scale.x, target.scale.x, ratio.x) ||
            !ScaleRatio(initial_.scale.y, target.scale.y, ratio.y) ||
            !ScaleRatio(initial_.scale.z, target.scale.z, ratio.z))
            return false;
        const auto translation = target.position - initial_.position;
        const auto originalRotation = Rotation(initial_.rotation);
        const auto targetRotation = Rotation(target.rotation);
        const auto inverseRotation = TransposedRotation(originalRotation);
        const auto rotationDelta = targetRotation * inverseRotation;
        const auto localRotationDelta = inverseRotation * targetRotation;
        const bool translateOnly =
            Same(initial_.rotation, target.rotation) && Same(initial_.scale, target.scale);
        std::vector<TransformComponent> locals;
        locals.reserve(entries_.size());
        for (const auto& entry : entries_) {
            if (!scene.HasComponent<TransformComponent>(entry.id) ||
                scene.GetParent(entry.id) != entry.parent ||
                (entry.parent && !Same(scene.GetWorldMatrix(entry.parent), entry.parentMatrix)))
                return false;
            Mat4 inverseParent;
            if (entry.parent && !Inverse(entry.parentMatrix, inverseParent))
                return false;
            TransformComponent local;
            if (translateOnly) {
                local = entry.local;
                local.position = TransformPoint(inverseParent, entry.world.position + translation);
            } else {
                const auto individual = pivotMode_ == SelectionPivot::Individual;
                const auto pivot = individual ? entry.world.position : pivot_;
                auto delta = rotationDelta;
                auto basis = space_ == TransformSpace::Local ? originalRotation : Mat4{};
                if (individual && space_ == TransformSpace::Local) {
                    basis = Rotation(entry.world.rotation);
                    delta = basis * localRotationDelta * TransposedRotation(basis);
                }
                const auto scale = basis * Mat4::Scale(ratio) * TransposedRotation(basis);
                const auto matrix = Mat4::Translate(translation) * Mat4::Translate(pivot) * delta *
                                    scale * Mat4::Translate(pivot * -1) * entry.matrix;
                if (!Decompose(inverseParent * matrix, local))
                    return false;
            }
            if (!Finite(local))
                return false;
            locals.push_back(local);
        }
        // No mutation occurs until every candidate has passed hierarchy, finiteness and TRS checks.
        changed_ = false;
        for (size_t index = 0; index < entries_.size(); ++index) {
            scene.GetComponent<TransformComponent>(entries_[index].id) = locals[index];
            changed_ |= !Same(locals[index], entries_[index].local);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool SelectionTransformSession::Cancel(LuxEngine::Scene& scene) {
    if (!IsActive() || scene_ != &scene)
        return false;
    if (!SameLifetime(scene, lifetime_)) {
        End();
        return false;
    }
    bool restored = true;
    for (const auto& entry : entries_) {
        if (auto* value = scene.TryGetComponent<LuxEngine::TransformComponent>(entry.id);
            value && scene.GetParent(entry.id) == entry.parent)
            *value = entry.local;
        else
            restored = false;
    }
    End();
    return restored;
}

void SelectionTransformSession::End() {
    scene_ = nullptr;
    lifetime_.reset();
    activeId_ = 0;
    entries_.clear();
    changed_ = false;
}

bool SelectionTransformSession::IsActive() const {
    return scene_ && !lifetime_.expired() && !entries_.empty();
}

bool SelectionTransformSession::HasChanged() const {
    return IsActive() && changed_;
}
} // namespace LuxEditor
