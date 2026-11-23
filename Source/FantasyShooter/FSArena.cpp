#include "FSArena.h"
#include "FSUtil.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	/** 一套场景主题的全部配色 */
	struct FFSTheme
	{
		const TCHAR* CN = TEXT("");
		const TCHAR* EN = TEXT("");
		FLinearColor Ground = FLinearColor::Black;
		FLinearColor Grid = FLinearColor::Black;
		FLinearColor Wall = FLinearColor::Black;
		FLinearColor Strip = FLinearColor::Black;
		FLinearColor CircleA = FLinearColor::Black;
		FLinearColor CircleB = FLinearColor::Black;
		FLinearColor CircleC = FLinearColor::Black;
		FLinearColor TowerBody = FLinearColor::Black;
		FLinearColor TowerTop = FLinearColor::Black;
		FLinearColor TowerGlow = FLinearColor::Black;
		FLinearColor PropA = FLinearColor::Black;
		FLinearColor PropB = FLinearColor::Black;
		FLinearColor Particle = FLinearColor::White;
		int32 ParticleKind = 0;
		FVector ParticleScale = FVector(0.03f, 0.03f, 1.1f);
		float ParticleGlow = 2.5f;
	};

	const FFSTheme& GetTheme(int32 Idx)
	{
		static const FFSTheme Themes[3] =
		{
			// 0 幽蓝竞技场
			{
				TEXT("幽 蓝 竞 技 场"), TEXT("AZURE ARENA"),
				FLinearColor(0.010f, 0.013f, 0.020f),
				FLinearColor(0.105f, 0.205f, 0.300f),
				FLinearColor(0.022f, 0.026f, 0.036f),
				FLinearColor(0.09f, 0.38f, 0.54f),
				FLinearColor(0.10f, 0.34f, 0.48f),
				FLinearColor(0.012f, 0.015f, 0.022f),
				FLinearColor(0.28f, 0.16f, 0.45f),
				FLinearColor(0.10f, 0.12f, 0.17f),
				FLinearColor(0.62f, 0.36f, 0.95f),
				FLinearColor(0.70f, 0.45f, 1.00f),
				FLinearColor(0.10f, 0.12f, 0.17f),
				FLinearColor(0.13f, 0.15f, 0.21f),
				FLinearColor(0.55f, 0.78f, 1.00f), 0,
				FVector(0.022f, 0.022f, 0.55f), 1.0f
			},
			// 1 霜封荒原
			{
				TEXT("霜 封 荒 原"), TEXT("FROST WASTE"),
				FLinearColor(0.006f, 0.011f, 0.019f),
				FLinearColor(0.050f, 0.105f, 0.150f),
				FLinearColor(0.020f, 0.032f, 0.048f),
				FLinearColor(0.190f, 0.420f, 0.580f),
				FLinearColor(0.042f, 0.120f, 0.185f),
				FLinearColor(0.010f, 0.017f, 0.027f),
				FLinearColor(0.075f, 0.195f, 0.280f),
				FLinearColor(0.045f, 0.070f, 0.100f),
				FLinearColor(0.280f, 0.460f, 0.600f),
				FLinearColor(0.430f, 0.600f, 0.700f),
				FLinearColor(0.040f, 0.062f, 0.090f),
				FLinearColor(0.058f, 0.092f, 0.130f),
				FLinearColor(0.85f, 0.93f, 1.00f), 1,
				FVector(0.062f, 0.062f, 0.062f), 1.1f
			},
			// 2 熔岩裂谷
			{
				TEXT("熔 岩 裂 谷"), TEXT("MAGMA RIFT"),
				FLinearColor(0.011f, 0.006f, 0.005f),
				FLinearColor(0.085f, 0.024f, 0.009f),
				FLinearColor(0.024f, 0.011f, 0.009f),
				FLinearColor(0.150f, 0.048f, 0.014f),
				FLinearColor(0.075f, 0.024f, 0.008f),
				FLinearColor(0.010f, 0.005f, 0.004f),
				FLinearColor(0.170f, 0.056f, 0.014f),
				FLinearColor(0.028f, 0.013f, 0.010f),
				FLinearColor(0.190f, 0.065f, 0.019f),
				FLinearColor(0.280f, 0.100f, 0.028f),
				FLinearColor(0.020f, 0.009f, 0.008f),
				FLinearColor(0.028f, 0.013f, 0.010f),
				FLinearColor(1.00f, 0.50f, 0.15f), 2,
				FVector(0.040f, 0.040f, 0.100f), 1.2f
			}
		};
		return Themes[FMath::Clamp(Idx, 0, 2)];
	}

	ADirectionalLight* SpawnLight(UWorld* W, const FVector& Loc, const FRotator& Rot,
		float Intensity, const FLinearColor& Color)
	{
		ADirectionalLight* L = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Loc, Rot);
		if (!L)
		{
			return nullptr;
		}
		if (ULightComponent* LC = L->GetLightComponent())
		{
			LC->SetMobility(EComponentMobility::Movable);
			LC->SetIntensity(Intensity);
			LC->SetLightColor(Color);
			LC->SetCastShadows(false);
		}
		return L;
	}
}

