#include "SpriteAnimation.h"

#include "AssetPaths.h"
#include "CombatTuning.h"

#include <algorithm>
#include <cmath>
#include <utility>

#pragma comment(lib, "gdiplus.lib")

namespace
{
    // 资源布局和动画时间轴常量。攻击关键时间直接引用 CombatTuning，保证
    // 视觉命中帧、玩家移动锁定和实际伤害判定使用同一组数值。
    const wchar_t* kQiongRoot = L"\u89d2\u8272\u5efa\u6a21\\\u7a79\\first_style_animation_bat\\combat_animation";
    const wchar_t* kUltimateVfxRoot = L"\u89d2\u8272\u5efa\u6a21\\\u7a79\\first_style_animation_bat\\combat_animation\\ultimate_blue_vfx_v1";
    constexpr int kDrawSize = 112;
    constexpr int kBatAttackDrawSize = 176;
    constexpr int kUnarmedAttackDrawSize = 112;
    constexpr int kThrowDrawSize = 112;
    constexpr float kTargetBodyHeight = 92.0f;
    constexpr float kCrouchScale = 1.72f;
    constexpr float kUnarmedCrouchScale = 1.45f;
    constexpr float kAnimationFramesPerSecond = 12.0f;
    constexpr float kAnimationFrameSeconds = 1.0f / kAnimationFramesPerSecond;
    constexpr float kIdleFramesPerSecond = 5.0f;
    constexpr int kIdleFrameCount = 4;
    constexpr int kWalkFrameCount = 8;
    constexpr int kSideOnlyV2WalkFrameCount = 4;
    constexpr int kAttackFrameCount = 10;
    constexpr int kThrowFrameCount = 5;
    constexpr int kBlockFrameCount = 10;
    constexpr int kDeathFrameCount = 8;
    constexpr float kAttackDurationSeconds = CombatTuning::BasicAttackDurationSeconds;
    constexpr float kAttackHitTimeSeconds = CombatTuning::BasicAttackHitTimeSeconds;
    constexpr float kAttackMovementLockSeconds = CombatTuning::BasicAttackMovementLockSeconds;
    constexpr float kAttackBufferStartSeconds = CombatTuning::BasicAttackBufferStartSeconds;
    constexpr float kThrowDurationSeconds = 0.50f;
    constexpr float kThrowReleaseTimeSeconds = 0.20f;
    constexpr float kDeathDurationSeconds = 0.80f;
    constexpr float kMovementBlendDurationSeconds = 0.10f;
    constexpr float kCombatBlendDurationSeconds = 0.06f;
    constexpr float kSideOnlyV3PivotX = 48.0f;
    constexpr float kSideOnlyV3PivotY = 90.0f;
    constexpr int kSideOnlyV3FrameSize = 96;
    constexpr int kUltimateActivationFrameSize = 192;
    constexpr int kUltimateComboFrameSize = 512;
    constexpr int kUltimateFrameCount = 10;
    constexpr int kUltimateHitCount = 5;
    constexpr int kUltimateActorFrameSize = 64;
    constexpr int kSuperBatProjectileFrameCount = 8;
    constexpr int kSuperBatExplosionFrameCount = 8;

    // 编译期验证关键帧顺序，调参错误会直接阻止构建而不是产生隐蔽的战斗 bug。
    static_assert(kAttackHitTimeSeconds < kAttackDurationSeconds, "Attack hit must occur before recovery completes.");
    static_assert(kAttackHitTimeSeconds <= kAttackMovementLockSeconds, "Movement must remain locked through the hit point.");
    static_assert(kAttackBufferStartSeconds < kAttackDurationSeconds, "Attack input buffer must open during recovery.");
    static_assert(kThrowReleaseTimeSeconds < kThrowDurationSeconds, "Bat release must occur before throw recovery completes.");

    // 战斗动作切换更短以保持响应，移动动作使用稍长混合减少姿势跳变。
    float GetAnimationBlendDuration(CharacterAnimation from, CharacterAnimation to)
    {
        const bool combatTransition =
            from == CharacterAnimation::Attack || from == CharacterAnimation::Throw || from == CharacterAnimation::Block || from == CharacterAnimation::Death ||
            to == CharacterAnimation::Attack || to == CharacterAnimation::Throw || to == CharacterAnimation::Block || to == CharacterAnimation::Death;
        return combatTransition ? kCombatBlendDurationSeconds : kMovementBlendDurationSeconds;
    }

    float ClampFloat(float value, float minimum, float maximum)
    {
        if (value < minimum)
        {
            return minimum;
        }
        if (value > maximum)
        {
            return maximum;
        }
        return value;
    }

