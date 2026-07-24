#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// 一次 Boss 技能决策所需的原始战斗观察。
// 这里保存连续数值，具体分桶规则集中在 BossAiModel 中，避免 Boss.cpp 和
// 训练脚本分别维护两套容易发生偏差的状态编码。
struct BossAiObservation
{
    int bossHealth = 0;              // Boss 当前本体生命值。
    int bossMaximumHealth = 1;       // Boss 本体最大生命值；至少为 1。
    int playerHealth = 0;            // 玩家当前生命值。
    int playerMaximumHealth = 1;     // 玩家最大生命值；至少为 1。
    float absoluteXDistance = 0.0f;  // Boss 与玩家的水平距离绝对值，单位为像素。
    float absoluteYDistance = 0.0f;  // Boss 与玩家的垂直距离绝对值，单位为像素。
    int minionCount = 0;             // 已存在和本帧待创建小怪的合计数量。
    bool directionConfused = false;  // 玩家是否处于五秒方向混乱状态。
    bool lightningMarked = false;    // 玩家是否带有等待下一次命中引爆的闪电标记。
    bool movementLocked = false;     // 玩家是否正处于两秒移动锁定状态。
    int rageStacks = 0;              // Boss 当前暴怒层数。
};

// 桌面 Q-learning 模型的轻量运行时读取器。
// 模型只返回高层技能编号；移动、预警、动画和伤害仍由 Boss 原状态机处理。
class BossAiModel
{
public:
    static constexpr std::size_t StateCount = 7776; // 3×3×3×3×4×8×3 个离散状态。
    static constexpr int ActionCount = 3;           // 0 刀击、1 响指召唤、2 冲锋枪。
    static constexpr int InvalidAction = -1;        // 未加载或状态非法时的失败返回值。

    // 从系统“桌面”已知文件夹读取 kafka_boss_ai_model.json，并完整校验模型 ABI。
    bool LoadFromDesktop();
    // 模型格式、策略长度及全部动作均通过校验后才返回 true。
    bool IsLoaded() const;
    // 将原始观察编码为状态并查询 policy_flat；失败时返回 InvalidAction。
    int SelectAction(const BossAiObservation& observation) const;
    // 返回解析后的桌面模型绝对路径，便于诊断用户移动或删除模型的情况。
    const std::wstring& GetModelPath() const;
    // 返回最近一次加载结果；HUD 和调试器可据此确认是否启用了学习策略。
    const std::wstring& GetLoadMessage() const;

    // 公开放置状态编码函数，便于独立验证 C++ 与训练脚本的索引完全一致。
    static std::size_t EncodeState(const BossAiObservation& observation);

private:
    std::vector<std::uint8_t> policy_; // 7776 个贪心动作编号，stateIndex 直接作为下标。
    std::wstring modelPath_;           // 系统 API 解析出的桌面 JSON 绝对路径。
    std::wstring loadMessage_;         // 成功信息或具体失败原因，不参与技能决策。
    bool loaded_ = false;              // 防止半解析数据被 Boss 当成有效策略使用。
};
