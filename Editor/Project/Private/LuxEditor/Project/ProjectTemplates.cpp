#include <LuxEditor/Project/ProjectTemplates.hpp>

#include <fstream>

namespace LuxEditor {
namespace {
constexpr const char* GameCMake = R"LUX(cmake_minimum_required(VERSION 3.24)
project(LuxGame LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(LuxEngine CONFIG REQUIRED)
include(${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.cmake OPTIONAL)
add_executable(LuxGame Source/Main.cpp)
find_program(LUX_GAME_DOTNET dotnet HINTS "$ENV{DOTNET_ROOT}" REQUIRED)
set(_lux_managed_output "${CMAKE_CURRENT_SOURCE_DIR}/.lux/managed")
add_custom_target(LuxGameScripts
    COMMAND "${LUX_GAME_DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/GameScripts.csproj"
        --configuration Release --output "${_lux_managed_output}"
        --artifacts-path "${CMAKE_CURRENT_BINARY_DIR}/managed-artifacts"
        "-p:LuxEngineScriptCore=${LuxEngine_MANAGED_DIR}/LuxEngine.ScriptCore.dll"
        "-p:RestoreConfigFile=${CMAKE_CURRENT_SOURCE_DIR}/Scripts/NuGet.Config"
        -p:NuGetAudit=false
    VERBATIM)
add_dependencies(LuxGame LuxGameScripts)
target_link_libraries(LuxGame PRIVATE LuxEngine::Engine LuxEngine::Runtime)
target_compile_definitions(LuxGame PRIVATE LUX_GAME_TEMPLATE_KIND=@KIND@)
if(MSVC)
    # Sources are UTF-8 (Korean text); MSVC would otherwise read them in the system code page.
    target_compile_options(LuxGame PRIVATE /utf-8)
endif()
if(COMMAND lux_copy_runtime)
    lux_copy_runtime(LuxGame)
elseif(COMMAND lux_copy_shaders)
    lux_copy_shaders(LuxGame)
endif()
set(_lux_manifest "${CMAKE_CURRENT_SOURCE_DIR}/project.luxproject")
if(NOT EXISTS "${_lux_manifest}")
    set(_lux_manifest "${CMAKE_CURRENT_SOURCE_DIR}/project.luxproj")
endif()
add_custom_target(LuxGameContent
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LuxGame>/Content"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_lux_manifest}"
        "$<TARGET_FILE_DIR:LuxGame>/Content/project.luxproject"
    VERBATIM)
add_dependencies(LuxGameContent LuxGameScripts)
add_custom_command(TARGET LuxGameContent POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_lux_managed_output}"
        "$<TARGET_FILE_DIR:LuxGame>/Content/Scripts")
add_dependencies(LuxGame LuxGameContent)
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
    add_custom_command(TARGET LuxGameContent POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
            "$<TARGET_FILE_DIR:LuxGame>/Content/Assets"
        VERBATIM)
endif()
file(GLOB_RECURSE _lux_scenes CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.lux"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.[lL][uU][xX][pP][rR][eE][fF][aA][bB]")
foreach(_scene IN LISTS _lux_scenes)
    if(_scene MATCHES "(^|/)(\\.git|\\.lux)(/|$)" OR
       _scene MATCHES "^(build|out|cmake-build[^/]*)(/|$)")
        continue()
    endif()
    get_filename_component(_scene_directory "${_scene}" DIRECTORY)
    add_custom_command(TARGET LuxGameContent POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:LuxGame>/Content/${_scene_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${_scene}"
            "$<TARGET_FILE_DIR:LuxGame>/Content/${_scene}"
        VERBATIM)
endforeach()
if(WIN32)
    get_target_property(_engine_type LuxEngine::Engine TYPE)
    if(_engine_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET LuxGame POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:LuxEngine::Engine> $<TARGET_FILE_DIR:LuxGame>)
    endif()
    if(TARGET SDL3::SDL3-shared)
        add_custom_command(TARGET LuxGame POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:LuxGame>)
    endif()
    if(TARGET box2d::box2d)
        get_target_property(_box2d_type box2d::box2d TYPE)
        if(_box2d_type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET LuxGame POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:box2d::box2d> $<TARGET_FILE_DIR:LuxGame>)
        endif()
    endif()
endif()
)LUX";

constexpr const char* GameSource = R"LUX(#include <LuxEngine/LuxEngine.hpp>
#include <SDL3/SDL_filesystem.h>
#include <LuxEngine/EntryPoint.hpp>
#include <LuxEngine/Scene/SceneManager.hpp>
#include <LuxEngine/Runtime/ProjectManifest.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace le = LuxEngine;

std::filesystem::path FindStartScene(const std::filesystem::path& root) {
    std::filesystem::path target;
    std::string error;
    if (!le::ReadProjectStartScene(root, target, error))
        throw std::runtime_error(error);
    return target;
}

class Game final : public le::Application {
public:
    explicit Game(bool smokeTest)
        : le::Application({.name = "LuxGame", .width = 1280, .height = 720}),
          camera_(-8.0f, 8.0f, -4.5f, 4.5f), smokeTest_(smokeTest) {}

protected:
    void OnInit() override {
        le::Renderer2D::Init(&GetGpuDevice());
        le::Renderer3D::Init(&GetGpuDevice());
        le::PhysicsEngine::Init();
        const char* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error("게임 실행 경로를 찾을 수 없습니다.");
        const auto basePath = std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(base), std::strlen(base)));
        const auto sceneFile = FindStartScene(basePath / "Content");
        std::string error;
        scene_ = manager_.Load(sceneFile, error);
        if (!scene_)
            throw std::runtime_error(error.empty() ? "시작 씬을 열 수 없습니다." : error);
        if (!scene_->GetComponentEntityIds<le::ScriptComponent>().empty()) {
            le::ScriptRuntimeConfig config;
            config.assemblyPath = basePath / "Content/Scripts/GameScripts.dll";
            if (!scripts_.Initialize(*scene_, config))
                throw std::runtime_error(scripts_.GetLastError());
        }
        camera3D_.SetPerspective(le::ToRadians(60), 16.0f / 9, .1f, 1000);
        camera3D_.SetPosition({0, 6, 12});
        camera3D_.SetTarget({0, 0, 0});
        le::Input::BindAction("Left", le::LuxKey::A);
        le::Input::BindAction("Left", le::LuxKey::Left);
        le::Input::BindAction("Right", le::LuxKey::D);
        le::Input::BindAction("Right", le::LuxKey::Right);
        le::Input::BindAction("Up", le::LuxKey::W);
        le::Input::BindAction("Up", le::LuxKey::Up);
        le::Input::BindAction("Down", le::LuxKey::S);
        le::Input::BindAction("Down", le::LuxKey::Down);
        le::Input::BindAction("Action", le::LuxKey::Space);
        le::Input::BindAction("Quit", le::LuxKey::Escape);
        FindActors();
    }