AFSArena::AFSArena()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

const TCHAR* AFSArena::GetThemeNameCN() const
{
	return GetTheme(Variant).CN;
}

const TCHAR* AFSArena::GetThemeNameEN() const
{
	return GetTheme(Variant).EN;
}

void AFSArena::Build(float HalfSize)
{
	Half = HalfSize;
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}

	// ---- 选主题（-fsmap=0/1/2 可强制指定，方便调试）
	int32 Forced = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("fsmap="), Forced) && Forced >= 0 && Forced <= 2)
	{
		Variant = Forced;
	}
	else
	{
		Variant = FMath::RandRange(0, 2);
	}
	const FFSTheme& T = GetTheme(Variant);
	ParticleKind = T.ParticleKind;

	// ---- 地面（六倍尺寸，防止视角边缘露底）
	// 地面上再挂一层细节贴图：加色通道把黑底刷出细密的科技网格，
	// 和上面 6 条粗发光网格叠在一起，近景才不至于是一块死黑。
	// Tiling 60 对应每格约 300uu（地面缩放 180 倍 × 100uu / 60），比一个身位略大。
	if (UStaticMeshComponent* GroundComp = FSUtil::AddShape(this, Root, TEXT("Cube"), FVector(0.f, 0.f, -26.f),
		FVector(Half * 6.f / 100.f, Half * 6.f / 100.f, 0.52f), T.Ground, true, 1.0f))
	{
		FSUtil::ApplyDetail(GroundComp, 60.f, 0.060f);
	}

	// ---- 发光网格
	const int32 Steps = 6;
	const float Spacing = Half / Steps;
	for (int32 i = -Steps; i <= Steps; ++i)
	{
		if (i == 0)
		{
			continue;
		}
		const float P = i * Spacing;
		FSUtil::AddShape(this, Root, TEXT("Cube"), FVector(0.f, P, 1.f),
			FVector(Half * 2.f / 100.f, 0.045f, 0.02f), T.Grid, true, 1.0f);
		FSUtil::AddShape(this, Root, TEXT("Cube"), FVector(P, 0.f, 1.f),
			FVector(0.045f, Half * 2.f / 100.f, 0.02f), T.Grid, true, 1.0f);
	}

	// ---- 中央法阵
	FSUtil::AddShape(this, Root, TEXT("Cylinder"), FVector(0.f, 0.f, 2.f),
		FVector(5.0f, 5.0f, 0.02f), T.CircleA, true, 1.0f);
	FSUtil::AddShape(this, Root, TEXT("Cylinder"), FVector(0.f, 0.f, 3.f),
		FVector(4.1f, 4.1f, 0.02f), T.CircleB, true, 1.0f);
	FSUtil::AddShape(this, Root, TEXT("Cylinder"), FVector(0.f, 0.f, 4.f),
		FVector(1.6f, 1.6f, 0.02f), T.CircleC, true, 1.0f);

	// ---- 围墙 + 顶部光带
	// 高度压到 220（原 300）：相机在角色斜后方 848 处，角色贴边时视线要能从墙顶越过去，
	// 否则整面墙会横在屏幕上把战场下半部分全遮住。
	const float WallH = (Variant == 1) ? 250.f : 220.f;
	for (int32 S = 0; S < 4; ++S)
	{
		const float Sign = (S % 2 == 0) ? 1.f : -1.f;
		const bool bX = (S < 2);
		const FVector Loc = bX ? FVector(Half * Sign, 0.f, WallH * 0.5f) : FVector(0.f, Half * Sign, WallH * 0.5f);
		const FVector Scale = bX ? FVector(0.6f, Half * 2.f / 100.f, WallH / 100.f)
		                         : FVector(Half * 2.f / 100.f, 0.6f, WallH / 100.f);
		FSUtil::AddShape(this, Root, TEXT("Cube"), Loc, Scale, T.Wall, true, 1.0f);

		const FVector StripLoc = bX ? FVector(Half * Sign, 0.f, WallH + 6.f) : FVector(0.f, Half * Sign, WallH + 6.f);
		const FVector StripScale = bX ? FVector(0.75f, Half * 2.f / 100.f, 0.10f) : FVector(Half * 2.f / 100.f, 0.75f, 0.10f);
		FSUtil::AddShape(this, Root, TEXT("Cube"), StripLoc, StripScale, T.Strip, true, 1.0f);

		// 相机防越界挡板。玩家活动范围是 Half-300，弹簧臂水平投影 849，
		// 所以贴边时相机会退到墙外 549 —— 视野下缘就横着一条墙顶光带。
		// 程序化几何体全是 NoCollision，弹簧臂探测不到任何东西，只能专门立一块
		// 不可见的遮挡体，并且只挡 ECC_Camera：玩家和敌人的位移完全不受影响。
		// 高度给到 900：墙本体只有 220，而相机在 740 高处，视线在 z≈320 处就已越过墙顶。
		const FVector BlockLoc = bX ? FVector(Half * Sign, 0.f, 440.f) : FVector(0.f, Half * Sign, 440.f);
		const FVector BlockScale = bX ? FVector(0.5f, Half * 2.f / 100.f, 9.f)
		                              : FVector(Half * 2.f / 100.f, 0.5f, 9.f);
		if (UStaticMeshComponent* Blk = FSUtil::AddShape(this, Root, TEXT("Cube"), BlockLoc, BlockScale,
			FLinearColor::Black, false, 0.f))
		{
			Blk->SetVisibility(false, true);
			Blk->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Blk->SetCollisionObjectType(ECC_WorldStatic);
			Blk->SetCollisionResponseToAllChannels(ECR_Ignore);
			Blk->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		}
	}

	// ---- 各主题专属装饰
	switch (Variant)
	{
	case 1: BuildDecorFrost(); break;
	case 2: BuildDecorLava(); break;
	default: BuildDecorBlue(); break;
	}

	// ---- 外部 CC0 道具（真实的低模摆件，与上面的程序化几何互补）
	BuildArtProps(Variant);

	// ---- 灯光：主光 + 补光 + 轮廓光
	KeyLight = SpawnLight(W, FVector(0.f, 0.f, 2400.f), FRotator(-54.f, 28.f, 0.f), 1.5f,
		FLinearColor(1.f, 0.95f, 0.88f));
	FillLight = SpawnLight(W, FVector(0.f, 0.f, 2000.f), FRotator(-78.f, 210.f, 0.f), 0.45f,
		FLinearColor(0.48f, 0.62f, 1.f));
	RimLight = SpawnLight(W, FVector(0.f, 0.f, 1200.f), FRotator(-30.f, 135.f, 0.f), 0.30f,
		FLinearColor(0.72f, 0.52f, 1.f));

	// ---- 环境粒子（雨 / 雪 / 余烬 共用一套实例化网格）
	ParticleComp = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Particles"));
	ParticleComp->SetMobility(EComponentMobility::Movable);
	ParticleComp->SetStaticMesh(FSUtil::Mesh(TEXT("Cube")));
	ParticleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ParticleComp->SetCastShadow(false);
	ParticleComp->SetGenerateOverlapEvents(false);
	if (UMaterialInstanceDynamic* MID = FSUtil::MakeMID(ParticleComp, T.Particle, true, T.ParticleGlow))
	{
		ParticleComp->SetMaterial(0, MID);
	}
	AddInstanceComponent(ParticleComp);
	ParticleComp->RegisterComponent();
	ParticleComp->SetVisibility(false);
}