    // 以下 Make*Path 只描述磁盘命名约定，统一两位帧号和四向目录名称。
    std::wstring MakeFramePath(const wchar_t* animation, const wchar_t* direction, const wchar_t* prefix, int frame, bool vfx = false)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\%s\\%s\\%s_%s_%02d.png", kQiongRoot, animation, vfx ? L"vfx_frames" : L"frames", prefix, direction, frame);
        return buffer;
    }

    std::wstring MakeReadyPath(const wchar_t* direction)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\bat_throw\\frames\\bat_throw_%s_00.png", kQiongRoot, direction);
        return buffer;
    }

    std::wstring MakeSideOnlyV2Path(const wchar_t* animation, const wchar_t* direction, int frame)
    {
        wchar_t buffer[256]{};
        wsprintfW(
            buffer,
            L"%s\\side_only_v2\\player\\%s\\%s\\qiong_%s_%s_%02d.png",
            kQiongRoot,
            animation,
            direction,
            animation,
            direction,
            frame);
        return buffer;
    }

    std::wstring MakeSideOnlyV3Path(const wchar_t* animation, const wchar_t* direction, int frame)
    {
        wchar_t buffer[256]{};
        wsprintfW(
            buffer,
            L"%s\\side_only_v3\\player\\%s\\%s\\qiong_%s_%s_%02d.png",
            kQiongRoot,
            animation,
            direction,
            animation,
            direction,
            frame);
        return buffer;
    }

    const wchar_t* GetDirectionName(CharacterDirection direction)
    {
        switch (direction)
        {
        case CharacterDirection::Down:
            return L"down";
        case CharacterDirection::Left:
            return L"left";
        case CharacterDirection::Up:
            return L"up";
        case CharacterDirection::Right:
        default:
            return L"right";
        }
    }

    // 四向精灵表约定：下、左、右、上四行。
    int GetDirectionRow(CharacterDirection direction)
    {
        switch (direction)
        {
        case CharacterDirection::Down:
            return 0;
        case CharacterDirection::Left:
            return 1;
        case CharacterDirection::Right:
            return 2;
        case CharacterDirection::Up:
        default:
            return 3;
        }
    }

    std::wstring MakeUltimateActivationSheetPath()
    {
        return std::wstring(kUltimateVfxRoot) + L"\\spritesheets\\activation_vfx_4dir_10frames.png";
    }

    std::wstring MakeUltimateActivationActorSheetPath()
    {
        return std::wstring(kUltimateVfxRoot) + L"\\spritesheets\\activation_actor_4dir_10frames.png";
    }

    std::wstring MakeUltimateThrowCompositeSheetPath()
    {
        return std::wstring(kUltimateVfxRoot) + L"\\spritesheets\\throw_blue_composite_4dir_5frames.png";
    }

    std::wstring MakeSuperBatProjectileSheetPath()
    {
        return std::wstring(kUltimateVfxRoot) + L"\\spritesheets\\super_bat_projectile_8frames.png";
    }

    std::wstring MakeSuperBatExplosionFramePath(int frame)
    {
        wchar_t buffer[384]{};
        wsprintfW(buffer, L"%s\\vfx\\endpoint_explosion\\explosion_%02d.png", kUltimateVfxRoot, frame);
        return buffer;
    }

    std::wstring MakeUltimateComboSheetPath(CharacterDirection direction, int hitIndex)
    {
        wchar_t buffer[384]{};
        wsprintfW(
            buffer,
            L"%s\\spritesheets\\ultimate_%s_hit%02d_10frames.png",
            kUltimateVfxRoot,
            GetDirectionName(direction),
            hitIndex + 1);
        return buffer;
    }

    std::wstring MakeWalkPath(const wchar_t* direction, int frame)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\walk_bat_10frames\\frames\\%s\\bat_walk_%s_%02d.png", kQiongRoot, direction, direction, frame);
        return buffer;
    }

    std::wstring MakeUnarmedWalkPath(const wchar_t* direction, int frame)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\walk_10frames\\frames\\%s\\unarmed_walk_%s_%02d.png", kQiongRoot, direction, direction, frame);
        return buffer;
    }

    std::wstring MakeTenFrameActionPath(const wchar_t* animation, const wchar_t* direction, const wchar_t* prefix, int frame, bool vfx = false)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\%s_10frames\\%s\\%s_%s_%02d.png", kQiongRoot, animation, vfx ? L"vfx_frames" : L"frames", prefix, direction, frame);
        return buffer;
    }

    std::wstring MakeBlockPath(const wchar_t* direction, int frame)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\block_bat\\frames\\block_bat_%s_%02d.png", kQiongRoot, direction, frame);
        return buffer;
    }

    std::wstring MakeUnarmedBlockPath(const wchar_t* direction, int frame)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\block_unarmed\\frames\\block_unarmed_%s_%02d.png", kQiongRoot, direction, frame);
        return buffer;
    }

    std::wstring MakeCrouchPath(const wchar_t* direction)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\crouch_bat\\qiong_crouch_bat_%s.png", kQiongRoot, direction);
        return buffer;
    }

    std::wstring MakeUnarmedPath(const wchar_t* direction)
    {
        wchar_t buffer[256]{};
        wsprintfW(buffer, L"%s\\unarmed\\qiong_unarmed_%s.png", kQiongRoot, direction);
        return buffer;
    }

    // 颜色矩阵把原图 RGB 强制为白色，仅保留经 opacity 缩放的 alpha，形成残影。
    void DrawWhiteTrail(Gdiplus::Graphics& graphics, Gdiplus::Image* image, const Gdiplus::RectF& destination, float opacity)
    {
        if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok)
        {
            return;
        }

        Gdiplus::ColorMatrix whiteMatrix =
        {
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, opacity, 0.0f,
            1.0f, 1.0f, 1.0f, 0.0f, 1.0f
        };
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&whiteMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
        graphics.DrawImage(
            image,
            destination,
            0.0f,
            0.0f,
            static_cast<float>(image->GetWidth()),
            static_cast<float>(image->GetHeight()),
            Gdiplus::UnitPixel,
            &attributes);
    }

    void DrawImageWithOpacity(Gdiplus::Graphics& graphics, Gdiplus::Image* image, const Gdiplus::RectF& destination, float opacity)
    {
        if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok)
        {
            return;
        }

        const float alpha = ClampFloat(opacity, 0.0f, 1.0f);
        Gdiplus::ColorMatrix alphaMatrix =
        {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, alpha, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f
        };
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&alphaMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
        graphics.DrawImage(
            image,
            destination,
            0.0f,
            0.0f,
            static_cast<float>(image->GetWidth()),
            static_cast<float>(image->GetHeight()),
            Gdiplus::UnitPixel,
            &attributes);
    }

    // 扫描非透明包围盒，自动把锚点设为可见像素底边中心（角色脚底）。
    SpriteAnimation::Frame MakeFrame(Gdiplus::Bitmap* image)
    {
        SpriteAnimation::Frame frame;
        frame.image.reset(image);

        if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok)
        {
            return frame;
        }

        const UINT width = image->GetWidth();
        const UINT height = image->GetHeight();
        UINT left = width;
        UINT top = height;
        UINT right = 0;
        UINT bottom = 0;
        bool foundPixel = false;

        // alpha <= 12 视作透明，容忍导出素材边缘的少量半透明噪点。
        for (UINT y = 0; y < height; ++y)
        {
            for (UINT x = 0; x < width; ++x)
            {
                Gdiplus::Color color;
                if (image->GetPixel(x, y, &color) == Gdiplus::Ok && color.GetAlpha() > 12)
                {
                    foundPixel = true;
                    if (x < left)
                    {
                        left = x;
                    }
                    if (x + 1 > right)
                    {
                        right = x + 1;
                    }
                    if (y < top)
                    {
                        top = y;
                    }
                    if (y + 1 > bottom)
                    {
                        bottom = y + 1;
                    }
                }
            }
        }

        if (foundPixel)
        {
            frame.anchorX = (static_cast<float>(left) + static_cast<float>(right)) * 0.5f;
            frame.anchorY = static_cast<float>(bottom);
            frame.visualHeight = static_cast<float>(bottom - top);
        }

        return frame;
    }

    SpriteAnimation::Frame MakeAnchoredFrame(Gdiplus::Bitmap* image, float anchorX, float anchorY, float visualHeight)
    {
        SpriteAnimation::Frame frame;
        frame.image.reset(image);
        frame.anchorX = anchorX;
        frame.anchorY = anchorY;
        frame.visualHeight = visualHeight;
        return frame;
    }

    SpriteAnimation::Frame MakeMeasuredAnchoredFrame(Gdiplus::Bitmap* image, float anchorX, float anchorY)
    {
        SpriteAnimation::Frame frame = MakeFrame(image);
        frame.anchorX = anchorX;
        frame.anchorY = anchorY;
        frame.useMeasuredVisualHeight = true;
        return frame;
    }

    Gdiplus::Bitmap* LoadBitmap(const std::wstring& relativePath)
    {
        const std::wstring path = FindAssetPath(relativePath);
        Gdiplus::Bitmap* image = Gdiplus::Bitmap::FromFile(path.c_str(), FALSE);
        if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok)
        {
            delete image;
            return nullptr;
        }
        return image;
    }

    // 大型精灵表需要严格尺寸才能安全按固定网格裁帧。
    Gdiplus::Bitmap* LoadBitmapWithExpectedSize(
        const std::wstring& relativePath,
        UINT expectedWidth,
        UINT expectedHeight)
    {
        Gdiplus::Bitmap* image = LoadBitmap(relativePath);
        if (image == nullptr)
        {
            return nullptr;
        }
        if (image->GetWidth() != expectedWidth || image->GetHeight() != expectedHeight)
        {
            delete image;
            return nullptr;
        }
        return image;
    }

    Gdiplus::Bitmap* LoadBitmapWithFallback(const std::wstring& relativePath, const std::wstring& fallbackRelativePath)
    {
        Gdiplus::Bitmap* image = LoadBitmap(relativePath);
        if (image != nullptr)
        {
            return image;
        }
        return LoadBitmap(fallbackRelativePath);
    }

    // 将固定 12 FPS 的逻辑采样点重映射到任意数量的源帧，首尾帧均可到达。
    int ResampleFrameIndex(float elapsedSeconds, float durationSeconds, int sourceFrameCount)
    {
        const int sampleCount = static_cast<int>(std::ceil(durationSeconds * kAnimationFramesPerSecond));
        if (sampleCount <= 1 || sourceFrameCount <= 1)
        {
            return 0;
        }

        const int sampleIndex = (std::min)(
            static_cast<int>(elapsedSeconds * kAnimationFramesPerSecond),
            sampleCount - 1);
        const float normalized = static_cast<float>(sampleIndex) / static_cast<float>(sampleCount - 1);
        return (std::min)(
            static_cast<int>(std::round(normalized * static_cast<float>(sourceFrameCount - 1))),
            sourceFrameCount - 1);
    }

    int TimelineFrameIndex(float elapsedSeconds, float durationSeconds, int sourceFrameCount)
    {
        if (durationSeconds <= 0.0f || sourceFrameCount <= 1)
        {
            return 0;
        }

        const float normalized = ClampFloat(elapsedSeconds / durationSeconds, 0.0f, 1.0f);
        return (std::min)(
            static_cast<int>(normalized * static_cast<float>(sourceFrameCount)),
            sourceFrameCount - 1);
    }
}

SpriteAnimation::~SpriteAnimation()
{
    Clear();
}

