#include "ProjectTemplates.hpp"

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
target_link_libraries(LuxGame PRIVATE LuxEngine::Engine)
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
add_custom_command(TARGET LuxGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Scenes" "$<TARGET_FILE_DIR:LuxGame>/Scenes")
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

constexpr const char* GameSource = R"LUX(#include "Engine/LuxEngine.hpp"
#include <SDL3/SDL_filesystem.h>
#include "Engine/EntryPoint.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace le = LuxEngine;

class Game final : public le::Application {
public:
    explicit Game(bool smokeTest)
        : le::Application({.name = "LuxGame", .width = 1280, .height = 720}),
          camera_(-8.0f, 8.0f, -4.5f, 4.5f), smokeTest_(smokeTest) {}

protected:
    void OnInit() override {
        le::Renderer2D::Init(&GetGpuDevice());
        le::PhysicsEngine::Init();
        const char* base = SDL_GetBasePath();
        const auto basePath = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(base), std::strlen(base)));
        const auto sceneFile = basePath / "Scenes" / "Main.lux";
        const auto bytes = sceneFile.u8string();
        const std::string scenePath(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!scene_.Deserialize(scenePath))
            throw std::runtime_error("시작 장면을 열 수 없습니다.");
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
        for (auto entity : scene_.GetEntities()) {
            const auto& tag = entity.GetComponent<le::TagComponent>().tag;
            if (tag == "플레이어" || tag == "주인공" || tag == "블록")
                player_ = entity;
            if (tag == "NPC")
                npc_ = entity;
            if (tag == "목표")
                goal_ = entity;
        }
    }

    void OnUpdate(double dt) override {
        if (le::Input::WasActionPressed("Quit"))
            Close();
        if (player_) {
            const float horizontal = (le::Input::IsActionDown("Right") ? 1.0f : 0.0f) -
                                     (le::Input::IsActionDown("Left") ? 1.0f : 0.0f);
            const float vertical = (le::Input::IsActionDown("Up") ? 1.0f : 0.0f) -
                                   (le::Input::IsActionDown("Down") ? 1.0f : 0.0f);
            auto& transform = player_.GetComponent<le::TransformComponent>();
            if constexpr (LUX_GAME_TEMPLATE_KIND == 1) {
                auto& body = player_.GetComponent<le::Rigidbody2DComponent>();
                body.velocity.x = horizontal * 4.0f;
                if (body.isGrounded && le::Input::WasActionPressed("Action"))
                    body.velocity.y = 7.0f;
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
                        player_.GetComponent<le::SpriteRendererComponent>().color = {0.42f, 0.95f,
                                                                                     0.58f, 1.0f};
                        if (!puzzleSolved_)
                            le::Log::Info("퍼즐 목표에 도착했습니다.");
                        puzzleSolved_ = true;
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
                        auto& color = npc_.GetComponent<le::SpriteRendererComponent>().color;
                        color = color.x > 0.5f ? le::Vec4{0.34f, 0.78f, 0.54f, 1.0f}
                                               : le::Vec4{0.82f, 0.52f, 0.38f, 1.0f};
                    }
                }
            }
        }
        scene_.OnUpdate(dt);
        if (smokeTest_ && ++frames_ >= 10)
            Close();
    }

    void OnRender(float) override {
        le::Renderer2D::BeginScene(camera_);
        // Scene::OnRender draws tilemaps and sprites, so maps painted in the editor appear in game.
        scene_.OnRender();
        le::Renderer2D::EndScene();
    }

    void OnShutdown() override {
        le::Input::ClearBindings();
        le::PhysicsEngine::Shutdown();
        le::Renderer2D::Shutdown();
    }

private:
    le::Scene scene_;
    le::Entity player_;
    le::Entity npc_;
    le::Entity goal_;
    le::OrthographicCamera camera_;
    bool smokeTest_ = false;
    bool puzzleSolved_ = false;
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
} // namespace

bool CreateTemplateFiles(const std::filesystem::path& directory, ProjectTemplate projectTemplate,
                         std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(directory / "Source", ec);
    if (ec) {
        error = "프로젝트 소스 폴더를 만들 수 없습니다.";
        return false;
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
