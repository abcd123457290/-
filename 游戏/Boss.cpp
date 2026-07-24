#include "Boss.h"

#include "AssetPaths.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
    // 所有会随难度改变的数值集中在一张表中，防止某个技能只修改伤害却忘记
    // 同步预警、血条或计时器。普通模式恢复规则要求的冲锋枪 10 点伤害。
    struct BossCombatTuning
    {
        int maximumHealth;
        int katanaDamage;
        int smgDamage;
        int rageDamagePerStack;
        int summonSkillHealth;
        float walkSpeed;
        float attackInterval;
        float firstAttackDelay;
        float postStunAttackDelay;
        float katanaWarningSeconds;
        float smgWarningSeconds;
        float recoverySeconds;
        float aimCommitSeconds;
        float warningTrackSpeed;
        float stunSeconds;
        float summonInterval;
        float initialSnapDelay;
        float passiveSummonInterval;
        float rageInterval;
        float katanaReach;
        float katanaHalfWidth;
        float smgHalfHeight;
    };

    constexpr BossCombatTuning kNormalTuning{
        600, 20, 10, 10, 60,
        145.0f, 5.0f, 1.35f, 0.75f,
        1.00f, 1.15f, 0.45f, 0.44f, 225.0f, 5.0f,
        5.0f, 2.25f, 10.0f, 60.0f,
        210.0f, 62.0f, 44.0f
    };
    constexpr BossCombatTuning kHardTuning{
        600, 25, 15, 10, 75,
        195.0f, 5.0f, 1.05f, 0.55f,
        0.78f, 0.90f, 0.38f, 0.58f, 300.0f, 4.0f,
        4.5f, 1.60f, 8.0f, 50.0f,
        // 困难攻击判定和红色预警均使用普通模式的 1.5 倍范围。
        315.0f, 93.0f, 66.0f
    };

    const BossCombatTuning& TuningFor(BossDifficulty difficulty)
    {
        return difficulty == BossDifficulty::Hard ? kHardTuning : kNormalTuning;
    }

    constexpr float kBossRadius = 31.0f;
    // 追击启停采用两个阈值形成滞回区间；纵向车道另行检查，避免只看总距离
    // 时停在刀枪都打不到的位置。
    constexpr float kWalkStartDistance = 145.0f;
    constexpr float kWalkStopDistance = 110.0f;
    constexpr float kFarChaseDistance = 360.0f;
    constexpr float kFarChaseSpeedScale = 1.22f;

    constexpr float kDeathSeconds = 1.25f;
    constexpr float kAttackFlashSeconds = 0.20f;
    constexpr float kActiveSummonCooldownSeconds = 12.0f;
    // 玩家当前使用 22px 脚底碰撞圆；高层动作有效性检查采用同一裕量，避免
    // 选择阶段判定必空、伤害阶段才额外加入玩家半径而产生两套范围标准。
    constexpr float kExpectedPlayerCollisionRadius = 22.0f;

    constexpr int kMaximumMinions = 3;
    constexpr unsigned int kTwoThirdsVoiceCueBit = 1u << 0;
    constexpr unsigned int kOneThirdVoiceCueBit = 1u << 1;

    // V2 帧的画布、脚底像素锚点和脚底相对世界中心的偏移。
    constexpr float kSpriteSize = 192.0f;
    constexpr float kSpritePivotY = 176.0f;
    constexpr float kSpriteFootWorldOffset = 34.0f;
    const wchar_t* kAssetRoot = L"游戏\\assets\\kafka_boss_v2\\runtime";

    // 统一验证 GDI+ 加载结果；失败时返回 nullptr，由 Draw 使用待机帧回退。
    std::unique_ptr<Gdiplus::Image> LoadImage(const std::wstring& relativePath)
    {
        auto image = std::make_unique<Gdiplus::Image>(FindAssetPath(relativePath).c_str());
        if (image->GetLastStatus() != Gdiplus::Ok)
        {
            return nullptr;
        }
        return image;
    }

    // 将动作名、方向和两位帧号拼成构建脚本约定的运行时文件路径。
    std::wstring FramePath(const wchar_t* action, const wchar_t* direction, int frame)
    {
        wchar_t path[320]{};
        wsprintfW(path, L"%s\\%s\\%s\\kafka_%s_%s_%02d.png", kAssetRoot, action, direction, action, direction, frame);
        return path;
    }
}

// 初始化只做一次资源加载；之后重新挑战使用 Reset，避免反复解码 112 张 PNG。
void Boss::Initialize(RECT clientRect)
{
    clientRect_ = clientRect;
    center_ = {
        static_cast<float>(clientRect.right - clientRect.left) * 0.78f,
        static_cast<float>(clientRect.bottom - clientRect.top) * 0.52f
    };
    LoadOptimizedSprites();
    // 模型按用户要求保留在桌面；加载失败只影响技能选择，不阻止关卡开始。
    aiModel_.LoadFromDesktop();
    Reset();
}

// unique_ptr::reset 会立即释放 GDI+ Image；遍历同类型数组避免遗漏某套动作。
void Boss::UnloadContent()
{
    portrait_.reset();
    for (auto* frames : {
        &idleLeft_, &idleRight_, &walkLeft_, &walkRight_,
        &katanaLeft_, &katanaRight_, &snapLeft_, &snapRight_,
        &smgLeft_, &smgRight_, &deathLeft_, &deathRight_,
        &stunnedLeft_, &stunnedRight_ })
    {
        for (auto& frame : *frames) frame.reset();
    }
}

// 基类负责世界对象的等比例平移，本类额外保存新客户区供 HUD/预警绘制。
void Boss::ResizeToClient(RECT oldClientRect, RECT newClientRect)
{
    Monster::ResizeToClient(oldClientRect, newClientRect);
    clientRect_ = newClientRect;
}

// 难度在 Initialize/Reset 前由 Scene 注入。难度自身不属于单局临时状态，
// 因而 Reset 不会把它恢复成普通模式。
void Boss::SetDifficulty(BossDifficulty difficulty)
{
    difficulty_ = difficulty;
}

