#pragma once

#include <array>

// 集中保存战斗数值，避免玩家、场景和动画模块各自维护一份“魔法数字”。
// 距离统一使用屏幕像素，时间统一使用秒，角度统一使用度。
namespace CombatTuning
{
    // 普通近战：球棒比拳击拥有更远判定；扇形半角决定角色前方可命中的范围。
    constexpr float BatAttackReach = 90.0f;
    constexpr float PunchAttackReach = 50.0f;
    constexpr float BasicAttackArcHalfAngleDegrees = 62.0f;
    constexpr float MeleeContactTolerance = 0.0f;

    // 投掷球棒：普通球棒按碰撞半径判定，释放点从角色中心向瞄准方向偏移。
    constexpr float ThrownBatHitRadius = 18.0f;
    constexpr float ThrowReleaseReach = 34.0f;
    constexpr float ThrowEnergyCost = 50.0f;
    constexpr int ThrowDamage = 50;

    // 强化球棒：飞到最大距离或撞到目标时引爆，并以圆形范围造成伤害。
    constexpr float SuperBatFlightSpeed = 760.0f;
    constexpr float SuperBatMaximumTravelDistance = 440.0f;
    constexpr float SuperBatExplosionRadius = 168.0f;
    constexpr int SuperBatExplosionDamage = 50;
    constexpr float SuperBatExplosionDurationSeconds = 0.67f;

    // 普攻时间轴：命中帧、移动锁定结束点和可缓存下一次输入的起点。
    constexpr float BasicAttackDurationSeconds = 0.32f;
    constexpr float BasicAttackHitTimeSeconds = 0.13f;
    constexpr float BasicAttackMovementLockSeconds = 0.14f;
    constexpr float BasicAttackBufferStartSeconds = 0.18f;

    // 终结技：消耗/冷却/启动时间，以及五段连击逐段扩大的半径和伤害。
    constexpr float UltimateEnergyCost = 100.0f;
    constexpr float UltimateCooldownSeconds = 10.0f;
    constexpr float UltimateActivationSeconds = 0.82f;
    constexpr float UltimateArcHalfAngleDegrees = 66.0f;
    constexpr int UltimateHitCount = 5;
    constexpr int UltimateUnlockKillCount = 5;
    constexpr std::array<float, UltimateHitCount> UltimateRadii{ 144.0f, 160.0f, 176.0f, 196.0f, 216.0f };
    constexpr std::array<int, UltimateHitCount> UltimateDamages{ 16, 19, 22, 26, 36 };
}
