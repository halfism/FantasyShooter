#include "FSEnemy.h"
#include "FSUtil.h"
#include "FSFx.h"
#include "FSAudio.h"
#include "FSProjectile.h"
#include "FSPlayer.h"
#include "FSGameMode.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

namespace
{
	/**
	 * 这一刀是不是玩家自己打出来的。
	 * 主动技充能只认这个来源：敌人互伤、环境伤害、玩家自己的招式伤害都不算。
	 */
	AFSPlayerChar* PlayerDamageSource(AActor* Causer)
	{
		if (!Causer)
		{
			return nullptr;
		}
		if (AFSPlayerChar* P = Cast<AFSPlayerChar>(Causer))
		{
			return P->IsArtDamageLocked() ? nullptr : P;
		}
		if (AFSProjectile* Pr = Cast<AFSProjectile>(Causer))
		{
			AFSPlayerChar* P = Cast<AFSPlayerChar>(Pr->GetShooter());
			if (P && !P->IsArtDamageLocked())
			{
				return P;
			}
		}
		return nullptr;
	}
}

AFSEnemy::AFSEnemy()
{
	PrimaryActorTick.bCanEverTick = true;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(45.f, 60.f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Capsule->SetGenerateOverlapEvents(false);
	SetRootComponent(Capsule);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	VisualRoot->SetupAttachment(Capsule);
	VisualRoot->SetMobility(EComponentMobility::Movable);
}

void AFSEnemy::AddPart(const TCHAR* MeshName, const FVector& Loc, const FVector& Scale,
	const FLinearColor& Color, bool bGlow, float Glow, const FRotator& Rot)
{
	if (UStaticMeshComponent* C = FSUtil::AddShape(this, VisualRoot, MeshName, Loc, Scale, Color, bGlow, Glow, Rot))
	{
		FFSBodyPart P;
		P.Comp = C;
		P.Color = Color;
		P.bGlow = bGlow;
		P.Glow = Glow;
		Parts.Add(P);
	}
}

void AFSEnemy::Setup(EFSEnemyKind InKind, float HPScale, float SizeMul, EFSElite InElite)
{
	Kind = InKind;
	Elite = InElite;
	SizeScale = FMath::Clamp(SizeMul, 0.35f, 2.f);
	const FFSEnemyDef& D = FSDefs::Enemy(InKind);

	MaxHP = D.MaxHP * HPScale;

	// 调试：-fsbosshp=0.25 把 BOSS 血量压下来，方便一次跑完三个阶段看表现
	if (IsBoss())
	{
		static float BossHpMul = -1.f;
		if (BossHpMul < 0.f)
		{
			BossHpMul = 1.f;
			FString HpArg;
			if (FParse::Value(FCommandLine::Get(), TEXT("fsbosshp="), HpArg))
			{
				BossHpMul = FMath::Clamp(FCString::Atof(*HpArg), 0.02f, 4.f);
			}
		}
		MaxHP *= BossHpMul;
	}

	HP = MaxHP;
	Speed = D.Speed;
	Radius = D.Radius * SizeScale;

	// ---- 精英：在 BOSS 血量覆盖之后再乘，波次成长与词缀倍率彼此独立
	SinceHurt = 999.f;
	ThornCd = 0.f;
	VolatileFuse = 0.f;
	EliteFxT = 0.f;
	if (Elite != EFSElite::None)
	{
		// 血量 2.3 倍 + 体型 1.14 倍。体型只是为了让"这不是普通货"在余光里也成立，
		// 并没有大到影响判定手感；真正的差异由词缀本身承担。
		MaxHP *= 2.3f;
		HP = MaxHP;
		SizeScale = FMath::Clamp(SizeScale * 1.14f, 0.35f, 2.2f);
		Radius = D.Radius * SizeScale;

		if (Elite == EFSElite::Frenzy)
		{
			// 上限 640：玩家跑 660。疾行鬼本身 520，乘 1.28 会到 666 ——
			// 那会造出一只"追得上你、你永远拉不开"的怪，跑位这条解法直接消失。
			// 留 3% 的速度差，玩家能勉强拉开，但代价是一直在跑。
			Speed = FMath::Min(Speed * 1.28f, 640.f);
		}
	}

	Phase = FMath::FRandRange(0.f, 6.28f);
	ContactCooldown = 0.f;
	FireCooldown = FMath::FRandRange(0.8f, 2.4f);
	SummonCooldown = 9.f;

	BossPhase = 1;
	PhaseShift = 0.f;
	DashCd = 4.5f;
	DashTelegraph = 0.f;
	DashTime = 0.f;
	RingCd = 6.f;

	// 熔铁君王：起手留 3.2 秒，让玩家先看清体型和正面装甲，再吃第一记震波
	SlamCd = 3.2f;
	SlamTelegraph = 0.f;
	SlamR = 0.f;
	SlamRMax = (Kind == EFSEnemyKind::Tyrant) ? 880.f : 760.f;
	bSlamWave = false;
	bSlamHitPlayer = false;
	SlamPending = 0;
	SlamNextTick = 0.f;

	if (Capsule)
	{
		Capsule->SetCapsuleSize(Radius, Radius * 1.5f, true);
	}
	// 地面投影
	FSUtil::AddShape(this, Capsule, TEXT("Cylinder"), FVector(0.f, 0.f, -Radius * 1.5f + 1.6f),
		FVector(Radius * 1.7f / 50.f, Radius * 1.7f / 50.f, 0.02f),
		FLinearColor(0.012f, 0.014f, 0.020f), true, 1.0f);

	// ---- 重甲的数值必须在 BuildVisual 之前定好：建模型时按 bHasArmor 登记甲片
	bHasArmor = (Kind == EFSEnemyKind::Shielder || Kind == EFSEnemyKind::Warlord
		|| Kind == EFSEnemyKind::Tyrant);
	bArmorBroken = false;
	ArmorBreakT = 0.f;
	ArmorFlash = 0.f;
	StaggerT = 0.f;
	ArmorParts.Reset();
	ArmorPartLoc.Reset();
	ArmorPartScale.Reset();
	ArmorHPMax = 0.f;
	if (Kind == EFSEnemyKind::Shielder)
	{
		// 铁卫：破盾要 1.0 倍血量的正面火力，比绕背硬打完（0.3 倍率 ≈ 3.3 倍血量）明显划算，
		// 但又比绕到背后费时——两条解都在，玩家可以按场面选。
		ArmorHPMax = MaxHP * 1.00f;
	}
	else if (Kind == EFSEnemyKind::Warlord)
	{
		// 君王：0.26 倍血量。正面打穿约等于多花一倍的输出，
		// 换来"正面全伤 + 易伤"，是给"就是不想绕背"的玩家留的第二条路。
		ArmorHPMax = MaxHP * 0.26f;
	}
	else if (Kind == EFSEnemyKind::Tyrant)
	{
		// 终焉君王：甲比君王还薄一点（0.20 倍）。
		// 它的三阶段本来就要求"换着解两套招"，甲再厚会把玩家钉在原地磨血；
		// 这里让"打穿"是能在一阶段内做到的，逼玩家做的选择是"现在打还是先躲弹幕"。
		ArmorHPMax = MaxHP * 0.20f;
	}
	ArmorHP = ArmorHPMax;

	BuildVisual();

	// 震波环：熔铁君王与终焉君王共用同一套（后者只在二阶段起用它）
	if (Kind == EFSEnemyKind::Warlord || Kind == EFSEnemyKind::Tyrant)
	{
		BuildSlamRing();
	}

	// 精英环必须在 BuildVisual 之后建：它挂在 Capsule 上，尺寸要用最终的 Radius
	if (Elite != EFSElite::None)
	{
		BuildEliteFx();
	}
}

void AFSEnemy::BuildVisual()
{
	const FFSEnemyDef& D = FSDefs::Enemy(Kind);
	const float R = Radius;
	const float G = -R * 1.5f;          // 胶囊中心到地面的偏移
	const float S = R * 2.f / 100.f;    // 球体/立方体基准缩放

	// 登记刚加进来的那一件是"甲片"：崩落时要按各自原始位置/缩放往外飞
	auto MarkLastAsArmor = [this](const FVector& L, const FVector& Sc)
	{
		if (!bHasArmor || Parts.Num() == 0)
		{
			return;
		}
		UStaticMeshComponent* C = Parts.Last().Comp;
		if (!C)
		{
			return;
		}
		ArmorParts.Add(C);
		ArmorPartLoc.Add(L);
		ArmorPartScale.Add(Sc);
	};

	switch (Kind)
	{
	case EFSEnemyKind::Slime:
	{
		AddPart(TEXT("Sphere"), FVector(0, 0, G + R * 1.02f), FVector(S, S, S * 0.92f), D.Color);
		AddPart(TEXT("Sphere"), FVector(R * 0.62f, -R * 0.33f, G + R * 1.25f), FVector(0.19f, 0.19f, 0.19f), FLinearColor(0.9f, 1.f, 0.95f), true, 1.15f);
		AddPart(TEXT("Sphere"), FVector(R * 0.62f, R * 0.33f, G + R * 1.25f), FVector(0.19f, 0.19f, 0.19f), FLinearColor(0.9f, 1.f, 0.95f), true, 1.15f);
		AddPart(TEXT("Cube"), FVector(R * 0.30f, 0, G + R * 0.35f), FVector(0.30f * S, 0.62f * S, 0.06f * S), D.Accent, true, 1.0f);
		break;
	}
	case EFSEnemyKind::Runner:
	{
		const float H = R * 2.2f;
		AddPart(TEXT("Cone"), FVector(0, 0, G + H * 0.5f), FVector(S * 0.95f, S * 0.95f, H / 100.f), D.Color);
		AddPart(TEXT("Cube"), FVector(R * 0.42f, -R * 0.26f, G + H * 0.80f), FVector(0.14f, 0.10f, 0.10f), FLinearColor(1.f, 0.95f, 0.7f), true, 1.15f);
		AddPart(TEXT("Cube"), FVector(R * 0.42f, R * 0.26f, G + H * 0.80f), FVector(0.14f, 0.10f, 0.10f), FLinearColor(1.f, 0.95f, 0.7f), true, 1.15f);
		AddPart(TEXT("Cube"), FVector(-R * 0.15f, -R * 0.55f, G + H * 0.42f), FVector(0.55f, 0.09f, 0.09f), D.Accent, true, 1.0f, FRotator(0, 20, 25));
		AddPart(TEXT("Cube"), FVector(-R * 0.15f, R * 0.55f, G + H * 0.42f), FVector(0.55f, 0.09f, 0.09f), D.Accent, true, 1.0f, FRotator(0, -20, -25));
		break;
	}
	case EFSEnemyKind::Grunt:
	{
		AddPart(TEXT("Cube"), FVector(0, 0, G + R * 1.90f), FVector(S * 0.30f, S * 0.26f, S * 0.24f), FLinearColor(0.22f, 0.22f, 0.26f));
		AddPart(TEXT("Cube"), FVector(0, 0, G + R * 0.95f), FVector(0.86f * S, 0.62f * S, 1.10f * S), D.Color);
		AddPart(TEXT("Sphere"), FVector(0, 0, G + R * 2.20f), FVector(0.62f, 0.62f, 0.58f), D.Accent);
		AddPart(TEXT("Cube"), FVector(R * 0.35f, -R * 0.34f, G + R * 2.20f), FVector(0.10f, 0.10f, 0.09f), FLinearColor(0.05f, 0.05f, 0.08f));
		AddPart(TEXT("Cube"), FVector(R * 0.35f, R * 0.34f, G + R * 2.20f), FVector(0.10f, 0.10f, 0.09f), FLinearColor(0.05f, 0.05f, 0.08f));
		AddPart(TEXT("Cube"), FVector(R * 0.30f, -R * 0.78f, G + R * 1.20f), FVector(0.12f, 0.12f, 0.55f), FLinearColor(0.75f, 0.78f, 0.85f));
		AddPart(TEXT("Cube"), FVector(R * 0.62f, -R * 0.78f, G + R * 1.62f), FVector(0.55f, 0.10f, 0.10f), FLinearColor(0.85f, 0.88f, 0.95f), false);
		AddPart(TEXT("Cube"), FVector(R * 0.30f, R * 0.78f, G + R * 1.20f), FVector(0.12f, 0.12f, 0.55f), FLinearColor(0.75f, 0.78f, 0.85f));
		break;
	}
	case EFSEnemyKind::Brute:
	{
		AddPart(TEXT("Sphere"), FVector(0, 0, G + R * 1.25f), FVector(S, S * 0.92f, S * 1.05f), D.Color);
		AddPart(TEXT("Sphere"), FVector(R * 0.55f, -R * 0.38f, G + R * 1.75f), FVector(0.30f, 0.30f, 0.30f), FLinearColor(1.f, 0.9f, 0.85f), true, 1.20f);
		AddPart(TEXT("Sphere"), FVector(R * 0.55f, R * 0.38f, G + R * 1.75f), FVector(0.30f, 0.30f, 0.30f), FLinearColor(1.f, 0.9f, 0.85f), true, 1.20f);
		AddPart(TEXT("Cone"), FVector(0, -R * 0.62f, G + R * 2.35f), FVector(0.34f, 0.34f, 0.55f), D.Accent, false, 0.f, FRotator(-25, 0, -18));
		AddPart(TEXT("Cone"), FVector(0, R * 0.62f, G + R * 2.35f), FVector(0.34f, 0.34f, 0.55f), D.Accent, false, 0.f, FRotator(25, 0, 18));
		AddPart(TEXT("Cube"), FVector(0, -R * 0.95f, G + R * 0.95f), FVector(0.55f, 0.34f, 1.30f), D.Accent);
		AddPart(TEXT("Cube"), FVector(0, R * 0.95f, G + R * 0.95f), FVector(0.55f, 0.34f, 1.30f), D.Accent);
		AddPart(TEXT("Cube"), FVector(R * 0.30f, 0, G + R * 0.55f), FVector(0.5f, 1.1f, 0.35f), FLinearColor(0.18f, 0.12f, 0.24f));
		break;
	}
	case EFSEnemyKind::Mage:
	{
		const float H = R * 2.5f;
		AddPart(TEXT("Cone"), FVector(0, 0, G + H * 0.5f), FVector(S * 0.92f, S * 0.92f, H / 100.f), D.Color);
		AddPart(TEXT("Sphere"), FVector(R * 0.12f, 0, G + R * 2.18f), FVector(0.60f, 0.60f, 0.58f), FLinearColor(0.92f, 0.88f, 0.95f));
		AddPart(TEXT("Cone"), FVector(-R * 0.05f, 0, G + R * 2.52f), FVector(0.55f, 0.55f, 0.50f), D.Color);
		AddPart(TEXT("Cube"), FVector(R * 0.33f, -R * 0.20f, G + R * 2.20f), FVector(0.08f, 0.08f, 0.07f), FLinearColor(0.05f, 0.05f, 0.08f));
		AddPart(TEXT("Cube"), FVector(R * 0.33f, R * 0.20f, G + R * 2.20f), FVector(0.08f, 0.08f, 0.07f), FLinearColor(0.05f, 0.05f, 0.08f));
		AddPart(TEXT("Sphere"), FVector(R * 0.95f, 0, G + R * 1.45f), FVector(0.34f, 0.34f, 0.34f), D.Accent, true, 1.20f);
		break;
	}
	case EFSEnemyKind::Boss:
	{
		AddPart(TEXT("Sphere"), FVector(0, 0, G + R * 1.15f), FVector(S, S * 0.95f, S * 1.10f), D.Color);
		AddPart(TEXT("Cone"), FVector(0, 0, G + R * 2.30f), FVector(1.1f, 1.1f, 1.0f), D.Accent, true, 1.20f);
		AddPart(TEXT("Sphere"), FVector(R * 0.62f, -R * 0.30f, G + R * 1.55f), FVector(0.44f, 0.44f, 0.44f), FLinearColor(1.f, 0.85f, 0.85f), true, 1.25f);
		AddPart(TEXT("Sphere"), FVector(R * 0.62f, R * 0.30f, G + R * 1.55f), FVector(0.44f, 0.44f, 0.44f), FLinearColor(1.f, 0.85f, 0.85f), true, 1.25f);
		AddPart(TEXT("Cylinder"), FVector(0, 0, G + R * 0.30f), FVector(1.55f, 1.55f, 0.03f), D.Accent, true, 1.10f);
		AddPart(TEXT("Cylinder"), FVector(0, 0, G + R * 2.05f), FVector(1.15f, 1.15f, 0.03f), FLinearColor(1.f, 0.9f, 0.5f), true, 1.10f);
		AddPart(TEXT("Cube"), FVector(0, -R * 0.90f, G + R * 0.85f), FVector(0.45f, 0.30f, 1.55f), FLinearColor(0.35f, 0.10f, 0.25f));
		AddPart(TEXT("Cube"), FVector(0, R * 0.90f, G + R * 0.85f), FVector(0.45f, 0.30f, 1.55f), FLinearColor(0.35f, 0.10f, 0.25f));
		break;
	}
	case EFSEnemyKind::Shielder:
	{
		// 造型要点：正面必须一眼看出"这块会挡刀"。
		// 盾牌做得比身体宽、比身体亮，眉心一道金色徽记，配合减伤才不显得莫名其妙。
		const FLinearColor Steel(0.72f, 0.78f, 0.88f);
		AddPart(TEXT("Cube"), FVector(0, 0, G + R * 1.05f), FVector(0.95f * S, 1.05f * S, 1.20f * S), D.Color);
		AddPart(TEXT("Cube"), FVector(0, 0, G + R * 2.10f), FVector(0.50f * S, 0.50f * S, 0.34f * S), FLinearColor(0.24f, 0.27f, 0.33f));
		AddPart(TEXT("Cube"), FVector(R * 0.26f, 0, G + R * 2.12f), FVector(0.05f, 0.36f * S, 0.07f), D.Accent, true, 1.15f);

		// 主盾：薄板 + 上下发光盾缘 + 中央徽记。这四件就是"甲"，打穿了会整块飞出去
		AddPart(TEXT("Cube"), FVector(R * 0.98f, 0, G + R * 1.15f), FVector(0.13f, 1.06f, 1.42f), Steel);
		MarkLastAsArmor(FVector(R * 0.98f, 0, G + R * 1.15f), FVector(0.13f, 1.06f, 1.42f));
		AddPart(TEXT("Cube"), FVector(R * 1.02f, 0, G + R * 1.86f), FVector(0.10f, 1.14f, 0.09f), D.Accent, true, 1.05f);
		MarkLastAsArmor(FVector(R * 1.02f, 0, G + R * 1.86f), FVector(0.10f, 1.14f, 0.09f));
		AddPart(TEXT("Cube"), FVector(R * 1.02f, 0, G + R * 0.44f), FVector(0.10f, 1.14f, 0.09f), D.Accent, true, 1.05f);
		MarkLastAsArmor(FVector(R * 1.02f, 0, G + R * 0.44f), FVector(0.10f, 1.14f, 0.09f));
		AddPart(TEXT("Sphere"), FVector(R * 1.08f, 0, G + R * 1.15f), FVector(0.26f, 0.26f, 0.26f), D.Accent, true, 1.10f);
		MarkLastAsArmor(FVector(R * 1.08f, 0, G + R * 1.15f), FVector(0.26f, 0.26f, 0.26f));

		AddPart(TEXT("Cube"), FVector(0, -R * 0.45f, G + R * 0.28f), FVector(0.30f, 0.26f, 0.50f), FLinearColor(0.20f, 0.23f, 0.29f));
		AddPart(TEXT("Cube"), FVector(0, R * 0.45f, G + R * 0.28f), FVector(0.30f, 0.26f, 0.50f), FLinearColor(0.20f, 0.23f, 0.29f));
		break;
	}
	case EFSEnemyKind::Splitter:
	{
		// 造型要点：两个交叠的团块 + 一道竖着的发光裂缝，读作"它自己就要裂开"
		const FLinearColor Dim = D.Color * 0.72f;
		AddPart(TEXT("Sphere"), FVector(0, 0, G + R * 1.05f), FVector(S * 0.98f, S * 1.02f, S * 0.96f), D.Color);
		AddPart(TEXT("Sphere"), FVector(-R * 0.62f, R * 0.10f, G + R * 0.88f), FVector(S * 0.62f, S * 0.60f, S * 0.58f), Dim);
		AddPart(TEXT("Cube"), FVector(-R * 0.06f, 0, G + R * 1.05f), FVector(0.05f, 0.06f, 1.22f), D.Accent, true, 1.20f);
		AddPart(TEXT("Sphere"), FVector(R * 0.58f, -R * 0.30f, G + R * 1.42f), FVector(0.20f, 0.20f, 0.20f), FLinearColor(0.95f, 1.f, 0.98f), true, 1.15f);
		AddPart(TEXT("Sphere"), FVector(R * 0.58f, R * 0.30f, G + R * 1.42f), FVector(0.20f, 0.20f, 0.20f), FLinearColor(0.95f, 1.f, 0.98f), true, 1.15f);
		AddPart(TEXT("Sphere"), FVector(-R * 0.02f, 0, G + R * 0.62f), FVector(0.30f, 0.30f, 0.26f), D.Accent, true, 0.95f);
		break;
	}
	case EFSEnemyKind::Warlord:
	{
		// 造型要点：读作"一整块铁"。
		// 方肩、方身、方头，唯独胸口和锤头是熔开的橙光——那就是它唯一的弱点暗示（正面装甲）。
		const FLinearColor Iron(0.30f, 0.26f, 0.25f);
		const FLinearColor IronDark(0.19f, 0.17f, 0.17f);
		const FLinearColor Molten = D.Accent;

		// 腿：又短又粗，故意不显灵活
		AddPart(TEXT("Cube"), FVector(0, -R * 0.42f, G + R * 0.30f), FVector(0.46f, 0.38f, 0.62f), IronDark);
		AddPart(TEXT("Cube"), FVector(0, R * 0.42f, G + R * 0.30f), FVector(0.46f, 0.38f, 0.62f), IronDark);

		// 躯干 + 甲片
		AddPart(TEXT("Cube"), FVector(0, 0, G + R * 1.12f), FVector(1.42f, 1.66f, 1.58f), D.Color);
		AddPart(TEXT("Cube"), FVector(R * 0.10f, 0, G + R * 1.10f), FVector(0.10f, 1.30f, 1.20f), Iron);
		// 肩甲：比躯干更宽，给"巨型"的体积感
		AddPart(TEXT("Cube"), FVector(0, -R * 0.66f, G + R * 1.62f), FVector(1.02f, 0.86f, 0.62f), Iron);
		AddPart(TEXT("Cube"), FVector(0, R * 0.66f, G + R * 1.62f), FVector(1.02f, 0.86f, 0.62f), Iron);
		AddPart(TEXT("Cube"), FVector(0, -R * 0.66f, G + R * 1.90f), FVector(0.86f, 0.20f, 0.09f), Molten, true, 0.95f);
		AddPart(TEXT("Cube"), FVector(0, R * 0.66f, G + R * 1.90f), FVector(0.86f, 0.20f, 0.09f), Molten, true, 0.95f);

		// 胸口的熔缝：一条横贯的甲缘 + 三条竖纹。
		// 这四件就是"正面甲"的外显——打穿时它们整块崩掉，王座胸口会暗下来，
		// 玩家不用看 HUD 也知道"它前面软了"。
		AddPart(TEXT("Cube"), FVector(R * 0.62f, 0, G + R * 1.52f), FVector(0.09f, 1.08f, 0.12f), Molten, true, 0.95f);
		MarkLastAsArmor(FVector(R * 0.62f, 0, G + R * 1.52f), FVector(0.09f, 1.08f, 0.12f));

		for (int32 i = -1; i <= 1; ++i)
		{
			AddPart(TEXT("Cube"), FVector(R * 0.72f, i * R * 0.30f, G + R * 1.14f),
				FVector(0.06f, 0.09f, 0.92f), Molten, true, 0.90f);
			MarkLastAsArmor(FVector(R * 0.72f, i * R * 0.30f, G + R * 1.14f), FVector(0.06f, 0.09f, 0.92f));
		}

		// 头：方盔 + 一条横贯的橙色目缝
		AddPart(TEXT("Cube"), FVector(0, 0, G + R * 2.00f), FVector(0.66f, 0.72f, 0.56f), IronDark);
		AddPart(TEXT("Cube"), FVector(R * 0.34f, 0, G + R * 2.02f), FVector(0.05f, 0.46f, 0.10f), Molten, true, 1.10f);
		AddPart(TEXT("Cone"), FVector(-R * 0.06f, 0, G + R * 2.32f), FVector(0.40f, 0.40f, 0.34f), Iron);

		// 巨锤：右手垂在身侧，锤头朝下。静止时也能看出"这东西靠砸"
		AddPart(TEXT("Cylinder"), FVector(-R * 0.30f, R * 0.92f, G + R * 1.05f),
			FVector(0.13f, 0.13f, 1.05f), IronDark, false, 0.f, FRotator(0, 0, 12));
		AddPart(TEXT("Cube"), FVector(-R * 0.06f, R * 0.92f, G + R * 0.34f),
			FVector(0.70f, 0.62f, 0.58f), Iron);
		AddPart(TEXT("Cube"), FVector(-R * 0.06f, R * 0.92f, G + R * 0.34f),
			FVector(0.52f, 0.66f, 0.14f), Molten, true, 1.05f);

		// 脚下熔岩圈：和巫师领主的金色圆盘做出区分。
		// 刻意压暗压小——它一亮就会抢掉整只 BOSS 的视觉重心，看起来像"铁块下面挂了个灯罩"。
		AddPart(TEXT("Cylinder"), FVector(0, 0, G + R * 0.06f), FVector(1.45f, 1.45f, 0.03f), Molten, true, 0.42f);
		break;
	}
	case EFSEnemyKind::Tyrant:
	{
		// 合体造型：**下半身是君王的方铁，上半身是巫师领主的袍与冠**。
		// 这不是随便拼的——它的招式正好是"上半身出弹幕、下半身出震波"，
		// 玩家在二阶段看到它蹲下去蓄震的时候，形状上已经先有了一半预告。
		const FLinearColor Iron(0.30f, 0.27f, 0.28f);
		const FLinearColor IronDark(0.18f, 0.16f, 0.19f);
		const FLinearColor Robe = D.Color;      // 暗紫袍
		const FLinearColor Molten = D.Accent;   // 熔金

		// 腿与铁裙
		AddPart(TEXT("Cube"), FVector(0, -R * 0.44f, G + R * 0.34f), FVector(0.50f, 0.42f, 0.70f), IronDark);
		AddPart(TEXT("Cube"), FVector(0, R * 0.44f, G + R * 0.34f), FVector(0.50f, 0.42f, 0.70f), IronDark);
		AddPart(TEXT("Cylinder"), FVector(0, 0, G + R * 0.80f), FVector(1.34f, 1.34f, 0.52f), Iron);

		// 躯干：紫袍（比君王窄，比巫师宽）
		AddPart(TEXT("Cube"), FVector(0, 0, G + R * 1.36f), FVector(1.14f, 1.26f, 1.22f), Robe);

		// 肩甲：君王的宽肩
		AddPart(TEXT("Cube"), FVector(0, -R * 0.70f, G + R * 1.72f), FVector(0.98f, 0.62f, 0.56f), Iron);
		AddPart(TEXT("Cube"), FVector(0, R * 0.70f, G + R * 1.72f), FVector(0.98f, 0.62f, 0.56f), Iron);

		// 胸口甲缝：一条横贯 + 三条竖纹，四件一起登记为"正面甲"。
		// 数值上它是 0.20 倍血量，打法上它决定"能不能正面硬拆"。
		AddPart(TEXT("Cube"), FVector(R * 0.60f, 0, G + R * 1.46f), FVector(0.10f, 0.96f, 0.14f), Molten, true, 0.95f);
		MarkLastAsArmor(FVector(R * 0.60f, 0, G + R * 1.46f), FVector(0.10f, 0.96f, 0.14f));
		for (int32 i = -1; i <= 1; ++i)
		{
			AddPart(TEXT("Cube"), FVector(R * 0.70f, i * R * 0.28f, G + R * 1.14f),
				FVector(0.06f, 0.09f, 0.86f), Molten, true, 0.90f);
			MarkLastAsArmor(FVector(R * 0.70f, i * R * 0.28f, G + R * 1.14f), FVector(0.06f, 0.09f, 0.86f));
		}

		// 头 + 尖冠（巫师的轮廓，但冠用熔金而不是铁灰）。
		// 冠做成细高的金锥：第一版冠是灰的，俯视角下整颗头读成"一顶宽檐草帽"。
		AddPart(TEXT("Sphere"), FVector(0, 0, G + R * 2.02f), FVector(0.56f, 0.56f, 0.62f), IronDark);
		AddPart(TEXT("Cone"), FVector(0, 0, G + R * 2.52f), FVector(0.42f, 0.42f, 0.82f), Molten, true, 0.70f);
		// 目缝：一条熔金横线，是脸上唯一发光的地方
		AddPart(TEXT("Cube"), FVector(R * 0.40f, 0, G + R * 2.06f), FVector(0.05f, 0.42f, 0.09f), Molten, true, 1.10f);

		// "它是法系"这件事交给一颗悬浮在右肩外的法球来说，而不是头顶一个圆盘。
		// 圆盘在俯视角下是实心一块，怎么调都像饼；一颗小球既有指向性，又不占视觉重心。
		AddPart(TEXT("Sphere"), FVector(R * 0.98f, -R * 0.52f, G + R * 1.62f),
			FVector(0.23f, 0.23f, 0.23f), Molten, true, 0.85f);

		// 脚下熔岩圈：比君王亮一档——它是这一战的终点，值得亮
		AddPart(TEXT("Cylinder"), FVector(0, 0, G + R * 0.06f), FVector(1.55f, 1.55f, 0.03f), Molten, true, 0.55f);
		break;
	}
	}
}

void AFSEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 时缓（玩家主动技）在这里统一生效：参数是按值传的，改一行全身都吃到——
	// 移动、攻击冷却、蓄力、震波波前、接触伤害节奏全都会跟着慢下来，
	// 而且不需要在每个用到 DeltaSeconds 的地方各写一次乘区。
	if (const AFSGameMode* SlowGM = AFSGameMode::Get(this))
	{
		if (SlowGM->EnemyTimeScale < 0.999f)
		{
			DeltaSeconds *= SlowGM->EnemyTimeScale;
		}
	}

	if (bDying)
	{
		// 爆裂精英：已经判定死亡，但还欠玩家一个"看得见的爆炸"。
		// 引信期间推进预警环，到点再炸并销毁本体。这一段必须放在 bDying 之前，
		// 否则引信永远不会走完，地上会留一具永远不炸的尸体。
		if (VolatileFuse > 0.f)
		{
			VolatileFuse = FMath::Max(0.f, VolatileFuse - DeltaSeconds);
			const float K = 1.f - VolatileFuse / VolatileFuseDur;   // 0 → 1
			if (BlastRing)
			{
				const float R = FMath::Max(10.f, VolatileRadius * K);
				BlastRing->SetRelativeScale3D(FVector(R / 50.f, R / 50.f, 0.02f));
			}
			// 尸体鼓起来 + 往上顶一点：读作"要炸了"，而不是"站着没动"
			if (VisualRoot)
			{
				VisualRoot->SetRelativeScale3D(FVector(1.f + K * 0.24f));
				VisualRoot->SetRelativeLocation(FVector(0.f, 0.f, K * Radius * 0.12f));
			}
			if (VolatileFuse <= 0.f)
			{
				VolatileBlast();
			}
		}
		return;
	}

	Time += DeltaSeconds;
	FlashTimer = FMath::Max(0.f, FlashTimer - DeltaSeconds);
	ContactCooldown = FMath::Max(0.f, ContactCooldown - DeltaSeconds);
	FireCooldown -= DeltaSeconds;
	SummonCooldown -= DeltaSeconds;

	// 甲片崩落动画独立推进：玩家死亡 / 暂停时也要把甲片飞完，不然会僵在半空
	TickArmor(DeltaSeconds);

	// 精英环的呼吸 / 再生回血：与 AI 解耦，玩家死了也照常跑（否则环会僵住）
	if (IsElite())
	{
		TickElite(DeltaSeconds);
	}

	// 破甲踉跄：定身 + 整体往下压一下，读作"被打服了一下"
	if (StaggerT > 0.f)
	{
		const float DipDur = (Kind == EFSEnemyKind::Warlord) ? 1.10f : 0.45f;
		StaggerT = FMath::Max(0.f, StaggerT - DeltaSeconds);
		if (VisualRoot)
		{
			VisualRoot->SetRelativeLocation(FVector(0.f, 0.f,
				-Radius * 0.20f * FMath::Sin(PI * StaggerT / DipDur)));
		}
	}

	AFSGameMode* GM = AFSGameMode::Get(this);
	if (!GM)
	{
		return;
	}

	AFSPlayerChar* P = GM->GetPlayerChar();
	if (!P || !P->IsAlive() || GM->Phase != EFSPhase::Playing)
	{
		return;
	}

	const FFSEnemyDef& D = FSDefs::Enemy(Kind);
	const FVector Loc = GetActorLocation();
	FVector ToP = P->GetActorLocation() - Loc;
	ToP.Z = 0.f;
	const float Dist = ToP.Size();
	const FVector Dir = Dist > 1.f ? ToP / Dist : FVector::ForwardVector;
	const FVector Perp(-Dir.Y, Dir.X, 0.f);

	// 阶段切换：原地硬直 + 整体膨胀回弹，给玩家一个"它变形态了"的明确信号
	if (PhaseShift > 0.f)
	{
		PhaseShift = FMath::Max(0.f, PhaseShift - DeltaSeconds);
		if (VisualRoot)
		{
			const float K = 1.f - PhaseShift / PhaseShiftDur;     // 0 → 1
			const float S = 1.f + FMath::Sin(K * PI) * 0.20f;
			VisualRoot->SetRelativeScale3D(FVector(S));
			if (PhaseShift <= 0.f)
			{
				VisualRoot->SetRelativeScale3D(FVector(1.f));
				VisualRoot->SetRelativeLocation(FVector::ZeroVector);
			}
		}
		return;
	}

	FVector Vel = FVector::ZeroVector;
	switch (Kind)
	{
	case EFSEnemyKind::Slime:
		Vel = Dir * Speed;
		break;
	case EFSEnemyKind::Runner:
		Vel = (Dir + Perp * FMath::Sin(Time * 5.f + Phase) * 0.55f).GetSafeNormal() * Speed;
		break;
	case EFSEnemyKind::Grunt:
		Vel = Dir * Speed;
		break;
	case EFSEnemyKind::Brute:
		Vel = Dir * Speed;
		break;
	case EFSEnemyKind::Mage:
		if (Dist > 1350.f) Vel = Dir * Speed;
		else if (Dist < 950.f) Vel = -Dir * Speed;
		else Vel = Perp * Speed * 0.75f;
		break;
	case EFSEnemyKind::Shielder:
		// 铁卫不退不让：直线顶上来，靠正面硬吃，逼玩家换位置
		Vel = Dir * Speed;
		break;
	case EFSEnemyKind::Splitter:
		// 裂魂走位带摆动：读作"不稳定"，也不容易被一串子弹全打中
		Vel = (Dir + Perp * FMath::Sin(Time * 3.2f + Phase) * 0.35f).GetSafeNormal() * Speed;
		break;
	case EFSEnemyKind::Warlord:
		// 熔铁君王：不拉扯、不闪避，一直压上来。
		// 阶段越高步子越沉越快，但永远比玩家慢——它靠的是震波逼位，不是追上你。
		Vel = Dir * Speed * (1.f + 0.12f * (BossPhase - 1));
		// 蓄力和放波时定身：否则震波中心会跟着跑，玩家没法判断该往哪躲
		if (SlamTelegraph > 0.f || bSlamWave)
		{
			Vel = FVector::ZeroVector;
		}
		break;
	case EFSEnemyKind::Tyrant:
		// 终焉君王：一阶段沿用巫师的"保持距离环绕"——弹幕需要射程空间；
		// 二阶段起换成君王的压上，因为震波只有贴近才有威胁。
		// 走位方式是它两个"父本"的拼合，玩家能从它靠近/后退的节奏里读出当前阶段。
		if (BossPhase == 1)
		{
			if (Dist > 1250.f) Vel = Dir * Speed;
			else if (Dist < 800.f) Vel = -Dir * Speed * 0.65f;
			else Vel = Perp * Speed * 0.55f;
		}
		else
		{
			Vel = Dir * Speed * (1.f + 0.10f * (BossPhase - 1));
		}
		// 蓄力/放波时定身：震波中心跟着跑的话，玩家没法判断该往哪躲
		if (SlamTelegraph > 0.f || bSlamWave)
		{
			Vel = FVector::ZeroVector;
		}
		break;
	case EFSEnemyKind::Boss:
		// 阶段越高越不耐烦：贴身距离缩短、环绕速度加快
		if (BossPhase == 1)
		{
			if (Dist > 1300.f) Vel = Dir * Speed;
			else if (Dist < 850.f) Vel = -Dir * Speed * 0.7f;
			else Vel = Perp * Speed * 0.55f;
		}
		else if (BossPhase == 2)
		{
			if (Dist > 1500.f) Vel = Dir * Speed * 1.15f;
			else if (Dist < 1000.f) Vel = -Dir * Speed * 0.85f;
			else Vel = Perp * Speed * 0.80f;
		}
		else
		{
			if (Dist > 1700.f) Vel = Dir * Speed * 1.30f;
			else if (Dist < 900.f) Vel = -Dir * Speed;
			else Vel = Perp * Speed;
		}
		break;
	}

	// 破甲踉跄期间完全定身：这就是"打穿甲片"换来的输出窗口
	if (StaggerT > 0.f)
	{
		Vel = FVector::ZeroVector;
	}

	// 震波：与移动解耦，蓄力期间定身。
	// 终焉君王二阶段起才启用——一阶段留给弹幕，让玩家先把它当巫师打。
	if (Kind == EFSEnemyKind::Warlord
		|| (Kind == EFSEnemyKind::Tyrant && BossPhase >= 2))
	{
		UpdateSlam(DeltaSeconds, Dir, Dist);
	}

	// BOSS 冲锋：先蓄力抖动（可预判），再沿直线高速突进。
	// 终焉君王只在三阶段加入冲锋：那时震波已经把玩家赶到外圈，冲锋正好扫在外圈——
	// 两个机制咬在一起，而不是各打各的。
	const bool bDashUser = (Kind == EFSEnemyKind::Boss && BossPhase >= 2)
		|| (Kind == EFSEnemyKind::Tyrant && BossPhase >= 3);
	float VisualJitter = 0.f;
	if (bDashUser)
	{
		DashCd -= DeltaSeconds;
		if (DashTelegraph > 0.f)
		{
			DashTelegraph -= DeltaSeconds;
			Vel = FVector::ZeroVector;
			VisualJitter = FMath::Clamp(0.55f - DashTelegraph, 0.f, 0.55f) * 26.f;
			if (DashTelegraph <= 0.f)
			{
				DashTime = 0.34f;
				DashDir = Dir;
				AFSFx::Burst(GetWorld(), GetActorLocation(), Radius * 1.5f, D.Accent, 0.28f);
			}
		}
		else if (DashTime > 0.f)
		{
			DashTime = FMath::Max(0.f, DashTime - DeltaSeconds);
			Vel = DashDir * (BossPhase >= 3 ? 2700.f : 2250.f);
			if (DashTime <= 0.f)
			{
				DashCd = (BossPhase >= 3) ? 3.2f : 4.8f;
			}
		}
		else if (DashCd <= 0.f && Dist < 2100.f && Dist > 360.f)
		{
			DashTelegraph = 0.55f;
			UE_LOG(LogTemp, Log, TEXT("[FSBOSS] dash telegraph (phase %d, dist %.0f)"), BossPhase, Dist);
			FSAudio::Play(this, EFSSfx::Dash, Loc, 0.85f, 0.62f);
		}
	}

	// 相互排开，避免全叠在一个点上
	for (const TWeakObjectPtr<AFSEnemy>& Other : GM->Enemies)
	{
		AFSEnemy* O = Other.Get();
		if (!O || O == this || O->IsDead())
		{
			continue;
		}
		FVector Away = Loc - O->GetActorLocation();
		Away.Z = 0.f;
		const float MinD = Radius + O->GetRadius();
		const float DD = Away.Size();
		if (DD < MinD && DD > 1.f)
		{
			Vel += (Away / DD) * Speed * 0.85f * (1.f - DD / MinD);
		}
	}

	FVector NewLoc = Loc + (Vel * GM->EnemySpeedMul + Push) * DeltaSeconds;
	Push *= FMath::Pow(0.015f, DeltaSeconds);

	const float Half = GM->ArenaHalf - Radius;
	NewLoc.X = FMath::Clamp(NewLoc.X, -Half, Half);
	NewLoc.Y = FMath::Clamp(NewLoc.Y, -Half, Half);
	NewLoc.Z = Radius * 1.5f;
	SetActorLocation(NewLoc);

	// 转向要"转过去"，不能一帧到位。
	// 这条是铁卫和熔铁君王"绕背"机制唯一的前提：朝向瞬移的话，它们永远正面朝着玩家，
	// 正面减伤就等于无法规避，玩家只会觉得"这怪打不动"。
	// 移动仍然用 Dir（照常朝玩家走），只有朝向有惯性。
	float TurnRate = 900.f;
	if (Kind == EFSEnemyKind::Shielder) { TurnRate = 150.f; }
	else if (Kind == EFSEnemyKind::Warlord) { TurnRate = 55.f; }   // 重甲，转身极慢
	else if (Kind == EFSEnemyKind::Tyrant) { TurnRate = 85.f; }   // 有甲，但还得瞄准放弹幕
	else if (Kind == EFSEnemyKind::Boss) { TurnRate = 210.f; }
	SetActorRotation(FRotator(0.f, FMath::FixedTurn(GetActorRotation().Yaw, Dir.Rotation().Yaw, TurnRate * DeltaSeconds), 0.f));

	if (VisualRoot)
	{
		VisualRoot->SetRelativeLocation(FVector(0.f,
			VisualJitter > 0.f ? FMath::FRandRange(-VisualJitter, VisualJitter) : 0.f,
			FMath::Sin(Time * 7.5f + Phase) * Radius * 0.07f));
		VisualRoot->SetRelativeRotation(FRotator(0.f, 0.f, FMath::Sin(Time * 3.f + Phase) * 4.f));
	}

	// 接触伤害
	if (Dist < Radius + 46.f && ContactCooldown <= 0.f)
	{
		const bool bCharging = (DashTime > 0.f);
		// 狂暴精英：更快也更疼。撞一下的代价明显更高，"贴身换血"对它不成立。
		const float EliteMul = (Elite == EFSElite::Frenzy) ? 1.30f : 1.f;
		P->TakeDamage(D.ContactDamage * (bCharging ? 1.35f : 1.f) * EliteMul, FDamageEvent(), nullptr, this);
		ContactCooldown = 0.85f;
		if (bCharging)
		{
			// 撞到人就刹住，否则会一路把玩家顶着跑
			P->AddShake(16.f);
			DashTime = 0.f;
			DashCd = 3.6f;
		}
	}

	// 远程攻击
	const FVector Muzzle = GetActorLocation() + FVector(0.f, 0.f, Radius * 0.9f);
	if (Kind == EFSEnemyKind::Boss || Kind == EFSEnemyKind::Tyrant)
	{
		const bool bIsTyrant = (Kind == EFSEnemyKind::Tyrant);
		const bool bBusy = (DashTime > 0.f || DashTelegraph > 0.f);
		if (FireCooldown <= 0.f && !bBusy && Dist < 2800.f)
		{
			// 巫师 3/5 发；终焉君王 3/4/5 发，逐阶段"加宽"而不"加密"——
			// 它二阶段起已经有震波在逼位，扇射再密就没有能站的地方了。
			const int32 Shots = bIsTyrant ? (2 + BossPhase) : ((BossPhase == 1) ? 3 : 5);
			const float Spread = bIsTyrant ? 14.f : ((BossPhase == 1) ? 16.f : 13.f);
			const float ShotSpeed = bIsTyrant ? 1900.f : 2050.f;
			const float BaseDmg = (bIsTyrant ? 15.f : 14.f) + BossPhase * 3.f;
			BossFireFan(Shots, Spread, ShotSpeed, BaseDmg, Dir, Muzzle);
			FireCooldown = bIsTyrant
				? ((BossPhase == 1) ? 2.05f : (BossPhase == 2 ? 1.85f : 1.55f))
				: ((BossPhase == 1) ? 1.90f : (BossPhase == 2 ? 1.55f : 1.30f));
		}

		// 第三阶段追加环形弹幕：必须靠走位穿过弹幕缝隙。
		// 终焉君王这一层放到 5.0 秒——它同时还有震波和冲锋，三个都按原速会变成"只能用冲刺解全部"。
		if (BossPhase >= 3)
		{
			RingCd -= DeltaSeconds;
			if (RingCd <= 0.f)
			{
				RingCd = bIsTyrant ? 5.0f : 3.8f;
				BossFireRing(bIsTyrant ? 16 : 14, 1450.f, 13.f, Time * 0.9f, Muzzle);
				FSAudio::Play(this, EFSSfx::Launcher, Loc, 0.9f, 0.80f);
			}
		}
	}
	else if (D.bRanged && FireCooldown <= 0.f && Dist < 2400.f)
	{
		if (AFSProjectile* Proj = GetWorld()->SpawnActor<AFSProjectile>(AFSProjectile::StaticClass(), Muzzle, FRotator::ZeroRotator))
		{
			Proj->Launch(Muzzle, Dir, 2000.f, 13.f, 0, true, D.Accent, 1.35f, false, 0.f, 0.f, this);
		}
		FireCooldown = 2.4f * ((Elite == EFSElite::Frenzy) ? 0.70f : 1.f);
	}

	// BOSS 召唤：不同阶段召不同的兵，逼玩家在打 BOSS 与清小怪之间做取舍
	if (IsBoss() && SummonCooldown <= 0.f)
	{
		if (Kind == EFSEnemyKind::Warlord)
		{
			// 熔铁君王的"近卫"一路升级：史莱姆 → 刀兵 → 铁卫。
			// 三阶段召铁卫是刻意的——玩家正被震波逼着走位，再加两个正面吃不满伤害的，
			// 会逼出一个真实取舍：先绕死铁卫，还是硬扛着磨 BOSS。
			SummonCooldown = (BossPhase >= 3) ? 8.f : 11.f;
			EFSEnemyKind Guard = EFSEnemyKind::Slime;
			if (BossPhase == 2) { Guard = EFSEnemyKind::Grunt; }
			else if (BossPhase >= 3) { Guard = EFSEnemyKind::Shielder; }
			for (int32 i = 0; i < 2; ++i)
			{
				const float A = PI * i + Time;
				const FVector Sp = Loc + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * (Radius + 200.f);
				GM->SpawnEnemyAt(Guard, Sp);
			}
		}
		else if (Kind == EFSEnemyKind::Tyrant)
		{
			// 终焉君王的阶段兵不是"加难度"，而是每阶段补一个跟当前招式打配合的：
			//   P1 弹幕 → 术士（躲扇射时多一路远程，逼你换位而不是原地绕）
			//   P2 震波 → 刀兵（你被波纹赶着跑，它正好堵在跑位上）
			//   P3 合招 → 铁卫（清还是不清？这是这一阶段唯一能让你算错的选项）
			SummonCooldown = (BossPhase >= 3) ? 9.f : 11.f;
			const EFSEnemyKind Guard = (BossPhase >= 3) ? EFSEnemyKind::Shielder
				: ((BossPhase == 2) ? EFSEnemyKind::Grunt : EFSEnemyKind::Mage);
			for (int32 i = 0; i < 2; ++i)
			{
				const float A = PI * i + Time * 0.7f;
				const FVector Sp = Loc + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * (Radius + 210.f);
				GM->SpawnEnemyAt(Guard, Sp);
			}
		}
		else
		{
			SummonCooldown = (BossPhase >= 3) ? 6.5f : 9.f;
			for (int32 i = 0; i < 3; ++i)
			{
				const float A = (i / 3.f) * 6.283f + Time;
				const FVector Sp = Loc + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * (Radius + 150.f);
				EFSEnemyKind SummonKind = EFSEnemyKind::Slime;
				if (BossPhase == 2)
				{
					SummonKind = EFSEnemyKind::Runner;
				}
				else if (BossPhase >= 3)
				{
					SummonKind = (i == 0) ? EFSEnemyKind::Mage : EFSEnemyKind::Grunt;
				}
				GM->SpawnEnemyAt(SummonKind, Sp);
			}
		}
	}

	// 受击闪白：纯白×1.0 已经是能显示的上限，再高只会被裁白并触发一大片泛光
	const bool bNeedFlash = FlashTimer > 0.f;
	if (bNeedFlash != bFlashApplied)
	{
		bFlashApplied = bNeedFlash;
		for (const FFSBodyPart& Part : Parts)
		{
			if (Part.Comp)
			{
				FSUtil::SetShapeColor(Part.Comp, bNeedFlash ? FLinearColor(1.f, 1.f, 1.f) : Part.Color,
					bNeedFlash ? 1.0f : Part.Glow);
			}
		}
	}
}

