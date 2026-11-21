<#
  OpenEditor.ps1 —— 一键打开 FantasyShooter 的虚幻编辑器
  走 Windows 计划任务启动，因此编辑器的父进程是 Task Scheduler 服务，
  不会在 Agent 命令结束时被连带回收。默认参数已固定为本工程路径。

  用法：
    .\Tools\OpenEditor.ps1                 # 打开本工程
    .\Tools\OpenEditor.ps1 -WaitSeconds 20 # 多等一会儿再报告（首次加载慢时用）
#>

param(
    [string]$ProjectFile = "G:\Epic\UnrealProjects\FantasyShooter58\FantasyShooter.uproject",
    [string]$EditorExe   = "G:\Epic\EpicApps\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [int]$WaitSeconds = 10
)

$ErrorActionPreference = "Stop"
$launcher = Join-Path $env:USERPROFILE ".workbuddy\tools\Start-Detached.ps1"

if (-not (Test-Path -LiteralPath $launcher)) { throw "缺少启动器: $launcher" }
if (-not (Test-Path -LiteralPath $ProjectFile)) { throw "找不到工程: $ProjectFile" }
if (-not (Test-Path -LiteralPath $EditorExe))  { throw "找不到编辑器: $EditorExe" }

# 已有实例就只报告，避免重复打开同一工程
$running = Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowTitle -like "*FantasyShooter*" }
if ($running) {
    $running | ForEach-Object {
        "编辑器已在运行: pid=$($_.Id)  内存=$([math]::Round($_.WorkingSet64/1MB))MB  标题=$($_.MainWindowTitle)"
    }
    return
}

& $launcher -FilePath $EditorExe -Arguments "`"$ProjectFile`"" -WaitSeconds $WaitSeconds

Start-Sleep -Seconds 2
$p = Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if ($p) {
    "窗口已就绪: pid=$($p.Id)  句柄=$($p.MainWindowHandle)  标题=$($p.MainWindowTitle)"
} else {
    "已提交启动，窗口尚未出现（首次加载通常需要 20~60 秒）"
}
