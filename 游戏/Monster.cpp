#include "Monster.h"

#include "AssetPaths.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kFrameSize = 64;
    constexpr float kDrawSize = 96.0f;
    constexpr float kAttackV2DrawSize = 160.0f;
    constexpr float kAttackV2PivotX = 32.0f;
    constexpr float kAttackV2PivotY = 60.0f;
    constexpr float kSpriteFootOffset = 34.0f;
    constexpr int kAttackV2HitFrame = 5;
    constexpr float kAnimationFramesPerSecond = 12.0f;
    constexpr float kMoveSpeed = 58.0f;
    constexpr float kAttackRange = 118.0f;
    constexpr float kAttackCorridorHalfWidth = 42.0f;
    constexpr float kAttackHitReachPadding = 24.0f;
    constexpr float kAttackOriginYOffset = -10.0f;
    constexpr float kAttackDuration = 0.40f;
    constexpr float kAttackWindupTime = 0.24f;
    constexpr float kAttackRecoveryTime = 0.16f;
    constexpr float kAttackCooldown = 2.00f;
    constexpr float kHitEffectDuration = 0.28f;
    constexpr float kDeathFallDuration = 0.55f;
    constexpr float kDeathAnimationDuration = 1.00f;
    constexpr float kLaunchDeathDuration = 0.92f;
    constexpr float kLaunchInitialSpeed = 720.0f;
    constexpr float kLaunchAcceleration = 1050.0f;

    const wchar_t* kIdlePath = L"\u89d2\u8272\u5efa\u6a21\\\u602a\u7269\\selected_monster_knight\\monster_knight_idle_4dir_64.png";
    const wchar_t* kWalkFrontPath = L"\u89d2\u8272\u5efa\u6a21\\\u602a\u7269\\selected_monster_knight\\monster_walk_front_4frames_64.png";
    const wchar_t* kWalkBackPath = L"\u89d2\u8272\u5efa\u6a21\\\u602a\u7269\\selected_monster_knight\\monster_walk_back_4frames_64.png";
    const wchar_t* kWalkSidePath = L"\u89d2\u8272\u5efa\u6a21\\\u602a\u7269\\selected_monster_knight\\monster_walk_side_4frames_64.png";
    const wchar_t* kAttackSideV2Path = L"\u89d2\u8272\u5efa\u6a21\\\u602a\u7269\\selected_monster_knight\\side_only_v2\\spritesheets\\monster_attack_side_8frames.png";
    const wchar_t* kAttackSideLegacyPath = L"\u89d2\u8272\u5efa\u6a21\\\u602a\u7269\\selected_monster_knight\\monster_attack_side_4frames_64.png";

    static_assert(
        kAttackWindupTime + kAttackRecoveryTime >= kAttackDuration - 0.0001f &&
            kAttackWindupTime + kAttackRecoveryTime <= kAttackDuration + 0.0001f,
        "Monster attack phases must preserve the authoritative 0.40 second timeline.");

    float Length(Vec2 value)
    {
        return std::sqrt(value.x * value.x + value.y * value.y);
    }

    int ClampFrameIndex(int value, int frameCount)
    {
        const int lastFrame = frameCount > 0 ? frameCount - 1 : 0;
        if (value < 0)
        {
            return 0;
        }
        if (value > lastFrame)
        {
            return lastFrame;
        }
        return value;
    }

    int ResampleAttackFrame(float elapsedSeconds, float durationSeconds, int sourceFrameCount)
    {
        if (sourceFrameCount <= 1 || durationSeconds <= 0.0f)
        {
            return 0;
        }

        // The v2 manifest marks frame 5 as the contact pose. Keep that pose
        // exactly on the authoritative 0.24 second hit boundary; legacy four-
        // frame art retains its original two-frame windup/two-frame recovery.
        const int hitFrameIndex = sourceFrameCount == 8
            ? kAttackV2HitFrame
            : (std::max)(1, sourceFrameCount / 2);
        if (elapsedSeconds < kAttackWindupTime)
        {
            const float normalized = Clamp(elapsedSeconds / kAttackWindupTime, 0.0f, 1.0f);
            return ClampFrameIndex(
                static_cast<int>(std::round(normalized * static_cast<float>(hitFrameIndex - 1))),
                sourceFrameCount);
        }

        const int strikeFrameCount = (std::max)(1, sourceFrameCount - hitFrameIndex);
        const float strikeDuration = durationSeconds - kAttackWindupTime;
        const float normalized = strikeDuration > 0.0f
            ? Clamp((elapsedSeconds - kAttackWindupTime) / strikeDuration, 0.0f, 1.0f)
            : 1.0f;
        return ClampFrameIndex(
            hitFrameIndex + static_cast<int>(std::round(normalized * static_cast<float>(strikeFrameCount - 1))),
            sourceFrameCount);
    }
}

Monster::~Monster() = default;

// 首次创建时加载共享于该实例的图集，并将怪物重置到屏幕内的默认出生点。
void Monster::Initialize(RECT clientRect)
{
    const float width = static_cast<float>(clientRect.right - clientRect.left);
    const float height = static_cast<float>(clientRect.bottom - clientRect.top);
    center_ = { width * 0.42f, height * 0.52f };
    Deactivate();
    LoadSprite();
}

void Monster::UnloadContent()
{
    idleSheet_.image.reset();
    walkFrontSheet_.image.reset();
    walkBackSheet_.image.reset();
    walkSideSheet_.image.reset();
    attackSideSheet_.image.reset();
}

