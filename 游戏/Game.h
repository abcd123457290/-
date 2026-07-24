#pragma once

#include <chrono>
#include <windows.h>

#include "AudioSystem.h"
#include "MenuSystem.h"
#include "Player.h"
#include "Scene.h"

// 应用级控制器：拥有 Win32 窗口和所有主要游戏系统，并负责主循环、
// 消息分发、游戏阶段切换，以及把玩家产生的战斗事件交给 Scene 结算。
class Game
{
public:
    // 创建窗口并运行消息/渲染循环，直到收到 WM_QUIT；返回进程退出码。
    int Run();

private:
    // 玩法模式与顶层暂停/死亡状态相互独立；这样失败后的“重来”能返回原模式。
    enum class GameplayMode
    {
        Training,
        Challenge,
        HardChallenge
    };

    // 互斥的顶层状态。Dying 只推进死亡动画，GameOver 显示失败菜单。
    enum class Phase
    {
        Menu,
        Playing,
        Dying,
        Paused,
        GameOver,
        Victory
    };

    // 静态窗口过程只负责取回 Game 实例，再把消息转发给 HandleMessage。
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // 生命周期与逐帧流程。
    bool CreateGameWindow();
    void LoadContent();
    void UnloadContent();
    void Tick();
    void Draw(HDC hdc);

    // 处理菜单提交的一次性动作，并同步游戏阶段/全屏状态。
    void ProcessMenuAction();
    void StartMode(GameplayMode mode);
    void ToggleFullscreen();
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // 窗口与各子系统均由 Game 独占，析构顺序与这里的声明顺序相反。
    HWND hwnd_ = nullptr;
    Scene scene_;
    Player player_;
    MenuSystem menu_;
    AudioSystem audio_;
    Phase phase_ = Phase::Menu;
    GameplayMode currentMode_ = GameplayMode::Training;

    // 按键沿检测：只在“未按下 -> 按下”的那一帧发出请求，避免长按重复触发。
    bool fullscreen_ = false;
    bool throwKeyDown_ = false;
    bool ultimateKeyDown_ = false;
    bool blockKeyDown_ = false;
    bool spawnKeyDown_ = false;

    // 切换无边框全屏前保存窗口样式和位置，以便准确恢复。
    DWORD windowedStyle_ = WS_OVERLAPPEDWINDOW;
    WINDOWPLACEMENT windowedPlacement_{ sizeof(WINDOWPLACEMENT) };
    std::chrono::steady_clock::time_point lastFrameTime_;
    // 记录上一帧客户区，用于窗口缩放时等比例搬移场景对象。
    RECT previousClientRect_{};
    bool hasPreviousClientRect_ = false;
};
