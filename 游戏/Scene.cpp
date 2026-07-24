#include "Scene.h"

#include "Boss.h"
#include "CombatTuning.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

namespace
{
    void FillPixelRect(Gdiplus::Graphics& graphics, Gdiplus::Brush& brush, float originX, float originY, int x, int y, int width, int height)
    {
        constexpr float kPixel = 2.0f;
        graphics.FillRectangle(
            &brush,
            originX + x * kPixel,
            originY + y * kPixel,
            width * kPixel,
            height * kPixel);
    }

    constexpr float kDummyRespawnSeconds = 5.0f;
    constexpr float kDummyImpactDuration = 0.24f;
    constexpr float kDummyImpactCompressionEnd = 0.30f;
    constexpr int kHeartHealAmount = 20;
    constexpr int kHeartDropPercent = 30;
    constexpr float kHeartPickupRadius = 38.0f;
    constexpr float kHeartLifetimeSeconds = 20.0f;
    constexpr float kPi = 3.14159265358979323846f;

    bool CircleIntersectsForwardSector(
        Vec2 origin,
        Vec2 forward,
        float sectorRadius,
        float halfAngleDegrees,
        Vec2 circleCenter,
        float circleRadius)
    {
        forward = Normalize(forward);
        if (forward.x == 0.0f && forward.y == 0.0f)
        {
            forward = { 1.0f, 0.0f };
        }

        const Vec2 toCircle{ circleCenter.x - origin.x, circleCenter.y - origin.y };
        const float distanceSquared = toCircle.x * toCircle.x + toCircle.y * toCircle.y;
        if (distanceSquared <= circleRadius * circleRadius)
        {
            return true;
        }

        const float maximumDistance = sectorRadius + circleRadius;
        if (distanceSquared > maximumDistance * maximumDistance)
        {
            return false;
        }

        const float distance = std::sqrt(distanceSquared);
        const float forwardProjection = toCircle.x * forward.x + toCircle.y * forward.y;
        if (forwardProjection < -circleRadius)
        {
            return false;
        }

        const float angularAllowance = std::asin(Clamp(circleRadius / distance, 0.0f, 1.0f));
        const float allowedAngle = (std::min)(
            kPi,
            halfAngleDegrees * kPi / 180.0f + angularAllowance);
        return forwardProjection / distance >= std::cos(allowedAngle);
    }

    float EaseOutCubic(float value)
    {
        const float clamped = Clamp(value, 0.0f, 1.0f);
        const float inverse = 1.0f - clamped;
        return 1.0f - inverse * inverse * inverse;
    }

    float SmoothRecovery(float value)
    {
        const float clamped = Clamp(value, 0.0f, 1.0f);
        const float remaining = 1.0f - clamped;
        return remaining * remaining * (3.0f - 2.0f * remaining);
    }

    void DrawTrainingDummyBody(Gdiplus::Graphics& graphics, float centerX, float bottomY, bool flash, bool defeated)
    {
        constexpr float kSpriteSize = 96.0f;
        const float originX = centerX - kSpriteSize * 0.5f;
        const float originY = bottomY - kSpriteSize;

        const Gdiplus::Color shadowColor = defeated ? Gdiplus::Color(72, 38, 38, 38) : Gdiplus::Color(95, 36, 31, 24);
        const Gdiplus::Color darkWoodColor = defeated ? Gdiplus::Color(255, 76, 76, 76) : Gdiplus::Color(255, 69, 45, 31);
        const Gdiplus::Color woodColor = defeated ? Gdiplus::Color(255, 112, 112, 112) : Gdiplus::Color(255, 118, 73, 43);
        const Gdiplus::Color woodLightColor = defeated ? Gdiplus::Color(255, 154, 154, 154) : Gdiplus::Color(255, 171, 111, 57);
        const Gdiplus::Color strawDarkColor = defeated ? Gdiplus::Color(255, 98, 98, 98) : Gdiplus::Color(255, 139, 91, 35);
        const Gdiplus::Color strawColor = defeated ? Gdiplus::Color(255, 150, 150, 150) : Gdiplus::Color(255, 220, 169, 65);
        const Gdiplus::Color strawLightColor = defeated ? Gdiplus::Color(255, 190, 190, 190) : Gdiplus::Color(255, 255, 220, 112);
        const Gdiplus::Color ropeColor = defeated ? Gdiplus::Color(255, 126, 126, 126) : Gdiplus::Color(255, 184, 124, 43);
        const Gdiplus::Color clothColor = defeated ? Gdiplus::Color(255, 82, 82, 82) : Gdiplus::Color(255, 174, 48, 42);
        const Gdiplus::Color clothLightColor = defeated ? Gdiplus::Color(255, 142, 142, 142) : Gdiplus::Color(255, 232, 78, 55);

        Gdiplus::SolidBrush shadow(shadowColor);
        Gdiplus::SolidBrush darkWood(flash ? Gdiplus::Color(255, 255, 255, 255) : darkWoodColor);
        Gdiplus::SolidBrush wood(flash ? Gdiplus::Color(255, 255, 246, 202) : woodColor);
        Gdiplus::SolidBrush woodLight(flash ? Gdiplus::Color(255, 255, 255, 255) : woodLightColor);
        Gdiplus::SolidBrush strawDark(flash ? Gdiplus::Color(255, 255, 224, 112) : strawDarkColor);
        Gdiplus::SolidBrush straw(flash ? Gdiplus::Color(255, 255, 250, 214) : strawColor);
        Gdiplus::SolidBrush strawLight(flash ? Gdiplus::Color(255, 255, 255, 255) : strawLightColor);
        Gdiplus::SolidBrush rope(flash ? Gdiplus::Color(255, 255, 240, 174) : ropeColor);
        Gdiplus::SolidBrush cloth(flash ? Gdiplus::Color(255, 255, 255, 255) : clothColor);
        Gdiplus::SolidBrush clothLight(flash ? Gdiplus::Color(255, 255, 255, 255) : clothLightColor);

        // Pixelated ground shadow and wooden base.
        FillPixelRect(graphics, shadow, originX, originY, 10, 44, 28, 3);
        FillPixelRect(graphics, darkWood, originX, originY, 12, 42, 24, 4);
        FillPixelRect(graphics, wood, originX, originY, 19, 35, 10, 8);
        FillPixelRect(graphics, woodLight, originX, originY, 22, 35, 3, 7);

        // Crossbar arms with dark one-pixel outlines.
        FillPixelRect(graphics, darkWood, originX, originY, 5, 23, 38, 7);
        FillPixelRect(graphics, wood, originX, originY, 7, 24, 34, 5);
        FillPixelRect(graphics, woodLight, originX, originY, 9, 24, 29, 2);
        FillPixelRect(graphics, straw, originX, originY, 3, 24, 5, 5);
        FillPixelRect(graphics, straw, originX, originY, 40, 24, 5, 5);

        // Straw body, rope bands, and a small red practice ribbon.
        FillPixelRect(graphics, strawDark, originX, originY, 15, 18, 18, 20);
        FillPixelRect(graphics, straw, originX, originY, 17, 18, 14, 19);
        FillPixelRect(graphics, strawLight, originX, originY, 19, 19, 5, 16);
        FillPixelRect(graphics, rope, originX, originY, 15, 25, 18, 3);
        FillPixelRect(graphics, rope, originX, originY, 16, 32, 16, 2);
        FillPixelRect(graphics, cloth, originX, originY, 31, 26, 7, 4);
        FillPixelRect(graphics, clothLight, originX, originY, 33, 27, 6, 2);
        FillPixelRect(graphics, cloth, originX, originY, 35, 29, 3, 6);

        // Round straw head with loose straw pixels and stitched eyes.
        FillPixelRect(graphics, strawDark, originX, originY, 14, 5, 20, 14);
        FillPixelRect(graphics, strawDark, originX, originY, 17, 2, 14, 20);
        FillPixelRect(graphics, straw, originX, originY, 15, 6, 18, 12);
        FillPixelRect(graphics, straw, originX, originY, 18, 3, 12, 18);
        FillPixelRect(graphics, strawLight, originX, originY, 18, 5, 7, 11);
        FillPixelRect(graphics, darkWood, originX, originY, 19, 11, 3, 2);
        FillPixelRect(graphics, darkWood, originX, originY, 27, 11, 3, 2);
        FillPixelRect(graphics, rope, originX, originY, 15, 17, 18, 3);
        FillPixelRect(graphics, strawLight, originX, originY, 12, 7, 3, 2);
        FillPixelRect(graphics, strawLight, originX, originY, 33, 8, 3, 2);
        FillPixelRect(graphics, straw, originX, originY, 21, 0, 2, 3);
        FillPixelRect(graphics, straw, originX, originY, 27, 1, 2, 3);
    }