// 以客户区中心为参照缩放位置，避免调整窗口后实体全都挤在左上角。
void Monster::ResizeToClient(RECT oldClientRect, RECT newClientRect)
{
    const float oldWidth = static_cast<float>(oldClientRect.right - oldClientRect.left);
    const float oldHeight = static_cast<float>(oldClientRect.bottom - oldClientRect.top);
    const float newWidth = static_cast<float>(newClientRect.right - newClientRect.left);
    const float newHeight = static_cast<float>(newClientRect.bottom - newClientRect.top);
    if (oldWidth <= 0.0f || oldHeight <= 0.0f || newWidth <= 0.0f || newHeight <= 0.0f)
    {
        return;
    }

    center_.x += (newWidth - oldWidth) * 0.5f;
    center_.y += (newHeight - oldHeight) * 0.5f;
}

// 敌人状态机：死亡/受伤表现优先，存活时依距离在追踪、前摇和攻击之间切换。
void Monster::Update(float deltaSeconds, Vec2 targetPosition)
{
    if (!active_)
    {
        return;
    }

    animationTimer_ += deltaSeconds;
    healthBar_.Update(deltaSeconds);

    if (hitEffectTimer_ > 0.0f)
    {
        hitEffectTimer_ -= deltaSeconds;
        if (hitEffectTimer_ < 0.0f)
        {
            hitEffectTimer_ = 0.0f;
        }
    }
    if (health_ <= 0)
    {
        deathTimer_ += deltaSeconds;
        if (launchedDeath_)
        {
            launchVelocity_.x += launchDirection_.x * kLaunchAcceleration * deltaSeconds;
            launchVelocity_.y += launchDirection_.y * kLaunchAcceleration * deltaSeconds;
            center_.x += launchVelocity_.x * deltaSeconds;
            center_.y += launchVelocity_.y * deltaSeconds;
            launchSpinDegrees_ += launchSpinVelocity_ * deltaSeconds;
        }
        return;
    }
    if (attackCooldown_ > 0.0f)
    {
        attackCooldown_ -= deltaSeconds;
        if (attackCooldown_ < 0.0f)
        {
            attackCooldown_ = 0.0f;
        }
    }
    if (attackTimer_ > 0.0f)
    {
        const float previousElapsed = kAttackDuration - attackTimer_;
        attackTimer_ -= deltaSeconds;
        if (attackTimer_ < 0.0f)
        {
            attackTimer_ = 0.0f;
        }
        const float elapsed = kAttackDuration - attackTimer_;
        if (!attackHitTriggered_ && previousElapsed < kAttackWindupTime && elapsed >= kAttackWindupTime)
        {
            attackHitTriggered_ = true;
            attackHitPending_ = true;
        }
        if (attackTimer_ <= 0.0f)
        {
            attackCooldown_ = kAttackCooldown * attackCooldownMultiplier_;
        }
        motion_ = elapsed < kAttackWindupTime ? Motion::Windup : Motion::Attack;
        return;
    }

    const Vec2 toTarget{ targetPosition.x - center_.x, targetPosition.y - center_.y };
    const float distance = Length(toTarget);
    if (distance > 0.001f)
    {
        if (std::abs(toTarget.x) > 0.001f)
        {
            horizontalFacing_ = toTarget.x < 0.0f ? Facing::Left : Facing::Right;
        }
        if (std::abs(toTarget.x) > std::abs(toTarget.y))
        {
            facing_ = toTarget.x < 0.0f ? Facing::Left : Facing::Right;
        }
        else
        {
            facing_ = toTarget.y < 0.0f ? Facing::Up : Facing::Down;
        }
    }

    attackFacing_ = horizontalFacing_;
    attackDirection_ = attackFacing_ == Facing::Left ? Vec2{ -1.0f, 0.0f } : Vec2{ 1.0f, 0.0f };
    const bool targetInsideAttackCorridor = IsTargetInsideAttackCorridor(targetPosition);
    if (targetInsideAttackCorridor && attackCooldown_ <= 0.0f)
    {
        attackTimer_ = kAttackDuration;
        attackCooldown_ = 0.0f;
        attackHitPending_ = false;
        attackHitTriggered_ = false;
        motion_ = Motion::Windup;
        animationTimer_ = 0.0f;
        return;
    }

    if (!targetInsideAttackCorridor || distance > kAttackRange * attackRangeMultiplier_ * 0.82f)
    {
        const Vec2 direction = Normalize(toTarget);
        center_.x += direction.x * kMoveSpeed * moveSpeedMultiplier_ * deltaSeconds;
        center_.y += direction.y * kMoveSpeed * moveSpeedMultiplier_ * deltaSeconds;
        motion_ = Motion::Walk;
    }
    else
    {
        motion_ = Motion::Idle;
    }
}

