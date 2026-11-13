#include <RainyCreek/CreekGeometry.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace RainyCreek {
namespace {
namespace le = LuxEngine;

constexpr float HalfWidth = 18.0f;
constexpr float HalfLength = 22.0f;
constexpr float StreamHalfWidth = 1.15f;
constexpr float TwoPi = std::numbers::pi_v<float> * 2.0f;

float SmoothStep(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

float PhaseAngle(unsigned phase, unsigned phaseCount) {
    if (phaseCount == 0)
        throw std::invalid_argument("애니메이션 단계 수는 1 이상이어야 합니다.");
    return static_cast<float>(phase % phaseCount) / static_cast<float>(phaseCount) * TwoPi;
}

le::Vec3 Normal(float x, float y, float z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length < 1e-8f)
        return {0.0f, 1.0f, 0.0f};
    return {x / length, y / length, z / length};
}

le::Vec3 FaceNormal(const le::Vec3& a, const le::Vec3& b, const le::Vec3& c) {
    const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    return Normal(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx);
}

void Quad(CpuMesh& mesh, const std::array<le::Vec3, 4>& positions) {
    const auto first = static_cast<le::uint32>(mesh.vertices.size());
    const auto normal = FaceNormal(positions[0], positions[1], positions[2]);
    const std::array<le::Vec2, 4> uvs{{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}};
    for (size_t i = 0; i < positions.size(); ++i)
        mesh.vertices.push_back({positions[i], normal, uvs[i]});
    for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u})
        mesh.indices.push_back(first + index);
}

void Triangle(CpuMesh& mesh, const std::array<le::Vec3, 3>& positions) {
    const auto normal = FaceNormal(positions[0], positions[1], positions[2]);
    for (size_t i = 0; i < positions.size(); ++i) {
        mesh.indices.push_back(static_cast<le::uint32>(mesh.vertices.size()));
        mesh.vertices.push_back(
            {positions[i], normal, {static_cast<float>(i == 1), static_cast<float>(i == 2)}});
    }
}

void Box(CpuMesh& mesh, float halfX, float bottom, float top, float halfZ) {
    Quad(mesh, {{{-halfX, bottom, halfZ},
                 {halfX, bottom, halfZ},
                 {halfX, top, halfZ},
                 {-halfX, top, halfZ}}});
    Quad(mesh, {{{halfX, bottom, -halfZ},
                 {-halfX, bottom, -halfZ},
                 {-halfX, top, -halfZ},
                 {halfX, top, -halfZ}}});
    Quad(mesh, {{{halfX, bottom, halfZ},
                 {halfX, bottom, -halfZ},
                 {halfX, top, -halfZ},
                 {halfX, top, halfZ}}});
    Quad(mesh, {{{-halfX, bottom, -halfZ},
                 {-halfX, bottom, halfZ},
                 {-halfX, top, halfZ},
                 {-halfX, top, -halfZ}}});
    Quad(
        mesh,
        {{{-halfX, top, halfZ}, {halfX, top, halfZ}, {halfX, top, -halfZ}, {-halfX, top, -halfZ}}});
    Quad(mesh, {{{-halfX, bottom, -halfZ},
                 {halfX, bottom, -halfZ},
                 {halfX, bottom, halfZ},
                 {-halfX, bottom, halfZ}}});
}

void Cylinder(CpuMesh& mesh, float radius, float bottom, float top) {
    constexpr unsigned sides = 12;
    for (unsigned i = 0; i < sides; ++i) {
        const float a = TwoPi * static_cast<float>(i) / static_cast<float>(sides);
        const float b = TwoPi * static_cast<float>(i + 1) / static_cast<float>(sides);
        const le::Vec3 p{radius * std::cos(a), bottom, radius * std::sin(a)};
        const le::Vec3 q{radius * std::cos(b), bottom, radius * std::sin(b)};
        Quad(mesh, {{{q.x, bottom, q.z}, {p.x, bottom, p.z}, {p.x, top, p.z}, {q.x, top, q.z}}});
        Triangle(mesh, {{{0.0f, top, 0.0f}, {q.x, top, q.z}, {p.x, top, p.z}}});
        Triangle(mesh, {{{0.0f, bottom, 0.0f}, p, q}});
    }
}

