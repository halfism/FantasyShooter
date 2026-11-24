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
| `ArtSource` | 美术源文件（`Meshes/*.obj` 由脚本生成；`Downloaded/` 是外部资源；`Converted/poly/*.obj` 是留档的转换产物） |
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
- 装饰分五段铺（详见 `AFSArena::BuildArtProps`）：出生视野内 12 件稀疏竖立件 +
  4 件灯柱/水晶、中景 18 簇、中景废墟建筑 7 件、场地边缘 36 簇林墙。
  **俯视视角下只有竖立件能被认出来**，平板状的东西一律看成地面斑块。

### 3. poly.pizza 废墟构件（`Tools/fetch_poly.py` + `Tools/glb2obj.py`）

kenney 那批是卡通方块风，缺"建筑结构"——没有拱门、塔、风车、断柱，场地读不出
"废墟"的意味。这批从 [poly.pizza](https://poly.pizza) 取 `.glb`，补的就是这些。

为什么走 poly.pizza：它的模型文件直接暴露在 `static.poly.pizza/<uuid>.glb` 上，
不需要登录、不走 Google Drive / itch.io（这两个在本机实测全部不通），
下载 ~56KB/s（kenney 只有 12~16KB/s），而且大量模型本来就**没有 UV 没有贴图**，
跟本工程的纯色渲染天然合拍。

| 步骤 | 脚本 | 产物 |
| --- | --- | --- |
| 搜索 + 下载 | `Tools/fetch_poly.py` | `ArtSource/Downloaded/poly/*.glb` + `manifest.json`（授权/作者/tris） |
| 挑模型 | `Tools/poly_select.txt` | 白名单，48 个 |
| 转 OBJ | `Tools/glb2obj.py` | `ArtSource/Converted/poly/*.obj` + `CREDITS.md` |
| 导入 | `Tools/import_poly.py` | `/Game/Art/Props/poly/` + `M_Art_poly` |

```powershell
python Tools\fetch_poly.py        # 173 个候选，约 13 分钟
python Tools\fetch_poly.py --meta # 只补授权/作者（首次用了粗略正则，作者全是 unknown）
python Tools\glb2obj.py           # 按白名单转换；未选中的连 OBJ 都不生成
# 然后用 UnrealEditor-Cmd 跑 import_poly.py（同上面那条命令）
```

这批的关键踩坑：

- **自己解 glb，不走 UE 的 glTF 导入器**。Interchange 导 glb 会连带建材质/贴图资产，
  而贴图在 `-nullrhi` 下拿不到 GPU 资源（见上一节），导了也是白导。
  `glb2obj.py` 只拆 JSON + BIN 两个 chunk，只取 POSITION / NORMAL / 索引。
- **枢轴要归到底面中心**。kenney 的 OBJ 天然满足（基座在 Y=0），glb 不满足；
  一开始把 `min(原始 z)` 当底面（正确是 `min(原始 y)`，glTF 是 Y-up），
  转出来 Z 是 ±87 对称，放进 UE 就一半埋进地里。
- **OBJ 必须有 `vt` 行**。`InterchangeOBJTranslator` 在没有 UV 时会
  `ensure(UVs.IsValidIndex(VertexData.UVIndex))`，导入虽能完成但每次刷一整屏错误栈，
  冒烟测试会当成故障。没有 UV 就按 XY 平面投影生成一份。
- **精度 `%.2f` 就够**（厘米制下 0.1mm）。glTF 的顶点是展开的（每面独立顶点），
  用 `%.6f` 会让这批 OBJ 从 5.5MB 涨到十来 MB。
- **授权要分开记**：这 48 个里 31 个 CC0、17 个 CC-BY 3.0，后者要求署名。
  `glb2obj.py` 会生成 `ArtSource/Converted/poly/CREDITS.md`（名称 / 标题 / 授权 / 作者 / 来源链接），
  和 OBJ 一起入库。
- `.gitignore` 不能直接写 `ArtSource/Converted/`：Git 不会进入被忽略的目录，
  里面的 `!` 例外永远不生效。得逐项排除（`ArtSource/Converted/*`）再逐项放行。

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