// 根据当前状态选择图集；素材缺失时仍使用几何图元绘制占位敌人。
void Monster::Draw(Gdiplus::Graphics& graphics) const
{
    if (!active_)
    {
        return;
    }
    if (health_ <= 0)
    {
        if (launchedDeath_)
        {
            DrawLaunchedDeath(graphics);
            return;
        }
        const float fallProgress = Clamp(deathTimer_ / kDeathFallDuration, 0.0f, 1.0f);
        const float easedFall = 1.0f - std::pow(1.0f - fallProgress, 3.0f);
        const float left = center_.x - kDrawSize * 0.5f;
        const float top = center_.y - kDrawSize * 0.58f + easedFall * 10.0f;
        Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(62, 42, 36, 20));
        graphics.FillEllipse(&shadowBrush, center_.x - 31.0f, center_.y + 27.0f, 62.0f, 14.0f);

        int frameIndex = 0;
        bool mirror = false;
        const AnimationSheet* sheet = GetCurrentSheet(frameIndex, mirror);
        Gdiplus::GraphicsState state = graphics.Save();
        graphics.TranslateTransform(center_.x, center_.y + 22.0f);
        graphics.RotateTransform(82.0f * easedFall);
        graphics.TranslateTransform(-center_.x, -(center_.y + 22.0f));
        if (sheet != nullptr && sheet->image != nullptr && sheet->image->GetLastStatus() == Gdiplus::Ok)
        {
            DrawSheetFrame(graphics, *sheet, frameIndex, mirror, left, top);
        }
        else
        {
            DrawFallback(graphics);
        }
        graphics.Restore(state);
        DrawHitEffect(graphics);
        return;
    }

    const float bob = motion_ == Motion::Idle ? std::sin(animationTimer_ * 3.2f) * 2.0f : 0.0f;
    const float left = center_.x - kDrawSize * 0.5f;
    const float top = center_.y + bob - kDrawSize * 0.58f;

    Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(78, 42, 36, 20));
    graphics.FillEllipse(&shadowBrush, center_.x - 27.0f, center_.y + 29.0f, 54.0f, 13.0f);

    int frameIndex = 0;
    bool mirror = false;
    const AnimationSheet* sheet = GetCurrentSheet(frameIndex, mirror);
    if (sheet != nullptr && sheet->image != nullptr && sheet->image->GetLastStatus() == Gdiplus::Ok)
    {
        DrawSheetFrame(graphics, *sheet, frameIndex, mirror, left, top);
    }
    else
    {
        DrawFallback(graphics);
    }

    DrawAttackEffect(graphics);
    DrawHitEffect(graphics);
    healthBar_.Draw(graphics, center_.x - 34.0f, top - 12.0f, 68.0f, 8.0f);
}

// 恢复数值和 AI 状态，但保留已经加载的 Bitmap，适合训练局快速重开。
void Monster::Reset()
{
    radius_ = 20.0f;
    maxHealth_ = 30;
    health_ = maxHealth_;
    attack_ = 5;
    moveSpeedMultiplier_ = 1.0f;
    attackRangeMultiplier_ = 1.0f;
    attackCooldownMultiplier_ = 1.0f;
    active_ = true;
    motion_ = Motion::Idle;
    facing_ = Facing::Down;
    horizontalFacing_ = Facing::Right;
    attackFacing_ = Facing::Right;
    attackDirection_ = { 1.0f, 0.0f };
    animationTimer_ = 0.0f;
    attackTimer_ = 0.0f;
    attackCooldown_ = 0.0f;
    hitEffectTimer_ = 0.0f;
    deathTimer_ = 0.0f;
    launchedDeath_ = false;
    launchDirection_ = { 1.0f, 0.0f };
    launchVelocity_ = {};
    launchSpinDegrees_ = 0.0f;
    launchSpinVelocity_ = 0.0f;
    attackHitPending_ = false;
    attackHitTriggered_ = false;
    healthBar_.ResetValue(health_, maxHealth_);
}

// 将实例放回对象池式的非活跃状态，并清除可能尚未消费的攻击事件。
void Monster::Deactivate()
{
    center_ = {};
    radius_ = 20.0f;
    maxHealth_ = 30;
    active_ = false;
    health_ = 0;
    attack_ = 5;
    moveSpeedMultiplier_ = 1.0f;
    attackRangeMultiplier_ = 1.0f;
    attackCooldownMultiplier_ = 1.0f;
    facing_ = Facing::Down;
    horizontalFacing_ = Facing::Right;
    attackFacing_ = Facing::Right;
    attackDirection_ = { 1.0f, 0.0f };
    motion_ = Motion::Idle;
    animationTimer_ = 0.0f;
    attackTimer_ = 0.0f;
    attackCooldown_ = 0.0f;
    hitEffectTimer_ = 0.0f;
    deathTimer_ = 0.0f;
    launchedDeath_ = false;
    launchDirection_ = { 1.0f, 0.0f };
    launchVelocity_ = {};
    launchSpinDegrees_ = 0.0f;
    launchSpinVelocity_ = 0.0f;
    attackHitPending_ = false;
    attackHitTriggered_ = false;
    healthBar_.ResetValue(health_, maxHealth_);
}

void Monster::SpawnAt(Vec2 spawnPosition)
{
    center_ = spawnPosition;
    Reset();
}

// 将两个相交圆各推开一半；重合中心使用固定方向，避免归一化零向量。
void Monster::ResolveCollisionWith(Monster& other)
{
    if (!IsAlive() || !other.IsAlive())
    {
        return;
    }

    const float dx = other.center_.x - center_.x;
    const float dy = other.center_.y - center_.y;
    const float minimumDistance = radius_ + other.radius_;
    const float distanceSquared = dx * dx + dy * dy;
    if (distanceSquared >= minimumDistance * minimumDistance)
    {
        return;
    }

    float normalX = 1.0f;
    float normalY = 0.0f;
    float distance = 0.0f;
    if (distanceSquared > 0.0001f)
    {
        distance = std::sqrt(distanceSquared);
        normalX = dx / distance;
        normalY = dy / distance;
    }
    const float correction = (minimumDistance - distance) * 0.5f;
    center_.x -= normalX * correction;
    center_.y -= normalY * correction;
    other.center_.x += normalX * correction;
    other.center_.y += normalY * correction;
}

// 扣血同时启动受击闪光；真正的死亡掉落和计数由 Scene 统一处理。
int Monster::TakeDamage(int damage)
{
    if (!IsAlive())
    {
        return 0;
    }

    const int previousHealth = health_;
    health_ -= damage > 0 ? damage : 0;
    if (health_ < 0)
    {
        health_ = 0;
    }

    if (health_ <= 0)
    {
        deathTimer_ = 0.0f;
        launchedDeath_ = false;
        launchVelocity_ = {};
        launchSpinDegrees_ = 0.0f;
        launchSpinVelocity_ = 0.0f;
        attackTimer_ = 0.0f;
        attackHitPending_ = false;
        attackHitTriggered_ = false;
        motion_ = Motion::Idle;
    }

    hitEffectTimer_ = kHitEffectDuration;
    healthBar_.SetValue(health_, maxHealth_);
    return previousHealth - health_;
}

