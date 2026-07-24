#pragma once

#include <array>
#include <memory>

#include "BossAiModel.h"
#include "CombatEffects.h"
#include "Monster.h"

// 普通与困难共用同一 Boss 类和动画状态机，只通过集中数值配置改变强度。
enum class BossDifficulty
{
    Normal,
    Hard
};

// 第一关 Boss：卡芙卡。
// 使用独立技能状态机和优化后的运行时素材；原始建模目录不会在游戏中加载。
class Boss final : public Monster
{
public:
    // 保存客户区、确定第一关出生点、加载 V2 动画资源，并重置本局 Boss 状态。
    void Initialize(RECT clientRect) override;
    // 主动释放所有 GDI+ 图片；Scene 退出或重建资源时调用。
    void UnloadContent() override;
    // 窗口尺寸变化时平移 Boss，并更新技能预警和 HUD 使用的客户区。
    void ResizeToClient(RECT oldClientRect, RECT newClientRect) override;
    // 推进移动 AI、技能状态机、召唤/暴怒计时、血条缓动和死亡动画。
    void Update(float deltaSeconds, Vec2 targetPosition) override;
    // 按“红色预警 -> 当前动作帧 -> Boss HUD”的顺序绘制本对象。
    void Draw(Gdiplus::Graphics& graphics) const override;
    // 在不重新加载图片的情况下恢复一局挑战所需的全部运行状态。
    void Reset() override;
    // 处理临时响指血条、小怪免伤和本体扣血；返回实际生效的伤害值。
    int TakeDamage(int damage) override;
    // Boss 不使用普通 Monster 的近战命中事件，保留覆盖仅用于关闭基类路径。
    bool ConsumeAttackHit(Vec2 targetPosition, Vec2& sourcePosition, int& damage) override;
    // 只有本体生命归零且专用死亡动画播放完毕后才允许 Scene 删除对象。
    bool IsDeathAnimationFinished() const override;
    // 返回 Boss 被击败后计入玩家终结技解锁系统的击杀额度。
    int GetKillCredit() const override;

    // Scene 在 Initialize 前设置难度；Reset 会保留该值并重建对应战斗参数。
    void SetDifficulty(BossDifficulty difficulty);
    // Scene 每帧同步仍存活的普通小怪数量，用于三只上限与 Boss 免伤判断。
    void SetActiveMinionCount(int count);
    // Scene 每帧同步玩家生命和异常状态，供下一次模型技能决策编码使用。
    void SetPlayerObservation(
        int health,
        int maximumHealth,
        bool directionConfused,
        bool lightningMarked,
        bool movementLocked);
    // 取出并清零本帧累计的召唤请求，具体创建小怪仍由 Scene 负责。
    int ConsumeSummonRequests();
    // 取出下一条尚未播放的困难模式阶段语音；每个血量阈值每局只提交一次。
    BossVoiceCue ConsumeVoiceCue();
    // 消费一次刀击/冲锋枪命中，输出攻击来源、伤害和附加状态效果。
    bool ConsumeSpecialAttackHit(
        Vec2 targetPosition,
        float targetCollisionRadius,
        Vec2& sourcePosition,
        int& damage,
        HostileStatusEffect& effect);
    // 供 HUD 或场景查询当前是否处于可被打断的响指阶段。
    bool IsSummoning() const;
    // 返回当前暴怒层数；普通每分钟、困难每四十秒增加一层。
    int GetRageStacks() const;

private:
    // 技能状态决定 Update 的逻辑分支以及 CurrentFrame 选择哪组动作。
    enum class SkillState
    {
        Idle,          // 可追踪玩家，并等待当前难度的攻击间隔结束。
        KatanaWarning, // 刀击红色预警与前四帧蓄力阶段。
        Recovering,    // 刀击/枪击命中后播放第 4～7 帧收招。
        Summoning,     // 原地响指，使用当前难度的独立临时技能条。
        SmgWarning,    // 冲锋枪整条 X 轴预警与举枪阶段。
        Stunned        // 临时技能条被击破后的难度相关眩晕阶段。
    };

    // 将“伤害事件”与“正在播放的动作”分离，确保一个攻击只结算一次。
    enum class PendingAttack
    {
        None,   // 当前没有等待 Scene/Game 消费的 Boss 命中。
        Katana, // 等待结算横向刀击和方向混乱效果。
        Smg     // 等待结算冲锋枪伤害和闪电标记效果。
    };

    // 每组动作固定八帧；unique_ptr 明确 Boss 独占每张 GDI+ 图片资源。
    using FrameArray8 = std::array<std::unique_ptr<Gdiplus::Image>, 8>;

    // 从 kafka_boss_v2/runtime 一次加载七组左右向动画及 HUD 头像。
    void LoadOptimizedSprites();
    // 查询桌面模型选择技能，再用距离、召唤冷却和连续技能上限修正不合理动作。
    void StartNextSkill(Vec2 targetPosition);
    // 返回固定轮换的下一项；跳过当前距离无法生效或仍处于冷却的动作。
    int SelectFallbackSkill(bool katanaViable, bool summonViable);
    // 预警前段推动 Boss 对齐玩家所在车道；最后短暂锁定，给玩家稳定躲避窗口。
    void UpdateWarningTracking(float deltaSeconds, Vec2 targetPosition);
    // 根据生命、技能、移动状态和计时器返回本帧应该绘制的图片。
    Gdiplus::Image* CurrentFrame() const;
    // 为刀击、冲锋枪与响指阶段绘制半透明红色危险范围。
    void DrawWarning(Gdiplus::Graphics& graphics) const;
    // 在实际伤害帧绘制刀光或整轴弹道，使“预警结束”和“攻击生效”有明确分界。
    void DrawAttackFlash(Gdiplus::Graphics& graphics) const;
    // 绘制头像、本体/临时血条、暴怒层数以及小怪免伤提示。
    void DrawBossHud(Gdiplus::Graphics& graphics) const;
    // 在“已存在 + 待创建”未达到三只时增加一个召唤请求。
    void RequestMinionIfPossible();

