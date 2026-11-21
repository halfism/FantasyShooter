#include "FSPickup.h"
#include "FSUtil.h"
#include "FSFx.h"
#include "FSAudio.h"
#include "FSProjectile.h"
#include "FSPlayer.h"
#include "FSGameMode.h"
#include "FSEnemy.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

// ------------------------------------------------------------------ 掉落物

AFSPickup::AFSPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	SpinRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Spin"));
	SpinRoot->SetupAttachment(Root);
	SpinRoot->SetMobility(EComponentMobility::Movable);
}

void AFSPickup::Setup(EFSPickupKind InKind, EFSWeapon InWeapon, int32 InValue)
{
	Kind = InKind;
	Weapon = InWeapon;
	Value = FMath::Max(1, InValue);

	switch (Kind)
	{
	case EFSPickupKind::Weapon:
	{
		const FFSWeaponDef& W = FSDefs::Weapon(Weapon);
		// 自发光强度别超过 1.2：M_Glow 是 Color×Intensity 后直接输出，超过 1 会被裁成白色
		Main = FSUtil::AddShape(this, SpinRoot, TEXT("Cube"),
			FVector(0.f, 0.f, 96.f), FVector(0.54f, 0.54f, 0.54f), W.Color, true, 1.15f);
		FSUtil::AddShape(this, SpinRoot, TEXT("Cylinder"),
			FVector(0.f, 0.f, 4.f), FVector(1.5f, 1.5f, 0.03f), W.Color, true, 1.0f);
		// 白色标记条原来 90 单位长，斜俯视 + 泛光下会被看成一块白色木板，收短到 34
		FSUtil::AddShape(this, SpinRoot, TEXT("Cube"),
			FVector(0.f, 0.f, 96.f), FVector(0.34f, 0.075f, 0.075f), FLinearColor(0.92f, 0.96f, 1.f), true, 0.9f,
			FRotator(0.f, 0.f, 45.f));
		Radius = 175.f;
		break;
	}
	case EFSPickupKind::Health:
	{
		Main = FSUtil::AddShape(this, SpinRoot, TEXT("Cube"),
			FVector(0.f, 0.f, 90.f), FVector(0.44f, 0.44f, 0.44f), FSPal::Green, true, 1.1f);
		FSUtil::AddShape(this, SpinRoot, TEXT("Cube"),
			FVector(0.f, 0.f, 90.f), FVector(0.40f, 0.115f, 0.115f), FLinearColor(0.92f, 1.f, 0.94f), true, 0.9f);
		FSUtil::AddShape(this, SpinRoot, TEXT("Cube"),
			FVector(0.f, 0.f, 90.f), FVector(0.115f, 0.115f, 0.40f), FLinearColor(0.92f, 1.f, 0.94f), true, 0.9f);
		FSUtil::AddShape(this, SpinRoot, TEXT("Cylinder"),
			FVector(0.f, 0.f, 4.f), FVector(1.2f, 1.2f, 0.03f), FSPal::Green, true, 0.9f);
		Radius = 165.f;
		break;
	}
	case EFSPickupKind::Rune:
	{
		Main = FSUtil::AddShape(this, SpinRoot, TEXT("Sphere"),
			FVector(0.f, 0.f, 110.f), FVector(0.56f, 0.56f, 0.56f), FSPal::Magic, true, 1.2f);
		FSUtil::AddShape(this, SpinRoot, TEXT("Cylinder"),
			FVector(0.f, 0.f, 110.f), FVector(1.35f, 1.35f, 0.02f), FSPal::Purple, true, 1.0f,
			FRotator(24.f, 0.f, 0.f));
		FSUtil::AddShape(this, SpinRoot, TEXT("Cylinder"),
			FVector(0.f, 0.f, 4.f), FVector(1.3f, 1.3f, 0.03f), FSPal::Purple, true, 0.85f);
		Radius = 175.f;
		Life = 40.f;
		break;
	}
	case EFSPickupKind::Coin:
	default:
	{
		Main = FSUtil::AddShape(this, SpinRoot, TEXT("Cube"),
			FVector(0.f, 0.f, 44.f), FVector(0.28f, 0.10f, 0.28f), FSPal::Gold, true, 1.05f,
			FRotator(0.f, 0.f, 45.f));
		Radius = 140.f;
		Life = 22.f;
		break;
	}
	}

	Time = FMath::FRandRange(0.f, 6.f);
}

void AFSPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Time += DeltaSeconds;
	Life -= DeltaSeconds;

	if (SpinRoot)
	{
		SpinRoot->SetRelativeRotation(FRotator(0.f, Time * (Kind == EFSPickupKind::Coin ? 220.f : 120.f), 0.f));
		const float Bob = FMath::Sin(Time * 3.2f) * 9.f;
		SpinRoot->SetRelativeLocation(FVector(0.f, 0.f, Bob));
	}

	if (Life <= 0.f)
	{
		Destroy();
		return;
	}

	if (bCollected)
	{
		return;
	}

	// 金币 / 符石 靠近时被吸过来。半径与吸速都吃「汲取」赐福的倍率
	if (Kind == EFSPickupKind::Coin || Kind == EFSPickupKind::Rune)
	{
		if (AFSGameMode* MAGM = AFSGameMode::Get(this))
		{
			if (AFSPlayerChar* MAP = MAGM->GetPlayerChar())
			{
				FVector ToP = MAP->GetActorLocation() - GetActorLocation();
				ToP.Z = 0.f;
				const float D2 = ToP.Size();
				const float MagScale = MAP->GetMagnetScale();
				const float MagR = 760.f * MagScale;
				if (D2 < MagR)
				{
					ToP.Normalize();
					const float Pull = FMath::Lerp(1400.f, 260.f, D2 / MagR) * MagScale;
					AddActorWorldOffset(ToP * Pull * DeltaSeconds);
				}
			}
		}
	}

	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		if (AFSPlayerChar* P = GM->GetPlayerChar())
		{
			if (P->IsAlive())
			{
				FVector D = P->GetActorLocation() - GetActorLocation();
				D.Z = 0.f;
				if (D.Size() < Radius * P->GetMagnetScale())
				{
					Collect(P);
				}
			}
		}
	}
}

void AFSPickup::Collect(AFSPlayerChar* P)
{
	bCollected = true;
	if (!P)
	{
		Destroy();
		return;
	}

	FLinearColor FxColor = FSPal::Gold;
	switch (Kind)
	{
	case EFSPickupKind::Weapon:
		P->TakeWeapon(Weapon);
		FSAudio::Play2D(this, EFSSfx::Upgrade, 0.9f);
		FxColor = FSDefs::Weapon(Weapon).Color;
		break;
	case EFSPickupKind::Health:
		P->Heal(25.f);
		FSAudio::Play2D(this, EFSSfx::Heal, 0.85f);
		FxColor = FSPal::Green;
		break;
	case EFSPickupKind::Rune:
		if (AFSGameMode* GM = AFSGameMode::Get(this))
		{
			GM->OnRune();
		}
		FSAudio::Play2D(this, EFSSfx::Rune, 0.9f);
		FxColor = FSPal::Magic;
		break;
	case EFSPickupKind::Coin:
		if (AFSGameMode* GM = AFSGameMode::Get(this))
		{
			GM->AddCoins(Value);
		}
		FSAudio::Play2D(this, EFSSfx::Coin, 0.6f, FMath::FRandRange(0.95f, 1.12f));
		FxColor = FSPal::Gold;
		break;
	}

	AFSFx::Burst(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 60.f), 150.f, FxColor, 0.25f);
	Destroy();
}

// ------------------------------------------------------------------ 守护兽

AFSGuardian::AFSGuardian()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	SpinRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Spin"));
	SpinRoot->SetupAttachment(Root);
	SpinRoot->SetMobility(EComponentMobility::Movable);
}

