#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <memory>

#include "HealthBar.h"
#include "MathTypes.h"

// 单个敌人的移动、攻击、受伤、死亡动画和精灵资源。
// Monster 不直接修改玩家生命；攻击命中先写入 pending 事件，再由 Scene/Game
// 在同一帧中统一消费，避免对象之间形成双向所有权。
class Monster
{
public:
    virtual ~Monster();
    Monster() = default;
    Monster(const Monster&) = delete;
    Monster& operator=(const Monster&) = delete;

    // 资源/状态生命周期。Initialize 首次加载并放置怪物，Reset 复用已加载资源。
    virtual void Initialize(RECT clientRect);
    virtual void UnloadContent();
    virtual void ResizeToClient(RECT oldClientRect, RECT newClientRect);
    // 根据玩家位置推进追踪 AI、攻击状态机和各类动画计时。
    virtual void Update(float deltaSeconds, Vec2 targetPosition);
    virtual void Draw(Gdiplus::Graphics& graphics) const;
    virtual void Reset();
    void Deactivate();
    void SpawnAt(Vec2 spawnPosition);
    // 圆形分离约束；每对怪物只需调用一次。
    void ResolveCollisionWith(Monster& other);
    virtual int TakeDamage(int damage);
    void LaunchDefeated(Vec2 direction, float strength = 1.0f);
    // 仅当攻击命中帧到达且目标位于攻击走廊时返回一次伤害事件。
    virtual bool ConsumeAttackHit(Vec2 targetPosition, Vec2& sourcePosition, int& damage);

    virtual Vec2 GetCenter() const;
    virtual float GetCollisionRadius() const;
    int GetAttack() const;
    virtual bool IsAlive() const;
    virtual bool IsDeathAnimationFinished() const;
    // 普通怪物计为 1 个击杀，Boss 可覆盖为更高奖励。
    virtual int GetKillCredit() const;

protected:
    // 派生敌人通过战斗配置改变数值，而无需复制普通怪物的 AI 状态机。
    void ConfigureCombatProfile(
        int maximumHealth,
        int attack,
        float collisionRadius,
        float moveSpeedMultiplier,
        float attackRangeMultiplier,
        float attackCooldownMultiplier);

protected:
    // Facing 选择精灵朝向；Motion 是简化的敌人行为/动画状态机。
    enum class Facing
    {
        Down,
        Up,
        Left,
        Right
    };

    enum class Motion
    {
        Idle,
        Walk,
        Windup,
        Attack
    };

    // 一张水平帧序列或“方向行 × 水平帧”的精灵表。
    struct AnimationSheet
    {
        std::unique_ptr<Gdiplus::Bitmap> image;
        int frameCount = 1;
        bool hasDirectionalRows = false;
        bool useAttackV2Layout = false;
    };

    // 精灵加载和回退。缺少外部素材时 DrawFallback 仍能绘制可玩的占位敌人。
    void LoadSprite();
    void LoadSheet(AnimationSheet& sheet, const wchar_t* relativePath, int frameCount);
    void LoadSheetWithFallback(
        AnimationSheet& sheet,
        const wchar_t* preferredRelativePath,
        int preferredFrameCount,
        const wchar_t* fallbackRelativePath,
        int fallbackFrameCount);
    // 攻击走廊由角色朝向轴上的前向距离和垂直于该轴的横向距离共同判定。
    Vec2 GetAttackOrigin() const;
    bool IsTargetInsideAttackCorridor(
        Vec2 targetPosition,
        float reachPadding = 0.0f,
        float lateralPadding = 0.0f) const;
    // 选择当前动作对应的图集，同时输出帧号和是否需要水平镜像。
    const AnimationSheet* GetCurrentSheet(int& frameIndex, bool& mirror) const;
    void DrawSheetFrame(
        Gdiplus::Graphics& graphics,
        const AnimationSheet& sheet,
        int frameIndex,
        bool mirror,
        float left,
        float top,
        float opacity = 1.0f,
        bool blueTint = false) const;
    void DrawLaunchedDeath(Gdiplus::Graphics& graphics) const;
    void DrawHitEffect(Gdiplus::Graphics& graphics) const;
    void DrawAttackEffect(Gdiplus::Graphics& graphics) const;
    void DrawFallback(Gdiplus::Graphics& graphics) const;

protected:
    // 物理与通用动画状态（距离为像素，时间为秒）。派生 Boss 复用这些状态，
    // 但拥有自己的技能状态机与渲染资源。
    Vec2 center_{};
    float radius_ = 20.0f;
    float animationTimer_ = 0.0f;
    float attackTimer_ = 0.0f;
    float attackCooldown_ = 0.0f;
    float hitEffectTimer_ = 0.0f;
    float deathTimer_ = 0.0f;
    // 击飞死亡使用独立速度/旋转，不再执行普通追踪 AI。
    bool launchedDeath_ = false;
    Vec2 launchDirection_{ 1.0f, 0.0f };
    Vec2 launchVelocity_{};
    float launchSpinDegrees_ = 0.0f;
    float launchSpinVelocity_ = 0.0f;
    int health_ = 30;
    int maxHealth_ = 30;
    int attack_ = 5;
    float moveSpeedMultiplier_ = 1.0f;
    float attackRangeMultiplier_ = 1.0f;
    float attackCooldownMultiplier_ = 1.0f;
    bool active_ = false;
    // pending 表示命中帧已到达但尚未消费；triggered 防止一次动作重复产生事件。
    bool attackHitPending_ = false;
    bool attackHitTriggered_ = false;
    Facing facing_ = Facing::Down;
    // 水平图集的最后朝向，以及攻击开始瞬间锁定的朝向/方向。
    Facing horizontalFacing_ = Facing::Right;
    Facing attackFacing_ = Facing::Right;
    Vec2 attackDirection_{ 1.0f, 0.0f };
    Motion motion_ = Motion::Idle;
    HealthBar healthBar_;
    // idle 为四向图集；行走前/后/侧和攻击侧分别使用独立素材。
    AnimationSheet idleSheet_;
    AnimationSheet walkFrontSheet_;
    AnimationSheet walkBackSheet_;
    AnimationSheet walkSideSheet_;
    AnimationSheet attackSideSheet_;
};