// Reset 必须清除全部跨帧事件，尤其是 pendingAttack_ 和召唤请求，
// 否则重新挑战的第一帧可能继承上一局尚未消费的伤害或小怪生成。
void Boss::Reset()
{
    Monster::Reset();
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    ConfigureCombatProfile(tuning.maximumHealth, tuning.katanaDamage, kBossRadius, 1.0f, 1.0f, 1.0f);
    skillState_ = SkillState::Idle;
    pendingAttack_ = PendingAttack::None;
    recoveringAttack_ = PendingAttack::None;
    lockedAttackDirection_ = { -1.0f, 0.0f };
    skillTimer_ = 0.0f;
    // 首次预警提前出现，避免 Boss 走到玩家附近后仍原地等待数秒；后续攻击
    // 依旧严格使用 attackInterval 的五秒“开始到开始”节拍。
    attackIntervalTimer_ = tuning.firstAttackDelay;
    passiveSummonTimer_ = tuning.passiveSummonInterval;
    snapTimer_ = 0.0f;
    summonCooldownTimer_ = 0.0f;
    rageTimer_ = 0.0f;
    shieldFlashTimer_ = 0.0f;
    attackFlashTimer_ = 0.0f;
    nextSkillIndex_ = 0;
    lastSkillIndex_ = -1;
    consecutiveSkillCount_ = 0;
    aiObservation_ = {};
    hasPlayerObservation_ = false;
    rageStacks_ = 0;
    activeMinionCount_ = 0;
    pendingSummonRequests_ = 0;
    summonSkillHealth_ = tuning.summonSkillHealth;
    summonSkillBar_.ResetValue(summonSkillHealth_, tuning.summonSkillHealth);
    pendingVoiceCueMask_ = 0u;
    twoThirdsVoiceTriggered_ = false;
    oneThirdVoiceTriggered_ = false;
}

// 每帧更新按“通用动画 -> 死亡 -> 被动 -> 当前技能 -> 主动攻击 -> 移动”排序。
// 各技能分支处理完后立即 return，保证同一帧不会同时移动并切换另一技能。
void Boss::Update(float deltaSeconds, Vec2 targetPosition)
{
    if (!active_)
    {
        return;
    }
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    animationTimer_ += deltaSeconds;
    healthBar_.Update(deltaSeconds);
    summonSkillBar_.Update(deltaSeconds);
    shieldFlashTimer_ = (std::max)(0.0f, shieldFlashTimer_ - deltaSeconds);
    attackFlashTimer_ = (std::max)(0.0f, attackFlashTimer_ - deltaSeconds);

    if (health_ <= 0)
    {
        deathTimer_ += deltaSeconds;
        return;
    }

    // 暴怒允许 deltaSeconds 较大时一次补上多层，因此使用 while 而不是 if。
    rageTimer_ += deltaSeconds;
    while (rageTimer_ >= tuning.rageInterval)
    {
        rageTimer_ -= tuning.rageInterval;
        ++rageStacks_;
    }
    summonCooldownTimer_ = (std::max)(0.0f, summonCooldownTimer_ - deltaSeconds);

    // 被动召唤与主动响指彼此独立，但都通过同一个请求队列交给 Scene。
    passiveSummonTimer_ -= deltaSeconds;
    if (passiveSummonTimer_ <= 0.0f)
    {
        passiveSummonTimer_ += tuning.passiveSummonInterval;
        RequestMinionIfPossible();
    }

    // 主动攻击使用“开始到开始”的固定五秒节拍。预警、收招和响指期间都让
    // 倒计时前进；只有真正无法行动的眩晕会暂停，防止响指把状态机永久卡住。
    if (skillState_ != SkillState::Stunned)
    {
        attackIntervalTimer_ -= deltaSeconds;
    }

    // 眩晕期间冻结攻击和移动。结束后仅保留短暂起身时间，不再额外等待完整
    // 五秒；玩家仍获得完整眩晕收益，Boss 也不会出现十秒以上的攻击真空。
    if (skillState_ == SkillState::Stunned)
    {
        skillTimer_ -= deltaSeconds;
        if (skillTimer_ <= 0.0f)
        {
            skillState_ = SkillState::Idle;
            attackIntervalTimer_ = tuning.postStunAttackDelay;
        }
        return;
    }

    // 主动响指有一个完整五秒反制窗口。进入技能时会先播放一次快速响指，
    // 随后仍按 summonInterval 追加；五秒节拍到达后自然切换下一招，避免满怪
    // 或玩家未打技能条时无限站桩。
    if (skillState_ == SkillState::Summoning)
    {
        snapTimer_ += deltaSeconds;
        while (snapTimer_ >= tuning.summonInterval)
        {
            snapTimer_ -= tuning.summonInterval;
            RequestMinionIfPossible();
        }
        if (attackIntervalTimer_ <= 0.0f)
        {
            skillState_ = SkillState::Idle;
            StartNextSkill(targetPosition);
        }
        return;
    }

    // 红色预警归零时只提交一次 pending 事件，实际是否命中由消费函数判断。
    if (skillState_ == SkillState::KatanaWarning || skillState_ == SkillState::SmgWarning)
    {
        UpdateWarningTracking(deltaSeconds, targetPosition);
        skillTimer_ -= deltaSeconds;
        if (skillTimer_ <= 0.0f)
        {
            pendingAttack_ = skillState_ == SkillState::KatanaWarning ? PendingAttack::Katana : PendingAttack::Smg;
            recoveringAttack_ = pendingAttack_;
            attackFlashTimer_ = kAttackFlashSeconds;
            skillState_ = SkillState::Recovering;
            skillTimer_ = tuning.recoverySeconds;
        }
        return;
    }

    // 收招期间保留 recoveringAttack_，让 CurrentFrame 知道应该播放刀还是枪的后四帧。
    if (skillState_ == SkillState::Recovering)
    {
        skillTimer_ -= deltaSeconds;
        if (skillTimer_ <= 0.0f)
        {
            recoveringAttack_ = PendingAttack::None;
            skillState_ = SkillState::Idle;
        }
        return;
    }

    if (attackIntervalTimer_ <= 0.0f)
    {
        StartNextSkill(targetPosition);
        return;
    }

    const Vec2 toPlayer{ targetPosition.x - center_.x, targetPosition.y - center_.y };
    if (std::abs(toPlayer.x) > 0.001f)
    {
        horizontalFacing_ = toPlayer.x < 0.0f ? Facing::Left : Facing::Right;
    }
    const float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
    const float verticalDistance = std::abs(toPlayer.y);
    // 除总距离外还要求 Boss 对齐枪击车道。这样玩家只在 Y 轴移动时，Boss
    // 不会因为欧氏距离已小于停止线而原地释放必定落空的技能。
    const bool wasWalking = motion_ == Motion::Walk;
    const float distanceLimit = wasWalking ? kWalkStopDistance : kWalkStartDistance;
    const float laneLimit = tuning.smgHalfHeight * (wasWalking ? 0.72f : 1.05f);
    const bool shouldWalk = distance > distanceLimit || verticalDistance > laneLimit;
    if (shouldWalk != wasWalking)
    {
        animationTimer_ = 0.0f;
    }
    if (shouldWalk)
    {
        const Vec2 direction = Normalize(toPlayer);
        const float chaseScale = distance > kFarChaseDistance ? kFarChaseSpeedScale : 1.0f;
        const float step = (std::min)(
            distance,
            tuning.walkSpeed * chaseScale * (std::max)(0.0f, deltaSeconds));
        center_.x += direction.x * step;
        center_.y += direction.y * step;
        // 即使窗口拖拽或系统短暂停顿产生异常大帧间隔，也不能越过目标或走出
        // 客户区。Game 还会全局钳制 dt，这里作为实体自身的第二层保护。
        const float left = static_cast<float>(clientRect_.left) + kBossRadius;
        const float right = static_cast<float>(clientRect_.right) - kBossRadius;
        const float top = static_cast<float>(clientRect_.top) + kBossRadius;
        const float bottom = static_cast<float>(clientRect_.bottom) - kBossRadius;
        center_.x = Clamp(center_.x, left, (std::max)(left, right));
        center_.y = Clamp(center_.y, top, (std::max)(top, bottom));
        motion_ = Motion::Walk;
    }
    else
    {
        motion_ = Motion::Idle;
    }
}