void AFSEnemy::BossFireFan(int32 Shots, float Spread, float InSpeed, float Dmg,
	const FVector& FanDir, const FVector& Muzzle)
{
	const FFSEnemyDef& D = FSDefs::Enemy(Kind);
	for (int32 i = 0; i < Shots; ++i)
	{
		const float Off = (Shots <= 1) ? 0.f : (i - (Shots - 1) * 0.5f) * Spread;
		const FVector DirShot = FanDir.RotateAngleAxis(Off, FVector::UpVector);
		if (AFSProjectile* Proj = GetWorld()->SpawnActor<AFSProjectile>(AFSProjectile::StaticClass(), Muzzle, FRotator::ZeroRotator))
		{
			Proj->Launch(Muzzle, DirShot, InSpeed, Dmg, 0, true, D.Accent, 1.35f, false, 0.f, 0.f, this);
		}
	}
	FSAudio::Play(this, EFSSfx::ShotHeavy, Muzzle, 0.70f, 0.62f);
}

void AFSEnemy::BossFireRing(int32 Shots, float InSpeed, float Dmg, float AngleOffset, const FVector& Muzzle)
{
	const FFSEnemyDef& D = FSDefs::Enemy(Kind);
	const int32 N = FMath::Max(Shots, 3);
	for (int32 i = 0; i < N; ++i)
	{
		const float A = AngleOffset + (2.f * PI * i) / N;
		const FVector DirShot(FMath::Cos(A), FMath::Sin(A), 0.f);
		if (AFSProjectile* Proj = GetWorld()->SpawnActor<AFSProjectile>(AFSProjectile::StaticClass(), Muzzle, FRotator::ZeroRotator))
		{
			Proj->Launch(Muzzle, DirShot, InSpeed, Dmg, 0, true, D.Accent, 1.10f, false, 0.f, 0.f, this);
		}
	}
}