    void FindActors() {
        player_ = {};
        npc_ = {};
        goal_ = {};
        for (auto entity : scene_->GetEntities()) {
            if (!entity.HasComponent<le::TransformComponent>())
                continue;
            const auto* tagComponent = entity.TryGetComponent<le::TagComponent>();
            if (!tagComponent)
                continue;
            const auto& tag = tagComponent->tag;
            if (tag == "플레이어" || tag == "주인공" || tag == "블록")
                player_ = entity;
            if (tag == "NPC")
                npc_ = entity;
            if (tag == "목표")
                goal_ = entity;
        }
    }

    void TickStreaming(double dt) {
        streamingElapsed_ += dt;
        if (streamingElapsed_ < .1)
            return;
        streamingElapsed_ = 0;
        const auto observer =
            player_ ? scene_->GetWorldTransform(player_.GetId()).position : le::Vec3{};
        std::string error;
        bool changed = false;
        if (!manager_.UpdateStreaming(*scene_, {observer.x, observer.y}, error, &changed)) {
            if (error != streamingError_)
                le::Log::Error(error);
            streamingError_ = std::move(error);
        } else if (changed) {
            streamingError_.clear();
            FindActors();
        }
    }

    void OnUpdate(double dt) override {
        if (le::Input::WasActionPressed("Quit"))
            Close();
        if (player_ && !player_.HasComponent<le::ScriptComponent>()) {
            const float horizontal = (le::Input::IsActionDown("Right") ? 1.0f : 0.0f) -
                                     (le::Input::IsActionDown("Left") ? 1.0f : 0.0f);
            const float vertical = (le::Input::IsActionDown("Up") ? 1.0f : 0.0f) -
                                   (le::Input::IsActionDown("Down") ? 1.0f : 0.0f);
            auto& transform = player_.GetComponent<le::TransformComponent>();
            if constexpr (LUX_GAME_TEMPLATE_KIND == 1) {
                if (auto* body = player_.TryGetComponent<le::Rigidbody2DComponent>()) {
                    body->velocity.x = horizontal * 4.0f;
                    if (body->isGrounded && le::Input::WasActionPressed("Action"))
                        body->velocity.y = 7.0f;
                }
            } else if constexpr (LUX_GAME_TEMPLATE_KIND == 4) {
                if (le::Input::WasActionPressed("Left"))
                    transform.position.x -= 1.0f;
                if (le::Input::WasActionPressed("Right"))
                    transform.position.x += 1.0f;
                if (le::Input::WasActionPressed("Up"))
                    transform.position.y += 1.0f;
                if (le::Input::WasActionPressed("Down"))
                    transform.position.y -= 1.0f;
                transform.position.x = std::clamp(transform.position.x, -3.0f, 3.0f);
                transform.position.y = std::clamp(transform.position.y, -3.0f, 3.0f);
                if (goal_) {
                    const auto& target = goal_.GetComponent<le::TransformComponent>().position;
                    if (transform.position.x == target.x && transform.position.y == target.y) {
                        if (auto* sprite = player_.TryGetComponent<le::SpriteRendererComponent>())
                            sprite->color = {0.42f, 0.95f, 0.58f, 1.0f};
                    }
                }
            } else {
                const float length = std::sqrt(horizontal * horizontal + vertical * vertical);
                if (length > 0.0f) {
                    transform.position.x += horizontal / length * 4.0f * static_cast<float>(dt);
                    transform.position.y += vertical / length * 4.0f * static_cast<float>(dt);
                }
            }
            if constexpr (LUX_GAME_TEMPLATE_KIND == 3) {
                if (npc_ && le::Input::WasActionPressed("Action")) {
                    const auto& target = npc_.GetComponent<le::TransformComponent>().position;
                    if (std::abs(target.x - transform.position.x) < 1.5f &&
                        std::abs(target.y - transform.position.y) < 1.5f) {
                        if (auto* sprite = npc_.TryGetComponent<le::SpriteRendererComponent>())
                            sprite->color = sprite->color.x > 0.5f
                                                ? le::Vec4{0.34f, 0.78f, 0.54f, 1.0f}
                                                : le::Vec4{0.82f, 0.52f, 0.38f, 1.0f};
                    }
                }
            }
        }
        TickStreaming(dt);
        if (scripts_.IsRunning() && !scripts_.Update(dt))
            throw std::runtime_error(scripts_.GetLastError());
        scene_->OnUpdate(dt);
        if (smokeTest_ && ++frames_ >= 10) {
            if (GetGpuDevice().GetCounters().validationErrors != 0)
                throw std::runtime_error("Vulkan 검증에서 렌더링 오류가 발견되었습니다.");
            Close();
        }
    }