// 预警追踪只使用尚未进入“最终锁定”的时间片。红框和 Boss 同步移动，最后
// aimCommitSeconds 内完全停止修正，因此画面既有压迫感，也保留可靠躲避窗口。
void Boss::UpdateWarningTracking(float deltaSeconds, Vec2 targetPosition)
{
    if (skillState_ != SkillState::KatanaWarning && skillState_ != SkillState::SmgWarning)
    {
        return;
    }
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    const float trackingSeconds = (std::max)(0.0f, skillTimer_ - tuning.aimCommitSeconds);
    const float trackingDelta = (std::min)((std::max)(0.0f, deltaSeconds), trackingSeconds);
    if (trackingDelta <= 0.0f)
    {
        return;
    }

    // 只移动到“目标碰撞圆刚好进入攻击矩形”的最近位置，不盲目追到玩家
    // 中心。这样同一段追踪路程不会在已经满足的轴上被浪费。
    Vec2 desiredCenter = center_;
    const float verticalDifference = targetPosition.y - center_.y;
    const float verticalAllowance =
        (skillState_ == SkillState::KatanaWarning ? tuning.katanaHalfWidth : tuning.smgHalfHeight) +
        kExpectedPlayerCollisionRadius;
    if (std::abs(verticalDifference) > verticalAllowance)
    {
        desiredCenter.y += (verticalDifference < 0.0f ? -1.0f : 1.0f) *
            (std::abs(verticalDifference) - verticalAllowance);
    }
    if (skillState_ == SkillState::KatanaWarning)
    {
        // 只在玩家仍位于刀锋前方且距离过远时向前压进；玩家穿到背后后不瞬间
        // 转身，避免角色左右帧闪烁，也让成功绕背成为有效躲避方式。
        const float forwardDistance = (targetPosition.x - center_.x) * lockedAttackDirection_.x;
        const float forwardAllowance = tuning.katanaReach + kExpectedPlayerCollisionRadius;
        if (forwardDistance > forwardAllowance)
        {
            desiredCenter.x += lockedAttackDirection_.x * (forwardDistance - forwardAllowance);
        }
    }

    const Vec2 toDesired{ desiredCenter.x - center_.x, desiredCenter.y - center_.y };
    const float distance = std::sqrt(toDesired.x * toDesired.x + toDesired.y * toDesired.y);
    if (distance > 0.001f)
    {
        const float step = (std::min)(distance, tuning.warningTrackSpeed * trackingDelta);
        const Vec2 direction = Normalize(toDesired);
        center_.x += direction.x * step;
        center_.y += direction.y * step;
    }

    const float left = static_cast<float>(clientRect_.left) + kBossRadius;
    const float right = static_cast<float>(clientRect_.right) - kBossRadius;
    const float top = static_cast<float>(clientRect_.top) + kBossRadius;
    const float bottom = static_cast<float>(clientRect_.bottom) - kBossRadius;
    center_.x = Clamp(center_.x, left, (std::max)(left, right));
    center_.y = Clamp(center_.y, top, (std::max)(top, bottom));
}