// 按动作批量加载帧。函数会先 Clear，因此重载时不会混用旧资源。
bool SpriteAnimation::LoadQiongSprites()
{
    Clear();

    bool loaded = true;
    // 当前运行时角色视觉只使用左右两向。旧的上下素材仍保留在磁盘中以便回退，
    // 但不再载入内存；逻辑上的上下移动会选择最近的水平朝向。
    for (int frame = 0; frame < kIdleFrameCount; ++frame)
    {
        loaded = LoadSideOnlyV3Frame(
            idleLeft_,
            MakeSideOnlyV3Path(L"idle", L"left", frame),
            MakeSideOnlyV2Path(L"idle", L"left", frame),
            MakeReadyPath(L"left"),
            32.0f,
            60.0f,
            37.0f) && loaded;
        loaded = LoadSideOnlyV3Frame(
            idleRight_,
            MakeSideOnlyV3Path(L"idle", L"right", frame),
            MakeSideOnlyV2Path(L"idle", L"right", frame),
            MakeReadyPath(L"right"),
            32.0f,
            60.0f,
            37.0f) && loaded;
    }

    loaded = LoadSideOnlyV3Frame(
        unarmedLeft_,
        MakeSideOnlyV3Path(L"unarmed_idle", L"left", 0),
        MakeUnarmedPath(L"left"),
        MakeUnarmedPath(L"left"),
        32.0f,
        60.0f,
        50.0f) && loaded;
    loaded = LoadSideOnlyV3Frame(
        unarmedRight_,
        MakeSideOnlyV3Path(L"unarmed_idle", L"right", 0),
        MakeUnarmedPath(L"right"),
        MakeUnarmedPath(L"right"),
        32.0f,
        60.0f,
        50.0f) && loaded;

    for (int frame = 0; frame < kWalkFrameCount; ++frame)
    {
        const int v2Frame = frame % kSideOnlyV2WalkFrameCount;
        loaded = LoadSideOnlyV3Frame(
            walkLeft_,
            MakeSideOnlyV3Path(L"walk", L"left", frame),
            MakeSideOnlyV2Path(L"walk", L"left", v2Frame),
            MakeWalkPath(L"left", frame),
            32.0f,
            60.0f,
            38.0f) && loaded;
        loaded = LoadSideOnlyV3Frame(
            walkRight_,
            MakeSideOnlyV3Path(L"walk", L"right", frame),
            MakeSideOnlyV2Path(L"walk", L"right", v2Frame),
            MakeWalkPath(L"right", frame),
            32.0f,
            60.0f,
            38.0f) && loaded;

        loaded = LoadSideOnlyV3Frame(
            unarmedWalkLeft_,
            MakeSideOnlyV3Path(L"unarmed_walk", L"left", frame),
            MakeUnarmedWalkPath(L"left", frame),
            MakeUnarmedWalkPath(L"left", frame),
            32.0f,
            60.0f,
            50.0f) && loaded;
        loaded = LoadSideOnlyV3Frame(
            unarmedWalkRight_,
            MakeSideOnlyV3Path(L"unarmed_walk", L"right", frame),
            MakeUnarmedWalkPath(L"right", frame),
            MakeUnarmedWalkPath(L"right", frame),
            32.0f,
            60.0f,
            50.0f) && loaded;
    }

    loaded = LoadFrame(crouchLeft_, MakeCrouchPath(L"left"), 32.0f, 60.0f, 44.0f) && loaded;
    loaded = LoadFrame(crouchRight_, MakeCrouchPath(L"right"), 32.0f, 60.0f, 44.0f) && loaded;

    for (int frame = 0; frame < kBlockFrameCount; ++frame)
    {
        loaded = LoadFrame(blockLeft_, MakeBlockPath(L"left", frame), 32.0f, 60.0f, 50.0f) && loaded;
        loaded = LoadFrame(blockRight_, MakeBlockPath(L"right", frame), 32.0f, 60.0f, 50.0f) && loaded;
        loaded = LoadFrame(unarmedBlockLeft_, MakeUnarmedBlockPath(L"left", frame), 32.0f, 60.0f, 50.0f) && loaded;
        loaded = LoadFrame(unarmedBlockRight_, MakeUnarmedBlockPath(L"right", frame), 32.0f, 60.0f, 50.0f) && loaded;
    }

    for (int frame = 0; frame < kAttackFrameCount; ++frame)
    {
        loaded = LoadSideOnlyV3Frame(
            attackLeft_,
            MakeSideOnlyV3Path(L"attack", L"left", frame),
            MakeSideOnlyV2Path(L"attack", L"left", frame),
            MakeTenFrameActionPath(L"bat_swing", L"left", L"bat_swing", frame),
            32.0f,
            60.0f,
            56.0f) && loaded;
        loaded = LoadSideOnlyV3Frame(
            attackRight_,
            MakeSideOnlyV3Path(L"attack", L"right", frame),
            MakeSideOnlyV2Path(L"attack", L"right", frame),
            MakeTenFrameActionPath(L"bat_swing", L"right", L"bat_swing", frame),
            32.0f,
            60.0f,
            56.0f) && loaded;
        loaded = LoadFrame(attackVfxLeft_, MakeTenFrameActionPath(L"bat_swing", L"left", L"bat_swing_vfx", frame, true)) && loaded;
        loaded = LoadFrame(attackVfxRight_, MakeTenFrameActionPath(L"bat_swing", L"right", L"bat_swing_vfx", frame, true)) && loaded;

        loaded = LoadFrame(punchLeft_, MakeTenFrameActionPath(L"punch", L"left", L"punch", frame), 32.0f, 60.0f, 50.0f) && loaded;
        loaded = LoadFrame(punchRight_, MakeTenFrameActionPath(L"punch", L"right", L"punch", frame), 32.0f, 60.0f, 50.0f) && loaded;
        loaded = LoadFrame(punchVfxLeft_, MakeTenFrameActionPath(L"punch", L"left", L"punch_vfx", frame, true)) && loaded;
        loaded = LoadFrame(punchVfxRight_, MakeTenFrameActionPath(L"punch", L"right", L"punch_vfx", frame, true)) && loaded;
    }

    for (int frame = 0; frame < kThrowFrameCount; ++frame)
    {
        loaded = LoadFrame(throwLeft_, MakeFramePath(L"bat_throw", L"left", L"bat_throw", frame), 32.0f, 56.0f, 50.0f) && loaded;
        loaded = LoadFrame(throwRight_, MakeFramePath(L"bat_throw", L"right", L"bat_throw", frame), 32.0f, 53.0f, 50.0f) && loaded;
        loaded = LoadFrame(throwVfxLeft_, MakeFramePath(L"bat_throw", L"left", L"bat_throw_vfx", frame, true)) && loaded;
        loaded = LoadFrame(throwVfxRight_, MakeFramePath(L"bat_throw", L"right", L"bat_throw_vfx", frame, true)) && loaded;
    }

    for (int frame = 0; frame < kDeathFrameCount; ++frame)
    {
        loaded = LoadFrame(
            deathLeft_,
            MakeSideOnlyV3Path(L"death", L"left", frame),
            kSideOnlyV3PivotX,
            kSideOnlyV3PivotY,
            56.0f) && loaded;
        loaded = LoadFrame(
            deathRight_,
            MakeSideOnlyV3Path(L"death", L"right", frame),
            kSideOnlyV3PivotX,
            kSideOnlyV3PivotY,
            56.0f) && loaded;
    }

    ultimateActivationActorSheet_.reset(LoadBitmapWithExpectedSize(
        MakeUltimateActivationActorSheetPath(),
        static_cast<UINT>(kUltimateActorFrameSize * kUltimateFrameCount),
        static_cast<UINT>(kUltimateActorFrameSize * 4)));
    loaded = ultimateActivationActorSheet_ != nullptr && loaded;

    ultimateThrowCompositeSheet_.reset(LoadBitmapWithExpectedSize(
        MakeUltimateThrowCompositeSheetPath(),
        static_cast<UINT>(kUltimateActorFrameSize * kThrowFrameCount),
        static_cast<UINT>(kUltimateActorFrameSize * 4)));
    loaded = ultimateThrowCompositeSheet_ != nullptr && loaded;

    superBatProjectileSheet_.reset(LoadBitmapWithExpectedSize(
        MakeSuperBatProjectileSheetPath(),
        static_cast<UINT>(kUltimateActorFrameSize * kSuperBatProjectileFrameCount),
        static_cast<UINT>(kUltimateActorFrameSize)));
    loaded = superBatProjectileSheet_ != nullptr && loaded;

    for (int frame = 0; frame < kSuperBatExplosionFrameCount; ++frame)
    {
        superBatExplosionFrames_[frame].reset(LoadBitmapWithExpectedSize(
            MakeSuperBatExplosionFramePath(frame),
            static_cast<UINT>(kUltimateComboFrameSize),
            static_cast<UINT>(kUltimateComboFrameSize)));
        loaded = superBatExplosionFrames_[frame] != nullptr && loaded;
    }

    return loaded;
}

