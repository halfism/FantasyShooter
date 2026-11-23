#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FSCommon.h"
#include "FSArena.generated.h"

class ADirectionalLight;
class UInstancedStaticMeshComponent;
class USceneComponent;

/**
 * 程序化生成的竞技场：地面、光带、围墙、装饰、三向灯光、环境粒子。
 * 共三套主题，每局随机：0 幽蓝竞技场 / 1 霜封荒原 / 2 熔岩裂谷。
 */
UCLASS()
class AFSArena : public AActor
{
	GENERATED_BODY()

public:
	AFSArena();

	void Build(float HalfSize);
	void TickWeatherVisuals(EFSWeather Weather, float DeltaSeconds);
	/** Wet: 天气的湿润度 0~1，决定雨雪强度 */
	void UpdateParticles(float DeltaSeconds, const FVector& Center, float Wet);

	const TCHAR* GetThemeNameCN() const;
	const TCHAR* GetThemeNameEN() const;
	int32 GetVariant() const { return Variant; }

	ADirectionalLight* GetKeyLight() const { return KeyLight; }

protected:
	UPROPERTY()
	USceneComponent* Root = nullptr;

	UPROPERTY()
	ADirectionalLight* KeyLight = nullptr;

	UPROPERTY()
	ADirectionalLight* FillLight = nullptr;

	UPROPERTY()
	ADirectionalLight* RimLight = nullptr;

	UPROPERTY()
	UInstancedStaticMeshComponent* ParticleComp = nullptr;

	void BuildDecorBlue();
	void BuildDecorFrost();
	void BuildDecorLava();
	/** 外部 CC0 道具（kenney.nl）。放在场内边缘环带，不进中心战场 */
	void BuildArtProps(int32 ThemeIdx);

	TArray<FVector> ParticlePos;
	TArray<float> ParticleSeed;
	float Half = 3000.f;
	int32 Variant = 0;
	int32 ParticleKind = 0;   // 0 雨 1 雪 2 余烬
	bool bParticleInit = false;
	bool bParticleVisible = false;
};
