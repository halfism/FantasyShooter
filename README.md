# FantasyShooter（异界神射手）

UE 5.8 纯 C++ 肉鸽射击游戏。无蓝图；角色、敌人与场景由**程序化拼装的低模部件**构成，HUD 用 Canvas 绘制。

## 玩法概要

- 三系赐福（烈焰 / 疾影 / 壁垒），同系累计 3 层觉醒，觉醒后该系退出抽签池。
- 9 种敌人、2 个 BOSS：巫师领主（远程弹幕 + 冲锋）与熔铁君王（地面震波 + 可击碎重甲）。
- BOSS 按波次交替出现；重甲可被打穿，破甲后附带踉跄和全场易伤，换阶段会重新锻甲。

## 局外成长：神龛

打一局攒下来的金币会存进神龛，跨局保留，用来买永久强化（标题页 / 阵亡页 / 通关页按 **G** 打开）。
**可用金币 = 累计金币 − 已花费金币**，不另起第二种货币——局内金币本来就在累计，再发一套代币只会让玩家记两个数字。

| 项 | 每级 | 五级合计 | 单级价格 |
| --- | --- | --- | --- |
| 体魄 | +10 最大生命 | +50 | 90 / 180 / 270 / 360 / 450 |
| 锋锐 | +5% 伤害 | +25% | 120 / 240 / 360 / 480 / 600 |
| 疾射 | +4% 射速 | +20% | 120 / 240 / 360 / 480 / 600 |
| 行速 | +3% 移速 | +15% | 100 / 200 / 300 / 400 / 500 |
| 天赐 | +8% 金币 | +40% | 150 / 300 / 450 / 600 / 750 |

点满五项需要 8700 币（约十几个满局）。加成在 `StartGame` 一次性灌进玩家：生命是加算
（面板写 +10 就要能和血条对账），其余三项乘进局内乘区。天赐走 `AddCoins`，因此**也会加快升级**
（金币同时是经验）——这是故意的，否则"贪"这一项就没有代价。

调试开关：`-fscoins=N`（把可用金币抬到 N）、`-fsshrine`（开局弹面板）、`-fsshrinemax`（全点满）、
`-fsshrtest`（在内存里推演 18 条购买/上限/加成断言，绝不落盘；`Tools\_shrine.bat` 一键跑）。

## 开发环境

- 引擎：UE 5.8.4（`G:/Epic/EpicApps/UE_5.8`），工程 `EngineAssociation=5.8`
- 编译器：Visual Studio 2026 / MSVC 14.52
- 已启用官方 MCP 插件（ModelContextProtocol + AllToolsets）

## 目录

| 路径 | 说明 |
| --- | --- |
| `Source/FantasyShooter` | 全部玩法代码 |
| `Config` | 默认引擎 / 游戏 / 输入配置 |
| `Content` | 地图、材质、字体资产 |
| `ArtSource` | 美术源文件（`Meshes/*.obj` 由脚本生成；`Downloaded/*.zip` 是外部 CC0 包） |
| `Tools` | 编译、打包、截图、验证与自动提交脚本 |

## 美术资产

### 1. 程序化部件（`Tools/gen_meshes.py`）

部件网格不是手工资产，而是脚本生成的凸多面体（无第三方依赖）：

| 部件 | 替换的引擎基础体 | 规格 |
| --- | --- | --- |
| `SM_BevelBox` | `Cube` | 倒角立方体，26 面 |
| `SM_Prism24` | `Cylinder` | 24 边棱柱，直径 100 高 100 |
| `SM_IcoSphere` | `Sphere` | 80 面低模球 |
| `SM_Cone8` | `Cone` | 8 边锥 |
| `SM_Rock` | — | 不规则岩块（场景装饰） |
| `SM_Shard` | — | 六棱尖晶（场景装饰） |

**尺寸与枢轴和引擎基础体逐位对齐**，所以替换不影响任何既有摆放。映射只在
`FSUtil::Mesh()` 一处，资产缺失会自动回退到 `BasicShapes`，打包不会变成隐形方块。

