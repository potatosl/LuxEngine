#pragma once

#include <LuxEngine/Core/Math.hpp>
#include <array>
#include <cstdint>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace ForestGame {
using LuxEngine::Vec2;
enum class Phase { Title, Playing, Paused, Upgrade, Defeat, Victory };
enum class UpgradeId { Might, Haste, Stride, Vitality, Harvest, Orbit, Multishot, Bloom, Renewal };

struct Upgrade {
    UpgradeId id{};
    std::string_view title;
    std::string_view description;
};

struct Player {
    Vec2 position{};
    float health = 100, maxHealth = 100;
    int level = 1, xp = 0, xpNeeded = 8;
    float damage = 18, speed = 3.8f, cooldown = .65f, pickupRadius = 2.1f;
    float invulnerable = 0, dashCooldown = 0;
    int multishot = 1;
};

struct Enemy {
    Vec2 position{};
    float health = 0, maxHealth = 0;
    int type = 0;
    bool boss = false;
    float radius = .32f;
    float flash = 0;
    std::uint32_t id = 0;
    float orbitCooldown = 0, attackTimer = 0;
};

struct Projectile {
    Vec2 position{}, direction{};
    float radius = .12f, remaining = 2.0f, damage = 0;
    bool hostile = false;
};

struct Gem {
    Vec2 position{};
    int value = 1;
};

struct Effect {
    Vec2 position{};
    float age = 0, duration = .5f, radius = 1;
    int type = 0; // 0: impact, 1: pulse, 2: level-up, 3: dash, 4: enemy spawn
};

struct Rules {
    float runDuration = 300;
    float spawnRate = 1;
    float incomingDamageScale = 1;
    float enemyHealthScale = 1;
    float bossHealth = 800;
};

class SurvivalGame {
public:
    static constexpr float RunDuration = 300;
    static constexpr float DashRecharge = 2.2f;
    static constexpr float ArenaHalfSize = 30;
    static constexpr size_t MaxEnemies = 220;
    static constexpr size_t MaxProjectiles = 500;
    static constexpr size_t MaxGems = 400;
    static constexpr size_t MaxEffects = 140;
    explicit SurvivalGame(std::uint32_t seed = 0x51a7e123u, Rules rules = {});
    static std::span<const Upgrade> UpgradeCatalog();
    void Start();
    void Restart();
    void TogglePause();
    bool ChooseUpgrade(int index);
    void Tick(double dt, Vec2 movement, bool dash = false);

    Phase GetPhase() const;

    const Player& GetPlayer() const;

    const std::vector<Enemy>& GetEnemies() const;

    const std::vector<Projectile>& GetProjectiles() const;

    const std::vector<Gem>& GetGems() const;

    const std::vector<Effect>& GetEffects() const;

    const std::array<Upgrade, 3>& GetChoices() const;

    float Elapsed() const;

    float Duration() const;

    int Kills() const;

    Vec2 LastFacing() const;

    float OrbitAngle() const;

    int OrbitCount() const;

    float OrbitRadius() const;

    float PulseRadius() const;

    bool BossSpawned() const;

private:
    void Reset();
    void Step(float dt, Vec2 movement);
    void SpawnEnemy(bool boss = false);
    void Shoot();
    void UpdateEnemies(float dt);
    void UpdateProjectiles(float dt);
    void UpdateGems(float dt);
    void ApplyDamage(float damage);
    void ResolveKills();
    void OfferUpgrade();
    void AddEffect(Vec2 position, float radius, int type);
    float Random(float min, float max);
    std::uint32_t seed_;
    Rules rules_;
    std::mt19937 random_;
    Phase phase_ = Phase::Title;
    Player player_{};
    Vec2 facing_{1, 0}, dashDirection_{1, 0};
    std::vector<Enemy> enemies_;
    std::vector<Projectile> projectiles_;
    std::vector<Gem> gems_;
    std::vector<Effect> effects_;
    std::array<Upgrade, 3> choices_{};
    float elapsed_ = 0, spawnTimer_ = 0, attackTimer_ = 0, pulseTimer_ = 3.5f;
    float orbitAngle_ = 0, orbitRadius_ = 1.55f, pulseRadius_ = 2.8f, regen_ = 0;
    float dashRemaining_ = 0;
    double accumulator_ = 0;
    int orbitCount_ = 0, kills_ = 0;
    bool bossSpawned_ = false, bossDefeated_ = false;
    std::uint32_t nextEnemyId_ = 1;
};
} // namespace ForestGame
