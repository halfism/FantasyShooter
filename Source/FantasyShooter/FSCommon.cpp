#include "FSCommon.h"

namespace FSDefs
{
	static FFSWeaponDef MakeWeapon(
		const TCHAR* CN, const TCHAR* EN, float Dmg, float Interval, float Spread, float Speed,
		int32 Pellets, int32 Pierce, int32 Ammo, float ProjScale, FLinearColor Color,
		bool bExplosive = false, float ExpRadius = 0.f)
	{
		FFSWeaponDef D;
		D.NameCN = CN; D.NameEN = EN; D.Damage = Dmg; D.Interval = Interval; D.Spread = Spread;
		D.Speed = Speed; D.Pellets = Pellets; D.Pierce = Pierce; D.AmmoMax = Ammo;
		D.ProjScale = ProjScale; D.Color = Color; D.bExplosive = bExplosive; D.ExplodeRadius = ExpRadius;
		D.ExplodeDamage = Dmg * 0.8f;
		return D;
	}

	const FFSWeaponDef& Weapon(EFSWeapon W)
	{
		static TArray<FFSWeaponDef> Table;
		if (Table.Num() == 0)
		{
			Table.SetNum((int32)EFSWeapon::Count);
			//                     CN          EN          DMG  INT   SPR  SPEED  P   PRC  AMMO  SCALE  COLOR
			Table[0] = MakeWeapon(TEXT("短枪"), TEXT("PISTOL"), 15.f, 0.17f, 2.0f, 5200.f, 1, 0, 0, 1.0f, FLinearColor(1.00f, 0.92f, 0.45f));
			Table[1] = MakeWeapon(TEXT("速射"), TEXT("SMG"), 9.f, 0.065f, 6.0f, 5400.f, 1, 0, 260, 0.8f, FLinearColor(0.45f, 0.95f, 1.00f));
			Table[2] = MakeWeapon(TEXT("霰弹"), TEXT("SHOTGUN"), 11.f, 0.52f, 13.f, 4300.f, 7, 0, 70, 1.1f, FLinearColor(1.00f, 0.60f, 0.20f));
			Table[3] = MakeWeapon(TEXT("激光"), TEXT("LASER"), 30.f, 0.24f, 0.5f, 14000.f, 1, 3, 130, 0.7f, FLinearColor(1.00f, 0.35f, 0.95f));
			Table[4] = MakeWeapon(TEXT("猎弓"), TEXT("HUNTER BOW"), 38.f, 0.40f, 1.0f, 7000.f, 1, 6, 90, 1.0f, FLinearColor(0.45f, 1.00f, 0.55f));
			Table[5] = MakeWeapon(TEXT("火箭"), TEXT("ROCKET"), 70.f, 0.90f, 1.5f, 2800.f, 1, 0, 34, 1.8f, FLinearColor(1.00f, 0.35f, 0.28f), true, 420.f);
		}
		int32 I = FMath::Clamp((int32)W, 0, (int32)EFSWeapon::Count - 1);
		return Table[I];
	}