    void DrawTrainingDummy(Gdiplus::Graphics& graphics, float centerX, float bottomY, bool flash, bool defeated)
    {
        if (!defeated)
        {
            DrawTrainingDummyBody(graphics, centerX, bottomY, flash, false);
            return;
        }

        Gdiplus::GraphicsState state = graphics.Save();
        graphics.TranslateTransform(centerX - 4.0f, bottomY - 24.0f);
        graphics.RotateTransform(82.0f);
        graphics.TranslateTransform(-(centerX - 4.0f), -(bottomY - 24.0f));
        DrawTrainingDummyBody(graphics, centerX + 15.0f, bottomY + 12.0f, false, true);
        graphics.Restore(state);
    }

    void DrawDummyPortrait(Gdiplus::Graphics& graphics, float centerX, float centerY)
    {
        Gdiplus::SolidBrush frame(Gdiplus::Color(235, 43, 34, 29));
        Gdiplus::SolidBrush fill(Gdiplus::Color(255, 235, 209, 123));
        Gdiplus::SolidBrush light(Gdiplus::Color(255, 255, 235, 155));
        Gdiplus::SolidBrush rope(Gdiplus::Color(255, 150, 90, 34));
        Gdiplus::SolidBrush eye(Gdiplus::Color(255, 48, 35, 28));

        graphics.FillEllipse(&frame, centerX - 28.0f, centerY - 28.0f, 56.0f, 56.0f);
        graphics.FillEllipse(&fill, centerX - 23.0f, centerY - 23.0f, 46.0f, 46.0f);
        graphics.FillRectangle(&light, centerX - 12.0f, centerY - 16.0f, 11.0f, 28.0f);
        graphics.FillRectangle(&rope, centerX - 19.0f, centerY + 8.0f, 38.0f, 5.0f);
        graphics.FillRectangle(&eye, centerX - 9.0f, centerY - 3.0f, 5.0f, 4.0f);
        graphics.FillRectangle(&eye, centerX + 5.0f, centerY - 3.0f, 5.0f, 4.0f);
        graphics.FillRectangle(&fill, centerX - 3.0f, centerY - 31.0f, 5.0f, 9.0f);
        graphics.FillRectangle(&fill, centerX + 10.0f, centerY - 28.0f, 5.0f, 8.0f);
    }
}

Scene::Scene()
{
    dummyHealthBar_.SetValue(dummyHealth_, dummyMaxHealth_);
}

void Scene::Initialize(RECT clientRect)
{
    ResetTrainingMode(clientRect);
}

void Scene::UnloadContent()
{
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        monster->UnloadContent();
    }
    monsters_.clear();
}

// 清空动态对象和跨帧事件，重建假人及其血条，得到确定的训练场初始状态。
void Scene::ResetTrainingMode(RECT clientRect)
{
    ResetMode(clientRect, false, false);
}

void Scene::ResetChallengeMode(RECT clientRect, bool hardMode)
{
    ResetMode(clientRect, true, hardMode);
}

// 三种玩法共用对象与随机状态清理，但只在普通/困难闯关创建卡芙卡。
// 训练模式保留稻草人及其五秒复活逻辑，各模式不会共享错误的敌人配置。
void Scene::ResetMode(RECT clientRect, bool challengeMode, bool hardMode)
{
    const float width = static_cast<float>(clientRect.right - clientRect.left);
    const float height = static_cast<float>(clientRect.bottom - clientRect.top);
    dummyCenter_ = { width * 0.68f, height * 0.5f + 8.0f };
    dummyHitEffect_.Reset();
    dummyRespawnTimer_ = 0.0f;
    dummyImpactTimer_ = 0.0f;
    dummyImpactDirection_ = { 1.0f, 0.0f };
    floatingDamages_.clear();
    monsters_.clear();
    heartPickups_.clear();
    monsterSpawnIndex_ = 0;
    pendingHealing_ = 0;
    pendingMonsterKills_ = 0;
    challengeMode_ = challengeMode;
    hardChallengeMode_ = challengeMode && hardMode;
    dummyEnabled_ = !challengeMode_;
    bossDefeatedReady_ = false;
    heartRandomState_ = 0x6d2b79f5u;
    minionRandomState_ = 0xa341316cu;
    ResetDummy();

    if (challengeMode_)
    {
        // 第一关固定生成卡芙卡；普通怪物只作为她召唤的随从出现。
        auto boss = std::make_unique<Boss>();
        boss->SetDifficulty(hardChallengeMode_ ? BossDifficulty::Hard : BossDifficulty::Normal);
        boss->Initialize(clientRect);
        monsters_.push_back(std::move(boss));
    }
}

