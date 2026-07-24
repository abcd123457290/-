#include "Player.h"

#include "AssetPaths.h"
#include "CombatTuning.h"
#include "Input.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kManaRegenerationPerSecond = 10.0f;
    constexpr int kCounterDamage = 15;
    constexpr float kCounterEffectDuration = 0.24f;
    constexpr float kBlockActiveDuration = 0.30f;
    constexpr float kBlockCooldownDuration = 0.60f;
    constexpr float kBlockInputBufferDuration = 0.12f;
    constexpr float kBlockGraceDuration = 0.06f;
    constexpr float kAttackRecoveryMoveScale = 0.95f;
    constexpr float kBasicAttackDuration = CombatTuning::BasicAttackDurationSeconds;
    constexpr float kUltimateVfxCenterYOffset = 20.0f;
    constexpr float kBatFlightSpeed = 560.0f;
    constexpr float kBatMaximumTravelDistance = 360.0f;
    constexpr float kBatPickupRadius = 46.0f;
    constexpr float kBatDropDuration = 0.18f;
    constexpr float kUltimateInputBufferDuration = 0.45f;

    Vec2 DirectionVector(CharacterDirection direction)
    {
        switch (direction)
        {
        case CharacterDirection::Left:
            return { -1.0f, 0.0f };
        case CharacterDirection::Right:
            return { 1.0f, 0.0f };
        case CharacterDirection::Up:
            return { 0.0f, -1.0f };
        case CharacterDirection::Down:
        default:
            return { 0.0f, 1.0f };
        }
    }

}

// 加载角色动画与终结技图标；任何图片失败时绘制函数都有几何占位回退。
void Player::LoadContent()
{
    sprites_.LoadQiongSprites();

    const std::wstring iconPath = FindAssetPath(
        L"角色建模\\穹\\first_style_animation_bat\\combat_animation\\side_only_v3\\icons\\qiong_finisher_icon_64.png");
    auto icon = std::make_unique<Gdiplus::Image>(iconPath.c_str());
    if (icon != nullptr && icon->GetLastStatus() == Gdiplus::Ok)
    {
        ultimateIcon_ = std::move(icon);
    }
    else
    {
        ultimateIcon_.reset();
    }
}

void Player::UnloadContent()
{
    ultimateIcon_.reset();
    sprites_.Clear();
}

// 重置一局内全部数值、输入沿、动作计时和一次性事件，同时保留已加载资源。
void Player::ResetTrainingMode(RECT clientRect)
{
    const float width = static_cast<float>(clientRect.right - clientRect.left);
    const float height = static_cast<float>(clientRect.bottom - clientRect.top);
    position_ = { width * 0.5f, height * 0.5f };
    velocity_ = {};
    isMoving_ = false;
    isCrouching_ = false;
    isBlocking_ = false;
    isAttacking_ = false;
    isThrowing_ = false;
    isUltimate_ = false;
    deathStarted_ = false;
    attackWasDown_ = IsAttackKeyDown();
    attackBuffered_ = false;
    blockWasDown_ = IsBlockKeyDown();
    blockRequested_ = false;
    throwRequested_ = false;
    ultimateRequested_ = false;
    ultimateActivationStarted_ = false;
    attackHitPending_ = false;
    health_ = maxHealth_;
    mana_ = maxMana_;
    hasBat_ = true;
    batState_ = BatWorldState::Held;
    batPosition_ = position_;
    batDirection_ = {};
    batTravelDistance_ = 0.0f;
    batRotationDegrees_ = 0.0f;
    batDropTimer_ = 0.0f;
    superBatThrowActive_ = false;
    superBatProjectileActive_ = false;
    superBatExplosionPending_ = false;
    superBatExplosionActive_ = false;
    superBatExplosionElapsed_ = 0.0f;
    superBatVisualTimer_ = 0.0f;
    superBatExplosionPosition_ = position_;
    superBatExplosionDirection_ = { 1.0f, 0.0f };
    blockActiveTimer_ = 0.0f;
    blockCooldownTimer_ = 0.0f;
    blockInputBufferTimer_ = 0.0f;
    blockGraceTimer_ = 0.0f;
    counterEffectTimer_ = 0.0f;
    counterEffectTarget_ = position_;
    directionConfusionTimer_ = 0.0f;
    invertHorizontalInput_ = false;
    lightningMarked_ = false;
    movementLockTimer_ = 0.0f;
    statusRandomState_ = 0x9e3779b9u;
    ultimateElapsed_ = 0.0f;
    ultimateCooldownTimer_ = 0.0f;
    ultimateInputBufferTimer_ = 0.0f;
    ultimateKillCount_ = 0;
    ultimateBuffActive_ = false;
    ultimateEmpoweredAttackCount_ = 0;
    activeEmpoweredAttackStage_ = -1;
    empoweredAttackElapsed_ = 0.0f;
    pendingUltimateHitMask_ = 0;
    direction_ = CharacterDirection::Right;
    aimDirection_ = CharacterDirection::Right;
    ultimateAimDirection_ = CharacterDirection::Right;
    sprites_.ResetState();
    sprites_.SetHasBat(true);
    sprites_.EndUltimateVfx();
    hitEffect_.Reset();
    ClampToClient(clientRect);
}

