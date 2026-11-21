#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FSFx.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** 一次性视觉特效：扩散淡出的发光球 / 冲击圆盘 */
UCLASS()
class AFSFx : public AActor
{
	GENERATED_BODY()

public:
	AFSFx();
	virtual void Tick(float DeltaSeconds) override;

	/** 爆散球 */
	static AFSFx* Burst(UWorld* World, const FVector& Loc, float Radius, const FLinearColor& Color,
		float Duration = 0.35f, float IntensityScale = 1.f);
	/**
	 * 地面冲击盘。
	 * IntensityScale：半径大的特效会自动压暗（避免糊成一团白），
	 * 但"流派觉醒"这类要大又要亮的事件需要手动抬回来。
	 */
	static AFSFx* Shock(UWorld* World, const FVector& Loc, float Radius, const FLinearColor& Color,
		float Duration = 0.45f, float IntensityScale = 1.f);

protected:
	void Init(const FVector& Loc, float R0, float R1, const FLinearColor& Color, float Duration,
		bool bFlat, float IntensityScale = 1.f);

	UPROPERTY()
	UStaticMeshComponent* Shell = nullptr;

	UPROPERTY()
	UMaterialInstanceDynamic* MID = nullptr;

	float T = 0.f;
	float Dur = 0.35f;
	float RadiusStart = 10.f;
	float RadiusEnd = 100.f;
	float BaseIntensity = 1.15f;
	/** 起始不透明度：地面冲击盘要远比爆散球透明，否则斜视角下会变成一根奶白色长条 */
	float OpacityStart = 0.82f;
};
