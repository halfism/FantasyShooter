#include "FSFx.h"
#include "FSUtil.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	/** 调试开关 -fnofx：完全关掉特效，用来判断画面上的异常发光来自哪里 */
	bool FxDisabled()
	{
		static const bool bDisabled = FParse::Param(FCommandLine::Get(), TEXT("fnofx"));
		return bDisabled;
	}
}

AFSFx::AFSFx()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

AFSFx* AFSFx::Burst(UWorld* World, const FVector& Loc, float Radius, const FLinearColor& Color,
	float Duration, float IntensityScale)
{
	if (!World || FxDisabled())
	{
		return nullptr;
	}
	AFSFx* Fx = World->SpawnActor<AFSFx>(AFSFx::StaticClass(), Loc, FRotator::ZeroRotator);
	if (Fx)
	{
		Fx->Init(Loc, Radius * 0.25f, Radius, Color, Duration, false, IntensityScale);
	}
	return Fx;
}

AFSFx* AFSFx::Shock(UWorld* World, const FVector& Loc, float Radius, const FLinearColor& Color,
	float Duration, float IntensityScale)
{
	if (!World || FxDisabled())
	{
		return nullptr;
	}
	AFSFx* Fx = World->SpawnActor<AFSFx>(AFSFx::StaticClass(), Loc, FRotator::ZeroRotator);
	if (Fx)
	{
		Fx->Init(Loc, Radius * 0.15f, Radius, Color, Duration, true, IntensityScale);
	}
	return Fx;
}

void AFSFx::Init(const FVector& Loc, float R0, float R1, const FLinearColor& Color, float Duration,
	bool bFlat, float IntensityScale)
{
	RadiusStart = R0;
	RadiusEnd = R1;
	// 半径越大越压暗，否则近距离会糊成一团白；IntensityScale 给"要大又要亮"的场合抬回来
	BaseIntensity = 0.52f * FMath::Clamp(260.f / FMath::Max(R1, 1.f), 0.45f, 1.f)
		* FMath::Max(0.1f, IntensityScale);
	Dur = FMath::Max(0.05f, Duration);
	T = 0.f;

	if (bFlat)
	{
		// 冲击盘几乎平铺在地面上，相机是斜俯视，太实就会看成一根长条
		OpacityStart = 0.17f;
		BaseIntensity *= 0.72f;
		Shell = FSUtil::AddShape(this, GetRootComponent(), TEXT("Cylinder"),
			FVector(0.f, 0.f, 4.f), FVector(1.f, 1.f, 0.02f), Color, true, BaseIntensity);
	}
	else
	{
		OpacityStart = 0.62f;
		Shell = FSUtil::AddShape(this, GetRootComponent(), TEXT("Sphere"),
			FVector::ZeroVector, FVector(1.f, 1.f, 1.f), Color, true, BaseIntensity);
	}

	if (Shell)
	{
		MID = FSUtil::MakeMIDFx(this, Color, BaseIntensity);
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("Opacity"), OpacityStart);
			Shell->SetMaterial(0, MID);
		}
	}
	SetActorLocation(Loc);
}

void AFSFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	T += DeltaSeconds;
	const float A = FMath::Clamp(T / Dur, 0.f, 1.f);
	const float Ease = 1.f - FMath::Pow(1.f - A, 2.f);
	const float R = FMath::Lerp(RadiusStart, RadiusEnd, Ease);

	if (Shell)
	{
		const float FlatZ = Shell->GetRelativeScale3D().Z;
		Shell->SetRelativeScale3D(FVector(R / 50.f, R / 50.f, FlatZ));
	}
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Intensity"), BaseIntensity * FMath::Pow(1.f - A, 1.4f));
		MID->SetScalarParameterValue(TEXT("Opacity"), OpacityStart * FMath::Pow(1.f - A, 1.8f));
	}

	if (A >= 1.f)
	{
		Destroy();
	}
}

// =====================================================================================
//  AFSPathFx：流派觉醒的落地表达
// =====================================================================================

AFSPathFx::AFSPathFx()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

