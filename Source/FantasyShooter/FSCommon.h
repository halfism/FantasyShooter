#pragma once

#include "CoreMinimal.h"
#include "FSCommon.generated.h"

/** 武器种类 */
UENUM()
enum class EFSWeapon : uint8
{
	Pistol,
	SMG,
	Shotgun,
	Laser,
	Bow,
	Rocket,
	Count
};

/** 怪物种类 */
UENUM()
enum class EFSEnemyKind : uint8
{
	Slime,
	Runner,
	Grunt,
	Brute,
	Mage,
	Boss,
	Shielder,   // 铁卫：正面来刀只吃三成，逼玩家绕后
	Splitter,   // 裂魂：死亡分裂成两只小史莱姆
	Warlord,    // 熔铁君王：第二种 BOSS。无远程、正面重甲、地面震波
	Tyrant,     // 终焉君王：第三种 BOSS，第 30 波。前两者的合体，三阶段各用一半招式
	Count
};

/** 天气 */
UENUM()
enum class EFSWeather : uint8
{
	Clear,
	Rain,
	Storm,
	BloodMoon
};

/** 游戏阶段 */
UENUM()
enum class EFSPhase : uint8
{
	Title,
	Playing,
	GameOver,
	/** 打完第 30 波：这不是"死"，所以不能复用 GameOver 那套文案与配色 */
	Victory
};

/** 掉落物类型 */
UENUM()
enum class EFSPickupKind : uint8
{
	Weapon,
	Health,
	Rune,
	Coin
};

/**
 * 赐福 / 增益类型。
 * 顺序即 AFSPlayerChar::ApplyUpgrade 的索引，也用于金币升级的固定轮转（只用到前 5 个），
 * 所以不要随手调整顺序或往中间插项；新增项一律追加在 Count 之前。
 */
UENUM()
enum class EFSUpgrade : uint8
{
	Damage,
	FireRate,
	MoveSpeed,
	MaxHP,
	DashCd,
	Pierce,
	Crit,
	Magnet,
	Ward,       // 护壁：受到伤害减免
	Leech,      // 嗜血：击杀回复
	Count
};

/** 赐福稀有度：同一张卡按稀有度放大效果，卡面也跟着变 */
UENUM()
enum class EFSRarity : uint8
{
	Common,     // 寻常
	Rare,       // 稀有
	Epic,       // 传世
	Count
};

/**
 * 赐福流派：同类累计 3 层触发"觉醒"，给一条专属被动。
 * 抽签时会偏向玩家已经在走的流派，形成真正的路线选择。
 */
UENUM()
enum class EFSPath : uint8
{
	Fury,       // 烈焰 · 输出
	Swift,      // 疾影 · 机动
	Ward,       // 壁垒 · 生存
	Count
};

/**
 * 精英词缀：同类兵种之间唯一的变化来源。
 * 不加新兵种、不做新模型，只往已有敌人身上挂一条"必须换个打法"的规则，
 * 让一波 20 只刀兵里出现"先杀那只"的判断。
 */
UENUM()
enum class EFSElite : uint8
{
	None,
	Frenzy,     // 狂暴：更快、更疼，逼你拉开距离
	Thorn,      // 荆棘：贴脸打它会反弹，逼你用远程/换位置
	Regen,      // 再生：不回血就白打，逼你集火
	Volatile,   // 爆裂：死后延迟炸开一圈，逼你杀完就走
	Count
};

/**
 * 流派主动技：一键、充能制。
 *
 * 存在的意义不是"多一个技能"，而是让「流派」第一次体现在手上：
 * 未觉醒时是个保命的工具，觉醒后才变成能解局的招——
 * 三系的差别因此从 HUD 上的数字变成"按下去发生什么"。
 *
 * 顺序必须和 EFSPath 一一对应（None=0，之后 +1），选招时直接按流派下标换算。
 */
UENUM()
enum class EFSAbility : uint8
{
	None,
	Pyre,       // 烈焰·焚场：脚下烧一圈持续伤害，期间自己输出也更高
	SlowTime,   // 疾影·时缓：全场敌人放慢，自己不变——连震波与弹幕都变慢
	Bulwark,    // 壁垒·壁垒降临：护盾吸收伤害，碎时炸成环形爆发
	Count
};

struct FFSWeaponDef
{
	const TCHAR* NameCN = TEXT("");
	const TCHAR* NameEN = TEXT("");
	float Damage = 10.f;
	float Interval = 0.2f;
	float Spread = 2.f;
	float Speed = 5000.f;
	int32 Pellets = 1;
	int32 Pierce = 0;
	int32 AmmoMax = 0;      // 0 = 无限
	float ProjScale = 1.f;
	bool bExplosive = false;
	float ExplodeRadius = 0.f;
	float ExplodeDamage = 0.f;
	FLinearColor Color = FLinearColor::White;
};

struct FFSEnemyDef
{
	const TCHAR* NameCN = TEXT("");
	const TCHAR* NameEN = TEXT("");
	float MaxHP = 30.f;
	float Speed = 300.f;
	float ContactDamage = 8.f;
	float BodyScale = 1.f;
	float Radius = 45.f;
	int32 Coins = 1;
	bool bRanged = false;
	FLinearColor Color = FLinearColor::White;
	FLinearColor Accent = FLinearColor::White;
};