// 将基础/战斗动画时间轴与所有待消费关键帧恢复到初始状态，不释放图片。
void SpriteAnimation::ResetState()
{
    EndUltimateVfx();
    frameTimer_ = 0.0f;
    walkPhase_ = 0.0f;
    currentFrame_ = 0;
    activeAnimation_ = CharacterAnimation::Idle;
    transitionSourceAnimation_ = CharacterAnimation::Idle;
    transitionSourceFrame_ = 0;
    transitionTimer_ = 0.0f;
    transitionDuration_ = 0.0f;
    animationFinished_ = false;
    attackHitFramePending_ = false;
    attackHitTriggered_ = false;
    throwReleaseFramePending_ = false;
    throwReleaseTriggered_ = false;
}

// 武器状态改变会影响之后 GetFrameSet 对持棒/空手动作组的选择。
void SpriteAnimation::SetHasBat(bool hasBat)
{
    hasBat_ = hasBat;
}

// 推进活动动作并在跨过命中/释放时刻时置位 pending；用“跨越时间点”而不是
// “恰好等于某帧”判断，可防止低帧率下一步跨过关键帧导致事件丢失。
void SpriteAnimation::Update(float deltaSeconds, CharacterAnimation animation)
{
    BeginAnimationTransition(animation);
    transitionTimer_ = (std::max)(0.0f, transitionTimer_ - deltaSeconds);

    if (animation == CharacterAnimation::Crouch)
    {
        currentFrame_ = 0;
        frameTimer_ = 0.0f;
        return;
    }

    if (animation == CharacterAnimation::Idle)
    {
        frameTimer_ += deltaSeconds;
        currentFrame_ = static_cast<int>(frameTimer_ * kIdleFramesPerSecond) % kIdleFrameCount;
        return;
    }

    if (animation == CharacterAnimation::Walk)
    {
        frameTimer_ += deltaSeconds;
        walkPhase_ += deltaSeconds * 12.0f;
        while (frameTimer_ >= kAnimationFrameSeconds)
        {
            frameTimer_ -= kAnimationFrameSeconds;
            currentFrame_ = (currentFrame_ + 1) % kWalkFrameCount;
        }
        return;
    }

    if (animation == CharacterAnimation::Block)
    {
        frameTimer_ += deltaSeconds;
        const int sampledFrame = static_cast<int>(frameTimer_ * kAnimationFramesPerSecond);
        currentFrame_ = sampledFrame < kBlockFrameCount
            ? sampledFrame
            : 6 + (sampledFrame - kBlockFrameCount) % 4;
        return;
    }

    if (animation == CharacterAnimation::Throw)
    {
        const float previousElapsed = frameTimer_;
        frameTimer_ = (std::min)(frameTimer_ + deltaSeconds, kThrowDurationSeconds);
        currentFrame_ = ResampleFrameIndex(frameTimer_, kThrowDurationSeconds, kThrowFrameCount);
        if (!throwReleaseTriggered_ && previousElapsed < kThrowReleaseTimeSeconds && frameTimer_ >= kThrowReleaseTimeSeconds)
        {
            throwReleaseTriggered_ = true;
            throwReleaseFramePending_ = true;
        }
        if (frameTimer_ >= kThrowDurationSeconds)
        {
            animationFinished_ = true;
        }
        return;
    }

    if (animation == CharacterAnimation::Death)
    {
        frameTimer_ = (std::min)(frameTimer_ + deltaSeconds, kDeathDurationSeconds);
        currentFrame_ = TimelineFrameIndex(frameTimer_, kDeathDurationSeconds, kDeathFrameCount);
        if (frameTimer_ >= kDeathDurationSeconds)
        {
            animationFinished_ = true;
        }
        return;
    }

    // The authored side swing has ten distinct poses. Sample its full timeline
    // while preserving the authoritative 0.16s hit and 0.40s recovery timings.
    const float previousElapsed = frameTimer_;
    frameTimer_ = (std::min)(frameTimer_ + deltaSeconds, kAttackDurationSeconds);
    currentFrame_ = TimelineFrameIndex(frameTimer_, kAttackDurationSeconds, kAttackFrameCount);

    if (!attackHitTriggered_ && previousElapsed < kAttackHitTimeSeconds && frameTimer_ >= kAttackHitTimeSeconds)
    {
        attackHitTriggered_ = true;
        attackHitFramePending_ = true;
    }
    if (frameTimer_ >= kAttackDurationSeconds)
    {
        animationFinished_ = true;
    }
}

// 一次性动作启动时清空上一轮的 triggered/pending，确保新动作能产生新事件。
void SpriteAnimation::StartAttack()
{
    BeginAnimationTransition(CharacterAnimation::Attack);
    frameTimer_ = 0.0f;
    currentFrame_ = 0;
    animationFinished_ = false;
    attackHitFramePending_ = false;
    attackHitTriggered_ = false;
    throwReleaseFramePending_ = false;
    throwReleaseTriggered_ = false;
}

void SpriteAnimation::StartThrow()
{
    BeginAnimationTransition(CharacterAnimation::Throw);
    frameTimer_ = 0.0f;
    currentFrame_ = 0;
    animationFinished_ = false;
    attackHitFramePending_ = false;
    attackHitTriggered_ = false;
    throwReleaseFramePending_ = false;
    throwReleaseTriggered_ = false;
}

void SpriteAnimation::StartBlock()
{
    BeginAnimationTransition(CharacterAnimation::Block);
    frameTimer_ = 0.0f;
    currentFrame_ = 0;
    animationFinished_ = false;
    attackHitFramePending_ = false;
    attackHitTriggered_ = false;
    throwReleaseFramePending_ = false;
    throwReleaseTriggered_ = false;
}

void SpriteAnimation::StartDeath()
{
    BeginAnimationTransition(CharacterAnimation::Death);
    frameTimer_ = 0.0f;
    currentFrame_ = 0;
    animationFinished_ = false;
    attackHitFramePending_ = false;
    attackHitTriggered_ = false;
    throwReleaseFramePending_ = false;
    throwReleaseTriggered_ = false;
}

// 只有激活图集和五段连击图集完整可用时才启动，避免播放到中途缺帧。
bool SpriteAnimation::StartUltimateVfx(CharacterDirection direction)
{
    if (ultimateVfxActive_ && ultimateVfxDirection_ == direction)
    {
        return true;
    }
    EndUltimateVfx();
    ultimateVfxDirection_ = direction;

    if (ultimateActivationVfxSheet_ == nullptr)
    {
        ultimateActivationVfxSheet_.reset(LoadBitmapWithExpectedSize(
            MakeUltimateActivationSheetPath(),
            static_cast<UINT>(kUltimateActivationFrameSize * kUltimateFrameCount),
            static_cast<UINT>(kUltimateActivationFrameSize * 4)));
    }

    bool loaded = ultimateActivationVfxSheet_ != nullptr;
    for (int hitIndex = 0; hitIndex < kUltimateHitCount; ++hitIndex)
    {
        ultimateComboVfxSheets_[hitIndex].reset(LoadBitmapWithExpectedSize(
            MakeUltimateComboSheetPath(direction, hitIndex),
            static_cast<UINT>(kUltimateComboFrameSize * kUltimateFrameCount),
            static_cast<UINT>(kUltimateComboFrameSize)));
        loaded = ultimateComboVfxSheets_[hitIndex] != nullptr && loaded;
    }

    if (!loaded)
    {
        EndUltimateVfx();
        return false;
    }

    ultimateVfxActive_ = true;
    return true;
}

// 从四向十帧角色图集中按 normalizedProgress 选择行列，并对齐脚底锚点。
bool SpriteAnimation::DrawUltimateActivationActor(
    Gdiplus::Graphics& graphics,
    float centerX,
    float bottomY,
    CharacterDirection direction,
    float normalizedProgress) const
{
    Gdiplus::Bitmap* sheet = ultimateActivationActorSheet_.get();
    if (sheet == nullptr || sheet->GetLastStatus() != Gdiplus::Ok)
    {
        return false;
    }

    const float progress = ClampFloat(normalizedProgress, 0.0f, 1.0f);
    const int frameIndex = (std::min)(
        static_cast<int>(progress * static_cast<float>(kUltimateFrameCount)),
        kUltimateFrameCount - 1);
    const float charge = std::sin(progress * 3.1415926f);
    const float scale = 1.84f * (1.0f + charge * 0.055f);
    const float frameSize = static_cast<float>(kUltimateActorFrameSize);
    const float sourceX = static_cast<float>(frameIndex * kUltimateActorFrameSize);
    const float sourceY = static_cast<float>(GetDirectionRow(direction) * kUltimateActorFrameSize);
    const float left = centerX - 32.0f * scale;
    const float top = bottomY - 60.0f * scale - charge * 7.0f;
    graphics.DrawImage(
        sheet,
        Gdiplus::RectF(left, top, frameSize * scale, frameSize * scale),
        sourceX,
        sourceY,
        frameSize,
        frameSize,
        Gdiplus::UnitPixel);
    return true;
}