// 模型只在技能间隔结束时做一次高层选择；随后通过有效距离、主动响指冷却、
// 闪电连携和连续动作上限进行导演式校正，避免确定性策略反复空招或重复同一招。
void Boss::StartNextSkill(Vec2 targetPosition)
{
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    // 攻击一开始便预约下一次五秒节拍；预警和收招的耗时包含在这五秒内。
    attackIntervalTimer_ = tuning.attackInterval;
    motion_ = Motion::Idle;
    lockedAttackDirection_ = targetPosition.x < center_.x ? Vec2{ -1.0f, 0.0f } : Vec2{ 1.0f, 0.0f };
    horizontalFacing_ = lockedAttackDirection_.x < 0.0f ? Facing::Left : Facing::Right;
    pendingAttack_ = PendingAttack::None;

    // 被动召唤在本帧早于主动决策执行，因此必须把 pending 也算进去。
    // active=2、pending=1 已等同满三只，不能再进入没有召唤收益的响指阶段。
    const int effectiveMinionCount = (std::min)(
        kMaximumMinions,
        activeMinionCount_ + pendingSummonRequests_);
    const float absoluteXDistance = std::abs(targetPosition.x - center_.x);
    const float absoluteYDistance = std::abs(targetPosition.y - center_.y);
    const float katanaTrackingDistance = tuning.warningTrackSpeed *
        (std::max)(0.0f, tuning.katanaWarningSeconds - tuning.aimCommitSeconds);
    const float katanaRequiredX = (std::max)(
        0.0f,
        absoluteXDistance - (tuning.katanaReach + kExpectedPlayerCollisionRadius));
    const float katanaRequiredY = (std::max)(
        0.0f,
        absoluteYDistance - (tuning.katanaHalfWidth + kExpectedPlayerCollisionRadius));
    const float katanaRequiredTracking = std::sqrt(
        katanaRequiredX * katanaRequiredX + katanaRequiredY * katanaRequiredY);
    const bool katanaViable = katanaRequiredTracking <= katanaTrackingDistance;
    const bool summonViable =
        effectiveMinionCount < kMaximumMinions && summonCooldownTimer_ <= 0.0f;

    int skill = BossAiModel::InvalidAction;
    if (aiModel_.IsLoaded() && hasPlayerObservation_)
    {
        aiObservation_.bossHealth = health_;
        aiObservation_.bossMaximumHealth = maxHealth_;
        aiObservation_.absoluteXDistance = absoluteXDistance;
        aiObservation_.absoluteYDistance = absoluteYDistance;
        aiObservation_.minionCount = effectiveMinionCount;
        aiObservation_.rageStacks = rageStacks_;
        skill = aiModel_.SelectAction(aiObservation_);
    }
    if (skill < 0 || skill >= BossAiModel::ActionCount)
    {
        skill = SelectFallbackSkill(katanaViable, summonViable);
    }
    if (skill == 0 && !katanaViable)
    {
        skill = 2;
    }
    else if (skill == 1 && !summonViable)
    {
        skill = katanaViable ? 0 : 2;
    }

    // 冲锋枪已经挂上标记且玩家进入刀距时，优先用横斩引爆追加伤害，形成可读
    // 的“枪标记 -> 刀追击”组合，而不是继续机械地重复扫射。
    if (lastSkillIndex_ == 2 && aiObservation_.lightningMarked && katanaViable)
    {
        skill = 0;
    }

    if (skill == lastSkillIndex_ && consecutiveSkillCount_ >= 2)
    {
        if (skill == 0)
        {
            skill = 2;
        }
        else if (skill == 1)
        {
            skill = katanaViable ? 0 : 2;
        }
        else if (katanaViable)
        {
            skill = 0;
        }
        else if (summonViable)
        {
            skill = 1;
        }
    }

    if (skill == lastSkillIndex_)
    {
        consecutiveSkillCount_ = (std::min)(2, consecutiveSkillCount_ + 1);
    }
    else
    {
        lastSkillIndex_ = skill;
        consecutiveSkillCount_ = 1;
    }
    animationTimer_ = 0.0f;

    if (skill == 0)
    {
        skillState_ = SkillState::KatanaWarning;
        skillTimer_ = tuning.katanaWarningSeconds;
    }
    else if (skill == 1)
    {
        skillState_ = SkillState::Summoning;
        summonSkillHealth_ = tuning.summonSkillHealth;
        summonSkillBar_.ResetValue(summonSkillHealth_, tuning.summonSkillHealth);
        // 第一次响指是技能起手：比旧版等待完整五秒更快，但仍保留普通2.25秒、
        // 困难1.6秒的破条窗口；后续响指继续严格按照 summonInterval 计时。
        snapTimer_ = (std::max)(0.0f, tuning.summonInterval - tuning.initialSnapDelay);
        summonCooldownTimer_ = kActiveSummonCooldownSeconds;
    }
    else
    {
        skillState_ = SkillState::SmgWarning;
        skillTimer_ = tuning.smgWarningSeconds;
    }
}

// 固定轮换是模型缺失、格式错误或观察尚未同步时的可靠后备路径。枪击始终
// 有效，因此最多检查三项必定得到结果，不会因动作掩码形成无限循环。
int Boss::SelectFallbackSkill(bool katanaViable, bool summonViable)
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const int skill = nextSkillIndex_ % 3;
        ++nextSkillIndex_;
        if ((skill == 0 && !katanaViable) || (skill == 1 && !summonViable))
        {
            continue;
        }
        return skill;
    }
    return 2;
}

// 受伤优先级：响指临时条 > 小怪免伤 > Boss 本体。
// 返回“实际伤害”让 Scene 的浮字、击杀和掉落不会把无效攻击当成成功扣血。
int Boss::TakeDamage(int damage)
{
    if (!IsAlive() || damage <= 0)
    {
        return 0;
    }

    if (skillState_ == SkillState::Summoning)
    {
        const BossCombatTuning& tuning = TuningFor(difficulty_);
        const int applied = (std::min)(summonSkillHealth_, damage);
        summonSkillHealth_ -= applied;
        summonSkillBar_.SetValue(summonSkillHealth_, tuning.summonSkillHealth);
        if (summonSkillHealth_ <= 0)
        {
            // Boss 本体生命保持技能开始前的数值，只击破独立的临时技能条。
            skillState_ = SkillState::Stunned;
            skillTimer_ = tuning.stunSeconds;
            snapTimer_ = 0.0f;
        }
        return applied;
    }

    if (activeMinionCount_ > 0)
    {
        shieldFlashTimer_ = 0.24f;
        return 0;
    }

    const int previous = health_;
    health_ = (std::max)(0, health_ - damage);
    healthBar_.SetValue(health_, maxHealth_);
    if (difficulty_ == BossDifficulty::Hard)
    {
        const int twoThirdsThreshold = maxHealth_ * 2 / 3;
        const int oneThirdThreshold = maxHealth_ / 3;
        if (!twoThirdsVoiceTriggered_ && previous > twoThirdsThreshold && health_ <= twoThirdsThreshold)
        {
            twoThirdsVoiceTriggered_ = true;
            pendingVoiceCueMask_ |= kTwoThirdsVoiceCueBit;
        }
        if (!oneThirdVoiceTriggered_ && previous > oneThirdThreshold && health_ <= oneThirdThreshold)
        {
            oneThirdVoiceTriggered_ = true;
            pendingVoiceCueMask_ |= kOneThirdVoiceCueBit;
        }
    }
    hitEffectTimer_ = 0.28f;
    if (health_ <= 0)
    {
        deathTimer_ = 0.0f;
        pendingAttack_ = PendingAttack::None;
        pendingSummonRequests_ = 0;
    }
    return previous - health_;
}

// 刻意禁用 Monster 的普通攻击管线，避免 Boss 专用事件被结算两次。
bool Boss::ConsumeAttackHit(Vec2, Vec2&, int&)
{
    return false;
}