// 在屏幕外生成敌人，使其自然走入视野；复用非活跃实例以减少重复加载图片。
void Scene::SpawnMonster(RECT clientRect)
{
    if (clientRect.right <= clientRect.left || clientRect.bottom <= clientRect.top || CountActiveMinions() >= 3)
    {
        return;
    }

    // 调试生成键只补充普通小怪；卡芙卡由关卡重置流程固定创建。
    std::unique_ptr<Monster> monster = std::make_unique<Monster>();
    monster->Initialize(clientRect);
    monster->SpawnAt(CalculateOffscreenMonsterSpawn(clientRect));
    monsters_.push_back(std::move(monster));
    ++monsterSpawnIndex_;
}

// 同时映射实体、浮字、掉落物和正在播放的命中特效，保持彼此相对位置不变。
void Scene::ResizeToClient(RECT oldClientRect, RECT newClientRect)
{
    const float oldWidth = static_cast<float>(oldClientRect.right - oldClientRect.left);
    const float oldHeight = static_cast<float>(oldClientRect.bottom - oldClientRect.top);
    const float newWidth = static_cast<float>(newClientRect.right - newClientRect.left);
    const float newHeight = static_cast<float>(newClientRect.bottom - newClientRect.top);
    if (oldWidth <= 0.0f || oldHeight <= 0.0f || newWidth <= 0.0f || newHeight <= 0.0f)
    {
        return;
    }

    const Vec2 offset{ (newWidth - oldWidth) * 0.5f, (newHeight - oldHeight) * 0.5f };
    dummyCenter_.x += offset.x;
    dummyCenter_.y += offset.y;
    dummyHitEffect_.Translate(offset);
    for (FloatingDamage& damage : floatingDamages_)
    {
        damage.position.x += offset.x;
        damage.position.y += offset.y;
    }
    for (HeartPickup& heart : heartPickups_)
    {
        heart.position.x += offset.x;
        heart.position.y += offset.y;
    }
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        monster->ResizeToClient(oldClientRect, newClientRect);
    }
}

// 场景每帧更新顺序：假人复活 -> 怪物 AI/分离 -> 掉落拾取 -> 短期视觉对象回收。
void Scene::Update(float deltaSeconds, const Player& player, RECT clientRect)
{
    const Vec2 playerPosition = player.GetPosition();
    const int playerMissingHealth = player.GetHealth() > 0
        ? player.GetMaxHealth() - player.GetHealth()
        : 0;

    if (dummyEnabled_ && !IsDummyAlive() && dummyRespawnTimer_ > 0.0f)
    {
        dummyRespawnTimer_ -= deltaSeconds;
        if (dummyRespawnTimer_ <= 0.0f)
        {
            ResetDummy();
        }
    }

    if (dummyEnabled_)
    {
        dummyHitEffect_.Update(deltaSeconds);
        dummyImpactTimer_ = (std::max)(0.0f, dummyImpactTimer_ - deltaSeconds);
        dummyHealthBar_.Update(deltaSeconds);
    }

    Boss* boss = FindBoss();
    if (boss != nullptr)
    {
        boss->SetActiveMinionCount(CountActiveMinions());
        // 必须先同步观察再调用 Boss::Update；若本帧恰好到达技能决策点，
        // AI 才能读到 Player::Update 刚更新完的生命和异常状态。
        boss->SetPlayerObservation(
            player.GetHealth(),
            player.GetMaxHealth(),
            player.IsDirectionConfused(),
            player.HasLightningMark(),
            player.IsMovementLocked());
    }
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        monster->Update(deltaSeconds, playerPosition);
    }

    // 主动响指和定时被动都只提交请求；Scene 统一生成并严格限制三只。
    if (boss != nullptr && boss->IsAlive())
    {
        int requests = boss->ConsumeSummonRequests();
        while (requests-- > 0 && CountActiveMinions() < 3)
        {
            SpawnMinionNearBoss(clientRect, boss->GetCenter());
        }
        boss->SetActiveMinionCount(CountActiveMinions());
    }
    for (std::size_t first = 0; first < monsters_.size(); ++first)
    {
        for (std::size_t second = first + 1; second < monsters_.size(); ++second)
        {
            monsters_[first]->ResolveCollisionWith(*monsters_[second]);
        }
    }
    monsters_.erase(
        std::remove_if(
            monsters_.begin(),
            monsters_.end(),
            [this](const std::unique_ptr<Monster>& monster)
            {
                if (!monster->IsDeathAnimationFinished())
                {
                    return false;
                }
                if (dynamic_cast<const Boss*>(monster.get()) != nullptr)
                {
                    bossDefeatedReady_ = true;
                }
                return true;
            }),
        monsters_.end());

    for (HeartPickup& heart : heartPickups_)
    {
        heart.lifetime += deltaSeconds;
        heart.bobTimer += deltaSeconds;
    }
    int healingNeeded = (std::max)(0, playerMissingHealth);
    heartPickups_.erase(
        std::remove_if(
            heartPickups_.begin(),
            heartPickups_.end(),
            [playerPosition, &healingNeeded, this](const HeartPickup& heart)
            {
                if (heart.lifetime >= kHeartLifetimeSeconds)
                {
                    return true;
                }
                if (healingNeeded <= 0)
                {
                    return false;
                }
                const float dx = playerPosition.x - heart.position.x;
                const float dy = playerPosition.y - heart.position.y;
                if (dx * dx + dy * dy > kHeartPickupRadius * kHeartPickupRadius)
                {
                    return false;
                }

                pendingHealing_ += kHeartHealAmount;
                healingNeeded = (std::max)(0, healingNeeded - kHeartHealAmount);
                FloatingDamage healing;
                healing.position = { playerPosition.x, playerPosition.y - 48.0f };
                healing.amount = kHeartHealAmount;
                healing.healing = true;
                floatingDamages_.push_back(healing);
                return true;
            }),
        heartPickups_.end());

    (void)clientRect;

    for (FloatingDamage& damage : floatingDamages_)
    {
        damage.lifetime += deltaSeconds;
        damage.position.y -= 38.0f * deltaSeconds;
    }

    floatingDamages_.erase(
        std::remove_if(
            floatingDamages_.begin(),
            floatingDamages_.end(),
            [](const FloatingDamage& damage)
            {
                return damage.lifetime >= damage.duration;
            }),
        floatingDamages_.end());
}