	const FFSEnemyDef& Enemy(EFSEnemyKind K)
	{
		static TArray<FFSEnemyDef> Table;
		if (Table.Num() == 0)
		{
			Table.SetNum((int32)EFSEnemyKind::Count);
			{
				FFSEnemyDef D;
				D.NameCN = TEXT("史莱姆"); D.NameEN = TEXT("SLIME");
				D.MaxHP = 34.f; D.Speed = 250.f; D.ContactDamage = 8.f; D.BodyScale = 1.0f;
				D.Radius = 42.f; D.Coins = 1;
				D.Color = FLinearColor(0.30f, 0.85f, 0.42f); D.Accent = FLinearColor(0.85f, 1.00f, 0.45f);
				Table[0] = D;
			}
			{
				FFSEnemyDef D;
				D.NameCN = TEXT("疾行鬼"); D.NameEN = TEXT("RUNNER");
				D.MaxHP = 26.f; D.Speed = 520.f; D.ContactDamage = 11.f; D.BodyScale = 0.85f;
				D.Radius = 36.f; D.Coins = 1;
				D.Color = FLinearColor(1.00f, 0.82f, 0.25f); D.Accent = FLinearColor(1.00f, 0.45f, 0.15f);
				Table[1] = D;
			}
			{
				FFSEnemyDef D;
				D.NameCN = TEXT("刀兵"); D.NameEN = TEXT("GRUNT");
				D.MaxHP = 72.f; D.Speed = 300.f; D.ContactDamage = 15.f; D.BodyScale = 1.15f;
				D.Radius = 48.f; D.Coins = 2;
				D.Color = FLinearColor(0.92f, 0.32f, 0.32f); D.Accent = FLinearColor(1.00f, 0.75f, 0.35f);
				Table[2] = D;
			}
			{
				FFSEnemyDef D;
				D.NameCN = TEXT("巨兽"); D.NameEN = TEXT("BRUTE");
				D.MaxHP = 260.f; D.Speed = 210.f; D.ContactDamage = 26.f; D.BodyScale = 2.1f;
				D.Radius = 78.f; D.Coins = 6;
				D.Color = FLinearColor(0.60f, 0.38f, 0.92f); D.Accent = FLinearColor(0.95f, 0.85f, 1.00f);
				Table[3] = D;
			}
			{
				FFSEnemyDef D;
				D.NameCN = TEXT("术士"); D.NameEN = TEXT("WARLOCK");
				D.MaxHP = 58.f; D.Speed = 230.f; D.ContactDamage = 10.f; D.BodyScale = 1.05f;
				D.Radius = 46.f; D.Coins = 3; D.bRanged = true;
				D.Color = FLinearColor(0.35f, 0.85f, 0.95f); D.Accent = FLinearColor(0.95f, 0.95f, 1.00f);
				Table[4] = D;
			}
			{
				FFSEnemyDef D;
				D.NameCN = TEXT("巫师领主"); D.NameEN = TEXT("ARCHMAGE");
				D.MaxHP = 1600.f; D.Speed = 200.f; D.ContactDamage = 34.f; D.BodyScale = 3.0f;
				D.Radius = 130.f; D.Coins = 60; D.bRanged = true;
				D.Color = FLinearColor(0.90f, 0.20f, 0.55f); D.Accent = FLinearColor(1.00f, 0.90f, 0.45f);
				Table[5] = D;
			}
			{
				// 铁卫：血厚、跑得慢、正面硬。玩家的最优解从"对着打"变成"绕过去打"
				FFSEnemyDef D;
				D.NameCN = TEXT("铁卫"); D.NameEN = TEXT("SHIELDER");
				D.MaxHP = 150.f; D.Speed = 235.f; D.ContactDamage = 18.f; D.BodyScale = 1.25f;
				D.Radius = 54.f; D.Coins = 3;
				D.Color = FLinearColor(0.52f, 0.60f, 0.72f); D.Accent = FLinearColor(0.95f, 0.78f, 0.30f);
				Table[6] = D;
			}
			{
				// 裂魂：单只不强，但死后分裂。逼玩家在"先点谁"上做判断
				FFSEnemyDef D;
				D.NameCN = TEXT("裂魂"); D.NameEN = TEXT("SPLITTER");
				D.MaxHP = 96.f; D.Speed = 285.f; D.ContactDamage = 14.f; D.BodyScale = 1.10f;
				D.Radius = 50.f; D.Coins = 2;
				D.Color = FLinearColor(0.62f, 0.45f, 0.95f); D.Accent = FLinearColor(0.70f, 1.00f, 0.78f);
				Table[7] = D;
			}
			{
				// 熔铁君王：和巫师领主刻意相反——不远程、不闪避、不拉扯，
				// 只有正面重甲和地面震波。巫师逼你"保持距离躲弹幕"，君王逼你"贴上去但必须绕背"。
				FFSEnemyDef D;
				D.NameCN = TEXT("熔铁君王"); D.NameEN = TEXT("IRON MONARCH");
				D.MaxHP = 2150.f; D.Speed = 178.f; D.ContactDamage = 40.f; D.BodyScale = 3.2f;
				D.Radius = 146.f; D.Coins = 75;
				D.Color = FLinearColor(0.36f, 0.31f, 0.29f); D.Accent = FLinearColor(1.00f, 0.42f, 0.12f);
				Table[8] = D;
			}
			{
				// 终焉君王：前两者的合体，第 30 波。
				// 数值只比 30 波的普通 BOSS 高一档（血量 +21%），真正加的是"招式组合"——
				// 一阶段考甲与弹幕，二阶段考震波与冲锋，三阶段同时考两套解法。
				// 血再厚就变成磨了，而这一战该死在"同时处理两件事"上，不是死在血量条上。
				FFSEnemyDef D;
				D.NameCN = TEXT("终焉君王"); D.NameEN = TEXT("LAST SOVEREIGN");
				D.MaxHP = 2600.f; D.Speed = 196.f; D.ContactDamage = 42.f; D.BodyScale = 3.3f;
				D.Radius = 152.f; D.Coins = 220; D.bRanged = true;
				D.Color = FLinearColor(0.30f, 0.16f, 0.34f); D.Accent = FLinearColor(1.00f, 0.55f, 0.25f);
				Table[9] = D;
			}
		}
		int32 I = FMath::Clamp((int32)K, 0, (int32)EFSEnemyKind::Count - 1);
		return Table[I];
	}