    void OnRender(float) override {
        const auto& settings = scene_->GetSettings();
        GetGpuDevice().Clear(settings.clearColor.x, settings.clearColor.y, settings.clearColor.z,
                             settings.clearColor.w);
        auto renderSettings = scene_->GetSettings().render3D;
        if (!renderSettings.environmentPath.empty())
            renderSettings.environmentPath =
                scene_->ResolveAssetPath(renderSettings.environmentPath);
        if (scene_->GetSettings().render3D.mode == le::Render3DMode::Pbr)
            le::Renderer3D::BeginScene(camera3D_, renderSettings, scene_->GetSettings().exposure);
        else
            le::Renderer3D::BeginScene(camera3D_, le::DirectionalLight{});
        scene_->OnRender3D();
        le::Renderer3D::EndScene();
        le::Renderer2D::BeginScene(camera_);
        // Scene::OnRender draws tilemaps and sprites, so maps painted in the editor appear in game.
        scene_->OnRender();
        le::Renderer2D::EndScene();
    }

    void OnShutdown() override {
        scripts_.Shutdown();
        le::Input::ClearBindings();
        manager_.ClearCache();
        le::PhysicsEngine::Shutdown();
        le::Renderer3D::Shutdown();
        le::Renderer2D::Shutdown();
    }

private:
    le::SceneManager manager_;
    le::ScriptRuntime scripts_;
    std::shared_ptr<le::Scene> scene_;
    le::Entity player_;
    le::Entity npc_;
    le::Entity goal_;
    le::OrthographicCamera camera_;
    le::PerspectiveCamera camera3D_;
    double streamingElapsed_ = 0;
    std::string streamingError_;
    bool smokeTest_ = false;
    int frames_ = 0;
};

int RunGame(int argc, char* argv[]) {
    Game game(argc > 1 && std::string_view(argv[1]) == "--smoke-test");
    if (!game.Initialize())
        return 1;
    const int result = game.Run();
    game.Shutdown();
    return result;
}
LUX_ENGINE_MAIN(RunGame)
)LUX";
constexpr const char* ScriptProject = R"LUX(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
  </PropertyGroup>
  <ItemGroup>
    <Reference Include="LuxEngine.ScriptCore">
      <HintPath>$(LuxEngineScriptCore)</HintPath>
      <Private>false</Private>
    </Reference>
  </ItemGroup>