// 点攻击命中假人碰撞圆后，统一生成扣血浮字、残影血条和表面冲击特效。
bool Scene::TryHitDummy(Vec2 attackPoint, RECT clientRect, int damage)
{
    if (!IsDummyAlive())
    {
        return false;
    }

    const Vec2 center = GetDummyCenter(clientRect);
    const float dx = attackPoint.x - center.x;
    const float dy = attackPoint.y - center.y;
    const float kHitRadius = GetDummyCollisionRadius() + CombatTuning::MeleeContactTolerance;
    if (dx * dx + dy * dy > kHitRadius * kHitRadius)
    {
        return false;
    }

    const Vec2 impactAway{ center.x - attackPoint.x, center.y - attackPoint.y };
    const float impactLengthSquared = impactAway.x * impactAway.x + impactAway.y * impactAway.y;
    if (impactLengthSquared > 0.0001f)
    {
        dummyImpactDirection_ = Normalize(impactAway);
    }
    dummyImpactTimer_ = kDummyImpactDuration;
    dummyHitEffect_.Trigger(attackPoint, center, 38.0f);

    const int appliedDamage = damage > 0 ? damage : 0;
    dummyHealth_ = dummyHealth_ - appliedDamage;
    if (dummyHealth_ < 0)
    {
        dummyHealth_ = 0;
    }
    dummyHealthBar_.SetValue(dummyHealth_, dummyMaxHealth_);
    if (!IsDummyAlive())
    {
        // 训练模式：稻草人被打倒后不立刻重置，而是启动 5 秒复活计时器。
        dummyRespawnTimer_ = kDummyRespawnSeconds;
    }

    FloatingDamage floatingDamage;
    floatingDamage.position = { center.x, center.y - 66.0f };
    floatingDamage.amount = appliedDamage;
    floatingDamages_.push_back(floatingDamage);
    return true;
}

// 点攻击可命中第一个符合条件的存活怪物，适用于普通飞行球棒。
bool Scene::TryHitMonster(Vec2 attackPoint, int damage)
{
    bool hitAny = false;
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (!monster->IsAlive())
        {
            continue;
        }
        const Vec2 center = monster->GetCenter();
        const float dx = attackPoint.x - center.x;
        const float dy = attackPoint.y - center.y;
        const float hitRadius = monster->GetCollisionRadius() + CombatTuning::MeleeContactTolerance;
        if (dx * dx + dy * dy > hitRadius * hitRadius)
        {
            continue;
        }

        ApplyDamageToMonster(*monster, damage);
        hitAny = true;
    }
    return hitAny;
}

// 普攻采用“前向投影 + 横向距离”的扇形/走廊判定，并可同时击中多个目标。
bool Scene::TryHitBasicAttack(
    Vec2 origin,
    Vec2 direction,
    float reach,
    int damage,
    RECT clientRect)
{
    bool hitAny = false;
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (!monster->IsAlive())
        {
            continue;
        }

        const Vec2 center = monster->GetCenter();
        if (!CircleIntersectsForwardSector(
            origin,
            direction,
            reach,
            CombatTuning::BasicAttackArcHalfAngleDegrees,
            center,
            monster->GetCollisionRadius()))
        {
            continue;
        }

        ApplyDamageToMonster(*monster, damage);
        hitAny = true;
    }

    if (IsDummyAlive())
    {
        const Vec2 center = GetDummyCenter(clientRect);
        const float dummyRadius = GetDummyCollisionRadius();
        if (CircleIntersectsForwardSector(
            origin,
            direction,
            reach,
            CombatTuning::BasicAttackArcHalfAngleDegrees,
            center,
            dummyRadius))
        {
            Vec2 impactDirection = Normalize({ center.x - origin.x, center.y - origin.y });
            if (impactDirection.x == 0.0f && impactDirection.y == 0.0f)
            {
                impactDirection = Normalize(direction);
            }
            const Vec2 impactPoint
            {
                center.x - impactDirection.x * (std::max)(0.0f, dummyRadius - 1.0f),
                center.y - impactDirection.y * (std::max)(0.0f, dummyRadius - 1.0f)
            };
            hitAny = TryHitDummy(impactPoint, clientRect, damage) || hitAny;
        }
    }

    return hitAny;
}

// 飞行球棒按自身半径扩张目标碰撞圆，避免高速移动时视觉接触却判定落空。
bool Scene::TryHitThrownBat(Vec2 batPosition, RECT clientRect, int damage)
{
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (!monster->IsAlive())
        {
            continue;
        }

        const Vec2 center = monster->GetCenter();
        const float dx = batPosition.x - center.x;
        const float dy = batPosition.y - center.y;
        const float hitRadius = monster->GetCollisionRadius() + CombatTuning::ThrownBatHitRadius;
        if (dx * dx + dy * dy <= hitRadius * hitRadius)
        {
            ApplyDamageToMonster(*monster, damage);
            return true;
        }
    }

    if (!IsDummyAlive())
    {
        return false;
    }

    const Vec2 center = GetDummyCenter(clientRect);
    const float dx = batPosition.x - center.x;
    const float dy = batPosition.y - center.y;
    const float hitRadius = GetDummyCollisionRadius() + CombatTuning::ThrownBatHitRadius;
    if (dx * dx + dy * dy > hitRadius * hitRadius)
    {
        return false;
    }

    Vec2 impactDirection = Normalize({ batPosition.x - center.x, batPosition.y - center.y });
    if (impactDirection.x == 0.0f && impactDirection.y == 0.0f)
    {
        impactDirection = { 1.0f, 0.0f };
    }
    const Vec2 impactPoint
    {
        center.x + impactDirection.x * (std::max)(0.0f, GetDummyCollisionRadius() - 1.0f),
        center.y + impactDirection.y * (std::max)(0.0f, GetDummyCollisionRadius() - 1.0f)
    };
    return TryHitDummy(impactPoint, clientRect, damage);
}

