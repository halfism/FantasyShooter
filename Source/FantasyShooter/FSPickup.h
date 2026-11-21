#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FSCommon.h"
#include "FSPickup.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class AFSPlayerChar;

/** 掉落物：武器 / 生命 / 符石 / 金币 */
UCLASS()
class AFSPickup : public AActor
{
	GENERATED_BODY()

public:
	AFSPickup();
	virtual void Tick(float DeltaSeconds) override;

	void Setup(EFSPickupKind InKind, EFSWeapon InWeapon, int32 InValue);

	EFSPickupKind GetKind() const { return Kind; }
	bool IsCollected() const { return bCollected; }

protected:
	void Collect(AFSPlayerChar* P);

	UPROPERTY()
	USceneComponent* Root = nullptr;

	UPROPERTY()
	USceneComponent* SpinRoot = nullptr;

	UPROPERTY()
	UStaticMeshComponent* Main = nullptr;

	EFSPickupKind Kind = EFSPickupKind::Coin;
	EFSWeapon Weapon = EFSWeapon::Pistol;
	int32 Value = 1;

	float Time = 0.f;
	float Life = 24.f;
	float Radius = 150.f;
	bool bCollected = false;
};

/** 符石召唤出的守护兽 */
UCLASS()
class AFSGuardian : public AActor
{
	GENERATED_BODY()

public:
	AFSGuardian();
	virtual void Tick(float DeltaSeconds) override;

	void Init(float InDuration);

	float GetLifeLeft() const { return LifeLeft; }
	float GetLifePct() const { return Duration > 0.f ? FMath::Clamp(LifeLeft / Duration, 0.f, 1.f) : 0.f; }

protected:
	UPROPERTY()
	USceneComponent* Root = nullptr;

	UPROPERTY()
	USceneComponent* SpinRoot = nullptr;

	UPROPERTY()
	UStaticMeshComponent* Orb = nullptr;

	float Time = 0.f;
	float LifeLeft = 20.f;
	float Duration = 20.f;
	float FireCd = 0.35f;
	float Angle = 0.f;
	float OrbitR = 300.f;
};