// 玩家主状态机。先衰减计时器/读取输入，再按动作优先级决定本帧可否移动或出招，
// 最后推进精灵时间轴并把命中帧转换为待消费战斗事件。
void Player::Update(float deltaSeconds, RECT clientRect)
{
    if (deathStarted_)
    {
        attackWasDown_ = IsAttackKeyDown();
        blockWasDown_ = IsBlockKeyDown();
        blockRequested_ = false;
        throwRequested_ = false;
        ultimateRequested_ = false;
        velocity_ = {};
        isMoving_ = false;
        hitEffect_.Update(deltaSeconds);
        sprites_.Update(deltaSeconds, CharacterAnimation::Death);
        return;
    }

    const bool attackDown = IsAttackKeyDown();
    const bool attackPressed = attackDown && !attackWasDown_;
    attackWasDown_ = attackDown;
    const bool ultimateRequestedThisFrame = ultimateRequested_;
    ultimateRequested_ = false;
    const bool throwPressed = throwRequested_;
    throwRequested_ = false;
    const bool crouchDown = IsCrouchKeyDown();
    directionConfusionTimer_ = (std::max)(0.0f, directionConfusionTimer_ - deltaSeconds);
    movementLockTimer_ = (std::max)(0.0f, movementLockTimer_ - deltaSeconds);
    Vec2 directionalInput = movementLockTimer_ > 0.0f ? Vec2{} : GetMoveInput();
    if (directionConfusionTimer_ > 0.0f)
    {
        if (invertHorizontalInput_)
        {
            directionalInput.x = -directionalInput.x;
        }
        else
        {
            directionalInput.y = -directionalInput.y;
        }
    }
    const bool blockDown = IsBlockKeyDown();
    const bool blockPressed = blockRequested_ || (blockDown && !blockWasDown_);
    blockRequested_ = false;
    blockWasDown_ = blockDown;

    const float previousBlockActive = blockActiveTimer_;
    blockActiveTimer_ = (std::max)(0.0f, blockActiveTimer_ - deltaSeconds);
    blockCooldownTimer_ = (std::max)(0.0f, blockCooldownTimer_ - deltaSeconds);
    blockInputBufferTimer_ = (std::max)(0.0f, blockInputBufferTimer_ - deltaSeconds);
    blockGraceTimer_ = (std::max)(0.0f, blockGraceTimer_ - deltaSeconds);
    ultimateCooldownTimer_ = (std::max)(0.0f, ultimateCooldownTimer_ - deltaSeconds);
    ultimateInputBufferTimer_ = (std::max)(0.0f, ultimateInputBufferTimer_ - deltaSeconds);
    if (ultimateRequestedThisFrame)
    {
        ultimateInputBufferTimer_ = kUltimateInputBufferDuration;
    }
    const bool ultimatePressed = ultimateInputBufferTimer_ > 0.0f;

    if (blockPressed)
    {
        blockInputBufferTimer_ = kBlockInputBufferDuration;
    }
    if (isBlocking_ && previousBlockActive > 0.0f && blockActiveTimer_ <= 0.0f)
    {
        isBlocking_ = false;
        blockGraceTimer_ = kBlockGraceDuration;
    }

    if (!isBlocking_ && !isAttacking_ && !isThrowing_ && !isUltimate_)
    {
        UpdateDirection(directionalInput);
    }

    const bool canCancelAttackIntoBlock = isAttacking_ && !sprites_.IsAttackMovementLocked();
    if (blockInputBufferTimer_ > 0.0f && blockCooldownTimer_ <= 0.0f && !isBlocking_ && !isThrowing_ && !isUltimate_ &&
        (!isAttacking_ || canCancelAttackIntoBlock) && !crouchDown)
    {
        isBlocking_ = true;
        isAttacking_ = false;
        isCrouching_ = false;
        attackBuffered_ = false;
        attackHitPending_ = false;
        EndEmpoweredAttack();
        blockInputBufferTimer_ = 0.0f;
        blockGraceTimer_ = 0.0f;
        blockActiveTimer_ = kBlockActiveDuration;
        blockCooldownTimer_ = kBlockCooldownDuration;
        sprites_.StartBlock();
    }

    if (isAttacking_ && attackPressed && sprites_.CanBufferAttack(deltaSeconds))
    {
        attackBuffered_ = true;
    }

    if (!isBlocking_ && !isAttacking_ && !isThrowing_ && !isUltimate_ && !ultimateBuffActive_ && !crouchDown && ultimatePressed &&
        IsUltimateUnlocked() && hasBat_ && mana_ >= CombatTuning::UltimateEnergyCost && ultimateCooldownTimer_ <= 0.0f)
    {
        ultimateInputBufferTimer_ = 0.0f;
        StartUltimate();
    }
    else if (!isBlocking_ && !isAttacking_ && !isThrowing_ && !isUltimate_ && !crouchDown && throwPressed &&
        hasBat_ && (ultimateBuffActive_ || mana_ >= CombatTuning::ThrowEnergyCost))
    {
        isThrowing_ = true;
        superBatThrowActive_ = ultimateBuffActive_;
        if (superBatThrowActive_)
        {
            ultimateBuffActive_ = false;
            activeEmpoweredAttackStage_ = -1;
            empoweredAttackElapsed_ = 0.0f;
            sprites_.EndUltimateVfx();
        }
        else
        {
            mana_ -= CombatTuning::ThrowEnergyCost;
        }
        sprites_.StartThrow();
    }
    else if (!isBlocking_ && !isAttacking_ && !isThrowing_ && !isUltimate_ && !crouchDown && attackPressed)
    {
        BeginBasicAttack();
    }
    if (isUltimate_ && attackPressed)
    {
        attackBuffered_ = true;
    }

    isCrouching_ = !isBlocking_ && !isAttacking_ && !isThrowing_ && !isUltimate_ && crouchDown;
    const bool attackMovementLocked = isAttacking_ && sprites_.IsAttackMovementLocked();
    const Vec2 input = (isBlocking_ || attackMovementLocked || isThrowing_ || isUltimate_ || isCrouching_) ? Vec2{} : directionalInput;
    const bool hasInput = input.x != 0.0f || input.y != 0.0f;
    const float acceleration = hasInput ? 1800.0f : 2400.0f;
    const float movementScale = isAttacking_ ? kAttackRecoveryMoveScale : 1.0f;
    const Vec2 targetVelocity{ input.x * speed_ * movementScale, input.y * speed_ * movementScale };
    velocity_.x = MoveToward(velocity_.x, targetVelocity.x, acceleration * deltaSeconds);
    velocity_.y = MoveToward(velocity_.y, targetVelocity.y, acceleration * deltaSeconds);
    if (isBlocking_ || attackMovementLocked || isThrowing_ || isUltimate_ || isCrouching_)
    {
        velocity_ = {};
    }
    isMoving_ = std::abs(velocity_.x) > 1.0f || std::abs(velocity_.y) > 1.0f;

    position_.x += velocity_.x * deltaSeconds;
    position_.y += velocity_.y * deltaSeconds;
    ClampToClient(clientRect);
    superBatVisualTimer_ += deltaSeconds;
    if (superBatExplosionActive_)
    {
        superBatExplosionElapsed_ = (std::min)(
            CombatTuning::SuperBatExplosionDurationSeconds,
            superBatExplosionElapsed_ + deltaSeconds);
        if (superBatExplosionElapsed_ >= CombatTuning::SuperBatExplosionDurationSeconds)
        {
            superBatExplosionActive_ = false;
        }
    }
    UpdateThrownBat(deltaSeconds, clientRect);

    hitEffect_.Update(deltaSeconds);
    counterEffectTimer_ = (std::max)(0.0f, counterEffectTimer_ - deltaSeconds);
    mana_ = (std::min)(maxMana_, mana_ + kManaRegenerationPerSecond * deltaSeconds);

    if (isUltimate_)
    {
        velocity_ = {};
        UpdateUltimate(deltaSeconds);
    }
    else if (isBlocking_)
    {
        velocity_ = {};
        sprites_.Update(deltaSeconds, CharacterAnimation::Block);
    }
    else if (isThrowing_)
    {
        velocity_ = {};
        sprites_.Update(deltaSeconds, CharacterAnimation::Throw);
        if (sprites_.ConsumeThrowReleaseFrame())
        {
            ReleaseBat();
        }
        if (sprites_.IsAnimationFinished())
        {
            isThrowing_ = false;
            sprites_.Update(0.0f, CharacterAnimation::Idle);
        }
    }
    else if (isAttacking_)
    {
        if (activeEmpoweredAttackStage_ >= 0)
        {
            empoweredAttackElapsed_ = (std::min)(
                kBasicAttackDuration,
                empoweredAttackElapsed_ + deltaSeconds);
        }
        if (attackMovementLocked)
        {
            velocity_ = {};
        }
        sprites_.Update(deltaSeconds, CharacterAnimation::Attack);
        if (sprites_.ConsumeAttackHitFrame())
        {
            if (activeEmpoweredAttackStage_ >= 0)
            {
                pendingUltimateHitMask_ |= 1u << static_cast<unsigned int>(activeEmpoweredAttackStage_);
                ultimateEmpoweredAttackCount_ = (std::min)(
                    CombatTuning::UltimateHitCount,
                    ultimateEmpoweredAttackCount_ + 1);
            }
            else
            {
                attackHitPending_ = true;
            }
        }
        if (sprites_.IsAnimationFinished())
        {
            if (attackBuffered_)
            {
                attackBuffered_ = false;
                EndEmpoweredAttack();
                BeginBasicAttack();
            }
            else
            {
                isAttacking_ = false;
                EndEmpoweredAttack();
                sprites_.Update(0.0f, CharacterAnimation::Idle);
            }
        }
    }
    else
    {
        sprites_.Update(
            deltaSeconds,
            isCrouching_ ? CharacterAnimation::Crouch : (isMoving_ ? CharacterAnimation::Walk : CharacterAnimation::Idle));
    }
}