// ------------------------------------------------------------------ 精英词缀

void AFSEnemy::BuildEliteFx()
{
	const FLinearColor Ring = FSDefs::EliteRingColor(Elite);
	const float Z = -Radius * 1.5f + 2.6f;
	const float RingR = Radius * EliteRingScale;

	// 亮盘 + 暗芯夹出一条细环：直接摆一个亮圆盘会盖住整块地面，读起来是"发光的饼"；
	// 暗芯放到 0.90 倍，环带只剩 10% 宽，才像"标在地板上的一圈线"。
	// 自发光强度压到 1 以下：暗场泛光很强，抬到 1.5 整圈会被冲成纯白，颜色信息全丢。
	EliteRing = FSUtil::AddShape(this, Capsule, TEXT("Cylinder"),
		FVector(0.f, 0.f, Z), FVector(RingR / 50.f, RingR / 50.f, 0.02f), Ring, true, 0.70f);

	EliteRingCore = FSUtil::AddShape(this, Capsule, TEXT("Cylinder"),
		FVector(0.f, 0.f, Z + 0.6f),
		FVector(RingR * 0.90f / 50.f, RingR * 0.90f / 50.f, 0.02f),
		FLinearColor(0.010f, 0.012f, 0.020f), true, 1.0f);

	// 爆裂预警环：平时藏起来，引信点燃时从 0 扩到 VolatileRadius。
	// 没有这圈提示，"死后炸一下"就是纯惩罚；有了它，玩家能看见边界并跑出去。
	if (Elite == EFSElite::Volatile)
	{
		BlastRing = FSUtil::AddShape(this, Capsule, TEXT("Cylinder"),
			FVector(0.f, 0.f, Z + 0.4f), FVector(0.10f, 0.10f, 0.02f),
			FLinearColor(1.f, 0.58f, 0.16f), true, 2.2f);
		if (BlastRing)
		{
			BlastRing->SetVisibility(false, true);
		}
	}
}

