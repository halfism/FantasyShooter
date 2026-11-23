# FantasyShooter（异界神射手）

UE 5.8 纯 C++ 肉鸽射击游戏。无蓝图；角色、敌人与场景由**程序化拼装的低模部件**构成，HUD 用 Canvas 绘制。

## 玩法概要

- 三系赐福（烈焰 / 疾影 / 壁垒），同系累计 3 层觉醒，觉醒后该系退出抽签池。
- 9 种敌人、2 个 BOSS：巫师领主（远程弹幕 + 冲锋）与熔铁君王（地面震波 + 可击碎重甲）。
- BOSS 按波次交替出现；重甲可被打穿，破甲后附带踉跄和全场易伤，换阶段会重新锻甲。

## 开发环境

- 引擎：UE 5.8.4（`G:/Epic/EpicApps/UE_5.8`），工程 `EngineAssociation=5.8`
- 编译器：Visual Studio 2026 / MSVC 14.52
- 已启用官方 MCP 插件（ModelContextProtocol + AllToolsets）

## 目录

| 路径 | 说明 |
| --- | --- |
| `Source/FantasyShooter` | 全部玩法代码 |
| `Config` | 默认引擎 / 游戏 / 输入配置 |
| `Content` | 地图、材质、贴图、字体资产 |
| `ArtSource` | 美术源文件（`Meshes/*.obj` 由脚本生成，可直接 diff） |
| `Tools` | 编译、打包、截图、验证与自动提交脚本 |

## 美术资产

部件网格不是手工资产，而是脚本生成的凸多面体（`Tools/gen_meshes.py`，无第三方依赖）：

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