// 围绕客户区中心等比例搬移玩家、球棒和爆炸位置，并再次限制到新边界内。
void Player::ResizeToClient(RECT oldClientRect, RECT newClientRect)
{
    const float oldWidth = static_cast<float>(oldClientRect.right - oldClientRect.left);
    const float oldHeight = static_cast<float>(oldClientRect.bottom - oldClientRect.top);
    const float newWidth = static_cast<float>(newClientRect.right - newClientRect.left);
    const float newHeight = static_cast<float>(newClientRect.bottom - newClientRect.top);
    if (oldWidth <= 0.0f || oldHeight <= 0.0f || newWidth <= 0.0f || newHeight <= 0.0f)
    {
        return;
    }

    position_.x += (newWidth - oldWidth) * 0.5f;
    position_.y += (newHeight - oldHeight) * 0.5f;
    if (batState_ != BatWorldState::Held)
    {
        batPosition_.x += (newWidth - oldWidth) * 0.5f;
        batPosition_.y += (newHeight - oldHeight) * 0.5f;
        batPosition_.x = Clamp(batPosition_.x, 18.0f, newWidth - 18.0f);
        batPosition_.y = Clamp(batPosition_.y, 18.0f, newHeight - 18.0f);
    }
    if (superBatExplosionActive_)
    {
        superBatExplosionPosition_.x += (newWidth - oldWidth) * 0.5f;
        superBatExplosionPosition_.y += (newHeight - oldHeight) * 0.5f;
    }
    hitEffect_.Translate({ (newWidth - oldWidth) * 0.5f, (newHeight - oldHeight) * 0.5f });
    ClampToClient(newClientRect);
}

// 把攻击开始时锁定的方向、武器射程和伤害打包，随后立即清除 pending。
bool Player::ConsumeAttackHit(BasicAttackHitEvent& event)
{
    if (!attackHitPending_)
    {
        return false;
    }

    attackHitPending_ = false;
    event.origin = position_;
    event.direction = DirectionVector(aimDirection_);
    event.reach = hasBat_ ? CombatTuning::BatAttackReach : CombatTuning::PunchAttackReach;
    event.damage = GetAttackDamage();
    return true;
}

// 爆炸伤害事件与持续绘制的 explosionActive 分离，因此只会结算一次。
bool Player::ConsumeSuperBatExplosion(SuperBatExplosionEvent& event)
{
    if (!superBatExplosionPending_)
    {
        return false;
    }

    superBatExplosionPending_ = false;
    event.center = superBatExplosionPosition_;
    event.direction = superBatExplosionDirection_;
    event.radius = CombatTuning::SuperBatExplosionRadius;
    event.damage = CombatTuning::SuperBatExplosionDamage;
    return true;
}

// 从位掩码中依次取出五段连击事件；每个 bit 清除后不能再次消费。
bool Player::ConsumeUltimateHit(UltimateHitEvent& event)
{
    for (int index = 0; index < CombatTuning::UltimateHitCount; ++index)
    {
        const std::uint32_t bit = 1u << static_cast<unsigned int>(index);
        if ((pendingUltimateHitMask_ & bit) == 0)
        {
            continue;
        }

        pendingUltimateHitMask_ &= ~bit;
        event.origin = position_;
        event.direction = DirectionVector(ultimateAimDirection_);
        event.radius = CombatTuning::UltimateRadii[static_cast<std::size_t>(index)];
        event.damage = CombatTuning::UltimateDamages[static_cast<std::size_t>(index)];
        event.comboIndex = index;
        event.finisher = index == CombatTuning::UltimateHitCount - 1;
        return true;
    }
    return false;
}

// 仅 Flying 状态参与命中检测，Dropping/Grounded 只作为可拾取的世界物体。
bool Player::GetThrownBatHitPoint(Vec2& hitPosition) const
{
    if (batState_ != BatWorldState::Flying)
    {
        return false;
    }

    hitPosition = batPosition_;
    return true;
}

// 普通球棒命中后落地；强化球棒命中后立即在碰撞点触发范围爆炸。
void Player::ResolveThrownBatHit()
{
    if (batState_ != BatWorldState::Flying)
    {
        return;
    }

    if (superBatProjectileActive_)
    {
        TriggerSuperBatExplosion(batPosition_);
        return;
    }

    batState_ = BatWorldState::Dropping;
    batRotationDegrees_ = 18.0f;
    batDropTimer_ = kBatDropDuration;
}

bool Player::IsSuperBatProjectile() const
{
    return superBatProjectileActive_ && batState_ == BatWorldState::Flying;
}

int Player::GetAttackDamage() const
{
    return hasBat_ ? 10 : 5;
}

int Player::GetThrowDamage() const
{
    return CombatTuning::ThrowDamage;
}

int Player::GetCounterDamage() const
{
    return kCounterDamage;
}

int Player::GetHealth() const
{
    return health_;
}

int Player::GetMaxHealth() const
{
    return maxHealth_;
}

float Player::GetMana() const
{
    return mana_;
}

float Player::GetMaxMana() const
{
    return maxMana_;
}

Vec2 Player::GetPosition() const
{
    return position_;
}

float Player::GetCollisionRadius() const
{
    return collisionRadius_;
}

bool Player::HasBat() const
{
    return hasBat_;
}

bool Player::IsUltimateUnlocked() const
{
    return ultimateKillCount_ >= CombatTuning::UltimateUnlockKillCount;
}

int Player::GetUltimateKillCount() const
{
    return ultimateKillCount_;
}

bool Player::IsDirectionConfused() const
{
    return directionConfusionTimer_ > 0.0f;
}

bool Player::HasLightningMark() const
{
    return lightningMarked_;
}

bool Player::IsMovementLocked() const
{
    return movementLockTimer_ > 0.0f;
}

bool Player::IsDeathAnimationFinished() const
{
    return deathStarted_ && sprites_.IsAnimationFinished();
}

