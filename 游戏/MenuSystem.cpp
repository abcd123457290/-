#include "MenuSystem.h"

#include "AssetPaths.h"

#include <algorithm>
#include <cmath>

namespace
{
    // 布局辅助函数统一使用客户区尺寸计算，使菜单在窗口化和全屏下都能自适应。
    constexpr float kTransitionSeconds = 0.24f;

    const Gdiplus::Color kBackground(255, 244, 241, 232);
    const Gdiplus::Color kGrid(255, 221, 216, 202);
    const Gdiplus::Color kPanel(248, 250, 244, 224);
    const Gdiplus::Color kCharcoal(255, 43, 42, 47);
    const Gdiplus::Color kSoftText(255, 96, 88, 78);
    const Gdiplus::Color kGold(255, 220, 169, 65);
    const Gdiplus::Color kGoldLight(255, 255, 220, 112);
    const Gdiplus::Color kWood(255, 118, 73, 43);
    const Gdiplus::Color kRed(255, 174, 48, 42);
    const Gdiplus::Color kLocked(255, 132, 130, 123);

    float Width(RECT rect)
    {
        return static_cast<float>(rect.right - rect.left);
    }

    float Height(RECT rect)
    {
        return static_cast<float>(rect.bottom - rect.top);
    }

    float Clamp01(float value)
    {
        return (std::max)(0.0f, (std::min)(1.0f, value));
    }

    // 三次缓出：页面移动开始快、结束慢，避免线性插值的机械感。
    float EaseOut(float value)
    {
        const float inverse = 1.0f - Clamp01(value);
        return 1.0f - inverse * inverse * inverse;
    }

    bool Contains(const Gdiplus::RectF& rect, int x, int y)
    {
        return x >= rect.X && x <= rect.GetRight() && y >= rect.Y && y <= rect.GetBottom();
    }

    // 所有 *Rect 函数同时被绘制和命中测试使用，防止视觉区域与点击区域偏离。
    Gdiplus::RectF MainButtonRect(RECT clientRect, int index)
    {
        const float width = (std::min)(280.0f, Width(clientRect) - 48.0f);
        const float x = (Width(clientRect) - width) * 0.5f;
        const float startY = (std::max)(250.0f, Height(clientRect) * 0.54f);
        return { x, startY + index * 70.0f, width, 54.0f };
    }

    Gdiplus::RectF BackRect()
    {
        return { 22.0f, 20.0f, 46.0f, 42.0f };
    }

    Gdiplus::RectF ModeCardRect(RECT clientRect, int index)
    {
        const float width = Width(clientRect);
        const float height = Height(clientRect);
        // 窄窗口将三个模式卡片纵向排列，宽窗口显示训练/普通/困难三列。
        const bool vertical = width < 820.0f;
        if (vertical)
        {
            const float cardWidth = (std::min)(430.0f, width - 40.0f);
            const float cardHeight = (std::max)(108.0f, (std::min)(150.0f, (height - 174.0f) / 3.0f));
            const float x = (width - cardWidth) * 0.5f;
            return { x, 86.0f + index * (cardHeight + 10.0f), cardWidth, cardHeight };
        }

        const float totalWidth = (std::min)(870.0f, width - 48.0f);
        const float gap = 16.0f;
        const float cardWidth = (totalWidth - gap * 2.0f) / 3.0f;
        const float cardHeight = (std::min)(300.0f, height - 190.0f);
        const float x = (width - totalWidth) * 0.5f + index * (cardWidth + gap);
        return { x, (std::max)(112.0f, (height - cardHeight) * 0.5f + 26.0f), cardWidth, cardHeight };
    }

    Gdiplus::RectF RulesCardRect(RECT clientRect, int index)
    {
        const float width = Width(clientRect);
        const float height = Height(clientRect);
        if (width >= 700.0f)
        {
            const float totalWidth = (std::min)(840.0f, width - 36.0f);
            const float gap = 16.0f;
            const float cardWidth = (totalWidth - gap) * 0.5f;
            const float cardHeight = (std::min)(168.0f, (height - 190.0f) * 0.5f);
            const int column = index % 2;
            const int row = index / 2;
            return {
                (width - totalWidth) * 0.5f + column * (cardWidth + gap),
                92.0f + row * (cardHeight + 14.0f),
                cardWidth,
                cardHeight
            };
        }

        const float cardWidth = width - 32.0f;
        const float cardHeight = (std::max)(82.0f, (std::min)(106.0f, (height - 176.0f) * 0.25f - 7.0f));
        return { 16.0f, 76.0f + index * (cardHeight + 8.0f), cardWidth, cardHeight };
    }

    Gdiplus::RectF RulesStartRect(RECT clientRect)
    {
        const float width = (std::min)(280.0f, Width(clientRect) - 48.0f);
        return { (Width(clientRect) - width) * 0.5f, Height(clientRect) - 66.0f, width, 48.0f };
    }

    Gdiplus::RectF SettingsRowRect(RECT clientRect, int index)
    {
        const float width = (std::min)(570.0f, Width(clientRect) - 48.0f);
        const float x = (Width(clientRect) - width) * 0.5f;
        const float startY = (std::max)(132.0f, Height(clientRect) * 0.27f);
        if (index == 3)
        {
            return { x + (width - 220.0f) * 0.5f, startY + 240.0f, 220.0f, 50.0f };
        }
        return { x, startY + index * 72.0f, width, 58.0f };
    }

    Gdiplus::RectF PauseButtonRect(RECT clientRect, int index)
    {
        const float width = (std::min)(260.0f, Width(clientRect) - 56.0f);
        const float x = (Width(clientRect) - width) * 0.5f;
        const float startY = Height(clientRect) * 0.5f;
        return { x, startY + index * 66.0f, width, 50.0f };
    }

    // 匿名命名空间中的绘制组件只在本文件复用，不暴露为 MenuSystem API。
    void DrawCenteredText(
        Gdiplus::Graphics& graphics,
        const wchar_t* text,
        Gdiplus::Font& font,
        const Gdiplus::RectF& rect,
        Gdiplus::Brush& brush)
    {
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        graphics.DrawString(text, -1, &font, rect, &format, &brush);
    }