// pendingAttack_ 在判断范围前就被清空，所以即使玩家躲开，当前攻击也不会再次判定。
bool Boss::ConsumeSpecialAttackHit(
    Vec2 targetPosition,
    float targetCollisionRadius,
    Vec2& sourcePosition,
    int& damage,
    HostileStatusEffect& effect)
{
    if (pendingAttack_ == PendingAttack::None || !IsAlive())
    {
        return false;
    }

    const PendingAttack attack = pendingAttack_;
    pendingAttack_ = PendingAttack::None;
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    const float targetRadius = (std::max)(0.0f, targetCollisionRadius);
    bool hit = false;
    if (attack == PendingAttack::Katana)
    {
        const float forward = (targetPosition.x - center_.x) * lockedAttackDirection_.x;
        // 目标坐标位于脚底中心，必须把碰撞半径纳入矩形相交判断；否则角色
        // 身体已经进入红框、中心点仍在框外时会出现“动画命中但没有伤害”。
        hit = forward >= -targetRadius && forward <= tuning.katanaReach + targetRadius &&
            std::abs(targetPosition.y - center_.y) <= tuning.katanaHalfWidth + targetRadius;
        damage = tuning.katanaDamage + rageStacks_ * tuning.rageDamagePerStack;
        effect = HostileStatusEffect::DirectionConfusion;
    }
    else
    {
        // 冲锋枪覆盖整条 X 轴，仅以玩家是否位于预警横带内判断命中。
        // 普通/困难基础伤害分别为 10/15；同样加入玩家碰撞半径，消除视觉
        // 上已经接触弹幕边缘、脚底中心却差几个像素而漏判的问题。
        hit = std::abs(targetPosition.y - center_.y) <= tuning.smgHalfHeight + targetRadius;
        damage = tuning.smgDamage + rageStacks_ * tuning.rageDamagePerStack;
        effect = HostileStatusEffect::LightningMark;
    }
    if (!hit)
    {
        return false;
    }
    sourcePosition = center_;
    return true;
}

// Scene 是小怪容器的所有者，因此 Boss 只保存经过 0～3 截断的数量镜像。
void Boss::SetActiveMinionCount(int count)
{
    activeMinionCount_ = (std::max)(0, (std::min)(kMaximumMinions, count));
}

void Boss::SetPlayerObservation(
    int health,
    int maximumHealth,
    bool directionConfused,
    bool lightningMarked,
    bool movementLocked)
{
    aiObservation_.playerHealth = (std::max)(0, health);
    aiObservation_.playerMaximumHealth = (std::max)(1, maximumHealth);
    aiObservation_.directionConfused = directionConfused;
    aiObservation_.lightningMarked = lightningMarked;
    aiObservation_.movementLocked = movementLocked;
    hasPlayerObservation_ = true;
}

// “取出并清零”保证一个召唤请求只会创建一只小怪。
int Boss::ConsumeSummonRequests()
{
    const int requests = pendingSummonRequests_;
    pendingSummonRequests_ = 0;
    return requests;
}

BossVoiceCue Boss::ConsumeVoiceCue()
{
    // 单次超高伤害若同时跨过两条线，只播放与当前更低血量阶段相符的 1/3
    // 台词，避免下一帧立刻用另一条语音截断它。
    if ((pendingVoiceCueMask_ & kOneThirdVoiceCueBit) != 0u)
    {
        pendingVoiceCueMask_ &= ~(kOneThirdVoiceCueBit | kTwoThirdsVoiceCueBit);
        return BossVoiceCue::OneThirdHealth;
    }
    if ((pendingVoiceCueMask_ & kTwoThirdsVoiceCueBit) != 0u)
    {
        pendingVoiceCueMask_ &= ~kTwoThirdsVoiceCueBit;
        return BossVoiceCue::TwoThirdsHealth;
    }
    return BossVoiceCue::None;
}

// 同时计算已存在与尚未消费的请求，避免同一帧连续请求突破三只上限。
void Boss::RequestMinionIfPossible()
{
    if (activeMinionCount_ + pendingSummonRequests_ < kMaximumMinions)
    {
        ++pendingSummonRequests_;
    }
}

// 轻量状态查询不暴露私有枚举，降低 Scene 对 Boss 状态机细节的依赖。
bool Boss::IsSummoning() const
{
    return skillState_ == SkillState::Summoning;
}

int Boss::GetRageStacks() const
{
    return rageStacks_;
}

// Scene 等死亡动画结束后再移除对象，确保倒地序列不会被生命归零立即截断。
bool Boss::IsDeathAnimationFinished() const
{
    return active_ && health_ <= 0 && deathTimer_ >= kDeathSeconds;
}

// Boss 提供多份击杀额度，用于玩家依赖击杀数的技能解锁/统计系统。
int Boss::GetKillCredit() const
{
    return 3;
}

// 文件名由构建脚本确定；这里一次加载全部左右向八帧，运行时只切换指针。
void Boss::LoadOptimizedSprites()
{
    for (int frame = 0; frame < 8; ++frame)
    {
        idleLeft_[frame] = LoadImage(FramePath(L"idle", L"left", frame));
        idleRight_[frame] = LoadImage(FramePath(L"idle", L"right", frame));
        walkLeft_[frame] = LoadImage(FramePath(L"walk", L"left", frame));
        walkRight_[frame] = LoadImage(FramePath(L"walk", L"right", frame));
        katanaLeft_[frame] = LoadImage(FramePath(L"katana", L"left", frame));
        katanaRight_[frame] = LoadImage(FramePath(L"katana", L"right", frame));
        snapLeft_[frame] = LoadImage(FramePath(L"snap", L"left", frame));
        snapRight_[frame] = LoadImage(FramePath(L"snap", L"right", frame));
        smgLeft_[frame] = LoadImage(FramePath(L"smg", L"left", frame));
        smgRight_[frame] = LoadImage(FramePath(L"smg", L"right", frame));
        deathLeft_[frame] = LoadImage(FramePath(L"death", L"left", frame));
        deathRight_[frame] = LoadImage(FramePath(L"death", L"right", frame));
        stunnedLeft_[frame] = LoadImage(FramePath(L"stunned", L"left", frame));
        stunnedRight_[frame] = LoadImage(FramePath(L"stunned", L"right", frame));
    }
    // 用户指定头像经过构图优化、绿幕移除与 512px UI 缩放后用于局内 HUD。
    portrait_ = LoadImage(std::wstring(kAssetRoot) + L"\\kafka_portrait_ui_v3.png");
}

