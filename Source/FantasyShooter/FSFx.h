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

/**
 * 流派觉醒的落地表达。
 *
 * 三招共用一套"极坐标薄片"运动学：每片独立插值 角度 / 半径 / 高度 / 长度 / 厚度 / 亮度，
 * 各带延迟。于是"裂缝撕开""残影甩出""护罩收拢"只是三组不同参数，
 * 不必为三条流派各写一个 Actor 类——新增流派只要加一组参数。
 *
 * 关键取舍：所有片都是**同一个 Actor 的组件**，一次 Spawn、一次 Tick、一次 Destroy。
 * 每片单独 Spawn 一个特效 Actor 会让觉醒瞬间产生十几个 Actor 和十几个 Tick，收益不值。
 */
UCLASS()
class AFSPathFx : public AActor
{
	GENERATED_BODY()

public:
	AFSPathFx();
	virtual void Tick(float DeltaSeconds) override;

	/** 烈焰·裂地：9 道放射裂缝从脚下撕开，外端越跑越细 */
	static AFSPathFx* Cracks(UWorld* World, const FVector& Loc, const FLinearColor& Color, float Scale = 1.f);
	/** 疾影·残像：三层残影沿朝向甩出去，外圈再套一圈旋转的竖向光条 */
	static AFSPathFx* Afterimage(UWorld* World, const FVector& Loc, const FVector& Facing,
		const FLinearColor& Color);
	/** 壁垒·聚障：一圈竖板向内收拢，外面压一层扁护罩 */
	static AFSPathFx* Dome(UWorld* World, const FVector& Loc, const FLinearColor& Color,
		float Radius = 210.f);

	/** 自检：还剩几片、动画走了多久 */
	int32 GetPartCount() const { return Parts.Num(); }
	float GetAge() const { return T; }
	const TCHAR* GetKindName() const { return KindName; }

protected:
	struct FFxPart
	{
		UStaticMeshComponent* Comp = nullptr;
		UMaterialInstanceDynamic* MID = nullptr;
		float AngleA = 0.f;   // 度
		float AngleB = 0.f;
		float RadA = 0.f;     // 离中心的水平距离（uu）
		float RadB = 0.f;
		float ZA = 0.f;
		float ZB = 0.f;
		float YawA = 0.f;     // 自身朝向，决定薄片长轴指向
		float YawB = 0.f;
		float LenA = 1.f;     // 长轴缩放（×50 = uu）
		float LenB = 1.f;
		float ThickA = 1.f;
		float ThickB = 1.f;
		float HgtA = 0.02f;
		float HgtB = 0.02f;
		float Delay = 0.f;
		float Dur = 0.6f;
		float IntA = 0.9f;
		float OpA = 0.55f;
		/**
		 * 亮度平台占比。0 = 一路线性衰减（旧行为）。
		 * 觉醒的落地表达必须"先亮住、再退"：陡衰减会让 0.3 秒那张截图里只剩几团暗影，
		 * 玩家根本来不及看清"我这一招长什么样"。
		 */
		float Hold = 0.38f;
	};

	FFxPart& NewPart(const TCHAR* MeshName, const FLinearColor& Color);
	void Finish(float InTotalDur, const TCHAR* InKind);

	TArray<FFxPart> Parts;
	float T = 0.f;
	float TotalDur = 1.f;
	const TCHAR* KindName = TEXT("SIG");
};