    void DrawButton(
        Gdiplus::Graphics& graphics,
        const Gdiplus::RectF& rect,
        const wchar_t* text,
        bool focused,
        bool pressed,
        bool primary,
        bool locked = false)
    {
        const float pressOffset = pressed ? 2.0f : 0.0f;
        Gdiplus::RectF drawRect(rect.X, rect.Y + pressOffset, rect.Width, rect.Height);
        Gdiplus::SolidBrush shadow(Gdiplus::Color(65, 69, 45, 31));
        Gdiplus::SolidBrush fill(locked ? kLocked : (primary ? kGold : kPanel));
        Gdiplus::Pen edge(locked ? Gdiplus::Color(255, 104, 102, 98) : (focused ? kGoldLight : kWood), focused ? 3.0f : 2.0f);
        Gdiplus::SolidBrush textBrush(locked ? Gdiplus::Color(255, 205, 203, 196) : kCharcoal);
        Gdiplus::FontFamily family(L"Microsoft YaHei");
        Gdiplus::Font font(&family, 17.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        if (!pressed)
        {
            graphics.FillRectangle(&shadow, drawRect.X + 4.0f, drawRect.Y + 5.0f, drawRect.Width, drawRect.Height);
        }
        graphics.FillRectangle(&fill, drawRect);
        graphics.DrawRectangle(&edge, drawRect);
        DrawCenteredText(graphics, text, font, drawRect, textBrush);
    }

    // 训练模式使用简洁靶心图标，不再用低精度人物块充当“模型”。
    void DrawTrainingTarget(
        Gdiplus::Graphics& graphics,
        const Gdiplus::RectF& bounds,
        bool focused)
    {
        Gdiplus::SolidBrush shadow(Gdiplus::Color(55, 69, 45, 31));
        Gdiplus::SolidBrush backdrop(Gdiplus::Color(255, 251, 238, 201));
        Gdiplus::SolidBrush center(kRed);
        Gdiplus::Pen edge(focused ? kGoldLight : kWood, focused ? 3.0f : 2.0f);
        Gdiplus::Pen ring(kWood, 4.0f);
        Gdiplus::Pen cross(kGold, 3.0f);
        graphics.FillRectangle(&shadow, bounds.X + 4.0f, bounds.Y + 5.0f, bounds.Width, bounds.Height);
        graphics.FillRectangle(&backdrop, bounds);
        graphics.DrawRectangle(&edge, bounds);

        const float padding = bounds.Width * 0.18f;
        const Gdiplus::RectF outer(
            bounds.X + padding,
            bounds.Y + padding,
            bounds.Width - padding * 2.0f,
            bounds.Height - padding * 2.0f);
        const float centerX = bounds.X + bounds.Width * 0.5f;
        const float centerY = bounds.Y + bounds.Height * 0.5f;
        graphics.DrawEllipse(&ring, outer);
        graphics.DrawEllipse(
            &cross,
            outer.X + outer.Width * 0.22f,
            outer.Y + outer.Height * 0.22f,
            outer.Width * 0.56f,
            outer.Height * 0.56f);
        graphics.DrawLine(&cross, centerX, bounds.Y + 8.0f, centerX, bounds.GetBottom() - 8.0f);
        graphics.DrawLine(&cross, bounds.X + 8.0f, centerY, bounds.GetRight() - 8.0f, centerY);
        graphics.FillEllipse(&center, centerX - 7.0f, centerY - 7.0f, 14.0f, 14.0f);
    }

    void DrawPixelLock(Gdiplus::Graphics& graphics, float centerX, float centerY, float scale)
    {
        Gdiplus::SolidBrush dark(Gdiplus::Color(255, 94, 93, 90));
        Gdiplus::SolidBrush gray(kLocked);
        Gdiplus::SolidBrush light(Gdiplus::Color(255, 180, 178, 170));
        Gdiplus::SolidBrush opening(kPanel);
        graphics.FillRectangle(&dark, centerX - 11.0f * scale, centerY - 2.0f * scale, 22.0f * scale, 17.0f * scale);
        graphics.FillRectangle(&gray, centerX - 9.0f * scale, centerY, 18.0f * scale, 13.0f * scale);
        graphics.FillRectangle(&dark, centerX - 8.0f * scale, centerY - 12.0f * scale, 16.0f * scale, 12.0f * scale);
        graphics.FillRectangle(&opening, centerX - 5.0f * scale, centerY - 9.0f * scale, 10.0f * scale, 9.0f * scale);
        graphics.FillRectangle(&light, centerX - 7.0f * scale, centerY + 2.0f * scale, 5.0f * scale, 8.0f * scale);
        graphics.FillRectangle(&dark, centerX - 2.0f * scale, centerY + 5.0f * scale, 4.0f * scale, 6.0f * scale);
    }

    // 透明头像使用独立底板与双层边框；高质量缩放保证 512px 原图缩到菜单尺寸时
    // 五官仍然清晰。加载失败时只显示简洁徽记，不退回粗糙人物占位模型。
    void DrawBossPortrait(
        Gdiplus::Graphics& graphics,
        Gdiplus::Image* portrait,
        const Gdiplus::RectF& bounds,
        bool hardMode,
        bool focused)
    {
        const Gdiplus::Color accent = hardMode
            ? Gdiplus::Color(255, 225, 45, 78)
            : Gdiplus::Color(255, 190, 55, 133);
        Gdiplus::SolidBrush shadow(Gdiplus::Color(75, 42, 20, 40));
        Gdiplus::SolidBrush backdrop(hardMode
            ? Gdiplus::Color(255, 45, 18, 39)
            : Gdiplus::Color(255, 57, 30, 54));
        Gdiplus::Pen outer(focused ? kGoldLight : accent, focused ? 4.0f : 2.5f);
        Gdiplus::Pen inner(Gdiplus::Color(170, 255, 196, 233), 1.0f);
        graphics.FillRectangle(&shadow, bounds.X + 5.0f, bounds.Y + 6.0f, bounds.Width, bounds.Height);
        graphics.FillRectangle(&backdrop, bounds);

        if (portrait != nullptr && portrait->GetLastStatus() == Gdiplus::Ok)
        {
            const Gdiplus::GraphicsState state = graphics.Save();
            graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            const Gdiplus::RectF imageRect(
                bounds.X + 3.0f,
                bounds.Y + 3.0f,
                bounds.Width - 6.0f,
                bounds.Height - 6.0f);
            graphics.DrawImage(portrait, imageRect);
            graphics.Restore(state);
        }
        else
        {
            Gdiplus::SolidBrush crest(accent);
            Gdiplus::PointF lightning[] = {
                { bounds.X + bounds.Width * 0.56f, bounds.Y + bounds.Height * 0.15f },
                { bounds.X + bounds.Width * 0.32f, bounds.Y + bounds.Height * 0.54f },
                { bounds.X + bounds.Width * 0.51f, bounds.Y + bounds.Height * 0.54f },
                { bounds.X + bounds.Width * 0.40f, bounds.Y + bounds.Height * 0.85f },
                { bounds.X + bounds.Width * 0.73f, bounds.Y + bounds.Height * 0.43f },
                { bounds.X + bounds.Width * 0.55f, bounds.Y + bounds.Height * 0.43f }
            };
            graphics.FillPolygon(&crest, lightning, 6);
        }

        graphics.DrawRectangle(&outer, bounds);
        graphics.DrawRectangle(
            &inner,
            bounds.X + 5.0f,
            bounds.Y + 5.0f,
            bounds.Width - 10.0f,
            bounds.Height - 10.0f);
        if (hardMode)
        {
            Gdiplus::Pen corner(accent, 4.0f);
            const float arm = (std::min)(18.0f, bounds.Width * 0.18f);
            graphics.DrawLine(&corner, bounds.X, bounds.Y, bounds.X + arm, bounds.Y);
            graphics.DrawLine(&corner, bounds.X, bounds.Y, bounds.X, bounds.Y + arm);
            graphics.DrawLine(&corner, bounds.GetRight(), bounds.GetBottom(), bounds.GetRight() - arm, bounds.GetBottom());
            graphics.DrawLine(&corner, bounds.GetRight(), bounds.GetBottom(), bounds.GetRight(), bounds.GetBottom() - arm);
        }
    }

    // 规则卡片只使用清晰的武器/技能符号，不再绘制低精度方块人物模型。
    void DrawRuleIllustration(Gdiplus::Graphics& graphics, const Gdiplus::RectF& card, int index)
    {
        const float scale = card.Height < 120.0f ? 0.78f : 1.0f;
        const float centerX = card.X + 72.0f * scale + 10.0f;
        const float centerY = card.Y + card.Height * 0.53f;
        Gdiplus::SolidBrush gold(kGold);
        Gdiplus::SolidBrush blue(Gdiplus::Color(255, 53, 145, 230));
        Gdiplus::SolidBrush red(kRed);
        Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 248, 218));
        Gdiplus::SolidBrush iconBack(Gdiplus::Color(210, 255, 247, 220));
        Gdiplus::Pen backdropEdge(Gdiplus::Color(180, 156, 110, 68), 2.0f * scale);
        Gdiplus::Pen darkPen(kCharcoal, 6.0f * scale);
        Gdiplus::Pen goldPen(kGoldLight, 4.0f * scale);
        Gdiplus::Pen bluePen(Gdiplus::Color(230, 76, 195, 255), 4.0f * scale);
        Gdiplus::Pen redPen(Gdiplus::Color(245, 208, 48, 50), 4.0f * scale);