bool SpriteAnimation::DrawSuperThrowActor(
    Gdiplus::Graphics& graphics,
    float centerX,
    float bottomY,
    CharacterDirection direction) const
{
    Gdiplus::Bitmap* sheet = ultimateThrowCompositeSheet_.get();
    if (sheet == nullptr || sheet->GetLastStatus() != Gdiplus::Ok)
    {
        return false;
    }

    const int frameIndex = currentFrame_ % kThrowFrameCount;
    const float frameSize = static_cast<float>(kUltimateActorFrameSize);
    constexpr float scale = 1.82f;
    const float sourceX = static_cast<float>(frameIndex * kUltimateActorFrameSize);
    const float sourceY = static_cast<float>(GetDirectionRow(direction) * kUltimateActorFrameSize);
    graphics.DrawImage(
        sheet,
        Gdiplus::RectF(centerX - 32.0f * scale, bottomY - 60.0f * scale, frameSize * scale, frameSize * scale),
        sourceX,
        sourceY,
        frameSize,
        frameSize,
        Gdiplus::UnitPixel);
    return true;
}

// 强化光环由 pulseTime 周期驱动，与角色当前基础动作独立。
void SpriteAnimation::DrawSuperBatAura(
    Gdiplus::Graphics& graphics,
    float centerX,
    float bottomY,
    CharacterAnimation animation,
    CharacterDirection direction,
    float pulseTime) const
{
    if (!hasBat_ || animation == CharacterAnimation::Attack || animation == CharacterAnimation::Throw)
    {
        return;
    }

    const float facing = direction == CharacterDirection::Left ? -1.0f : 1.0f;
    float gripX = centerX + facing * 3.0f;
    float gripY = bottomY - 54.0f;
    float tipX = centerX - facing * 49.0f;
    float tipY = bottomY - 16.0f;
    if (animation == CharacterAnimation::Walk)
    {
        gripX = centerX + facing * 4.0f;
        gripY = bottomY - 66.0f;
        tipX = centerX + facing * 50.0f;
        tipY = bottomY - 112.0f;
    }
    else if (animation == CharacterAnimation::Block || animation == CharacterAnimation::Crouch)
    {
        gripX = centerX + facing * 2.0f;
        gripY = bottomY - 55.0f;
        tipX = centerX + facing * 42.0f;
        tipY = bottomY - 91.0f;
    }

    const float pulse = 0.5f + 0.5f * std::sin(pulseTime * 11.0f);
    const BYTE glowAlpha = static_cast<BYTE>(125.0f + pulse * 70.0f);
    Gdiplus::Pen outerGlow(Gdiplus::Color(glowAlpha, 22, 120, 255), 18.0f + pulse * 3.0f);
    Gdiplus::Pen innerGlow(Gdiplus::Color(235, 34, 210, 255), 9.0f);
    Gdiplus::Pen whiteCore(Gdiplus::Color(255, 226, 252, 255), 3.0f);
    graphics.DrawLine(&outerGlow, gripX, gripY, tipX, tipY);
    graphics.DrawLine(&innerGlow, gripX, gripY, tipX, tipY);
    graphics.DrawLine(&whiteCore, gripX, gripY, tipX, tipY);

    const float dx = tipX - gripX;
    const float dy = tipY - gripY;
    const float ringPositions[] = { 0.34f, 0.62f, 0.88f };
    for (int index = 0; index < 3; ++index)
    {
        const float ringPulse = pulse * 2.5f + static_cast<float>(index) * 1.5f;
        const float radius = 7.0f + std::sin(ringPulse) * 1.5f;
        const float x = gripX + dx * ringPositions[index];
        const float y = gripY + dy * ringPositions[index];
        Gdiplus::Pen ringGlow(Gdiplus::Color(glowAlpha, 26, 136, 255), 6.0f);
        Gdiplus::Pen ringCore(Gdiplus::Color(245, 104, 236, 255), 2.0f);
        graphics.DrawEllipse(&ringGlow, x - radius, y - radius, radius * 2.0f, radius * 2.0f);
        graphics.DrawEllipse(&ringCore, x - radius, y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void SpriteAnimation::DrawSuperBatProjectile(
    Gdiplus::Graphics& graphics,
    float centerX,
    float centerY,
    Vec2 direction,
    float rotationDegrees) const
{
    const float directionLength = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (directionLength > 0.0001f)
    {
        direction.x /= directionLength;
        direction.y /= directionLength;
    }
    const Vec2 side{ -direction.y, direction.x };
    for (int streak = -1; streak <= 1; ++streak)
    {
        const float offset = static_cast<float>(streak) * 7.0f;
        const float startX = centerX - direction.x * 92.0f + side.x * offset;
        const float startY = centerY - direction.y * 92.0f + side.y * offset;
        const float endX = centerX - direction.x * 18.0f + side.x * offset * 0.35f;
        const float endY = centerY - direction.y * 18.0f + side.y * offset * 0.35f;
        Gdiplus::Pen trailGlow(Gdiplus::Color(88, 18, 104, 255), 13.0f - std::abs(streak) * 2.0f);
        Gdiplus::Pen trailCore(Gdiplus::Color(185, 86, 232, 255), 3.0f);
        graphics.DrawLine(&trailGlow, startX, startY, endX, endY);
        graphics.DrawLine(&trailCore, startX, startY, endX, endY);
    }

    Gdiplus::Bitmap* sheet = superBatProjectileSheet_.get();
    if (sheet == nullptr || sheet->GetLastStatus() != Gdiplus::Ok)
    {
        Gdiplus::SolidBrush fallback(Gdiplus::Color(255, 100, 232, 255));
        graphics.FillEllipse(&fallback, centerX - 24.0f, centerY - 24.0f, 48.0f, 48.0f);
        return;
    }

    int frameIndex = static_cast<int>(std::abs(rotationDegrees) / 45.0f) % kSuperBatProjectileFrameCount;
    constexpr float drawSize = 82.0f;
    graphics.DrawImage(
        sheet,
        Gdiplus::RectF(centerX - drawSize * 0.5f, centerY - drawSize * 0.5f, drawSize, drawSize),
        static_cast<float>(frameIndex * kUltimateActorFrameSize),
        0.0f,
        static_cast<float>(kUltimateActorFrameSize),
        static_cast<float>(kUltimateActorFrameSize),
        Gdiplus::UnitPixel);
}

void SpriteAnimation::DrawSuperBatExplosion(
    Gdiplus::Graphics& graphics,
    float centerX,
    float centerY,
    float radius,
    float normalizedProgress) const
{
    const float progress = ClampFloat(normalizedProgress, 0.0f, 1.0f);
    const int frameIndex = (std::min)(
        static_cast<int>(progress * static_cast<float>(kSuperBatExplosionFrameCount)),
        kSuperBatExplosionFrameCount - 1);
    Gdiplus::Bitmap* image = superBatExplosionFrames_[frameIndex].get();
    const float drawSize = radius * 2.35f;
    if (image != nullptr && image->GetLastStatus() == Gdiplus::Ok)
    {
        graphics.DrawImage(
            image,
            Gdiplus::RectF(centerX - drawSize * 0.5f, centerY - drawSize * 0.5f, drawSize, drawSize),
            0.0f,
            0.0f,
            static_cast<float>(kUltimateComboFrameSize),
            static_cast<float>(kUltimateComboFrameSize),
            Gdiplus::UnitPixel);
        return;
    }

    const BYTE alpha = static_cast<BYTE>((1.0f - progress) * 230.0f);
    const float fallbackRadius = radius * (0.2f + progress * 0.8f);
    Gdiplus::Pen fallbackGlow(Gdiplus::Color(alpha / 2, 20, 122, 255), 18.0f);
    Gdiplus::Pen fallbackCore(Gdiplus::Color(alpha, 120, 244, 255), 5.0f);
    graphics.DrawEllipse(&fallbackGlow, centerX - fallbackRadius, centerY - fallbackRadius, fallbackRadius * 2.0f, fallbackRadius * 2.0f);
    graphics.DrawEllipse(&fallbackCore, centerX - fallbackRadius, centerY - fallbackRadius, fallbackRadius * 2.0f, fallbackRadius * 2.0f);
}

// stage 选择五段图集，normalizedProgress 选择该段的十帧时间轴。
void SpriteAnimation::DrawUltimateVfx(
    Gdiplus::Graphics& graphics,
    float worldX,
    float worldY,
    int stage,
    float normalizedProgress) const
{
    if (!ultimateVfxActive_)
    {
        return;
    }

    const float progress = std::isfinite(normalizedProgress)
        ? ClampFloat(normalizedProgress, 0.0f, 1.0f)
        : 0.0f;
    const int frameIndex = (std::min)(
        static_cast<int>(progress * static_cast<float>(kUltimateFrameCount)),
        kUltimateFrameCount - 1);

    if (stage == -1)
    {
        Gdiplus::Bitmap* image = ultimateActivationVfxSheet_.get();
        if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok)
        {
            return;
        }

        const float frameSize = static_cast<float>(kUltimateActivationFrameSize);
        const float sourceX = static_cast<float>(frameIndex * kUltimateActivationFrameSize);
        const float sourceY = static_cast<float>(GetDirectionRow(ultimateVfxDirection_) * kUltimateActivationFrameSize);
        graphics.DrawImage(
            image,
            Gdiplus::RectF(worldX - frameSize * 0.5f, worldY - frameSize * 0.5f, frameSize, frameSize),
            sourceX,
            sourceY,
            frameSize,
            frameSize,
            Gdiplus::UnitPixel);
        return;
    }

    if (stage < 0 || stage >= kUltimateHitCount)
    {
        return;
    }

    Gdiplus::Bitmap* image = ultimateComboVfxSheets_[stage].get();
    if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok)
    {
        return;
    }

    const float frameSize = static_cast<float>(kUltimateComboFrameSize);
    const float sourceX = static_cast<float>(frameIndex * kUltimateComboFrameSize);
    graphics.DrawImage(
        image,
        Gdiplus::RectF(worldX - frameSize * 0.5f, worldY - frameSize * 0.5f, frameSize, frameSize),
        sourceX,
        0.0f,
        frameSize,
        frameSize,
        Gdiplus::UnitPixel);
}

void SpriteAnimation::EndUltimateVfx()
{
    for (std::unique_ptr<Gdiplus::Bitmap>& sheet : ultimateComboVfxSheets_)
    {
        sheet.reset();
    }
    ultimateVfxActive_ = false;
}

// 保存旧动作当前帧，在 transitionDuration 内与新动作首帧做透明度交叉淡化。
void SpriteAnimation::BeginAnimationTransition(CharacterAnimation animation)
{
    if (animation == activeAnimation_)
    {
        return;
    }

    transitionSourceAnimation_ = activeAnimation_;
    transitionSourceFrame_ = currentFrame_;
    transitionDuration_ = GetAnimationBlendDuration(activeAnimation_, animation);
    transitionTimer_ = transitionDuration_;
    activeAnimation_ = animation;
}

bool SpriteAnimation::IsAnimationFinished() const
{
    return animationFinished_;
}

bool SpriteAnimation::IsAttackMovementLocked() const
{
    return activeAnimation_ == CharacterAnimation::Attack && frameTimer_ < kAttackMovementLockSeconds;
}

bool SpriteAnimation::CanBufferAttack(float lookaheadSeconds) const
{
    const float anticipatedFrameTime = frameTimer_ + (std::max)(0.0f, lookaheadSeconds);
    return activeAnimation_ == CharacterAnimation::Attack &&
        anticipatedFrameTime >= kAttackBufferStartSeconds &&
        frameTimer_ < kAttackDurationSeconds;
}

// 标准的一次性事件消费模式：复制值 -> 清零 -> 返回。
bool SpriteAnimation::ConsumeAttackHitFrame()
{
    const bool pending = attackHitFramePending_;
    attackHitFramePending_ = false;
    return pending;
}

bool SpriteAnimation::ConsumeThrowReleaseFrame()
{
    const bool pending = throwReleaseFramePending_;
    throwReleaseFramePending_ = false;
    return pending;
}

// 统一基础角色绘制：选帧、计算视觉高度缩放、按锚点定位，再叠加动作 VFX/过渡。
void SpriteAnimation::Draw(
    Gdiplus::Graphics& graphics,
    float centerX,
    float bottomY,
    CharacterAnimation animation,
    CharacterDirection visualDirection,
    CharacterDirection actionDirection,
    bool flash) const
{
    CharacterAnimation renderAnimation = animation;
    int renderFrameIndex = currentFrame_;
    float renderOpacity = 1.0f;
    bool renderingTargetAnimation = true;

    if (transitionTimer_ > 0.0f && transitionDuration_ > 0.0f)
    {
        const float progress = ClampFloat(1.0f - transitionTimer_ / transitionDuration_, 0.0f, 1.0f);
        if (progress < 0.5f)
        {
            renderAnimation = transitionSourceAnimation_;
            renderFrameIndex = transitionSourceFrame_;
            renderOpacity = 1.0f - progress * 1.30f;
            renderingTargetAnimation = false;
        }
        else
        {
            renderOpacity = 0.35f + (progress - 0.5f) * 1.30f;
        }
    }

    const FrameSet& frameSet = GetFrameSet(renderAnimation, visualDirection);
    if (frameSet.frames.empty())
    {
        return;
    }

    const int index = renderFrameIndex % static_cast<int>(frameSet.frames.size());
    const Frame& frame = frameSet.frames[index];
    Gdiplus::Image* image = frame.image.get();
    if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok)
    {
        return;
    }

    const float imageWidth = static_cast<float>(image->GetWidth());
    const float imageHeight = static_cast<float>(image->GetHeight());
    if (imageWidth <= 0.0f || imageHeight <= 0.0f)
    {
        return;
    }

    float scale = static_cast<float>(kDrawSize) / imageHeight;
    if (frame.useMeasuredVisualHeight && frame.visualHeight > 0.0f)
    {
        // v3 uses a larger 96x96 canvas. Normalize from the actual alpha-bounds
        // height instead of the canvas so the actor keeps the established body
        // size while the fixed (48, 90) pivot keeps every foot planted.
        scale = ClampFloat(kTargetBodyHeight / frame.visualHeight, 1.0f, 2.60f);
    }
    else if (renderAnimation == CharacterAnimation::Attack)
    {
        scale = static_cast<float>(hasBat_ ? kBatAttackDrawSize : kUnarmedAttackDrawSize) / imageHeight;
    }
    else if (renderAnimation == CharacterAnimation::Throw)
    {
        scale = static_cast<float>(kThrowDrawSize) / imageHeight;
    }
    else if (renderAnimation == CharacterAnimation::Crouch)
    {
        scale = hasBat_ ? kCrouchScale : kUnarmedCrouchScale;
    }
    else
    {
        const float normalizedScale = kTargetBodyHeight / frame.visualHeight;
        scale = ClampFloat(normalizedScale, 1.62f, 2.60f);
    }

    const float drawWidth = imageWidth * scale;
    const float drawHeight = imageHeight * scale;
    const float left = centerX - frame.anchorX * scale;
    const float top = bottomY - frame.anchorY * scale;
    DrawImageWithOpacity(graphics, image, Gdiplus::RectF(left, top, drawWidth, drawHeight), renderOpacity);

    if (flash)
    {
        DrawWhiteTrail(graphics, image, Gdiplus::RectF(left, top, drawWidth, drawHeight), 0.88f);
    }

    if (renderingTargetAnimation && animation == CharacterAnimation::Block)
    {
        float startAngle = 20.0f;
        if (actionDirection == CharacterDirection::Up)
        {
            startAngle = 200.0f;
        }
        else if (actionDirection == CharacterDirection::Left)
        {
            startAngle = 110.0f;
        }
        else if (actionDirection == CharacterDirection::Right)
        {
            startAngle = -70.0f;
        }

        Gdiplus::Pen outerGuard(Gdiplus::Color(150, 255, 190, 48), 7.0f);
        Gdiplus::Pen innerGuard(Gdiplus::Color(230, 255, 255, 255), 3.0f);
        const Gdiplus::RectF guardRect(centerX - 47.0f, bottomY - 86.0f, 94.0f, 76.0f);
        graphics.DrawArc(&outerGuard, guardRect, startAngle, 140.0f);
        graphics.DrawArc(&innerGuard, guardRect, startAngle, 140.0f);
    }

    if (renderingTargetAnimation && (animation == CharacterAnimation::Attack || animation == CharacterAnimation::Throw))
    {
        float directionX = 0.0f;
        float directionY = 1.0f;
        float directionAngle = 90.0f;
        if (actionDirection == CharacterDirection::Left)
        {
            directionX = -1.0f;
            directionY = 0.0f;
            directionAngle = 180.0f;
        }
        else if (actionDirection == CharacterDirection::Right)
        {
            directionX = 1.0f;
            directionY = 0.0f;
            directionAngle = 0.0f;
        }
        else if (actionDirection == CharacterDirection::Up)
        {
            directionX = 0.0f;
            directionY = -1.0f;
            directionAngle = 270.0f;
        }

        const float effectCenterY = bottomY - 56.0f;
        const float eventTime = animation == CharacterAnimation::Throw
            ? kThrowReleaseTimeSeconds
            : kAttackHitTimeSeconds;
        const float halfWindow = animation == CharacterAnimation::Throw ? 0.10f : 0.12f;
        const float intensity = ClampFloat(1.0f - std::abs(frameTimer_ - eventTime) / halfWindow, 0.0f, 1.0f);
        const BYTE alpha = static_cast<BYTE>(intensity * 235.0f);

        if (intensity > 0.0f && animation == CharacterAnimation::Attack && hasBat_)
        {
            constexpr float kBatGlowHalfWidth = 7.5f;
            const float radius = CombatTuning::BatAttackReach - kBatGlowHalfWidth;
            const Gdiplus::RectF arcRect(centerX - radius, effectCenterY - radius, radius * 2.0f, radius * 2.0f);
            Gdiplus::Pen arcGlow(Gdiplus::Color(alpha / 2, 74, 176, 255), 15.0f);
            Gdiplus::Pen arcCore(Gdiplus::Color(alpha, 190, 232, 255), 5.0f);
            graphics.DrawArc(&arcGlow, arcRect, directionAngle - 62.0f, 124.0f);
            graphics.DrawArc(&arcCore, arcRect, directionAngle - 62.0f, 124.0f);
        }
        else if (intensity > 0.0f && animation == CharacterAnimation::Attack)
        {
            const float startDistance = 15.0f;
            const float reach = CombatTuning::PunchAttackReach;
            const float startX = centerX + directionX * startDistance;
            const float startY = effectCenterY + directionY * startDistance;
            const float endX = centerX + directionX * reach;
            const float endY = effectCenterY + directionY * reach;
            Gdiplus::Pen punchGlow(Gdiplus::Color(alpha / 2, 255, 116, 36), 9.0f);
            Gdiplus::Pen punchCore(Gdiplus::Color(alpha, 255, 255, 255), 3.0f);
            graphics.DrawLine(&punchGlow, startX, startY, endX, endY);
            graphics.DrawLine(&punchCore, startX, startY, endX, endY);
        }
        else if (intensity > 0.0f)
        {
            const float radius = CombatTuning::ThrowReleaseReach;
            Gdiplus::Pen releaseGlow(Gdiplus::Color(alpha / 2, 90, 220, 255), 7.0f);
            Gdiplus::Pen releaseCore(Gdiplus::Color(alpha, 255, 255, 255), 2.0f);
            graphics.DrawEllipse(&releaseGlow, centerX - radius, effectCenterY - radius, radius * 2.0f, radius * 2.0f);
            graphics.DrawEllipse(&releaseCore, centerX - radius, effectCenterY - radius, radius * 2.0f, radius * 2.0f);
        }
    }
}