// 把死亡动作切换为带初速度和旋转的击飞，strength 同时缩放平移与旋转力度。
void Monster::LaunchDefeated(Vec2 direction, float strength)
{
    if (!active_ || health_ > 0)
    {
        return;
    }

    direction = Normalize(direction);
    if (direction.x == 0.0f && direction.y == 0.0f)
    {
        direction = horizontalFacing_ == Facing::Left ? Vec2{ -1.0f, 0.0f } : Vec2{ 1.0f, 0.0f };
    }
    const float clampedStrength = Clamp(strength, 0.75f, 1.60f);
    launchedDeath_ = true;
    deathTimer_ = 0.0f;
    launchDirection_ = direction;
    launchVelocity_ =
    {
        direction.x * kLaunchInitialSpeed * clampedStrength,
        direction.y * kLaunchInitialSpeed * clampedStrength
    };
    launchSpinDegrees_ = 0.0f;
    launchSpinVelocity_ = (direction.x + direction.y >= 0.0f ? 1.0f : -1.0f) * 420.0f * clampedStrength;
}

// pending 先清除再做几何检测：无论本帧是否命中，一个挥击都只检测一次。
bool Monster::ConsumeAttackHit(Vec2 targetPosition, Vec2& sourcePosition, int& damage)
{
    if (!attackHitPending_ || !IsAlive())
    {
        return false;
    }

    attackHitPending_ = false;
    // The player collision disc, not only its center point, can meet the blade.
    // This keeps a visually connected strike from being rejected at the corridor edge.
    if (!IsTargetInsideAttackCorridor(targetPosition, kAttackHitReachPadding, 22.0f))
    {
        return false;
    }

    sourcePosition = GetAttackOrigin();
    damage = attack_;
    return true;
}

Vec2 Monster::GetCenter() const
{
    return center_;
}

float Monster::GetCollisionRadius() const
{
    return IsAlive() ? radius_ : 0.0f;
}

int Monster::GetAttack() const
{
    return attack_;
}

bool Monster::IsAlive() const
{
    return active_ && health_ > 0;
}

bool Monster::IsDeathAnimationFinished() const
{
    const float duration = launchedDeath_ ? kLaunchDeathDuration : kDeathAnimationDuration;
    return active_ && health_ <= 0 && deathTimer_ >= duration;
}

int Monster::GetKillCredit() const
{
    return 1;
}

void Monster::ConfigureCombatProfile(
    int maximumHealth,
    int attack,
    float collisionRadius,
    float moveSpeedMultiplier,
    float attackRangeMultiplier,
    float attackCooldownMultiplier)
{
    maxHealth_ = (std::max)(1, maximumHealth);
    health_ = maxHealth_;
    attack_ = (std::max)(0, attack);
    radius_ = (std::max)(1.0f, collisionRadius);
    moveSpeedMultiplier_ = (std::max)(0.1f, moveSpeedMultiplier);
    attackRangeMultiplier_ = (std::max)(0.1f, attackRangeMultiplier);
    attackCooldownMultiplier_ = (std::max)(0.1f, attackCooldownMultiplier);
    healthBar_.ResetValue(health_, maxHealth_);
}

// 分别加载待机、三方向行走和侧向攻击；新素材不存在时自动退回旧版图集。
void Monster::LoadSprite()
{
    LoadSheet(idleSheet_, kIdlePath, 4);
    LoadSheet(walkFrontSheet_, kWalkFrontPath, 4);
    LoadSheet(walkBackSheet_, kWalkBackPath, 4);
    LoadSheet(walkSideSheet_, kWalkSidePath, 4);
    LoadSheetWithFallback(attackSideSheet_, kAttackSideV2Path, 8, kAttackSideLegacyPath, 4);
}

// 校验 GDI+ 解码状态后才提交 image，防止容器中保留“非空但不可绘制”的 Bitmap。
void Monster::LoadSheet(AnimationSheet& sheet, const wchar_t* relativePath, int frameCount)
{
    sheet.frameCount = frameCount;
    sheet.hasDirectionalRows = false;
    sheet.useAttackV2Layout = false;
    const std::wstring path = FindAssetPath(relativePath);
    sheet.image.reset(Gdiplus::Bitmap::FromFile(path.c_str(), FALSE));
    if (sheet.image == nullptr || sheet.image->GetLastStatus() != Gdiplus::Ok)
    {
        sheet.image.reset();
    }
}

void Monster::LoadSheetWithFallback(
    AnimationSheet& sheet,
    const wchar_t* preferredRelativePath,
    int preferredFrameCount,
    const wchar_t* fallbackRelativePath,
    int fallbackFrameCount)
{
    LoadSheet(sheet, preferredRelativePath, preferredFrameCount);
    if (sheet.image != nullptr)
    {
        // side_only_v2 is a 512x128 sheet: row 0 left, row 1 right. Its
        // source actor is intentionally compact so the spear remains inside
        // each 64x64 frame; render it at 2.5x and preserve its (32, 60) pivot.
        sheet.hasDirectionalRows = true;
        sheet.useAttackV2Layout = true;
    }
    else
    {
        LoadSheet(sheet, fallbackRelativePath, fallbackFrameCount);
    }
}

// 攻击原点略微前移，使判定与挥击精灵的视觉接触位置更一致。
Vec2 Monster::GetAttackOrigin() const
{
    return { center_.x, center_.y + kAttackOriginYOffset };
}