        const float backdropSize = 100.0f * scale;
        graphics.FillEllipse(
            &iconBack,
            centerX - backdropSize * 0.5f,
            centerY - backdropSize * 0.5f,
            backdropSize,
            backdropSize);
        graphics.DrawEllipse(
            &backdropEdge,
            centerX - backdropSize * 0.5f,
            centerY - backdropSize * 0.5f,
            backdropSize,
            backdropSize);

        if (index == 0)
        {
            // 普攻：球棒、挥击弧线与红色命中靶构成一眼可读的攻击图标。
            graphics.DrawEllipse(
                &redPen,
                centerX + 17.0f * scale,
                centerY - 19.0f * scale,
                34.0f * scale,
                34.0f * scale);
            graphics.FillEllipse(&red, centerX + 29.0f * scale, centerY - 7.0f * scale, 10.0f * scale, 10.0f * scale);
            graphics.DrawLine(
                &darkPen,
                centerX - 34.0f * scale,
                centerY + 29.0f * scale,
                centerX + 16.0f * scale,
                centerY - 17.0f * scale);
            graphics.DrawLine(
                &goldPen,
                centerX - 31.0f * scale,
                centerY + 26.0f * scale,
                centerX + 13.0f * scale,
                centerY - 14.0f * scale);
            graphics.DrawArc(
                &goldPen,
                centerX - 42.0f * scale,
                centerY - 43.0f * scale,
                88.0f * scale,
                77.0f * scale,
                210.0f,
                135.0f);
        }
        else if (index == 1)
        {
            // 反击：蓝色盾面挡住红色来袭线，金色回转弧表示立即反制。
            Gdiplus::PointF shield[] = {
                { centerX - 22.0f * scale, centerY - 33.0f * scale },
                { centerX + 9.0f * scale, centerY - 22.0f * scale },
                { centerX + 4.0f * scale, centerY + 18.0f * scale },
                { centerX - 22.0f * scale, centerY + 35.0f * scale }
            };
            graphics.FillPolygon(&blue, shield, 4);
            graphics.DrawPolygon(&darkPen, shield, 4);
            graphics.DrawLine(
                &redPen,
                centerX + 48.0f * scale,
                centerY - 27.0f * scale,
                centerX + 6.0f * scale,
                centerY);
            graphics.DrawArc(
                &goldPen,
                centerX - 7.0f * scale,
                centerY - 36.0f * scale,
                63.0f * scale,
                70.0f * scale,
                300.0f,
                118.0f);
            graphics.FillEllipse(&white, centerX - 2.0f * scale, centerY - 7.0f * scale, 14.0f * scale, 14.0f * scale);
        }
        else if (index == 2)
        {
            // 投掷：倾斜球棒、三条速度线和末端圆环表示高速直线飞行。
            graphics.DrawEllipse(
                &bluePen,
                centerX + 24.0f * scale,
                centerY - 17.0f * scale,
                28.0f * scale,
                28.0f * scale);
            graphics.DrawLine(&bluePen, centerX - 48.0f * scale, centerY - 24.0f * scale, centerX - 15.0f * scale, centerY - 24.0f * scale);
            graphics.DrawLine(&bluePen, centerX - 54.0f * scale, centerY, centerX - 20.0f * scale, centerY);
            graphics.DrawLine(&bluePen, centerX - 46.0f * scale, centerY + 24.0f * scale, centerX - 13.0f * scale, centerY + 24.0f * scale);
            graphics.DrawLine(
                &darkPen,
                centerX - 13.0f * scale,
                centerY + 27.0f * scale,
                centerX + 27.0f * scale,
                centerY - 28.0f * scale);
            graphics.DrawLine(
                &goldPen,
                centerX - 10.0f * scale,
                centerY + 23.0f * scale,
                centerX + 24.0f * scale,
                centerY - 24.0f * scale);
        }
        else
        {
            // 终结技：同心能量环、中心星芒和五个节点对应五段攻击。
            graphics.DrawEllipse(&bluePen, centerX - 39.0f * scale, centerY - 39.0f * scale, 78.0f * scale, 78.0f * scale);
            graphics.DrawEllipse(&goldPen, centerX - 25.0f * scale, centerY - 25.0f * scale, 50.0f * scale, 50.0f * scale);
            graphics.FillRectangle(&white, centerX - 4.0f * scale, centerY - 31.0f * scale, 8.0f * scale, 62.0f * scale);
            graphics.FillRectangle(&white, centerX - 31.0f * scale, centerY - 4.0f * scale, 62.0f * scale, 8.0f * scale);
            constexpr float kNodeAngles[] = { -90.0f, -18.0f, 54.0f, 126.0f, 198.0f };
            for (float angleDegrees : kNodeAngles)
            {
                const float angle = angleDegrees * 3.1415926f / 180.0f;
                const float nodeX = centerX + std::cos(angle) * 43.0f * scale;
                const float nodeY = centerY + std::sin(angle) * 43.0f * scale;
                graphics.FillEllipse(&gold, nodeX - 5.0f * scale, nodeY - 5.0f * scale, 10.0f * scale, 10.0f * scale);
            }
        }
    }
}