// 圆形范围伤害；fallbackDirection 用于目标恰好位于爆心时确定稳定击飞方向。
bool Scene::TryHitSuperBatExplosion(
    Vec2 center,
    Vec2 fallbackDirection,
    float radius,
    int damage,
    RECT clientRect)
{
    bool hitAny = false;
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (!monster->IsAlive())
        {
            continue;
        }

        const Vec2 monsterCenter = monster->GetCenter();
        const float dx = monsterCenter.x - center.x;
        const float dy = monsterCenter.y - center.y;
        const float hitRadius = radius + monster->GetCollisionRadius();
        if (dx * dx + dy * dy > hitRadius * hitRadius)
        {
            continue;
        }

        Vec2 launchDirection = Normalize({ dx, dy });
        if (launchDirection.x == 0.0f && launchDirection.y == 0.0f)
        {
            launchDirection = Normalize(fallbackDirection);
        }
        ApplyDamageToMonster(*monster, damage, true, launchDirection, 1.35f);
        hitAny = true;
    }

    if (IsDummyAlive())
    {
        const Vec2 dummyCenter = GetDummyCenter(clientRect);
        const float dx = dummyCenter.x - center.x;
        const float dy = dummyCenter.y - center.y;
        const float dummyRadius = GetDummyCollisionRadius();
        const float hitRadius = radius + dummyRadius;
        if (dx * dx + dy * dy <= hitRadius * hitRadius)
        {
            Vec2 impactDirection = Normalize({ dx, dy });
            if (impactDirection.x == 0.0f && impactDirection.y == 0.0f)
            {
                impactDirection = Normalize(fallbackDirection);
            }
            const Vec2 impactPoint
            {
                dummyCenter.x - impactDirection.x * (std::max)(0.0f, dummyRadius - 1.0f),
                dummyCenter.y - impactDirection.y * (std::max)(0.0f, dummyRadius - 1.0f)
            };
            hitAny = TryHitDummy(impactPoint, clientRect, damage) || hitAny;
        }
    }
    return hitAny;
}

// 终结技每一段都独立检查方向与半径，最后一段额外触发死亡击飞。
bool Scene::TryHitUltimate(
    Vec2 origin,
    Vec2 direction,
    float radius,
    int damage,
    bool finisher,
    RECT clientRect)
{
    bool hitAny = false;
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (!monster->IsAlive())
        {
            continue;
        }
        const Vec2 center = monster->GetCenter();
        if (CircleIntersectsForwardSector(
            origin,
            direction,
            radius,
            CombatTuning::UltimateArcHalfAngleDegrees,
            center,
            monster->GetCollisionRadius()))
        {
            Vec2 launchDirection = Normalize({ center.x - origin.x, center.y - origin.y });
            if (launchDirection.x == 0.0f && launchDirection.y == 0.0f)
            {
                launchDirection = Normalize(direction);
            }
            ApplyDamageToMonster(*monster, damage, true, launchDirection, finisher ? 1.35f : 1.0f);
            hitAny = true;
        }
    }

    if (IsDummyAlive())
    {
        const Vec2 center = GetDummyCenter(clientRect);
        const float dummyRadius = GetDummyCollisionRadius();
        if (CircleIntersectsForwardSector(
            origin,
            direction,
            radius,
            CombatTuning::UltimateArcHalfAngleDegrees,
            center,
            dummyRadius))
        {
            Vec2 impactDirection = Normalize({ center.x - origin.x, center.y - origin.y });
            if (impactDirection.x == 0.0f && impactDirection.y == 0.0f)
            {
                impactDirection = Normalize(direction);
            }
            const Vec2 impactPoint
            {
                center.x - impactDirection.x * (std::max)(0.0f, dummyRadius - 1.0f),
                center.y - impactDirection.y * (std::max)(0.0f, dummyRadius - 1.0f)
            };
            hitAny = TryHitDummy(impactPoint, clientRect, damage) || hitAny;
        }
    }

    return hitAny;
}

// 优先选择距离原攻击来源最近的存活怪物，使格挡反击命中正确的攻击者。
bool Scene::ApplyCounterAttack(Vec2 attackerPosition, int damage)
{
    Monster* nearest = nullptr;
    float nearestDistanceSquared = 0.0f;
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (!monster->IsAlive())
        {
            continue;
        }
        const Vec2 center = monster->GetCenter();
        const float dx = attackerPosition.x - center.x;
        const float dy = attackerPosition.y - center.y;
        const float distanceSquared = dx * dx + dy * dy;
        const float identityTolerance = monster->GetCollisionRadius() + 12.0f;
        if (distanceSquared <= identityTolerance * identityTolerance &&
            (nearest == nullptr || distanceSquared < nearestDistanceSquared))
        {
            nearest = monster.get();
            nearestDistanceSquared = distanceSquared;
        }
    }
    if (nearest == nullptr)
    {
        return false;
    }
    ApplyDamageToMonster(*nearest, damage);
    return true;
}

// 按容器顺序消费一只怪物的待处理命中；Game 通过 while 可在本帧继续取下一只。
bool Scene::ConsumeMonsterAttackHit(
    Vec2 playerPosition,
    float playerCollisionRadius,
    Vec2& sourcePosition,
    int& damage,
    HostileStatusEffect& statusEffect)
{
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (Boss* boss = dynamic_cast<Boss*>(monster.get()))
        {
            if (boss->ConsumeSpecialAttackHit(
                playerPosition,
                playerCollisionRadius,
                sourcePosition,
                damage,
                statusEffect))
            {
                return true;
            }
            continue;
        }
        if (monster->ConsumeAttackHit(playerPosition, sourcePosition, damage))
        {
            statusEffect = HostileStatusEffect::None;
            return true;
        }
    }
    return false;
}

bool Scene::ConsumeBossDefeated()
{
    const bool defeated = bossDefeatedReady_;
    bossDefeatedReady_ = false;
    return defeated;
}