// 绘制顺序保证球棒/光环位于正确层级，并在受击时应用短暂水平抖动和闪白。
void Player::Draw(Gdiplus::Graphics& graphics) const
{
    const float shakeX = hitEffect_.GetShakeX();
    if (sprites_.IsLoaded())
    {
        if (deathStarted_)
        {
            const float actorBottomY = position_.y + collisionRadius_ + 34.0f;
            sprites_.Draw(
                graphics,
                position_.x + shakeX,
                actorBottomY,
                CharacterAnimation::Death,
                direction_,
                direction_,
                hitEffect_.IsFlashVisible());
            hitEffect_.Draw(graphics);
            return;
        }

        const bool empoweredSwing = isAttacking_ && activeEmpoweredAttackStage_ >= 0;
        const CharacterAnimation actorAnimation = isBlocking_
            ? CharacterAnimation::Block
            : isThrowing_
            ? CharacterAnimation::Throw
            : isAttacking_
            ? CharacterAnimation::Attack
            : (isCrouching_ ? CharacterAnimation::Crouch : (isMoving_ ? CharacterAnimation::Walk : CharacterAnimation::Idle));
        const CharacterDirection visualDirection =
            (isUltimate_ || empoweredSwing) &&
                (ultimateAimDirection_ == CharacterDirection::Left || ultimateAimDirection_ == CharacterDirection::Right)
            ? ultimateAimDirection_
            : direction_;
        const CharacterDirection actionDirection = (isUltimate_ || empoweredSwing) ? ultimateAimDirection_ : aimDirection_;

        if (isUltimate_)
        {
            sprites_.DrawUltimateVfx(
                graphics,
                position_.x + shakeX,
                position_.y + kUltimateVfxCenterYOffset,
                -1,
                Clamp(ultimateElapsed_ / CombatTuning::UltimateActivationSeconds, 0.0f, 1.0f));
        }
        else if (empoweredSwing)
        {
            sprites_.DrawUltimateVfx(
                graphics,
                position_.x + shakeX,
                position_.y + kUltimateVfxCenterYOffset,
                activeEmpoweredAttackStage_,
                Clamp(empoweredAttackElapsed_ / kBasicAttackDuration, 0.0f, 1.0f));
        }

        const float actorBottomY = position_.y + collisionRadius_ + 34.0f;
        bool drewSpecialActor = false;
        if (isUltimate_)
        {
            drewSpecialActor = sprites_.DrawUltimateActivationActor(
                graphics,
                position_.x + shakeX,
                actorBottomY,
                ultimateAimDirection_,
                Clamp(ultimateElapsed_ / CombatTuning::UltimateActivationSeconds, 0.0f, 1.0f));
        }
        else if (isThrowing_ && superBatThrowActive_)
        {
            drewSpecialActor = sprites_.DrawSuperThrowActor(
                graphics,
                position_.x + shakeX,
                actorBottomY,
                aimDirection_);
        }

        if (!drewSpecialActor)
        {
            sprites_.Draw(
                graphics,
                position_.x + shakeX,
                actorBottomY,
                actorAnimation,
                visualDirection,
                actionDirection,
                hitEffect_.IsFlashVisible());
        }
        // 大招强化仍保留伤害和连段机制，但不再给球棒叠加持续发光光环。
        DrawBat(graphics);
        DrawSuperBatExplosion(graphics);
        hitEffect_.Draw(graphics);
        DrawCounterEffect(graphics);
        DrawStatusEffects(graphics);
        return;
    }

    DrawFallback(graphics);
    DrawBat(graphics);
    DrawSuperBatExplosion(graphics);
    hitEffect_.Draw(graphics);
    DrawCounterEffect(graphics);
    DrawStatusEffects(graphics);
}

