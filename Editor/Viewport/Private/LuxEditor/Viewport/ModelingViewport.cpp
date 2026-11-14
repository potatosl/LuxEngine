#include <LuxEditor/Scene/EditorDocument.hpp>
#include <LuxEditor/Viewport/EngineSceneRenderer.hpp>
#include <LuxEditor/Viewport/ModelingViewport.hpp>
#include <LuxEditor/Viewport/SceneViewport3D.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>

namespace LuxEditor {
namespace {
using namespace LuxEngine;

Vec3 Point(const Mat4& matrix, Vec3 point) {
    const auto* m = matrix.elements;
    return {m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12],
            m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13],
            m[2] * point.x + m[6] * point.y + m[10] * point.z + m[14]};
}

bool Inverse(const Mat4& value, Mat4& inverse) {
    const auto* m = value.elements;
    const double a = m[0], b = m[4], c = m[8];
    const double d = m[1], e = m[5], f = m[9];
    const double g = m[2], h = m[6], i = m[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-20 || std::abs(m[3]) > 1e-6 ||
        std::abs(m[7]) > 1e-6 || std::abs(m[11]) > 1e-6 || std::abs(m[15] - 1) > 1e-6)
        return false;
    inverse.elements[0] = float((e * i - f * h) / determinant);
    inverse.elements[4] = float((c * h - b * i) / determinant);
    inverse.elements[8] = float((b * f - c * e) / determinant);
    inverse.elements[1] = float((f * g - d * i) / determinant);
    inverse.elements[5] = float((a * i - c * g) / determinant);
    inverse.elements[9] = float((c * d - a * f) / determinant);
    inverse.elements[2] = float((d * h - e * g) / determinant);
    inverse.elements[6] = float((b * g - a * h) / determinant);
    inverse.elements[10] = float((a * e - b * d) / determinant);
    const auto translation = Point(inverse, {m[12], m[13], m[14]}) * -1;
    inverse.elements[12] = translation.x;
    inverse.elements[13] = translation.y;
    inverse.elements[14] = translation.z;
    return std::all_of(std::begin(inverse.elements), std::end(inverse.elements),
                       [](float component) { return std::isfinite(component); });
}

std::optional<double> Intersect(const ViewportRay3D& ray, Vec3 a, Vec3 b, Vec3 c) {
    const auto first = b - a, second = c - a;
    const auto p = ray.direction.Cross(second);
    const double determinant = first.Dot(p);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        return std::nullopt;
    const auto offset = ray.origin - a;
    const double u = offset.Dot(p) / determinant;
    const auto q = offset.Cross(first);
    const double v = ray.direction.Dot(q) / determinant;
    const double distance = second.Dot(q) / determinant;
    if (u < 0 || v < 0 || u + v > 1 || !std::isfinite(distance) || distance < 0)
        return std::nullopt;
    return distance;
}
} // namespace

std::optional<uint32> PickModelingVertex(const ModelingMesh& mesh, const Mat4& world,
                                         const PerspectiveCamera& camera, LuxUI::SizeF viewport,
                                         LuxUI::PointF point, double radius) {
    if (!std::isfinite(radius) || radius <= 0)
        return std::nullopt;
    std::optional<uint32> selected;
    double best = radius;
    double depth = std::numeric_limits<double>::infinity();
    const auto forward = (camera.GetTarget() - camera.GetPosition()).Normalized();
    for (uint32 id = 0; id < mesh.vertices.size(); ++id) {
        const auto position = Point(world, mesh.vertices[id].position);
        const auto projected = ProjectViewportPoint(camera, viewport, position);
        if (!projected)
            continue;
        const double distance = LuxUI::LineF(*projected, point).length();
        const double candidateDepth = (position - camera.GetPosition()).Dot(forward);
        if (distance > radius || distance > best + .5 ||
            (std::abs(distance - best) <= .5 && candidateDepth >= depth))
            continue;
        selected = id;
        best = distance;
        depth = candidateDepth;
    }
    return selected;
}

