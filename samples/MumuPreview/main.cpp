#include <LuxEngine/Application.hpp>
#include <LuxEngine/Assets/AssetManager.hpp>
#include <LuxEngine/Renderer/Renderer3D/Renderer3D.hpp>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace le = LuxEngine;
namespace fs = std::filesystem;

namespace {
constexpr std::array<const char*, 4> PartNames{"body.obj", "face.obj", "foot_l.obj", "foot_r.obj"};
constexpr size_t LodCount = 3;
constexpr float Pi = std::numbers::pi_v<float>;

fs::path FromUtf8(std::string_view text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string ToUtf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

struct Options {
    fs::path assets;
    fs::path capture;
    unsigned frames = 0;
    unsigned lod = 0;
    bool validate = false;
    bool smoke = false;
    bool help = false;
};

unsigned ParseUnsigned(std::string_view value, unsigned maximum) {
    unsigned result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result > maximum)
        throw std::runtime_error("숫자 옵션이 올바르지 않습니다.");
    return result;
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--help")
            options.help = true;
        else if (option == "--validate-assets")
            options.validate = true;
        else if (option == "--smoke-test") {
            options.smoke = true;
            options.frames = 120;
        } else if ((option == "--assets" || option == "--capture" || option == "--frames" ||
                    option == "--lod") &&
                   index + 1 < argc) {
            const std::string_view value(argv[++index]);
            if (option == "--assets")
                options.assets = FromUtf8(value);
            else if (option == "--capture")
                options.capture = FromUtf8(value);
            else if (option == "--lod")
                options.lod = ParseUnsigned(value, static_cast<unsigned>(LodCount - 1));
            else {
                options.frames = ParseUnsigned(value, 100000);
                if (options.frames == 0)
                    throw std::runtime_error("프레임 수는 1 이상이어야 합니다.");
            }
        } else
            throw std::runtime_error("지원하지 않는 옵션입니다. --help를 확인하세요.");
    }
    if (options.assets.empty()) {
        const char* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error("실행 파일 경로를 확인하지 못했습니다.");
        options.assets = FromUtf8(base) / "assets" / "mumu";
    }
    if (!options.capture.empty() && options.capture.extension() != ".ppm" &&
        options.capture.extension() != ".png")
        throw std::runtime_error("캡처 파일은 .png 또는 .ppm 확장자를 사용하세요.");
    if (!options.capture.empty() && options.frames == 0)
        options.frames = 60;
    return options;
}

struct CpuMesh {
    std::vector<le::Vertex3D> vertices;
    std::vector<le::uint32> indices;
};

using CpuLod = std::array<CpuMesh, 4>;

std::array<CpuLod, LodCount> LoadAssets(const fs::path& directory) {
    std::array<CpuLod, LodCount> result;
    for (size_t lod = 0; lod < result.size(); ++lod) {
        size_t vertices = 0;
        size_t triangles = 0;
        for (size_t part = 0; part < PartNames.size(); ++part) {
            const auto path = directory / ("lod" + std::to_string(lod)) / PartNames[part];
            le::RawMeshData raw;
            if (!le::AssetManager::LoadMesh(ToUtf8(path), raw) || raw.vertices.size() % 3 != 0 ||
                raw.normals.size() != raw.vertices.size() ||
                raw.texCoords.size() / 2 != raw.vertices.size() / 3 || raw.indices.size() % 3 != 0)
                throw std::runtime_error("메시를 읽지 못했습니다: " + ToUtf8(path));
            auto& mesh = result[lod][part];
            // Keep the loader's smoothing-group normals so the character is not lit per face.
            for (size_t index = 0; index < raw.vertices.size() / 3; ++index)
                mesh.vertices.push_back({{raw.vertices[index * 3], raw.vertices[index * 3 + 1],
                                          raw.vertices[index * 3 + 2]},
                                         {raw.normals[index * 3], raw.normals[index * 3 + 1],
                                          raw.normals[index * 3 + 2]},
                                         {raw.texCoords[index * 2], raw.texCoords[index * 2 + 1]}});
            mesh.indices = std::move(raw.indices);
            for (size_t index = 0; index < mesh.indices.size(); index += 3) {
                for (size_t corner = 0; corner < 3; ++corner)
                    if (mesh.indices[index + corner] >= mesh.vertices.size())
                        throw std::runtime_error("메시 인덱스가 범위를 벗어납니다.");
                const auto a = mesh.vertices[mesh.indices[index]].position;
                const auto b = mesh.vertices[mesh.indices[index + 1]].position;
                const auto c = mesh.vertices[mesh.indices[index + 2]].position;
                const auto area = (b - a).Cross(c - a);
                if (area.Dot(area) < 1e-16f)
                    throw std::runtime_error("면적이 0인 삼각형이 있습니다: " + ToUtf8(path));
            }
            vertices += mesh.vertices.size();
            triangles += mesh.indices.size() / 3;
        }
        std::cout << "LOD" << lod << ": " << vertices << " vertices, " << triangles
                  << " triangles, 4 parts\n";
    }
    le::AssetManager::Clear();
    return result;
}