bool Monster::IsTargetInsideAttackCorridor(
    Vec2 targetPosition,
    float reachPadding,
    float lateralPadding) const
{
    const Vec2 origin = GetAttackOrigin();
    const Vec2 toTarget{ targetPosition.x - origin.x, targetPosition.y - origin.y };
    const Vec2 side{ -attackDirection_.y, attackDirection_.x };
    const float forwardProjection = toTarget.x * attackDirection_.x + toTarget.y * attackDirection_.y;
    const float lateralProjection = std::abs(toTarget.x * side.x + toTarget.y * side.y);
    return forwardProjection >= 0.0f &&
        forwardProjection <= kAttackRange * attackRangeMultiplier_ + reachPadding &&
        lateralProjection <= kAttackCorridorHalfWidth + lateralPadding;
}

// 将 AI motion、朝向和运行时间转换成具体图集帧；左向可复用右向素材镜像。
const Monster::AnimationSheet* Monster::GetCurrentSheet(int& frameIndex, bool& mirror) const
{
    mirror = facing_ == Facing::Left;

    if (motion_ == Motion::Windup || motion_ == Motion::Attack)
    {
        const float elapsedSeconds = kAttackDuration - attackTimer_;
        mirror = attackFacing_ == Facing::Left;
        frameIndex = ResampleAttackFrame(elapsedSeconds, kAttackDuration, attackSideSheet_.frameCount);
        return &attackSideSheet_;
    }

    if (motion_ == Motion::Walk)
    {
        const int walkFrame = static_cast<int>(animationTimer_ * kAnimationFramesPerSecond) % 4;
        frameIndex = walkFrame;
        if (facing_ == Facing::Up)
        {
            return &walkBackSheet_;
        }
        if (facing_ == Facing::Left || facing_ == Facing::Right)
        {
            return &walkSideSheet_;
        }
        return &walkFrontSheet_;
    }

    switch (facing_)
    {
    case Facing::Left:
        frameIndex = 1;
        break;
    case Facing::Right:
        frameIndex = 2;
        mirror = false;
        break;
    case Facing::Up:
        frameIndex = 3;
        mirror = false;
        break;
    case Facing::Down:
    default:
        frameIndex = 0;
        mirror = false;
        break;
    }
    return &idleSheet_;
}

void Monster::DrawSheetFrame(
    Gdiplus::Graphics& graphics,
    const AnimationSheet& sheet,
    int frameIndex,
    bool mirror,
    float left,
    float top,
    float opacity,
    bool blueTint) const
{
    frameIndex = ClampFrameIndex(frameIndex, sheet.frameCount);
    const float sourceX = static_cast<float>(frameIndex * kFrameSize);
    const float sourceY = sheet.hasDirectionalRows && !mirror
        ? static_cast<float>(kFrameSize)
        : 0.0f;
    const bool drawMirrored = mirror && !sheet.hasDirectionalRows;
    float drawSize = kDrawSize;
    if (sheet.useAttackV2Layout)
    {
        drawSize = kAttackV2DrawSize;
        left = center_.x - (kAttackV2PivotX / static_cast<float>(kFrameSize)) * drawSize;
        top = center_.y + kSpriteFootOffset -
            (kAttackV2PivotY / static_cast<float>(kFrameSize)) * drawSize;
    }

    opacity = Clamp(opacity, 0.0f, 1.0f);
    const float redScale = blueTint ? 0.28f : 1.0f;
    const float greenScale = blueTint ? 0.70f : 1.0f;
    const float blueScale = blueTint ? 1.15f : 1.0f;
    const float redOffset = blueTint ? 0.02f : 0.0f;
    const float greenOffset = blueTint ? 0.10f : 0.0f;
    const float blueOffset = blueTint ? 0.22f : 0.0f;
    Gdiplus::ColorMatrix colorMatrix =
    {
        redScale, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, greenScale, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, blueScale, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, opacity, 0.0f,
        redOffset, greenOffset, blueOffset, 0.0f, 1.0f
    };
    Gdiplus::ImageAttributes attributes;
    attributes.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);

    if (!drawMirrored)
    {
        graphics.DrawImage(
            sheet.image.get(),
            Gdiplus::RectF(left, top, drawSize, drawSize),
            sourceX,
            sourceY,
            static_cast<float>(kFrameSize),
            static_cast<float>(kFrameSize),
            Gdiplus::UnitPixel,
            &attributes);
        return;
    }

    Gdiplus::GraphicsState state = graphics.Save();
    graphics.TranslateTransform(left + drawSize, top);
    graphics.ScaleTransform(-1.0f, 1.0f);
    graphics.DrawImage(
        sheet.image.get(),
        Gdiplus::RectF(0.0f, 0.0f, drawSize, drawSize),
        sourceX,
        sourceY,
        static_cast<float>(kFrameSize),
        static_cast<float>(kFrameSize),
        Gdiplus::UnitPixel,
        &attributes);
    graphics.Restore(state);
}