AFSPathFx::FFxPart& AFSPathFx::NewPart(const TCHAR* MeshName, const FLinearColor& Color)
{
	FFxPart& P = Parts.AddDefaulted_GetRef();
	P.Comp = FSUtil::AddShape(this, GetRootComponent(), MeshName,
		FVector::ZeroVector, FVector(0.1f, 0.1f, 0.02f), Color, true, 0.9f);
	if (P.Comp)
	{
		// AddShape 给的是不淡出的 M_Glow；特效必须能压透明度，换成 M_Fx
		P.MID = FSUtil::MakeMIDFx(this, Color, 0.9f);
		if (P.MID)
		{
			P.Comp->SetMaterial(0, P.MID);
			P.MID->SetScalarParameterValue(TEXT("Opacity"), 0.f);
		}
	}
	return P;
}

void AFSPathFx::Finish(float InTotalDur, const TCHAR* InKind)
{
	TotalDur = FMath::Max(0.1f, InTotalDur);
	KindName = InKind;
	T = 0.f;
}

// ---------------------------------------------------------------- 烈焰·裂地
AFSPathFx* AFSPathFx::Cracks(UWorld* World, const FVector& Loc, const FLinearColor& Color, float Scale)
{
	if (!World || FxDisabled())
	{
		return nullptr;
	}
	AFSPathFx* Fx = World->SpawnActor<AFSPathFx>(AFSPathFx::StaticClass(), Loc, FRotator::ZeroRotator);
	if (!Fx)
	{
		return nullptr;
	}

	const float S = FMath::Clamp(Scale, 0.4f, 2.f);

	// 主裂缝：每道拆成内外两段，外段偏一个随机角度。
	// 一条笔直的辐条读成"太阳光芒"，拐一下才读成"地裂"——这 5 度是整段代码里最划算的一笔。
	const int32 N = 9;
	for (int32 i = 0; i < N; ++i)
	{
		const float Base = (360.f / N) * i + FMath::FRandRange(-13.f, 13.f);
		const float Kink = FMath::FRandRange(-16.f, 16.f);

		// 内段：从脚下附近撕到两百出头
		{
			FFxPart& P = Fx->NewPart(TEXT("Cylinder"), Color);
			P.AngleA = P.AngleB = Base;
			P.YawA = P.YawB = Base;
			P.RadA = FMath::FRandRange(30.f, 55.f) * S;
			P.RadB = FMath::FRandRange(130.f, 175.f) * S;
			P.ZA = P.ZB = 6.f;
			P.LenA = 0.45f * S;
			P.LenB = FMath::FRandRange(1.25f, 1.65f) * S;
			P.ThickA = 0.24f;
			P.ThickB = 0.13f;
			P.Delay = i * 0.014f + FMath::FRandRange(0.f, 0.03f);
			P.Dur = 0.72f;
			P.Hold = 0.42f;
			P.IntA = 0.92f;
			P.OpA = 0.94f;
		}

		// 外段：接着往里段的外端继续撕，末端收成细尖
		{
			FFxPart& P = Fx->NewPart(TEXT("Cylinder"), Color);
			P.AngleA = P.AngleB = Base + Kink * 0.5f;
			P.YawA = P.YawB = Base + Kink;
			P.RadA = FMath::FRandRange(150.f, 195.f) * S;
			P.RadB = FMath::FRandRange(265.f, 320.f) * S;
			P.ZA = P.ZB = 6.f;
			P.LenA = 1.00f * S;
			P.LenB = FMath::FRandRange(1.55f, 2.15f) * S;
			P.ThickA = 0.13f;
			P.ThickB = 0.02f;
			P.Delay = 0.04f + i * 0.014f + FMath::FRandRange(0.f, 0.04f);
			P.Dur = 0.80f;
			P.Hold = 0.40f;
			P.IntA = 0.90f;
			P.OpA = 0.92f;
		}
	}

	// 裂口崩出的火星：短、粗、一瞬即逝，负责"刚裂开"的爆点
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = FMath::FRandRange(0.f, 360.f);
		FFxPart& P = Fx->NewPart(TEXT("Cylinder"), Color);
		P.AngleA = P.AngleB = A;
		P.YawA = P.YawB = A + FMath::FRandRange(-25.f, 25.f);
		P.RadA = FMath::FRandRange(120.f, 300.f) * S;
		P.RadB = P.RadA + 70.f * S;
		P.ZA = P.ZB = 12.f;
		P.LenA = 0.14f;
		P.LenB = 0.03f;
		P.ThickA = 0.14f;
		P.ThickB = 0.05f;
		P.Delay = FMath::FRandRange(0.02f, 0.20f);
		P.Dur = 0.26f;
		P.Hold = 0.05f;
		P.IntA = 1.0f;
		P.OpA = 1.0f;
	}

	// 脚下核心：一闪就收，把视线钉在原点
	{
		FFxPart& P = Fx->NewPart(TEXT("Cylinder"), Color);
		P.RadA = 10.f;
		P.RadB = 130.f * S;
		P.ZA = P.ZB = 8.f;
		P.LenA = P.LenB = 1.f;
		P.ThickA = P.ThickB = 1.f;
		P.Dur = 0.24f;
		P.Hold = 0.10f;
		P.IntA = 1.0f;
		P.OpA = 0.80f;
	}

	Fx->Finish(1.00f, TEXT("CRACK"));
	return Fx;
}

