#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "MathTypes.h"

// 角色的逻辑朝向。枚举顺序不是图集行号，具体映射由实现文件负责。
enum class CharacterDirection
{
    Down,
    Left,
    Right,
    Up
};

// 可由玩家状态选择的基础动画；终结技等大型特效使用独立接口播放。
enum class CharacterAnimation
{
    Idle,
    Walk,
    Crouch,
    Block,
    Attack,
    Throw,
    Death
};

// 玩家精灵动画与战斗特效资源管理器。
//
// 它负责从独立 PNG 帧/精灵表加载素材、按动作时间轴选帧、在指定帧产生
// “命中/释放”事件，并处理方向、锚点、镜像和素材回退。它不结算任何伤害。
class SpriteAnimation
{
public:
    ~SpriteAnimation();
    SpriteAnimation() = default;
    SpriteAnimation(const SpriteAnimation&) = delete;
    SpriteAnimation& operator=(const SpriteAnimation&) = delete;

    // 加载角色“穹”的全套素材；只要核心待机图加载成功即可进入精灵绘制模式。
    bool LoadQiongSprites();
    void Clear();
    void ResetState();
    void SetHasBat(bool hasBat);
    // 推进当前基础动画。animation 改变时自动建立短暂的动作混合过渡。
    void Update(float deltaSeconds, CharacterAnimation animation);

    // 显式启动一次性动作并重置对应命中/释放帧标志。
    void StartAttack();
    void StartThrow();
    void StartBlock();
    void StartDeath();
    // 启动/结束独立终结技特效序列，不干扰基础移动动画的资源选择。
    bool StartUltimateVfx(CharacterDirection direction);
    // 以下绘制函数返回 bool 的接口会在所需素材缺失时返回 false，
    // 允许 Player 选择普通角色帧作为回退。
    bool DrawUltimateActivationActor(
        Gdiplus::Graphics& graphics,
        float centerX,
        float bottomY,
        CharacterDirection direction,
        float normalizedProgress) const;
    bool DrawSuperThrowActor(
        Gdiplus::Graphics& graphics,
        float centerX,
        float bottomY,
        CharacterDirection direction) const;
    // 强化状态的光环、飞行物、爆炸和五段连击特效。
    void DrawSuperBatAura(
        Gdiplus::Graphics& graphics,
        float centerX,
        float bottomY,
        CharacterAnimation animation,
        CharacterDirection direction,
        float pulseTime) const;
    void DrawSuperBatProjectile(
        Gdiplus::Graphics& graphics,
        float centerX,
        float centerY,
        Vec2 direction,
        float rotationDegrees) const;
    void DrawSuperBatExplosion(
        Gdiplus::Graphics& graphics,
        float centerX,
        float centerY,
        float radius,
        float normalizedProgress) const;
    void DrawUltimateVfx(
        Gdiplus::Graphics& graphics,
        float worldX,
        float worldY,
        int stage,
        float normalizedProgress) const;
    void EndUltimateVfx();
    // 动作时间轴查询。CanBufferAttack 用于手感友好的连击预输入窗口。
    bool IsAnimationFinished() const;
    bool IsAttackMovementLocked() const;
    bool CanBufferAttack(float lookaheadSeconds) const;
    // 返回一次并清除事件，确保一个动画帧不会重复造成伤害/释放球棒。
    bool ConsumeAttackHitFrame();
    bool ConsumeThrowReleaseFrame();
    // visualDirection 决定身体朝向，actionDirection 决定攻击特效方向；两者在
    // 攻击期间可以不同，以便锁定出手方向而不阻止后续移动输入。
    void Draw(
        Gdiplus::Graphics& graphics,
        float centerX,
        float bottomY,
        CharacterAnimation animation,
        CharacterDirection visualDirection,
        CharacterDirection actionDirection,
        bool flash = false) const;
    bool IsLoaded() const;

    // 单张 PNG 及其落脚锚点。anchor 是图片局部坐标中应对齐到世界位置的点。
    struct Frame
    {
        std::unique_ptr<Gdiplus::Image> image;
        float anchorX = 32.0f;
        float anchorY = 64.0f;
        float visualHeight = 58.0f;
        // true 时以透明像素包围盒测量高度，减少不同素材裁切留白造成的跳动。
        bool useMeasuredVisualHeight = false;
    };

private:
    // 同一动作、同一方向的有序帧序列。
    struct FrameSet
    {
        std::vector<Frame> frames;
    };