```powershell
python Tools\gen_meshes.py     # 生成 ArtSource/Meshes/*.obj
# 导入 /Game/Art/Meshes（无界面，约 10s）
& "G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "G:\Epic\UnrealProjects\FantasyShooter58\FantasyShooter.uproject" `
  -run=pythonscript -script="G:\Epic\UnrealProjects\FantasyShooter58\Tools\import_art.py" `
  -nullrhi -nosplash -nop4 -stdout
```

加 `-fsbasicshapes` 可强制退回引擎基础体，用于新旧观感 A/B 对比
（`Tools\_shot_artpass.bat 0|1`）。

### 2. 外部 CC0 道具（`Tools/fetch_art.py` + `Tools/import_props.py`）

竞技场装饰用的是 [kenney.nl](https://kenney.nl) 的 **CC0** 低模包（可商用、无需署名，
包内附 `License.txt`）。工程里入库的只有原始 zip：

| 包 | 模型数 | 用途 |
| --- | --- | --- |
| `survival-kit` | 80 | 岩石 / 木箱 / 木桶 / 树 / 栅栏 |
| `castle-kit` | 76 | 岩块 / 石阶 / 城墙 / 塔基 / 树 |
| `graveyard-kit` | 91 | 墓碑 / 十字架 / 松树 / 铁栏 / 路灯 |
| `fantasy-town-kit` | 167 | 备用（城墙 / 屋顶 / 摊位） |
| `mini-dungeon` | 30 | 备用（地牢件） |

```powershell
python Tools\fetch_art.py      # 下载 + 解压到 ArtSource/Downloaded（需要网络）
# 转换（Y-up 米制 -> Z-up 厘米制）并导入 /Game/Art/Props（无界面，约 5~8 分钟）
& "G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "G:\Epic\UnrealProjects\FantasyShooter58\FantasyShooter.uproject" `
  -run=pythonscript -script="G:\Epic\UnrealProjects\FantasyShooter58\Tools\import_props.py" `
  -nullrhi -nosplash -nop4 -stdout
```

注意事项（都是踩过的坑）：

- **材质是纯色，不用贴图。** Kenney 的 `colormap.png` 在 `-nullrhi` commandlet 里
  拿不到有效 GPU 资源（CPU 侧 512×512 且内容正确，`GetSizeX()` 恒为 32×32，采出来全黑），
  所以每个包只建一个 `Unlit + VectorParameter("Tint")` 材质，颜色由
  `FSUtil::PropColor()` 按模型名分类决定（植被绿 / 木作棕 / 石作按主题微调 / 金属冷灰）。
- **Y-up 要转 Z-up，米要乘 100**，否则模型躺倒、尺寸差 100 倍。
- **UV 不要翻 V**：Kenney 调色板上半区（v<0.45）是纯黑占位，翻了全部变黑。
- **丢掉 OBJ 的 `g`/`o` 行**：导入器会拿组名当资产名，带连字符的名字和 `Group` 就是这么来的。
- 摆放一律按**目标高度**等比缩放（`FSUtil::AddProp` 的 `TargetHeight`），枢轴在底面；
  全部 `NoCollision`，不挡走位。
- 装饰分三段铺（详见 `AFSArena::BuildArtProps`）：出生视野内 12 件稀疏竖立件、
  中景 18 簇、场地边缘 36 簇林墙。**俯视视角下只有竖立件能被认出来**，
  平板状的东西一律看成地面斑块。

## 构建与运行

```powershell
& "G:\Epic\UnrealProjects\FantasyShooter58\Tools\build_dev.bat"                     # Development 编译
& "G:\Epic\UnrealProjects\FantasyShooter58\Tools\_pack.bat" Development Packaged    # Development 打包
& "G:\Epic\UnrealProjects\FantasyShooter58\Tools\OpenEditor.ps1"                    # 打开编辑器
```

## Git 自动提交

远端：<https://github.com/halfism/FantasyShooter>（`main` 分支）。

每次功能完成后执行：

```powershell
& "G:\Epic\UnrealProjects\FantasyShooter58\Tools\autocommit.ps1" -Message "feat: 功能说明"
```

脚本会 `git add -A` → `git commit` → `git push`；没有改动时直接跳过，不产生空提交。
`Binaries/` `Intermediate/` `Saved/` `DerivedDataCache/` `Build/` 与打包产物均由 `.gitignore` 排除。
