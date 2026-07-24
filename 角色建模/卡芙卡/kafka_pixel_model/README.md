# 卡芙卡像素模型

全部角色帧均为 64x64 RGBA PNG，透明背景。角色头部中心固定在 x=32，鞋底基线固定在 y=59。

## 待机模型

- `base`：空手四方向待机。
- `katana`：持长刀四方向待机。
- `smg`：持冲锋枪四方向待机。

## 行走动画

- `walk_unarmed/kafka_walk_4dir_4frames.png`：空手行走，4 帧。
- `walk_katana/kafka_katana_walk_4dir_4frames.png`：持长刀行走，4 帧。
- `walk_smg/kafka_smg_walk_4dir_4frames.png`：持冲锋枪行走，4 帧。

## 攻击动画

- `attack_katana/kafka_katana_attack_4dir_5frames.png`：长刀横向挥击，5 帧，第 4 帧命中。
- `attack_smg/kafka_smg_attack_4dir_5frames.png`：冲锋枪射击，5 帧，第 3 帧开火。

图集均为“列=动作帧、行=方向”，方向行顺序为下、左、右、上。每个文件夹同时包含裁切好的独立帧和 2 倍预览图。详细配置见 `kafka_model.json`。