bool SpriteAnimation::IsLoaded() const
{
    return !idleLeft_.frames.empty() && !idleRight_.frames.empty() &&
        !deathLeft_.frames.empty() && !deathRight_.frames.empty();
}

// 加载成功才 push_back，调用者可通过 FrameSet 是否为空判断整组素材可用性。
bool SpriteAnimation::LoadFrame(FrameSet& frameSet, const std::wstring& relativePath)
{
    Gdiplus::Bitmap* image = LoadBitmap(relativePath);
    if (image == nullptr)
    {
        return false;
    }

    frameSet.frames.push_back(MakeFrame(image));
    return true;
}

bool SpriteAnimation::LoadFrame(FrameSet& frameSet, const std::wstring& relativePath, float anchorX, float anchorY, float visualHeight)
{
    Gdiplus::Bitmap* image = LoadBitmap(relativePath);
    if (image == nullptr)
    {
        return false;
    }

    frameSet.frames.push_back(MakeAnchoredFrame(image, anchorX, anchorY, visualHeight));
    return true;
}

bool SpriteAnimation::LoadFrame(FrameSet& frameSet, const std::wstring& relativePath, const std::wstring& fallbackRelativePath, float anchorX, float anchorY, float visualHeight)
{
    Gdiplus::Bitmap* image = LoadBitmapWithFallback(relativePath, fallbackRelativePath);
    if (image == nullptr)
    {
        return false;
    }

    frameSet.frames.push_back(MakeAnchoredFrame(image, anchorX, anchorY, visualHeight));
    return true;
}