BossVoiceCue Scene::ConsumeBossVoiceCue()
{
    Boss* boss = FindBoss();
    return boss != nullptr ? boss->ConsumeVoiceCue() : BossVoiceCue::None;
}

Vec2 Scene::GetDummyCenter(RECT clientRect) const
{
    (void)clientRect;
    return dummyCenter_;
}

float Scene::GetDummyCollisionRadius() const
{
    return IsDummyAlive() ? 38.0f : 0.0f;
}

// 先把玩家推出每个敌人的碰撞圆；怪物之间的成对分离在 Update 中完成。
void Scene::ResolveMonsterCollisions(Player& player) const
{
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (monster->IsAlive())
        {
            player.ResolveCircleCollision(monster->GetCenter(), monster->GetCollisionRadius());
        }
    }
}

int Scene::ConsumePendingHealing()
{
    const int healing = pendingHealing_;
    pendingHealing_ = 0;
    return healing;
}

int Scene::ConsumeMonsterKillCount()
{
    const int kills = pendingMonsterKills_;
    pendingMonsterKills_ = 0;
    return kills;
}

// 所有怪物受伤路径汇合于此，确保击杀数、掉落、浮字和死亡表现不会遗漏。
void Scene::ApplyDamageToMonster(
    Monster& monster,
    int damage,
    bool launchOnKill,
    Vec2 launchDirection,
    float launchStrength)
{
    const Vec2 center = monster.GetCenter();
    const int requestedDamage = damage > 0 ? damage : 0;
    const bool wasAlive = monster.IsAlive();
    const int appliedDamage = monster.TakeDamage(requestedDamage);

    if (wasAlive && !monster.IsAlive())
    {
        if (launchOnKill)
        {
            monster.LaunchDefeated(launchDirection, launchStrength);
        }
        pendingMonsterKills_ += monster.GetKillCredit();
        if (appliedDamage > 0 && RollHeartDrop())
        {
            HeartPickup heart;
            heart.position = center;
            heartPickups_.push_back(heart);
        }
    }

    FloatingDamage floatingDamage;
    floatingDamage.position = { center.x, center.y - 62.0f };
    floatingDamage.amount = appliedDamage;
    floatingDamages_.push_back(floatingDamage);
}

// xorshift32：状态小、无需全局随机引擎，且固定种子便于复现调试。
bool Scene::RollHeartDrop()
{
    heartRandomState_ = heartRandomState_ * 1664525u + 1013904223u;
    return static_cast<int>((heartRandomState_ >> 16) % 100u) < kHeartDropPercent;
}

// 按背景、场景物、敌人、特效、HUD 辅助信息的层级顺序绘制。
void Scene::Draw(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;

    Gdiplus::SolidBrush backgroundBrush(Gdiplus::Color(255, 244, 241, 232));
    Gdiplus::Pen gridPen(Gdiplus::Color(255, 221, 216, 202), 1.0f);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 59, 58, 54));
    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
    Gdiplus::Font font(&fontFamily, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    graphics.FillRectangle(&backgroundBrush, 0, 0, width, height);

    for (int x = 0; x < width; x += 48)
    {
        graphics.DrawLine(&gridPen, x, 0, x, height);
    }
    for (int y = 0; y < height; y += 48)
    {
        graphics.DrawLine(&gridPen, 0, y, width, y);
    }

    if (dummyEnabled_)
    {
    const Vec2 dummyCenter = GetDummyCenter(clientRect);
    float shakeX = 0.0f;
    const bool dummyDefeated = dummyHealth_ <= 0;
    if (dummyHitEffect_.IsActive() && !dummyDefeated)
    {
        shakeX = dummyHitEffect_.GetShakeX();
    }
    const float impactProgress = dummyImpactTimer_ > 0.0f
        ? 1.0f - dummyImpactTimer_ / kDummyImpactDuration
        : 1.0f;
    float impactEnvelope = 0.0f;
    float rebound = 0.0f;
    if (dummyImpactTimer_ > 0.0f)
    {
        if (impactProgress < kDummyImpactCompressionEnd)
        {
            impactEnvelope = EaseOutCubic(impactProgress / kDummyImpactCompressionEnd);
        }
        else
        {
            const float recoveryProgress =
                (impactProgress - kDummyImpactCompressionEnd) / (1.0f - kDummyImpactCompressionEnd);
            impactEnvelope = SmoothRecovery(recoveryProgress);
            rebound = std::sin(recoveryProgress * 3.1415926f) * (1.0f - recoveryProgress);
        }
    }

    const float dominantImpactAxis = std::abs(dummyImpactDirection_.x) >= std::abs(dummyImpactDirection_.y)
        ? dummyImpactDirection_.x
        : dummyImpactDirection_.y;
    const float impactTiltSign = dominantImpactAxis < 0.0f ? -1.0f : 1.0f;
    const float impactStrength = (std::max)(
        std::abs(dummyImpactDirection_.x),
        std::abs(dummyImpactDirection_.y));
    const float tiltDegrees = impactTiltSign *
        (4.0f + 3.0f * impactStrength) *
        (impactEnvelope - rebound * 0.16f);
    const float recoilDistance = 12.0f * impactEnvelope - 2.5f * rebound;
    const float recoilX = dummyImpactDirection_.x * recoilDistance;
    const float recoilY = dummyImpactDirection_.y * recoilDistance * 0.55f;
    const float scaleX = 1.0f + impactEnvelope * 0.08f - rebound * 0.02f;
    const float scaleY = 1.0f - impactEnvelope * 0.12f + rebound * 0.04f;
    const float drawCenterX = dummyCenter.x + shakeX;
    const float drawBottomY = dummyCenter.y + 48.0f;
    const bool impactFlash = dummyImpactTimer_ > kDummyImpactDuration - 0.075f;
    const bool flash = !dummyDefeated && (dummyHitEffect_.IsFlashVisible() || impactFlash);

    const Gdiplus::GraphicsState dummyState = graphics.Save();
    graphics.TranslateTransform(drawCenterX + recoilX, drawBottomY + recoilY);
    graphics.RotateTransform(tiltDegrees);
    graphics.ScaleTransform(scaleX, scaleY);
    graphics.TranslateTransform(-drawCenterX, -drawBottomY);
    DrawTrainingDummy(graphics, drawCenterX, drawBottomY, flash, dummyDefeated);
    graphics.Restore(dummyState);
    DrawDummyOverheadHealthBar(graphics, dummyCenter);
    }
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        monster->Draw(graphics);
    }
    DrawHeartPickups(graphics);

    if (dummyEnabled_ && dummyHitEffect_.IsActive())
    {
        dummyHitEffect_.Draw(graphics);
    }

    DrawFloatingDamage(graphics);
    DrawMonsterStatus(graphics, clientRect);

    const wchar_t* modeHint = hardChallengeMode_
        ? L"\u56f0\u96be\u6311\u6218    WASD \u79fb\u52a8    J \u653b\u51fb    K \u683c\u6321/\u53cd\u51fb    Q \u6295\u63b7\u7403\u68d2    P \u7ec8\u7ed3\u6280    ESC \u6682\u505c"
        : (challengeMode_
            ? L"\u666e\u901a\u95ef\u5173    WASD \u79fb\u52a8    J \u653b\u51fb    K \u683c\u6321/\u53cd\u51fb    Q \u6295\u63b7\u7403\u68d2    P \u7ec8\u7ed3\u6280    ESC \u6682\u505c"
            : L"\u8bad\u7ec3\u6a21\u5f0f    WASD \u79fb\u52a8    J \u653b\u51fb    K \u683c\u6321/\u53cd\u51fb    Q \u6295\u63b7\u7403\u68d2    P \u7ec8\u7ed3\u6280    ESC \u6682\u505c");
    graphics.DrawString(modeHint, -1, &font, Gdiplus::PointF(16.0f, 128.0f), &textBrush);
}