float Random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

float SurfaceHeight(float x, float z) {
    const float ground = TerrainHeight(x, z);
    return std::abs(x - StreamCenter(z)) < StreamHalfWidth ? std::max(ground, WaterHeight() + .012f)
                                                           : ground;
}

} // namespace

float StreamCenter(float z) {
    return 1.65f * std::sin(z * .16f) + .55f * std::sin(z * .41f + .7f);
}

float TerrainHeight(float x, float z) {
    const float bank = std::clamp(.42f + .12f * std::sin(x * .19f + z * .11f) +
                                      .09f * std::cos(z * .23f - x * .15f),
                                  .2f, .7f);
    const float bed = -.28f + .025f * std::sin(z * .31f);
    const float distance = std::abs(x - StreamCenter(z));
    const float shore = SmoothStep((distance - (StreamHalfWidth - .4f)) / .8f);
    return bed + (bank - bed) * shore;
}

float WaterHeight() {
    return .02f;
}

CpuMesh BuildTerrain() {
    CpuMesh mesh;
    constexpr unsigned columns = 72, rows = 88;
    constexpr float step = .5f, tileSize = 2.0f;
    mesh.vertices.reserve(columns * rows * 4);
    mesh.indices.reserve(columns * rows * 6);
    for (unsigned row = 0; row < rows; ++row) {
        for (unsigned column = 0; column < columns; ++column) {
            const float x = -HalfWidth + static_cast<float>(column) * step;
            const float z = -HalfLength + static_cast<float>(row) * step;
            const float u = static_cast<float>(column % 4) * step / tileSize;
            const float v = static_cast<float>(row % 4) * step / tileSize;
            const float uvStep = step / tileSize;
            const std::array<le::Vec2, 4> positions{
                {{x, z}, {x, z + step}, {x + step, z + step}, {x + step, z}}};
            const std::array<le::Vec2, 4> uvs{
                {{u, v}, {u, v + uvStep}, {u + uvStep, v + uvStep}, {u + uvStep, v}}};
            const auto first = static_cast<le::uint32>(mesh.vertices.size());
            for (size_t i = 0; i < positions.size(); ++i) {
                const float px = positions[i].x, pz = positions[i].y;
                constexpr float sample = .025f;
                const float dx = (TerrainHeight(px + sample, pz) - TerrainHeight(px - sample, pz)) /
                                 (2.0f * sample);
                const float dz = (TerrainHeight(px, pz + sample) - TerrainHeight(px, pz - sample)) /
                                 (2.0f * sample);
                mesh.vertices.push_back(
                    {{px, TerrainHeight(px, pz), pz}, Normal(-dx, 1.0f, -dz), uvs[i]});
            }
            for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u})
                mesh.indices.push_back(first + index);
        }
    }
    return mesh;
}

CpuMesh BuildWater(unsigned phase, unsigned phaseCount) {
    const float angle = PhaseAngle(phase, phaseCount);
    CpuMesh mesh;
    constexpr unsigned columns = 8, rows = 176;
    mesh.vertices.reserve((columns + 1) * (rows + 1));
    mesh.indices.reserve(columns * rows * 6);
    // TODO(Renderer3D): A depth-tested transparent pass and reflection/refraction targets are
    // needed for transmissive water. The current public API displays an opaque rippled surface.
    for (unsigned row = 0; row <= rows; ++row) {
        const float z = -HalfLength + static_cast<float>(row) * (HalfLength * 2.0f / rows);
        for (unsigned column = 0; column <= columns; ++column) {
            const float u = static_cast<float>(column) / columns;
            const float x = StreamCenter(z) + (u * 2.0f - 1.0f) * StreamHalfWidth;
            const float waveA = x * 1.3f + z * 2.1f + angle;
            const float waveB = x * 3.4f - z * 1.4f - angle * 2.0f;
            const float y = WaterHeight() + .007f * std::sin(waveA) + .0035f * std::sin(waveB);
            const float dx = .0091f * std::cos(waveA) + .0119f * std::cos(waveB);
            const float dz = .0147f * std::cos(waveA) - .0049f * std::cos(waveB);
            mesh.vertices.push_back(
                {{x, y, z}, Normal(-dx, 1.0f, -dz), {u, static_cast<float>(row) / rows}});
        }
    }
    for (unsigned row = 0; row < rows; ++row) {
        for (unsigned column = 0; column < columns; ++column) {
            const auto first = row * (columns + 1) + column;
            for (const auto index : {first, first + columns + 1, first + columns + 2, first,
                                     first + columns + 2, first + 1})
                mesh.indices.push_back(index);
        }
    }
    return mesh;
}