std::optional<uint32> PickModelingFace(const ModelingMesh& mesh, const Mat4& world,
                                       const ViewportRay3D& ray) {
    std::optional<uint32> selected;
    double nearest = std::numeric_limits<double>::infinity();
    for (size_t offset = 0; offset + 2 < mesh.indices.size(); offset += 3) {
        const auto a = mesh.indices[offset], b = mesh.indices[offset + 1],
                   c = mesh.indices[offset + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size())
            continue;
        const auto distance = Intersect(ray, Point(world, mesh.vertices[a].position),
                                        Point(world, mesh.vertices[b].position),
                                        Point(world, mesh.vertices[c].position));
        if (distance && *distance < nearest) {
            nearest = *distance;
            selected = static_cast<uint32>(offset / 3);
        }
    }
    return selected;
}

std::vector<uint32> ModelingFaceVertices(const ModelingMesh& mesh, std::span<const uint32> faces) {
    std::set<uint32> selected;
    for (const auto face : faces) {
        if (face >= mesh.indices.size() / 3)
            continue;
        for (size_t corner = 0; corner < 3; ++corner) {
            const auto id = mesh.indices[static_cast<size_t>(face) * 3 + corner];
            if (id >= mesh.vertices.size())
                continue;
            selected.insert(id);
        }
    }
    if (selected.empty() || selected.size() == mesh.vertices.size())
        return {selected.begin(), selected.end()};
    // Expand every selected face in one spatial pass. Calling CoincidentVertices for each
    // triangle corner makes Select All quadratic on imported meshes with many material seams.
    constexpr double tolerance = 1e-6;
    using Cell = std::array<std::int64_t, 3>;
    const auto cell = [](Vec3 point) -> std::optional<Cell> {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
            std::abs(point.x) > 1e9 || std::abs(point.y) > 1e9 || std::abs(point.z) > 1e9)
            return std::nullopt;
        return Cell{static_cast<std::int64_t>(std::floor(double(point.x) / tolerance)),
                    static_cast<std::int64_t>(std::floor(double(point.y) / tolerance)),
                    static_cast<std::int64_t>(std::floor(double(point.z) / tolerance))};
    };
    std::set<std::array<float, 3>> unique;
    std::map<Cell, std::vector<Vec3>> positions;
    for (const auto id : selected) {
        const auto point = mesh.vertices[id].position;
        if (const auto key = cell(point); key && unique.insert({point.x, point.y, point.z}).second)
            positions[*key].push_back(point);
    }
    for (uint32 id = 0; id < mesh.vertices.size(); ++id) {
        if (selected.contains(id))
            continue;
        const auto point = mesh.vertices[id].position;
        const auto key = cell(point);
        if (!key)
            continue;
        bool found = false;
        for (int x = -1; x <= 1 && !found; ++x)
            for (int y = -1; y <= 1 && !found; ++y)
                for (int z = -1; z <= 1 && !found; ++z) {
                    const auto nearby =
                        positions.find({(*key)[0] + x, (*key)[1] + y, (*key)[2] + z});
                    if (nearby == positions.end())
                        continue;
                    for (const auto candidate : nearby->second) {
                        const double dx = double(point.x) - candidate.x;
                        const double dy = double(point.y) - candidate.y;
                        const double dz = double(point.z) - candidate.z;
                        if (dx * dx + dy * dy + dz * dz <= tolerance * tolerance) {
                            found = true;
                            break;
                        }
                    }
                }
        if (found)
            selected.insert(id);
    }
    return {selected.begin(), selected.end()};
}

Vec3 ModelingPivot(const ModelingMesh& mesh, const ModelingSelection& selection,
                   const Mat4& world) {
    Vec3 sum;
    std::set<std::array<float, 3>> unique;
    for (const auto id : selection.vertices) {
        if (id >= mesh.vertices.size())
            continue;
        const auto point = mesh.vertices[id].position;
        if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
            unique.insert({point.x, point.y, point.z}).second)
            sum += point;
    }
    return unique.empty() ? Point(world, {}) : Point(world, sum * (1.f / unique.size()));
}

bool PreviewModelingTransform(const ModelingMesh& original, ModelingMesh& preview,
                              const ModelingSelection& selection, const Mat4& world,
                              const Mat4& worldDelta, std::string& error) {
    Mat4 inverse;
    if (!Inverse(world, inverse)) {
        error = "오브젝트의 월드 변환을 역변환할 수 없습니다.";
        return false;
    }
    ModelingMesh candidate = original;
    if (!TransformModelingVertices(candidate, selection.vertices, inverse * worldDelta * world,
                                   error))
        return false;
    preview = std::move(candidate);
    return true;
}