// 绘制生命、能量、击杀解锁进度、操作提示和技能冷却，不改变任何游戏状态。
void Player::DrawHud(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    const float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
    // 与左下角终结技图标并排形成统一玩家 HUD，避免覆盖顶部 Boss 头像和血条。
    constexpr float x = 96.0f;
    const float y = (std::max)(8.0f, clientHeight - 100.0f);
    constexpr float panelWidth = 226.0f;
    constexpr float panelHeight = 82.0f;
    constexpr float barX = x + 42.0f;
    constexpr float barWidth = 164.0f;
    constexpr float barHeight = 13.0f;

    Gdiplus::SolidBrush shadow(Gdiplus::Color(75, 32, 25, 22));
    Gdiplus::SolidBrush panel(Gdiplus::Color(226, 250, 244, 224));
    Gdiplus::SolidBrush track(Gdiplus::Color(255, 67, 58, 55));
    Gdiplus::SolidBrush hp(Gdiplus::Color(255, 216, 54, 54));
    Gdiplus::SolidBrush hpLight(Gdiplus::Color(255, 255, 104, 82));
    Gdiplus::SolidBrush energy(Gdiplus::Color(255, 22, 116, 226));
    Gdiplus::SolidBrush energyLight(Gdiplus::Color(255, 78, 224, 255));
    Gdiplus::SolidBrush text(Gdiplus::Color(255, 48, 41, 38));
    Gdiplus::Pen edge(Gdiplus::Color(220, 76, 58, 46), 2.0f);
    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font labelFont(&family, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font valueFont(&family, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    graphics.FillRectangle(&shadow, x + 4.0f, y + 5.0f, panelWidth, panelHeight);
    graphics.FillRectangle(&panel, x, y, panelWidth, panelHeight);
    graphics.DrawRectangle(&edge, x, y, panelWidth, panelHeight);

    const float hpRatio = Clamp(static_cast<float>(health_) / static_cast<float>(maxHealth_), 0.0f, 1.0f);
    const float energyRatio = Clamp(mana_ / maxMana_, 0.0f, 1.0f);
    graphics.DrawString(L"HP", -1, &labelFont, Gdiplus::PointF(x + 12.0f, y + 12.0f), &text);
    graphics.FillRectangle(&track, barX, y + 13.0f, barWidth, barHeight);
    graphics.FillRectangle(&hp, barX, y + 13.0f, barWidth * hpRatio, barHeight);
    graphics.FillRectangle(&hpLight, barX, y + 15.0f, barWidth * hpRatio, 4.0f);

    graphics.DrawString(L"ULT", -1, &labelFont, Gdiplus::PointF(x + 8.0f, y + 41.0f), &text);
    graphics.FillRectangle(&track, barX, y + 42.0f, barWidth, barHeight);
    graphics.FillRectangle(&energy, barX, y + 42.0f, barWidth * energyRatio, barHeight);
    graphics.FillRectangle(&energyLight, barX, y + 44.0f, barWidth * energyRatio, 4.0f);

    wchar_t hpText[32]{};
    wsprintfW(hpText, L"%d / %d", health_, maxHealth_);
    graphics.DrawString(hpText, -1, &valueFont, Gdiplus::PointF(barX + 102.0f, y + 10.0f), &text);
    wchar_t energyText[64]{};
    wsprintfW(
        energyText,
        L"%d/%d  Q:%d  P:%d",
        static_cast<int>(mana_),
        static_cast<int>(maxMana_),
        static_cast<int>(CombatTuning::ThrowEnergyCost),
        static_cast<int>(CombatTuning::UltimateEnergyCost));
    graphics.DrawString(energyText, -1, &valueFont, Gdiplus::PointF(barX + 48.0f, y + 39.0f), &text);

    DrawUltimateSkillIcon(graphics, clientRect);
}

// 格挡只在 active/grace 窗口内生效；成功时保留生命并记录反击特效目标。
bool Player::TakeDamage(int damage, Vec2 sourcePosition, HostileStatusEffect statusEffect)
{
    if (damage <= 0 || health_ <= 0)
    {
        return false;
    }

    if (isBlocking_ || blockGraceTimer_ > 0.0f)
    {
        UpdateDirection({ sourcePosition.x - position_.x, sourcePosition.y - position_.y });
        counterEffectTarget_ = sourcePosition;
        counterEffectTimer_ = kCounterEffectDuration;
        blockGraceTimer_ = 0.0f;
        return true;
    }

    int finalDamage = damage;
    if (lightningMarked_)
    {
        // 紫色闪电标记由“下一次命中”引爆：追加伤害并锁定移动 2 秒。伤害
        // 结算发生在本帧 Player::Update 之后，因此必须立即清空惯性；否则角色
        // 仍会漂移十余像素，看起来像定身没有及时生效。
        finalDamage += 15;
        lightningMarked_ = false;
        movementLockTimer_ = 2.0f;
        velocity_ = {};
        isMoving_ = false;
    }

    health_ -= finalDamage;
    if (health_ < 0)
    {
        health_ = 0;
    }
    hitEffect_.Trigger(sourcePosition, position_, 34.0f);
    if (statusEffect == HostileStatusEffect::DirectionConfusion)
    {
        directionConfusionTimer_ = 5.0f;
        statusRandomState_ = statusRandomState_ * 1664525u + 1013904223u;
        invertHorizontalInput_ = (statusRandomState_ & 1u) != 0;
    }
    else if (statusEffect == HostileStatusEffect::LightningMark)
    {
        lightningMarked_ = true;
    }
    if (health_ == 0)
    {
        StartDeath(sourcePosition);
    }
    return false;
}

void Player::StartDeath(Vec2 sourcePosition)
{
    if (deathStarted_)
    {
        return;
    }

    UpdateDirection({ sourcePosition.x - position_.x, sourcePosition.y - position_.y });
    deathStarted_ = true;
    velocity_ = {};
    isMoving_ = false;
    isCrouching_ = false;
    isBlocking_ = false;
    isAttacking_ = false;
    isThrowing_ = false;
    isUltimate_ = false;
    attackBuffered_ = false;
    attackHitPending_ = false;
    pendingUltimateHitMask_ = 0;
    ultimateActivationStarted_ = false;
    ultimateBuffActive_ = false;
    activeEmpoweredAttackStage_ = -1;
    empoweredAttackElapsed_ = 0.0f;
    superBatThrowActive_ = false;
    superBatProjectileActive_ = false;
    superBatExplosionPending_ = false;
    superBatExplosionActive_ = false;
    hasBat_ = true;
    batState_ = BatWorldState::Held;
    batPosition_ = position_;
    sprites_.SetHasBat(true);
    sprites_.EndUltimateVfx();
    sprites_.StartDeath();
}

// 治疗值限制在最大生命，零/负数请求直接忽略。
void Player::Heal(int amount)
{
    if (amount <= 0 || health_ <= 0)
    {
        return;
    }

    health_ = (std::min)(maxHealth_, health_ + amount);
}

void Player::RequestUltimate()
{
    ultimateRequested_ = true;
}

void Player::RequestThrow()
{
    throwRequested_ = true;
}

void Player::RequestBlock()
{
    blockRequested_ = true;
}

// 累计击杀达到阈值后永久解锁本局终结技；额外击杀不会突破所需上限显示。
void Player::RegisterMonsterKills(int count)
{
    if (count <= 0 || IsUltimateUnlocked())
    {
        return;
    }
    ultimateKillCount_ = (std::min)(
        CombatTuning::UltimateUnlockKillCount,
        ultimateKillCount_ + count);
}

bool Player::ConsumeUltimateActivationStarted()
{
    const bool started = ultimateActivationStarted_;
    ultimateActivationStarted_ = false;
    return started;
}

// 消耗能量、锁定启动朝向、清空旧连段位图，并启动独立的激活动画/语音事件。
void Player::StartUltimate()
{
    isUltimate_ = true;
    ultimateBuffActive_ = false;
    superBatThrowActive_ = false;
    superBatProjectileActive_ = false;
    ultimateEmpoweredAttackCount_ = 0;
    activeEmpoweredAttackStage_ = -1;
    empoweredAttackElapsed_ = 0.0f;
    isMoving_ = false;
    isCrouching_ = false;
    velocity_ = {};
    attackHitPending_ = false;
    attackBuffered_ = false;
    pendingUltimateHitMask_ = 0;
    ultimateElapsed_ = 0.0f;
    ultimateAimDirection_ = aimDirection_;
    mana_ = (std::max)(0.0f, mana_ - CombatTuning::UltimateEnergyCost);
    ultimateCooldownTimer_ = CombatTuning::UltimateCooldownSeconds;
    ultimateActivationStarted_ = true;
    sprites_.StartUltimateVfx(ultimateAimDirection_);
    sprites_.Update(0.0f, CharacterAnimation::Idle);
}

// 根据累计时间跨越的时间点置位五段命中 bit；即使掉帧跨过多段也不会漏事件。
void Player::UpdateUltimate(float deltaSeconds)
{
    ultimateElapsed_ = (std::min)(
        CombatTuning::UltimateActivationSeconds,
        ultimateElapsed_ + deltaSeconds);
    sprites_.Update(deltaSeconds, CharacterAnimation::Idle);

    if (ultimateElapsed_ >= CombatTuning::UltimateActivationSeconds)
    {
        isUltimate_ = false;
        ultimateBuffActive_ = true;
        sprites_.Update(0.0f, CharacterAnimation::Idle);
        if (attackBuffered_)
        {
            BeginBasicAttack();
        }
    }
}

// 开始普通或强化普攻，锁定本次动作方向并让 SpriteAnimation 从第 0 帧计时。
void Player::BeginBasicAttack()
{
    isAttacking_ = true;
    attackBuffered_ = false;
    activeEmpoweredAttackStage_ = -1;
    empoweredAttackElapsed_ = 0.0f;

    if (ultimateBuffActive_ && hasBat_ && ultimateEmpoweredAttackCount_ < CombatTuning::UltimateHitCount)
    {
        activeEmpoweredAttackStage_ = ultimateEmpoweredAttackCount_;
        ultimateAimDirection_ = aimDirection_;
        sprites_.StartUltimateVfx(ultimateAimDirection_);
    }
    sprites_.StartAttack();
}

// 强化连段结束时推进阶段；完成规定次数后关闭增益并恢复普通攻击。
void Player::EndEmpoweredAttack()
{
    activeEmpoweredAttackStage_ = -1;
    empoweredAttackElapsed_ = 0.0f;
    if (ultimateEmpoweredAttackCount_ >= CombatTuning::UltimateHitCount)
    {
        ultimateBuffActive_ = false;
        sprites_.EndUltimateVfx();
    }
}

// 技能图标叠层同时表达未解锁、能量不足、冷却中和可释放四种状态。
void Player::DrawUltimateSkillIcon(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    constexpr float size = 64.0f;
    const float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
    const float x = 18.0f;
    const float y = (std::max)(8.0f, clientHeight - 88.0f);
    const bool unlocked = IsUltimateUnlocked();
    const bool ready = unlocked && hasBat_ && !isUltimate_ && !ultimateBuffActive_ && ultimateCooldownTimer_ <= 0.0f &&
        mana_ >= CombatTuning::UltimateEnergyCost;

    Gdiplus::SolidBrush shadow(Gdiplus::Color(115, 6, 12, 25));
    graphics.FillRectangle(&shadow, x + 4.0f, y + 5.0f, size, size);

    if (ultimateIcon_ != nullptr && ultimateIcon_->GetLastStatus() == Gdiplus::Ok)
    {
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.DrawImage(ultimateIcon_.get(), x, y, size, size);
    }
    else
    {
        Gdiplus::SolidBrush fallback(Gdiplus::Color(255, 20, 43, 77));
        Gdiplus::Pen batGlow(Gdiplus::Color(255, 70, 224, 255), 8.0f);
        graphics.FillRectangle(&fallback, x, y, size, size);
        graphics.DrawLine(&batGlow, x + 17.0f, y + 48.0f, x + 49.0f, y + 16.0f);
    }

    float chargeRatio = unlocked
        ? Clamp(mana_ / CombatTuning::UltimateEnergyCost, 0.0f, 1.0f)
        : Clamp(
            static_cast<float>(ultimateKillCount_) / static_cast<float>(CombatTuning::UltimateUnlockKillCount),
            0.0f,
            1.0f);
    if (isUltimate_ || ultimateBuffActive_)
    {
        chargeRatio = 1.0f;
    }
    else if (ultimateCooldownTimer_ > 0.0f)
    {
        chargeRatio = Clamp(
            1.0f - ultimateCooldownTimer_ / CombatTuning::UltimateCooldownSeconds,
            0.0f,
            1.0f);
    }
    if (!ready)
    {
        const float coveredHeight = size * (1.0f - chargeRatio);
        Gdiplus::SolidBrush cooldownShade(
            unlocked ? Gdiplus::Color(176, 4, 9, 19) : Gdiplus::Color(205, 7, 10, 18));
        graphics.FillRectangle(&cooldownShade, x, y, size, coveredHeight);
    }

    if (isUltimate_ || ultimateBuffActive_)
    {
        const float pulse = 0.5f + 0.5f * std::sin(superBatVisualTimer_ * 12.0f);
        const BYTE alpha = static_cast<BYTE>(150.0f + pulse * 105.0f);
        Gdiplus::Pen pulseRing(Gdiplus::Color(alpha, 80, 232, 255), 4.0f);
        graphics.DrawRectangle(&pulseRing, x - 3.0f, y - 3.0f, size + 6.0f, size + 6.0f);
    }

    Gdiplus::Pen border(
        ready
            ? Gdiplus::Color(255, 102, 240, 255)
            : (unlocked ? Gdiplus::Color(235, 86, 98, 120) : Gdiplus::Color(245, 224, 154, 58)),
        ready ? 3.0f : 2.0f);
    graphics.DrawRectangle(&border, x, y, size, size);

    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font keyFont(&family, 14.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font cooldownFont(&family, 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush keyBack(Gdiplus::Color(235, 7, 19, 39));
    Gdiplus::SolidBrush white(Gdiplus::Color(255, 244, 252, 255));
    graphics.FillEllipse(&keyBack, x - 6.0f, y + size - 19.0f, 25.0f, 25.0f);
    graphics.DrawString(L"P", -1, &keyFont, Gdiplus::PointF(x + 1.0f, y + size - 17.0f), &white);

    if (!unlocked)
    {
        wchar_t unlockText[16]{};
        wsprintfW(unlockText, L"%d / %d", ultimateKillCount_, CombatTuning::UltimateUnlockKillCount);
        graphics.DrawString(unlockText, -1, &cooldownFont, Gdiplus::PointF(x + 10.0f, y + 21.0f), &white);
    }
    else if (ultimateBuffActive_)
    {
        wchar_t empoweredText[16]{};
        wsprintfW(
            empoweredText,
            L"%d / %d",
            CombatTuning::UltimateHitCount - ultimateEmpoweredAttackCount_,
            CombatTuning::UltimateHitCount);
        graphics.DrawString(empoweredText, -1, &cooldownFont, Gdiplus::PointF(x + 10.0f, y + 21.0f), &white);
    }
    else if (ultimateCooldownTimer_ > 0.05f && !isUltimate_)
    {
        wchar_t cooldownText[16]{};
        wsprintfW(cooldownText, L"%d", static_cast<int>(std::ceil(ultimateCooldownTimer_)));
        graphics.DrawString(cooldownText, -1, &cooldownFont, Gdiplus::PointF(x + 18.0f, y + 22.0f), &white);
    }

    for (int index = 0; index < CombatTuning::UltimateHitCount; ++index)
    {
        const bool activeNode = !unlocked
            ? index < ultimateKillCount_
            : (isUltimate_ || (ultimateBuffActive_ && index >= ultimateEmpoweredAttackCount_));
        Gdiplus::SolidBrush node(
            activeNode ? Gdiplus::Color(255, 110, 244, 255) : Gdiplus::Color(210, 32, 68, 102));
        graphics.FillEllipse(&node, x + 8.0f + index * 11.0f, y + size + 6.0f, 7.0f, 7.0f);
    }
}

// 最小平移向量将玩家推出圆形障碍；中心重合时使用当前朝向作为推出方向。
void Player::ResolveCircleCollision(Vec2 obstacleCenter, float obstacleRadius)
{
    if (deathStarted_ || obstacleRadius <= 0.0f)
    {
        return;
    }

    const float dx = position_.x - obstacleCenter.x;
    const float dy = position_.y - obstacleCenter.y;
    const float minimumDistance = collisionRadius_ + obstacleRadius;
    const float distanceSquared = dx * dx + dy * dy;
    if (distanceSquared >= minimumDistance * minimumDistance)
    {
        return;
    }

    float normalX = 1.0f;
    float normalY = 0.0f;
    if (distanceSquared > 0.0001f)
    {
        const float distance = std::sqrt(distanceSquared);
        normalX = dx / distance;
        normalY = dy / distance;
    }

    position_.x = obstacleCenter.x + normalX * minimumDistance;
    position_.y = obstacleCenter.y + normalY * minimumDistance;

    const float inwardSpeed = velocity_.x * normalX + velocity_.y * normalY;
    if (inwardSpeed < 0.0f)
    {
        velocity_.x -= normalX * inwardSpeed;
        velocity_.y -= normalY * inwardSpeed;
    }
}

// 球棒状态机：Flying 累积射程，Dropping 播放落地延时，Grounded 等待玩家靠近拾取。
void Player::UpdateThrownBat(float deltaSeconds, RECT clientRect)
{
    if (batState_ == BatWorldState::Held)
    {
        batPosition_ = position_;
        return;
    }

    if (batState_ == BatWorldState::Flying)
    {
        const float flightSpeed = superBatProjectileActive_
            ? CombatTuning::SuperBatFlightSpeed
            : kBatFlightSpeed;
        const float maximumTravelDistance = superBatProjectileActive_
            ? CombatTuning::SuperBatMaximumTravelDistance
            : kBatMaximumTravelDistance;
        const float step = flightSpeed * deltaSeconds;
        batPosition_.x += batDirection_.x * step;
        batPosition_.y += batDirection_.y * step;
        batTravelDistance_ += step;
        batRotationDegrees_ += (superBatProjectileActive_ ? 1080.0f : 720.0f) * deltaSeconds;

        const float width = static_cast<float>(clientRect.right - clientRect.left);
        const float height = static_cast<float>(clientRect.bottom - clientRect.top);
        const bool reachedBoundary =
            batPosition_.x <= 18.0f || batPosition_.x >= width - 18.0f ||
            batPosition_.y <= 18.0f || batPosition_.y >= height - 18.0f;
        if (batTravelDistance_ >= maximumTravelDistance || reachedBoundary)
        {
            batPosition_.x = Clamp(batPosition_.x, 18.0f, width - 18.0f);
            batPosition_.y = Clamp(batPosition_.y, 18.0f, height - 18.0f);
            if (superBatProjectileActive_)
            {
                TriggerSuperBatExplosion(batPosition_);
                return;
            }
            batState_ = BatWorldState::Dropping;
            batRotationDegrees_ = 18.0f;
            batDropTimer_ = kBatDropDuration;
        }
        return;
    }

    if (batState_ == BatWorldState::Dropping)
    {
        batDropTimer_ -= deltaSeconds;
        if (batDropTimer_ <= 0.0f)
        {
            batDropTimer_ = 0.0f;
            batState_ = BatWorldState::Grounded;
        }
        return;
    }

    const float dx = position_.x - batPosition_.x;
    const float dy = position_.y - batPosition_.y;
    if (dx * dx + dy * dy <= kBatPickupRadius * kBatPickupRadius)
    {
        PickUpBat();
    }
}

// 在角色前方生成飞行球棒；强化状态决定速度/最大射程以及最终是否爆炸。
void Player::ReleaseBat()
{
    if (!hasBat_ || batState_ != BatWorldState::Held)
    {
        return;
    }

    batDirection_ = DirectionVector(aimDirection_);
    batPosition_ =
    {
        position_.x + batDirection_.x * CombatTuning::ThrowReleaseReach,
        position_.y + batDirection_.y * CombatTuning::ThrowReleaseReach
    };
    batTravelDistance_ = 0.0f;
    batRotationDegrees_ = 0.0f;
    batDropTimer_ = 0.0f;
    batState_ = BatWorldState::Flying;
    superBatProjectileActive_ = superBatThrowActive_;
    hasBat_ = false;
    sprites_.SetHasBat(false);
}

// 收回球棒并同步 SpriteAnimation 的持棒素材选择。
void Player::PickUpBat()
{
    batState_ = BatWorldState::Held;
    batPosition_ = position_;
    batDirection_ = {};
    batTravelDistance_ = 0.0f;
    batRotationDegrees_ = 0.0f;
    batDropTimer_ = 0.0f;
    superBatThrowActive_ = false;
    superBatProjectileActive_ = false;
    hasBat_ = true;
    sprites_.SetHasBat(true);
}

// 同时启动一次伤害事件和一段持续视觉动画，两者使用各自标志管理生命周期。
void Player::TriggerSuperBatExplosion(Vec2 position)
{
    if (!superBatProjectileActive_)
    {
        return;
    }

    superBatExplosionPosition_ = position;
    superBatExplosionDirection_ = batDirection_;
    superBatExplosionPending_ = true;
    superBatExplosionActive_ = true;
    superBatExplosionElapsed_ = 0.0f;
    superBatProjectileActive_ = false;
    superBatThrowActive_ = false;
    ultimateBuffActive_ = false;
    activeEmpoweredAttackStage_ = -1;
    empoweredAttackElapsed_ = 0.0f;
    batState_ = BatWorldState::Dropping;
    batRotationDegrees_ = 18.0f;
    batDropTimer_ = kBatDropDuration;
    sprites_.EndUltimateVfx();
}

// 优先绘制强化飞行物素材；普通/落地球棒用局部变换围绕中心旋转。
void Player::DrawBat(Gdiplus::Graphics& graphics) const
{
    if (batState_ == BatWorldState::Held)
    {
        return;
    }

    if (batState_ == BatWorldState::Flying && superBatProjectileActive_)
    {
        sprites_.DrawSuperBatProjectile(
            graphics,
            batPosition_.x,
            batPosition_.y,
            batDirection_,
            batRotationDegrees_);
        return;
    }

    if (batState_ == BatWorldState::Flying)
    {
        const float trailStartX = batPosition_.x - batDirection_.x * 54.0f;
        const float trailStartY = batPosition_.y - batDirection_.y * 54.0f;
        const float trailEndX = batPosition_.x - batDirection_.x * 12.0f;
        const float trailEndY = batPosition_.y - batDirection_.y * 12.0f;
        Gdiplus::Pen trailGlow(Gdiplus::Color(90, 70, 190, 255), 7.0f);
        Gdiplus::Pen trailCore(Gdiplus::Color(215, 235, 252, 255), 2.0f);
        Gdiplus::Pen hitRadius(Gdiplus::Color(72, 90, 220, 255), 1.5f);
        graphics.DrawLine(&trailGlow, trailStartX, trailStartY, trailEndX, trailEndY);
        graphics.DrawLine(&trailCore, trailStartX, trailStartY, trailEndX, trailEndY);
        graphics.DrawEllipse(
            &hitRadius,
            batPosition_.x - CombatTuning::ThrownBatHitRadius,
            batPosition_.y - CombatTuning::ThrownBatHitRadius,
            CombatTuning::ThrownBatHitRadius * 2.0f,
            CombatTuning::ThrownBatHitRadius * 2.0f);
    }

    const Gdiplus::GraphicsState state = graphics.Save();
    const float dropOffset = batState_ == BatWorldState::Dropping
        ? 24.0f * (batDropTimer_ / kBatDropDuration)
        : 0.0f;
    graphics.TranslateTransform(batPosition_.x, batPosition_.y - dropOffset);
    graphics.RotateTransform(batRotationDegrees_);
    Gdiplus::SolidBrush shadow(Gdiplus::Color(80, 35, 30, 27));
    Gdiplus::SolidBrush outline(Gdiplus::Color(255, 34, 31, 34));
    Gdiplus::SolidBrush metal(Gdiplus::Color(255, 125, 133, 148));
    Gdiplus::SolidBrush highlight(Gdiplus::Color(255, 218, 226, 234));
    Gdiplus::SolidBrush grip(Gdiplus::Color(255, 205, 151, 48));

    if (batState_ == BatWorldState::Grounded)
    {
        graphics.FillEllipse(&shadow, -23.0f, 8.0f, 46.0f, 10.0f);
    }
    graphics.FillRectangle(&outline, -25.0f, -5.0f, 50.0f, 10.0f);
    graphics.FillRectangle(&metal, -22.0f, -3.0f, 39.0f, 6.0f);
    graphics.FillRectangle(&highlight, -18.0f, -2.0f, 28.0f, 2.0f);
    graphics.FillRectangle(&grip, 16.0f, -4.0f, 11.0f, 8.0f);
    graphics.Restore(state);
}

// 将 elapsed 映射到 [0,1] 交给 SpriteAnimation 采样爆炸序列。
void Player::DrawSuperBatExplosion(Gdiplus::Graphics& graphics) const
{
    if (!superBatExplosionActive_)
    {
        return;
    }

    sprites_.DrawSuperBatExplosion(
        graphics,
        superBatExplosionPosition_.x,
        superBatExplosionPosition_.y,
        CombatTuning::SuperBatExplosionRadius,
        Clamp(
            superBatExplosionElapsed_ / CombatTuning::SuperBatExplosionDurationSeconds,
            0.0f,
            1.0f));
}

// 格挡反击的视觉连线从玩家指向来袭目标，强度随计时衰减。
void Player::DrawCounterEffect(Gdiplus::Graphics& graphics) const
{
    if (counterEffectTimer_ <= 0.0f)
    {
        return;
    }

    Vec2 direction = Normalize({ counterEffectTarget_.x - position_.x, counterEffectTarget_.y - position_.y });
    if (direction.x == 0.0f && direction.y == 0.0f)
    {
        direction = { 1.0f, 0.0f };
    }

    const float progress = 1.0f - counterEffectTimer_ / kCounterEffectDuration;
    const BYTE alpha = static_cast<BYTE>(Clamp((1.0f - progress) * 255.0f, 0.0f, 255.0f));
    const Vec2 perpendicular{ -direction.y, direction.x };
    const Vec2 start
    {
        position_.x + direction.x * 22.0f - perpendicular.x * 25.0f,
        position_.y + direction.y * 22.0f - perpendicular.y * 25.0f
    };
    const Vec2 end
    {
        counterEffectTarget_.x + perpendicular.x * 13.0f,
        counterEffectTarget_.y + perpendicular.y * 13.0f
    };

    Gdiplus::Pen ringGlow(Gdiplus::Color(static_cast<BYTE>(alpha / 2), 38, 190, 255), 7.0f);
    Gdiplus::Pen ringCore(Gdiplus::Color(alpha, 145, 238, 255), 2.0f);
    const float ringRadius = 28.0f + progress * 24.0f;
    graphics.DrawEllipse(
        &ringGlow,
        position_.x - ringRadius,
        position_.y - ringRadius,
        ringRadius * 2.0f,
        ringRadius * 2.0f);
    graphics.DrawEllipse(
        &ringCore,
        position_.x - ringRadius,
        position_.y - ringRadius,
        ringRadius * 2.0f,
        ringRadius * 2.0f);

    Gdiplus::Pen slashGlow(Gdiplus::Color(static_cast<BYTE>(alpha / 2), 32, 160, 255), 10.0f);
    Gdiplus::Pen slashCore(Gdiplus::Color(alpha, 255, 255, 255), 4.0f);
    slashGlow.SetStartCap(Gdiplus::LineCapRound);
    slashGlow.SetEndCap(Gdiplus::LineCapRound);
    slashCore.SetStartCap(Gdiplus::LineCapRound);
    slashCore.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawLine(&slashGlow, start.x, start.y, end.x, end.y);
    graphics.DrawLine(&slashCore, start.x, start.y, end.x, end.y);

    Gdiplus::SolidBrush spark(Gdiplus::Color(alpha, 102, 236, 255));
    const float sparkRadius = 8.0f * (1.0f - progress);
    graphics.FillEllipse(
        &spark,
        counterEffectTarget_.x - sparkRadius,
        counterEffectTarget_.y - sparkRadius,
        sparkRadius * 2.0f,
        sparkRadius * 2.0f);
}

void Player::DrawStatusEffects(Gdiplus::Graphics& graphics) const
{
    if (directionConfusionTimer_ > 0.0f)
    {
        // 半透明浅紫光覆盖全身，头顶双星沿圆周旋转表示方向混乱。
        const float pulse = 0.5f + std::sin(directionConfusionTimer_ * 8.0f) * 0.16f;
        Gdiplus::SolidBrush aura(Gdiplus::Color(static_cast<BYTE>(70.0f + pulse * 45.0f), 190, 126, 242));
        Gdiplus::Pen ring(Gdiplus::Color(190, 218, 170, 255), 3.0f);
        Gdiplus::SolidBrush star(Gdiplus::Color(255, 250, 224, 92));
        graphics.FillEllipse(&aura, position_.x - 35.0f, position_.y - 66.0f, 70.0f, 104.0f);
        graphics.DrawEllipse(&ring, position_.x - 30.0f, position_.y - 86.0f, 60.0f, 22.0f);
        const float angle = directionConfusionTimer_ * 6.0f;
        for (int index = 0; index < 2; ++index)
        {
            const float x = position_.x + std::cos(angle + index * 3.1415926f) * 26.0f;
            const float y = position_.y - 75.0f + std::sin(angle + index * 3.1415926f) * 8.0f;
            graphics.FillRectangle(&star, x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        }
    }

    if (lightningMarked_)
    {
        Gdiplus::Pen bolt(Gdiplus::Color(245, 190, 76, 255), 5.0f);
        const Gdiplus::PointF points[] =
        {
            { position_.x + 16.0f, position_.y - 83.0f },
            { position_.x + 4.0f, position_.y - 67.0f },
            { position_.x + 14.0f, position_.y - 67.0f },
            { position_.x + 2.0f, position_.y - 48.0f }
        };
        graphics.DrawLines(&bolt, points, 4);
    }

    if (movementLockTimer_ > 0.0f)
    {
        Gdiplus::Pen lockRing(Gdiplus::Color(220, 154, 82, 238), 4.0f);
        graphics.DrawEllipse(&lockRing, position_.x - 31.0f, position_.y + 17.0f, 62.0f, 18.0f);
    }
}

// 优先采用分量绝对值更大的轴决定四向朝向，避免斜向输入频繁抖动。
void Player::UpdateDirection(Vec2 input)
{
    if (input.x == 0.0f && input.y == 0.0f)
    {
        return;
    }

    if (std::abs(input.y) >= std::abs(input.x))
    {
        aimDirection_ = input.y < 0.0f ? CharacterDirection::Up : CharacterDirection::Down;
    }
    else
    {
        aimDirection_ = input.x < 0.0f ? CharacterDirection::Left : CharacterDirection::Right;
    }

    if (input.x < 0.0f)
    {
        direction_ = CharacterDirection::Left;
    }
    else if (input.x > 0.0f)
    {
        direction_ = CharacterDirection::Right;
    }
}

// 按碰撞半径和角色视觉高度保留边距，确保脚底与身体不会移出客户区。
void Player::ClampToClient(RECT clientRect)
{
    const float width = static_cast<float>(clientRect.right - clientRect.left);
    const float height = static_cast<float>(clientRect.bottom - clientRect.top);
    if (width <= collisionRadius_ * 2.0f)
    {
        position_.x = width * 0.5f;
    }
    else
    {
        position_.x = Clamp(position_.x, collisionRadius_, width - collisionRadius_);
    }

    if (height <= collisionRadius_ * 2.0f)
    {
        position_.y = height * 0.5f;
    }
    else
    {
        position_.y = Clamp(position_.y, collisionRadius_, height - collisionRadius_);
    }
}

void Player::DrawFallback(Gdiplus::Graphics& graphics) const
{
    Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(120, 28, 31, 36));
    Gdiplus::SolidBrush bodyBrush(Gdiplus::Color(255, 238, 238, 241));
    Gdiplus::SolidBrush coatBrush(Gdiplus::Color(255, 45, 47, 54));
    Gdiplus::SolidBrush eyeBrush(Gdiplus::Color(255, 242, 174, 34));
    Gdiplus::Pen outlinePen(Gdiplus::Color(255, 20, 20, 24), 3.0f);

    const float x = position_.x;
    const float y = position_.y;
    const float r = collisionRadius_;

    graphics.FillEllipse(&shadowBrush, x - r + 5.0f, y - r + 8.0f, r * 2.0f, r * 2.0f);
    graphics.DrawEllipse(&outlinePen, x - r, y - r, r * 2.0f, r * 2.0f);
    graphics.FillEllipse(&bodyBrush, x - r, y - r, r * 2.0f, r * 2.0f);
    graphics.FillRectangle(&coatBrush, x - r + 5.0f, y + 8.0f, r * 2.0f - 10.0f, r + 18.0f);
    graphics.FillEllipse(&eyeBrush, x - 12.0f, y - 3.0f, 8.0f, 11.0f);
    graphics.FillEllipse(&eyeBrush, x + 4.0f, y - 3.0f, 8.0f, 11.0f);
}