void AppendBigEndian(std::vector<le::uint8>& bytes, le::uint32 value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<le::uint8>(value >> shift));
}

void AppendPngChunk(std::vector<le::uint8>& png, std::string_view type,
                    std::span<const le::uint8> data) {
    AppendBigEndian(png, static_cast<le::uint32>(data.size()));
    const auto first = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), data.begin(), data.end());
    le::uint32 crc = 0xffffffffu;
    for (size_t index = first; index < png.size(); ++index) {
        crc ^= png[index];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    AppendBigEndian(png, crc ^ 0xffffffffu);
}

std::vector<le::uint8> EncodePng(const le::GpuFrameCapture& image) {
    std::vector<le::uint8> raw;
    const size_t rowBytes = static_cast<size_t>(image.width) * 4;
    raw.reserve((rowBytes + 1) * image.height);
    for (size_t row = 0; row < image.height; ++row) {
        raw.push_back(0);
        raw.insert(raw.end(), image.rgba.begin() + row * rowBytes,
                   image.rgba.begin() + (row + 1) * rowBytes);
    }
    std::vector<le::uint8> compressed{0x78, 0x01};
    for (size_t first = 0; first < raw.size();) {
        const auto length = static_cast<le::uint32>(std::min(size_t{65535}, raw.size() - first));
        compressed.push_back(first + length == raw.size() ? 1 : 0);
        compressed.push_back(static_cast<le::uint8>(length));
        compressed.push_back(static_cast<le::uint8>(length >> 8));
        compressed.push_back(static_cast<le::uint8>(~length));
        compressed.push_back(static_cast<le::uint8>((~length) >> 8));
        compressed.insert(compressed.end(), raw.begin() + first, raw.begin() + first + length);
        first += length;
    }
    le::uint32 a = 1, b = 0;
    for (const auto value : raw) {
        a = (a + value) % 65521;
        b = (b + a) % 65521;
    }
    AppendBigEndian(compressed, (b << 16) | a);
    std::vector<le::uint8> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<le::uint8> header;
    AppendBigEndian(header, image.width);
    AppendBigEndian(header, image.height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    AppendPngChunk(png, "IHDR", header);
    AppendPngChunk(png, "IDAT", compressed);
    AppendPngChunk(png, "IEND", {});
    return png;
}

void WriteCapture(le::GpuDevice& gpu, const fs::path& path) {
    le::GpuFrameCapture capture;
    if (!gpu.ReadFrameCapture(capture) || capture.width == 0 || capture.height == 0 ||
        capture.rgba.size() != static_cast<size_t>(capture.width) * capture.height * 4)
        throw std::runtime_error("GPU 화면 캡처를 읽지 못했습니다.");
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (path.extension() == ".png") {
        const auto png = EncodePng(capture);
        output.write(reinterpret_cast<const char*>(png.data()),
                     static_cast<std::streamsize>(png.size()));
    } else {
        output << "P6\n" << capture.width << ' ' << capture.height << "\n255\n";
        for (size_t index = 0; index < capture.rgba.size(); index += 4)
            output.write(reinterpret_cast<const char*>(capture.rgba.data() + index), 3);
    }
    output.close();
    if (!output)
        throw std::runtime_error("캡처 파일을 저장하지 못했습니다.");
    std::cout << "캡처 저장: " << ToUtf8(path) << '\n';
}

class MumuPreview final : public le::Application {
public:
    explicit MumuPreview(Options options, std::array<CpuLod, LodCount> meshes)
        : Application({"Mumu | 1 Idle  2 Walk  3 Hop | A/D Orbit  W/S Zoom | Tab LOD | Space Pause",
                       1100, 850}),
          options_(std::move(options)), cpuMeshes_(std::move(meshes)) {}

    ~MumuPreview() override {
        Shutdown();
    }

    int RunPreview() {
        unsigned skipped = 0;
        for (;;) {
            const auto result = RunFrame();
            if (result == FrameResult::Error)
                return 1;
            if (result == FrameResult::Stopped)
                return options_.capture.empty() ? 0 : 1;
            if (result == FrameResult::Skipped) {
                if (options_.frames && ++skipped > 10000)
                    throw std::runtime_error("GPU 프레임 준비 시간을 초과했습니다.");
                SDL_Delay(1);
                continue;
            }
            skipped = 0;
            if (options_.frames && rendered_ >= options_.frames) {
                if (!options_.capture.empty()) {
                    WriteCapture(GetGpuDevice(), options_.capture);
                    options_.capture.clear();
                }
                const auto counters = GetGpuDevice().GetCounters();
                std::cout << "GPU 검증 오류: " << counters.validationErrors
                          << ", 제출 프레임: " << counters.framesSubmitted << '\n';
                return counters.validationErrors == 0 ? 0 : 1;
            }
        }
    }

private:
    Options options_;
    std::array<CpuLod, LodCount> cpuMeshes_;
    std::array<std::array<le::Mesh3D, 4>, LodCount> meshes_;
    le::Mesh3D ground_;
    le::Renderer3D::Context context_;
    unsigned rendered_ = 0;
    unsigned mode_ = 0;
    float time_ = 0;
    float yaw_ = .36f;
    float distance_ = 5.6f;
    bool paused_ = false;

    void OnInit() override {
        for (size_t lod = 0; lod < meshes_.size(); ++lod)
            for (size_t part = 0; part < PartNames.size(); ++part)
                if (!meshes_[lod][part].Upload(&GetGpuDevice(), cpuMeshes_[lod][part].vertices,
                                               cpuMeshes_[lod][part].indices))
                    throw std::runtime_error("캐릭터 GPU 버퍼를 만들지 못했습니다.");
        cpuMeshes_ = {};
        const std::array<le::Vec3, 4> positions{
            {{-200, -.008f, -200}, {-200, -.008f, 200}, {200, -.008f, 200}, {200, -.008f, -200}}};
        const std::array<le::uint32, 6> indices{0, 1, 2, 0, 2, 3};
        if (!ground_.Upload(&GetGpuDevice(), positions, indices))
            throw std::runtime_error("바닥 GPU 버퍼를 만들지 못했습니다.");
        context_.Init(&GetGpuDevice());
        le::Input::BindAction("quit", le::LuxKey::Escape);
        le::Input::BindAction("idle", le::LuxKey::Num1);
        le::Input::BindAction("walk", le::LuxKey::Num2);
        le::Input::BindAction("hop", le::LuxKey::Num3);
        le::Input::BindAction("pause", le::LuxKey::Space);
        le::Input::BindAction("lod", le::LuxKey::Tab);
        le::Input::BindAction("reset", le::LuxKey::R);
        std::cout << "1 대기 / 2 걷기 / 3 점프, A/D 회전, W/S 확대·축소, Tab LOD, Space 일시정지, "
                     "R 초기화, Esc 종료\n";
    }

    void OnUpdate(double dt) override {
        if (le::Input::WasActionPressed("quit"))
            Close();
        if (le::Input::WasActionPressed("pause"))
            paused_ = !paused_;
        if (le::Input::WasActionPressed("lod"))
            options_.lod = (options_.lod + 1) % static_cast<unsigned>(LodCount);
        for (const auto& [action, mode] : std::array<std::pair<const char*, unsigned>, 3>{
                 {{"idle", 0}, {"walk", 1}, {"hop", 2}}})
            if (le::Input::WasActionPressed(action)) {
                mode_ = mode;
                time_ = 0;
            }
        if (le::Input::WasActionPressed("reset")) {
            yaw_ = .36f;
            distance_ = 5.6f;
            time_ = 0;
        }
        yaw_ += (float(le::Input::IsKeyPressed(le::LuxKey::D)) -
                 float(le::Input::IsKeyPressed(le::LuxKey::A))) *
                static_cast<float>(dt);
        distance_ = std::clamp(distance_ + (float(le::Input::IsKeyPressed(le::LuxKey::S)) -
                                            float(le::Input::IsKeyPressed(le::LuxKey::W))) *
                                               static_cast<float>(dt) * 2,
                               3.0f, 10.0f);
        if (!paused_ && !options_.smoke)
            time_ += static_cast<float>(dt);
    }

    void OnRender(float) override {
        if (options_.smoke) {
            mode_ = std::min(2u, rendered_ / 40);
            options_.lod = (rendered_ / 10) % static_cast<unsigned>(LodCount);
            time_ = static_cast<float>(rendered_ % 40) / 30;
        }
        if (!options_.capture.empty() && rendered_ + 1 == options_.frames &&
            !GetGpuDevice().RequestFrameCapture())
            throw std::runtime_error("프레임 캡처를 요청하지 못했습니다.");
        GetGpuDevice().Clear(.72f, .79f, .83f, 1);
        le::PerspectiveCamera camera(35 * Pi / 180,
                                     float(GetWindow().GetWidth()) /
                                         float(std::max(1u, GetWindow().GetHeight())),
                                     .05f, 500);
        camera.SetPosition({std::sin(yaw_) * distance_, 2.05f, std::cos(yaw_) * distance_});
        camera.SetTarget({0, .86f, 0});
        le::DirectionalLight sun;
        sun.direction = le::Vec3{-.6f, -1.0f, -.8f}.Normalized();
        sun.color = {1.0f, .97f, .91f};
        sun.intensity = .95f;
        const std::array<le::PointLight, 1> fill{{{{2, 2.8f, 3}, {.72f, .84f, 1}, .7f, 7}}};
        le::Renderer3D::ScopedContext scope(context_);
        le::Renderer3D::BeginScene(camera, sun, fill);
        le::Material3D groundMaterial;
        groundMaterial.albedoColor = {.62f, .70f, .74f, 1};
        le::Renderer3D::DrawMesh(ground_, le::Mat4::Identity(), groundMaterial);
        float lift = 0, bob = .012f * std::sin(time_ * Pi * 1.5f), roll = 0;
        float stretch = 1 + .012f * std::sin(time_ * Pi * 1.5f);
        if (mode_ == 1) {
            bob = .035f * std::abs(std::sin(time_ * Pi * 3));
            roll = .035f * std::sin(time_ * Pi * 3);
            stretch = 1;
        } else if (mode_ == 2) {
            const float phase = std::fmod(time_, 1.2f) / 1.2f;
            const float airborne = std::clamp((phase - .14f) / .70f, 0.0f, 1.0f);
            lift = .72f * std::sin(airborne * Pi);
            stretch = phase < .14f   ? 1 - .12f * std::sin(phase / .14f * Pi)
                      : phase > .84f ? 1 - .10f * std::sin((phase - .84f) / .16f * Pi)
                                     : 1.02f;
            bob = .80f * (stretch - 1);
        }
        const auto root = le::Mat4::Translate({0, lift, 0});
        const auto body =
            root * le::Mat4::Translate({0, .95f + bob, 0}) * le::Mat4::RotateZ(roll) *
            le::Mat4::Scale({1 / std::sqrt(stretch), stretch, 1 / std::sqrt(stretch)}) *
            le::Mat4::Translate({0, -.95f, 0});
        le::Material3D cream;
        cream.albedoColor = {.82f, .81f, .76f, 1};
        le::Material3D face;
        face.albedoColor = {.008f, .009f, .012f, 1};
        const auto& lod = meshes_[options_.lod];
        le::Renderer3D::DrawMesh(lod[0], body, cream);
        le::Renderer3D::DrawMesh(lod[1], body, face);
        for (unsigned foot = 0; foot < 2; ++foot) {
            const float swing = mode_ == 1 ? std::sin(time_ * Pi * 3 + foot * Pi) : 0;
            const float raised = std::max(0.0f, swing);
            const auto transform = root *
                                   le::Mat4::Translate({foot == 0 ? -.53f : .53f,
                                                        .10f + raised * .13f, swing * .09f}) *
                                   le::Mat4::RotateX(raised * .35f);
            le::Renderer3D::DrawMesh(lod[foot + 2], transform, cream);
        }
        le::Renderer3D::EndScene();
        const auto stats = context_.GetStats();
        if (stats.meshCount != 5 || stats.drawCalls != 5)
            throw std::runtime_error("일부 메시 렌더링이 누락되었습니다.");
        ++rendered_;
    }

    void OnShutdown() override {
        context_.Shutdown();
        ground_.Release();
        for (auto& lod : meshes_)
            for (auto& mesh : lod)
                mesh.Release();
        le::Input::ClearBindings();
    }
};

int RunMumu(int argc, char** argv) {
    auto options = ParseOptions(argc, argv);
    if (options.help) {
        std::cout << "Mumu LuxEngine 3D 미리보기\n"
                     "--validate-assets  GPU 없이 LOD0/LOD1/LOD2 OBJ 검증\n"
                     "--smoke-test       대기/걷기/점프와 세 LOD를 120프레임 검증\n"
                     "--capture out.png  마지막 프레임 캡처 (기본 60프레임 후 종료, PPM도 지원)\n"
                     "--frames N         지정 프레임 후 종료\n"
                     "--lod 0|1|2        시작 LOD\n"
                     "--assets PATH      lod0/lod1/lod2가 있는 디렉터리\n";
        return 0;
    }
    auto meshes = LoadAssets(options.assets);
    if (options.validate)
        return 0;
    MumuPreview app(std::move(options), std::move(meshes));
    return app.Initialize() ? app.RunPreview() : 1;
}
} // namespace

int main(int argc, char** argv) {
    try {
        return RunMumu(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Mumu: " << error.what() << '\n';
        return 1;
    }
}