void DrawModelingOverlay(LuxUI::Painter& painter, const ModelingMesh& mesh,
                         const ModelingSelection& selection, const Mat4& world,
                         const PerspectiveCamera& camera, LuxUI::SizeF viewport,
                         ModelingEditMode mode) {
    const std::set<uint32> vertices(selection.vertices.begin(), selection.vertices.end());
    const std::set<uint32> faces(selection.faces.begin(), selection.faces.end());
    painter.save();
    painter.setRenderHint(LuxUI::Painter::Antialiasing);
    for (size_t offset = 0; offset + 2 < mesh.indices.size(); offset += 3) {
        const bool active = faces.contains(static_cast<uint32>(offset / 3));
        if (!active && offset >= 300000)
            continue;
        LuxUI::PolygonF polygon;
        bool valid = true;
        for (size_t corner = 0; corner < 3; ++corner) {
            const auto id = mesh.indices[offset + corner];
            const auto point = id < mesh.vertices.size()
                                   ? ProjectViewportPoint(camera, viewport,
                                                          Point(world, mesh.vertices[id].position))
                                   : std::nullopt;
            if (!point) {
                valid = false;
                break;
            }
            polygon << *point;
        }
        if (!valid)
            continue;
        painter.setPen(LuxUI::Pen(
            active ? LuxUI::Color(255, 191, 78) : LuxUI::Color(99, 168, 211, 160), active ? 2 : 1));
        if (active)
            painter.setBrush(LuxUI::Color(255, 180, 54, 65));
        else
            painter.setBrush(LuxUI::Flags::NoBrush);
        painter.drawPolygon(polygon);
    }
    if (mode == ModelingEditMode::Vertex) {
        const size_t step = std::max<size_t>(1, mesh.vertices.size() / 50000);
        for (uint32 id = 0; id < mesh.vertices.size(); ++id) {
            const bool active = vertices.contains(id);
            if (!active && id % step)
                continue;
            if (const auto point = ProjectViewportPoint(camera, viewport,
                                                        Point(world, mesh.vertices[id].position))) {
                painter.setPen(LuxUI::Color(25, 30, 35));
                painter.setBrush(active ? LuxUI::Color(255, 199, 84) : LuxUI::Color(119, 197, 241));
                const double radius = active ? 4.5 : 3;
                painter.drawEllipse(*point, radius, radius);
            }
        }
    }
    painter.restore();
}

bool SceneViewport3D::ModelingActive() const {
    const auto selected = document_.GetSelected();
    return modelingMode_ != ModelingEditMode::Object && modelingEntity_ && selected &&
           selected.GetId() == modelingEntity_ && document_.GetSelection().size() == 1 &&
           document_.GetActiveTabId() == modelingTab_ && !modelingLifetime_.expired() &&
           !modelingLifetime_.owner_before(document_.GetScene().Lifetime()) &&
           !document_.GetScene().Lifetime().owner_before(modelingLifetime_) &&
           !modelingMesh_.vertices.empty() && selected.HasComponent<TransformComponent>();
}

void SceneViewport3D::PublishModelingPreview() {
    if (ModelingActive()) {
        if (!engineRenderer_ && EngineSceneRenderer::Allowed())
            engineRenderer_ = std::make_shared<EngineSceneRenderer>();
        if (engineRenderer_)
            engineRenderer_->SetMeshPreview(modelingEntity_, modelingMesh_.vertices,
                                            modelingMesh_.indices);
    } else if (engineRenderer_) {
        engineRenderer_->ClearMeshPreview();
    }
    update();
}

void SceneViewport3D::SetModelingMode(ModelingEditMode mode) {
    if (modelingMode_ != mode) {
        CancelInteraction();
        modelingMode_ = mode;
        setToolTip({});
        LuxUI::ToolTip::hideText();
        modelingSelection_ = {};
        if (onModelingSelection_)
            onModelingSelection_(modelingSelection_);
    }
    PublishModelingPreview();
}

ModelingEditMode SceneViewport3D::GetModelingMode() const {
    return modelingMode_;
}