	const FFSUpgradeDef& Upgrade(EFSUpgrade U)
	{
		static TArray<FFSUpgradeDef> Table;
		if (Table.Num() == 0)
		{
			auto Mk = [](const TCHAR* CN, const TCHAR* EN, const TCHAR* DCN, const TCHAR* DEN,
				FLinearColor C, EFSPath Pth)
			{
				FFSUpgradeDef D;
				D.NameCN = CN; D.NameEN = EN; D.DescCN = DCN; D.DescEN = DEN;
				D.Color = C; D.Path = Pth;
				return D;
			};

			Table.SetNum((int32)EFSUpgrade::Count);
			Table[0] = Mk(TEXT("锋  刃"), TEXT("BLADE"), TEXT("伤害 +14%"), TEXT("DAMAGE +14%"),
				FLinearColor(1.00f, 0.36f, 0.30f), EFSPath::Fury);
			Table[1] = Mk(TEXT("疾  射"), TEXT("HAIL OF SHOT"), TEXT("射速 +10%"), TEXT("FIRE RATE +10%"),
				FLinearColor(0.38f, 0.90f, 1.00f), EFSPath::Fury);
			Table[2] = Mk(TEXT("疾  风"), TEXT("SWIFT"), TEXT("移速 +8%"), TEXT("MOVE SPEED +8%"),
				FLinearColor(0.40f, 0.95f, 0.56f), EFSPath::Swift);
			Table[3] = Mk(TEXT("铁  躯"), TEXT("IRON HIDE"), TEXT("生命上限 +18，并立即回复"), TEXT("MAX HP +18 AND HEAL"),
				FLinearColor(1.00f, 0.46f, 0.62f), EFSPath::Ward);
			Table[4] = Mk(TEXT("幻  步"), TEXT("PHANTOM STEP"), TEXT("闪避冷却 -12%"), TEXT("DASH COOLDOWN -12%"),
				FLinearColor(0.58f, 0.64f, 1.00f), EFSPath::Swift);
			Table[5] = Mk(TEXT("穿  透"), TEXT("PIERCING"), TEXT("子弹多穿透 1 个敌人"), TEXT("BULLETS PIERCE +1"),
				FLinearColor(1.00f, 0.72f, 0.26f), EFSPath::Fury);
			Table[6] = Mk(TEXT("致  命"), TEXT("LETHAL"), TEXT("+10% 概率打出 2.2 倍伤害"), TEXT("+10% CHANCE OF 2.2x DAMAGE"),
				FLinearColor(1.00f, 0.86f, 0.22f), EFSPath::Fury);
			Table[7] = Mk(TEXT("汲  取"), TEXT("GRAVITY WELL"), TEXT("拾取范围 +40%，掉落更快飞来"),
				TEXT("PICKUP RANGE +40%, FASTER PULL"), FLinearColor(0.74f, 0.52f, 1.00f), EFSPath::Swift);
			Table[8] = Mk(TEXT("护  壁"), TEXT("BULWARK"), TEXT("受到伤害 -8%"), TEXT("DAMAGE TAKEN -8%"),
				FLinearColor(0.36f, 0.88f, 0.74f), EFSPath::Ward);
			Table[9] = Mk(TEXT("嗜  血"), TEXT("BLOODTHIRST"), TEXT("每次击杀回复 1.5 生命"), TEXT("HEAL 1.5 ON KILL"),
				FLinearColor(0.92f, 0.26f, 0.42f), EFSPath::Ward);
		}
		const int32 I = FMath::Clamp((int32)U, 0, (int32)EFSUpgrade::Count - 1);
		return Table[I];
	}