// v3 -> v2 -> legacy 逐级回退；v3 使用固定枢轴，旧素材沿用其各自锚点参数。
bool SpriteAnimation::LoadSideOnlyV3Frame(
    FrameSet& frameSet,
    const std::wstring& preferredRelativePath,
    const std::wstring& v2RelativePath,
    const std::wstring& legacyRelativePath,
    float fallbackAnchorX,
    float fallbackAnchorY,
    float fallbackVisualHeight)
{
    Gdiplus::Bitmap* v3Image = LoadBitmapWithExpectedSize(
        preferredRelativePath,
        kSideOnlyV3FrameSize,
        kSideOnlyV3FrameSize);
    if (v3Image != nullptr)
    {
        Frame frame = MakeMeasuredAnchoredFrame(
            v3Image,
            kSideOnlyV3PivotX,
            kSideOnlyV3PivotY);
        if (sideOnlyV3VisualHeight_ <= 0.0f)
        {
            sideOnlyV3VisualHeight_ = frame.visualHeight;
        }
        // v3 is authored with one fixed uniform scale per source master. Use
        // one measured runtime height for every v3 pose so the animation never
        // pumps in size as the bat or silhouette changes between frames.
        frame.visualHeight = sideOnlyV3VisualHeight_;
        frameSet.frames.push_back(std::move(frame));
        return true;
    }

    return LoadFrame(
        frameSet,
        v2RelativePath,
        legacyRelativePath,
        fallbackAnchorX,
        fallbackAnchorY,
        fallbackVisualHeight);
}

