# Side-only v2 图像生成记录

- Imagegen 模式：`generation`
- 用户参考图：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v2/style_sources/user_reference.png`
- 生成后只引用以下项目内绿幕母图；原始临时目录不作为后续依赖。

## 三张源图

1. 角色侧身待机/行走：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v2/style_sources/player_idle_walk_chroma.png`
2. 角色无 VFX 十帧挥棒：`角色建模/穹/first_style_animation_bat/combat_animation/side_only_v2/style_sources/player_attack_chroma.png`
3. 怪物八帧攻击：`角色建模/怪物/selected_monster_knight/side_only_v2/style_sources/monster_attack_chroma.png`

## 实际用途提示词摘要

### 1. 侧身待机/行走（4×2）

16-bit 像素风游戏角色动画母图，沿用参考图中“穹”的银发、黑金服装与木质球棒身份；严格侧视、统一镜头和身体比例、全部朝右。4×2 等分网格，上排 4 帧待机呼吸，下排 4 帧循环行走；动作衔接自然，人物完整、不贴边、无文字、无阴影、无攻击 VFX，纯高饱和绿色背景便于抠图。

### 2. 无 VFX 十帧挥棒（5×2）

同一角色与同一侧视比例的 16-bit 像素动画母图，全部朝右，5×2 等分网格按先上排后下排组成完整 10 帧：预备、蓄力、挥出、命中延伸、随势与收招回待机。木棒和四肢运动轨迹连续，角色身份不漂移；明确禁止挥击弧、蓝光、能量环、粒子、文字和阴影，纯绿幕背景。

### 3. 怪物八帧攻击（4×2）

16-bit 像素风灰色骑士怪物侧视攻击动画母图，保持同一盔甲、体型和长枪身份，全部朝右。4×2 等分网格按先上排后下排组成 8 帧完整刺击：待机、压低重心、蓄力、突刺、最大延伸、回收与恢复；动作重心和脚底连贯，无 VFX、无文字、无阴影，纯绿幕背景。

## 后处理约束

三张母图均先执行 `remove_chroma_key.py --auto-key border --soft-matte --despill`，再用全序列共用缩放、固定身体轴 `(32, 60)` 和脚底基线制作 64×64 RGBA 运行时帧；右向来自母图，左向仅围绕 x=32 镜像。