	int32 UpgradeCount()
	{
		return (int32)EFSUpgrade::Count;
	}

	float RarityMul(EFSRarity R)
	{
		switch (R)
		{
		case EFSRarity::Rare: return 1.6f;
		case EFSRarity::Epic: return 2.4f;
		default:              return 1.0f;
		}
	}

	const TCHAR* RarityNameCN(EFSRarity R)
	{
		switch (R)
		{
		case EFSRarity::Rare: return TEXT("稀  有");
		case EFSRarity::Epic: return TEXT("传  世");
		default:              return TEXT("寻  常");
		}
	}

	const TCHAR* RarityNameEN(EFSRarity R)
	{
		switch (R)
		{
		case EFSRarity::Rare: return TEXT("RARE");
		case EFSRarity::Epic: return TEXT("RELIC");
		default:              return TEXT("COMMON");
		}
	}

	FLinearColor RarityColor(EFSRarity R)
	{
		switch (R)
		{
		case EFSRarity::Rare: return FLinearColor(0.36f, 0.74f, 1.00f);
		case EFSRarity::Epic: return FLinearColor(1.00f, 0.74f, 0.26f);
		default:              return FLinearColor(0.66f, 0.72f, 0.80f);
		}
	}

	EFSPath PathOf(EFSUpgrade U)
	{
		return Upgrade(U).Path;
	}

	const TCHAR* PathNameCN(EFSPath P)
	{
		switch (P)
		{
		case EFSPath::Swift: return TEXT("疾  影");
		case EFSPath::Ward:  return TEXT("壁  垒");
		default:             return TEXT("烈  焰");
		}
	}

	const TCHAR* PathNameEN(EFSPath P)
	{
		switch (P)
		{
		case EFSPath::Swift: return TEXT("SWIFT");
		case EFSPath::Ward:  return TEXT("WARD");
		default:             return TEXT("FURY");
		}
	}

	FLinearColor PathColor(EFSPath P)
	{
		switch (P)
		{
		case EFSPath::Swift: return FLinearColor(0.35f, 0.92f, 0.90f);
		case EFSPath::Ward:  return FLinearColor(0.44f, 0.92f, 0.52f);
		default:             return FLinearColor(1.00f, 0.42f, 0.30f);
		}
	}

	FLinearColor PathFxColor(EFSPath P)
	{
		switch (P)
		{
		case EFSPath::Swift: return FLinearColor(0.30f, 0.95f, 1.00f);
		case EFSPath::Ward:  return FLinearColor(0.16f, 0.85f, 0.38f);
		default:             return FLinearColor(1.00f, 0.20f, 0.04f);
		}
	}

