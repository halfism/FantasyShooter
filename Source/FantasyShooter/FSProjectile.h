#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FSCommon.h"
#include "FSProjectile.generated.h"

class UStaticMeshComponent;

UCLASS()
class AFSProjectile : public AActor
{
	GENERATED_BODY()

public:
	AFSProjectile();

	virtual void Tick(float DeltaSeconds) override;

	void Launch(
		const FVector& Start, const FVector& Dir, float InSpeed, float InDamage, int32 InPierce,
		bool bInEnemyOwned, const FLinearColor& InColor, float InScale, bool bInExplosive,
		float InExpRadius, float InExpDamage, AActor* InShooter);

protected:
	void HandleHit(AActor* HitActor, const FVector& Point);
	void Explode(const FVector& Point);

	UPROPERTY()
	UStaticMeshComponent* Visual = nullptr;

	UPROPERTY()
	AActor* Shooter = nullptr;

	FVector Dir = FVector::ForwardVector;
	float Speed = 5000.f;
	float Damage = 10.f;
	int32 Pierce = 0;
	bool bEnemyOwned = false;
	bool bExplosive = false;
	float ExpRadius = 0.f;
	float ExpDamage = 0.f;
	float LifeLeft = 2.2f;
	bool bLaunched = false;

	TSet<TWeakObjectPtr<AActor>> HitActors;
};
