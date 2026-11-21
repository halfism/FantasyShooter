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