</Project>
)LUX";

constexpr const char* ScriptSource = R"LUX(using LuxEngine;

namespace Game {
    public sealed class Mover : EntityScript {
        public float Speed = 4.0f;

        protected override void OnUpdate(double deltaTime) {
            Transform transform = Entity.Transform;
            float horizontal = (Input.IsKeyPressed(LuxKey.D) ? 1.0f : 0.0f)
                - (Input.IsKeyPressed(LuxKey.A) ? 1.0f : 0.0f);
            transform.Position.X += horizontal * Speed * (float)deltaTime;
            Entity.Transform = transform;
        }
    }
}
)LUX";

constexpr const char* ScriptFormatting = R"LUX(root = true

[*.cs]
indent_style = space
indent_size = 4
end_of_line = lf
charset = utf-8
insert_final_newline = true
csharp_new_line_before_open_brace = none
csharp_new_line_before_else = false
csharp_new_line_before_catch = false
csharp_new_line_before_finally = false
csharp_style_namespace_declarations = block_scoped:warning
csharp_style_expression_bodied_methods = false:warning
csharp_preserve_single_line_blocks = false
csharp_preserve_single_line_statements = false
)LUX";
} // namespace

bool CreateTemplateFiles(const std::filesystem::path& directory, ProjectTemplate projectTemplate,
                         std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(directory / "Scripts", ec);
    if (ec) {
        error = "프로젝트 소스 폴더를 만들 수 없습니다.";
        return false;
    }
    std::filesystem::create_directories(directory / "Source", ec);
    if (ec) {
        error = "프로젝트 소스 폴더를 만들 수 없습니다.";
        return false;
    }
    const std::pair<const char*, const char*> scriptFiles[] = {
        {"Scripts/GameScripts.csproj", ScriptProject},
        {"Scripts/Mover.cs", ScriptSource},
        {".editorconfig", ScriptFormatting},
        {"Scripts/NuGet.Config",
         "<configuration><packageSources><clear /></packageSources></configuration>\n"}};
    for (const auto& [path, content] : scriptFiles) {
        std::ofstream file(directory / path, std::ios::binary);
        file << content;
        file.close();
        if (!file) {
            error = "C# 프로젝트 파일을 저장할 수 없습니다.";
            return false;
        }
    }
    std::string cmake = GameCMake;
    const auto marker = cmake.find("@KIND@");
    cmake.replace(marker, 6, std::to_string(static_cast<int>(projectTemplate)));
    std::ofstream buildFile(directory / "CMakeLists.txt", std::ios::binary);
    std::ofstream sourceFile(directory / "Source" / "Main.cpp", std::ios::binary);
    buildFile << cmake;
    sourceFile << GameSource;
    buildFile.close();
    sourceFile.close();
    if (!buildFile || !sourceFile) {
        error = "프로젝트 시작 코드를 저장할 수 없습니다.";
        return false;
    }
    return true;
}

} // namespace LuxEditor