void MenuSystem::LoadContent()
{
    const std::wstring path = FindAssetPath(
        L"游戏\\assets\\kafka_boss_v2\\runtime\\kafka_portrait_ui_v3.png");
    auto portrait = std::make_unique<Gdiplus::Image>(path.c_str());
    if (portrait->GetLastStatus() == Gdiplus::Ok)
    {
        bossPortrait_ = std::move(portrait);
    }
    else
    {
        bossPortrait_.reset();
    }
}

void MenuSystem::UnloadContent()
{
    bossPortrait_.reset();
}

void MenuSystem::Update(float deltaSeconds)
{
    // introTimer 有上限，lockedFeedbackTimer 有下限，避免浮点计时无限增长或变负。
    introTimer_ = (std::min)(0.32f, introTimer_ + deltaSeconds);
    lockedFeedbackTimer_ = (std::max)(0.0f, lockedFeedbackTimer_ - deltaSeconds);

    if (!transitioning_)
    {
        return;
    }

    transitionTimer_ += deltaSeconds;
    // 过渡结束才提交 page_，并重置新页面的交互焦点。
    if (transitionTimer_ >= kTransitionSeconds)
    {
        page_ = nextPage_;
        transitioning_ = false;
        transitionTimer_ = 0.0f;
        selectedIndex_ = 0;
        hoveredIndex_ = -1;
        pressedIndex_ = -1;
    }
}

void MenuSystem::Draw(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    // 暂停页和失败页是游戏画面上的半透明叠层，不绘制主菜单背景。
    if ((page_ == MenuPage::Pause || page_ == MenuPage::GameOver || page_ == MenuPage::Victory) && !transitioning_)
    {
        if (page_ == MenuPage::Pause)
        {
            DrawPause(graphics, clientRect);
        }
        else if (page_ == MenuPage::GameOver)
        {
            DrawGameOver(graphics, clientRect);
        }
        else
        {
            DrawVictory(graphics, clientRect);
        }
        return;
    }

    DrawBackground(graphics, clientRect);
    if (!transitioning_)
    {
        const float intro = EaseOut(introTimer_ / 0.32f);
        const float offset = page_ == MenuPage::Main ? (1.0f - intro) * 18.0f : 0.0f;
        DrawPage(graphics, clientRect, page_, offset);
        return;
    }

    // 旧页向左退出，新页从右侧进入，两者位移之和始终等于一屏宽。
    const float progress = EaseOut(transitionTimer_ / kTransitionSeconds);
    const float width = Width(clientRect);
    DrawPage(graphics, clientRect, page_, -width * progress);
    DrawPage(graphics, clientRect, nextPage_, width * (1.0f - progress));
}

void MenuSystem::Show(MenuPage page, bool animate)
{
    if (animate)
    {
        RequestPage(page);
        return;
    }

    page_ = page;
    nextPage_ = page;
    transitioning_ = false;
    transitionTimer_ = 0.0f;
    selectedIndex_ = 0;
    hoveredIndex_ = -1;
    pressedIndex_ = -1;
}

void MenuSystem::HandleMouseMove(int x, int y, RECT clientRect)
{
    lastClientRect_ = clientRect;
    if (transitioning_)
    {
        return;
    }

    hoveredIndex_ = HitTest(x, y, clientRect);
    if (hoveredIndex_ >= 0)
    {
        selectedIndex_ = hoveredIndex_;
    }
    // 拖动时即使鼠标已经离开滑条矩形，也继续按横坐标更新并 Clamp 到端点。
    if (draggingSlider_)
    {
        AdjustSliderFromMouse(x, clientRect);
    }
}

void MenuSystem::HandleMouseDown(int x, int y, RECT clientRect)
{
    lastClientRect_ = clientRect;
    if (transitioning_)
    {
        return;
    }

    pressedIndex_ = HitTest(x, y, clientRect);
    if (pressedIndex_ >= 0)
    {
        selectedIndex_ = pressedIndex_;
    }
    draggingSlider_ = page_ == MenuPage::Settings && (selectedIndex_ == 0 || selectedIndex_ == 1) && pressedIndex_ >= 0;
    if (draggingSlider_)
    {
        AdjustSliderFromMouse(x, clientRect);
    }
}