// 选帧优先级必须与 Update 状态一致：死亡 > 眩晕 > 攻击 > 响指 > 移动 > 待机。
// 预警使用前四帧，Recovering 使用后四帧，因此伤害事件与爆发动作同步。
Gdiplus::Image* Boss::CurrentFrame() const
{
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    const bool left = horizontalFacing_ == Facing::Left;
    const auto pick = [left](const FrameArray8& leftFrames, const FrameArray8& rightFrames, int frame)
    {
        const int safeFrame = (std::max)(0, (std::min)(7, frame));
        return left ? leftFrames[safeFrame].get() : rightFrames[safeFrame].get();
    };

    if (health_ <= 0)
    {
        const float progress = Clamp(deathTimer_ / kDeathSeconds, 0.0f, 0.999f);
        return pick(deathLeft_, deathRight_, static_cast<int>(progress * 8.0f));
    }
    if (skillState_ == SkillState::Stunned)
    {
        return pick(stunnedLeft_, stunnedRight_, static_cast<int>(animationTimer_ * 8.0f) % 8);
    }
    if (skillState_ == SkillState::KatanaWarning ||
        (skillState_ == SkillState::Recovering && recoveringAttack_ == PendingAttack::Katana))
    {
        const float progress = skillState_ == SkillState::Recovering
            ? 1.0f - Clamp(skillTimer_ / tuning.recoverySeconds, 0.0f, 1.0f)
            : 1.0f - Clamp(skillTimer_ / tuning.katanaWarningSeconds, 0.0f, 1.0f);
        const int frame = skillState_ == SkillState::Recovering
            ? 4 + static_cast<int>(progress * 4.0f)
            : static_cast<int>(progress * 4.0f);
        return pick(katanaLeft_, katanaRight_, frame);
    }
    if (skillState_ == SkillState::SmgWarning ||
        (skillState_ == SkillState::Recovering && recoveringAttack_ == PendingAttack::Smg))
    {
        const float progress = skillState_ == SkillState::Recovering
            ? 1.0f - Clamp(skillTimer_ / tuning.recoverySeconds, 0.0f, 1.0f)
            : 1.0f - Clamp(skillTimer_ / tuning.smgWarningSeconds, 0.0f, 1.0f);
        const int frame = skillState_ == SkillState::Recovering
            ? 4 + static_cast<int>(progress * 4.0f)
            : static_cast<int>(progress * 4.0f);
        return pick(smgLeft_, smgRight_, frame);
    }
    if (skillState_ == SkillState::Summoning)
    {
        constexpr float kSnapAnimationSeconds = 8.0f / 12.0f;
        const float snapStart = tuning.summonInterval - kSnapAnimationSeconds;
        if (snapTimer_ >= snapStart)
        {
            const int frame = static_cast<int>((snapTimer_ - snapStart) * 12.0f);
            return pick(snapLeft_, snapRight_, frame);
        }
        return pick(idleLeft_, idleRight_, static_cast<int>(animationTimer_ * 6.0f) % 8);
    }
    if (motion_ == Motion::Walk)
    {
        return pick(walkLeft_, walkRight_, static_cast<int>(animationTimer_ * 10.0f) % 8);
    }
    return pick(idleLeft_, idleRight_, static_cast<int>(animationTimer_ * 6.0f) % 8);
}

// 绘制不改变任何战斗状态；资源缺帧时使用同方向待机第 0 帧保持模型可见。
void Boss::Draw(Gdiplus::Graphics& graphics) const
{
    if (!active_)
    {
        return;
    }
    DrawWarning(graphics);
    if (difficulty_ == BossDifficulty::Hard && IsAlive())
    {
        // 困难模式使用低透明度脉冲光环提供持续的视觉辨识，不修改或复用原图，
        // 也避免用高亮叠加角色本身而造成旧版本的模型闪烁。
        const float pulse = 3.0f + std::sin(animationTimer_ * 4.0f) * 2.0f;
        Gdiplus::SolidBrush hardAura(Gdiplus::Color(42, 182, 52, 214));
        graphics.FillEllipse(
            &hardAura,
            center_.x - kBossRadius - pulse,
            center_.y - kBossRadius - pulse,
            (kBossRadius + pulse) * 2.0f,
            (kBossRadius + pulse) * 2.0f);
    }
    Gdiplus::Image* image = CurrentFrame();
    if (image == nullptr)
    {
        // 单帧资源损坏时保持角色可见，避免 nullptr 帧造成一帧完全消失。
        image = horizontalFacing_ == Facing::Left ? idleLeft_[0].get() : idleRight_[0].get();
    }
    if (image != nullptr)
    {
        const float top = center_.y + kSpriteFootWorldOffset - kSpritePivotY;
        graphics.DrawImage(image, Gdiplus::RectF(center_.x - kSpriteSize * 0.5f, top, kSpriteSize, kSpriteSize));
    }
    DrawAttackFlash(graphics);
    DrawBossHud(graphics);
}

// 预警只在 Boss 存活时显示。刀击为单方向矩形，枪击覆盖整个 X 轴，
// 响指使用圆形区域提示玩家当前处于特殊可打断阶段。
void Boss::DrawWarning(Gdiplus::Graphics& graphics) const
{
    if (!IsAlive()) return;
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    float warningProgress = 0.0f;
    if (skillState_ == SkillState::KatanaWarning)
    {
        warningProgress = 1.0f - Clamp(skillTimer_ / tuning.katanaWarningSeconds, 0.0f, 1.0f);
    }
    else if (skillState_ == SkillState::SmgWarning)
    {
        warningProgress = 1.0f - Clamp(skillTimer_ / tuning.smgWarningSeconds, 0.0f, 1.0f);
    }
    const BYTE fillAlpha = static_cast<BYTE>(54.0f + warningProgress * 58.0f);
    const BYTE edgeAlpha = static_cast<BYTE>(205.0f + warningProgress * 50.0f);
    Gdiplus::SolidBrush fill(Gdiplus::Color(fillAlpha, 235, 32, 42));
    Gdiplus::Pen edge(Gdiplus::Color(edgeAlpha, 255, 48, 54), 2.5f + warningProgress * 2.5f);
    if (skillState_ == SkillState::KatanaWarning)
    {
        const float left = lockedAttackDirection_.x < 0.0f ? center_.x - tuning.katanaReach : center_.x;
        const Gdiplus::RectF warning(
            left,
            center_.y - tuning.katanaHalfWidth,
            tuning.katanaReach,
            tuning.katanaHalfWidth * 2.0f);
        graphics.FillRectangle(&fill, warning);
        graphics.DrawRectangle(&edge, warning);
    }
    else if (skillState_ == SkillState::SmgWarning)
    {
        const Gdiplus::RectF warning(
            static_cast<float>(clientRect_.left),
            center_.y - tuning.smgHalfHeight,
            static_cast<float>(clientRect_.right - clientRect_.left),
            tuning.smgHalfHeight * 2.0f);
        graphics.FillRectangle(&fill, warning);
        graphics.DrawRectangle(&edge, warning);
    }
    else if (skillState_ == SkillState::Summoning)
    {
        graphics.FillEllipse(&fill, center_.x - 90.0f, center_.y - 90.0f, 180.0f, 180.0f);
        graphics.DrawEllipse(&edge, center_.x - 90.0f, center_.y - 90.0f, 180.0f, 180.0f);
    }
}