void AFSEnemy::TickElite(float DeltaSeconds)
{
	EliteFxT += DeltaSeconds;
	SinceHurt += DeltaSeconds;
	ThornCd = FMath::Max(0.f, ThornCd - DeltaSeconds);

	const FLinearColor Ring = FSDefs::EliteRingColor(Elite);

	// 呼吸：静止的环看起来像"地上贴了张图"，幅度做小、频率做慢才像在搏动
	const float Breath = 1.f + FMath::Sin(EliteFxT * 2.4f) * 0.05f;
	const float RingR = Radius * EliteRingScale * Breath;

	if (EliteRing)
	{
		EliteRing->SetRelativeScale3D(FVector(RingR / 50.f, RingR / 50.f, 0.02f));
	}
	if (EliteRingCore)
	{
		const float CoreR = RingR * 0.90f;
		EliteRingCore->SetRelativeScale3D(FVector(CoreR / 50.f, CoreR / 50.f, 0.02f));
	}

	// 再生：3 秒不挨打就翻倍回血。这段窗口就是"必须集火"的具体含义——
	// 打两枪就换目标，等于把它打回满。
	if (Elite == EFSElite::Regen && !bDying && HP > 0.f && HP < MaxHP)
	{
		const float Pct = (SinceHurt > 3.f) ? 0.030f : 0.012f;
		HP = FMath::Min(MaxHP, HP + MaxHP * Pct * DeltaSeconds);
		if (EliteRing)
		{
			// 血越满环越亮：不用血条也能看出"它快回满了"
			const float K = 0.45f + 0.55f * GetHealthPct();
			FSUtil::SetShapeColor(EliteRing, Ring * K, 0.70f);
		}
	}
}