// 击飞死亡使用 Graphics 变换围绕角色中心旋转，并随 deathTimer 逐渐淡出。
void Monster::DrawLaunchedDeath(Gdiplus::Graphics& graphics) const
{
    const float progress = Clamp(deathTimer_ / kLaunchDeathDuration, 0.0f, 1.0f);
    const float speed = std::sqrt(launchVelocity_.x * launchVelocity_.x + launchVelocity_.y * launchVelocity_.y);
    const float streakLength = Clamp(speed * 0.10f, 48.0f, 132.0f);
    const Vec2 side{ -launchDirection_.y, launchDirection_.x };

    for (int streak = -2; streak <= 2; ++streak)
    {
        const float sideOffset = static_cast<float>(streak) * 8.0f;
        const float startX = center_.x - launchDirection_.x * streakLength + side.x * sideOffset;
        const float startY = center_.y - launchDirection_.y * streakLength + side.y * sideOffset;
        const float endX = center_.x - launchDirection_.x * 24.0f + side.x * sideOffset * 0.25f;
        const float endY = center_.y - launchDirection_.y * 24.0f + side.y * sideOffset * 0.25f;
        const BYTE glowAlpha = static_cast<BYTE>((1.0f - progress) * (streak == 0 ? 150.0f : 80.0f));
        Gdiplus::Pen trailGlow(Gdiplus::Color(glowAlpha, 24, 112, 255), streak == 0 ? 13.0f : 8.0f);
        Gdiplus::Pen trailCore(Gdiplus::Color(glowAlpha, 126, 238, 255), 2.5f);
        graphics.DrawLine(&trailGlow, startX, startY, endX, endY);
        graphics.DrawLine(&trailCore, startX, startY, endX, endY);
    }

    int frameIndex = 0;
    bool mirror = false;
    const AnimationSheet* sheet = GetCurrentSheet(frameIndex, mirror);
    if (sheet == nullptr || sheet->image == nullptr || sheet->image->GetLastStatus() != Gdiplus::Ok)
    {
        Gdiplus::SolidBrush fallback(Gdiplus::Color(220, 70, 190, 255));
        graphics.FillEllipse(&fallback, center_.x - 30.0f, center_.y - 42.0f, 60.0f, 70.0f);
        return;
    }

    for (int trail = 4; trail >= 1; --trail)
    {
        const float distance = static_cast<float>(trail) * (20.0f + progress * 8.0f);
        const Vec2 trailCenter
        {
            center_.x - launchDirection_.x * distance,
            center_.y - launchDirection_.y * distance
        };
        const float opacity = (0.38f / static_cast<float>(trail)) * (1.0f - progress * 0.45f);
        DrawSheetFrame(
            graphics,
            *sheet,
            frameIndex,
            mirror,
            trailCenter.x - kDrawSize * 0.5f,
            trailCenter.y - kDrawSize * 0.58f,
            opacity,
            true);
    }

    const Gdiplus::GraphicsState state = graphics.Save();
    graphics.TranslateTransform(center_.x, center_.y);
    graphics.RotateTransform(launchSpinDegrees_);
    graphics.TranslateTransform(-center_.x, -center_.y);
    DrawSheetFrame(
        graphics,
        *sheet,
        frameIndex,
        mirror,
        center_.x - kDrawSize * 0.5f,
        center_.y - kDrawSize * 0.58f,
        1.0f - progress * 0.18f,
        true);
    graphics.Restore(state);
}

// 受击粒子完全由剩余计时派生，确保渲染帧率变化不会改变效果总时长。
void Monster::DrawHitEffect(Gdiplus::Graphics& graphics) const
{
    if (hitEffectTimer_ <= 0.0f)
    {
        return;
    }

    const float fade = hitEffectTimer_ / kHitEffectDuration;
    const float progress = 1.0f - fade;
    const float burst = std::sin((std::min)(progress * 1.4f, 1.0f) * 3.1415926f);
    const BYTE alpha = static_cast<BYTE>(230.0f * fade);
    Gdiplus::SolidBrush white(Gdiplus::Color(alpha, 255, 255, 255));
    Gdiplus::SolidBrush gold(Gdiplus::Color(alpha, 255, 224, 82));
    Gdiplus::SolidBrush orange(Gdiplus::Color(alpha, 255, 108, 36));
    const float x = center_.x;
    const float y = center_.y - 18.0f;
    const float spread = 14.0f + progress * 42.0f;

    const float ringRadius = 10.0f + progress * 38.0f;
    const BYTE ringAlpha = static_cast<BYTE>(Clamp(burst * fade * 220.0f, 0.0f, 255.0f));
    Gdiplus::Pen ringGlow(Gdiplus::Color(ringAlpha, 255, 154, 32), 8.0f);
    Gdiplus::Pen ringCore(Gdiplus::Color(alpha, 255, 255, 245), 2.5f);
    graphics.DrawEllipse(&ringGlow, x - ringRadius, y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);
    graphics.DrawEllipse(&ringCore, x - ringRadius, y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);

    const float cross = 18.0f + burst * 18.0f;
    graphics.FillRectangle(&white, x - cross, y - 4.0f, cross * 2.0f, 8.0f);
    graphics.FillRectangle(&white, x - 4.0f, y - cross, 8.0f, cross * 2.0f);
    graphics.FillRectangle(&gold, x - 10.0f, y - 10.0f, 20.0f, 20.0f);
    graphics.FillRectangle(&white, x - spread, y - spread * 0.35f, 8.0f, 8.0f);
    graphics.FillRectangle(&white, x + spread - 8.0f, y + spread * 0.25f, 8.0f, 8.0f);
    graphics.FillRectangle(&gold, x - spread * 0.55f, y + spread * 0.72f, 7.0f, 7.0f);
    graphics.FillRectangle(&orange, x + spread * 0.44f, y - spread * 0.78f, 6.0f, 6.0f);
    graphics.FillRectangle(&white, x - spread * 0.12f, y - spread, 5.0f, 5.0f);
}