void Scene::DrawMonsterStatus(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    if (challengeMode_)
    {
        // Boss 自己绘制完整血条；Scene 只补充本关目标，避免与 Boss HUD 重叠。
        Gdiplus::SolidBrush objectivePanel(Gdiplus::Color(205, 42, 24, 39));
        Gdiplus::SolidBrush objectiveText(Gdiplus::Color(255, 255, 234, 244));
        Gdiplus::FontFamily objectiveFamily(L"Microsoft YaHei");
        Gdiplus::Font objectiveFont(&objectiveFamily, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        graphics.FillRectangle(&objectivePanel, 16.0f, 154.0f, 210.0f, 34.0f);
        graphics.DrawString(hardChallengeMode_
            ? L"\u56f0\u96be\u76ee\u6807\uff1a\u51fb\u8d25\u5f3a\u5316\u5361\u8299\u5361"
            : L"\u7b2c\u4e00\u5173\u76ee\u6807\uff1a\u51fb\u8d25\u5361\u8299\u5361", -1, &objectiveFont,
            Gdiplus::PointF(28.0f, 161.0f), &objectiveText);
        return;
    }

    const float width = static_cast<float>(clientRect.right - clientRect.left);
    const float panelWidth = 430.0f;
    const float x = (width - panelWidth) * 0.5f;
    const float y = 18.0f;

    Gdiplus::SolidBrush shadow(Gdiplus::Color(70, 26, 20, 16));
    Gdiplus::SolidBrush panel(Gdiplus::Color(222, 250, 244, 224));
    Gdiplus::Pen edge(Gdiplus::Color(210, 70, 52, 38), 2.0f);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 55, 42, 32));
    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&fontFamily, 15.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font hpFont(&fontFamily, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    graphics.FillRectangle(&shadow, x + 4.0f, y + 5.0f, panelWidth, 68.0f);
    graphics.FillRectangle(&panel, x, y, panelWidth, 68.0f);
    graphics.DrawRectangle(&edge, x, y, panelWidth, 68.0f);

    DrawDummyPortrait(graphics, x + 42.0f, y + 34.0f);

    graphics.DrawString(L"\u8bad\u7ec3\u7a3b\u8349\u4eba", -1, &titleFont, Gdiplus::PointF(x + 84.0f, y + 10.0f), &textBrush);

    wchar_t hpText[64]{};
    wsprintfW(hpText, L"HP %d / %d", dummyHealth_, dummyMaxHealth_);
    graphics.DrawString(hpText, -1, &hpFont, Gdiplus::PointF(x + 326.0f, y + 13.0f), &textBrush);

    if (IsDummyAlive())
    {
        dummyHealthBar_.Draw(graphics, x + 84.0f, y + 38.0f, 312.0f, 14.0f);
    }
    else
    {
        wchar_t respawnText[64]{};
        const int remainingSeconds = static_cast<int>(std::ceil(dummyRespawnTimer_));
        wsprintfW(respawnText, L"\u5df2\u51fb\u7834 - %d \u79d2\u540e\u91cd\u7f6e", remainingSeconds > 0 ? remainingSeconds : 0);
        graphics.DrawString(respawnText, -1, &hpFont, Gdiplus::PointF(x + 84.0f, y + 38.0f), &textBrush);
    }
}

void Scene::DrawDummyOverheadHealthBar(Gdiplus::Graphics& graphics, Vec2 dummyCenter) const
{
    // 头顶跟随血条只在活着时绘制；死亡后等同于隐藏/销毁 UI。
    if (!IsDummyAlive())
    {
        return;
    }

    dummyHealthBar_.Draw(graphics, dummyCenter.x - 44.0f, dummyCenter.y - 74.0f, 88.0f, 8.0f);
}

void Scene::DrawFloatingDamage(Gdiplus::Graphics& graphics) const
{
    Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
    Gdiplus::Font font(&fontFamily, 20.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);

    for (const FloatingDamage& damage : floatingDamages_)
    {
        const float progress = damage.lifetime / damage.duration;
        const BYTE alpha = static_cast<BYTE>(Clamp((1.0f - progress) * 255.0f, 0.0f, 255.0f));
        const float pop = progress < 0.18f ? 1.0f + (0.18f - progress) * 1.6f : 1.0f;

        wchar_t text[16]{};
        wsprintfW(text, damage.healing ? L"+%d" : L"-%d", damage.amount);

        Gdiplus::SolidBrush outline(damage.healing
            ? Gdiplus::Color(alpha, 18, 86, 42)
            : Gdiplus::Color(alpha, 72, 28, 18));
        Gdiplus::SolidBrush fill(damage.healing
            ? Gdiplus::Color(alpha, 112, 255, 142)
            : Gdiplus::Color(alpha, 255, 238, 100));
        Gdiplus::RectF rect(damage.position.x - 50.0f, damage.position.y - 14.0f, 100.0f, 28.0f * pop);

        graphics.DrawString(text, -1, &font, Gdiplus::RectF(rect.X - 2.0f, rect.Y, rect.Width, rect.Height), &format, &outline);
        graphics.DrawString(text, -1, &font, Gdiplus::RectF(rect.X + 2.0f, rect.Y, rect.Width, rect.Height), &format, &outline);
        graphics.DrawString(text, -1, &font, Gdiplus::RectF(rect.X, rect.Y - 2.0f, rect.Width, rect.Height), &format, &outline);
        graphics.DrawString(text, -1, &font, Gdiplus::RectF(rect.X, rect.Y + 2.0f, rect.Width, rect.Height), &format, &outline);
        graphics.DrawString(text, -1, &font, rect, &format, &fill);
    }
}

