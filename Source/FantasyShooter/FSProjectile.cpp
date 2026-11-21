#include "FSProjectile.h"
#include "FSUtil.h"
#include "FSFx.h"
#include "FSEnemy.h"
#include "FSPlayer.h"
#include "FSGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

AFSProjectile::AFSProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

void AFSProjectile::Launch(
	const FVector& Start, const FVector& InDir, float InSpeed, float InDamage, int32 InPierce,
	bool bInEnemyOwned, const FLinearColor& InColor, float InScale, bool bInExplosive,
	float InExpRadius, float InExpDamage, AActor* InShooter)
{
	// 临时诊断：-fsnobullet 完全不生成子弹
	if (FParse::Param(FCommandLine::Get(), TEXT("fsnobullet")))
	{
		Destroy();
		return;
	}
	Dir = InDir.GetSafeNormal();
	Speed = InSpeed;
	Damage = InDamage;
	Pierce = InPierce;
	bEnemyOwned = bInEnemyOwned;
	bExplosive = bInExplosive;
	ExpRadius = InExpRadius;
	ExpDamage = InExpDamage;
	Shooter = InShooter;
	LifeLeft = FMath::Clamp(12000.f / FMath::Max(Speed, 100.f), 0.6f, 3.0f);

	SetActorLocation(Start);
	SetActorRotation(FRotationMatrix::MakeFromX(Dir).Rotator());

	const float S = FMath::Max(0.15f, InScale);
	// 弹体是"曳光条"造型。斜俯视下 1.1×0.14 已经有 110 单位长，加泛光会糊成一根奶油色木板，
	// 所以再收细一档，靠颜色而不是体积来保证可见度。
	Visual = FSUtil::AddShape(this, GetRootComponent(), TEXT("Cube"),
		FVector::ZeroVector, FVector(0.80f * S, 0.09f * S, 0.09f * S), InColor, true, 1.20f);

	SetActorTickEnabled(true);
}

void AFSProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bLaunched && Dir.SizeSquared() > 0.f)
	{
		bLaunched = true;
	}

	// 时缓：敌人的弹幕也要一起慢下来，否则"时间变慢"只慢了走位、没慢到躲弹，
	// 这招最值钱的那部分（在弹幕缝里穿过去）就等于没做
	if (bEnemyOwned)
	{
		if (const AFSGameMode* GM = AFSGameMode::Get(this))
		{
			if (GM->EnemyTimeScale < 0.999f)
			{
				DeltaSeconds *= GM->EnemyTimeScale;
			}
		}
	}

	const FVector Prev = GetActorLocation();
	const FVector Next = Prev + Dir * Speed * DeltaSeconds;

	FCollisionQueryParams Q(SCENE_QUERY_STAT(FSProj), false, this);
	if (Shooter)
	{
		Q.AddIgnoredActor(Shooter);
	}

	FHitResult Hit;
	bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Prev, Next, ECC_Pawn, Q);
	if (!bHit)
	{
		bHit = GetWorld()->LineTraceSingleByChannel(Hit, Prev, Next, ECC_WorldStatic, Q);
	}

	if (bHit)
	{
		HandleHit(Hit.GetActor(), Hit.ImpactPoint);
		return;
	}

	SetActorLocation(Next);

	LifeLeft -= DeltaSeconds;
	if (LifeLeft <= 0.f)
	{
		if (bExplosive)
		{
			Explode(Next);
		}
		Destroy();
	}
}

void AFSProjectile::HandleHit(AActor* HitActor, const FVector& Point)
{
	if (bExplosive)
	{
		Explode(Point);
		Destroy();
		return;
	}

	if (!HitActor)
	{
		Destroy();
		return;
	}

	if (bEnemyOwned)
	{
		if (AFSPlayerChar* P = Cast<AFSPlayerChar>(HitActor))
		{
			P->TakeDamage(Damage, FDamageEvent(), nullptr, Shooter);
			Destroy();
			return;
		}
		Destroy();
		return;
	}

	AFSEnemy* E = Cast<AFSEnemy>(HitActor);
	if (E)
	{
		if (HitActors.Contains(E))
		{
			SetActorLocation(Point + Dir * 20.f);
			return;
		}
		HitActors.Add(E);
		E->TakeDamage(Damage, FDamageEvent(), nullptr, this);
		if (Pierce > 0)
		{
			Pierce--;
			SetActorLocation(Point + Dir * 45.f);
			return;
		}
		Destroy();
		return;
	}

	// 撞墙
	Destroy();
}

void AFSProjectile::Explode(const FVector& Point)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AFSEnemy> It(World); It; ++It)
	{
		AFSEnemy* E = *It;
		if (!E || E->IsDead())
		{
			continue;
		}
		const float Dist = FVector::Dist(E->GetActorLocation(), Point);
		if (Dist <= ExpRadius)
		{
			const float Falloff = FMath::Clamp(1.f - (Dist / ExpRadius) * 0.55f, 0.35f, 1.f);
			E->TakeDamage(ExpDamage * Falloff, FDamageEvent(), nullptr, this);
		}
	}

	AFSFx::Burst(World, Point, ExpRadius * 0.85f, FLinearColor(1.f, 0.55f, 0.20f), 0.4f);
	AFSFx::Shock(World, Point, ExpRadius, FLinearColor(1.f, 0.35f, 0.15f), 0.5f);
}