// 攻击特效跟随锁定的 attackFacing_，而不是追踪过程中随时变化的 facing_。
void Monster::DrawAttackEffect(Gdiplus::Graphics& graphics) const
{
    if ((motion_ != Motion::Windup && motion_ != Motion::Attack) || attackTimer_ <= 0.0f)
    {
        return;
    }

    const float elapsed = kAttackDuration - attackTimer_;
    const Vec2 origin = GetAttackOrigin();
    const Vec2 direction = attackDirection_;
    const Vec2 side{ -direction.y, direction.x };
    const float visibleReach = kAttackRange * attackRangeMultiplier_ + kAttackHitReachPadding;

    const auto MakePoint = [&origin, &direction, &side](float forward, float lateral)
    {
        return Gdiplus::PointF(
            origin.x + direction.x * forward + side.x * lateral,
            origin.y + direction.y * forward + side.y * lateral);
    };

    Gdiplus::PointF corridor[4]
    {
        MakePoint(0.0f, -kAttackCorridorHalfWidth),
        MakePoint(visibleReach, -kAttackCorridorHalfWidth),
        MakePoint(visibleReach, kAttackCorridorHalfWidth),
        MakePoint(0.0f, kAttackCorridorHalfWidth)
    };

    if (motion_ == Motion::Windup)
    {
        const float windupProgress = Clamp(elapsed / kAttackWindupTime, 0.0f, 1.0f);
        const float easedCharge = windupProgress * windupProgress * (3.0f - 2.0f * windupProgress);
        const float pulse = 0.72f + 0.28f * std::sin(windupProgress * 8.0f * 3.1415926f);
        const BYTE fillAlpha = static_cast<BYTE>(Clamp(28.0f + easedCharge * 62.0f, 0.0f, 255.0f));
        const BYTE edgeAlpha = static_cast<BYTE>(Clamp((105.0f + easedCharge * 130.0f) * pulse, 0.0f, 255.0f));
        Gdiplus::SolidBrush warningFill(Gdiplus::Color(fillAlpha, 172, 25, 12));
        Gdiplus::Pen warningOuter(Gdiplus::Color(edgeAlpha, 255, 66, 22), 5.0f);
        Gdiplus::Pen warningInner(Gdiplus::Color(edgeAlpha, 255, 198, 52), 2.0f);
        graphics.FillPolygon(&warningFill, corridor, 4);
        graphics.DrawPolygon(&warningOuter, corridor, 4);
        graphics.DrawPolygon(&warningInner, corridor, 4);

        // Crossbars make the telegraph's damage corridor readable at a glance.
        for (int index = 1; index <= 3; ++index)
        {
            const float forward = visibleReach * static_cast<float>(index) * 0.25f;
            const float halfWidth = kAttackCorridorHalfWidth * (0.35f + easedCharge * 0.45f);
            const Gdiplus::PointF lineStart = MakePoint(forward, -halfWidth);
            const Gdiplus::PointF lineEnd = MakePoint(forward, halfWidth);
            graphics.DrawLine(&warningInner, lineStart, lineEnd);
        }

        const Gdiplus::PointF chargePoint = MakePoint(26.0f + easedCharge * 8.0f, 0.0f);
        const float chargeRadius = 8.0f + easedCharge * 8.0f + pulse * 2.0f;
        Gdiplus::Pen chargeGlow(Gdiplus::Color(edgeAlpha, 255, 76, 20), 7.0f);
        Gdiplus::Pen chargeCore(Gdiplus::Color(edgeAlpha, 255, 232, 118), 2.0f);
        Gdiplus::SolidBrush chargePixel(Gdiplus::Color(edgeAlpha, 255, 246, 184));
        graphics.DrawEllipse(
            &chargeGlow,
            chargePoint.X - chargeRadius,
            chargePoint.Y - chargeRadius,
            chargeRadius * 2.0f,
            chargeRadius * 2.0f);
        graphics.DrawEllipse(
            &chargeCore,
            chargePoint.X - chargeRadius,
            chargePoint.Y - chargeRadius,
            chargeRadius * 2.0f,
            chargeRadius * 2.0f);
        graphics.FillRectangle(&chargePixel, chargePoint.X - 4.0f, chargePoint.Y - 4.0f, 8.0f, 8.0f);

        for (int index = 0; index < 5; ++index)
        {
            const float forward = 44.0f + static_cast<float>(index) * 22.0f;
            const float lateral = (index % 2 == 0 ? -1.0f : 1.0f) *
                (kAttackCorridorHalfWidth - 5.0f);
            const Gdiplus::PointF sparkPoint = MakePoint(forward, lateral);
            const float size = 3.0f + static_cast<float>(index % 3);
            graphics.FillRectangle(
                &chargePixel,
                sparkPoint.X - size * 0.5f,
                sparkPoint.Y - size * 0.5f,
                size,
                size);
        }
        return;
    }

    const float strikeProgress = Clamp(
        (elapsed - kAttackWindupTime) / kAttackRecoveryTime,
        0.0f,
        1.0f);
    const float remaining = 1.0f - strikeProgress;
    const float revealProgress = Clamp(strikeProgress / 0.22f, 0.0f, 1.0f);
    const float easedReveal = 1.0f - std::pow(1.0f - revealProgress, 3.0f);
    const float bladeStart = 22.0f;
    const float bladeEnd = bladeStart + (visibleReach - bladeStart) * (0.30f + easedReveal * 0.70f);
    const BYTE corridorAlpha = static_cast<BYTE>(Clamp(remaining * 74.0f, 0.0f, 255.0f));
    const BYTE strikeAlpha = static_cast<BYTE>(Clamp((0.30f + remaining * 0.70f) * 255.0f, 0.0f, 255.0f));

    Gdiplus::SolidBrush strikeFill(Gdiplus::Color(corridorAlpha, 46, 164, 218));
    Gdiplus::Pen corridorEdge(Gdiplus::Color(corridorAlpha, 255, 126, 32), 2.0f);
    graphics.FillPolygon(&strikeFill, corridor, 4);
    graphics.DrawPolygon(&corridorEdge, corridor, 4);

    const Gdiplus::PointF bladeStartPoint = MakePoint(bladeStart, 0.0f);
    const Gdiplus::PointF bladeEndPoint = MakePoint(bladeEnd, 0.0f);
    Gdiplus::Pen thrustGlow(Gdiplus::Color(strikeAlpha, 255, 104, 28), 12.0f);
    Gdiplus::Pen thrustEnergy(Gdiplus::Color(strikeAlpha, 74, 214, 255), 7.0f);
    Gdiplus::Pen thrustCore(Gdiplus::Color(strikeAlpha, 246, 255, 255), 3.0f);
    graphics.DrawLine(&thrustGlow, bladeStartPoint, bladeEndPoint);
    graphics.DrawLine(&thrustEnergy, bladeStartPoint, bladeEndPoint);
    graphics.DrawLine(&thrustCore, bladeStartPoint, bladeEndPoint);

    // A deterministic zig-zag keeps the thrust crisp and readable without RNG flicker.
    constexpr int kBoltPointCount = 8;
    Gdiplus::PointF boltPoints[kBoltPointCount];
    for (int index = 0; index < kBoltPointCount; ++index)
    {
        const float segment = static_cast<float>(index) / static_cast<float>(kBoltPointCount - 1);
        const float forward = bladeStart + (bladeEnd - bladeStart) * segment;
        const float alternating = index % 2 == 0 ? -1.0f : 1.0f;
        const float lateral = (index == 0 || index == kBoltPointCount - 1)
            ? 0.0f
            : alternating * (3.0f + static_cast<float>(index % 3) * 2.0f) * remaining;
        boltPoints[index] = MakePoint(forward, lateral);
    }
    Gdiplus::Pen boltPen(Gdiplus::Color(strikeAlpha, 255, 255, 255), 2.0f);
    graphics.DrawLines(&boltPen, boltPoints, kBoltPointCount);

    const float burstProgress = Clamp(strikeProgress / 0.62f, 0.0f, 1.0f);
    const BYTE burstAlpha = static_cast<BYTE>(Clamp((1.0f - burstProgress) * 245.0f, 0.0f, 255.0f));
    const float burstRadius = 8.0f + burstProgress * 26.0f;
    Gdiplus::Pen burstGlow(Gdiplus::Color(burstAlpha, 60, 218, 255), 7.0f);
    Gdiplus::Pen burstCore(Gdiplus::Color(burstAlpha, 255, 255, 245), 2.0f);
    graphics.DrawEllipse(
        &burstGlow,
        bladeEndPoint.X - burstRadius,
        bladeEndPoint.Y - burstRadius,
        burstRadius * 2.0f,
        burstRadius * 2.0f);
    graphics.DrawEllipse(
        &burstCore,
        bladeEndPoint.X - burstRadius,
        bladeEndPoint.Y - burstRadius,
        burstRadius * 2.0f,
        burstRadius * 2.0f);

    for (int index = 0; index < 8; ++index)
    {
        const float launchDelay = static_cast<float>(index) * 0.045f;
        const float particleProgress = Clamp((strikeProgress - launchDelay) / 0.70f, 0.0f, 1.0f);
        if (particleProgress <= 0.0f)
        {
            continue;
        }

        const float alternating = index % 2 == 0 ? -1.0f : 1.0f;
        const float forward = visibleReach - particleProgress * (12.0f + static_cast<float>(index % 3) * 6.0f);
        const float lateral = alternating *
            (8.0f + particleProgress * (16.0f + static_cast<float>(index) * 1.5f));
        const Gdiplus::PointF particlePoint = MakePoint(forward, lateral);
        const BYTE particleAlpha = static_cast<BYTE>(Clamp((1.0f - particleProgress) * 230.0f, 0.0f, 255.0f));
        const float particleSize = 6.0f - particleProgress * 3.0f;
        const Gdiplus::Color particleColor = index % 3 == 0
            ? Gdiplus::Color(particleAlpha, 255, 150, 38)
            : Gdiplus::Color(particleAlpha, 92, 226, 255);
        Gdiplus::SolidBrush particleBrush(particleColor);
        graphics.FillRectangle(
            &particleBrush,
            particlePoint.X - particleSize * 0.5f,
            particlePoint.Y - particleSize * 0.5f,
            particleSize,
            particleSize);
    }
}

