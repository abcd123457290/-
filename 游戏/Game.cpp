#include "Game.h"

#include <algorithm>
#include <gdiplus.h>
#include <windowsx.h>

namespace
{
    // 指客户区的设计分辨率；CreateGameWindow 会额外补上非客户区边框尺寸。
    constexpr int kWindowWidth = 960;
    constexpr int kWindowHeight = 640;
}

int Game::Run()
{
    // GDI+ token 的生命周期覆盖所有可能持有 Image/Bitmap 的子系统。
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    if (!CreateGameWindow())
    {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 1;
    }

    LoadContent();
    audio_.Start();
    lastFrameTime_ = std::chrono::steady_clock::now();

    // 非阻塞消息泵：处理完当前消息后仍会执行 Tick，因此没有输入时游戏也会更新。
    MSG message{};
    while (message.message != WM_QUIT)
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        Tick();
        Sleep(1);
    }

    audio_.Stop();
    UnloadContent();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK Game::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Game* game = nullptr;

    // CreateWindowEx 的最后一个参数携带 this。在最早的 WM_NCCREATE 中把它
    // 存入窗口用户数据，之后所有消息都能找到对应 Game 实例。
    if (message == WM_NCCREATE)
    {
        CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        game = static_cast<Game*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(game));
    }
    else
    {
        game = reinterpret_cast<Game*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (game != nullptr)
    {
        return game->HandleMessage(hwnd, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool Game::CreateGameWindow()
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t className[] = L"QiongRoguelikeWindow";

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = Game::WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    RegisterClassW(&windowClass);

    // 调整外框后，窗口客户区才能精确保持设计分辨率。
    RECT windowRect{ 0, 0, kWindowWidth, kWindowHeight };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowExW(
        0,
        className,
        L"Qiong Roguelike - Combat Prototype",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        this);

    return hwnd_ != nullptr;
}

void Game::LoadContent()
{
    menu_.LoadContent();
    player_.LoadContent();

    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    scene_.Initialize(clientRect);
    previousClientRect_ = clientRect;
    hasPreviousClientRect_ = true;
}

void Game::UnloadContent()
{
    player_.UnloadContent();
    scene_.UnloadContent();
    menu_.UnloadContent();
}

void Game::Tick()
{
    // steady_clock 不受系统时间校准影响，适合计算帧间隔。
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> delta = now - lastFrameTime_;
    lastFrameTime_ = now;

    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    if (clientWidth <= 0 || clientHeight <= 0)
    {
        return;
    }

    // 只在客户区尺寸真正变化时重新映射实体，普通窗口移动不会影响世界坐标。
    if (hasPreviousClientRect_)
    {
        const bool clientSizeChanged =
            (previousClientRect_.right - previousClientRect_.left) != (clientRect.right - clientRect.left) ||
            (previousClientRect_.bottom - previousClientRect_.top) != (clientRect.bottom - clientRect.top);
        if (clientSizeChanged)
        {
            player_.ResizeToClient(previousClientRect_, clientRect);
            scene_.ResizeToClient(previousClientRect_, clientRect);
        }
    }
    else
    {
        previousClientRect_ = clientRect;
        hasPreviousClientRect_ = true;
    }

    // 单帧最多推进 50ms。窗口拖拽、断点或系统短暂停顿后不补算数秒物理，
    // 避免 Boss/小怪瞬移、一次跨过多个攻击状态或玩家突然连续受击。
    const float deltaSeconds = (std::min)(delta.count(), 0.05f);
    const bool menuActive =
        phase_ == Phase::Menu || phase_ == Phase::Paused || phase_ == Phase::GameOver || phase_ == Phase::Victory;
    if (menuActive)
    {
        menu_.Update(deltaSeconds);
        ProcessMenuAction();
    }

    // 死亡阶段冻结场景、怪物、掉落物和战斗判定，只让玩家死亡动画继续播放。
    if (phase_ == Phase::Dying)
    {
        player_.Update(deltaSeconds, clientRect);
        if (player_.IsDeathAnimationFinished())
        {
            phase_ = Phase::GameOver;
            menu_.Show(MenuPage::GameOver);
        }
    }

    if (phase_ == Phase::Playing)
    {
        // 更新顺序很重要：玩家先产生动作事件，随后解决物理约束并更新场景，
        // 最后统一消费双方产生的一次性战斗事件。
        player_.Update(deltaSeconds, clientRect);
        if (player_.ConsumeUltimateActivationStarted())
        {
            audio_.PlayUltimateActivationVoice();
        }
        player_.ResolveCircleCollision(scene_.GetDummyCenter(clientRect), scene_.GetDummyCollisionRadius());
        scene_.ResolveMonsterCollisions(player_);
        scene_.Update(deltaSeconds, player_, clientRect);
        player_.Heal(scene_.ConsumePendingHealing());

        Vec2 monsterHitSource;
        int monsterDamage = 0;
        HostileStatusEffect monsterStatusEffect = HostileStatusEffect::None;
        // 同一帧可能有多个怪物到达命中帧，因此使用 while 完整排空事件。
        while (scene_.ConsumeMonsterAttackHit(
            player_.GetPosition(),
            player_.GetCollisionRadius(),
            monsterHitSource,
            monsterDamage,
            monsterStatusEffect))
        {
            const bool blocked = player_.TakeDamage(monsterDamage, monsterHitSource, monsterStatusEffect);
            // 格挡播放独立的高频金属音，真实受伤保留普通命中音；两种结果不再
            // 共用低频撞击声，玩家可以不看血条也立即判断反击是否成立。
            if (blocked)
            {
                audio_.PlayBlockSound();
            }
            else
            {
                audio_.PlayHitSound(false);
            }
            if (blocked)
            {
                scene_.ApplyCounterAttack(monsterHitSource, player_.GetCounterDamage());
            }
            if (player_.GetHealth() <= 0)
            {
                // 生命归零后立即停止消费本帧剩余攻击，避免死亡后继续结算伤害。
                phase_ = Phase::Dying;
                break;
            }
        }

        // 玩家死亡后不再结算同一帧尚未处理的普攻、投掷或终结技事件。
        if (phase_ == Phase::Playing)
        {
            // 普攻每次动作最多一个命中帧，单次 if 即可。
            BasicAttackHitEvent basicAttack;
            if (player_.ConsumeAttackHit(basicAttack))
            {
                const bool hitTarget = scene_.TryHitBasicAttack(
                    basicAttack.origin,
                    basicAttack.direction,
                    basicAttack.reach,
                    basicAttack.damage,
                    clientRect);
                if (hitTarget)
                {
                    audio_.PlayHitSound(false);
                }
            }

            // 普通投掷直接用飞行物当前位置做点碰撞；强化投掷改由爆炸事件结算。
            Vec2 thrownBatPoint;
            if (player_.GetThrownBatHitPoint(thrownBatPoint) &&
                !player_.IsSuperBatProjectile() &&
                scene_.TryHitThrownBat(thrownBatPoint, clientRect, player_.GetThrowDamage()))
            {
                player_.ResolveThrownBatHit();
                audio_.PlayHitSound(true);
            }

            SuperBatExplosionEvent superBatExplosion;
            while (player_.ConsumeSuperBatExplosion(superBatExplosion))
            {
                const bool hitTarget = scene_.TryHitSuperBatExplosion(
                    superBatExplosion.center,
                    superBatExplosion.direction,
                    superBatExplosion.radius,
                    superBatExplosion.damage,
                    clientRect);
                audio_.PlaySuperBatExplosionSound(hitTarget);
            }

            UltimateHitEvent ultimateHit;
            while (player_.ConsumeUltimateHit(ultimateHit))
            {
                const bool hitTarget = scene_.TryHitUltimate(
                    ultimateHit.origin,
                    ultimateHit.direction,
                    ultimateHit.radius,
                    ultimateHit.damage,
                    ultimateHit.finisher,
                    clientRect);
                audio_.PlayEmpoweredAttackSound(
                    ultimateHit.comboIndex,
                    hitTarget,
                    ultimateHit.finisher);
            }

            player_.RegisterMonsterKills(scene_.ConsumeMonsterKillCount());
        }

        if (phase_ == Phase::Playing && currentMode_ != GameplayMode::Training && scene_.ConsumeBossDefeated())
        {
            phase_ = Phase::Victory;
            menu_.Show(MenuPage::Victory);
        }

        if (phase_ == Phase::Playing && currentMode_ == GameplayMode::HardChallenge)
        {
            switch (scene_.ConsumeBossVoiceCue())
            {
            case BossVoiceCue::TwoThirdsHealth:
                audio_.PlayKafkaTwoThirdsVoice();
                break;
            case BossVoiceCue::OneThirdHealth:
                audio_.PlayKafkaOneThirdVoice();
                break;
            case BossVoiceCue::None:
            default:
                break;
            }
        }
    }

    // 菜单滑块即使在游戏中不可见，也始终是音量设置的唯一数据源。
    audio_.SetMasterVolume(menu_.GetMasterVolume());
    audio_.SetEffectsVolume(menu_.GetEffectsVolume());
    audio_.Update(deltaSeconds);

    previousClientRect_ = clientRect;
    // 标记整个客户区待重绘；FALSE 防止系统先擦背景而造成闪烁。
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Game::Draw(HDC hdc)
{
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    if (clientWidth <= 0 || clientHeight <= 0)
    {
        return;
    }

    // 每帧先画入内存位图，再一次性 BitBlt 到窗口，消除逐对象绘制的闪烁。
    HDC backBuffer = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, clientWidth, clientHeight);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(backBuffer, bitmap));

    Gdiplus::Graphics graphics(backBuffer);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    // 暂停态保留最后的场景画面，并在其上叠加暂停菜单。
    if (phase_ == Phase::Menu)
    {
        menu_.Draw(graphics, clientRect);
    }
    else
    {
        scene_.Draw(graphics, clientRect);
        player_.Draw(graphics);
        player_.DrawHud(graphics, clientRect);
        if (phase_ == Phase::Paused || phase_ == Phase::GameOver || phase_ == Phase::Victory)
        {
            menu_.Draw(graphics, clientRect);
        }
    }

    BitBlt(hdc, 0, 0, clientWidth, clientHeight, backBuffer, 0, 0, SRCCOPY);

    SelectObject(backBuffer, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(backBuffer);
}

void Game::ProcessMenuAction()
{
    // ConsumeAction 清空动作，因此这里每帧调用不会重复执行上一次点击。
    switch (menu_.ConsumeAction())
    {
    case MenuAction::StartTraining:
        StartMode(GameplayMode::Training);
        break;
    case MenuAction::StartChallenge:
        StartMode(GameplayMode::Challenge);
        break;
    case MenuAction::StartHardChallenge:
        StartMode(GameplayMode::HardChallenge);
        break;
    case MenuAction::RestartCurrent:
        StartMode(currentMode_);
        break;
    case MenuAction::ResumeGame:
        phase_ = Phase::Playing;
        break;
    case MenuAction::ReturnToMain:
        phase_ = Phase::Menu;
        menu_.Show(MenuPage::Main);
        audio_.SetHardModeMusic(false, true);
        break;
    case MenuAction::ToggleFullscreen:
        ToggleFullscreen();
        break;
    case MenuAction::None:
    default:
        break;
    }
}

void Game::StartMode(GameplayMode mode)
{
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    currentMode_ = mode;
    const bool hardMode = currentMode_ == GameplayMode::HardChallenge;
    // 每个新回合都从对应曲目的开头播放；困难模式额外播放卡芙卡开战台词。
    audio_.SetHardModeMusic(hardMode, true);
    player_.ResetTrainingMode(clientRect);
    if (currentMode_ == GameplayMode::Challenge || currentMode_ == GameplayMode::HardChallenge)
    {
        scene_.ResetChallengeMode(clientRect, currentMode_ == GameplayMode::HardChallenge);
    }
    else
    {
        scene_.ResetTrainingMode(clientRect);
    }

    previousClientRect_ = clientRect;
    hasPreviousClientRect_ = true;
    lastFrameTime_ = std::chrono::steady_clock::now();
    // 清除边沿状态，避免从菜单切回游戏时把之前按住的键误认为新按下。
    throwKeyDown_ = false;
    ultimateKeyDown_ = false;
    blockKeyDown_ = false;
    spawnKeyDown_ = false;
    phase_ = Phase::Playing;
    if (hardMode)
    {
        audio_.PlayKafkaBattleStartVoice();
    }
}

void Game::ToggleFullscreen()
{
    if (!fullscreen_)
    {
        // 使用无边框窗口覆盖当前显示器，而不切换显示模式；退出时恢复原位置。
        windowedStyle_ = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
        windowedPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd_, &windowedPlacement_);

        MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(windowedStyle_ & ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(
            hwnd_,
            HWND_TOP,
            monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        fullscreen_ = true;
    }
    else
    {
        // 先恢复样式/placement，再发送 FRAMECHANGED 让系统重算非客户区。
        SetWindowLongPtrW(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(windowedStyle_));
        SetWindowPlacement(hwnd_, &windowedPlacement_);
        SetWindowPos(
            hwnd_,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        fullscreen_ = false;
    }
    menu_.SetFullscreen(fullscreen_);
}

LRESULT Game::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_KEYDOWN:
        // K/Q/P/空格可能收到 Windows 自动重复消息，成员布尔值将其压缩成一次请求。
        if (wParam == VK_SPACE && phase_ == Phase::Playing && currentMode_ == GameplayMode::Training)
        {
            if (!spawnKeyDown_)
            {
                spawnKeyDown_ = true;
                RECT clientRect{};
                GetClientRect(hwnd, &clientRect);
                scene_.SpawnMonster(clientRect);
            }
            return 0;
        }

        if ((wParam == 'K' || wParam == 'k') && phase_ == Phase::Playing)
        {
            if (!blockKeyDown_)
            {
                blockKeyDown_ = true;
                player_.RequestBlock();
            }
            return 0;
        }

        if ((wParam == 'Q' || wParam == 'q') && phase_ == Phase::Playing)
        {
            if (!throwKeyDown_)
            {
                throwKeyDown_ = true;
                player_.RequestThrow();
            }
            return 0;
        }

        if ((wParam == 'P' || wParam == 'p') && phase_ == Phase::Playing)
        {
            if (!ultimateKeyDown_)
            {
                ultimateKeyDown_ = true;
                player_.RequestUltimate();
            }
            return 0;
        }

        if (wParam == VK_ESCAPE && phase_ == Phase::Playing)
        {
            phase_ = Phase::Paused;
            menu_.Show(MenuPage::Pause);
            return 0;
        }
        if (wParam == VK_ESCAPE && phase_ == Phase::Paused)
        {
            phase_ = Phase::Playing;
            return 0;
        }

        if (phase_ != Phase::Playing && phase_ != Phase::Dying)
        {
            menu_.HandleKeyDown(wParam);
        }
        return 0;
    case WM_KEYUP:
        if (wParam == VK_SPACE)
        {
            spawnKeyDown_ = false;
            return 0;
        }
        if (wParam == 'K' || wParam == 'k')
        {
            blockKeyDown_ = false;
            return 0;
        }
        if (wParam == 'Q' || wParam == 'q')
        {
            throwKeyDown_ = false;
            return 0;
        }
        if (wParam == 'P' || wParam == 'p')
        {
            ultimateKeyDown_ = false;
            return 0;
        }
        return 0;
    case WM_MOUSEMOVE:
        if (phase_ != Phase::Playing && phase_ != Phase::Dying)
        {
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            menu_.HandleMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), clientRect);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (phase_ != Phase::Playing && phase_ != Phase::Dying)
        {
            // 捕获鼠标后，即使用户拖动到窗口外再松开，菜单仍能收到 WM_LBUTTONUP。
            SetCapture(hwnd);
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            menu_.HandleMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), clientRect);
        }
        return 0;
    case WM_LBUTTONUP:
        if (phase_ != Phase::Playing && phase_ != Phase::Dying)
        {
            ReleaseCapture();
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            menu_.HandleMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), clientRect);
        }
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC hdc = BeginPaint(hwnd, &paint);
        Draw(hdc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