// 伤害事件虽然只结算一帧，但视觉爆发会保留 0.2 秒。刀击使用面向方向上的
// 双层斩线，冲锋枪使用覆盖客户区的三束弹道；即使玩家成功躲开也能看出技能
// 已经真正开火，而不会把预警消失误解成攻击失效。
void Boss::DrawAttackFlash(Gdiplus::Graphics& graphics) const
{
    if (!IsAlive() || attackFlashTimer_ <= 0.0f || recoveringAttack_ == PendingAttack::None)
    {
        return;
    }
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    const float strength = Clamp(attackFlashTimer_ / kAttackFlashSeconds, 0.0f, 1.0f);
    // Alpha 随剩余时间归零，避免最后一帧仍然明亮、下一帧突然整条消失。
    const BYTE outerAlpha = static_cast<BYTE>(strength * 220.0f);
    const BYTE coreAlpha = static_cast<BYTE>(strength * 255.0f);
    Gdiplus::Pen outer(Gdiplus::Color(outerAlpha, 183, 67, 255), 10.0f * strength + 3.0f);
    Gdiplus::Pen core(Gdiplus::Color(coreAlpha, 255, 230, 255), 2.0f + 2.5f * strength);

    if (recoveringAttack_ == PendingAttack::Katana)
    {
        const float direction = lockedAttackDirection_.x;
        for (int line = -1; line <= 1; ++line)
        {
            const float offset = static_cast<float>(line) * 11.0f;
            const Gdiplus::PointF start(
                center_.x + direction * 8.0f,
                center_.y + tuning.katanaHalfWidth * 0.72f + offset);
            const Gdiplus::PointF end(
                center_.x + direction * tuning.katanaReach,
                center_.y - tuning.katanaHalfWidth * 0.72f + offset);
            graphics.DrawLine(&outer, start, end);
            graphics.DrawLine(&core, start, end);
        }
    }
    else
    {
        // 短虚线表现高速弹链而非持续激光；伤害仍只在开火首帧结算一次。
        outer.SetDashStyle(Gdiplus::DashStyleDash);
        core.SetDashStyle(Gdiplus::DashStyleDash);
        outer.SetDashCap(Gdiplus::DashCapRound);
        core.SetDashCap(Gdiplus::DashCapRound);
        const float left = static_cast<float>(clientRect_.left);
        const float right = static_cast<float>(clientRect_.right);
        for (int lane = -1; lane <= 1; ++lane)
        {
            const float y = center_.y + static_cast<float>(lane) * tuning.smgHalfHeight * 0.42f;
            graphics.DrawLine(&outer, left, y, right, y);
            graphics.DrawLine(&core, left, y, right, y);
        }
        Gdiplus::SolidBrush muzzle(Gdiplus::Color(coreAlpha, 255, 238, 172));
        const float muzzleX = center_.x + lockedAttackDirection_.x * 42.0f;
        graphics.FillEllipse(&muzzle, muzzleX - 13.0f, center_.y - 13.0f, 26.0f, 26.0f);
    }
}

