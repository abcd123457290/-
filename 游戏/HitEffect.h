#pragma once

#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>

#include "MathTypes.h"

// 一次近战命中的短生命周期视觉反馈。
// 负责计算目标边缘上的撞击点，并统一提供闪白、屏幕震动、冲击环、十字闪光和
// 飞散粒子的动画；位置使用世界/客户区像素坐标。
class HitEffect
{
public:
    static constexpr float DurationSeconds = 0.42f;

    // 清除进行中的效果并恢复安全的默认方向。
    void Reset()
    {
        timer_ = 0.0f;
        impactPoint_ = {};
        impactDirection_ = { 1.0f, 0.0f };
    }

    // 根据攻击来源与目标中心的连线，把撞击点放在目标碰撞圆的迎击面。
    void Trigger(Vec2 sourcePosition, Vec2 targetCenter, float targetRadius)
    {
        Vec2 direction{ sourcePosition.x - targetCenter.x, sourcePosition.y - targetCenter.y };
        const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        // 来源和目标重合时无法归一化，使用固定左向量作为稳定回退值。
        if (length > 0.001f)
        {
            direction.x /= length;
            direction.y /= length;
        }
        else
        {
            direction = { -1.0f, 0.0f };
        }

        impactPoint_ =
        {
            targetCenter.x + direction.x * targetRadius,
            targetCenter.y + direction.y * targetRadius
        };
        impactDirection_ = direction;
        timer_ = DurationSeconds;
    }

    // 窗口尺寸变化后跟随场景平移，保持特效贴在原目标上。
    void Translate(Vec2 offset)
    {
        impactPoint_.x += offset.x;
        impactPoint_.y += offset.y;
    }

    // 倒计时归零后效果自动失活。
    void Update(float deltaSeconds)
    {
        if (timer_ <= 0.0f)
        {
            return;
        }

        timer_ -= deltaSeconds;
        if (timer_ < 0.0f)
        {
            timer_ = 0.0f;
        }
    }

    bool IsActive() const
    {
        return timer_ > 0.0f;
    }

    // 开始阶段持续闪白，尾段间歇闪烁，形成更清晰的受击顿挫感。
    bool IsFlashVisible() const
    {
        return timer_ > 0.24f || (timer_ > 0.08f && std::fmod(timer_, 0.10f) > 0.055f);
    }

    // 返回随时间衰减的水平震动量；调用者决定把它应用到哪个绘制层。
    float GetShakeX() const
    {
        if (!IsActive())
        {
            return 0.0f;
        }
        return std::sin(timer_ * 92.0f) * 7.0f * (timer_ / DurationSeconds);
    }

    // 所有图形均由 progress（0 -> 1）驱动，不创建持久 GDI+ 资源。
    void Draw(Gdiplus::Graphics& graphics) const
    {
        if (!IsActive())
        {
            return;
        }

        // progress 控制扩散距离，fade 控制整体消散，burst 提供先增强后减弱的脉冲。
        const float progress = 1.0f - timer_ / DurationSeconds;
        const float fade = 1.0f - progress;
        const float burst = std::sin((std::min)(progress * 1.35f, 1.0f) * 3.1415926f);
        const BYTE alpha = static_cast<BYTE>(Clamp(fade * 255.0f, 0.0f, 255.0f));
        const BYTE glowAlpha = static_cast<BYTE>(Clamp(burst * fade * 210.0f, 0.0f, 255.0f));
        Gdiplus::SolidBrush white(Gdiplus::Color(alpha, 255, 255, 255));
        Gdiplus::SolidBrush gold(Gdiplus::Color(alpha, 255, 190, 42));
        Gdiplus::SolidBrush orange(Gdiplus::Color(alpha, 255, 104, 32));

        const float ringRadius = 8.0f + progress * 43.0f;
        Gdiplus::Pen ringGlow(Gdiplus::Color(glowAlpha, 255, 165, 35), 8.0f * fade + 1.0f);
        Gdiplus::Pen ringCore(Gdiplus::Color(alpha, 255, 255, 238), 2.5f);
        graphics.DrawEllipse(&ringGlow, impactPoint_.x - ringRadius, impactPoint_.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);
        graphics.DrawEllipse(&ringCore, impactPoint_.x - ringRadius, impactPoint_.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);

        const float crossLength = 16.0f + burst * 24.0f;
        graphics.FillRectangle(&white, impactPoint_.x - crossLength, impactPoint_.y - 3.0f, crossLength * 2.0f, 6.0f);
        graphics.FillRectangle(&white, impactPoint_.x - 3.0f, impactPoint_.y - crossLength, 6.0f, crossLength * 2.0f);
        graphics.FillRectangle(&gold, impactPoint_.x - 10.0f, impactPoint_.y - 10.0f, 20.0f, 20.0f);
        graphics.FillRectangle(&white, impactPoint_.x - 4.0f, impactPoint_.y - 4.0f, 8.0f, 8.0f);

        // 切线与撞击方向垂直，用于让三条速度线在冲击轴两侧展开。
        const Vec2 tangent{ -impactDirection_.y, impactDirection_.x };
        Gdiplus::Pen directionGlow(Gdiplus::Color(glowAlpha, 255, 126, 24), 9.0f);
        Gdiplus::Pen directionCore(Gdiplus::Color(alpha, 255, 255, 255), 3.0f);
        const float streakBack = 28.0f + progress * 18.0f;
        const float streakFront = 18.0f + progress * 30.0f;
        for (int side = -1; side <= 1; ++side)
        {
            const float offset = static_cast<float>(side) * 8.0f;
            const float sx = impactPoint_.x - impactDirection_.x * streakBack + tangent.x * offset;
            const float sy = impactPoint_.y - impactDirection_.y * streakBack + tangent.y * offset;
            const float ex = impactPoint_.x + impactDirection_.x * streakFront + tangent.x * offset * 0.35f;
            const float ey = impactPoint_.y + impactDirection_.y * streakFront + tangent.y * offset * 0.35f;
            graphics.DrawLine(&directionGlow, sx, sy, ex, ey);
            graphics.DrawLine(&directionCore, sx, sy, ex, ey);
        }

        // 固定方向表让粒子效果可复现，避免每帧随机导致画面跳动。
        const Vec2 directions[] =
        {
            { -1.0f, -0.45f }, { -0.82f, 0.72f },
            { -0.35f, -1.0f }, { 0.2f, 1.0f },
            { 0.72f, -0.78f }, { 1.0f, 0.38f },
            { -0.12f, 0.82f }, { 0.46f, -0.92f }
        };
        const float travel = 12.0f + progress * 52.0f;
        for (int i = 0; i < 8; ++i)
        {
            const float x = impactPoint_.x + directions[i].x * travel;
            const float y = impactPoint_.y + directions[i].y * travel;
            Gdiplus::Brush* brush = i % 3 == 0
                ? static_cast<Gdiplus::Brush*>(&white)
                : (i % 3 == 1 ? static_cast<Gdiplus::Brush*>(&gold) : static_cast<Gdiplus::Brush*>(&orange));
            const float size = i % 2 == 0 ? 7.0f : 5.0f;
            graphics.FillRectangle(brush, x - size * 0.5f, y - size * 0.5f, size, size);
            graphics.FillRectangle(brush, x + directions[i].x * 10.0f - 2.0f, y + directions[i].y * 10.0f - 2.0f, 4.0f, 4.0f);
        }
    }

private:
    // timer_ 同时代表是否激活以及效果剩余时间。
    float timer_ = 0.0f;
    Vec2 impactPoint_{};
    Vec2 impactDirection_{ 1.0f, 0.0f };
};