// ------------------------------------------------------------------ 主题装饰

void AFSArena::BuildDecorBlue()
{
	const FFSTheme& T = GetTheme(0);

	// 四角塔：柱 + 尖顶 + 悬浮法球
	for (int32 i = 0; i < 4; ++i)
	{
		const float SX = (i < 2) ? 1.f : -1.f;
		const float SY = (i % 2 == 0) ? 1.f : -1.f;
		const FVector Base(Half * SX, Half * SY, 0.f);
		FSUtil::AddShape(this, Root, TEXT("Cylinder"), Base + FVector(0.f, 0.f, 260.f),
			FVector(1.9f, 1.9f, 5.2f), T.TowerBody);
		FSUtil::AddShape(this, Root, TEXT("Cone"), Base + FVector(0.f, 0.f, 640.f),
			FVector(2.4f, 2.4f, 2.4f), T.TowerTop, true, 1.15f);
		FSUtil::AddShape(this, Root, TEXT("Sphere"), Base + FVector(0.f, 0.f, 800.f),
			FVector(0.6f, 0.6f, 0.6f), T.TowerGlow, true, 1.25f);
	}

	// 散落岩块
	// 用自有部件 SM_Shard（六棱尖晶）/ SM_Rock（不规则多面体）替换等比缩放的 Cone/Cube：
	// 两者枢轴同为几何中心、尺寸同为 100，所以 Loc 与 Scale 一行都不用动。
	FRandomStream Rnd(20261115);
	for (int32 i = 0; i < 34; ++i)
	{
		const float Ang = Rnd.FRandRange(0.f, 6.283f);
		const float Rad = Rnd.FRandRange(Half * 0.20f, Half * 0.76f);
		const FVector Loc(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 0.f);
		const float Size = Rnd.FRandRange(0.35f, 1.5f);
		if (Rnd.FRand() < 0.45f)
		{
			FSUtil::AddShape(this, Root, TEXT("Shard"), Loc + FVector(0.f, 0.f, 60.f * Size),
				FVector(Size, Size, Size * 1.6f), T.PropB,
				false, 0.f, FRotator(0.f, Rnd.FRandRange(0.f, 360.f), Rnd.FRandRange(-9.f, 9.f)));
		}
		else
		{
			FSUtil::AddShape(this, Root, TEXT("Rock"), Loc + FVector(0.f, 0.f, 40.f * Size),
				FVector(Size, Size * 0.9f, Size * 0.8f), T.PropA,
				false, 0.f, FRotator(0.f, Rnd.FRandRange(0.f, 360.f), 0.f));
		}
	}
}