/** 赐福的展示数据：名称、效果说明、所属流派、卡片配色 */
struct FFSUpgradeDef
{
	const TCHAR* NameCN = TEXT("");
	const TCHAR* NameEN = TEXT("");
	/** 基础效果文案（寻常档）。稀有 / 传世会在末尾追加 ×倍率 标注 */
	const TCHAR* DescCN = TEXT("");
	const TCHAR* DescEN = TEXT("");
	FLinearColor Color = FLinearColor::White;
	EFSPath Path = EFSPath::Fury;
};

namespace FSDefs
{
	const FFSWeaponDef& Weapon(EFSWeapon W);
	const FFSEnemyDef& Enemy(EFSEnemyKind K);
	const FFSUpgradeDef& Upgrade(EFSUpgrade U);
	int32 UpgradeCount();
	const TCHAR* WeatherNameCN(EFSWeather W);
	const TCHAR* WeatherNameEN(EFSWeather W);

	/** 稀有度带来的效果倍率：1.0 / 1.6 / 2.4 */
	float RarityMul(EFSRarity R);
	const TCHAR* RarityNameCN(EFSRarity R);
	const TCHAR* RarityNameEN(EFSRarity R);
	FLinearColor RarityColor(EFSRarity R);

	EFSPath PathOf(EFSUpgrade U);
	const TCHAR* PathNameCN(EFSPath P);
	const TCHAR* PathNameEN(EFSPath P);
	/** 紧凑名（无字间空格）：卡片底栏与 HUD 流派条用，避免和带空格的大标题写法混用 */
	const TCHAR* PathShortCN(EFSPath P);
	const TCHAR* PathShortEN(EFSPath P);
	FLinearColor PathColor(EFSPath P);
	/**
	 * 特效用的流派色：比 PathColor 深一档。
	 * PathColor 是给 HUD 当文字色的，浅而亮才好读；可同一个值铺成大面积自发光时，
	 * 线性→sRGB 会把它抬成奶油色（烈焰裂地实测采到 (223,181,162)，不像火）。
	 * 大块特效一律走这个深色版本，"橙 / 青 / 绿"三个色相才站得住。
	 */
	FLinearColor PathFxColor(EFSPath P);

	/** 觉醒后的专属被动名：狂怒 / 残影 / 不屈 */
	const TCHAR* PathAwakenNameCN(EFSPath P);
	const TCHAR* PathAwakenNameEN(EFSPath P);

	/** 流派觉醒需要累计的层数 */
	static constexpr int32 PathAwakenStacks = 3;

	// ---- 流派主动技
	/** 主动技所属的流派（None 返回 Fury 占位，调用方不该对 None 取色） */
	EFSPath AbilityPath(EFSAbility A);
	/** 由流派下标直接换算成主动技（0/1/2 → Pyre/SlowTime/Bulwark） */
	EFSAbility AbilityOfPath(EFSPath P);
	const TCHAR* AbilityNameCN(EFSAbility A);
	const TCHAR* AbilityNameEN(EFSAbility A);
	/** 未觉醒形态的一句话说明（HUD 悬停 / 日志用） */
	const TCHAR* AbilityDescCN(EFSAbility A, bool bAwakened);
	const TCHAR* AbilityDescEN(EFSAbility A, bool bAwakened);

	// ---- 精英词缀
	const TCHAR* EliteNameCN(EFSElite E);
	const TCHAR* EliteNameEN(EFSElite E);
	/** 词缀说明（卡面无用，留给 HUD / 日志与调试阅读） */
	const TCHAR* EliteDescCN(EFSElite E);
	const TCHAR* EliteDescEN(EFSElite E);
	FLinearColor EliteColor(EFSElite E);
	/** 词缀色 + 白色混合的"高亮版"，画外环用，避免纯色在暗底上发闷 */
	FLinearColor EliteRingColor(EFSElite E);

	/**
	 * 按稀有度算出实际效果文案（表里的 DescCN/DescEN 是寻常档的固定串）。
	 * 公式必须和 AFSPlayerChar::ApplyUpgrade 保持一致，否则卡面会骗人。
	 */
	FString UpgradeDescCN(EFSUpgrade U, EFSRarity R);
	FString UpgradeDescEN(EFSUpgrade U, EFSRarity R);
}

namespace FSPal
{
	const FLinearColor Ground       = FLinearColor(0.045f, 0.055f, 0.075f);
	const FLinearColor GroundLine   = FLinearColor(0.10f, 0.13f, 0.19f);
	const FLinearColor Wall         = FLinearColor(0.16f, 0.19f, 0.27f);
	const FLinearColor PropA        = FLinearColor(0.10f, 0.12f, 0.17f);
	const FLinearColor PropB        = FLinearColor(0.13f, 0.15f, 0.21f);
	const FLinearColor Skin         = FLinearColor(0.93f, 0.87f, 0.78f);
	const FLinearColor Cloth        = FLinearColor(0.18f, 0.62f, 0.92f);
	const FLinearColor ClothDark    = FLinearColor(0.11f, 0.38f, 0.60f);
	const FLinearColor Cyan         = FLinearColor(0.25f, 0.90f, 1.00f);
	const FLinearColor Gold         = FLinearColor(1.00f, 0.80f, 0.25f);
	const FLinearColor Danger       = FLinearColor(1.00f, 0.28f, 0.30f);
	const FLinearColor Green        = FLinearColor(0.35f, 0.95f, 0.50f);
	const FLinearColor Purple       = FLinearColor(0.62f, 0.36f, 0.95f);
	const FLinearColor Magic        = FLinearColor(0.70f, 0.45f, 1.00f);
	const FLinearColor White        = FLinearColor(1.f, 1.f, 1.f);
	const FLinearColor Dim          = FLinearColor(0.65f, 0.70f, 0.80f);
}