CpuMesh BuildRain(unsigned phase, unsigned phaseCount) {
    const float angle = PhaseAngle(phase, phaseCount);
    const float cycle = angle / TwoPi;
    CpuMesh mesh;
    constexpr unsigned drops = 350;
    mesh.vertices.reserve(drops * 8);
    mesh.indices.reserve(drops * 12);
    std::uint32_t randomState = 0x71a39c2du;
    // TODO(Renderer3D): Proper transparent rain needs alpha blending with the HDR scene's
    // depth buffer. Thin opaque streaks currently share the PBR scene and its occlusion.
    for (unsigned i = 0; i < drops; ++i) {
        const float x = (Random(randomState) * 2.0f - 1.0f) * (HalfWidth - .1f);
        const float z = (Random(randomState) * 2.0f - 1.0f) * (HalfLength - .1f);
        const float start = Random(randomState);
        const float length = .16f + Random(randomState) * .17f;
        const float width = .0045f + Random(randomState) * .0025f;
        const float y = 8.0f * (1.0f - std::fmod(start + cycle, 1.0f));
        const float bottom = std::max(y - length * .5f, SurfaceHeight(x, z) + .035f);
        const float top = std::min(y + length * .5f, 8.0f);
        if (top <= bottom + .02f)
            continue;
        constexpr float leanX = .035f, leanZ = .012f;
        Quad(mesh, {{{x - width, bottom, z},
                     {x + width, bottom, z},
                     {x + leanX + width, top, z + leanZ},
                     {x + leanX - width, top, z + leanZ}}});
        Quad(mesh, {{{x, bottom, z + width},
                     {x, bottom, z - width},
                     {x + leanX, top, z + leanZ - width},
                     {x + leanX, top, z + leanZ + width}}});
    }
    return mesh;
}

CpuMesh BuildLampPost() {
    CpuMesh mesh;
    Cylinder(mesh, .15f, 0.0f, .08f);
    Cylinder(mesh, .055f, .08f, 2.10f);
    Box(mesh, .18f, 2.05f, 2.10f, .18f);
    Box(mesh, .20f, 2.42f, 2.46f, .20f);
    for (const float x : {-.145f, .145f}) {
        for (const float z : {-.145f, .145f}) {
            CpuMesh pillar;
            Box(pillar, .018f, 2.10f, 2.42f, .018f);
            const auto first = static_cast<le::uint32>(mesh.vertices.size());
            for (auto vertex : pillar.vertices) {
                vertex.position.x += x;
                vertex.position.z += z;
                mesh.vertices.push_back(vertex);
            }
            for (const auto index : pillar.indices)
                mesh.indices.push_back(first + index);
        }
    }
    const le::Vec3 peak{0.0f, 2.61f, 0.0f};
    Triangle(mesh, {{{-.23f, 2.46f, .23f}, {.23f, 2.46f, .23f}, peak}});
    Triangle(mesh, {{{.23f, 2.46f, .23f}, {.23f, 2.46f, -.23f}, peak}});
    Triangle(mesh, {{{.23f, 2.46f, -.23f}, {-.23f, 2.46f, -.23f}, peak}});
    Triangle(mesh, {{{-.23f, 2.46f, -.23f}, {-.23f, 2.46f, .23f}, peak}});
    return mesh;
}

CpuMesh BuildLampGlass() {
    CpuMesh mesh;
    // TODO(Material3D): Connect a self-lit emissive material and bloom when available.
    // This mesh currently uses a bright opaque surface with a separate public PointLight.
    Box(mesh, .125f, 2.10f, 2.40f, .125f);
    return mesh;
}

} // namespace RainyCreek