void AFSArena::BuildDecorFrost()
{
	const FFSTheme& T = GetTheme(1);
	FRandomStream Rnd(77001);

	// 环绕冰峰
	for (int32 i = 0; i < 9; ++i)
	{
		const float Ang = (6.283f / 9.f) * i + Rnd.FRandRange(-0.12f, 0.12f);
		const float Rad = Rnd.FRandRange(Half * 0.34f, Half * 0.78f);
		const FVector Loc(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 0.f);
		const float H = Rnd.FRandRange(3.2f, 7.4f);
		const float R = Rnd.FRandRange(0.9f, 1.9f);
		// SM_Shard：下半截是尖的、埋在地下，露出来的正好是"石笋"轮廓
		FSUtil::AddShape(this, Root, TEXT("Shard"), Loc + FVector(0.f, 0.f, H * 50.f),
			FVector(R, R, H), T.PropA, false, 0.f,
			FRotator(Rnd.FRandRange(-7.f, 7.f), Rnd.FRandRange(0.f, 360.f), Rnd.FRandRange(-7.f, 7.f)));
		// 冰峰顶端的微光
		FSUtil::AddShape(this, Root, TEXT("Sphere"), Loc + FVector(0.f, 0.f, H * 100.f),
			FVector(0.22f, 0.22f, 0.22f), T.TowerGlow, true, 1.0f);
	}

	// 冰面裂纹：细长的冷光裂痕
	for (int32 i = 0; i < 26; ++i)
	{
		const float Ang = Rnd.FRandRange(0.f, 6.283f);
		const float Rad = Rnd.FRandRange(Half * 0.18f, Half * 0.74f);
		const FVector Loc(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 0.f);
		const float Len = Rnd.FRandRange(220.f, 720.f);
		FSUtil::AddShape(this, Root, TEXT("Cube"), Loc + FVector(0.f, 0.f, 2.4f),
			FVector(Len / 100.f, 0.045f, 0.02f), T.CircleC, true, 0.55f,
			FRotator(0.f, Rnd.FRandRange(0.f, 360.f), 0.f));
	}

	// 四角冰柱
	for (int32 i = 0; i < 4; ++i)
	{
		const float SX = (i < 2) ? 1.f : -1.f;
		const float SY = (i % 2 == 0) ? 1.f : -1.f;
		const FVector Base(Half * SX, Half * SY, 0.f);
		FSUtil::AddShape(this, Root, TEXT("Cylinder"), Base + FVector(0.f, 0.f, 300.f),
			FVector(1.4f, 1.4f, 6.0f), T.TowerBody);
		FSUtil::AddShape(this, Root, TEXT("Cone"), Base + FVector(0.f, 0.f, 740.f),
			FVector(1.9f, 1.9f, 2.6f), T.TowerTop, true, 1.05f);
	}
}

