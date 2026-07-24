#pragma once

#include <windows.h>
#include <gdiplus.h>

#include "MathTypes.h"

// 带“受伤残影”的通用生命条。
// target_ 立即反映真实生命值，delayed_ 会短暂停留后追赶 target_，从而让玩家
// 清楚看到本次攻击削掉了多少血。该类只保存显示状态，不拥有角色生命值。
class HealthBar
{
public:
    // 强制同步真实值与残影值。重开关卡、怪物重生时使用，不播放扣血动画。
    void ResetValue(int current, int maximum)
    {
        maximum_ = maximum > 0 ? maximum : 1;
        target_ = Clamp(static_cast<float>(current), 0.0f, static_cast<float>(maximum_));
        delayed_ = target_;
        delayTimer_ = 0.0f;
        initialized_ = true;
    }

    // 更新真实目标值并重新开始残影停留计时；maximum 至少按 1 处理以防除零。
    void SetValue(int current, int maximum)
    {
        maximum_ = maximum > 0 ? maximum : 1;
        target_ = Clamp(static_cast<float>(current), 0.0f, static_cast<float>(maximum_));
        if (!initialized_)
        {
            delayed_ = target_;
            initialized_ = true;
        }
        delayTimer_ = 0.20f;
    }

    // 等待 0.2 秒后，以“最大生命值/秒”为比例平滑收拢残影。
    void Update(float deltaSeconds)
    {
        if (delayTimer_ > 0.0f)
        {
            delayTimer_ -= deltaSeconds;
            return;
        }

        const float speed = static_cast<float>(maximum_) * 1.45f;
        delayed_ = MoveToward(delayed_, target_, speed * deltaSeconds);
    }

    // 绘制顺序为：外框 -> 底色 -> 损失残影 -> 当前生命 -> 高光 -> 边线。
    void Draw(Gdiplus::Graphics& graphics, float x, float y, float width, float height) const
    {
        const float targetRatio = Clamp(target_ / static_cast<float>(maximum_), 0.0f, 1.0f);
        const float delayedRatio = Clamp(delayed_ / static_cast<float>(maximum_), 0.0f, 1.0f);

        Gdiplus::SolidBrush outer(Gdiplus::Color(220, 41, 32, 28));
        Gdiplus::SolidBrush inner(Gdiplus::Color(255, 72, 57, 50));
        Gdiplus::SolidBrush lostSoft(Gdiplus::Color(78, 255, 214, 112));
        Gdiplus::SolidBrush lostCore(Gdiplus::Color(150, 255, 178, 56));
        Gdiplus::SolidBrush health(Gdiplus::Color(255, 210, 48, 48));
        Gdiplus::SolidBrush healthLight(Gdiplus::Color(255, 255, 92, 72));
        Gdiplus::Pen edge(Gdiplus::Color(245, 34, 27, 24), 2.0f);

        graphics.FillRectangle(&outer, x - 3.0f, y - 3.0f, width + 6.0f, height + 6.0f);
        graphics.FillRectangle(&inner, x, y, width, height);

        // delayed_ 大于 target_ 的区间就是最近一次受伤所损失的生命。
        if (delayedRatio > targetRatio)
        {
            const float lostX = x + width * targetRatio;
            const float lostWidth = width * (delayedRatio - targetRatio);
            graphics.FillRectangle(&lostSoft, lostX - 4.0f, y - 2.0f, lostWidth + 8.0f, height + 4.0f);
            graphics.FillRectangle(&lostCore, lostX, y, lostWidth, height);
        }

        graphics.FillRectangle(&health, x, y, width * targetRatio, height);
        graphics.FillRectangle(&healthLight, x, y + 2.0f, width * targetRatio, height * 0.32f);
        graphics.DrawRectangle(&edge, x - 3.0f, y - 3.0f, width + 6.0f, height + 6.0f);
    }

private:
    int maximum_ = 1;
    // target_ 是即时值，delayed_ 是供动画使用的滞后值。
    float target_ = 1.0f;
    float delayed_ = 1.0f;
    float delayTimer_ = 0.0f;
    bool initialized_ = false;
};
