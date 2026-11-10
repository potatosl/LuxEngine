#include "SurvivalGame.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace ForestGame {
namespace {
constexpr float Pi = std::numbers::pi_v<float>;
constexpr double StepSeconds = 1.0 / 120.0;
float LengthSquared(Vec2 v) {
    return v.x * v.x + v.y * v.y;
}
Vec2 Normalized(Vec2 value) {
    const float length = std::sqrt(LengthSquared(value));
    return length > .00001f ? value * (1.0f / length) : Vec2{};
}
Vec2 ClampPosition(Vec2 position, float radius = .4f) {
    const float extent = SurvivalGame::ArenaHalfSize - radius;
    return {std::clamp(position.x, -extent, extent), std::clamp(position.y, -extent, extent)};
}
constexpr std::array<Upgrade, 9> Upgrades{{
    {UpgradeId::Might, "WILD THORNS", "+30% weapon damage"},
    {UpgradeId::Haste, "QUICKENING", "Fire seeds 18% faster"},
    {UpgradeId::Stride, "WINDSTEP", "+15% movement speed"},
    {UpgradeId::Vitality, "OLD GROWTH", "+20 maximum health and heal"},
    {UpgradeId::Harvest, "MAGNET BLOOM", "+35% experience collection range"},
    {UpgradeId::Orbit, "LEAF GUARD", "Add an orbiting razor leaf"},
    {UpgradeId::Multishot, "FORKED BRANCH", "Fire one additional seed"},
    {UpgradeId::Bloom, "EARTH PULSE", "Larger and stronger pulse attack"},
    {UpgradeId::Renewal, "SPRING WATER", "Heal 35 HP and regenerate health"},
}};
} // namespace