void MenuSystem::HandleMouseUp(int x, int y, RECT clientRect)
{
    lastClientRect_ = clientRect;
    const int releasedIndex = HitTest(x, y, clientRect);
    // 按下和松开必须落在同一控件，拖动滑条也不能被误判成按钮点击。
    const bool activate = !transitioning_ && pressedIndex_ >= 0 && pressedIndex_ == releasedIndex && !draggingSlider_;
    draggingSlider_ = false;
    pressedIndex_ = -1;
    if (activate)
    {
        selectedIndex_ = releasedIndex;
        ActivateSelection();
    }
}

void MenuSystem::HandleKeyDown(WPARAM key)
{
    if (transitioning_)
    {
        return;
    }

    if (key == VK_ESCAPE)
    {
        if (page_ == MenuPage::ModeSelect || page_ == MenuPage::Settings)
        {
            RequestPage(MenuPage::Main);
        }
        else if (page_ == MenuPage::Rules)
        {
            RequestPage(MenuPage::Main);
        }
        else if (page_ == MenuPage::Pause)
        {
            pendingAction_ = MenuAction::ResumeGame;
        }
        else if (page_ == MenuPage::GameOver || page_ == MenuPage::Victory)
        {
            pendingAction_ = MenuAction::ReturnToMain;
        }
        return;
    }

    // 设置页在滑条选中时将左右键解释为微调，其余情况解释为焦点导航。
    if (key == VK_UP || key == 'W' || key == VK_LEFT || key == 'A')
    {
        if (page_ == MenuPage::Settings && (key == VK_LEFT || key == 'A') && selectedIndex_ < 2)
        {
            float& value = selectedIndex_ == 0 ? masterVolume_ : effectsVolume_;
            value = Clamp01(value - 0.05f);
        }
        else
        {
            MoveSelection(-1);
        }
        return;
    }

    if (key == VK_DOWN || key == 'S' || key == VK_RIGHT || key == 'D')
    {
        if (page_ == MenuPage::Settings && (key == VK_RIGHT || key == 'D') && selectedIndex_ < 2)
        {
            float& value = selectedIndex_ == 0 ? masterVolume_ : effectsVolume_;
            value = Clamp01(value + 0.05f);
        }
        else
        {
            MoveSelection(1);
        }
        return;
    }

    if (key == VK_RETURN || key == VK_SPACE || key == 'J')
    {
        ActivateSelection();
    }
}

MenuAction MenuSystem::ConsumeAction()
{
    const MenuAction action = pendingAction_;
    pendingAction_ = MenuAction::None;
    return action;
}

MenuPage MenuSystem::GetPage() const
{
    return page_;
}

float MenuSystem::GetMasterVolume() const
{
    return masterVolume_;
}

float MenuSystem::GetEffectsVolume() const
{
    return effectsVolume_;
}

void MenuSystem::SetFullscreen(bool fullscreen)
{
    fullscreen_ = fullscreen;
}

void MenuSystem::RequestPage(MenuPage page)
{
    if (transitioning_ || page == page_)
    {
        return;
    }
    nextPage_ = page;
    transitioning_ = true;
    transitionTimer_ = 0.0f;
    hoveredIndex_ = -1;
    pressedIndex_ = -1;
}

void MenuSystem::ActivateSelection()
{
    // selectedIndex 的意义随 page_ 变化；这里只生成页面请求或交给 Game 的动作。
    switch (page_)
    {
    case MenuPage::Main:
        if (selectedIndex_ == 0)
        {
            RequestPage(MenuPage::Rules);
        }
        else
        {
            RequestPage(MenuPage::Settings);
        }
        break;
    case MenuPage::ModeSelect:
        if (selectedIndex_ == 0)
        {
            pendingAction_ = MenuAction::StartTraining;
        }
        else if (selectedIndex_ == 1)
        {
            pendingAction_ = MenuAction::StartChallenge;
        }
        else if (selectedIndex_ == 2)
        {
            pendingAction_ = MenuAction::StartHardChallenge;
        }
        else
        {
            RequestPage(MenuPage::Main);
        }
        break;
    case MenuPage::Rules:
        if (selectedIndex_ == 0)
        {
            RequestPage(MenuPage::ModeSelect);
        }
        else
        {
            RequestPage(MenuPage::Main);
        }
        break;
    case MenuPage::Settings:
        if (selectedIndex_ == 2)
        {
            pendingAction_ = MenuAction::ToggleFullscreen;
        }
        else if (selectedIndex_ == 3)
        {
            RequestPage(MenuPage::Main);
        }
        break;
    case MenuPage::Pause:
        pendingAction_ = selectedIndex_ == 0 ? MenuAction::ResumeGame : MenuAction::ReturnToMain;
        break;
    case MenuPage::GameOver:
        pendingAction_ = selectedIndex_ == 0 ? MenuAction::RestartCurrent : MenuAction::ReturnToMain;
        break;
    case MenuPage::Victory:
        pendingAction_ = selectedIndex_ == 0 ? MenuAction::RestartCurrent : MenuAction::ReturnToMain;
        break;
    }
}

void MenuSystem::MoveSelection(int direction)
{
    int itemCount = 2;
    if (page_ == MenuPage::ModeSelect || page_ == MenuPage::Settings)
    {
        itemCount = 4;
    }
    // 加 itemCount 后取模，实现从首项向上回绕到末项。
    selectedIndex_ = (selectedIndex_ + direction + itemCount) % itemCount;
    hoveredIndex_ = -1;
}

int MenuSystem::HitTest(int x, int y, RECT clientRect) const
{
    // 返回值与键盘 selectedIndex 共用；返回 -1 代表未命中交互区域。
    if (page_ == MenuPage::Main)
    {
        for (int i = 0; i < 2; ++i)
        {
            if (Contains(MainButtonRect(clientRect, i), x, y))
            {
                return i;
            }
        }
    }
    else if (page_ == MenuPage::ModeSelect)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (Contains(ModeCardRect(clientRect, i), x, y))
            {
                return i;
            }
        }
        if (Contains(BackRect(), x, y))
        {
            return 3;
        }
    }
    else if (page_ == MenuPage::Rules)
    {
        if (Contains(RulesStartRect(clientRect), x, y))
        {
            return 0;
        }
        if (Contains(BackRect(), x, y))
        {
            return 1;
        }
    }
    else if (page_ == MenuPage::Settings)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (Contains(SettingsRowRect(clientRect, i), x, y))
            {
                return i;
            }
        }
        if (Contains(BackRect(), x, y))
        {
            return 3;
        }
    }
    else
    {
        for (int i = 0; i < 2; ++i)
        {
            if (Contains(PauseButtonRect(clientRect, i), x, y))
            {
                return i;
            }
        }
    }
    return -1;
}