    // 多种加载重载分别支持默认锚点、显式锚点、多级回退、底部裁切和纵向偏移。
    bool LoadFrame(FrameSet& frameSet, const std::wstring& relativePath);
    bool LoadFrame(FrameSet& frameSet, const std::wstring& relativePath, float anchorX, float anchorY, float visualHeight);
    bool LoadFrame(FrameSet& frameSet, const std::wstring& relativePath, const std::wstring& fallbackRelativePath, float anchorX, float anchorY, float visualHeight);
    bool LoadBottomCroppedFrame(FrameSet& frameSet, const std::wstring& relativePath, const std::wstring& fallbackRelativePath, float anchorX, float anchorY, float visualHeight, int sourceHeight);
    bool LoadOffsetFrame(FrameSet& frameSet, const std::wstring& relativePath, const std::wstring& fallbackRelativePath, float anchorX, float anchorY, float visualHeight, float offsetY);
    // 优先 v3，其次 v2，最后旧版素材；不同代素材可携带不同锚点元数据。
    bool LoadSideOnlyV3Frame(
        FrameSet& frameSet,
        const std::wstring& preferredRelativePath,
        const std::wstring& v2RelativePath,
        const std::wstring& legacyRelativePath,
        float fallbackAnchorX,
        float fallbackAnchorY,
        float fallbackVisualHeight);
    // 记录切换前帧，用短交叉淡化避免待机/行走/攻击之间瞬间跳变。
    void BeginAnimationTransition(CharacterAnimation animation);
    const FrameSet& GetFrameSet(CharacterAnimation animation, CharacterDirection direction) const;
    const FrameSet& GetActionVfxFrameSet(CharacterAnimation animation, CharacterDirection direction) const;

    // 基础持棒动作，每个方向一组。
    FrameSet idleDown_;
    FrameSet idleLeft_;
    FrameSet idleRight_;
    FrameSet idleUp_;
    FrameSet walkDown_;
    FrameSet walkLeft_;
    FrameSet walkRight_;
    FrameSet walkUp_;
    FrameSet crouchDown_;
    FrameSet crouchLeft_;
    FrameSet crouchRight_;
    FrameSet crouchUp_;
    FrameSet blockDown_;
    FrameSet blockLeft_;
    FrameSet blockRight_;
    FrameSet blockUp_;
    // 空手格挡、空手待机/行走、拳击及其 VFX。
    FrameSet unarmedBlockDown_;
    FrameSet unarmedBlockLeft_;
    FrameSet unarmedBlockRight_;
    FrameSet unarmedBlockUp_;
    FrameSet attackDown_;
    FrameSet attackLeft_;
    FrameSet attackRight_;
    FrameSet attackUp_;
    FrameSet attackVfxDown_;
    FrameSet attackVfxLeft_;
    FrameSet attackVfxRight_;
    FrameSet attackVfxUp_;
    FrameSet unarmedDown_;
    FrameSet unarmedLeft_;
    FrameSet unarmedRight_;
    FrameSet unarmedUp_;
    FrameSet unarmedWalkDown_;
    FrameSet unarmedWalkLeft_;
    FrameSet unarmedWalkRight_;
    FrameSet unarmedWalkUp_;
    FrameSet punchDown_;
    FrameSet punchLeft_;
    FrameSet punchRight_;
    FrameSet punchUp_;
    FrameSet punchVfxDown_;
    FrameSet punchVfxLeft_;
    FrameSet punchVfxRight_;
    FrameSet punchVfxUp_;
    // 投掷角色帧和与其同步的动作特效帧。
    FrameSet throwDown_;
    FrameSet throwLeft_;
    FrameSet throwRight_;
    FrameSet throwUp_;
    FrameSet throwVfxDown_;
    FrameSet throwVfxLeft_;
    FrameSet throwVfxRight_;
    FrameSet throwVfxUp_;
    FrameSet deathLeft_;
    FrameSet deathRight_;
    // 基础动画运行状态，所有时间单位均为秒。
    float frameTimer_ = 0.0f;
    float walkPhase_ = 0.0f;
    int currentFrame_ = 0;
    CharacterAnimation activeAnimation_ = CharacterAnimation::Idle;
    CharacterAnimation transitionSourceAnimation_ = CharacterAnimation::Idle;
    int transitionSourceFrame_ = 0;
    float transitionTimer_ = 0.0f;
    float transitionDuration_ = 0.0f;
    bool animationFinished_ = false;
    // Pending 等待 Player 消费，Triggered 保证一次动作只置位一次。
    bool attackHitFramePending_ = false;
    bool attackHitTriggered_ = false;
    bool throwReleaseFramePending_ = false;
    bool throwReleaseTriggered_ = false;
    bool hasBat_ = true;
    // 大型技能使用整张图集；爆炸和五段连击按阶段分别保存，便于独立采样。
    std::unique_ptr<Gdiplus::Bitmap> ultimateActivationVfxSheet_;
    std::unique_ptr<Gdiplus::Bitmap> ultimateActivationActorSheet_;
    std::unique_ptr<Gdiplus::Bitmap> ultimateThrowCompositeSheet_;
    std::unique_ptr<Gdiplus::Bitmap> superBatProjectileSheet_;
    std::array<std::unique_ptr<Gdiplus::Bitmap>, 8> superBatExplosionFrames_;
    std::array<std::unique_ptr<Gdiplus::Bitmap>, 5> ultimateComboVfxSheets_;
    CharacterDirection ultimateVfxDirection_ = CharacterDirection::Right;
    bool ultimateVfxActive_ = false;
    // v3 侧视素材测得的统一视觉高度，用于跨动作保持角色脚底稳定。
    float sideOnlyV3VisualHeight_ = 0.0f;
};