	const TCHAR* PathShortCN(EFSPath P)
	{
		switch (P)
		{
		case EFSPath::Swift: return TEXT("疾影");
		case EFSPath::Ward:  return TEXT("壁垒");
		default:             return TEXT("烈焰");
		}
	}

	const TCHAR* PathShortEN(EFSPath P)
	{
		switch (P)
		{
		case EFSPath::Swift: return TEXT("SWIFT");
		case EFSPath::Ward:  return TEXT("WARD");
		default:             return TEXT("FURY");
		}
	}

	const TCHAR* PathAwakenNameCN(EFSPath P)
	{
		switch (P)
		{
		case EFSPath::Swift: return TEXT("残影");
		case EFSPath::Ward:  return TEXT("不屈");
		default:             return TEXT("狂怒");
		}
	}

	const TCHAR* PathAwakenNameEN(EFSPath P)
	{
		switch (P)
		{
		case EFSPath::Swift: return TEXT("AFTERIMAGE");
		case EFSPath::Ward:  return TEXT("UNDYING");
		default:             return TEXT("FRENZY");
		}
	}

	// ------------------------------------------------------------------ 流派主动技

	EFSAbility AbilityOfPath(EFSPath P)
	{
		// 枚举顺序刻意对齐：Pyre/SlowTime/Bulwark 紧跟在 None 后面
		const int32 I = (int32)P;
		if (I < 0 || I >= (int32)EFSPath::Count)
		{
			return EFSAbility::None;
		}
		return (EFSAbility)(I + 1);
	}

	EFSPath AbilityPath(EFSAbility A)
	{
		const int32 I = (int32)A - 1;
		if (I < 0 || I >= (int32)EFSPath::Count)
		{
			return EFSPath::Fury;
		}
		return (EFSPath)I;
	}

	const TCHAR* AbilityNameCN(EFSAbility A)
	{
		switch (A)
		{
		case EFSAbility::Pyre:     return TEXT("焚  场");
		case EFSAbility::SlowTime: return TEXT("时  缓");
		case EFSAbility::Bulwark:  return TEXT("壁 垒 降 临");
		default:                   return TEXT("未 定");
		}
	}

	const TCHAR* AbilityNameEN(EFSAbility A)
	{
		switch (A)
		{
		case EFSAbility::Pyre:     return TEXT("PYRE");
		case EFSAbility::SlowTime: return TEXT("SLOW TIME");
		case EFSAbility::Bulwark:  return TEXT("BULWARK");
		default:                   return TEXT("UNBOUND");
		}
	}

	const TCHAR* AbilityDescCN(EFSAbility A, bool bAwakened)
	{
		switch (A)
		{
		case EFSAbility::Pyre:
			return bAwakened
				? TEXT("脚下焚场更大更烫，期间自身伤害 +15%")
				: TEXT("脚下烧出焚场，期间自身伤害 +10%");
		case EFSAbility::SlowTime:
			return bAwakened
				? TEXT("敌人 0.24 倍速 3.2 秒，期间自己射速 +20%")
				: TEXT("敌人 0.32 倍速 2.4 秒，自己不受影响");
		case EFSAbility::Bulwark:
			return bAwakened
				? TEXT("护盾吸 72 点，破碎时炸开一圈")
				: TEXT("护盾吸 45 点，破碎时炸开一圈");
		default:
			return TEXT("先选一条流派，才会得到对应的主动技");
		}
	}

	const TCHAR* AbilityDescEN(EFSAbility A, bool bAwakened)
	{
		switch (A)
		{
		case EFSAbility::Pyre:
			return bAwakened
				? TEXT("Bigger, hotter pyre - +15% damage while burning")
				: TEXT("Burn the ground - +10% damage while it lasts");
		case EFSAbility::SlowTime:
			return bAwakened
				? TEXT("Enemies at 0.24x for 3.2s - your fire rate +20%")
				: TEXT("Enemies at 0.32x for 2.4s - you are unaffected");
		case EFSAbility::Bulwark:
			return bAwakened
				? TEXT("Shield absorbs 72, bursts when broken")
				: TEXT("Shield absorbs 45, bursts when broken");
		default:
			return TEXT("Pick a path first to bind an art");
		}
	}

