#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <memory>

// 可见菜单页面。ModeSelect 包含训练、普通闯关和困难挑战，Settings 管理音量和全屏。
enum class MenuPage
{
    Main,
    ModeSelect,
    Rules,
    Settings,
    Pause,
    GameOver,
    Victory
};

// 菜单只产生意图，不直接操作 Game；Game 每帧通过 ConsumeAction 取走一次。
enum class MenuAction
{
    None,
    StartTraining,
    StartChallenge,
    StartHardChallenge,
    RestartCurrent,
    ResumeGame,
    ReturnToMain,
    ToggleFullscreen
};

// 主菜单/暂停菜单的输入、过渡动画、命中测试与绘制系统。
// 鼠标和键盘共用 selectedIndex_，保证两套输入方式显示一致的焦点反馈。
class MenuSystem
{
public:
    // 加载/释放模式选择页使用的正式 Boss 头像；生命周期必须位于 GDI+ 之间。
    void LoadContent();
    void UnloadContent();
    // 推进页面切换、首次出现动画和“未开放”提示等短计时器。
    void Update(float deltaSeconds);
    void Draw(Gdiplus::Graphics& graphics, RECT clientRect) const;
    // 立即显示页面；animate 为 true 时播放入场过渡。
    void Show(MenuPage page, bool animate = false);

    // Win32 消息由 Game 转换成这些与窗口句柄无关的输入调用。
    void HandleMouseMove(int x, int y, RECT clientRect);
    void HandleMouseDown(int x, int y, RECT clientRect);
    void HandleMouseUp(int x, int y, RECT clientRect);
    void HandleKeyDown(WPARAM key);
    // 返回并清空待执行动作，确保一次点击只被消费一次。
    MenuAction ConsumeAction();
    MenuPage GetPage() const;
    float GetMasterVolume() const;
    float GetEffectsVolume() const;
    void SetFullscreen(bool fullscreen);

private:
    // 页面导航和当前选项操作。
    void RequestPage(MenuPage page);
    void ActivateSelection();
    void MoveSelection(int direction);
    int HitTest(int x, int y, RECT clientRect) const;
    void AdjustSliderFromMouse(int x, RECT clientRect);

    // DrawPage 根据页面分派；其余函数只负责各自页面/背景的视觉元素。
    void DrawPage(Gdiplus::Graphics& graphics, RECT clientRect, MenuPage page, float offsetX) const;
    void DrawBackground(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawMain(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawModes(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawRules(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawSettings(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawPause(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawGameOver(Gdiplus::Graphics& graphics, RECT clientRect) const;
    void DrawVictory(Gdiplus::Graphics& graphics, RECT clientRect) const;

    // page_ 是当前页，nextPage_ 是横向切换动画完成后正式生效的目标页。
    MenuPage page_ = MenuPage::Main;
    MenuPage nextPage_ = MenuPage::Main;
    MenuAction pendingAction_ = MenuAction::None;
    // -1 表示鼠标没有悬停/按住任何可交互区域。
    int selectedIndex_ = 0;
    int hoveredIndex_ = -1;
    int pressedIndex_ = -1;
    bool draggingSlider_ = false;
    bool transitioning_ = false;
    bool fullscreen_ = false;
    // 所有计时器单位均为秒。
    float transitionTimer_ = 0.0f;
    float introTimer_ = 0.0f;
    float lockedFeedbackTimer_ = 0.0f;
    float masterVolume_ = 0.8f;
    float effectsVolume_ = 0.9f;
    // 模式卡片与局内 Boss HUD 使用同一版透明头像，菜单不再绘制粗糙人物模型。
    std::unique_ptr<Gdiplus::Image> bossPortrait_;
    // 缓存最近绘制区域，供无显式 RECT 参数的键盘操作保持布局一致。
    RECT lastClientRect_{};
};
