#include "Game/SurvivalGame.hpp"
#include "Presentation/TextLayout.hpp"
#include <LuxEngine/EntryPoint.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace ForestGame;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

float Length(Vec2 value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

Vec2 BotMovement(const SurvivalGame& game) {
    Vec2 target = game.GetPlayer().position;
    float closest = std::numeric_limits<float>::max();
    for (const auto& gem : game.GetGems()) {
        const float distance = Length(gem.position - game.GetPlayer().position);
        if (distance < closest) {
            closest = distance;
            target = gem.position;
        }
    }
    if (game.GetGems().empty())
        for (const auto& enemy : game.GetEnemies()) {
            const float distance = Length(enemy.position - game.GetPlayer().position);
            if (distance < closest) {
                closest = distance;
                target = enemy.position;
            }
        }
    Vec2 movement = target - game.GetPlayer().position;
    const float length = Length(movement);
    return length > .1f ? movement * (1 / length) : Vec2{};
}

void TestKoreanWrapping() {
    const auto width = [](std::string_view text) {
        size_t offset = 0;
        float count = 0;
        while (offset < text.size()) {
            Check(LuxSamples::NextCodepoint(text, offset) != 0xFFFD,
                  "wrapped Korean lines contain complete UTF-8 characters");
            ++count;
        }
        return count;
    };
    const auto korean = WrapText("가나다라마바사힣", 3, 3, width);
    Check(korean.complete && korean.lines.size() == 3 && korean.lines[0] == "가나다" &&
              korean.lines[1] == "라마바",
          "long Korean words wrap on codepoint boundaries");
    Check(korean.lines[2] == "사힣", "the last Korean syllable is preserved");
    const auto mixed = WrapText("WASD 이동 후 돌진", 7, 3, width);
    Check(mixed.complete && mixed.lines.size() == 2 && mixed.lines[0] == "WASD 이동" &&
              mixed.lines[1] == "후 돌진",
          "mixed Korean and ASCII prefer word boundaries");
    Check(!WrapText("가나다라", 1, 3, width).complete,
          "layout reports truncated Korean text instead of silently dropping it");
    Check(!WrapText("힣", .5f, 2, width).complete,
          "a glyph wider than its container reports overflow without splitting its bytes");
}

void TestMovementAndTime() {
    Rules rules;
    rules.spawnRate = 0;
    rules.runDuration = 600;
    SurvivalGame axis(42, rules), diagonal(42, rules), smallSteps(42, rules);
    axis.Start();
    diagonal.Start();
    smallSteps.Start();
    for (int frame = 0; frame < 120; ++frame) {
        axis.Tick(1.0 / 60, {1, 0});
        diagonal.Tick(1.0 / 60, {1, 1});
    }
    for (int frame = 0; frame < 480; ++frame)
        smallSteps.Tick(1.0 / 240, {1, 0});
    Check(std::abs(Length(axis.GetPlayer().position) - Length(diagonal.GetPlayer().position)) <
              .001f,
          "diagonal movement has the same speed as axis movement");
    Check(std::abs(axis.GetPlayer().position.x - smallSteps.GetPlayer().position.x) < .001f &&
              std::abs(axis.Elapsed() - smallSteps.Elapsed()) < .001f,
          "fixed-step state is independent of render frame rate");
    const auto before = axis.GetPlayer().position;
    axis.TogglePause();
    for (int frame = 0; frame < 60; ++frame)
        axis.Tick(1.0 / 60, {1, 0}, true);
    Check(axis.GetPhase() == Phase::Paused && axis.GetPlayer().position.x == before.x &&
              axis.Elapsed() == smallSteps.Elapsed(),
          "pause freezes movement and the run clock");
    axis.TogglePause();
    axis.Tick(1.0 / 60, {1, 0}, true);
    Check(axis.GetPlayer().dashCooldown > 0 && axis.GetPlayer().position.x > before.x + .1f,
          "dash moves faster and starts its cooldown");
    axis.Tick(std::numeric_limits<double>::quiet_NaN(), {1, 0});
    axis.Tick(-1, {1, 0});
    axis.Tick(1.0 / 60, {std::numeric_limits<float>::infinity(), 0});
    Check(std::isfinite(axis.GetPlayer().position.x),
          "invalid frame input cannot poison the state");
    for (int frame = 0; frame < 1000; ++frame)
        axis.Tick(1.0 / 60, {1, 1});
    Check(std::abs(axis.GetPlayer().position.x) < SurvivalGame::ArenaHalfSize &&
              std::abs(axis.GetPlayer().position.y) < SurvivalGame::ArenaHalfSize,
          "the player stays inside the arena");
}

void TestCombatAndUpgrade() {
    Rules rules;
    rules.runDuration = 100;
    rules.spawnRate = 3;
    rules.incomingDamageScale = 0;
    rules.enemyHealthScale = .001f;
    SurvivalGame game(184, rules);
    game.Start();
    for (int frame = 0; frame < 3600 && game.GetPhase() == Phase::Playing; ++frame)
        game.Tick(1.0 / 60, BotMovement(game));
    Check(game.Kills() > 0 && game.GetPhase() == Phase::Upgrade && game.GetPlayer().level == 2,
          "automatic combat, XP drops and collection lead to an upgrade choice");
    const auto choices = game.GetChoices();
    Check(choices[0].id != choices[1].id && choices[1].id != choices[2].id &&
              choices[0].id != choices[2].id,
          "each level offers three different upgrades");
    const auto elapsed = game.Elapsed();
    const auto position = game.GetPlayer().position;
    for (int frame = 0; frame < 120; ++frame)
        game.Tick(1.0 / 60, {1, 1}, true);
    Check(game.Elapsed() == elapsed && game.GetPlayer().position.x == position.x &&
              game.GetPlayer().dashCooldown == 0,
          "upgrade selection freezes the complete simulation");
    Check(!game.ChooseUpgrade(-1) && !game.ChooseUpgrade(3) && game.GetPhase() == Phase::Upgrade,
          "invalid upgrade selections leave the pending choice intact");
    Check(game.ChooseUpgrade(0) && game.GetPhase() == Phase::Playing,
          "a valid choice applies the upgrade and resumes the run");
    game.Restart();
    Check(game.GetPhase() == Phase::Playing && game.Elapsed() == 0 && game.Kills() == 0 &&
              game.GetPlayer().level == 1 && game.GetPlayer().health == 100 &&
              game.GetPlayer().xp == 0 && game.GetEnemies().empty() && game.GetGems().empty() &&
              game.GetProjectiles().empty() && game.GetEffects().empty() && game.OrbitCount() == 0,
          "restart clears progression, combat entities and effects");
}

void TestOpeningSurvival() {
    SurvivalGame game;
    game.Start();
    int choice = 0;
    for (int frame = 0; frame < 60 * 60; ++frame) {
        if (game.GetPhase() == Phase::Upgrade)
            Check(game.ChooseUpgrade(choice++ % 3), "showcase bot can select its pending upgrade");
        const float time = game.Elapsed();
        const Vec2 target{std::cos(time * .21f) * 6, std::sin(time * .21f) * 4.5f};
        Vec2 movement = target - game.GetPlayer().position;
        const float length = Length(movement);
        movement = length > .1f ? movement * (1 / length) : Vec2{};
        game.Tick(1.0 / 60, movement);
        Check(game.GetPhase() != Phase::Defeat, "basic kiting survives the opening forest waves");
    }
    Check(game.Elapsed() > 58 && game.Kills() >= 20 && game.GetPlayer().level >= 2,
          "the first minute contains combat, collected XP and meaningful character growth");
}

void TestEndStatesAndLimits() {
    Rules quick;
    quick.runDuration = .5f;
    quick.spawnRate = 0;
    quick.bossHealth = 1;
    quick.incomingDamageScale = 0;
    SurvivalGame victory(91, quick);
    victory.Start();
    for (int frame = 0; frame < 600 && victory.GetPhase() == Phase::Playing; ++frame)
        victory.Tick(1.0 / 60, {});
    Check(victory.BossSpawned() && victory.GetPhase() == Phase::Victory && victory.Kills() == 1,
          "reaching the timer spawns a boss and defeating it wins the run");
    const auto endTime = victory.Elapsed();
    victory.Tick(.2, {1, 0});
    Check(victory.Elapsed() == endTime, "victory freezes the simulation");

    Rules hard;
    hard.runDuration = 300;
    hard.spawnRate = 4;
    hard.incomingDamageScale = 100;
    hard.enemyHealthScale = 100;
    SurvivalGame defeat(3, hard);
    defeat.Start();
    for (int frame = 0; frame < 1800 && defeat.GetPhase() == Phase::Playing; ++frame)
        defeat.Tick(1.0 / 60, {});
    Check(defeat.GetPhase() == Phase::Defeat && defeat.GetPlayer().health == 0,
          "enemy contact can defeat the player without negative health");
    defeat.Restart();
    Check(defeat.GetPhase() == Phase::Playing && defeat.GetPlayer().health == 100,
          "a lost run can be restarted");

    hard.incomingDamageScale = 0;
    hard.spawnRate = 10;
    SurvivalGame bounded(101, hard);
    bounded.Start();
    for (int frame = 0; frame < 3600; ++frame) {
        bounded.Tick(1.0 / 60, {});
        Check(bounded.GetEnemies().size() <= SurvivalGame::MaxEnemies &&
                  bounded.GetProjectiles().size() <= SurvivalGame::MaxProjectiles &&
                  bounded.GetGems().size() <= SurvivalGame::MaxGems &&
                  bounded.GetEffects().size() <= SurvivalGame::MaxEffects,
              "dense waves respect game-object bounds");
    }
    Check(bounded.GetEnemies().size() == SurvivalGame::MaxEnemies,
          "the density test reaches the enemy cap");
}
} // namespace

int RunForestSurvivorTests(int, char**) {
    try {
        TestKoreanWrapping();
        TestMovementAndTime();
        TestCombatAndUpgrade();
        TestOpeningSurvival();
        TestEndStatesAndLimits();
        std::cout << "Forest Survivor gameplay checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Forest Survivor check failed: " << error.what() << '\n';
        return 1;
    }
}

LUX_ENGINE_MAIN(RunForestSurvivorTests)