// ---------------------------------------------------------------- 疾影·残像
AFSPathFx* AFSPathFx::Afterimage(UWorld* World, const FVector& Loc, const FVector& Facing,
	const FLinearColor& Color)
{
	if (!World || FxDisabled())
	{
		return nullptr;
	}
	AFSPathFx* Fx = World->SpawnActor<AFSPathFx>(AFSPathFx::StaticClass(), Loc, FRotator::ZeroRotator);
	if (!Fx)
	{
		return nullptr;
	}

	const float Back = Facing.Rotation().Yaw + 180.f;

	// 地面拖影：三道沿"身后"方向的扁条。位置固定、亮度递减，
	// 递减才是"刚跑过去"的读法；三团一样亮只会读成"身边有三个东西"。
	for (int32 i = 0; i < 3; ++i)
	{
		const float K = (float)i;
		const float Dim = 1.f - K * 0.28f;
		FFxPart& P = Fx->NewPart(TEXT("Cylinder"), Color);
		P.AngleA = P.AngleB = Back;
		P.YawA = P.YawB = Back;          // 长轴贴着运动方向
		P.RadA = 95.f + K * 88.f;
		P.RadB = P.RadA + 26.f;
		P.ZA = P.ZB = 5.f;
		P.LenA = 1.15f;
		P.LenB = 0.80f;
		P.ThickA = 0.46f;
		P.ThickB = 0.34f;
		P.HgtA = P.HgtB = 0.02f;
		P.Delay = K * 0.05f;
		P.Dur = 0.52f;
		P.Hold = 0.22f;
		P.IntA = 0.85f * Dim;
		P.OpA = 0.62f * Dim;
	}

	// 贴地的切向光弧：12 道一起绕 170°，这才是"快"的读法。
	// 关键不是数量而是**半径错落**：等半径的一圈在任何一帧都摆成"花瓣冠"，
	// 只有让每道起始半径不同（螺旋），看起来才像转起来的风刃。
	// 尺寸也不能省：上一版 30uu 长、5uu 宽，实机里等于没画。
	for (int32 i = 0; i < 12; ++i)
	{
		const float A0 = (360.f / 12.f) * i;
		const float R0 = 120.f + (float)(i % 4) * 58.f;
		FFxPart& P = Fx->NewPart(TEXT("Cylinder"), Color);
		P.AngleA = A0;
		P.AngleB = A0 + 170.f;
		P.YawA = A0 + 90.f;      // 切向：垂直于半径，像被甩出去的光弧
		P.YawB = A0 + 260.f;
		P.RadA = R0;
		P.RadB = R0 + 175.f;
		P.ZA = P.ZB = 8.f;
		P.LenA = 2.60f;
		P.LenB = 1.30f;
		P.ThickA = 0.16f;
		P.ThickB = 0.10f;
		P.HgtA = P.HgtB = 0.02f;
		P.Delay = i * 0.008f;
		P.Dur = 0.58f;
		P.Hold = 0.32f;
		P.IntA = 1.00f;
		P.OpA = 0.95f;
	}

	Fx->Finish(0.90f, TEXT("AFTER"));
	return Fx;
}