	// ------------------------------------------------------------------ 精英词缀

	const TCHAR* EliteNameCN(EFSElite E)
	{
		switch (E)
		{
		case EFSElite::Frenzy:   return TEXT("狂  暴");
		case EFSElite::Thorn:    return TEXT("荆  棘");
		case EFSElite::Regen:    return TEXT("再  生");
		case EFSElite::Volatile: return TEXT("爆  裂");
		default:                 return TEXT("");
		}
	}

	const TCHAR* EliteNameEN(EFSElite E)
	{
		switch (E)
		{
		case EFSElite::Frenzy:   return TEXT("FRENZIED");
		case EFSElite::Thorn:    return TEXT("THORNS");
		case EFSElite::Regen:    return TEXT("REGENERATING");
		case EFSElite::Volatile: return TEXT("VOLATILE");
		default:                 return TEXT("");
		}
	}

	const TCHAR* EliteDescCN(EFSElite E)
	{
		switch (E)
		{
		case EFSElite::Frenzy:   return TEXT("移动与攻击都更快，贴上去会吃亏");
		case EFSElite::Thorn:    return TEXT("近身打它会反弹刺伤，迫使你保持距离");
		case EFSElite::Regen:    return TEXT("持续回血，不集火就等于白打");
		case EFSElite::Volatile: return TEXT("死后延迟炸开一圈，杀完必须立刻离开");
		default:                 return TEXT("");
		}
	}

	const TCHAR* EliteDescEN(EFSElite E)
	{
		switch (E)
		{
		case EFSElite::Frenzy:   return TEXT("FASTER MOVEMENT AND ATTACKS - KEEP YOUR DISTANCE");
		case EFSElite::Thorn:    return TEXT("MELEE RANGE HITS REFLECT DAMAGE BACK AT YOU");
		case EFSElite::Regen:    return TEXT("HEALS OVER TIME - FOCUS IT DOWN OR WASTE YOUR DAMAGE");
		case EFSElite::Volatile: return TEXT("BLASTS A RING ON DEATH - MOVE AWAY AFTER THE KILL");
		default:                 return TEXT("");
		}
	}

	FLinearColor EliteColor(EFSElite E)
	{
		switch (E)
		{
		case EFSElite::Frenzy:   return FLinearColor(1.00f, 0.13f, 0.09f);
		case EFSElite::Thorn:    return FLinearColor(0.72f, 0.18f, 1.00f);
		case EFSElite::Regen:    return FLinearColor(0.16f, 1.00f, 0.38f);
		case EFSElite::Volatile: return FLinearColor(1.00f, 0.60f, 0.04f);
		default:                 return FLinearColor(0.80f, 0.85f, 0.95f);
		}
	}

	FLinearColor EliteRingColor(EFSElite E)
	{
		// 一点都不掺白：暗场泛光本来就强，掺白就等于把整圈推向"白灯泡"，
		// "颜色 = 词缀"这层信息立刻丢失。亮度交给自发光强度控制，色相保持纯的。
		return EliteColor(E);
	}

	namespace
	{
		/** 带正负号的百分比文案。格式串必须字面量：UE5.7 的 Printf 会做编译期格式校验 */
		FString SignedPct(float V, bool bMinus)
		{
			const int32 P = FMath::RoundToInt(FMath::Abs(V) * 100.f);
			return bMinus
				? FString::Printf(TEXT("-%d%%"), P)
				: FString::Printf(TEXT("+%d%%"), P);
		}
	}

