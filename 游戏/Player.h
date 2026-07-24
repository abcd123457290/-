#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <cstdint>
#include <memory>

#include "HitEffect.h"
#include "CombatEffects.h"
#include "MathTypes.h"
#include "SpriteAnimation.h"

// 玩家动作产生的事件数据。Player 负责动作时间轴，Scene 负责判断命中了谁；
// 这种拆分使玩家逻辑不需要持有任何 Monster 或训练假人的引用。
struct UltimateHitEvent
{
    Vec2 origin{};
    Vec2 direction{ 1.0f, 0.0f };
    float radius = 0.0f;
    int damage = 0;
    int comboIndex = 0;
    bool finisher = false;
};

struct BasicAttackHitEvent
{
    Vec2 origin{};
    Vec2 direction{ 1.0f, 0.0f };
    float reach = 0.0f;
    int damage = 0;
};

struct SuperBatExplosionEvent
{
    Vec2 center{};
    Vec2 direction{ 1.0f, 0.0f };
    float radius = 0.0f;
    int damage = 0;
};

// 玩家角色控制器：处理移动、格挡、普攻、投掷、终结技、生命/能量以及绘制。
class Player
{
public:
    // 加载/释放图片资源；ResetTrainingMode 只重置一局训练的运行状态。
    void LoadContent();
    void UnloadContent();
    void ResetTrainingMode(RECT clientRect);
    // 采集连续键盘状态并推进所有动作状态机。一次性按键由 Request* 注入。
    void Update(float deltaSeconds, RECT clientRect);
    void ResizeToClient(RECT oldClientRect, RECT newClientRect);
    void ResolveCircleCollision(Vec2 obstacleCenter, float obstacleRadius);
    void Draw(Gdiplus::Graphics& graphics) const;
    void DrawHud(Gdiplus::Graphics& graphics, RECT clientRect) const;
    // 应用伤害。处于有效格挡窗口时不扣血并触发反击反馈，返回是否成功格挡。
    bool TakeDamage(
        int damage,
        Vec2 sourcePosition,
        HostileStatusEffect statusEffect = HostileStatusEffect::None);
    void Heal(int amount);
    // 请求类接口由 Game 的按键沿检测调用，实际能否发动在下一次 Update 中判定。
    void RequestThrow();
    void RequestUltimate();
    void RequestBlock();
    void RegisterMonsterKills(int count);
    // “Consume”接口读取并清除一次性事件，调用方每帧可安全轮询。
    bool ConsumeUltimateActivationStarted();
    bool ConsumeUltimateHit(UltimateHitEvent& event);
    bool ConsumeAttackHit(BasicAttackHitEvent& event);
    bool ConsumeSuperBatExplosion(SuperBatExplosionEvent& event);
    // 飞行球棒存在时输出当前碰撞中心；ResolveThrownBatHit 通知玩家结束飞行/引爆。
    bool GetThrownBatHitPoint(Vec2& hitPosition) const;
    void ResolveThrownBatHit();
    bool IsSuperBatProjectile() const;
    int GetAttackDamage() const;
    int GetThrowDamage() const;
    int GetCounterDamage() const;
    int GetHealth() const;
    int GetMaxHealth() const;
    float GetMana() const;
    float GetMaxMana() const;
    Vec2 GetPosition() const;
    // 返回角色受击圆半径；Boss 范围攻击据此判断角色图形与预警区是否相交。
    float GetCollisionRadius() const;
    bool HasBat() const;
    bool IsUltimateUnlocked() const;
    int GetUltimateKillCount() const;
    // 以下三个只读状态供 Boss AI 构造决策观察，不暴露内部计时器的具体秒数。
    bool IsDirectionConfused() const;
    bool HasLightningMark() const;
    bool IsMovementLocked() const;
    // 死亡动作播放完毕后，Game 才显示失败界面。
    bool IsDeathAnimationFinished() const;

private:
    // 球棒与玩家分离后的完整生命周期。
    enum class BatWorldState
    {
        Held,
        Flying,
        Dropping,
        Grounded
    };

