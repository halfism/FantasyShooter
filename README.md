# FantasyShooter（异界神射手）

UE 5.8 纯 C++ 肉鸽射击游戏。无蓝图，场景与模型全部程序化生成，HUD 用 Canvas 绘制。

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
| `ArtSource` | 美术源文件 |
| `Tools` | 编译、打包、截图、验证与自动提交脚本 |

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
