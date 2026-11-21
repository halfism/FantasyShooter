using UnrealBuildTool;
using System.Collections.Generic;

public class FantasyShooterTarget : TargetRules
{
	public FantasyShooterTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		// 注意：不要打开 bUseLoggingInShipping。
		// 引擎是安装版，Shipping 的 Core 库编译时就关掉了日志，
		// 一旦在 Game 目标打开它，模块里的 UE_LOG(LogTemp,...) 会变成
		// 无法解析的外部符号 LogTemp（LNK2001），Shipping 直接链接失败。
		// Shipping 的行为验证改为"计划任务启动 + 外部截屏"，不依赖日志。
		ExtraModuleNames.Add("FantasyShooter");
	}
}