void Scene::DrawHeartPickups(Gdiplus::Graphics& graphics) const
{
    for (const HeartPickup& heart : heartPickups_)
    {
        const float bob = std::sin(heart.bobTimer * 4.2f) * 3.0f;
        const float x = heart.position.x;
        const float y = heart.position.y - 22.0f + bob;
        Gdiplus::SolidBrush shadow(Gdiplus::Color(70, 42, 24, 24));
        Gdiplus::SolidBrush outline(Gdiplus::Color(255, 82, 20, 30));
        Gdiplus::SolidBrush red(Gdiplus::Color(255, 226, 38, 57));
        Gdiplus::SolidBrush light(Gdiplus::Color(255, 255, 126, 136));
        Gdiplus::Pen glow(Gdiplus::Color(110, 255, 82, 96), 3.0f);

        graphics.FillEllipse(&shadow, x - 15.0f, y + 17.0f, 30.0f, 8.0f);
        graphics.DrawEllipse(&glow, x - 20.0f, y - 20.0f, 40.0f, 40.0f);
        graphics.FillRectangle(&outline, x - 14.0f, y - 8.0f, 28.0f, 17.0f);
        graphics.FillRectangle(&outline, x - 10.0f, y + 8.0f, 20.0f, 7.0f);
        graphics.FillRectangle(&outline, x - 5.0f, y + 15.0f, 10.0f, 5.0f);
        graphics.FillEllipse(&outline, x - 15.0f, y - 14.0f, 16.0f, 16.0f);
        graphics.FillEllipse(&outline, x - 1.0f, y - 14.0f, 16.0f, 16.0f);
        graphics.FillEllipse(&red, x - 12.0f, y - 11.0f, 13.0f, 13.0f);
        graphics.FillEllipse(&red, x - 1.0f, y - 11.0f, 13.0f, 13.0f);
        graphics.FillRectangle(&red, x - 11.0f, y - 4.0f, 22.0f, 12.0f);
        graphics.FillRectangle(&red, x - 7.0f, y + 8.0f, 14.0f, 6.0f);
        graphics.FillRectangle(&red, x - 3.0f, y + 14.0f, 6.0f, 4.0f);
        graphics.FillRectangle(&light, x - 8.0f, y - 8.0f, 5.0f, 5.0f);
    }
}

// monsterSpawnIndex_ 轮换四条屏幕边，避免连续生成点集中在同一方向。
Vec2 Scene::CalculateOffscreenMonsterSpawn(RECT clientRect) const
{
    const float width = static_cast<float>(clientRect.right - clientRect.left);
    const float height = static_cast<float>(clientRect.bottom - clientRect.top);
    constexpr float kSpawnMargin = 96.0f;

    const int side = monsterSpawnIndex_ % 4;
    switch (side)
    {
    case 0:
        return { -kSpawnMargin, height * 0.35f };
    case 1:
        return { width + kSpawnMargin, height * 0.65f };
    case 2:
        return { width * 0.45f, -kSpawnMargin };
    case 3:
    default:
        return { width * 0.55f, height + kSpawnMargin };
    }
}

Boss* Scene::FindBoss()
{
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (Boss* boss = dynamic_cast<Boss*>(monster.get()))
        {
            return boss;
        }
    }
    return nullptr;
}

const Boss* Scene::FindBoss() const
{
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (const Boss* boss = dynamic_cast<const Boss*>(monster.get()))
        {
            return boss;
        }
    }
    return nullptr;
}

int Scene::CountActiveMinions() const
{
    int count = 0;
    for (const std::unique_ptr<Monster>& monster : monsters_)
    {
        if (dynamic_cast<const Boss*>(monster.get()) == nullptr && monster->IsAlive())
        {
            ++count;
        }
    }
    return count;
}

void Scene::SpawnMinionNearBoss(RECT clientRect, Vec2 bossPosition)
{
    if (CountActiveMinions() >= 3)
    {
        return;
    }

    static constexpr Vec2 kOffsets[] = {
        { -118.0f, -54.0f },
        { 118.0f, -54.0f },
        { 0.0f, 118.0f },
        { -142.0f, 42.0f },
        { 142.0f, 42.0f },
        { -82.0f, 112.0f },
        { 82.0f, 112.0f },
        { 0.0f, -126.0f }
    };
    minionRandomState_ = minionRandomState_ * 1664525u + 1013904223u;
    const Vec2 offset = kOffsets[(minionRandomState_ >> 16) % (sizeof(kOffsets) / sizeof(kOffsets[0]))];
    const float width = static_cast<float>(clientRect.right - clientRect.left);
    const float height = static_cast<float>(clientRect.bottom - clientRect.top);
    const Vec2 position{
        Clamp(bossPosition.x + offset.x, 42.0f, width - 42.0f),
        Clamp(bossPosition.y + offset.y, 112.0f, height - 42.0f)
    };

    auto minion = std::make_unique<Monster>();
    minion->Initialize(clientRect);
    minion->SpawnAt(position);
    monsters_.push_back(std::move(minion));
    ++monsterSpawnIndex_;
}

void Scene::ResetDummy()
{
    dummyHealth_ = dummyMaxHealth_;
    dummyRespawnTimer_ = 0.0f;
    dummyImpactTimer_ = 0.0f;
    dummyImpactDirection_ = { 1.0f, 0.0f };
    dummyHitEffect_.Reset();
    dummyHealthBar_.ResetValue(dummyHealth_, dummyMaxHealth_);
}

bool Scene::IsDummyAlive() const
{
    return dummyEnabled_ && dummyHealth_ > 0;
}