	FString UpgradeDescCN(EFSUpgrade U, EFSRarity R)
	{
		const float M = RarityMul(R);
		switch (U)
		{
		case EFSUpgrade::Damage:
			return FString::Printf(TEXT("伤害 %s"), *SignedPct(0.14f * M, false));
		case EFSUpgrade::FireRate:
			return FString::Printf(TEXT("射速 %s"), *SignedPct(0.10f * M, false));
		case EFSUpgrade::MoveSpeed:
			return FString::Printf(TEXT("移速 %s"), *SignedPct(0.08f * M, false));
		case EFSUpgrade::MaxHP:
			return FString::Printf(TEXT("生命上限 +%d，并立即回复"), FMath::RoundToInt(18.f * M));
		case EFSUpgrade::DashCd:
			return FString::Printf(TEXT("闪避冷却 %s"), *SignedPct(0.12f * M, true));
		case EFSUpgrade::Pierce:
			return FString::Printf(TEXT("子弹多穿透 %d 个敌人"), (M >= 2.f) ? 2 : 1);
		case EFSUpgrade::Crit:
			return FString::Printf(TEXT("%s 概率打出 2.2 倍伤害"), *SignedPct(0.10f * M, false));
		case EFSUpgrade::Magnet:
			return FString::Printf(TEXT("拾取范围 %s，掉落更快飞来"), *SignedPct(0.40f * M, false));
		case EFSUpgrade::Ward:
			return FString::Printf(TEXT("受到伤害 %s"), *SignedPct(0.08f * M, true));
		case EFSUpgrade::Leech:
			return FString::Printf(TEXT("每次击杀回复 %.1f 生命"), 1.5f * M);
		default:
			return FString();
		}
	}

	FString UpgradeDescEN(EFSUpgrade U, EFSRarity R)
	{
		const float M = RarityMul(R);
		switch (U)
		{
		case EFSUpgrade::Damage:
			return FString::Printf(TEXT("DAMAGE %s"), *SignedPct(0.14f * M, false));
		case EFSUpgrade::FireRate:
			return FString::Printf(TEXT("FIRE RATE %s"), *SignedPct(0.10f * M, false));
		case EFSUpgrade::MoveSpeed:
			return FString::Printf(TEXT("MOVE SPEED %s"), *SignedPct(0.08f * M, false));
		case EFSUpgrade::MaxHP:
			return FString::Printf(TEXT("MAX HP +%d AND HEAL"), FMath::RoundToInt(18.f * M));
		case EFSUpgrade::DashCd:
			return FString::Printf(TEXT("DASH COOLDOWN %s"), *SignedPct(0.12f * M, true));
		case EFSUpgrade::Pierce:
			return FString::Printf(TEXT("BULLETS PIERCE +%d"), (M >= 2.f) ? 2 : 1);
		case EFSUpgrade::Crit:
			return FString::Printf(TEXT("%s CHANCE OF 2.2x DAMAGE"), *SignedPct(0.10f * M, false));
		case EFSUpgrade::Magnet:
			return FString::Printf(TEXT("PICKUP RANGE %s, FASTER PULL"), *SignedPct(0.40f * M, false));
		case EFSUpgrade::Ward:
			return FString::Printf(TEXT("DAMAGE TAKEN %s"), *SignedPct(0.08f * M, true));
		case EFSUpgrade::Leech:
			return FString::Printf(TEXT("HEAL %.1f ON KILL"), 1.5f * M);
		default:
			return FString();
		}
	}

	const TCHAR* WeatherNameCN(EFSWeather W)
	{
		switch (W)
		{
		case EFSWeather::Clear:     return TEXT("晴  朗");
		case EFSWeather::Rain:      return TEXT("寒  雨");
		case EFSWeather::Storm:     return TEXT("雷  暴");
		case EFSWeather::BloodMoon: return TEXT("血  月");
		}
		return TEXT("");
	}

	const TCHAR* WeatherNameEN(EFSWeather W)
	{
		switch (W)
		{
		case EFSWeather::Clear:     return TEXT("CLEAR");
		case EFSWeather::Rain:      return TEXT("CHILL RAIN");
		case EFSWeather::Storm:     return TEXT("THUNDERSTORM");
		case EFSWeather::BloodMoon: return TEXT("BLOOD MOON");
		}
		return TEXT("");
	}
}