void MenuSystem::AdjustSliderFromMouse(int x, RECT clientRect)
{
    if (selectedIndex_ != 0 && selectedIndex_ != 1)
    {
        return;
    }

    const Gdiplus::RectF row = SettingsRowRect(clientRect, selectedIndex_);
    const float start = row.X + 190.0f;
    const float end = row.GetRight() - 28.0f;
    float& value = selectedIndex_ == 0 ? masterVolume_ : effectsVolume_;
    // 把滑条像素坐标线性映射到 [0, 1] 音量。
    value = Clamp01((static_cast<float>(x) - start) / (end - start));
}

void MenuSystem::DrawPage(Gdiplus::Graphics& graphics, RECT clientRect, MenuPage page, float offsetX) const
{
    // Save/Restore 保证页面过渡的平移矩阵不会污染之后的 HUD/场景绘制。
    const Gdiplus::GraphicsState state = graphics.Save();
    graphics.TranslateTransform(offsetX, 0.0f);
    switch (page)
    {
    case MenuPage::Main:
        DrawMain(graphics, clientRect);
        break;
    case MenuPage::ModeSelect:
        DrawModes(graphics, clientRect);
        break;
    case MenuPage::Rules:
        DrawRules(graphics, clientRect);
        break;
    case MenuPage::Settings:
        DrawSettings(graphics, clientRect);
        break;
    case MenuPage::Pause:
        DrawPause(graphics, clientRect);
        break;
    case MenuPage::GameOver:
        DrawGameOver(graphics, clientRect);
        break;
    case MenuPage::Victory:
        DrawVictory(graphics, clientRect);
        break;
    }
    graphics.Restore(state);
}

void MenuSystem::DrawBackground(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    // 背景和装饰网格覆盖完整客户区；各页面只绘制前景内容。
    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    Gdiplus::SolidBrush background(kBackground);
    Gdiplus::Pen grid(kGrid, 1.0f);
    graphics.FillRectangle(&background, 0, 0, width, height);
    for (int x = 0; x < width; x += 48)
    {
        graphics.DrawLine(&grid, x, 0, x, height);
    }
    for (int y = 0; y < height; y += 48)
    {
        graphics.DrawLine(&grid, 0, y, width, y);
    }
}