// ---------------------------------------------------------------- 壁垒·聚障
AFSPathFx* AFSPathFx::Dome(UWorld* World, const FVector& Loc, const FLinearColor& Color, float Radius)
{
	if (!World || FxDisabled())
	{
		return nullptr;
	}
	AFSPathFx* Fx = World->SpawnActor<AFSPathFx>(AFSPathFx::StaticClass(), Loc, FRotator::ZeroRotator);
	if (!Fx)
	{
		return nullptr;
	}

	const float R = FMath::Max(120.f, Radius);

	// 外圈 16 块竖板向内收拢。
	// 长轴必须是**切向**（Yaw = 角度 + 90）：写成径向就变成 16 根辐条，
	// 从俯视角看像一圈栏杆倒在地上，读不出"墙"。
	const int32 N = 16;
	for (int32 i = 0; i < N; ++i)
	{
		const float A = (360.f / N) * i;
		FFxPart& P = Fx->NewPart(TEXT("Cylinder"), Color);
		P.AngleA = A;
		P.AngleB = A + 18.f;
		P.YawA = A + 90.f;
		P.YawB = A + 108.f;
		P.RadA = R * 1.15f;
		P.RadB = R * 0.66f;
		P.ZA = P.ZB = -10.f;
		P.LenA = 0.46f;
		P.LenB = 0.46f;
		P.ThickA = 0.075f;
		P.ThickB = 0.055f;
		P.HgtA = 1.90f;
		P.HgtB = 1.55f;
		P.Delay = (float)(i % 3) * 0.03f;
		P.Dur = 0.75f;
		P.Hold = 0.40f;
		P.IntA = 0.88f;
		P.OpA = 0.60f;
	}

	// 内圈 8 块矮一点的板做第二道：单圈总显得单薄，两圈错开才像"层层落下的护壁"
	for (int32 i = 0; i < 8; ++i)
	{
		const float A = (360.f / 8.f) * i + 22.f;
		FFxPart& P = Fx->NewPart(TEXT("Cylinder"), Color);
		P.AngleA = A;
		P.AngleB = A + 16.f;
		P.YawA = A + 90.f;
		P.YawB = A + 106.f;
		P.RadA = R * 1.55f;
		P.RadB = R * 0.98f;
		P.ZA = P.ZB = -10.f;
		P.LenA = 0.40f;
		P.LenB = 0.40f;
		P.ThickA = 0.060f;
		P.ThickB = 0.045f;
		P.HgtA = 1.45f;
		P.HgtB = 1.15f;
		P.Delay = 0.05f + (float)(i % 2) * 0.035f;
		P.Dur = 0.68f;
		P.Hold = 0.36f;
		P.IntA = 0.62f;
		P.OpA = 0.44f;
	}

	Fx->Finish(1.05f, TEXT("DOME"));
	return Fx;
}

void AFSPathFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	T += DeltaSeconds;

	for (FFxPart& P : Parts)
	{
		if (!P.Comp)
		{
			continue;
		}
		const float Local = FMath::Clamp((T - P.Delay) / FMath::Max(0.02f, P.Dur), 0.f, 1.f);
		const float E = 1.f - FMath::Pow(1.f - Local, 2.f);
		const float Ang = FMath::DegreesToRadians(FMath::Lerp(P.AngleA, P.AngleB, E));
		const float Rad = FMath::Lerp(P.RadA, P.RadB, E);
		P.Comp->SetRelativeLocation(FVector(FMath::Cos(Ang) * Rad, FMath::Sin(Ang) * Rad,
			FMath::Lerp(P.ZA, P.ZB, E)));
		P.Comp->SetRelativeRotation(FRotator(0.f, FMath::Lerp(P.YawA, P.YawB, E), 0.f));
		P.Comp->SetRelativeScale3D(FVector(
			FMath::Lerp(P.LenA, P.LenB, E),
			FMath::Lerp(P.ThickA, P.ThickB, E),
			FMath::Lerp(P.HgtA, P.HgtB, E)));

		// 起手 10% 快速点亮，然后**平台**停留，最后收尾。
		// 旧版是 (1-Local)^1.6 一路衰减，0.3 秒时只剩 35% 亮度——
		// 实机里就是"放完什么都没看见"，这轮返工就是冲这条来的。
		const float Attack = FMath::Clamp(Local / 0.10f, 0.f, 1.f);
		const float HoldF = FMath::Clamp(P.Hold, 0.f, 0.9f);
		float Decay = 1.f;
		if (Local > HoldF)
		{
			const float Tail = FMath::Clamp((Local - HoldF) / FMath::Max(0.01f, 1.f - HoldF), 0.f, 1.f);
			Decay = FMath::Pow(1.f - Tail, 1.5f);
		}
		const float Fade = Attack * Decay;

		if (P.MID)
		{
			P.MID->SetScalarParameterValue(TEXT("Intensity"), P.IntA * Fade);
			P.MID->SetScalarParameterValue(TEXT("Opacity"), P.OpA * Fade);
		}
	}

	if (T >= TotalDur)
	{
		Destroy();
	}
}