    // V2 的七组动作都使用固定 192x192 单元格与统一脚底锚点。
    // 左向帧由构建脚本逐格镜像，帧顺序不会像整图镜像那样被反转。
    FrameArray8 idleLeft_;     // 向左待机：呼吸、头发和衣摆循环。
    FrameArray8 idleRight_;    // 向右待机，与左向资源成对选择。
    FrameArray8 walkLeft_;     // 向左追踪玩家时的完整步行动画。
    FrameArray8 walkRight_;    // 向右追踪玩家时的完整步行动画。
    FrameArray8 katanaLeft_;   // 向左刀击：蓄力、命中和收招共八帧。
    FrameArray8 katanaRight_;  // 向右刀击；第 4 帧附近产生实际命中事件。
    FrameArray8 snapLeft_;     // 向左响指召唤动画及紫色能量特效。
    FrameArray8 snapRight_;    // 向右响指召唤动画及紫色能量特效。
    FrameArray8 smgLeft_;      // 向左冲锋枪举枪、射击、后坐和回收动画。
    FrameArray8 smgRight_;     // 向右冲锋枪动画；命中范围仍由代码计算。
    FrameArray8 deathLeft_;    // 面向左时从失衡到倒地的非循环死亡序列。
    FrameArray8 deathRight_;   // 面向右时使用的死亡序列。
    FrameArray8 stunnedLeft_;  // 面向左时循环播放的眩晕摇摆动画。
    FrameArray8 stunnedRight_; // 面向右时循环播放的眩晕摇摆动画。
    std::unique_ptr<Gdiplus::Image> portrait_; // Boss 顶部 HUD 使用的 512×512 透明优化头像。

    BossAiModel aiModel_; // 从桌面加载的高层技能策略；不会接管移动、动画或伤害判定。
    BossAiObservation aiObservation_{}; // 最近一次玩家观察及决策时补齐的 Boss/距离字段。
    bool hasPlayerObservation_ = false; // Scene 至少同步过一次玩家状态后才允许模型决策。
    BossDifficulty difficulty_ = BossDifficulty::Normal; // 本实例使用普通或困难集中数值表。

    SkillState skillState_ = SkillState::Idle; // 当前技能状态机节点。
    PendingAttack pendingAttack_ = PendingAttack::None; // 尚未被 Game 消费的一次性伤害事件。
    PendingAttack recoveringAttack_ = PendingAttack::None; // Recovering 应继续播放刀或枪的依据。
    Vec2 lockedAttackDirection_{ -1.0f, 0.0f }; // 技能开始时锁定的水平攻击方向，预警结束前不跟随玩家改变。
    RECT clientRect_{}; // 当前客户区，用于整轴枪击预警、HUD 宽度和缩放平移。
    float skillTimer_ = 0.0f; // 当前预警、收招或眩晕状态的剩余秒数。
    float attackIntervalTimer_ = 5.0f; // 从上次攻击开始计算、距离下次五秒节拍的倒计时。
    float passiveSummonTimer_ = 10.0f; // 距离下一次难度相关被动召唤的倒计时。
    float snapTimer_ = 0.0f; // Summoning 内部响指周期的累计时间，同时驱动动画采样。
    float summonCooldownTimer_ = 0.0f; // 主动响指再次进入技能选择池前的剩余时间，不影响被动召唤。
    float rageTimer_ = 0.0f; // 累计存活时间；达到当前难度阈值后转换为暴怒层数。
    float shieldFlashTimer_ = 0.0f; // 小怪存活时受到无效攻击后，HUD 边框加粗闪烁的剩余时间。
    float attackFlashTimer_ = 0.0f; // 刀光或冲锋枪弹道在伤害帧后继续显示的极短时间。
    int nextSkillIndex_ = 0; // 模型缺失或决策非法时使用的轮换索引：0 刀、1 响指、2 枪。
    int lastSkillIndex_ = -1; // 上一次实际发动的技能，用于阻止确定性模型无限重复同一动作。
    int consecutiveSkillCount_ = 0; // 当前技能连续出现的次数；达到两次后强制选择有效替代动作。
    int rageStacks_ = 0; // 当前暴怒层数，每层给刀击和枪击增加固定伤害。
    int activeMinionCount_ = 0; // Scene 同步的存活小怪数；大于零时本体通常免伤。
    int pendingSummonRequests_ = 0; // 等待 Scene 创建的小怪数量，消费后立即清零。
    int summonSkillHealth_ = 60; // 响指阶段的难度相关临时血量，不会扣除 Boss 本体生命。
    HealthBar summonSkillBar_; // 临时技能条的显示值与缓动效果。
    unsigned int pendingVoiceCueMask_ = 0u; // 尚未被 Game 消费的 2/3、1/3 血量语音位集合。
    bool twoThirdsVoiceTriggered_ = false; // 防止生命在阈值附近变化时重复播放 2/3 语音。
    bool oneThirdVoiceTriggered_ = false; // 防止 1/3 血量语音在后续每次受击时重复提交。
};