void MenuSystem::DrawMain(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&family, 34.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font subtitleFont(&family, 13.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush title(kCharcoal);
    Gdiplus::SolidBrush soft(kSoftText);
    Gdiplus::SolidBrush red(kRed);
    const float width = Width(clientRect);
    const float titleY = (std::max)(62.0f, Height(clientRect) * 0.17f);

    DrawCenteredText(graphics, L"\u7a79  \u6218\u6597\u8bad\u7ec3", titleFont, { 20.0f, titleY, width - 40.0f, 58.0f }, title);
    graphics.FillRectangle(&red, width * 0.5f - 42.0f, titleY + 63.0f, 84.0f, 4.0f);
    DrawCenteredText(graphics, L"QIONG COMBAT TRAINING", subtitleFont, { 20.0f, titleY + 72.0f, width - 40.0f, 28.0f }, soft);

    for (int i = 0; i < 2; ++i)
    {
        const bool focused = i == selectedIndex_ || i == hoveredIndex_;
        const wchar_t* label = i == 0 ? L"\u5f00\u59cb\u6e38\u620f   START" : L"\u8bbe\u7f6e   SETTINGS";
        DrawButton(graphics, MainButtonRect(clientRect, i), label, focused, i == pressedIndex_, i == 0);
    }

    DrawCenteredText(
        graphics,
        L"W / S \u9009\u62e9     ENTER \u786e\u8ba4",
        subtitleFont,
        { 16.0f, Height(clientRect) - 48.0f, width - 32.0f, 24.0f },
        soft);
}

void MenuSystem::DrawModes(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&family, 26.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font cardFont(&family, 19.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(&family, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush title(kCharcoal);
    Gdiplus::SolidBrush soft(kSoftText);
    const float width = Width(clientRect);
    const bool vertical = width < 820.0f;

    DrawCenteredText(graphics, L"\u9009\u62e9\u6a21\u5f0f", titleFont, { 80.0f, 24.0f, width - 160.0f, 54.0f }, title);
    const Gdiplus::RectF back = BackRect();
    Gdiplus::Pen backPen((hoveredIndex_ == 3 || selectedIndex_ == 3) ? kGold : kWood, 3.0f);
    graphics.DrawLine(&backPen, back.X + 30.0f, back.Y + 9.0f, back.X + 15.0f, back.Y + 21.0f);
    graphics.DrawLine(&backPen, back.X + 15.0f, back.Y + 21.0f, back.X + 30.0f, back.Y + 33.0f);

    for (int i = 0; i < 3; ++i)
    {
        Gdiplus::RectF card = ModeCardRect(clientRect, i);
        const bool focused = i == selectedIndex_ || i == hoveredIndex_;
        const Gdiplus::Color idleEdge = i == 2 ? Gdiplus::Color(255, 170, 48, 92) : kWood;
        Gdiplus::Pen edge(focused ? kGoldLight : idleEdge, focused ? 3.0f : 2.0f);
        Gdiplus::SolidBrush shadow(Gdiplus::Color(45, 69, 45, 31));
        const Gdiplus::Color cardColor = i == 2
            ? Gdiplus::Color(248, 252, 232, 238)
            : (i == 1 ? Gdiplus::Color(248, 250, 238, 242) : kPanel);
        Gdiplus::SolidBrush cardPanel(cardColor);
        Gdiplus::SolidBrush topAccent(i == 2
            ? Gdiplus::Color(255, 212, 44, 79)
            : (i == 1 ? Gdiplus::Color(255, 175, 55, 126) : kGold));
        graphics.FillRectangle(&shadow, card.X + 5.0f, card.Y + 6.0f, card.Width, card.Height);
        graphics.FillRectangle(&cardPanel, card);
        graphics.DrawRectangle(&edge, card);
        graphics.FillRectangle(&topAccent, card.X, card.Y, card.Width, focused ? 6.0f : 4.0f);

        const float visualSize = vertical
            ? (std::min)(96.0f, card.Height - 16.0f)
            : (std::min)(156.0f, (std::min)(card.Width - 36.0f, card.Height - 112.0f));
        const Gdiplus::RectF visual = vertical
            ? Gdiplus::RectF(card.X + 14.0f, card.Y + (card.Height - visualSize) * 0.5f, visualSize, visualSize)
            : Gdiplus::RectF(card.X + (card.Width - visualSize) * 0.5f, card.Y + 18.0f, visualSize, visualSize);
        if (i == 0)
        {
            DrawTrainingTarget(graphics, visual, focused);
        }
        else
        {
            DrawBossPortrait(graphics, bossPortrait_.get(), visual, i == 2, focused);
        }

        const float textX = vertical ? card.X + 122.0f : card.X;
        const float textWidth = vertical ? card.GetRight() - textX - 12.0f : card.Width;
        const float nameY = vertical ? card.Y + card.Height * 0.5f - 35.0f : card.Y + card.Height - 88.0f;
        Gdiplus::SolidBrush nameBrush(kCharcoal);
        const wchar_t* modeName = i == 0
            ? L"\u8bad\u7ec3\u6a21\u5f0f"
            : (i == 1 ? L"\u666e\u901a\u95ef\u5173" : L"\u56f0\u96be\u6311\u6218");
        const wchar_t* modeDescription = i == 0
            ? L"\u7a3b\u8349\u4eba\u81ea\u7531\u7ec3\u4e60"
            : (i == 1 ? L"\u7b2c\u4e00\u5173\uff1a\u51fb\u8d25\u5361\u8299\u5361" : L"\u5f3a\u5316\u5361\u8299\u5361 \u00b7 \u9ad8\u538b\u6311\u6218");
        DrawCenteredText(graphics, modeName, cardFont,
            { textX, nameY, textWidth, 34.0f }, nameBrush);
        DrawCenteredText(
            graphics,
            modeDescription,
            smallFont,
            { textX, nameY + 37.0f, textWidth, 28.0f },
            soft);
    }
}

void MenuSystem::DrawRules(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&family, 25.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font cardTitleFont(&family, 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(&family, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font footerFont(&family, 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush title(kCharcoal);
    Gdiplus::SolidBrush body(kSoftText);
    Gdiplus::SolidBrush panel(kPanel);
    Gdiplus::SolidBrush keyFill(kGold);

    const float width = Width(clientRect);
    DrawCenteredText(graphics, L"战斗规则", titleFont, { 80.0f, 18.0f, width - 160.0f, 48.0f }, title);
    DrawCenteredText(graphics, L"先了解基本操作，再进入训练场", bodyFont, { 80.0f, 58.0f, width - 160.0f, 24.0f }, body);

    const Gdiplus::RectF back = BackRect();
    Gdiplus::Pen backPen((hoveredIndex_ == 1 || selectedIndex_ == 1) ? kGold : kWood, 3.0f);
    graphics.DrawLine(&backPen, back.X + 30.0f, back.Y + 9.0f, back.X + 15.0f, back.Y + 21.0f);
    graphics.DrawLine(&backPen, back.X + 15.0f, back.Y + 21.0f, back.X + 30.0f, back.Y + 33.0f);

    const wchar_t* keys[] = { L"J", L"K", L"Q", L"P" };
    const wchar_t* names[] = { L"普通攻击", L"格挡反击", L"投掷球棒", L"终结技" };
    const wchar_t* descriptions[] =
    {
        L"连续按键可衔接攻击；持棍时攻击距离更远。",
        L"在敌人攻击时格挡，成功后会立即反击来袭敌人。",
        L"消耗 50 能量掷出球棒；靠近落地球棒可将其捡回。",
        L"击败 5 只怪物后解锁；能量达到 100 时发动五段攻击。"
    };

    for (int index = 0; index < 4; ++index)
    {
        const Gdiplus::RectF card = RulesCardRect(clientRect, index);
        Gdiplus::Pen edge(index == 1 ? Gdiplus::Color(255, 72, 142, 205) : kWood, 2.0f);
        graphics.FillRectangle(&panel, card);
        graphics.DrawRectangle(&edge, card);
        DrawRuleIllustration(graphics, card, index);

        const float textX = card.X + (card.Height < 120.0f ? 132.0f : 148.0f);
        const float textWidth = (std::max)(80.0f, card.GetRight() - textX - 14.0f);
        const float titleY = card.Y + (card.Height < 120.0f ? 13.0f : 25.0f);
        graphics.FillRectangle(&keyFill, textX, titleY, 28.0f, 24.0f);
        DrawCenteredText(graphics, keys[index], cardTitleFont, { textX, titleY, 28.0f, 24.0f }, title);
        graphics.DrawString(names[index], -1, &cardTitleFont, Gdiplus::PointF(textX + 38.0f, titleY + 1.0f), &title);
        graphics.DrawString(
            descriptions[index],
            -1,
            &bodyFont,
            Gdiplus::RectF(textX, titleY + 34.0f, textWidth, card.Height - 48.0f),
            nullptr,
            &body);
    }

    DrawCenteredText(
        graphics,
        L"W A S D  移动     CTRL  下蹲     ESC  暂停",
        footerFont,
        { 20.0f, Height(clientRect) - 98.0f, width - 40.0f, 24.0f },
        body);
    DrawButton(
        graphics,
        RulesStartRect(clientRect),
        L"了解，选择模式",
        selectedIndex_ == 0 || hoveredIndex_ == 0,
        pressedIndex_ == 0,
        true);
}

void MenuSystem::DrawSettings(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&family, 26.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font labelFont(&family, 15.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font valueFont(&family, 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush title(kCharcoal);
    Gdiplus::SolidBrush panel(kPanel);
    Gdiplus::SolidBrush soft(kSoftText);
    Gdiplus::SolidBrush gold(kGold);
    Gdiplus::SolidBrush track(kGrid);
    const float width = Width(clientRect);

    DrawCenteredText(graphics, L"\u8bbe\u7f6e", titleFont, { 80.0f, 24.0f, width - 160.0f, 54.0f }, title);
    const Gdiplus::RectF back = BackRect();
    Gdiplus::Pen backPen(kWood, 3.0f);
    graphics.DrawLine(&backPen, back.X + 30.0f, back.Y + 9.0f, back.X + 15.0f, back.Y + 21.0f);
    graphics.DrawLine(&backPen, back.X + 15.0f, back.Y + 21.0f, back.X + 30.0f, back.Y + 33.0f);

    for (int i = 0; i < 3; ++i)
    {
        const Gdiplus::RectF row = SettingsRowRect(clientRect, i);
        const bool focused = i == selectedIndex_ || i == hoveredIndex_;
        Gdiplus::Pen edge(focused ? kGold : kWood, focused ? 3.0f : 1.5f);
        graphics.FillRectangle(&panel, row);
        graphics.DrawRectangle(&edge, row);
        const wchar_t* label = i == 0 ? L"\u4e3b\u97f3\u91cf" : (i == 1 ? L"\u97f3\u6548\u97f3\u91cf" : L"\u5168\u5c4f\u663e\u793a");
        graphics.DrawString(label, -1, &labelFont, Gdiplus::PointF(row.X + 22.0f, row.Y + 17.0f), &title);

        if (i < 2)
        {
            const float value = i == 0 ? masterVolume_ : effectsVolume_;
            const float start = row.X + 190.0f;
            const float end = row.GetRight() - 28.0f;
            const float knob = start + (end - start) * value;
            graphics.FillRectangle(&track, start, row.Y + 27.0f, end - start, 6.0f);
            graphics.FillRectangle(&gold, start, row.Y + 27.0f, knob - start, 6.0f);
            graphics.FillRectangle(&title, knob - 5.0f, row.Y + 20.0f, 10.0f, 20.0f);
            wchar_t valueText[16]{};
            wsprintfW(valueText, L"%d%%", static_cast<int>(value * 100.0f + 0.5f));
            graphics.DrawString(valueText, -1, &valueFont, Gdiplus::PointF(row.GetRight() - 64.0f, row.Y + 6.0f), &soft);
        }
        else
        {
            const float toggleWidth = 56.0f;
            const float x = row.GetRight() - 82.0f;
            const float y = row.Y + 16.0f;
            Gdiplus::SolidBrush toggleFill(fullscreen_ ? kGold : kLocked);
            graphics.FillRectangle(&toggleFill, x, y, toggleWidth, 27.0f);
            graphics.FillRectangle(&title, fullscreen_ ? x + 33.0f : x + 4.0f, y + 4.0f, 19.0f, 19.0f);
        }
    }

    DrawButton(graphics, SettingsRowRect(clientRect, 3), L"\u8fd4\u56de", selectedIndex_ == 3 || hoveredIndex_ == 3, pressedIndex_ == 3, false);
}

void MenuSystem::DrawPause(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    Gdiplus::SolidBrush overlay(Gdiplus::Color(175, 43, 42, 47));
    graphics.FillRectangle(&overlay, 0, 0, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&family, 30.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush title(kGoldLight);
    DrawCenteredText(graphics, L"\u5df2\u6682\u505c", titleFont, { 0.0f, Height(clientRect) * 0.30f, Width(clientRect), 60.0f }, title);
    DrawButton(graphics, PauseButtonRect(clientRect, 0), L"\u7ee7\u7eed\u6e38\u620f", selectedIndex_ == 0 || hoveredIndex_ == 0, pressedIndex_ == 0, true);
    DrawButton(graphics, PauseButtonRect(clientRect, 1), L"\u8fd4\u56de\u4e3b\u83dc\u5355", selectedIndex_ == 1 || hoveredIndex_ == 1, pressedIndex_ == 1, false);
}

void MenuSystem::DrawGameOver(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    Gdiplus::SolidBrush overlay(Gdiplus::Color(205, 31, 25, 29));
    graphics.FillRectangle(&overlay, 0, 0, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);

    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&family, 36.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font subtitleFont(&family, 14.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush title(Gdiplus::Color(255, 244, 82, 72));
    Gdiplus::SolidBrush subtitle(Gdiplus::Color(255, 232, 218, 202));

    const float width = Width(clientRect);
    const float height = Height(clientRect);
    DrawCenteredText(graphics, L"\u6e38\u620f\u5931\u8d25", titleFont, { 0.0f, height * 0.22f, width, 58.0f }, title);
    DrawCenteredText(graphics, L"\u7a79\u5df2\u5931\u53bb\u6218\u6597\u80fd\u529b", subtitleFont, { 0.0f, height * 0.22f + 58.0f, width, 32.0f }, subtitle);
    DrawButton(graphics, PauseButtonRect(clientRect, 0), L"\u91cd\u65b0\u5f00\u59cb", selectedIndex_ == 0 || hoveredIndex_ == 0, pressedIndex_ == 0, true);
    DrawButton(graphics, PauseButtonRect(clientRect, 1), L"\u8fd4\u56de\u4e3b\u9875\u9762", selectedIndex_ == 1 || hoveredIndex_ == 1, pressedIndex_ == 1, false);
}

void MenuSystem::DrawVictory(Gdiplus::Graphics& graphics, RECT clientRect) const
{
    Gdiplus::SolidBrush overlay(Gdiplus::Color(205, 31, 25, 38));
    graphics.FillRectangle(&overlay, 0, 0, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);

    Gdiplus::FontFamily family(L"Microsoft YaHei");
    Gdiplus::Font titleFont(&family, 36.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font subtitleFont(&family, 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush title(Gdiplus::Color(255, 255, 214, 86));
    Gdiplus::SolidBrush subtitle(Gdiplus::Color(255, 242, 226, 238));

    const float width = Width(clientRect);
    const float height = Height(clientRect);
    DrawCenteredText(graphics, L"\u7b2c\u4e00\u5173\u5b8c\u6210", titleFont,
        { 0.0f, height * 0.22f, width, 58.0f }, title);
    DrawCenteredText(graphics, L"\u5df2\u51fb\u8d25\u5361\u8299\u5361", subtitleFont,
        { 0.0f, height * 0.22f + 58.0f, width, 32.0f }, subtitle);
    DrawButton(graphics, PauseButtonRect(clientRect, 0), L"\u91cd\u65b0\u6311\u6218",
        selectedIndex_ == 0 || hoveredIndex_ == 0, pressedIndex_ == 0, true);
    DrawButton(graphics, PauseButtonRect(clientRect, 1), L"\u8fd4\u56de\u4e3b\u9875\u9762",
        selectedIndex_ == 1 || hoveredIndex_ == 1, pressedIndex_ == 1, false);
}