void AFSArena::BuildDecorLava()
{
	const FFSTheme& T = GetTheme(2);
	FRandomStream Rnd(51413);

	// 岩浆裂缝：从中心放射的长条发光带
	for (int32 i = 0; i < 8; ++i)
	{
		const float Ang = (6.283f / 8.f) * i + Rnd.FRandRange(-0.16f, 0.16f);
		const float Len = Rnd.FRandRange(Half * 0.30f, Half * 0.72f);
		const FVector Dir(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
		FSUtil::AddShape(this, Root, TEXT("Cube"), Dir * (Len * 0.5f) + FVector(0.f, 0.f, 2.2f),
			FVector(Len / 100.f, 0.05f, 0.02f), T.Strip, true, 0.95f,
			FRotator(0.f, FMath::RadiansToDegrees(Ang), 0.f));
	}

	// 岩浆池
	for (int32 i = 0; i < 6; ++i)
	{
		const float Ang = Rnd.FRandRange(0.f, 6.283f);
		const float Rad = Rnd.FRandRange(Half * 0.32f, Half * 0.70f);
		const FVector Loc(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 2.6f);
		const float R = Rnd.FRandRange(0.9f, 2.1f);
		FSUtil::AddShape(this, Root, TEXT("Cylinder"), Loc - FVector(0.f, 0.f, 0.4f),
			FVector(R * 1.28f, R * 1.28f, 0.02f), T.CircleB, true, 1.0f);
		FSUtil::AddShape(this, Root, TEXT("Cylinder"), Loc, FVector(R, R, 0.02f), T.CircleC, true, 1.2f);
	}

	// 黑曜石尖刺
	for (int32 i = 0; i < 22; ++i)
	{
		const float Ang = Rnd.FRandRange(0.f, 6.283f);
		const float Rad = Rnd.FRandRange(Half * 0.28f, Half * 0.74f);
		const FVector Loc(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 0.f);
		const float S = Rnd.FRandRange(0.5f, 1.7f);
		// SM_Shard 的六棱面在熔岩环境光下会显出黑白两道棱，比光滑锥更像黑曜石
		FSUtil::AddShape(this, Root, TEXT("Shard"), Loc + FVector(0.f, 0.f, 90.f * S),
			FVector(S * 0.8f, S * 0.8f, S * 2.4f), T.PropA, false, 0.f,
			FRotator(Rnd.FRandRange(-14.f, 14.f), Rnd.FRandRange(0.f, 360.f), Rnd.FRandRange(-14.f, 14.f)));
	}

	// 四角熔岩柱
	for (int32 i = 0; i < 4; ++i)
	{
		const float SX = (i < 2) ? 1.f : -1.f;
		const float SY = (i % 2 == 0) ? 1.f : -1.f;
		const FVector Base(Half * SX, Half * SY, 0.f);
		FSUtil::AddShape(this, Root, TEXT("Cylinder"), Base + FVector(0.f, 0.f, 250.f),
			FVector(1.7f, 1.7f, 5.0f), T.TowerBody);
		FSUtil::AddShape(this, Root, TEXT("Cylinder"), Base + FVector(0.f, 0.f, 520.f),
			FVector(1.3f, 1.3f, 0.22f), T.TowerGlow, true, 1.2f);
	}
}

// ------------------------------------------------------------------ 外部 CC0 道具

void AFSArena::BuildArtProps(int32 ThemeIdx)
{
	// 调试：-fsproptest 把三个道具摆在玩家眼前并染色，用于判断"看不见"到底是
	// 位置太远还是渲染/材质问题（染色同时验证材质的 Tint 参数）。
	if (FParse::Param(FCommandLine::Get(), TEXT("fsproptest")))
	{
		FSUtil::AddProp(this, Root, TEXT("survival_kit/tree"), FVector(420.f, 0.f, 0.f),
			300.f, 0.f, FLinearColor(1.0f, 0.15f, 0.10f));
		FSUtil::AddProp(this, Root, TEXT("survival_kit/box_large"), FVector(0.f, 420.f, 0.f),
			160.f, 0.f, FLinearColor(0.15f, 1.0f, 0.20f));
		FSUtil::AddProp(this, Root, TEXT("survival_kit/barrel"), FVector(-420.f, 0.f, 0.f),
			130.f, 0.f, FLinearColor(0.20f, 0.45f, 1.0f));
		UE_LOG(LogTemp, Warning, TEXT("[FSARTPROP] PROPTEST placed 3 debug props around player"));
		return;
	}

	FSUtil::SetArtTheme(ThemeIdx);

	FRandomStream Rnd(41077 + ThemeIdx * 913);

	// **视野预算**（实测，不是估算）：相机臂 1480 / 俯角 55 / 垂直 FOV 37.4°，
	// 屏幕底边到顶边对应的地面带是「玩家身后 495 → 前方 802」，更远的东西全在画面上沿
	// 之外（物件越高越靠上，反而更看不见）。所以不能靠"远处一圈"来当装饰，得按密度分三段：
	//   A 出生视野（R 470~840）：**稀疏**小碎屑，单件、高 22~40，只做地面质感，不许挡敌；
	//   B 中景（R 1080~1950）：成对散件，高 40~95，走过去才看清；
	//   C 场地边缘（R 2150~2800）：高件林墙，靠围墙站成一圈，走到边线才露全貌。
	// 密度教训：第一版把 22 簇 × 3 件全塞在 R 270~1020，半径小、角距只有 77uu，
	// 出生画面直接被石头淹没、敌人完全被挡 —— 近场必须"单件 + 稀疏"。
	int32 Placed = 0;

	// 越界/穿墙保护 + 统一入口
	auto PutAt = [&](const TCHAR* Name, float H, const FVector& P, float Yaw)
	{
		if (P.Size2D() > Half - 60.f)
		{
			return;
		}
		if (FSUtil::AddProp(this, Root, Name, P, H, Yaw))
		{
			++Placed;
		}
	};

	auto RingLoc = [&](float Ang, float Rad) { return FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad, 0.f); };

	// ---- A 段：出生视野内的小碎屑（单件、稀疏、低矮）
	// 12 件、半径从 525 起 —— 法阵半径 250，玩家出生点周围 500uu 内必须是干净地面。
	const int32 NearClusters = 12;
	for (int32 c = 0; c < NearClusters; ++c)
	{
		const float Ang = (6.2831853f / NearClusters) * c + Rnd.FRandRange(-0.22f, 0.22f);
		const float Rad = Rnd.FRandRange(Half * 0.175f, Half * 0.290f);
		const FVector P = RingLoc(Ang, Rad);
		const float Y = Rnd.FRandRange(0.f, 360.f);

		switch (ThemeIdx)
		{
		case 1:
			// 俯视视角里"趴着的东西"只会被看成地面斑块，所以近场一律用竖立件：
			// 木十字 / 十字墓碑 / 路灯柱。墓碑用 gravestone_round 那种平板会看不出来。
			if (c % 3 == 0)      { PutAt(TEXT("graveyard_kit/cross_wood"),        88.f, P, Y); }
			else if (c % 3 == 1) { PutAt(TEXT("graveyard_kit/gravestone_cross"),  80.f, P, Y); }
			else                 { PutAt(TEXT("graveyard_kit/lightpost_single"), 150.f, P, Y); }
			break;
		case 2:
			PutAt((c % 2 == 0) ? TEXT("castle_kit/rocks_large") : TEXT("castle_kit/rocks_small"),
				52.f, P, Y);
			break;
		default:
			// 不要 rock_flat / rock_flat_grass：它们又薄又宽，30uu 高的时候像地上的灰水洼
			PutAt((c % 2 == 0) ? TEXT("survival_kit/rock_c") : TEXT("survival_kit/rock_a"),
				42.f, P, Y);
			break;
		}
	}

	// ---- B 段：中景成对散件
	const int32 MidClusters = 18;
	for (int32 c = 0; c < MidClusters; ++c)
	{
		const float Ang = (6.2831853f / MidClusters) * c + Rnd.FRandRange(-0.20f, 0.20f) + 0.11f;
		const float Rad = Rnd.FRandRange(Half * 0.360f, Half * 0.650f);
		const FVector Center = RingLoc(Ang, Rad);
		const float Y1 = Rnd.FRandRange(0.f, 360.f);
		const float Y2 = Rnd.FRandRange(0.f, 360.f);

		switch (ThemeIdx)
		{
		case 1:
			PutAt(TEXT("graveyard_kit/gravestone_cross_large"), 84.f, Center, Y1);
			PutAt(TEXT("graveyard_kit/pine"),                   250.f, Center + RingLoc(Ang + 1.25f, 110.f), Y2);
			break;
		case 2:
			PutAt(TEXT("castle_kit/rocks_large"),       76.f, Center, Y1);
			PutAt(TEXT("castle_kit/tower_square_base"), 300.f, Center + RingLoc(Ang + 1.25f, 120.f), Y2);
			break;
		default:
			PutAt(TEXT("survival_kit/rock_b"), 68.f, Center, Y1);
			PutAt(TEXT("survival_kit/tree"),  230.f, Center + RingLoc(Ang + 1.25f, 110.f), Y2);
			break;
		}
	}

	// ---- C 段：场地边缘的高件林墙（贴着围墙，走到边线才看清）
	const int32 FarClusters = 36;
	for (int32 c = 0; c < FarClusters; ++c)
	{
		const float Ang = (6.2831853f / FarClusters) * c + Rnd.FRandRange(-0.055f, 0.055f) + 0.04f;
		const float Rad = Half * Rnd.FRandRange(0.717f, 0.930f);
		const FVector Center = RingLoc(Ang, Rad);
		const float Tan = FMath::RadiansToDegrees(Ang) + 90.f;
		const float Y1 = Rnd.FRandRange(0.f, 360.f);

		switch (ThemeIdx)
		{
		case 1:
			PutAt((c % 5 == 0) ? TEXT("graveyard_kit/pine_crooked") : TEXT("graveyard_kit/pine"),
				Rnd.FRandRange(300.f, 400.f), Center, Y1);
			PutAt(TEXT("graveyard_kit/iron_fence"), 118.f, Center + RingLoc(Ang + 0.055f, 150.f), Tan);
			break;
		case 2:
			PutAt((c % 6 == 0) ? TEXT("castle_kit/tower_square_base") : TEXT("castle_kit/tree_large"),
				Rnd.FRandRange(280.f, 380.f), Center, Y1);
			PutAt(TEXT("castle_kit/wall_half"), 175.f, Center + RingLoc(Ang + 0.055f, 155.f), Tan);
			break;
		default:
			PutAt((c % 4 == 0) ? TEXT("survival_kit/tree_tall") : TEXT("survival_kit/tree"),
				Rnd.FRandRange(280.f, 370.f), Center, Y1);
			PutAt(TEXT("survival_kit/fence_fortified"), 105.f, Center + RingLoc(Ang + 0.055f, 148.f), Tan);
			break;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[FSARTPROP] theme=%d near=%d mid=%d far=%d placed=%d"),
		ThemeIdx, NearClusters, MidClusters, FarClusters, Placed);
}

// ------------------------------------------------------------------ 天气与粒子

void AFSArena::TickWeatherVisuals(EFSWeather Weather, float DeltaSeconds)
{
	FLinearColor TargetKey(1.f, 0.96f, 0.90f);
	FLinearColor TargetFill(0.55f, 0.68f, 1.00f);
	FLinearColor TargetRim(0.75f, 0.55f, 1.00f);
	float KeyInt = 1.5f;
	float FillInt = 0.45f;

	switch (Weather)
	{
	case EFSWeather::Clear:
		break;
	case EFSWeather::Rain:
		TargetKey = FLinearColor(0.62f, 0.74f, 1.00f);
		TargetFill = FLinearColor(0.35f, 0.50f, 0.85f);
		TargetRim = FLinearColor(0.45f, 0.65f, 1.00f);
		KeyInt = 1.05f;
		FillInt = 0.60f;
		break;
	case EFSWeather::Storm:
		TargetKey = FLinearColor(0.45f, 0.56f, 0.95f);
		TargetFill = FLinearColor(0.25f, 0.35f, 0.75f);
		TargetRim = FLinearColor(0.70f, 0.85f, 1.00f);
		KeyInt = 0.75f;
		FillInt = 0.70f;
		break;
	case EFSWeather::BloodMoon:
		TargetKey = FLinearColor(1.00f, 0.42f, 0.34f);
		TargetFill = FLinearColor(0.60f, 0.18f, 0.22f);
		TargetRim = FLinearColor(1.00f, 0.30f, 0.20f);
		KeyInt = 1.40f;
		FillInt = 0.40f;
		break;
	}

	const float Alpha = FMath::Clamp(DeltaSeconds * 1.1f, 0.f, 1.f);
	if (KeyLight)
	{
		if (ULightComponent* LC = KeyLight->GetLightComponent())
		{
			LC->SetLightColor(FMath::Lerp(LC->GetLightColor(), TargetKey, Alpha));
			LC->SetIntensity(FMath::Lerp(LC->Intensity, KeyInt, Alpha));
		}
	}
	if (FillLight)
	{
		if (ULightComponent* LC = FillLight->GetLightComponent())
		{
			LC->SetLightColor(FMath::Lerp(LC->GetLightColor(), TargetFill, Alpha));
			LC->SetIntensity(FMath::Lerp(LC->Intensity, FillInt, Alpha));
		}
	}
	if (RimLight)
	{
		if (ULightComponent* LC = RimLight->GetLightComponent())
		{
			LC->SetLightColor(FMath::Lerp(LC->GetLightColor(), TargetRim, Alpha));
		}
	}
}

void AFSArena::UpdateParticles(float DeltaSeconds, const FVector& Center, float Wet)
{
	if (!ParticleComp)
	{
		return;
	}

	// 雨只在下雨/雷暴时出现；雪与余烬常驻（天气不好时更密）
	float Intensity = Wet;
	if (Variant == 1)
	{
		Intensity = FMath::Max(Wet * 0.9f, 0.42f);
	}
	else if (Variant == 2)
	{
		Intensity = FMath::Max(Wet * 0.75f, 0.55f);
	}

	if (Intensity <= 0.02f)
	{
		if (bParticleVisible)
		{
			bParticleVisible = false;
			ParticleComp->SetVisibility(false);
		}
		return;
	}

	const FFSTheme& T = GetTheme(Variant);
	const int32 Count = (ParticleKind == 1) ? 300 : (ParticleKind == 2 ? 190 : 240);
	const float Range = 3400.f;
	const float TopZ = 1900.f;

	if (!bParticleInit)
	{
		bParticleInit = true;
		ParticlePos.Reset();
		ParticleSeed.Reset();
		FRandomStream Rnd(9871);
		ParticleComp->ClearInstances();
		for (int32 i = 0; i < Count; ++i)
		{
			const FVector P(
				Center.X + Rnd.FRandRange(-Range, Range),
				Center.Y + Rnd.FRandRange(-Range, Range),
				Rnd.FRandRange(0.f, TopZ));
			ParticlePos.Add(P);
			ParticleSeed.Add(Rnd.FRandRange(0.f, 6.283f));
			ParticleComp->AddInstance(FTransform(FRotator::ZeroRotator, P, T.ParticleScale));
		}
	}

	for (int32 i = 0; i < ParticlePos.Num(); ++i)
	{
		FVector& P = ParticlePos[i];
		if (ParticleKind == 2)
		{
			// 余烬：缓慢上升并左右飘
			const float Spd = 220.f + 260.f * FMath::Frac(ParticleSeed[i]);
			P.Z += Spd * DeltaSeconds;
			P.X += FMath::Sin(ParticleSeed[i] + P.Z * 0.004f) * 26.f * DeltaSeconds;
			if (P.Z > TopZ)
			{
				P.Z = 0.f;
				P.X = Center.X + FMath::FRandRange(-Range, Range);
				P.Y = Center.Y + FMath::FRandRange(-Range, Range);
			}
		}
		else if (ParticleKind == 1)
		{
			// 雪：缓慢下落 + 横向摆动
			const float Spd = 210.f + 130.f * FMath::Frac(ParticleSeed[i]);
			P.Z -= Spd * DeltaSeconds;
			P.X += FMath::Sin(ParticleSeed[i] + P.Z * 0.01f) * 42.f * DeltaSeconds;
			if (P.Z < 0.f)
			{
				P.Z += TopZ;
				P.X = Center.X + FMath::FRandRange(-Range, Range);
				P.Y = Center.Y + FMath::FRandRange(-Range, Range);
			}
		}
		else
		{
			// 雨：快速下落
			P.Z -= 3400.f * DeltaSeconds;
			if (P.Z < 0.f)
			{
				P.Z += TopZ;
				P.X = Center.X + FMath::FRandRange(-Range, Range);
				P.Y = Center.Y + FMath::FRandRange(-Range, Range);
			}
		}

		if (FMath::Abs(P.X - Center.X) > Range * 1.15f || FMath::Abs(P.Y - Center.Y) > Range * 1.15f)
		{
			P.X = Center.X + FMath::FRandRange(-Range, Range);
			P.Y = Center.Y + FMath::FRandRange(-Range, Range);
		}

		ParticleComp->UpdateInstanceTransform(i,
			FTransform(FRotator::ZeroRotator, P, T.ParticleScale), false, false, false);
	}
	ParticleComp->MarkRenderStateDirty();
	if (!bParticleVisible)
	{
		bParticleVisible = true;
		ParticleComp->SetVisibility(true);
	}
}
