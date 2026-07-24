#pragma once

#include <windows.h>

#include "MathTypes.h"

// 直接读取 Win32 异步键盘状态。最高位为 1 表示此刻正按住按键。
inline bool IsKeyDown(int virtualKey)
{
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

inline Vec2 GetMoveInput()
{
    Vec2 input;

    // 屏幕坐标的 y 轴向下，因此 W 对应负 y、S 对应正 y。
    if (IsKeyDown('A'))
    {
        input.x -= 1.0f;
    }
    if (IsKeyDown('D'))
    {
        input.x += 1.0f;
    }
    if (IsKeyDown('W'))
    {
        input.y -= 1.0f;
    }
    if (IsKeyDown('S'))
    {
        input.y += 1.0f;
    }

    // 归一化避免同时按两个方向时斜向速度变为直线速度的 sqrt(2) 倍。
    return Normalize(input);
}

// 战斗键位集中在此处，便于以后统一改键或接入手柄。
inline bool IsAttackKeyDown()
{
    return IsKeyDown('J');
}

inline bool IsCrouchKeyDown()
{
    return IsKeyDown(VK_LCONTROL) || IsKeyDown(VK_RCONTROL);
}

inline bool IsBlockKeyDown()
{
    return IsKeyDown('K');
}