void AFSGuardian::Init(float InDuration)
{
	Duration = FMath::Max(5.f, InDuration);
	LifeLeft = Duration;
	Time = 0.f;
	FireCd = 0.2f;

	if (Orb)
	{
		Orb->DestroyComponent();
	}
	// 发光强度必须控制在"颜色×强度 ≤ 1"以内，否则紫球会被冲到纯白，看不出是守护兽
	// 圆盘半径也收小一点，200 单位的大盘叠在一起会糊成一坨白斑
	Orb = FSUtil::AddShape(this, SpinRoot, TEXT("Sphere"), FVector(0.f, 0.f, 150.f),
		FVector(0.72f, 0.72f, 0.72f), FSPal::Magic, true, 1.45f);
	FSUtil::AddShape(this, SpinRoot, TEXT("Cylinder"), FVector(0.f, 0.f, 150.f),
		FVector(1.45f, 1.45f, 0.02f), FSPal::Purple, true, 1.35f, FRotator(20.f, 0.f, 0.f));
	FSUtil::AddShape(this, SpinRoot, TEXT("Cylinder"), FVector(0.f, 0.f, 150.f),
		FVector(1.45f, 1.45f, 0.02f), FSPal::Cyan, true, 1.35f, FRotator(-20.f, 0.f, 0.f));
	FSUtil::AddShape(this, SpinRoot, TEXT("Cone"), FVector(0.f, 0.f, 195.f),
		FVector(0.55f, 0.55f, 0.8f), FSPal::Magic, true, 1.45f);

	AFSFx::Shock(GetWorld(), GetActorLocation(), 380.f, FSPal::Magic, 0.55f);
}

void AFSGuardian::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Time += DeltaSeconds;
	LifeLeft -= DeltaSeconds;

	AFSGameMode* GM = AFSGameMode::Get(this);
	AFSPlayerChar* P = GM ? GM->GetPlayerChar() : nullptr;

	if (LifeLeft <= 0.f || !P || !P->IsAlive())
	{
		AFSFx::Burst(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 150.f), 420.f, FSPal::Magic, 0.4f);
		Destroy();
		return;
	}

	Angle += DeltaSeconds * 2.1f;
	const FVector Center = P->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * OrbitR;
	SetActorLocation(FVector(Center.X, Center.Y, 0.f));

	if (SpinRoot)
	{
		SpinRoot->SetRelativeRotation(FRotator(0.f, Time * 160.f, 0.f));
	}

	// 自动索敌
	FireCd -= DeltaSeconds;
	if (FireCd > 0.f)
	{
		return;
	}

	AFSEnemy* Best = nullptr;
	float BestDist = 3200.f;
	for (const TWeakObjectPtr<AFSEnemy>& W : GM->Enemies)
	{
		AFSEnemy* E = W.Get();
		if (!E || E->IsDead())
		{
			continue;
		}
		const float D = FVector::Dist2D(E->GetActorLocation(), GetActorLocation());
		if (D < BestDist)
		{
			BestDist = D;
			Best = E;
		}
	}

	if (Best)
	{
		FireCd = 0.42f;
		const FVector Origin = GetActorLocation() + FVector(0.f, 0.f, 150.f);
		FVector Dir = Best->GetActorLocation() - Origin;
		Dir.Z = 0.f;
		Dir.Normalize();
		if (AFSProjectile* Proj = GetWorld()->SpawnActor<AFSProjectile>(AFSProjectile::StaticClass(), Origin, FRotator::ZeroRotator))
		{
			Proj->Launch(Origin, Dir, 9000.f, 48.f, 2, false, FSPal::Magic, 1.5f, false, 0.f, 0.f, this);
		}
		FSAudio::Play(this, EFSSfx::GuardianShot, Origin, 0.55f, FMath::FRandRange(0.94f, 1.08f));
		AFSFx::Burst(GetWorld(), Origin, 90.f, FSPal::Magic, 0.12f);
	}
}