void SceneViewport3D::SetModelingMesh(uint32 entity, const ModelingMesh& mesh) {
    ModelingMesh copy = mesh;
    CancelInteraction();
    modelingEntity_ = entity;
    modelingTab_ = document_.GetActiveTabId();
    modelingLifetime_ = document_.GetScene().Lifetime();
    modelingMesh_ = std::move(copy);
    modelingSelection_ = {};
    PublishModelingPreview();
}

void SceneViewport3D::ClearModelingMesh() {
    CancelInteraction();
    modelingEntity_ = 0;
    modelingLifetime_.reset();
    modelingMesh_ = {};
    modelingInitialMesh_ = {};
    modelingSelection_ = {};
    if (engineRenderer_)
        engineRenderer_->ClearMeshPreview();
    update();
}

const ModelingMesh* SceneViewport3D::GetModelingMesh() const {
    return modelingEntity_ ? &modelingMesh_ : nullptr;
}

uint32 SceneViewport3D::GetModelingEntity() const {
    return modelingEntity_;
}

void SceneViewport3D::SetModelingSelection(const ModelingSelection& selection) {
    ModelingSelection copy = selection;
    if (modelingDrag_)
        CancelInteraction();
    std::erase_if(copy.faces, [this](uint32 id) { return id >= modelingMesh_.indices.size() / 3; });
    std::sort(copy.faces.begin(), copy.faces.end());
    copy.faces.erase(std::unique(copy.faces.begin(), copy.faces.end()), copy.faces.end());
    if (modelingMode_ == ModelingEditMode::Face) {
        copy.vertices = ModelingFaceVertices(modelingMesh_, copy.faces);
    } else {
        copy.faces.clear();
        std::erase_if(copy.vertices,
                      [this](uint32 id) { return id >= modelingMesh_.vertices.size(); });
        std::sort(copy.vertices.begin(), copy.vertices.end());
        copy.vertices.erase(std::unique(copy.vertices.begin(), copy.vertices.end()),
                            copy.vertices.end());
    }
    modelingSelection_ = std::move(copy);
    if (onModelingSelection_)
        onModelingSelection_(modelingSelection_);
    update();
}

const ModelingSelection& SceneViewport3D::GetModelingSelection() const {
    return modelingSelection_;
}

void SceneViewport3D::SetModelingCommitCallback(
    std::function<void(const ModelingMesh&, const ModelingSelection&)> callback) {
    onModelingCommit_ = std::move(callback);
}

void SceneViewport3D::SetModelingSelectionCallback(
    std::function<void(const ModelingSelection&)> callback) {
    onModelingSelection_ = std::move(callback);
}

void SceneViewport3D::SelectModeling(LuxUI::PointF point,
                                     LuxUI::Flags::KeyboardModifiers modifiers) {
    if (!ModelingActive())
        return;
    const auto world = document_.GetScene().GetWorldMatrix(modelingEntity_);
    const auto camera = GetViewCamera();
    const bool toggle = modifiers & LuxUI::Flags::ControlModifier;
    ModelingSelection next =
        modifiers & (LuxUI::Flags::ControlModifier | LuxUI::Flags::ShiftModifier)
            ? modelingSelection_
            : ModelingSelection{};
    const auto merge = [toggle](std::vector<uint32>& values, const std::vector<uint32>& patch) {
        const bool remove = toggle && std::any_of(patch.begin(), patch.end(), [&](uint32 id) {
                                return std::find(values.begin(), values.end(), id) != values.end();
                            });
        for (const auto id : patch) {
            if (remove)
                std::erase(values, id);
            else if (std::find(values.begin(), values.end(), id) == values.end())
                values.push_back(id);
        }
    };
    if (modelingMode_ == ModelingEditMode::Vertex) {
        if (const auto vertex = PickModelingVertex(modelingMesh_, world, camera, size(), point))
            merge(next.vertices, CoincidentVertices(modelingMesh_, *vertex));
    } else if (const auto ray = MakeViewportRay3D(camera, size(), point)) {
        if (const auto face = PickModelingFace(modelingMesh_, world, *ray))
            merge(next.faces, CoplanarFaces(modelingMesh_, *face));
    }
    SetModelingSelection(next);
}
} // namespace LuxEditor
