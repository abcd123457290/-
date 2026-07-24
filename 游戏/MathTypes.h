#pragma once

#include <cmath>

// 游戏采用屏幕空间的二维向量：x 向右为正，y 向下为正。
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

inline float Clamp(float value, float minimum, float maximum)
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

// 以不超过 maxDelta 的步长逼近 target，并保证不会越过目标值。
// 血条追帧等与帧率无关的平滑变化会使用这个函数。
inline float MoveToward(float current, float target, float maxDelta)
{
    if (std::abs(target - current) <= maxDelta)
    {
        return target;
    }
    return current + (target > current ? maxDelta : -maxDelta);
}

// 返回同方向单位向量。零向量保持为零，避免除零和 NaN 扩散。
inline Vec2 Normalize(Vec2 value)
{
    const float length = std::sqrt(value.x * value.x + value.y * value.y);
    if (length > 0.0f)
    {
        value.x /= length;
        value.y /= length;
    }
    return value;
}
