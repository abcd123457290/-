# Side-only v3 图像生成记录

- Imagegen 模式：`generation`
- 身份参考：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v3/style_sources/user_reference.png`
- 运行时人物：96×96 RGBA，pivot `(48, 90)`
- 终结技图标：64×64 RGBA

## 项目内母图

1. Idle 原始 4×2 母图：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v3/style_sources/qiong_idle_source_4x2_chroma.png`。仅使用上排 idle 4 帧；下排旧 walk4 因球棒拖地被明确弃用。
2. 最终修正版 walk8 母图：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v3/style_sources/qiong_walk_8frames_chroma.png`。4×2、8 帧，双手在身体前方持棒，球棒朝右前方斜上，完全不越肩背且全程离地。
3. Attack10 母图：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v3/style_sources/qiong_attack_10frames_chroma.png`。5×2、10 帧，无挥击 VFX。
4. Death8 母图：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v3/style_sources/qiong_death_8frames_chroma.png`。4×2、8 帧，从受击失衡、倒地到侧躺静止，无血腥内容。
5. 终结技图标母图：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v3/style_sources/qiong_finisher_icon_chroma.png`。单图超级球棒蓝色能量图标。

## 实际用途提示词摘要

### Idle 4

高精细 16-bit 像素风侧视角色，固定灰发、黑金长外套与黑金木棒身份，全部朝右；上排四帧自然待机呼吸，人物比例、镜头、脚底和身体轴一致，纯绿色无阴影背景。

### Walk 8（拖棒问题修正）

同一角色的 4×2 八帧侧视行走循环，全部朝右；步态完整连贯，双手清晰可见并在身体胸腰前方共同握住球棒，球棒朝角色右前方斜向上，整根球棒完全不越过肩部或身体背面，棒头与地面保持清晰间隙；严禁单手背持、搭在肩背后、拖地、落地或产生地面刮擦；身份、占格比例和纯绿幕背景保持一致。

### Attack 10

同一角色的 5×2 十帧基础挥棒动画，按上排后下排依次为预备、蓄力、挥出、最大延伸、随势与回待机；全部朝右，无能量弧、粒子、文字、阴影或其他 VFX，纯绿色背景。

### Death 8

同一角色的 4×2 八帧非血腥死亡动画，按上排后下排依次为受击后仰、腿部失力、伸手撑地、倒向地面、侧卧缓冲、身体下沉、闭眼停稳与最终静止；球棒自然脱手落在身边，全部朝右，动作连续，纯绿色无阴影背景。

### 终结技技能图标

64×64 游戏 HUD 用像素图标母图：黑金超级球棒被明亮霓虹蓝能量环和冲击形状包围，轮廓紧凑、无文字、无 UI 边框、纯绿色背景；缩到 32×32 仍能识别球棒与蓝色终结技能量。

## 后处理

每张母图先运行 `remove_chroma_key.py --auto-key border --soft-matte --transparent-threshold 12 --opaque-threshold 220 --despill`。每张动画母图只使用一个固定等比缩放；不做逐帧缩放或非等比拉伸。右向为母图，左向绕 x=48 精确镜像。