void Monster::DrawFallback(Gdiplus::Graphics& graphics) const
{
    const float bob = motion_ == Motion::Idle ? std::sin(animationTimer_ * 3.2f) * 2.0f : 0.0f;
    const float r = radius_;
    const float x = center_.x;
    const float y = center_.y + bob;

    Gdiplus::SolidBrush outlineBrush(Gdiplus::Color(255, 72, 54, 24));
    Gdiplus::SolidBrush bodyBrush(Gdiplus::Color(255, 246, 202, 46));
    Gdiplus::SolidBrush lightBrush(Gdiplus::Color(255, 255, 238, 112));
    Gdiplus::SolidBrush eyeBrush(Gdiplus::Color(255, 72, 54, 24));

    graphics.FillEllipse(&outlineBrush, x - r - 2.0f, y - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);
    graphics.FillEllipse(&bodyBrush, x - r, y - r, r * 2.0f, r * 2.0f);
    graphics.FillEllipse(&lightBrush, x - r * 0.45f, y - r * 0.55f, r * 0.55f, r * 0.45f);
    graphics.FillEllipse(&eyeBrush, x - r * 0.38f, y - r * 0.06f, 5.0f, 6.0f);
    graphics.FillEllipse(&eyeBrush, x + r * 0.16f, y - r * 0.06f, 5.0f, 6.0f);
}