bool SpriteAnimation::LoadBottomCroppedFrame(FrameSet& frameSet, const std::wstring& relativePath, const std::wstring& fallbackRelativePath, float anchorX, float anchorY, float visualHeight, int sourceHeight)
{
    Gdiplus::Bitmap* source = LoadBitmapWithFallback(relativePath, fallbackRelativePath);
    if (source == nullptr)
    {
        return false;
    }

    const int sourceWidth = static_cast<int>(source->GetWidth());
    const int croppedHeight = static_cast<int>(std::min<UINT>(source->GetHeight(), static_cast<UINT>(sourceHeight)));
    Gdiplus::Bitmap* image = new Gdiplus::Bitmap(sourceWidth, croppedHeight, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(image);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.DrawImage(
        source,
        Gdiplus::RectF(0.0f, 0.0f, static_cast<float>(sourceWidth), static_cast<float>(croppedHeight)),
        0.0f,
        0.0f,
        static_cast<float>(sourceWidth),
        static_cast<float>(croppedHeight),
        Gdiplus::UnitPixel);

    delete source;
    frameSet.frames.push_back(MakeAnchoredFrame(image, anchorX, anchorY, visualHeight));
    return true;
}

bool SpriteAnimation::LoadOffsetFrame(FrameSet& frameSet, const std::wstring& relativePath, const std::wstring& fallbackRelativePath, float anchorX, float anchorY, float visualHeight, float offsetY)
{
    Gdiplus::Bitmap* source = LoadBitmapWithFallback(relativePath, fallbackRelativePath);
    if (source == nullptr)
    {
        return false;
    }

    const int sourceWidth = static_cast<int>(source->GetWidth());
    const int sourceHeight = static_cast<int>(source->GetHeight());
    const int offsetPixels = static_cast<int>(std::ceil(offsetY));
    Gdiplus::Bitmap* image = new Gdiplus::Bitmap(sourceWidth, sourceHeight + offsetPixels, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(image);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.DrawImage(
        source,
        Gdiplus::RectF(0.0f, offsetY, static_cast<float>(sourceWidth), static_cast<float>(sourceHeight)),
        0.0f,
        0.0f,
        static_cast<float>(sourceWidth),
        static_cast<float>(sourceHeight),
        Gdiplus::UnitPixel);

    delete source;
    frameSet.frames.push_back(MakeAnchoredFrame(image, anchorX, anchorY + offsetY, visualHeight));
    return true;
}

// 把动作、方向和持棒状态映射到帧组；空组由调用者继续采用安全回退帧。
const SpriteAnimation::FrameSet& SpriteAnimation::GetFrameSet(CharacterAnimation animation, CharacterDirection direction) const
{
    if (animation == CharacterAnimation::Death)
    {
        return direction == CharacterDirection::Left ? deathLeft_ : deathRight_;
    }

    if (animation == CharacterAnimation::Block)
    {
        if (!hasBat_)
        {
            switch (direction)
            {
            case CharacterDirection::Left:
                return unarmedBlockLeft_;
            case CharacterDirection::Right:
                return unarmedBlockRight_;
            case CharacterDirection::Up:
                return unarmedBlockUp_;
            case CharacterDirection::Down:
            default:
                return unarmedBlockDown_;
            }
        }

        switch (direction)
        {
        case CharacterDirection::Left:
            return blockLeft_;
        case CharacterDirection::Right:
            return blockRight_;
        case CharacterDirection::Up:
            return blockUp_;
        case CharacterDirection::Down:
        default:
            return blockDown_;
        }
    }

    if (animation == CharacterAnimation::Throw)
    {
        switch (direction)
        {
        case CharacterDirection::Left:
            return throwLeft_;
        case CharacterDirection::Right:
            return throwRight_;
        case CharacterDirection::Up:
            return throwUp_;
        case CharacterDirection::Down:
        default:
            return throwDown_;
        }
    }

    if (animation == CharacterAnimation::Attack)
    {
        if (!hasBat_)
        {
            switch (direction)
            {
            case CharacterDirection::Left:
                return punchLeft_;
            case CharacterDirection::Right:
                return punchRight_;
            case CharacterDirection::Up:
                return punchUp_;
            case CharacterDirection::Down:
            default:
                return punchDown_;
            }
        }

        switch (direction)
        {
        case CharacterDirection::Left:
            return attackLeft_;
        case CharacterDirection::Right:
            return attackRight_;
        case CharacterDirection::Up:
            return attackUp_;
        case CharacterDirection::Down:
        default:
            return attackDown_;
        }
    }

    if (animation == CharacterAnimation::Walk)
    {
        if (!hasBat_)
        {
            switch (direction)
            {
            case CharacterDirection::Left:
                return unarmedWalkLeft_;
            case CharacterDirection::Right:
                return unarmedWalkRight_;
            case CharacterDirection::Up:
                return unarmedWalkUp_;
            case CharacterDirection::Down:
            default:
                return unarmedWalkDown_;
            }
        }

        switch (direction)
        {
        case CharacterDirection::Left:
            return walkLeft_;
        case CharacterDirection::Right:
            return walkRight_;
        case CharacterDirection::Up:
            return walkUp_;
        case CharacterDirection::Down:
        default:
            return walkDown_;
        }
    }

    if (animation == CharacterAnimation::Crouch)
    {
        if (!hasBat_)
        {
            switch (direction)
            {
            case CharacterDirection::Left:
                return unarmedLeft_;
            case CharacterDirection::Right:
                return unarmedRight_;
            case CharacterDirection::Up:
                return unarmedUp_;
            case CharacterDirection::Down:
            default:
                return unarmedDown_;
            }
        }

        switch (direction)
        {
        case CharacterDirection::Left:
            return crouchLeft_;
        case CharacterDirection::Right:
            return crouchRight_;
        case CharacterDirection::Up:
            return crouchUp_;
        case CharacterDirection::Down:
        default:
            return crouchDown_;
        }
    }

    if (!hasBat_)
    {
        switch (direction)
        {
        case CharacterDirection::Left:
            return unarmedLeft_;
        case CharacterDirection::Right:
            return unarmedRight_;
        case CharacterDirection::Up:
            return unarmedUp_;
        case CharacterDirection::Down:
        default:
            return unarmedDown_;
        }
    }

    switch (direction)
    {
    case CharacterDirection::Left:
        return idleLeft_;
    case CharacterDirection::Right:
        return idleRight_;
    case CharacterDirection::Up:
        return idleUp_;
    case CharacterDirection::Down:
    default:
        return idleDown_;
    }
}

// 动作 VFX 与角色帧分开选取，但共享相同方向和帧索引以保持同步。
const SpriteAnimation::FrameSet& SpriteAnimation::GetActionVfxFrameSet(CharacterAnimation animation, CharacterDirection direction) const
{
    if (animation == CharacterAnimation::Throw)
    {
        switch (direction)
        {
        case CharacterDirection::Left:
            return throwVfxLeft_;
        case CharacterDirection::Right:
            return throwVfxRight_;
        case CharacterDirection::Up:
            return throwVfxUp_;
        case CharacterDirection::Down:
        default:
            return throwVfxDown_;
        }
    }

    if (!hasBat_)
    {
        switch (direction)
        {
        case CharacterDirection::Left:
            return punchVfxLeft_;
        case CharacterDirection::Right:
            return punchVfxRight_;
        case CharacterDirection::Up:
            return punchVfxUp_;
        case CharacterDirection::Down:
        default:
            return punchVfxDown_;
        }
    }

    switch (direction)
    {
    case CharacterDirection::Left:
        return attackVfxLeft_;
    case CharacterDirection::Right:
        return attackVfxRight_;
    case CharacterDirection::Up:
        return attackVfxUp_;
    case CharacterDirection::Down:
    default:
        return attackVfxDown_;
    }
}

// 释放全部 unique_ptr/FrameSet，并重置时间轴；可安全重复调用。
void SpriteAnimation::Clear()
{
    EndUltimateVfx();
    ultimateActivationVfxSheet_.reset();
    ultimateActivationActorSheet_.reset();
    ultimateThrowCompositeSheet_.reset();
    superBatProjectileSheet_.reset();
    for (std::unique_ptr<Gdiplus::Bitmap>& frame : superBatExplosionFrames_)
    {
        frame.reset();
    }

    FrameSet* sets[] =
    {
        &idleDown_, &idleLeft_, &idleRight_, &idleUp_,
        &walkDown_, &walkLeft_, &walkRight_, &walkUp_,
        &crouchDown_, &crouchLeft_, &crouchRight_, &crouchUp_,
        &blockDown_, &blockLeft_, &blockRight_, &blockUp_,
        &unarmedBlockDown_, &unarmedBlockLeft_, &unarmedBlockRight_, &unarmedBlockUp_,
        &attackDown_, &attackLeft_, &attackRight_, &attackUp_,
        &attackVfxDown_, &attackVfxLeft_, &attackVfxRight_, &attackVfxUp_,
        &unarmedDown_, &unarmedLeft_, &unarmedRight_, &unarmedUp_,
        &unarmedWalkDown_, &unarmedWalkLeft_, &unarmedWalkRight_, &unarmedWalkUp_,
        &punchDown_, &punchLeft_, &punchRight_, &punchUp_,
        &punchVfxDown_, &punchVfxLeft_, &punchVfxRight_, &punchVfxUp_,
        &throwDown_, &throwLeft_, &throwRight_, &throwUp_,
        &throwVfxDown_, &throwVfxLeft_, &throwVfxRight_, &throwVfxUp_,
        &deathLeft_, &deathRight_
    };

    for (FrameSet* set : sets)
    {
        for (Frame& frame : set->frames)
        {
            frame.image.reset();
        }
        set->frames.clear();
    }

    frameTimer_ = 0.0f;
    walkPhase_ = 0.0f;
    currentFrame_ = 0;
    activeAnimation_ = CharacterAnimation::Idle;
    transitionSourceAnimation_ = CharacterAnimation::Idle;
    transitionSourceFrame_ = 0;
    transitionTimer_ = 0.0f;
    transitionDuration_ = 0.0f;
    animationFinished_ = false;
    attackHitFramePending_ = false;
    attackHitTriggered_ = false;
    throwReleaseFramePending_ = false;
    throwReleaseTriggered_ = false;
    hasBat_ = true;
    ultimateVfxDirection_ = CharacterDirection::Right;
    sideOnlyV3VisualHeight_ = 0.0f;
}