    // 输入、边界和各动作子状态机。
    void UpdateDirection(Vec2 input);
    void ClampToClient(RECT clientRect);
    void StartUltimate();
    void UpdateUltimate(float deltaSeconds);
    void BeginBasicAttack();
    void EndEmpoweredAttack();
    void StartDeath(Vec2 sourcePosition);
    void UpdateThrownBat(float deltaSeconds, RECT clientRect);
    void ReleaseBat();
    void PickUpBat();
    void TriggerSuperBatExplosion(Vec2 position);
    // 世界物体、技能 UI 和素材缺失时占位图的绘制函数。
    void DrawBat(Gdiplus::Graphics& graphics) const;
    void DrawSuperBatExplosion(Gdiplus::Graphics& graphics) const;
    void DrawUltimateSkillIcon(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawCounterEffect(Gdiplus::Graphics& graphics) const;
    void DrawStatusEffects(Gdiplus::Graphics& graphics) const;
    void DrawFallback(Gdiplus::Graphics& graphics) const;

    // 基础运动/动作状态。position_ 是角色脚底附近的世界坐标。
    Vec2 position_{ 480.0f, 320.0f };
    float collisionRadius_ = 22.0f;
    float speed_ = 260.0f;
    bool isMoving_ = false;
    bool isCrouching_ = false;
    bool isBlocking_ = false;
    bool isAttacking_ = false;
    bool isThrowing_ = false;
    bool isUltimate_ = false;
    bool deathStarted_ = false;
    // WasDown 用于本地按键沿检测；Buffered 允许在动作尾段预输入下一次普攻。
    bool attackWasDown_ = false;
    bool attackBuffered_ = false;
    bool blockWasDown_ = false;
    // Request 标志只存活到下一次 Update，避免窗口消息直接改变复杂状态机。
    bool blockRequested_ = false;
    bool throwRequested_ = false;
    bool ultimateRequested_ = false;
    bool ultimateActivationStarted_ = false;
    bool attackHitPending_ = false;
    int health_ = 100;
    int maxHealth_ = 100;
    float mana_ = 100.0f;
    float maxMana_ = 100.0f;
    bool hasBat_ = true;
    Vec2 velocity_{};
    // 格挡包含有效窗、冷却、输入缓存和受击宽限四套独立计时器。
    float blockActiveTimer_ = 0.0f;
    float blockCooldownTimer_ = 0.0f;
    float blockInputBufferTimer_ = 0.0f;
    float blockGraceTimer_ = 0.0f;
    float counterEffectTimer_ = 0.0f;
    Vec2 counterEffectTarget_{};
    // 卡芙卡施加的控制状态：方向混乱、紫色闪电标记和移动锁定。
    float directionConfusionTimer_ = 0.0f;
    bool invertHorizontalInput_ = false;
    bool lightningMarked_ = false;
    float movementLockTimer_ = 0.0f;
    std::uint32_t statusRandomState_ = 0x9e3779b9u;
    // 终结技及其解锁/强化连段状态。
    float ultimateElapsed_ = 0.0f;
    float ultimateCooldownTimer_ = 0.0f;
    float ultimateInputBufferTimer_ = 0.0f;
    int ultimateKillCount_ = 0;
    bool ultimateBuffActive_ = false;
    int ultimateEmpoweredAttackCount_ = 0;
    int activeEmpoweredAttackStage_ = -1;
    float empoweredAttackElapsed_ = 0.0f;
    // 五个 bit 各代表一段尚未被 Scene 消费的终结技伤害事件。
    std::uint32_t pendingUltimateHitMask_ = 0;
    CharacterDirection ultimateAimDirection_ = CharacterDirection::Right;
    CharacterDirection direction_ = CharacterDirection::Right;
    CharacterDirection aimDirection_ = CharacterDirection::Right;
    // 球棒世界状态；travelDistance 用于限制射程，rotation 仅影响视觉旋转。
    BatWorldState batState_ = BatWorldState::Held;
    Vec2 batPosition_{};
    Vec2 batDirection_{};
    float batTravelDistance_ = 0.0f;
    float batRotationDegrees_ = 0.0f;
    float batDropTimer_ = 0.0f;
    // 强化投掷和爆炸的事件/视觉状态相互分离，保证伤害只结算一次而动画可持续。
    bool superBatThrowActive_ = false;
    bool superBatProjectileActive_ = false;
    bool superBatExplosionPending_ = false;
    bool superBatExplosionActive_ = false;
    float superBatExplosionElapsed_ = 0.0f;
    float superBatVisualTimer_ = 0.0f;
    Vec2 superBatExplosionPosition_{};
    Vec2 superBatExplosionDirection_{ 1.0f, 0.0f };
    std::unique_ptr<Gdiplus::Image> ultimateIcon_;
    SpriteAnimation sprites_;
    HitEffect hitEffect_;
};