SurvivalGame::SurvivalGame(std::uint32_t seed, Rules rules)
    : seed_(seed), rules_(rules), random_(seed) {
    const auto sane = [](float value, float fallback, float minimum, float maximum) {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
    };
    rules_.runDuration = sane(rules_.runDuration, RunDuration, .5f, 3600);
    rules_.spawnRate = sane(rules_.spawnRate, 1, 0, 10);
    rules_.incomingDamageScale = sane(rules_.incomingDamageScale, 1, 0, 100);
    rules_.enemyHealthScale = sane(rules_.enemyHealthScale, 1, .001f, 100);
    rules_.bossHealth = sane(rules_.bossHealth, 800, 1, 100000);
    Reset();
}
void SurvivalGame::Reset() {
    random_.seed(seed_);
    player_ = {};
    enemies_.clear();
    projectiles_.clear();
    gems_.clear();
    effects_.clear();
    choices_ = {};
    facing_ = dashDirection_ = {1, 0};
    elapsed_ = spawnTimer_ = attackTimer_ = orbitAngle_ = dashRemaining_ = regen_ = 0;
    pulseTimer_ = 3.5f;
    orbitRadius_ = 1.55f;
    pulseRadius_ = 2.8f;
    orbitCount_ = kills_ = 0;
    bossSpawned_ = bossDefeated_ = false;
    accumulator_ = 0;
    nextEnemyId_ = 1;
}
void SurvivalGame::Start() {
    if (phase_ == Phase::Title || phase_ == Phase::Defeat || phase_ == Phase::Victory)
        Restart();
}
void SurvivalGame::Restart() {
    Reset();
    phase_ = Phase::Playing;
}
void SurvivalGame::TogglePause() {
    if (phase_ == Phase::Playing)
        phase_ = Phase::Paused;
    else if (phase_ == Phase::Paused)
        phase_ = Phase::Playing;
}
bool SurvivalGame::ChooseUpgrade(int index) {
    if (phase_ != Phase::Upgrade || index < 0 || index >= static_cast<int>(choices_.size()))
        return false;
    switch (choices_[static_cast<size_t>(index)].id) {
    case UpgradeId::Might:
        player_.damage *= 1.3f;
        break;
    case UpgradeId::Haste:
        player_.cooldown = std::max(.16f, player_.cooldown * .82f);
        break;
    case UpgradeId::Stride:
        player_.speed = std::min(8.0f, player_.speed * 1.15f);
        break;
    case UpgradeId::Vitality:
        player_.maxHealth += 20;
        player_.health = std::min(player_.maxHealth, player_.health + 20);
        break;
    case UpgradeId::Harvest:
        player_.pickupRadius = std::min(12.0f, player_.pickupRadius * 1.35f);
        break;
    case UpgradeId::Orbit:
        orbitCount_ = std::min(4, orbitCount_ + 1);
        break;
    case UpgradeId::Multishot:
        player_.multishot = std::min(5, player_.multishot + 1);
        break;
    case UpgradeId::Bloom:
        pulseRadius_ = std::min(8.0f, pulseRadius_ + .75f);
        break;
    case UpgradeId::Renewal:
        player_.health = std::min(player_.maxHealth, player_.health + 35);
        regen_ += .35f;
        break;
    }
    phase_ = Phase::Playing;
    return true;
}
float SurvivalGame::Random(float minimum, float maximum) {
    return std::uniform_real_distribution<float>(minimum, maximum)(random_);
}
void SurvivalGame::Tick(double dt, Vec2 movement, bool dash) {
    if (phase_ != Phase::Playing || !std::isfinite(dt) || dt <= 0)
        return;
    if (!std::isfinite(movement.x) || !std::isfinite(movement.y))
        movement = {};
    if (LengthSquared(movement) > 1)
        movement = Normalized(movement);
    if (LengthSquared(movement) > .001f)
        facing_ = Normalized(movement);
    if (dash && player_.dashCooldown <= 0) {
        dashDirection_ = facing_;
        dashRemaining_ = .16f;
        player_.dashCooldown = DashRecharge;
        player_.invulnerable = std::max(player_.invulnerable, .2f);
        AddEffect(player_.position, .6f, 3);
    }
    accumulator_ += std::min(dt, .25);
    while (accumulator_ + 1e-9 >= StepSeconds && phase_ == Phase::Playing) {
        accumulator_ -= StepSeconds;
        Step(static_cast<float>(StepSeconds), movement);
    }
    if (phase_ != Phase::Playing)
        accumulator_ = 0;
}
void SurvivalGame::Step(float dt, Vec2 movement) {
    elapsed_ += dt;
    player_.invulnerable = std::max(0.0f, player_.invulnerable - dt);
    player_.dashCooldown = std::max(0.0f, player_.dashCooldown - dt);
    player_.health = std::min(player_.maxHealth, player_.health + regen_ * dt);
    if (dashRemaining_ > 0) {
        dashRemaining_ -= dt;
        player_.position += dashDirection_ * (player_.speed * 3.5f * dt);
    } else {
        player_.position += movement * (player_.speed * dt);
    }
    player_.position = ClampPosition(player_.position);
    for (auto& effect : effects_)
        effect.age += dt;
    std::erase_if(effects_, [](const Effect& effect) { return effect.age >= effect.duration; });

    if (elapsed_ < rules_.runDuration && rules_.spawnRate > 0) {
        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0) {
            const float wave = std::floor(elapsed_ / 30);
            SpawnEnemy();
            spawnTimer_ += 1.0f / (rules_.spawnRate * (1.15f + .4f * wave));
        }
    }
    if (!bossSpawned_ && elapsed_ >= rules_.runDuration) {
        SpawnEnemy(true);
        bossSpawned_ = true;
    }
    attackTimer_ -= dt;
    if (attackTimer_ <= 0 && !enemies_.empty()) {
        Shoot();
        attackTimer_ = player_.cooldown;
    }
    orbitAngle_ += dt * 2.8f;
    if (orbitAngle_ > 2 * Pi)
        orbitAngle_ -= 2 * Pi;
    pulseTimer_ -= dt;
    if (pulseTimer_ <= 0) {
        pulseTimer_ += 4.0f;
        AddEffect(player_.position, pulseRadius_, 1);
        for (auto& enemy : enemies_) {
            if (LengthSquared(enemy.position - player_.position) <
                (pulseRadius_ + enemy.radius) * (pulseRadius_ + enemy.radius)) {
                enemy.health -= player_.damage * (.7f + (pulseRadius_ - 2.8f) * .3f);
                enemy.flash = .12f;
                enemy.position += Normalized(enemy.position - player_.position) * .55f;
            }
        }
    }
    UpdateEnemies(dt);
    UpdateProjectiles(dt);
    ResolveKills();
    if (phase_ != Phase::Playing)
        return;
    UpdateGems(dt);
    if (bossDefeated_) {
        phase_ = Phase::Victory;
        return;
    }
    if (player_.xp >= player_.xpNeeded) {
        player_.xp -= player_.xpNeeded;
        ++player_.level;
        player_.xpNeeded =
            std::min(100000, static_cast<int>(std::ceil(player_.xpNeeded * 1.24f + 2)));
        OfferUpgrade();
    }
}
void SurvivalGame::SpawnEnemy(bool boss) {
    if (enemies_.size() >= MaxEnemies) {
        if (!boss)
            return;
        enemies_.pop_back();
    }
    const float angle = Random(0, 2 * Pi);
    const float distance = boss ? 8.0f : Random(10.0f, 14.0f);
    Enemy enemy;
    enemy.position =
        ClampPosition(player_.position + Vec2{std::cos(angle), std::sin(angle)} * distance, 1.2f);
    // Avoid spawning on the player when they stand close to the forest boundary.
    if (LengthSquared(enemy.position - player_.position) < 36)
        enemy.position = ClampPosition(
            player_.position - Vec2{std::cos(angle), std::sin(angle)} * distance, 1.2f);
    const float roll = Random(0, 1);
    enemy.type = elapsed_ > 60 && roll > .73f ? 2 : elapsed_ > 25 && roll > .44f ? 1 : 0;
    enemy.boss = boss;
    enemy.id = nextEnemyId_++;
    enemy.radius = boss ? 1.05f : enemy.type == 2 ? .48f : enemy.type == 1 ? .24f : .34f;
    enemy.maxHealth = boss ? rules_.bossHealth
                           : (enemy.type == 2   ? 54.0f
                              : enemy.type == 1 ? 14.0f
                                                : 22.0f) *
                                 (1 + elapsed_ * .005f) * rules_.enemyHealthScale;
    enemy.health = enemy.maxHealth;
    enemy.attackTimer = 2.5f;
    enemies_.push_back(enemy);
    if (boss)
        AddEffect(enemy.position, 2.0f, 4);
}
void SurvivalGame::Shoot() {
    const Enemy* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (const auto& enemy : enemies_) {
        const float squared = LengthSquared(enemy.position - player_.position);
        if (enemy.health > 0 && squared < best) {
            best = squared;
            nearest = &enemy;
        }
    }
    if (!nearest)
        return;
    const Vec2 direction = Normalized(nearest->position - player_.position);
    const float baseAngle = std::atan2(direction.y, direction.x);
    for (int shot = 0; shot < player_.multishot && projectiles_.size() < MaxProjectiles; ++shot) {
        const float angle = baseAngle + (shot - (player_.multishot - 1) * .5f) * .15f;
        Projectile projectile;
        projectile.position = player_.position;
        projectile.direction = {std::cos(angle), std::sin(angle)};
        projectile.damage = player_.damage;
        projectiles_.push_back(projectile);
    }
}
void SurvivalGame::UpdateEnemies(float dt) {
    for (auto& enemy : enemies_) {
        if (enemy.health <= 0)
            continue;
        enemy.flash = std::max(0.0f, enemy.flash - dt);
        enemy.orbitCooldown = std::max(0.0f, enemy.orbitCooldown - dt);
        const float speed = enemy.boss ? .95f
                                       : (enemy.type == 1   ? 2.2f
                                          : enemy.type == 2 ? .85f
                                                            : 1.25f);
        enemy.position += Normalized(player_.position - enemy.position) * (speed * dt);
        for (int leaf = 0; leaf < orbitCount_ && enemy.orbitCooldown <= 0; ++leaf) {
            const float angle = orbitAngle_ + leaf * 2 * Pi / orbitCount_;
            const Vec2 position =
                player_.position + Vec2{std::cos(angle), std::sin(angle)} * orbitRadius_;
            if (LengthSquared(enemy.position - position) <
                (enemy.radius + .25f) * (enemy.radius + .25f)) {
                enemy.health -= player_.damage * .65f;
                enemy.flash = .12f;
                enemy.orbitCooldown = .3f;
            }
        }
        if (enemy.health > 0 && LengthSquared(enemy.position - player_.position) <
                                    (enemy.radius + .3f) * (enemy.radius + .3f)) {
            ApplyDamage(enemy.boss ? 20.0f : enemy.type == 2 ? 14.0f : 8.0f);
            if (!enemy.boss)
                enemy.position += Normalized(enemy.position - player_.position) * .3f;
        }
        if (enemy.boss) {
            enemy.attackTimer -= dt;
            if (enemy.attackTimer <= 0) {
                enemy.attackTimer += 3.0f;
                AddEffect(enemy.position, 1.5f, 4);
                const float offset = elapsed_ * .55f;
                for (int shot = 0; shot < 12 && projectiles_.size() < MaxProjectiles; ++shot) {
                    const float angle = offset + shot * 2 * Pi / 12;
                    Projectile projectile;
                    projectile.position = enemy.position;
                    projectile.direction = {std::cos(angle), std::sin(angle)};
                    projectile.radius = .18f;
                    projectile.remaining = 5;
                    projectile.damage = 12;
                    projectile.hostile = true;
                    projectiles_.push_back(projectile);
                }
            }
        }
    }
    for (size_t a = 0; a < enemies_.size(); ++a) {
        for (size_t b = a + 1; b < enemies_.size(); ++b) {
            const Vec2 delta = enemies_[a].position - enemies_[b].position;
            const float squared = LengthSquared(delta);
            const float radius = (enemies_[a].radius + enemies_[b].radius) * .8f;
            if (squared < radius * radius && squared > .00001f) {
                const Vec2 push = Normalized(delta) * (.9f * dt);
                enemies_[a].position += push;
                enemies_[b].position += push * -1;
            }
        }
    }
}
void SurvivalGame::UpdateProjectiles(float dt) {
    for (auto& projectile : projectiles_) {
        projectile.remaining -= dt;
        projectile.position += projectile.direction * ((projectile.hostile ? 3.0f : 13.0f) * dt);
        if (projectile.hostile) {
            if (LengthSquared(projectile.position - player_.position) <
                (projectile.radius + .3f) * (projectile.radius + .3f)) {
                ApplyDamage(projectile.damage);
                projectile.remaining = 0;
            }
            continue;
        }
        for (auto& enemy : enemies_) {
            if (enemy.health <= 0 || projectile.remaining <= 0)
                continue;
            if (LengthSquared(projectile.position - enemy.position) <
                (projectile.radius + enemy.radius) * (projectile.radius + enemy.radius)) {
                enemy.health -= projectile.damage;
                enemy.flash = .12f;
                AddEffect(projectile.position, .3f, 0);
                projectile.remaining = 0;
            }
        }
    }
    std::erase_if(projectiles_,
                  [](const Projectile& projectile) { return projectile.remaining <= 0; });
}
void SurvivalGame::ApplyDamage(float damage) {
    if (player_.invulnerable > 0 || rules_.incomingDamageScale <= 0)
        return;
    player_.health = std::max(0.0f, player_.health - damage * rules_.incomingDamageScale);
    player_.invulnerable = .65f;
    AddEffect(player_.position, .65f, 0);
    if (player_.health <= 0)
        phase_ = Phase::Defeat;
}
void SurvivalGame::ResolveKills() {
    for (const auto& enemy : enemies_) {
        if (enemy.health > 0)
            continue;
        ++kills_;
        const int value = enemy.boss ? 100 : enemy.type == 2 ? 4 : enemy.type == 1 ? 2 : 1;
        if (gems_.size() < MaxGems) {
            gems_.push_back({enemy.position, value});
        } else {
            auto nearest =
                std::min_element(gems_.begin(), gems_.end(), [&](const Gem& a, const Gem& b) {
                    return LengthSquared(a.position - enemy.position) <
                           LengthSquared(b.position - enemy.position);
                });
            nearest->value += value;
        }
        AddEffect(enemy.position, enemy.radius * 1.6f, 0);
        if (enemy.boss)
            bossDefeated_ = true;
    }
    std::erase_if(enemies_, [](const Enemy& enemy) { return enemy.health <= 0; });
}
void SurvivalGame::UpdateGems(float dt) {
    for (auto& gem : gems_) {
        const Vec2 delta = player_.position - gem.position;
        const float squared = LengthSquared(delta);
        if (squared < player_.pickupRadius * player_.pickupRadius)
            gem.position += Normalized(delta) * (8.0f * dt);
        if (squared < .45f * .45f) {
            player_.xp += gem.value;
            gem.value = 0;
        }
    }
    std::erase_if(gems_, [](const Gem& gem) { return gem.value == 0; });
}
void SurvivalGame::OfferUpgrade() {
    phase_ = Phase::Upgrade;
    AddEffect(player_.position, 1.3f, 2);
    std::array<Upgrade, Upgrades.size()> available = Upgrades;
    std::shuffle(available.begin(), available.end(), random_);
    int selected = 0;
    for (const auto& upgrade : available) {
        if ((upgrade.id == UpgradeId::Orbit && orbitCount_ >= 4) ||
            (upgrade.id == UpgradeId::Multishot && player_.multishot >= 5) ||
            (upgrade.id == UpgradeId::Haste && player_.cooldown <= .16f) ||
            (upgrade.id == UpgradeId::Stride && player_.speed >= 8) ||
            (upgrade.id == UpgradeId::Harvest && player_.pickupRadius >= 12) ||
            (upgrade.id == UpgradeId::Bloom && pulseRadius_ >= 8))
            continue;
        choices_[static_cast<size_t>(selected++)] = upgrade;
        if (selected == static_cast<int>(choices_.size()))
            break;
    }
}
void SurvivalGame::AddEffect(Vec2 position, float radius, int type) {
    if (effects_.size() >= MaxEffects)
        effects_.erase(effects_.begin());
    effects_.push_back({position, 0, type == 4 ? 1.0f : .45f, radius, type});
}
} // namespace ForestGame
