#pragma once

// 敌方攻击附带的控制效果。伤害和状态分开传递，普通怪物使用 None。
enum class HostileStatusEffect
{
    None,
    DirectionConfusion,
    LightningMark
};

// Boss 生命跨过阶段线时向 Scene/Game 提交的一次性语音事件。音频播放不放在
// Boss 内部，避免战斗实体直接依赖设备、音量设置或具体文件格式。
enum class BossVoiceCue
{
    None,
    TwoThirdsHealth,
    OneThirdHealth
};