void AFSEnemy::EliteThornRetort()
{
	AFSGameMode* GM = AFSGameMode::Get(this);
	AFSPlayerChar* P = GM ? GM->GetPlayerChar() : nullptr;
	if (!P || !P->IsAlive())
	{
		return;
	}
	FVector D = P->GetActorLocation() - GetActorLocation();
	D.Z = 0.f;
	const float Dist = D.Size();

	// 判定用"玩家离它多近"而不是"子弹从哪来"：荆棘要惩罚的是贴身站桩，
	// 换成拉开距离就完全规避，所以这是个位置题，不是运气题。
	if (Dist > Radius + 520.f)
	{
		return;
	}

	ThornCd = 0.32f;
	P->TakeDamage(3.f, FDamageEvent(), nullptr, this);
	P->AddShake(3.f);
	AFSFx::Burst(GetWorld(), GetActorLocation() + (Dist > 1.f ? D / Dist : FVector::ForwardVector) * Radius,
		Radius * 0.85f, FSDefs::EliteColor(Elite), 0.16f);
	FSAudio::Play(this, EFSSfx::Hit, GetActorLocation(), 0.50f, 1.55f);
}

void AFSEnemy::VolatileBlast()
{
	AFSGameMode* GM = AFSGameMode::Get(this);
	const FVector Loc = GetActorLocation();
	const FLinearColor Hot(1.f, 0.55f, 0.15f);

	AFSFx::Shock(GetWorld(), FVector(Loc.X, Loc.Y, 6.f), VolatileRadius, Hot, 0.55f);
	AFSFx::Burst(GetWorld(), Loc, VolatileRadius * 0.85f, FLinearColor(1.f, 0.78f, 0.28f), 0.35f);
	FSAudio::Play(this, EFSSfx::ShotHeavy, Loc, 0.85f, 0.55f);

	if (GM)
	{
		if (AFSPlayerChar* P = GM->GetPlayerChar())
		{
			FVector D = P->GetActorLocation() - Loc;
			D.Z = 0.f;
			const float Dist = D.Size();
			if (Dist <= VolatileRadius)
			{
				// 边缘只吃 45%：跑出去是有意义的，不需要靠精准闪避
				const float K = FMath::Lerp(1.f, 0.45f, FMath::Clamp(Dist / VolatileRadius, 0.f, 1.f));
				P->TakeDamage(VolatileDamage * K, FDamageEvent(), nullptr, this);
				P->AddShake(11.f * K);
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[FSELITE] volatile blast r=%.0f dmg=%.0f"), VolatileRadius, VolatileDamage);
	Destroy();
}

void AFSEnemy::BuildSlamRing()
{
	// 40 段方块拼成一个"会呼吸的圆环"。段长按最大半径算（圆周 / 40 ≈ 157），
	// 小半径时相邻段会重叠成实心盘——那正好读作"刚从它脚下炸开"。
	const int32 N = 40;
	const float Z = -Radius * 1.5f + 3.f;
	const FLinearColor Molten(1.00f, 0.34f, 0.10f);
	SlamRing.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const float A = (2.f * PI * i) / N;
		const FRotator Rot(0.f, FMath::RadiansToDegrees(A) + 90.f, 0.f);
		if (UStaticMeshComponent* C = FSUtil::AddShape(this, Capsule, TEXT("Cube"),
			FVector(FMath::Cos(A) * SlamRMax, FMath::Sin(A) * SlamRMax, Z),
			FVector(1.60f, 0.16f, 0.02f), Molten, true, 1.0f, Rot))
		{
			C->SetVisibility(false, true);
			SlamRing.Add(C);
		}
	}
	bRingVisible = false;
}

void AFSEnemy::LayoutSlamRing(float R, float Intensity)
{
	if (SlamRing.Num() == 0)
	{
		return;
	}
	if (R <= 1.f)
	{
		if (bRingVisible)
		{
			bRingVisible = false;
			for (UStaticMeshComponent* C : SlamRing)
			{
				C->SetVisibility(false, true);
			}
		}
		return;
	}

	if (!bRingVisible)
	{
		bRingVisible = true;
		for (UStaticMeshComponent* C : SlamRing)
		{
			C->SetVisibility(true, true);
		}
	}

	const int32 N = SlamRing.Num();
	const float Z = -Radius * 1.5f + 3.f;
	// 每段的长度跟着半径走：(圆周 / 段数) × 1.02，刚好首尾相接。
	// 固定段长的话，波在起点附近会叠成一朵"齿轮"，半径大了又会断成虚线。
	const float SegLen = FMath::Max(0.10f, (2.f * PI * R / N) * 1.02f / 100.f);
	for (int32 i = 0; i < N; ++i)
	{
		const float A = (2.f * PI * i) / N;
		SlamRing[i]->SetRelativeLocation(FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Z));
		SlamRing[i]->SetRelativeScale3D(FVector(SegLen, 0.20f, 0.02f));
	}

	// 颜色只在亮度明显变化时重设：40 个组件每帧写参数没必要
	if (!FMath::IsNearlyEqual(Intensity, RingIntensity, 0.04f))
	{
		RingIntensity = Intensity;
		const FLinearColor Molten(1.00f, 0.34f, 0.10f);
		for (UStaticMeshComponent* C : SlamRing)
		{
			FSUtil::SetShapeColor(C, Molten, Intensity * 1.05f);
		}
	}
}

void AFSEnemy::UpdateSlam(float DeltaSeconds, const FVector& Dir, float Dist)
{
	(void)Dir;      // 朝向由 Tick 统一处理，这里不需要
	AFSGameMode* GM = AFSGameMode::Get(this);
	AFSPlayerChar* P = GM ? GM->GetPlayerChar() : nullptr;
	if (!P)
	{
		LayoutSlamRing(0.f, 0.f);
		return;
	}

	const FVector Loc = GetActorLocation();

	// 1) 蓄力：环钉在最大半径，当作"危险边界"。
	// 玩家永远先在边界上看到范围，再看到波从中心扫过来——这是这招唯一的信息来源。
	if (SlamTelegraph > 0.f)
	{
		SlamTelegraph -= DeltaSeconds;
		LayoutSlamRing(SlamRMax, 0.55f + 0.45f * FMath::Sin(Time * 24.f));
		if (SlamTelegraph > 0.f)
		{
			return;
		}
		// 起手结束：按阶段排波数，三阶段是三连震。
		// 终焉君王的圈比君王大一圈（同场地、更强的压迫感），这里必须同样用它的基准值——
		// 否则 EnterBossPhase 里设的 880 会在第一次起手时被 760 覆盖回去。
		SlamPending = (BossPhase >= 3) ? 3 : ((BossPhase == 2) ? 2 : 1);
		SlamRMax = (Kind == EFSEnemyKind::Tyrant ? 880.f : 760.f) + 70.f * (BossPhase - 1);
		SlamNextTick = 0.f;
		UE_LOG(LogTemp, Log, TEXT("[FSS] slam fire waves=%d rmax=%.0f phase=%d"), SlamPending, SlamRMax, BossPhase);
	}

	// 2) 放波：一波一波推，连震之间留 0.55 秒让玩家换位置
	if (SlamNextTick > 0.f)
	{
		SlamNextTick -= DeltaSeconds;
	}
	if (!bSlamWave && SlamPending > 0 && SlamNextTick <= 0.f)
	{
		--SlamPending;
		bSlamWave = true;
		bSlamHitPlayer = false;
		SlamR = Radius * 0.55f;
		SlamNextTick = 0.55f;
		AFSFx::Shock(GetWorld(), FVector(Loc.X, Loc.Y, 6.f), Radius * 2.4f,
			FLinearColor(1.f, 0.45f, 0.12f), 0.30f);
		FSAudio::Play(this, EFSSfx::ShotHeavy, Loc, 0.95f, 0.52f);
		if (AFSPlayerChar* SP = GM->GetPlayerChar())
		{
			SP->AddShake(9.f);
		}
		// 调试：-fscapture 时给"波正在扩散"留一张，环的可见性只能靠眼睛判断
		if (FParse::Param(FCommandLine::Get(), TEXT("fscapture")))
		{
			static bool bShotWave = false;
			if (!bShotWave)
			{
				bShotWave = true;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Slam_Launch.png"), false, false);
			}
		}
	}

	// 3) 波前推进 + 判定。
	// 判定带 = 环厚（56） + 玩家体型（46），比纯环厚宽松一点：
	// 视觉上"擦到边"也应该算命中，否则玩家会觉得环穿过了自己却没掉血。
	if (bSlamWave)
	{
		SlamR += SlamWaveSpeed * DeltaSeconds;
		LayoutSlamRing(SlamR, 1.0f);
		// 调试：起手点那一帧波还压在 BOSS 脚下，看不出东西；补一张"推到中途"的
		if (FParse::Param(FCommandLine::Get(), TEXT("fscapture")) && SlamR > SlamRMax * 0.55f)
		{
			static bool bShotMid = false;
			if (!bShotMid)
			{
				bShotMid = true;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Slam_Mid.png"), false, false);
			}
		}
		if (!bSlamHitPlayer)
		{
			const float Edge = 56.f + 62.f;
			if (FMath::Abs(Dist - SlamR) < Edge)
			{
				bSlamHitPlayer = true;
				P->TakeDamage(22.f + 6.f * BossPhase, FDamageEvent(), nullptr, this);
				P->AddShake(14.f);
				AFSFx::Burst(GetWorld(), P->GetActorLocation(), 70.f,
					FLinearColor(1.f, 0.40f, 0.16f), 0.22f);
			}
		}
		if (SlamR > SlamRMax)
		{
			bSlamWave = false;
			LayoutSlamRing(0.f, 0.f);
		}
	}

	// 4) 冷却：等所有波放完再重新起手
	if (!bSlamWave && SlamPending <= 0)
	{
		SlamCd -= DeltaSeconds;
		if (SlamCd <= 0.f)
		{
			SlamCd = (BossPhase >= 3) ? 3.9f : ((BossPhase == 2) ? 4.6f : 5.6f);
			SlamTelegraph = 0.85f;
			LayoutSlamRing(SlamRMax, 0.70f);
			FSAudio::Play(this, EFSSfx::Beep, Loc, 0.85f, 0.58f);
			UE_LOG(LogTemp, Log, TEXT("[FSS] slam telegraph  dist=%.0f  rmax=%.0f"), Dist, SlamRMax);
			if (FParse::Param(FCommandLine::Get(), TEXT("fscapture")))
			{
				static bool bShotTel = false;
				if (!bShotTel)
				{
					bShotTel = true;
					FScreenshotRequest::RequestScreenshot(
						FPaths::ProjectSavedDir() / TEXT("Slam_Telegraph.png"), false, false);
				}
			}
		}
	}
}

void AFSEnemy::EnterBossPhase(int32 NewPhase)
{
	BossPhase = FMath::Clamp(NewPhase, 1, 3);
	UE_LOG(LogTemp, Log, TEXT("[FSBOSS] enter phase %d  hp=%.0f/%.0f"), BossPhase, HP, MaxHP);
	PhaseShift = PhaseShiftDur;
	Push = FVector::ZeroVector;
	DashTelegraph = 0.f;
	DashTime = 0.f;
	DashCd = (BossPhase >= 3) ? 2.8f : 4.2f;
	RingCd = 2.6f;
	FireCooldown = 1.8f;
	SummonCooldown = 1.2f;

	// 受击闪白可能正好开着，进硬直前先还原，否则整段无敌期都是白团
	FlashTimer = 0.f;
	if (bFlashApplied)
	{
		bFlashApplied = false;
		for (const FFSBodyPart& Part : Parts)
		{
			if (Part.Comp)
			{
				FSUtil::SetShapeColor(Part.Comp, Part.Color, Part.Glow);
			}
		}
	}

	const FVector Loc = GetActorLocation();
	const bool bWarlord = (Kind == EFSEnemyKind::Warlord);
	const bool bTyrant = (Kind == EFSEnemyKind::Tyrant);
	// 熔铁君王的变身是"炉门炸开"，配色统一走熔铁橙，别和巫师领主的金／品红撞。
	// 终焉君王用白热金：它是两者的合体，颜色也该是两者的中间值。
	FLinearColor RingCol = FLinearColor(1.f, 0.82f, 0.35f);
	FLinearColor CoreCol = FLinearColor(1.f, 0.45f, 0.80f);
	if (bWarlord) { RingCol = FLinearColor(1.f, 0.40f, 0.10f); CoreCol = FLinearColor(1.f, 0.66f, 0.22f); }
	else if (bTyrant) { RingCol = FLinearColor(1.f, 0.74f, 0.34f); CoreCol = FLinearColor(1.f, 0.90f, 0.62f); }
	AFSFx::Shock(GetWorld(), FVector(Loc.X, Loc.Y, 6.f), Radius * 5.4f, RingCol, 0.75f);
	AFSFx::Burst(GetWorld(), Loc + FVector(0.f, 0.f, Radius * 1.2f), Radius * 2.0f, CoreCol, 0.55f);
	FSAudio::Play(this, EFSSfx::Boss, Loc, 1.f, (BossPhase >= 3) ? 0.72f : 0.82f);

	// 震波状态清零：变身瞬间不能把上一阶段的波带进新阶段
	if (bWarlord || bTyrant)
	{
		SlamTelegraph = 0.f;
		bSlamWave = false;
		SlamPending = 0;
		SlamNextTick = 0.f;
		SlamCd = 1.6f;
		// 终焉君王的圈更大：场地还是那块场地，但它该把整块场地都当成危险区
		SlamRMax = (bTyrant ? 880.f : 760.f) + 70.f * (BossPhase - 1);
		LayoutSlamRing(0.f, 0.f);
	}

	// 换阶段即"重新锻甲"：否则玩家在第一阶段把甲打穿，后两个阶段就变成纯沙包。
	// 重铸是有代价的——它把"打穿"变成每阶段都要重新争取一次的事。
	RefitArmor();

	AFSGameMode* GM = AFSGameMode::Get(this);
	if (!GM)
	{
		return;
	}
	if (AFSPlayerChar* P = GM->GetPlayerChar())
	{
		P->AddShake(16.f + BossPhase * 7.f);
	}
	GM->AnnounceBossPhase(this, BossPhase, Loc);

	// 阶段兵：绕自身一圈冒出来，位置夹在场地内
	const int32 Adds = 3;
	const float Half = GM->ArenaHalf - 220.f;
	for (int32 i = 0; i < Adds; ++i)
	{
		const float A = (2.f * PI * i) / Adds + Time;
		FVector Sp = Loc + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * (Radius + 170.f);
		Sp.X = FMath::Clamp(Sp.X, -Half, Half);
		Sp.Y = FMath::Clamp(Sp.Y, -Half, Half);
		EFSEnemyKind K = EFSEnemyKind::Grunt;
		if (bWarlord)
		{
			// 君王的阶段兵只召近卫：史莱姆 → 刀兵 → 铁卫
			K = (BossPhase >= 3) ? EFSEnemyKind::Shielder
				: ((BossPhase == 2) ? EFSEnemyKind::Grunt : EFSEnemyKind::Slime);
		}
		else if (bTyrant)
		{
			// 终焉君王：二阶段刀兵（堵你被波纹赶着跑的那条线），
			// 三阶段一只术士 + 两个铁卫——"先清哪只"在这个阶段没有正确答案。
			if (BossPhase == 2)
			{
				K = EFSEnemyKind::Grunt;
			}
			else
			{
				K = (i == 0) ? EFSEnemyKind::Mage : EFSEnemyKind::Shielder;
			}
		}
		else if (BossPhase >= 3)
		{
			K = (i == 0) ? EFSEnemyKind::Mage : EFSEnemyKind::Grunt;
		}
		GM->SpawnEnemyAt(K, Sp);
	}
}

void AFSEnemy::BreakArmor()
{
	if (!bHasArmor || bArmorBroken)
	{
		return;
	}
	bArmorBroken = true;
	ArmorHP = 0.f;
	ArmorBreakT = 1.f;
	ArmorFlash = 0.f;

	// 破甲的额外回报：踉跄。
	// 君王被打穿的那一下同时会打断正在蓄力的震波——"顶着甲硬砸"这条路才有意义，
	// 否则它只是"换个位置继续挨打"。终焉君王同理：三阶段三个招式同时压过来时，
	// 破甲就是唯一一个"能把它的节奏打断"的操作。
	if (Kind == EFSEnemyKind::Warlord || Kind == EFSEnemyKind::Tyrant)
	{
		if (SlamTelegraph > 0.f || bSlamWave)
		{
			SlamTelegraph = 0.f;
			bSlamWave = false;
			SlamPending = 0;
			SlamNextTick = 0.f;
			LayoutSlamRing(0.f, 0.f);
			UE_LOG(LogTemp, Log, TEXT("[FSARMOR] slam interrupted by armor break"));
		}
		SlamCd = FMath::Max(SlamCd, 2.6f);
		// 终焉君王定身短一点：它有三个招式，1.1 秒的输出窗口已经很长了
		StaggerT = (Kind == EFSEnemyKind::Tyrant) ? 0.85f : 1.10f;
	}
	else
	{
		StaggerT = 0.45f;
	}

	const FVector Loc = GetActorLocation();
	const FVector Front = Loc + GetActorForwardVector() * (Radius * 0.72f);
	AFSFx::Burst(GetWorld(), Front, Radius * 0.85f, FLinearColor(1.f, 0.66f, 0.24f), 0.34f);
	AFSFx::Shock(GetWorld(), FVector(Loc.X, Loc.Y, 6.f), Radius * 2.0f,
		FLinearColor(1.f, 0.52f, 0.16f), 0.42f);
	FSAudio::Play(this, EFSSfx::Kill, Loc, 0.90f, 0.68f);

	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		if (AFSPlayerChar* P = GM->GetPlayerChar())
		{
			P->AddShake(10.f + Radius * 0.05f);
			P->AddNotice(Kind == EFSEnemyKind::Shielder
				? FSUtil::L(TEXT("盾  破"), TEXT("SHIELD DOWN"))
				: FSUtil::L(TEXT("甲片崩落 · 正面暴露"), TEXT("ARMOR BROKEN")));
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[FSARMOR] %s armor broken (was %.0f) -> front mult 1.00, +15%% taken"),
		FSDefs::Enemy(Kind).NameCN, ArmorHPMax);
}

void AFSEnemy::RefitArmor()
{
	if (!bHasArmor)
	{
		return;
	}
	bArmorBroken = false;
	ArmorHP = ArmorHPMax;
	ArmorBreakT = 0.f;
	ArmorFlash = 0.f;
	StaggerT = 0.f;
	for (int32 i = 0; i < ArmorParts.Num(); ++i)
	{
		if (UStaticMeshComponent* C = ArmorParts[i])
		{
			if (ArmorPartLoc.IsValidIndex(i)) { C->SetRelativeLocation(ArmorPartLoc[i]); }
			if (ArmorPartScale.IsValidIndex(i)) { C->SetRelativeScale3D(ArmorPartScale[i]); }
			C->SetVisibility(true);
		}
	}
	AFSFx::Burst(GetWorld(), GetActorLocation() + GetActorForwardVector() * (Radius * 0.6f),
		Radius * 0.9f, FLinearColor(1.f, 0.72f, 0.30f), 0.30f);
	UE_LOG(LogTemp, Log, TEXT("[FSARMOR] %s reforge phase=%d armor=%.0f"),
		FSDefs::Enemy(Kind).NameCN, BossPhase, ArmorHPMax);
}

void AFSEnemy::TickArmor(float DeltaSeconds)
{
	ArmorFlash = FMath::Max(0.f, ArmorFlash - DeltaSeconds);
	if (ArmorBreakT <= 0.f)
	{
		// 甲还在挨打：甲片整体微微一胀，读作"扛住了这一下"。
		// （受击白闪走的是全身上下所有部件，这里只动甲片，两者不冲突）
		if (bHasArmor && !bArmorBroken)
		{
			const float P = ArmorFlash / 0.16f;
			const float S = 1.f + 0.10f * P;
			for (int32 i = 0; i < ArmorParts.Num(); ++i)
			{
				if (ArmorParts[i] && ArmorPartScale.IsValidIndex(i))
				{
					ArmorParts[i]->SetRelativeScale3D(ArmorPartScale[i] * S);
				}
			}
		}
		return;
	}

	ArmorBreakT = FMath::Max(0.f, ArmorBreakT - DeltaSeconds / 0.75f);
	const float K = 1.f - ArmorBreakT;                  // 0 → 1
	const float E = 1.f - (1.f - K) * (1.f - K);        // 先快后慢
	for (int32 i = 0; i < ArmorParts.Num(); ++i)
	{
		UStaticMeshComponent* C = ArmorParts[i];
		if (!C || !ArmorPartLoc.IsValidIndex(i) || !ArmorPartScale.IsValidIndex(i))
		{
			continue;
		}
		// 每片朝自己的方向外抛：横向散开 + 以上抛为主，读作"炸开"而不是"整体平移"
		FVector Dir = ArmorPartLoc[i];
		Dir.Z = 0.f;
		Dir = Dir.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Dir.GetSafeNormal();
		Dir = (Dir + FVector(0.55f, 0.f, 1.15f)).GetSafeNormal();
		C->SetRelativeLocation(ArmorPartLoc[i] + Dir * (Radius * 2.0f) * E);
		C->SetRelativeScale3D(ArmorPartScale[i] * FMath::Max(0.12f, 1.f - E * 0.85f));
		if (ArmorBreakT <= 0.f)
		{
			C->SetVisibility(false);
			C->SetRelativeLocation(ArmorPartLoc[i]);
			C->SetRelativeScale3D(ArmorPartScale[i]);
		}
	}
}

void AFSEnemy::DebugBreakArmor()
{
	BreakArmor();
}

float AFSEnemy::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (bDying || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	// 阶段切换硬直期间无敌：这是给玩家"喘口气 + 认出形态变了"的窗口
	if (PhaseShift > 0.f)
	{
		return 0.f;
	}

	// 重甲单位（铁卫的盾、熔铁君王的胸甲）：正面命中先削甲，削穿了就不再有正面减伤。
	// 判定统一走"伤害来源相对自己的朝向"，所以绕后 / 侧打 / 穿透串背都有明确收益，
	// 而"顶着盾硬打"也不再是白费——只是要付出一份额外的甲值。
	float Mult = 1.f;
	bool bFrontHit = false;
	if (DamageCauser && bHasArmor)
	{
		FVector ToSrc = DamageCauser->GetActorLocation() - GetActorLocation();
		ToSrc.Z = 0.f;
		const FVector ToSrcN = ToSrc.GetSafeNormal();
		// 铁卫 ±70°，熔铁君王 与 终焉君王 ±50°（cos 0.34 / 0.64）
		const float FrontCos = (Kind == EFSEnemyKind::Shielder) ? 0.34f : 0.64f;
		if (!ToSrcN.IsNearlyZero() && FVector::DotProduct(ToSrcN, GetActorForwardVector()) > FrontCos)
		{
			bFrontHit = true;
			Mult = (Kind == EFSEnemyKind::Shielder) ? 0.30f : 0.40f;
		}
	}

	// 甲值按"贴到甲上的原始伤害"扣：和减伤系数解耦，否则正面只吃三成的铁卫
	// 会因为自己减伤而永远打不穿自己。
	if (bFrontHit && !bArmorBroken)
	{
		ArmorHP -= DamageAmount;
		ArmorFlash = 0.16f;
		if (ArmorHP <= 0.f)
		{
			BreakArmor();
		}
	}

	// 甲片还在，正面才打折；打穿之后正面全吃，还额外吃 15% 易伤
	if (bFrontHit && !bArmorBroken)
	{
		DamageAmount *= Mult;
		// 给一下火花：否则玩家分不清"没打中"和"被盾挡了"
		const FLinearColor Spark = (Kind == EFSEnemyKind::Shielder)
			? FLinearColor(1.f, 0.86f, 0.42f) : FLinearColor(1.f, 0.55f, 0.18f);
		AFSFx::Burst(GetWorld(), GetActorLocation() + GetActorForwardVector() * (Radius * 0.92f),
			Radius * 0.58f, Spark, 0.14f);
		FSAudio::Play(this, EFSSfx::Hit, GetActorLocation(), 0.52f, 1.34f);
	}
	if (bArmorBroken)
	{
		DamageAmount *= 1.15f;
	}

	HP -= DamageAmount;
	FlashTimer = 0.085f;
	SinceHurt = 0.f;

	// 主动技充能：取"减伤 / 护甲 / 易伤都算完"的最终伤害。
	// 于是"打穿甲的那一刻充能明显加速"是自然结果，不需要再单独写一条奖励规则。
	if (AFSPlayerChar* Src = PlayerDamageSource(DamageCauser))
	{
		Src->AddAbilityCharge(DamageAmount);
	}

	// 荆棘：贴着它打就反刺。规则单纯到"一看就懂"才有意义——
	// 不是概率、不是条件分支，只是"你离得太近了"。
	if (Elite == EFSElite::Thorn && ThornCd <= 0.f)
	{
		EliteThornRetort();
	}

	// BOSS 血量撞到阶段门槛时不再继续掉血，避免一发大招直接跳过整个阶段
	if (IsBoss() && BossPhase < 3)
	{
		const float Gate = PhaseGate(BossPhase);
		if (HP / MaxHP <= Gate)
		{
			HP = MaxHP * Gate;
			FlashTimer = 0.f;
			EnterBossPhase(BossPhase + 1);
			return DamageAmount;
		}
	}

	if (HP > 0.f && FMath::FRand() < 0.55f)
	{
		FSAudio::Play(this, EFSSfx::Hit, GetActorLocation(), 0.45f, FMath::FRandRange(0.88f, 1.18f));
	}

	if (DamageCauser)
	{
		FVector Away = GetActorLocation() - DamageCauser->GetActorLocation();
		Away.Z = 0.f;
		const float PushStrength = FMath::Clamp(320.f - Radius * 0.6f, 40.f, 320.f);
		Push += Away.GetSafeNormal() * PushStrength;
	}

	if (HP <= 0.f)
	{
		Die();
	}
	return DamageAmount;
}

void AFSEnemy::Die()
{
	if (bDying)
	{
		return;
	}
	bDying = true;

	const FFSEnemyDef& D = FSDefs::Enemy(Kind);
	const FVector Loc = GetActorLocation();

	// 连击越高，击杀音高越往上走，听觉上能感到节奏在加快
	AFSGameMode* GM = AFSGameMode::Get(this);
	const float ComboPitch = GM ? FMath::Min((float)GM->GetCombo() * 0.015f, 0.30f) : 0.f;
	FSAudio::Play(this, EFSSfx::Kill, Loc, Radius > 70.f ? 1.15f : 0.75f,
		FMath::FRandRange(0.92f, 1.10f) + ComboPitch);
	AFSFx::Burst(GetWorld(), Loc, Radius * 1.35f, D.Accent, 0.30f);
	if (Radius > 70.f)
	{
		AFSFx::Shock(GetWorld(), FVector(Loc.X, Loc.Y, 6.f), Radius * 3.2f, D.Color, 0.5f);
	}

	// 裂魂：死后分成两只小史莱姆。血按 0.45 继承、体型 0.70，
	// 否则"分裂"就变成免费加怪，清完比不清还累。
	if (Kind == EFSEnemyKind::Splitter && GM)
	{
		const FVector Fwd = GetActorForwardVector();
		const FVector Side(-Fwd.Y, Fwd.X, 0.f);
		const float Lim = GM->ArenaHalf - 160.f;
		for (int32 i = 0; i < 2; ++i)
		{
			const float Sgn = (i == 0) ? 1.f : -1.f;
			FVector Sp = Loc + Side * (Radius * 0.9f * Sgn);
			Sp.X = FMath::Clamp(Sp.X, -Lim, Lim);
			Sp.Y = FMath::Clamp(Sp.Y, -Lim, Lim);
			GM->SpawnEnemyAt(EFSEnemyKind::Slime, Sp, 0.45f, 0.70f);
		}
		AFSFx::Shock(GetWorld(), FVector(Loc.X, Loc.Y, 6.f), Radius * 2.6f, D.Accent, 0.45f);
		UE_LOG(LogTemp, Log, TEXT("[FSSPLIT] splitter died -> 2 slimes"));
	}

	if (GM)
	{
		GM->OnEnemyKilled(this, Loc);
	}

	// 爆裂：先不销毁，留 0.55 秒把爆炸半径画出来再炸。
	// 这样"杀完就走"是一个可执行的动作，而不是死后凭空掉一截血。
	if (Elite == EFSElite::Volatile)
	{
		VolatileFuse = VolatileFuseDur;
		if (BlastRing)
		{
			BlastRing->SetVisibility(true, true);
			BlastRing->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.02f));
		}
		FSAudio::Play(this, EFSSfx::Beep, Loc, 0.75f, 0.55f);
		UE_LOG(LogTemp, Log, TEXT("[FSELITE] volatile armed r=%.0f dmg=%.0f"), VolatileRadius, VolatileDamage);
		return;
	}

	Destroy();
}