// HUD 根据 Summoning 切换本体血条和难度相关临时技能条；边框宽度反馈免疫攻击。
void Boss::DrawBossHud(Gdiplus::Graphics& graphics) const
{
    if (!IsAlive()) return;
    const BossCombatTuning& tuning = TuningFor(difficulty_);
    const float width = static_cast<float>(clientRect_.right - clientRect_.left);
    const float panelWidth = (std::min)(660.0f, width - 48.0f);
    constexpr float kPanelHeight = 104.0f;
    const float x = (width - panelWidth) * 0.5f;
    const float y = 14.0f;
    const float portraitX = x + 10.0f;
    const float portraitY = y + 9.0f;
    constexpr float kPortraitSize = 86.0f;
    const float contentX = portraitX + kPortraitSize + 16.0f;
    const float contentRight = x + panelWidth - 16.0f;
    const float barWidth = (std::max)(120.0f, contentRight - contentX);

    Gdiplus::SolidBrush shadow(Gdiplus::Color(105, 12, 6, 16));
    Gdiplus::SolidBrush panel(Gdiplus::Color(242, 28, 16, 31));
    Gdiplus::SolidBrush portraitBack(Gdiplus::Color(255, 54, 25, 49));
    const Gdiplus::Color edgeColor = difficulty_ == BossDifficulty::Hard
        ? Gdiplus::Color(255, 239, 60, 85)
        : Gdiplus::Color(255, 216, 58, 118);
    Gdiplus::Pen edge(edgeColor, shieldFlashTimer_ > 0.0f ? 5.0f : 2.0f);
    Gdiplus::Pen innerEdge(Gdiplus::Color(165, 255, 180, 224), 1.0f);
    Gdiplus::Pen portraitEdge(edgeColor, 2.0f);
    Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 234, 244));
    Gdiplus::SolidBrush secondary(Gdiplus::Color(255, 216, 175, 197));
    Gdiplus::SolidBrush shield(Gdiplus::Color(255, 255, 200, 87));
    Gdiplus::SolidBrush rage(Gdiplus::Color(255, 240, 52, 48));
    Gdiplus::SolidBrush badge(edgeColor);
    Gdiplus::SolidBrush aiStatus(aiModel_.IsLoaded()
        ? Gdiplus::Color(255, 185, 132, 255)
        : Gdiplus::Color(255, 190, 190, 190));
    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&family, 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(&family, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font badgeFont(&family, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    graphics.FillRectangle(&shadow, x + 5.0f, y + 6.0f, panelWidth, kPanelHeight);
    graphics.FillRectangle(&panel, x, y, panelWidth, kPanelHeight);
    graphics.DrawRectangle(&edge, x, y, panelWidth, kPanelHeight);
    graphics.DrawRectangle(&innerEdge, x + 5.0f, y + 5.0f, panelWidth - 10.0f, kPanelHeight - 10.0f);
    graphics.FillRectangle(&badge, x, y, 5.0f, kPanelHeight);
    graphics.FillRectangle(&portraitBack, portraitX, portraitY, kPortraitSize, kPortraitSize);
    if (portrait_ != nullptr)
    {
        // 游戏全局使用最近邻绘制像素动作；头像单独使用高质量缩放，避免 512px
        // 透明原图压到 86px 后仍呈现粗糙锯齿。Restore 不会污染角色动画。
        const Gdiplus::GraphicsState portraitState = graphics.Save();
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.DrawImage(
            portrait_.get(),
            Gdiplus::RectF(portraitX + 2.0f, portraitY + 2.0f, kPortraitSize - 4.0f, kPortraitSize - 4.0f));
        graphics.Restore(portraitState);
    }
    else
    {
        // 图片异常时显示简洁文字徽记，不退回旧的粗糙全身模型。
        Gdiplus::StringFormat portraitFormat;
        portraitFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
        portraitFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        graphics.DrawString(
            L"卡",
            -1,
            &titleFont,
            Gdiplus::RectF(portraitX, portraitY, kPortraitSize, kPortraitSize),
            &portraitFormat,
            &text);
    }
    graphics.DrawRectangle(&portraitEdge, portraitX, portraitY, kPortraitSize, kPortraitSize);

    graphics.DrawString(
        L"卡芙卡",
        -1,
        &titleFont,
        Gdiplus::PointF(contentX, y + 8.0f),
        &text);

    const Gdiplus::RectF difficultyBadge(contentX + 72.0f, y + 9.0f, 58.0f, 19.0f);
    Gdiplus::StringFormat badgeFormat;
    badgeFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    badgeFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    graphics.FillRectangle(&badge, difficultyBadge);
    graphics.DrawString(
        difficulty_ == BossDifficulty::Hard ? L"HARD" : L"NORMAL",
        -1,
        &badgeFont,
        difficultyBadge,
        &badgeFormat,
        &text);

    wchar_t hp[48]{};
    wsprintfW(hp, L"HP  %d / %d", health_, maxHealth_);
    graphics.DrawString(hp, -1, &smallFont, Gdiplus::PointF(contentRight - 106.0f, y + 11.0f), &text);
    graphics.DrawString(
        aiModel_.IsLoaded() ? L"AI  Q-learning 已启用" : L"AI  固定轮换（模型未加载）",
        -1,
        &smallFont,
        Gdiplus::PointF(contentX, y + 33.0f),
        &aiStatus);

    if (activeMinionCount_ > 0 && skillState_ != SkillState::Summoning)
    {
        graphics.DrawString(
            L"◆ 召唤物护盾：本体免伤",
            -1,
            &smallFont,
            Gdiplus::PointF(contentX + 150.0f, y + 33.0f),
            &shield);
    }
    else
    {
        graphics.DrawString(
            L"第一关 BOSS",
            -1,
            &smallFont,
            Gdiplus::PointF(contentX + 150.0f, y + 33.0f),
            &secondary);
    }

    if (skillState_ == SkillState::Summoning)
    {
        const float skillRatio = Clamp(
            static_cast<float>(summonSkillHealth_) / static_cast<float>((std::max)(1, tuning.summonSkillHealth)),
            0.0f,
            1.0f);
        Gdiplus::SolidBrush skillTrack(Gdiplus::Color(255, 53, 37, 61));
        Gdiplus::SolidBrush skillFill(Gdiplus::Color(255, 151, 89, 229));
        Gdiplus::SolidBrush skillLight(Gdiplus::Color(255, 220, 176, 255));
        Gdiplus::Pen skillEdge(Gdiplus::Color(240, 221, 177, 255), 2.0f);
        graphics.FillRectangle(&skillTrack, contentX, y + 57.0f, barWidth, 17.0f);
        graphics.FillRectangle(&skillFill, contentX, y + 57.0f, barWidth * skillRatio, 17.0f);
        graphics.FillRectangle(&skillLight, contentX, y + 59.0f, barWidth * skillRatio, 4.0f);
        graphics.DrawRectangle(&skillEdge, contentX, y + 57.0f, barWidth, 17.0f);
        wchar_t skillHp[48]{};
        wsprintfW(
            skillHp,
            L"打断响指  %d / %d    击破后眩晕 %d 秒",
            summonSkillHealth_,
            tuning.summonSkillHealth,
            static_cast<int>(tuning.stunSeconds + 0.5f));
        graphics.DrawString(skillHp, -1, &smallFont, Gdiplus::PointF(contentX, y + 79.0f), &text);
    }
    else
    {
        healthBar_.Draw(graphics, contentX, y + 57.0f, barWidth, 17.0f);
        wchar_t combatStatus[96]{};
        Gdiplus::Brush* combatStatusBrush = &secondary;
        const auto formatTenths = [](float seconds)
        {
            return (std::max)(0, static_cast<int>(std::ceil((std::max)(0.0f, seconds) * 10.0f)));
        };
        if (skillState_ == SkillState::KatanaWarning || skillState_ == SkillState::SmgWarning)
        {
            const int tenths = formatTenths(skillTimer_);
            wsprintfW(
                combatStatus,
                skillState_ == SkillState::KatanaWarning
                    ? L"横斩锁定  %d.%d 秒后命中"
                    : L"整轴扫射  %d.%d 秒后开火",
                tenths / 10,
                tenths % 10);
            combatStatusBrush = &rage;
        }
        else if (skillState_ == SkillState::Stunned)
        {
            const int tenths = formatTenths(skillTimer_);
            wsprintfW(combatStatus, L"破防眩晕  剩余 %d.%d 秒", tenths / 10, tenths % 10);
            combatStatusBrush = &shield;
        }
        else if (skillState_ == SkillState::Recovering)
        {
            lstrcpyW(combatStatus, L"攻击结束，正在重新追击");
        }
        else if (activeMinionCount_ > 0)
        {
            lstrcpyW(combatStatus, L"优先消灭召唤物，解除 Boss 免伤");
            combatStatusBrush = &shield;
        }
        else
        {
            const int tenths = formatTenths(attackIntervalTimer_);
            wsprintfW(combatStatus, L"下一次攻击  %d.%d 秒", tenths / 10, tenths % 10);
        }
        graphics.DrawString(
            combatStatus,
            -1,
            &smallFont,
            Gdiplus::RectF(contentX, y + 78.0f, (std::max)(80.0f, barWidth - 78.0f), 18.0f),
            nullptr,
            combatStatusBrush);
    }
    if (rageStacks_ > 0)
    {
        wchar_t count[24]{};
        wsprintfW(count, L"暴怒 ×%d", rageStacks_);
        graphics.DrawString(count, -1, &smallFont, Gdiplus::PointF(contentRight - 66.0f, y + 79.0f), &rage);
    }
}
