#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <cstdint>
#include <memory>
#include <vector>

#include "HealthBar.h"
#include "HitEffect.h"
#include "MathTypes.h"
#include "Monster.h"
#include "CombatEffects.h"

class Player;
class Boss;

// 训练场景控制器：拥有训练假人、动态怪物、掉落物、伤害数字和场景绘制。
// 它也是战斗判定的集中入口，将 Player 发出的几何攻击事件应用到具体目标。
class Scene
{
public:
    Scene();
    void Initialize(RECT clientRect);
    void UnloadContent();
    void ResetTrainingMode(RECT clientRect);
    // hardMode=false 创建普通卡芙卡，true 创建强化后的困难卡芙卡。
    void ResetChallengeMode(RECT clientRect, bool hardMode);
    void SpawnMonster(RECT clientRect);
    void ResizeToClient(RECT oldClientRect, RECT newClientRect);
    // 推进怪物 AI、复活、掉落物和浮字；只读 Player 同时提供拾取与 Boss AI 观察。
    void Update(float deltaSeconds, const Player& player, RECT clientRect);
    void ResolveMonsterCollisions(Player& player) const;
    int ConsumePendingHealing();
    int ConsumeMonsterKillCount();
    // 点/球类攻击判定。
    bool TryHitDummy(Vec2 attackPoint, RECT clientRect, int damage);
    bool TryHitMonster(Vec2 attackPoint, int damage);
    // 方向性近战、圆形爆炸和多段终结技判定；返回是否至少命中一个目标。
    bool TryHitBasicAttack(
        Vec2 origin,
        Vec2 direction,
        float reach,
        int damage,
        RECT clientRect);
    bool TryHitThrownBat(Vec2 batPosition, RECT clientRect, int damage);
    bool TryHitSuperBatExplosion(
        Vec2 center,
        Vec2 fallbackDirection,
        float radius,
        int damage,
        RECT clientRect);
    bool TryHitUltimate(
        Vec2 origin,
        Vec2 direction,
        float radius,
        int damage,
        bool finisher,
        RECT clientRect);
    // 从攻击来源附近选择可反击目标并施加伤害。
    bool ApplyCounterAttack(Vec2 attackerPosition, int damage);
    bool ConsumeMonsterAttackHit(
        Vec2 playerPosition,
        float playerCollisionRadius,
        Vec2& sourcePosition,
        int& damage,
        HostileStatusEffect& statusEffect);
    bool ConsumeBossDefeated();
    // 转发 Boss 在生命跨过 2/3、1/3 时提交的一次性语音事件。
    BossVoiceCue ConsumeBossVoiceCue();
    void Draw(Gdiplus::Graphics& graphics, RECT clientRect) const;
    Vec2 GetDummyCenter(RECT clientRect) const;
    float GetDummyCollisionRadius() const;

private:
    // 伤害/治疗浮字是纯视觉对象，lifetime 到期后从 vector 中移除。
    struct FloatingDamage
    {
        Vec2 position{};
        int amount = 0;
        bool healing = false;
        float lifetime = 0.0f;
        float duration = 0.85f;
    };

    // 怪物概率掉落的治疗物；bobTimer 只控制上下漂浮动画。
    struct HeartPickup
    {
        Vec2 position{};
        float lifetime = 0.0f;
        float bobTimer = 0.0f;
    };

    void DrawMonsterStatus(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawDummyOverheadHealthBar(Gdiplus::Graphics& graphics, Vec2 dummyCenter) const;
    void DrawFloatingDamage(Gdiplus::Graphics& graphics) const;
    void DrawHeartPickups(Gdiplus::Graphics& graphics) const;
    // 统一处理扣血、命中特效、死亡计数、爱心掉落和可选击飞。
    void ApplyDamageToMonster(
        Monster& monster,
        int damage,
        bool launchOnKill = false,
        Vec2 launchDirection = Vec2{ 1.0f, 0.0f },
        float launchStrength = 1.0f);
    bool RollHeartDrop();
    Vec2 CalculateOffscreenMonsterSpawn(RECT clientRect) const;
    Boss* FindBoss();
    const Boss* FindBoss() const;
    int CountActiveMinions() const;
    void SpawnMinionNearBoss(RECT clientRect, Vec2 bossPosition);
    void ResetMode(RECT clientRect, bool challengeMode, bool hardMode);
    void ResetDummy();
    bool IsDummyAlive() const;

    // 训练假人使用固定场景位置，并在死亡延时后自动恢复。
    float dummyRespawnTimer_ = 0.0f;
    float dummyImpactTimer_ = 0.0f;
    Vec2 dummyCenter_{};
    Vec2 dummyImpactDirection_{ 1.0f, 0.0f };
    HitEffect dummyHitEffect_;
    int dummyHealth_ = 100;
    int dummyMaxHealth_ = 100;
    HealthBar dummyHealthBar_;
    // unique_ptr 保证 Monster 不会因 vector 扩容而被复制，同时明确 Scene 的所有权。
    std::vector<std::unique_ptr<Monster>> monsters_;
    std::vector<HeartPickup> heartPickups_;
    std::vector<FloatingDamage> floatingDamages_;
    int monsterSpawnIndex_ = 0;
    // Update 累加，Game 在同一帧消费并应用给玩家。
    int pendingHealing_ = 0;
    int pendingMonsterKills_ = 0;
    bool dummyEnabled_ = false;
    bool challengeMode_ = false;
    bool hardChallengeMode_ = false; // 控制困难 HUD 文案，并决定新建 Boss 的强化配置。
    bool bossDefeatedReady_ = false;
    // 独立、可复现的轻量随机状态，只用于爱心掉落判定。
    std::uint32_t heartRandomState_ = 0x6d2b79f5u;
    // 独立于掉落物随机数，保证召唤位置变化不会改变爱心掉落序列。
    std::uint32_t minionRandomState_ = 0xa341316cu;
};
