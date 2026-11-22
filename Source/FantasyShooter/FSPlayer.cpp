#include "FSPlayer.h"
#include "FSUtil.h"
#include "FSFx.h"
#include "FSAudio.h"
#include "FSProjectile.h"
#include "FSGameMode.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"

// ------------------------------------------------------------------ 角色

AFSPlayerChar::AFSPlayerChar()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = false;
	Move->MaxFlySpeed = 660.f;
	Move->MaxAcceleration = 10000.f;
	Move->BrakingDecelerationFlying = 6000.f;
	Move->AirControl = 1.f;
	Move->GroundFriction = 8.f;
	Move->JumpZVelocity = 0.f;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->SetUsingAbsoluteRotation(true);
	SpringArm->TargetArmLength = 1480.f;
	SpringArm->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f));
	SpringArm->bDoCollisionTest = true;   // 只对竞技场四周的相机挡板生效，见 FSArena 建墙处
	SpringArm->ProbeSize = 24.f;
	SpringArm->bEnableCameraLag = true;
	// 9 的滞后在冲刺时会把人甩到屏幕边缘：0.16 秒位移一大截，镜头要追 0.3 秒才回来，
	// 那半秒里既看不清自己的位置也看不清前方。提到 16 保留一点拖尾的手感，但不至于丢人。
	SpringArm->CameraLagSpeed = 16.f;
	SpringArm->CameraLagMaxDistance = 700.f;

	// 调试：-fscam=<臂长>[,<俯角>]，把镜头拉远拉高，用来整只拍下体型大的 BOSS。
	// 只在该开关存在时生效，正常玩法不受影响。
	{
		FString CamArg;
		if (FParse::Value(FCommandLine::Get(), TEXT("fscam="), CamArg))
		{
			TArray<FString> Bits;
			CamArg.ParseIntoArray(Bits, TEXT(","), true);
			if (Bits.Num() >= 1 && Bits[0].Len() > 0)
			{
				SpringArm->TargetArmLength = FMath::Clamp(FCString::Atof(*Bits[0]), 600.f, 6000.f);
			}
			if (Bits.Num() >= 2 && Bits[1].Len() > 0)
			{
				CamPitchBase = FMath::Clamp(-FCString::Atof(*Bits[1]), -89.f, -20.f);
			}
		}
	}

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = 62.f;
	Camera->bUsePawnControlRotation = false;

	// 必须在 -fscam 解析之后抓基准：开招推近是在基准上做减法，
	// 拿构造函数里的 1480 当基准会把调试臂长直接顶回去。
	CamBaseArm = SpringArm->TargetArmLength;
	CamBaseFov = Camera->FieldOfView;

	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetVisibility(false);
		SkelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AFSPlayerChar::BeginPlay()
{
	Super::BeginPlay();

	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	BuildBody();
}

void AFSPlayerChar::BuildBody()
{
	const float G = -92.f; // 胶囊中心到底面

	auto Add = [this](const TCHAR* MeshName, const FVector& Loc, const FVector& Scale,
		const FLinearColor& Color, bool bGlow = false, float Glow = 4.f,
		const FRotator& Rot = FRotator::ZeroRotator)
	{
		if (UStaticMeshComponent* C = FSUtil::AddShape(this, GetCapsuleComponent(), MeshName, Loc, Scale, Color, bGlow, Glow, Rot))
		{
			FFSBodyPart P;
			P.Comp = C;
			P.Color = Color;
			P.bGlow = bGlow;
			P.Glow = Glow;
			Parts.Add(P);
			return C;
		}
		return static_cast<UStaticMeshComponent*>(nullptr);
	};

	// 脚下光环
	HaloOuter = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cylinder"),
		FVector(0.f, 0.f, G + 1.6f), FVector(1.62f, 1.62f, 0.02f),
		FLinearColor(0.16f, 0.62f, 0.85f), true, 1.0f);
	// 中间压一层暗色，把整块圆盘掏成"环"，否则脚下是一坨发光圆饼
	HaloInner = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cylinder"),
		FVector(0.f, 0.f, G + 3.0f), FVector(1.34f, 1.34f, 0.02f),
		FLinearColor(0.012f, 0.014f, 0.020f), true, 1.0f);

	// 觉醒才亮的外圈与碎晶：先建好藏起来，觉醒时改色显示。
	// 每一件都靠 SetShapeColor 换色，不需要为三条流派各准备一套模型。
	// 外圈同样要"亮盘 + 暗盘"两层夹出一条细环，否则半径 115 的实心亮盘会盖掉半屏地面。
	AwakenRing = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cylinder"),
		FVector(0.f, 0.f, G + 1.2f), FVector(1.78f, 1.78f, 0.02f),
		FSDefs::PathColor(EFSPath::Fury), true, 0.55f);
	if (AwakenRing)
	{
		AwakenRing->SetVisibility(false, true);
	}
	AwakenRingCore = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cylinder"),
		FVector(0.f, 0.f, G + 1.4f), FVector(1.58f, 1.58f, 0.02f),
		FLinearColor(0.012f, 0.014f, 0.020f), true, 0.6f);
	if (AwakenRingCore)
	{
		AwakenRingCore->SetVisibility(false, true);
	}

	// 残影窗口的外显：一圈比觉醒环更大的细环，随剩余时间向内收。
	// 准星弧只有"盯着准星"的玩家才看得到，而俯视视角下注意力在角色身上，
	// 所以真正承担读秒的是脚下这一圈。同样用"亮盘 + 暗芯"夹出细环。
	SurgeRing = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cylinder"),
		FVector(0.f, 0.f, G + 0.8f), FVector(2.35f, 2.35f, 0.02f),
		FLinearColor(0.55f, 1.00f, 1.00f), true, 0.7f);
	if (SurgeRing)
	{
		SurgeRing->SetVisibility(false, true);
	}
	SurgeRingCore = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cylinder"),
		FVector(0.f, 0.f, G + 1.0f), FVector(2.13f, 2.13f, 0.02f),
		FLinearColor(0.012f, 0.014f, 0.020f), true, 0.6f);
	if (SurgeRingCore)
	{
		SurgeRingCore->SetVisibility(false, true);
	}

	// 碎晶：在角色腰际绕圈。太矮会被脚下光环的泛光吃掉，太亮又会糊成白点，
	// 所以位置抬到 80、发光只给 2.2。
	for (int32 i = 0; i < 3; ++i)
	{
		UStaticMeshComponent* Shard = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cone"),
			FVector(0.f, 0.f, G + 80.f), FVector(0.34f, 0.34f, 0.72f),
			FSDefs::PathColor(EFSPath::Fury), true, 0.22f);
		if (Shard)
		{
			Shard->SetVisibility(false, true);
			AwakenShards.Add(Shard);
		}
	}

	// 腿
	Add(TEXT("Cylinder"), FVector(0.f, -21.f, G + 36.f), FVector(0.23f, 0.23f, 0.72f), FSPal::ClothDark);
	Add(TEXT("Cylinder"), FVector(0.f, 21.f, G + 36.f), FVector(0.23f, 0.23f, 0.72f), FSPal::ClothDark);
	Add(TEXT("Cube"), FVector(6.f, -21.f, G + 7.f), FVector(0.30f, 0.23f, 0.13f), FLinearColor(0.12f, 0.13f, 0.16f));
	Add(TEXT("Cube"), FVector(6.f, 21.f, G + 7.f), FVector(0.30f, 0.23f, 0.13f), FLinearColor(0.12f, 0.13f, 0.16f));

	// 躯干
	Add(TEXT("Cylinder"), FVector(0.f, 0.f, -26.f), FVector(0.66f, 0.58f, 0.68f), FLinearColor(0.22f, 0.68f, 0.98f));
	Add(TEXT("Cube"), FVector(0.f, 0.f, -22.f), FVector(0.66f, 0.58f, 0.07f), FSPal::Gold, true, 1.2f);
	Add(TEXT("Cube"), FVector(0.f, 0.f, 20.f), FVector(0.60f, 0.52f, 0.34f), FLinearColor(0.30f, 0.80f, 1.00f));

	// 肩
	Add(TEXT("Cube"), FVector(0.f, -36.f, 34.f), FVector(0.34f, 0.30f, 0.22f), FLinearColor(0.16f, 0.62f, 0.92f), true, 1.4f);
	Add(TEXT("Cube"), FVector(0.f, 36.f, 34.f), FVector(0.34f, 0.30f, 0.22f), FLinearColor(0.16f, 0.62f, 0.92f), true, 1.4f);

	// 手臂
	Add(TEXT("Cylinder"), FVector(14.f, -40.f, 8.f), FVector(0.14f, 0.14f, 0.44f), FSPal::Skin, false, 0.f, FRotator(0.f, 0.f, 22.f));
	Add(TEXT("Cylinder"), FVector(16.f, 40.f, 10.f), FVector(0.14f, 0.14f, 0.44f), FSPal::Skin, false, 0.f, FRotator(0.f, 0.f, 18.f));

	// 头
	Add(TEXT("Sphere"), FVector(0.f, 0.f, 62.f), FVector(0.50f, 0.50f, 0.52f), FSPal::Skin);
	Add(TEXT("Cone"), FVector(-4.f, 0.f, 92.f), FVector(0.52f, 0.52f, 0.34f), FLinearColor(0.14f, 0.46f, 0.72f));
	Add(TEXT("Cone"), FVector(-4.f, 0.f, 112.f), FVector(0.20f, 0.20f, 0.22f), FLinearColor(0.70f, 0.96f, 1.00f), true, 2.4f);
	Add(TEXT("Cube"), FVector(23.f, -10.f, 66.f), FVector(0.06f, 0.09f, 0.05f), FLinearColor(0.65f, 0.95f, 1.f), true, 2.2f);
	Add(TEXT("Cube"), FVector(23.f, 10.f, 66.f), FVector(0.06f, 0.09f, 0.05f), FLinearColor(0.65f, 0.95f, 1.f), true, 2.2f);

	// 披风
	Add(TEXT("Cube"), FVector(-33.f, 0.f, 0.f), FVector(0.07f, 0.40f, 0.64f), FLinearColor(0.08f, 0.30f, 0.62f));

	// 枪
	GunMesh = Add(TEXT("Cube"), FVector(30.f, 30.f, 16.f), FVector(0.62f, 0.15f, 0.15f), FLinearColor(0.20f, 0.22f, 0.28f));
	Add(TEXT("Cube"), FVector(62.f, 30.f, 16.f), FVector(0.22f, 0.09f, 0.09f), FSPal::Gold, true, 1.3f);
	Add(TEXT("Cube"), FVector(22.f, 30.f, 22.f), FVector(0.16f, 0.11f, 0.10f), FLinearColor(0.14f, 0.15f, 0.19f));

	// 主动技的地面光环：平时全隐藏，施放时才亮出来
	BuildAbilityFx();
}

FVector AFSPlayerChar::MuzzleLocation() const
{
	return GetActorTransform().TransformPosition(FVector(78.f, 30.f, -22.f));
}

void AFSPlayerChar::SetMoveInput(const FVector2D& In)
{
	MoveInput = In.GetSafeNormal();
}

void AFSPlayerChar::SetAimWorld(const FVector& In)
{
	AimPoint = In;
	const FVector D = In - GetActorLocation();
	const float Yaw = D.Rotation().Yaw;
	AimYaw = FMath::FInterpTo(AimYaw, Yaw, GetWorld()->GetDeltaSeconds(), 26.f);
	SetActorRotation(FRotator(0.f, AimYaw, 0.f));
}

void AFSPlayerChar::SetFiring(bool b)
{
	bFiring = b && IsAlive();
}

void AFSPlayerChar::TryDash()
{
	if (!IsAlive() || DashCooldown > 0.f)
	{
		return;
	}
	DashDir = FVector(MoveInput.X, MoveInput.Y, 0.f);
	if (DashDir.SizeSquared() < 0.01f)
	{
		DashDir = FVector::ForwardVector.RotateAngleAxis(AimYaw, FVector::UpVector);
	}
	DashDir.Normalize();
	DashTime = 0.16f;
	DashCooldown = DashCdMax * DashCdScale;
	Invuln = FMath::Max(Invuln, 0.40f);

	// 疾影觉醒「残影」：闪避结束后的 0.8 秒内射速 +35%
	if (IsPathAwakened(EFSPath::Swift))
	{
		DashSurge = DashSurgeMax;
		SurgeTick = 0.f;
	}

	AFSFx::Shock(GetWorld(), GetActorLocation() - FVector(0.f, 0.f, 84.f), 220.f, FSPal::Cyan, 0.35f);
	FSAudio::Play(this, EFSSfx::Dash, GetActorLocation(), 1.0f);
	AddShake(3.f);
}

// ------------------------------------------------------------------ 流派主动技

int32 AFSPlayerChar::DamageAroundSelf(float Radius, float Damage)
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return 0;
	}

	// 先把目标收进数组再逐个结算：TakeDamage 可能直接打死敌人并触发分裂 / 掉落，
	// 边迭代 TActorIterator 边销毁是自找麻烦。
	TArray<AFSEnemy*> Targets;
	for (TActorIterator<AFSEnemy> It(W); It; ++It)
	{
		AFSEnemy* E = *It;
		if (!E || E->IsDead())
		{
			continue;
		}
		// 判定要算上体型：大块头压着半边身子站在圈边上，不能因为圆心远就放过
		const float D = FVector::Dist2D(E->GetActorLocation(), GetActorLocation());
		if (D - E->GetRadius() <= Radius)
		{
			Targets.Add(E);
		}
	}

	// 自己的招式不给自己充能：否则焚场一边烧一边攒，下一发就白送了
	bArtDamageLock = true;
	int32 Hits = 0;
	for (AFSEnemy* E : Targets)
	{
		if (!IsValid(E) || E->IsDead())
		{
			continue;
		}
		const float D = FVector::Dist2D(E->GetActorLocation(), GetActorLocation());
		const float Falloff = FMath::Clamp(1.f - (D / FMath::Max(Radius, 1.f)) * 0.55f, 0.45f, 1.f);
		E->TakeDamage(Damage * Falloff, FDamageEvent(), nullptr, this);
		++Hits;
	}
	bArtDamageLock = false;
	return Hits;
}

EFSAbility AFSPlayerChar::GetAbilityKind() const
{
	// 取"叠得最多"的那条流派。平手时按 Fury→Swift→Ward 的固定顺序，
	// 所以同一套赐福组合永远绑同一个招，不会因为选卡先后摇摆。
	int32 Best = -1;
	int32 BestSt = 0;
	for (int32 i = 0; i < (int32)EFSPath::Count; ++i)
	{
		if (PathStacks[i] > BestSt)
		{
			BestSt = PathStacks[i];
			Best = i;
		}
	}
	return (Best < 0) ? EFSAbility::None : FSDefs::AbilityOfPath((EFSPath)Best);
}

bool AFSPlayerChar::IsAbilityAwakened() const
{
	const EFSAbility A = (ActiveAbility != EFSAbility::None) ? ActiveAbility : GetAbilityKind();
	return (A != EFSAbility::None) && IsPathAwakened(FSDefs::AbilityPath(A));
}

void AFSPlayerChar::DebugSetPathStacks(EFSPath P, int32 N)
{
	const int32 I = (int32)P;
	if (I < 0 || I >= (int32)EFSPath::Count)
	{
		return;
	}
	PathStacks[I] = FMath::Clamp(N, 0, 9);
	if (PathStacks[I] >= FSDefs::PathAwakenStacks && AwakenPath < 0)
	{
		AwakenPath = I;
	}
	RefreshAwakenBody();
}

float AFSPlayerChar::GetAbilityPct() const
{
	// 一个函数喂一条进度条：没放的时候显示"攒了多少"，放出去了显示"还剩多久"。
	// 两段语义不同但位置相同，玩家不需要再学第二种读法。
	if (AbilityTimeLeft > 0.f)
	{
		return FMath::Clamp(AbilityTimeLeft / FMath::Max(AbilityDuration, 0.01f), 0.f, 1.f);
	}
	return GetAbilityChargePct();
}

float AFSPlayerChar::GetAbilityDmgMul() const
{
	if (AbilityTimeLeft <= 0.f || ActiveAbility != EFSAbility::Pyre)
	{
		return 1.f;
	}
	return IsAbilityAwakened() ? 1.15f : 1.10f;
}

float AFSPlayerChar::GetAbilityRofMul() const
{
	// 时缓觉醒额外给射速：敌人慢了、自己更快，"时间差"才真的变成输出窗口
	if (AbilityTimeLeft <= 0.f || ActiveAbility != EFSAbility::SlowTime || !IsAbilityAwakened())
	{
		return 1.f;
	}
	return 1.20f;
}

void AFSPlayerChar::AddAbilityCharge(float DamageDealt)
{
	if (!IsAlive() || DamageDealt <= 0.f || AbilityCharge >= AbilityChargeMax)
	{
		return;
	}
	// 招式进行中与充能冷却期都不攒条。
	// 只卡冷却不够——焚场有 4.4 秒，这期间照常开火就足够把条重新充满，
	// 结果是"招一结束立刻再放一次"，冷却形同摆设。
	if (AbilityLock > 0.f || IsAbilityActive())
	{
		return;
	}
	// 觉醒后充得更快：大招既然变成"能解局的招"，就该更常驻在手边
	const float Gain = DamageDealt * AbilityChargePerDamage * (IsAbilityAwakened() ? 1.25f : 1.f);
	AbilityCharge = FMath::Min(AbilityChargeMax, AbilityCharge + Gain);
}

float AFSPlayerChar::DebugGetArtRadius() const
{
	// Cylinder 原始半径 50，缩放 X 乘回去就是世界半径
	return ArtRing ? ArtRing->GetRelativeScale3D().X * 50.f : 0.f;
}

FLinearColor AFSPlayerChar::DebugGetArtColor() const
{
	FLinearColor Out(0.f, 0.f, 0.f, 1.f);
	if (ArtRing)
	{
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(ArtRing->GetMaterial(0)))
		{
			M->GetVectorParameterValue(TEXT("Color"), Out);
		}
	}
	return Out;
}

void AFSPlayerChar::BuildAbilityFx(){
	// 主动技是"正在发生的事"，必须压在所有常驻光环之上：觉醒外圈在 G+1.2、
	// 内层掏空的暗盘在 G+3.0，ArtRing 放在 0.8 会被它们整个盖住（壁垒环 128uu
	// 正好落在 HaloInner 的 134uu 以内，实测一寸都看不见）。
	const float G = -92.f;
	ArtRing = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cylinder"),
		FVector(0.f, 0.f, G + 3.6f), FVector(1.f, 1.f, 0.02f),
		FLinearColor(0.20f, 0.20f, 0.20f), true, 0.85f);
	ArtCore = FSUtil::AddShape(this, GetCapsuleComponent(), TEXT("Cylinder"),
		FVector(0.f, 0.f, G + 4.2f), FVector(0.94f, 0.94f, 0.02f),
		FLinearColor(0.012f, 0.014f, 0.020f), true, 0.90f);
	if (ArtRing) { ArtRing->SetVisibility(false, true); }
	if (ArtCore) { ArtCore->SetVisibility(false, true); }
}

void AFSPlayerChar::TryAbility()
{
	if (!IsAlive() || IsAbilityActive())
	{
		return;
	}

	const EFSAbility A = GetAbilityKind();
	if (A == EFSAbility::None)
	{
		// 前两波还没选流派：这一按不能说"没反应"，否则玩家会以为是键坏了
		AddNotice(FSUtil::L(TEXT("先选一条流派，才会得到对应的主动技"),
			TEXT("PICK A PATH TO BIND AN ART")), 1.8f);
		FSAudio::Play2D(this, EFSSfx::Beep, 0.45f);
		return;
	}
	if (AbilityCharge < AbilityChargeMax)
	{
		FSAudio::Play2D(this, EFSSfx::Beep, 0.30f);
		return;
	}

	CastAbility(A);
}

void AFSPlayerChar::CastAbility(EFSAbility A)
{
	const EFSPath Path = FSDefs::AbilityPath(A);
	const bool bAwe = IsPathAwakened(Path);
	const FLinearColor Col = FSDefs::PathColor(Path);

	ActiveAbility = A;
	AbilityCharge = 0.f;
	ArtTickAcc = 0.f;
	ArtPulse = 0.f;
	ArtCastFlash = 1.f;
	++AbilityCasts;
	ShieldHP = 0.f;
	ShieldHpMax = 0.f;
	ShieldAbsorbed = 0.f;

	float RingR = 0.f;
	FLinearColor RingCol = Col;
	float CoreMul = 0.94f;
	FLinearColor CoreCol(0.012f, 0.014f, 0.020f);
	float RingGlow = 0.95f;

	switch (A)
	{
	case EFSAbility::Pyre:
		AbilityDuration = bAwe ? 4.4f : 3.6f;
		// 半径刻意收得比"看上去够大"小一档：520 的实心盘会把整个战场铺成一张砖红色地板，
		// 敌人、地面网格、掉落全糊在一起。340 已经足够罩住贴身的一圈敌人。
		RingR = bAwe ? 410.f : 340.f;
		// 内圈只是"被烧黑的地面"，不是第二个发光体：亮的是边缘那一圈。
		// 数值必须压到接近黑——M_Glow 是自发光，线性 0.12 在 sRGB 下已经是砖红色，
		// 0.16 倍整片会渲染成一块实心红地板，把敌人和地面网格全糊掉。
		CoreMul = PyreCoreMul;
		CoreCol = FLinearColor(0.030f, 0.012f, 0.007f);
		// 火线用偏橙的红，而不是流派那个偏粉的珊瑚色：后者一亮就被泛光冲成米白，
		// 看着像一根奶油绳子。强度压在 0.70——这条边带宽 22uu，0.85 时自身泛光
		// 会盖住自己，整圈读成 (246,196,123) 的米黄色（实测采样），色相全丢。
		RingCol = FLinearColor(1.00f, 0.28f, 0.06f);
		RingGlow = 0.70f;
		break;
	case EFSAbility::SlowTime:
		AbilityDuration = bAwe ? 3.2f : 2.4f;
		// 时缓没有地面范围可言：它是全场的时间改动，画个圈反而骗人
		RingR = 0.f;
		// 施放的这一帧就生效：等下一次 Tick 才慢下来，手感上"按了没反应"
		if (AFSGameMode* GM = AFSGameMode::Get(this))
		{
			GM->EnemyTimeScale = bAwe ? SlowScaleAwake : SlowScaleBase;
		}
		break;
	case EFSAbility::Bulwark:
		AbilityDuration = bAwe ? 7.5f : 6.0f;
		// 半径必须明显小于觉醒光环（178uu）：176 会正好和它重合，青+绿叠成一圈
		// 说不清颜色的钢青色，护盾的"绿"就没了。收到贴身尺寸反而更像"裹在身上的一层壳"。
		RingR = bAwe ? 130.f : 112.f;
		// ArtRing 是实心圆柱，"环"靠 ArtCore 压在上面露出的边缘。0.94 的差值在
		// 半径 130 时只剩 8uu（屏幕上一根发丝），所以壁垒单独把边带加厚到 20%。
		CoreMul = 0.80f;
		ShieldHpMax = bAwe ? 72.f : 45.f;
		ShieldHP = ShieldHpMax;
		// 绿色抬到 1.15 会直接冲成白圈，和"护盾"这个色相脱钩（和精英环同一条经验：
		// 暗场里自发光一过 1.0 就留不住色相）。
		// 注意：竞技场主题是随机的，熔岩裂谷（-fsmap=2）的暖橙地面会把绿环压成土黄，
		// 想核对护盾的绿色必须用 -fsmap=0 的幽蓝竞技场。
		RingGlow = 0.75f;
		break;
	default:
		return;
	}

	AbilityTimeLeft = AbilityDuration;

	if (RingR > 0.f)
	{
		if (ArtRing)
		{
			FSUtil::SetShapeColor(ArtRing, RingCol, RingGlow);
			ArtRing->SetRelativeScale3D(FVector(RingR / 50.f, RingR / 50.f, 0.02f));
			ArtRing->SetVisibility(true, true);
		}
		if (ArtCore)
		{
			FSUtil::SetShapeColor(ArtCore, CoreCol, 0.75f);
			ArtCore->SetRelativeScale3D(FVector(RingR * CoreMul / 50.f, RingR * CoreMul / 50.f, 0.02f));
			ArtCore->SetVisibility(true, true);
		}
	}

	// 施放反馈：一圈冲击 + 一次顿帧。三招共用，玩家只需要学会"按下去世界动一下"。
	// 焚场的冲击圈刻意比火环小：410 的冲击盘会和火环叠成一张铺满屏的棕毯，
	// 把"火线"这个唯一的形状信息淹掉。小一圈反而读成"从脚下炸开一圈"。
	AFSFx::Shock(GetWorld(), GetActorLocation() - FVector(0.f, 0.f, 84.f),
		A == EFSAbility::Pyre ? 300.f : 320.f, Col, 0.55f, A == EFSAbility::Pyre ? 0.95f : 1.25f);
	AFSFx::Burst(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 40.f), 240.f, Col, 0.32f);
	FSAudio::Play(this, EFSSfx::ArtCast, GetActorLocation(), 1.30f,
		A == EFSAbility::Pyre ? 0.94f : (A == EFSAbility::SlowTime ? 1.06f : 0.78f));

	// ---- 开招冲击的"手感三件套"：镜头推近 / 屏幕色脉冲 / 震屏
	// 三招的动作、颜色、声音各不相同，但这一下必须是同一套生理反应，
	// 否则玩家得为每招重新学一次"我到底放出来了没有"。
	CastPunch = 1.f;
	CastPunchPull = bAwe ? 150.f : 108.f;   // 觉醒招更大，镜头也推得更狠
	CastPunchDur = bAwe ? 0.55f : 0.46f;
	CamPullPeak = 0.f;
	AddShake(bAwe ? 15.f : 12.f);

	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		GM->RequestHitStop(0.18f, 0.07f);
		// 屏幕脉冲由 HUD 统一画：GameMode 只存"还剩多亮"和"什么颜色"
		GM->ArtFlash = 1.f;
		GM->ArtFlashColor = Col;
	}

	AddNotice(FString::Printf(TEXT("%s%s"),
		FSUtil::L(FSDefs::AbilityNameCN(A), FSDefs::AbilityNameEN(A)),
		bAwe ? FSUtil::L(TEXT("  ·  觉醒"), TEXT("  -  AWAKENED")) : TEXT("")), 2.2f);

	UE_LOG(LogTemp, Log, TEXT("[FSART] cast kind=%d awakened=%d dur=%.1f path=%d"),
		(int32)A, bAwe ? 1 : 0, AbilityDuration, (int32)Path);
}

void AFSPlayerChar::EndAbility()
{
	const EFSAbility A = ActiveAbility;
	ActiveAbility = EFSAbility::None;
	AbilityTimeLeft = 0.f;
	ArtTickAcc = 0.f;

	// 充能冷却从"招式结束"开始算，而不是从施放算：否则壁垒降临（7.5 秒）
	// 会把冷却整段吃掉，一个 8 秒的盾等于没有冷却
	if (A != EFSAbility::None)
	{
		AbilityLock = IsPathAwakened(FSDefs::AbilityPath(A)) ? AbilityLockAwake : AbilityLockBase;
	}

	if (ArtRing) { ArtRing->SetVisibility(false, true); }
	if (ArtCore) { ArtCore->SetVisibility(false, true); }

	// 时间流速必须无条件还回去：不管是到期、被打断还是玩家阵亡，
	// 任何一条漏掉都会让整局敌人永远慢半拍
	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		GM->EnemyTimeScale = 1.f;
	}

	// 护盾到期同样炸：否则玩家会学"等它自己没"而不是"用满再换"
	if (A == EFSAbility::Bulwark)
	{
		BurstShield();
	}
	ShieldHP = 0.f;
	ShieldHpMax = 0.f;
	ShieldAbsorbed = 0.f;
}

void AFSPlayerChar::BurstShield()
{
	const bool bAwe = IsPathAwakened(EFSPath::Ward);
	const float R = bAwe ? 460.f : 380.f;
	// 吸收量直接进伤害：顶住多少就还多少，玩家不需要记第二套换算
	const float Dmg = 22.f + ShieldAbsorbed * 0.9f;

	const int32 Hits = DamageAroundSelf(R, Dmg);

	AFSFx::Shock(GetWorld(), GetActorLocation() - FVector(0.f, 0.f, 84.f), R, FSPal::Green, 0.55f, 1.2f);
	AFSFx::Burst(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 60.f), R * 0.5f, FSPal::Green, 0.34f);
	FSAudio::Play(this, EFSSfx::ShotHeavy, GetActorLocation(), 1.15f, 0.68f);
	AddShake(9.f);

	UE_LOG(LogTemp, Log, TEXT("[FSART] bulwark burst absorbed=%.1f dmg=%.1f radius=%.0f hits=%d"),
		ShieldAbsorbed, Dmg, R, Hits);
}

void AFSPlayerChar::TickAbility(float DeltaSeconds)
{
	ArtCastFlash = FMath::Max(0.f, ArtCastFlash - DeltaSeconds * 2.2f);
	AbilityLock = FMath::Max(0.f, AbilityLock - DeltaSeconds);

	if (AbilityTimeLeft <= 0.f)
	{
		return;
	}

	AbilityTimeLeft -= DeltaSeconds;
	if (AbilityTimeLeft <= 0.f)
	{
		EndAbility();
		return;
	}

	ArtPulse += DeltaSeconds;

	if (ActiveAbility == EFSAbility::Pyre)
	{
		const bool bAwe = IsPathAwakened(EFSPath::Fury);
		const float R = bAwe ? 410.f : 340.f;
		// 呼吸幅度很小、频率偏快：幅度大了像在伸缩的橡皮圈，太快又像故障
		const float Breath = 1.f + FMath::Sin(ArtPulse * 5.2f) * 0.035f;
		if (ArtRing)
		{
			ArtRing->SetRelativeScale3D(FVector(R * Breath / 50.f, R * Breath / 50.f, 0.02f));
		}
		if (ArtCore)
		{
			ArtCore->SetRelativeScale3D(FVector(R * PyreCoreMul * Breath / 50.f, R * PyreCoreMul * Breath / 50.f, 0.02f));
		}

		ArtTickAcc += DeltaSeconds;
		if (ArtTickAcc >= 0.4f)
		{
			ArtTickAcc -= 0.4f;
			const float Dmg = bAwe ? 40.f : 26.f;
			const int32 N = DamageAroundSelf(R, Dmg);

			// 火线上随机冒一小簇火：静止的圆环看着像贴图，
			// 有东西在边缘跳才像"这一圈正在烧"。位置取环上随机角度，规模压小，别糊住敌人
			{
				const float A = FMath::FRandRange(0.f, 2.f * PI);
				const FVector Puff = GetActorLocation()
					+ FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * R * 0.94f
					+ FVector(0.f, 0.f, -56.f);
				AFSFx::Burst(GetWorld(), Puff, 58.f, FLinearColor(1.f, 0.46f, 0.12f), 0.22f);
			}
			// 命中时脚下再亮一下：区分"烧到人"和"空烧"
			if (N > 0)
			{
				AFSFx::Burst(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, -50.f),
					150.f, FLinearColor(1.f, 0.58f, 0.18f), 0.15f);
			}
		}
	}
	else if (ActiveAbility == EFSAbility::SlowTime)
	{
		// 每帧重写而不是"进出来回设一次"：中途被暂停 / 顿帧 / 重开都不会留下错的值
		if (AFSGameMode* GM = AFSGameMode::Get(this))
		{
			GM->EnemyTimeScale = IsPathAwakened(EFSPath::Swift) ? SlowScaleAwake : SlowScaleBase;
		}
	}
	else if (ActiveAbility == EFSAbility::Bulwark)
	{
		// 护盾越薄环越暗：不用血条也能看出"这层壳快没了"
		const float K = 0.30f + 0.70f * GetShieldPct();
		const float Pulse = 1.f + FMath::Sin(ArtPulse * 8.f) * 0.04f;
		const float BaseR = IsPathAwakened(EFSPath::Ward) ? 130.f : 112.f;
		if (ArtRing)
		{
			// 强度压在 0.75：这个材质是纯自发光，暗场里超过 1.0 会被夹成白圈
			FSUtil::SetShapeColor(ArtRing, FSPal::Green * K, 0.75f);
			ArtRing->SetRelativeScale3D(FVector(BaseR * Pulse / 50.f, BaseR * Pulse / 50.f, 0.02f));
		}
		if (ArtCore)
		{
			// 内盘是"壳里面"，不是第二个发光体：染一层极暗的绿，让护盾看起来有体积
			FSUtil::SetShapeColor(ArtCore, FLinearColor(0.012f, 0.028f, 0.018f), 0.55f);
			ArtCore->SetRelativeScale3D(FVector(BaseR * 0.80f * Pulse / 50.f, BaseR * 0.80f * Pulse / 50.f, 0.02f));
		}
	}
}

void AFSPlayerChar::TakeWeapon(EFSWeapon W)
{
	const FFSWeaponDef& Def = FSDefs::Weapon(W);
	if (W == Weapon)
	{
		WeaponLevel = FMath::Min(WeaponLevel + 1, 5);
		if (Def.AmmoMax > 0)
		{
			Ammo = Def.AmmoMax;
		}
		AddNotice(FString::Printf(TEXT("%s  Lv.%d"), FSUtil::L(Def.NameCN, Def.NameEN), WeaponLevel));
	}
	else
	{
		Weapon = W;
		WeaponLevel = 1;
		Ammo = Def.AmmoMax;
		AddNotice(FString::Printf(TEXT("%s"), FSUtil::L(Def.NameCN, Def.NameEN)));
	}
	WeaponSwitchFlash = 0.5f;
	FireCooldown = FMath::Max(FireCooldown, 0.12f);
}

void AFSPlayerChar::Heal(float Amount)
{
	if (!IsAlive())
	{
		return;
	}
	HP = FMath::Min(MaxHP, HP + Amount);
	FSAudio::Play2D(this, EFSSfx::Heal, 0.7f);
	AddNotice(FSUtil::L(TEXT("生命 +25"), TEXT("+25 HP")));
}

void AFSPlayerChar::AddShake(float Amount)
{
	Shake = FMath::Min(Shake + Amount, 26.f);
}

void AFSPlayerChar::AddNotice(const FString& Text, float Duration)
{
	NoticeText = Text;
	NoticeTime = Duration;
}

float AFSPlayerChar::GetNoticeAlpha() const
{
	if (NoticeTime <= 0.f || NoticeText.IsEmpty())
	{
		return 0.f;
	}
	// 最后 0.45 秒淡出，避免文字硬切
	return FMath::Clamp(NoticeTime / 0.45f, 0.f, 1.f);
}

void AFSPlayerChar::UpgradeName(int32 Index, const TCHAR*& OutCN, const TCHAR*& OutEN)
{
	// 文案统一放在 FSDefs 表里，卡片与升级提示共用一份，避免两处写岔
	const FFSUpgradeDef& D = FSDefs::Upgrade((EFSUpgrade)FMath::Clamp(Index, 0, (int32)EFSUpgrade::Count - 1));
	OutCN = D.NameCN;
	OutEN = D.NameEN;
}

void AFSPlayerChar::ApplyUpgrade(int32 Index, int32 Rarity, bool bCountPath)
{
	const int32 Kind = FMath::Clamp(Index, 0, (int32)EFSUpgrade::Count - 1);
	const int32 RI = FMath::Clamp(Rarity, 0, (int32)EFSRarity::Count - 1);
	const float R = FSDefs::RarityMul((EFSRarity)RI);

	switch (Kind)
	{
	case (int32)EFSUpgrade::Damage:
		UpDmg *= 1.f + 0.14f * R;
		break;
	case (int32)EFSUpgrade::FireRate:
		UpRof *= 1.f + 0.10f * R;
		break;
	case (int32)EFSUpgrade::MoveSpeed:
		UpMove *= 1.f + 0.08f * R;
		UpMove = FMath::Min(UpMove, 1.75f);   // 别让移速飞出去，跑图节奏会乱
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->MaxFlySpeed = 660.f * UpMove;
		}
		break;
	case (int32)EFSUpgrade::MaxHP:
	{
		const float Add = 18.f * R;
		MaxHP += Add;
		HP = FMath::Min(MaxHP, HP + Add);
		break;
	}
	case (int32)EFSUpgrade::DashCd:
		DashCdScale = FMath::Max(0.45f, DashCdScale * (1.f - 0.12f * R));
		break;
	case (int32)EFSUpgrade::Pierce:
		// 穿透给的是"颗数"而不是百分比：传世一次 +2，其余 +1。
		// 叠太多会让激光/猎弓变成一路清屏，总量仍卡在 +4。
		UpPierce = FMath::Min(4.f, UpPierce + ((R >= 2.f) ? 2.f : 1.f));
		break;
	case (int32)EFSUpgrade::Crit:
		UpCrit = FMath::Min(0.60f, UpCrit + 0.10f * R);
		break;
	case (int32)EFSUpgrade::Magnet:
		UpMagnet = FMath::Min(2.6f, UpMagnet * (1.f + 0.40f * R));
		break;
	case (int32)EFSUpgrade::Ward:
		// 减伤走乘区叠加，卡在 0.60 倍（最多减 40%），否则后期站着不动也不掉血
		DmgTakenScale = FMath::Max(0.60f, DmgTakenScale * (1.f - 0.08f * R));
		break;
	case (int32)EFSUpgrade::Leech:
		UpLeech = FMath::Min(6.f, UpLeech + 1.5f * R);
		break;
	default:
		break;
	}
	++UpgradeCount;
	++UpStacks[Kind];

	// 流派层数：抽签加权与觉醒判定都读这张表。
	// 金币自动升级不计入，否则"流派"就成了系统替玩家选的路。
	if (bCountPath)
	{
		const EFSPath Pth = FSDefs::PathOf((EFSUpgrade)Kind);
		const int32 PathIdx = (int32)Pth;
		if (PathIdx >= 0 && PathIdx < (int32)EFSPath::Count)
		{
			++PathStacks[PathIdx];
			PathPulse[PathIdx] = 1.f;

			// 第 3 层落地即觉醒：外观和数值一起变，别让玩家只在日志里知道
			if (PathStacks[PathIdx] == FSDefs::PathAwakenStacks)
			{
				if (AwakenPath < 0)
				{
					AwakenPath = PathIdx;
				}
				RefreshAwakenBody();
			}
			else if (PathStacks[PathIdx] > FSDefs::PathAwakenStacks)
			{
				// 已觉醒还继续叠：不换新被动，但也不能白抽——折算成溢出加成。
				// 觉醒后该系卡会退池，所以这条只在"三系全觉醒、池子抽空"的残局里生效。
				ApplyPathOverflow(Pth, R);
				++OverflowStacks[PathIdx];
			}
		}
	}
}

void AFSPlayerChar::ApplyPathOverflow(EFSPath P, float R)
{
	// 溢出是残局的"零钱"：单层很小，靠数量累积，但不能小到没感觉。
	// 三条路各补自己最缺的那一项，不改变流派的定位。
	switch (P)
	{
	case EFSPath::Fury:
		// 烈焰：只给伤害，走乘区。后期伤害乘区最值钱，给太保守等于没给。
		UpDmg *= 1.f + 0.035f * R;
		break;

	case EFSPath::Swift:
	{
		// 疾影：射速 + 移速各一点，维持"快"的手感
		UpRof *= 1.f + 0.030f * R;
		UpMove = FMath::Min(1.75f, UpMove * (1.f + 0.020f * R));
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->MaxFlySpeed = 660.f * UpMove;
		}
		break;
	}

	case EFSPath::Ward:
	{
		// 壁垒：加上限并当场补上这部分，觉醒后"越叠越耐打"
		const float Add = 10.f * R;
		MaxHP += Add;
		HP = FMath::Min(MaxHP, HP + Add);
		break;
	}

	default:
		break;
	}

	UE_LOG(LogTemp, Log, TEXT("[FSOVERFLOW] path=%d overflow=%d dmg=%.3f rof=%.3f move=%.3f maxhp=%.0f"),
		(int32)P, OverflowStacks[(int32)P] + 1, UpDmg, UpRof, UpMove, MaxHP);
}

float AFSPlayerChar::GetDeathSavePct() const
{
	// 未觉醒时恒为 1（"随时可以免死"是假象，所以 HUD 只在觉醒后才画这个进度）
	return DeathSaveCdMax > 0.f ? FMath::Clamp(1.f - DeathSaveCd / DeathSaveCdMax, 0.f, 1.f) : 1.f;
}

float AFSPlayerChar::GetSurgePct() const
{
	return DashSurgeMax > 0.f ? FMath::Clamp(DashSurge / DashSurgeMax, 0.f, 1.f) : 0.f;
}

void AFSPlayerChar::RefreshAwakenBody()
{
	if (AwakenPath < 0)
	{
		return;
	}
	const EFSPath Pth = (EFSPath)AwakenPath;
	const FLinearColor Col = FSDefs::PathColor(Pth);

	// 脚下光环换成流派色——这是"这局走的哪条路"最省事的常驻标识，
	// 战斗中不用切界面也能一眼认出自己和上次玩的不一样。
	// 发光只给 0.8：这个材质是纯自发光，Intensity 一过 1 就被夹成白色，
	// 绿/橙/青全变成一个颜色，流派色就白给了。
	if (HaloOuter)
	{
		FSUtil::SetShapeColor(HaloOuter, Col, 0.55f);
	}
	if (AwakenRing)
	{
		FSUtil::SetShapeColor(AwakenRing, Col, 0.60f);
		AwakenRing->SetVisibility(true, true);
	}
	if (AwakenRingCore)
	{
		AwakenRingCore->SetVisibility(true, true);
	}
	for (UStaticMeshComponent* S : AwakenShards)
	{
		if (S)
		{
			FSUtil::SetShapeColor(S, Col, 0.22f);
			S->SetVisibility(true, true);
		}
	}
}

void AFSPlayerChar::TickAwakenBody(float DeltaSeconds)
{
	HaloT += DeltaSeconds;

	if (AwakenPath < 0)
	{
		return;
	}
	AwakenT += DeltaSeconds;

	const EFSPath Pth = (EFSPath)AwakenPath;
	const FLinearColor Col = FSDefs::PathColor(Pth);

	// 三条流派各给一个"性格"：烈焰转得急、疾影铺得开、壁垒沉而稳。
	// 只换颜色的话，三条路看起来会一模一样，觉醒就没有辨识度。
	// 半径要略小于觉醒外圈（1.78 → 178uu），碎晶才像"绕着光环转"，
	// 而不是在环外孤零零地飘。三档半径都按这个比例收。
	float Speed = 2.2f;
	float Radius = 118.f;
	float Bob = 12.f;
	if (Pth == EFSPath::Swift)      { Speed = 3.4f; Radius = 132.f; Bob = 18.f; }
	else if (Pth == EFSPath::Ward)  { Speed = 1.3f; Radius = 106.f; Bob = 7.f; }

	const float G = -92.f;   // 与 BuildBody 里胶囊中心到底面的偏移保持一致
	const int32 N = AwakenShards.Num();
	for (int32 i = 0; i < N; ++i)
	{
		UStaticMeshComponent* S = AwakenShards[i];
		if (!S)
		{
			continue;
		}
		const float A = AwakenT * Speed + (2.f * PI * i) / FMath::Max(1, N);
		const float Z = G + 88.f + FMath::Sin(AwakenT * 2.6f + i * 1.7f) * Bob;
		S->SetRelativeLocation(FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, Z));
		S->SetRelativeRotation(FRotator(0.f, FMath::RadiansToDegrees(A) * 2.f, 0.f));
	}

	// 外圈呼吸：0.50~0.72 之间起伏。上限压得比之前低很多 —— 这个材质的 Intensity
	// 一过 0.8 就把饱和色削成白色，呼吸幅度再大也只是在"白"和"更白"之间晃。
	if (AwakenRing)
	{
		const float K = 0.5f + 0.5f * FMath::Sin(HaloT * 3.0f);
		FSUtil::SetShapeColor(AwakenRing, Col, 0.50f + 0.22f * K);
	}
}

void AFSPlayerChar::TickSurgeRing()
{
	const float Pct = GetSurgePct();
	const bool bShow = (Pct > 0.01f) && IsAlive();
	if (SurgeRing)
	{
		SurgeRing->SetVisibility(bShow, true);
	}
	if (SurgeRingCore)
	{
		SurgeRingCore->SetVisibility(bShow, true);
	}
	if (!bShow)
	{
		return;
	}

	// 半径 2.55 → 1.95 线性收：眼睛对"光圈往里缩"的判读比读数字准得多，
	// 玩家不用看 HUD 就知道这波加成还剩多久。
	const float R = 1.95f + 0.60f * Pct;
	if (SurgeRing)
	{
		SurgeRing->SetRelativeScale3D(FVector(R, R, 0.02f));
		// 越接近结束越亮：收口那一下有"要没了"的催促感
		FSUtil::SetShapeColor(SurgeRing, FLinearColor(0.62f, 1.00f, 1.00f),
			0.55f + 0.55f * (1.f - Pct));
	}
	if (SurgeRingCore)
	{
		SurgeRingCore->SetRelativeScale3D(FVector(R - 0.20f, R - 0.20f, 0.02f));
	}
}

void AFSPlayerChar::FireOnce()
{	const FFSWeaponDef& W = FSDefs::Weapon(Weapon);
	// 主动技的乘区接在这里：焚场期间自己打得更重，时缓觉醒期间打得更快
	const float DmgMul = (1.f + (WeaponLevel - 1) * 0.17f) * UpDmg * GetAbilityDmgMul();
	float RofMul = (1.f + (WeaponLevel - 1) * 0.07f) * UpRof * GetAbilityRofMul();

	// 疾影觉醒「残影」：刚闪避完的 0.8 秒里射速狂飙，鼓励"贴脸闪避接输出"
	if (DashSurge > 0.f)
	{
		RofMul *= 1.35f;
	}
	FireCooldown = W.Interval / RofMul;

	const FVector Muzzle = MuzzleLocation();
	FVector Base = AimPoint - Muzzle;
	Base.Z = 0.f;
	if (Base.SizeSquared() < 100.f)
	{
		Base = FVector::ForwardVector.RotateAngleAxis(AimYaw, FVector::UpVector);
	}
	Base.Normalize();

	// 赐福带来的穿透是加算的：武器自带穿透 + 增益层数
	const int32 TotalPierce = W.Pierce + FMath::RoundToInt(UpPierce);

	// 烈焰觉醒「狂怒」：暴击伤害从 2.2 倍抬到 2.65 倍。
	// 暴击弹丸同时再放大一圈——"这一发不一样"要看得见，而不是只体现在掉血数字上。
	const bool bFury = IsPathAwakened(EFSPath::Fury);
	const float CritMul = bFury ? 2.65f : 2.2f;

	// 残影窗口里弹丸加粗：射速 +35% 只体现在数字上玩家是感知不到的，
	// 让弹道本身变粗才看得出"自己正在狂喷"。
	const float SurgeScale = (DashSurge > 0.f) ? 1.16f : 1.f;

	for (int32 i = 0; i < W.Pellets; ++i)
	{
		const float YawOff = (W.Pellets == 1)
			? FMath::FRandRange(-W.Spread, W.Spread) * 0.35f
			: FMath::FRandRange(-W.Spread, W.Spread);
		const FVector Dir = Base.RotateAngleAxis(YawOff, FVector::UpVector);

		// 暴击按每颗弹丸独立判定：霰弹一枪里跳出两三颗金弹更有感
		const bool bCrit = (UpCrit > 0.f) && (FMath::FRand() < UpCrit);
		const float ShotDmg = W.Damage * DmgMul * (bCrit ? CritMul : 1.f);
		const FLinearColor ShotColor = bCrit ? FLinearColor(1.00f, 0.90f, 0.30f) : W.Color;
		const float CritScale = bCrit ? (bFury ? 1.44f : 1.22f) : 1.f;

		if (AFSProjectile* Proj = GetWorld()->SpawnActor<AFSProjectile>(AFSProjectile::StaticClass(), Muzzle, FRotator::ZeroRotator))
		{
			Proj->Launch(Muzzle, Dir, W.Speed, ShotDmg, TotalPierce, false, ShotColor,
				W.ProjScale * CritScale * SurgeScale, W.bExplosive, W.ExplodeRadius, W.ExplodeDamage, this);
		}
	}

	{
		EFSSfx Sfx = EFSSfx::Shot;
		float SfxPitch = 1.f;
		float SfxVol = 1.15f;
		switch (Weapon)
		{
		case EFSWeapon::Shotgun: Sfx = EFSSfx::ShotHeavy; SfxVol = 1.30f; break;
		case EFSWeapon::SMG: Sfx = EFSSfx::Shot; SfxPitch = 1.24f; SfxVol = 0.95f; break;
		case EFSWeapon::Laser: Sfx = EFSSfx::Laser; SfxVol = 1.05f; break;
		case EFSWeapon::Bow: Sfx = EFSSfx::Bow; SfxVol = 1.10f; break;
		case EFSWeapon::Rocket: Sfx = EFSSfx::Launcher; SfxVol = 1.20f; break;
		default: break;
		}
		FSAudio::Play(this, Sfx, Muzzle, SfxVol, SfxPitch);
	}

	// 枪口火光：半径和时长都要小，否则连射时会在身上糊出一个大白球
	AFSFx::Burst(GetWorld(), Muzzle + Base * 30.f, 40.f * W.ProjScale, W.Color, 0.075f);
	AddShake(W.bExplosive ? 4.f : 0.9f);

	if (W.AmmoMax > 0)
	{
		Ammo--;
		if (Ammo <= 0)
		{
			Ammo = 0;
			Weapon = EFSWeapon::Pistol;
			WeaponLevel = 1;
			AddNotice(FSUtil::L(TEXT("弹药耗尽 - 换回短枪"), TEXT("OUT OF AMMO - PISTOL")));
		}
	}
}

void AFSPlayerChar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Shake = FMath::Max(0.f, Shake - Shake * 9.f * DeltaSeconds - 1.5f * DeltaSeconds);
	ShakeT += DeltaSeconds;

	// 开招推近：二次方衰减 → 落手瞬间顶到底，0.1 秒内收回一半，读起来是"弹一下"，
	// 线性衰减会变成"慢慢飘走"，反而拖慢节奏。
	CastPunch = FMath::Max(0.f, CastPunch - DeltaSeconds / FMath::Max(0.01f, CastPunchDur));
	CamPullNow = -CastPunchPull * CastPunch * CastPunch;
	CamPullPeak = FMath::Min(CamPullPeak, CamPullNow);

	if (SpringArm)
	{
		const float OX = Shake * FMath::Sin(ShakeT * 41.f) * 0.55f;
		const float OY = Shake * FMath::Cos(ShakeT * 37.f) * 0.55f;
		SpringArm->SetRelativeRotation(FRotator(CamPitchBase + OY, OX, 0.f));

		// 只缩臂长会读成"镜头变近了"，FOV 一起收才有"被压过来"的冲击感。
		// 两个量都从基准算，绝不累加，所以掉帧或被 hitstop 拉长也不会漂。
		SpringArm->TargetArmLength = CamBaseArm + CamPullNow;
		if (Camera)
		{
			Camera->SetFieldOfView(CamBaseFov + CamPullNow * 0.022f);
		}
	}

	if (!IsAlive())
	{
		return;
	}

	// 别让角色走进围墙里：一是穿模，二是相机落在墙外侧，整面墙会把战场下半屏全挡住。
	// 留 300 的余量：相机在角色斜后方 849 处，离墙太近时墙顶会切掉角色下半身，
	// 按「相机高度 1300、墙高 226」反算，距墙面至少要 150，留到 300 才有足够余量给相机滞后。
	if (const AFSGameMode* BoundGM = AFSGameMode::Get(this))
	{
		const float Lim = BoundGM->ArenaHalf - 300.f;
		FVector L = GetActorLocation();
		const float CX = FMath::Clamp(L.X, -Lim, Lim);
		const float CY = FMath::Clamp(L.Y, -Lim, Lim);
		if (CX != L.X || CY != L.Y)
		{
			L.X = CX;
			L.Y = CY;
			SetActorLocation(L, false);
		}
	}

	FireCooldown -= DeltaSeconds;
	DashCooldown = FMath::Max(0.f, DashCooldown - DeltaSeconds);
	Invuln = FMath::Max(0.f, Invuln - DeltaSeconds);
	DeathSaveCd = FMath::Max(0.f, DeathSaveCd - DeltaSeconds);
	DeathSaveFlash = FMath::Max(0.f, DeathSaveFlash - DeltaSeconds * 1.6f);
	DashSurge = FMath::Max(0.f, DashSurge - DeltaSeconds);
	if (DashSurge > 0.f)
	{
		SurgeTick += DeltaSeconds;
	}
	NoticeTime = FMath::Max(0.f, NoticeTime - DeltaSeconds);
	WeaponSwitchFlash = FMath::Max(0.f, WeaponSwitchFlash - DeltaSeconds);
	HitFlash = FMath::Max(0.f, HitFlash - DeltaSeconds);

	// 选卡脉冲与觉醒光环：跟游戏是否在跑无关，暂停时也慢慢淡，不会有"卡住"的亮块
	for (int32 i = 0; i < (int32)EFSPath::Count; ++i)
	{
		PathPulse[i] = FMath::Max(0.f, PathPulse[i] - DeltaSeconds * 1.1f);
	}
	TickAwakenBody(DeltaSeconds);
	TickSurgeRing();
	TickAbility(DeltaSeconds);

	// 自动演示模式
	if (bBot)
	{
		if (AFSGameMode* BotGM = AFSGameMode::Get(this))
		{
			AFSEnemy* Best = nullptr;
			float BestD = 1e9f;
			for (const TWeakObjectPtr<AFSEnemy>& W : BotGM->Enemies)
			{
				AFSEnemy* E = W.Get();
				if (!E || E->IsDead())
				{
					continue;
				}
				const float D = FVector::Dist2D(E->GetActorLocation(), GetActorLocation());
				if (D < BestD)
				{
					BestD = D;
					Best = E;
				}
			}
			if (Best)
			{
				SetAimWorld(Best->GetActorLocation());
				bFiring = true;

				FVector Away = GetActorLocation() - Best->GetActorLocation();
				Away.Z = 0.f;
				Away.Normalize();
				const FVector Strafe(-Away.Y, Away.X, 0.f);
				FVector Wish = Strafe;
				if (BestD > 900.f)
				{
					Wish = -Away + Strafe * 0.6f;
				}
				else if (BestD < 560.f)
				{
					Wish = Away + Strafe * 0.8f;
				}
				Wish.Z = 0.f;
				Wish.Normalize();
				MoveInput = FVector2D(Wish.X, Wish.Y);
				bFiring = true;

				// 机器人也会放招：自动化跑图时，这一条是"招式在真实战斗里跑得通"的唯一证据
				if (IsAbilityReady() && BestD < 820.f)
				{
					TryAbility();
				}
			}
			else
			{
				bFiring = false;
				MoveInput = FVector2D(FMath::Cos(GetWorld()->GetTimeSeconds() * 0.9f),
					FMath::Sin(GetWorld()->GetTimeSeconds() * 0.9f));
			}
		}
	}

	// 移动
	if (DashTime > 0.f)
	{
		DashTime -= DeltaSeconds;
		GetCharacterMovement()->Velocity = DashDir * 2700.f;
	}
	else
	{
		const FVector Wish(MoveInput.X, MoveInput.Y, 0.f);
		if (Wish.SizeSquared() > 0.01f)
		{
			AddMovementInput(FVector(Wish.X, Wish.Y, 0.f), 1.f);
		}
		StepPhase += DeltaSeconds * (Wish.SizeSquared() > 0.01f ? 10.f : 0.f);
	}

	// 站位限制在场地内
	FVector Loc = GetActorLocation();
	const float Half = 2900.f;
	Loc.X = FMath::Clamp(Loc.X, -Half, Half);
	Loc.Y = FMath::Clamp(Loc.Y, -Half, Half);
	Loc.Z = 92.f;
	SetActorLocation(Loc, false, nullptr, ETeleportType::TeleportPhysics);

	// 开火
	if (bFiring && FireCooldown <= 0.f)
	{
		FireOnce();
	}

	// 受击闪红
	if (HitFlash > 0.f)
	{
		const float A = HitFlash / 0.22f;
		for (const FFSBodyPart& Part : Parts)
		{
			FSUtil::SetShapeColor(Part.Comp, FSUtil::Lerp(Part.Color, FSPal::Danger, A * 0.85f), Part.Glow);
		}
	}
	else if (Parts.Num() > 0 && HitFlash <= 0.f && Invuln > 0.f && DashTime > 0.f)
	{
		for (const FFSBodyPart& Part : Parts)
		{
			FSUtil::SetShapeColor(Part.Comp, FSUtil::Lerp(Part.Color, FSPal::Cyan, 0.35f), Part.Glow);
		}
	}
}

float AFSPlayerChar::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (!IsAlive() || Invuln > 0.f || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("fsgod")))
	{
		return 0.f;
	}

	// 护壁：受到的伤害走乘区（下限 0.60 倍）
	const float Mitigated = DamageAmount * DmgTakenScale;

	// 壁垒降临：护盾先吃，吃到穿为止。
	// 吸收量记下来算爆发伤害——"顶住多少就还多少"是一条规则，
	// 玩家不需要另外记"护盾爆了会炸多痛"。
	if (ShieldHP > 0.f)
	{
		const float Abs = FMath::Min(ShieldHP, Mitigated);
		ShieldHP -= Abs;
		ShieldAbsorbed += Abs;
		HitFlash = FMath::Max(HitFlash, 0.12f);
		AddShake(4.f);

		// 吃伤害时环闪一下：不然"被盾吃掉"和"没打中"看起来一样
		if (ArtRing)
		{
			FSUtil::SetShapeColor(ArtRing, FLinearColor(0.82f, 1.00f, 0.88f), 2.0f);
		}

		if (ShieldHP <= 0.f)
		{
			EndAbility();   // 内部会调 BurstShield，把吸收量炸出去
		}
		return Abs;
	}

	// 壁垒觉醒「不屈」：每 45 秒能把一次致命伤挡成 1 点血。
	// 放在扣血之前判定，所以是真"免死"而不是"死了再拉起来"。
	if (HP - Mitigated <= 0.f && DeathSaveCd <= 0.f && IsPathAwakened(EFSPath::Ward))
	{
		HP = 1.f;
		DeathSaveCd = DeathSaveCdMax;
		DeathSaveFlash = 1.f;
		Invuln = FMath::Max(Invuln, 1.5f);
		HitFlash = 0.30f;
		AddShake(22.f);
		AddNotice(FSUtil::L(TEXT("不  屈   ·   残 血 存 活"), TEXT("UNDYING   -   SURVIVED ON 1 HP")), 2.4f);
		UE_LOG(LogTemp, Warning, TEXT("[FSWARD] undying triggered  dmg=%.1f  stacks=%d"),
			Mitigated, GetPathStacks(EFSPath::Ward));
		FSAudio::Play(this, EFSSfx::Upgrade, GetActorLocation(), 1.40f, 0.62f);
		if (UWorld* W = GetWorld())
		{
			// 半径别铺太大：540 的冲击盘会把角色整个盖住，玩家只看到一块绿饼，
			// 反而不知道"自己站在哪"。分量交给顿帧和全屏绿闪，特效只负责点一下。
			AFSFx::Shock(W, GetActorLocation() - FVector(0.f, 0.f, 84.f), 360.f, FSPal::Green, 0.55f);
			AFSFx::Burst(W, GetActorLocation() + FVector(0.f, 0.f, 70.f), 190.f, FSPal::Green, 0.40f);
		}
		if (AFSGameMode* GM = AFSGameMode::Get(this))
		{
			// 顿帧：这 0.11 秒里世界几乎停住，玩家才意识到"刚才那下本来该死了"。
			// 只加闪光不加停顿的话，免死和普通挨打看起来一模一样。
			GM->RequestHitStop(0.24f, 0.11f);
			GM->OnPlayerHurt();
		}
		return Mitigated;
	}

	HP -= Mitigated;
	FSAudio::Play(this, EFSSfx::Hurt, GetActorLocation(), 1.25f, FMath::FRandRange(0.92f, 1.06f));
	Invuln = 0.42f;
	HitFlash = 0.22f;
	AddShake(9.f);

	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		GM->OnPlayerHurt();
	}

	if (HP <= 0.f)
	{
		HP = 0.f;
		Die();
	}
	return Mitigated;
}

void AFSPlayerChar::Die()
{
	if (!bAlive)
	{
		return;
	}
	bAlive = false;
	bFiring = false;

	// 死了先把招收掉：时缓还在跑的话，整局敌人会永远慢半拍，
	// 结算画面后面站着一群以 0.24 倍速挪动的怪，非常出戏
	if (IsAbilityActive())
	{
		EndAbility();
	}
	AbilityCharge = 0.f;

	AFSFx::Burst(GetWorld(), GetActorLocation(), 420.f, FSPal::Danger, 0.55f);
	AFSFx::Shock(GetWorld(), GetActorLocation() - FVector(0.f, 0.f, 84.f), 700.f, FSPal::Danger, 0.7f);
	FSAudio::Play2D(this, EFSSfx::GameOver, 1.0f);
	AddShake(20.f);

	GetCharacterMovement()->StopMovementImmediately();

	for (const FFSBodyPart& Part : Parts)
	{
		FSUtil::SetShapeColor(Part.Comp, FLinearColor(0.35f, 0.08f, 0.10f), 0.f);
	}

	// 觉醒光环跟着一起熄掉：死了脚下还转着一圈碎晶非常出戏
	if (HaloOuter)       { FSUtil::SetShapeColor(HaloOuter, FLinearColor(0.10f, 0.06f, 0.08f), 0.6f); }
	if (AwakenRing)      { AwakenRing->SetVisibility(false, true); }
	if (AwakenRingCore)  { AwakenRingCore->SetVisibility(false, true); }
	for (UStaticMeshComponent* S : AwakenShards)
	{
		if (S)
		{
			S->SetVisibility(false, true);
		}
	}

	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		GM->EndGame();
	}
}

// ------------------------------------------------------------------ 控制器

AFSPlayerController::AFSPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	DefaultMouseCursor = EMouseCursor::Crosshairs;
}

void AFSPlayerController::EnsureInput()
{
	if (bInputReady)
	{
		return;
	}
	bInputReady = true;

	IA_Move = NewObject<UInputAction>(this, TEXT("IA_Move"));
	IA_Move->ValueType = EInputActionValueType::Axis2D;

	IMC = NewObject<UInputMappingContext>(this, TEXT("IMC_Game"));

	auto MapMove = [this](const FKey& Key, bool bNegate, bool bSwizzle)
	{
		FEnhancedActionKeyMapping& Mapping = IMC->MapKey(IA_Move, Key);
		if (bSwizzle)
		{
			UInputModifierSwizzleAxis* Sw = NewObject<UInputModifierSwizzleAxis>(IMC);
			Sw->Order = EInputAxisSwizzle::YXZ;
			Mapping.Modifiers.Add(Sw);
		}
		if (bNegate)
		{
			UInputModifierNegate* Ng = NewObject<UInputModifierNegate>(IMC);
			Mapping.Modifiers.Add(Ng);
		}
	};

	MapMove(EKeys::W, false, false);
	MapMove(EKeys::S, true, false);
	MapMove(EKeys::D, false, true);
	MapMove(EKeys::A, true, true);
	MapMove(EKeys::Up, false, false);
	MapMove(EKeys::Down, true, false);
	MapMove(EKeys::Right, false, true);
	MapMove(EKeys::Left, true, true);

	// 手柄左摇杆：摇杆 X 是横向、Y 是纵向，需要交换到世界坐标
	{
		FEnhancedActionKeyMapping& Mapping = IMC->MapKey(IA_Move, EKeys::Gamepad_Left2D);
		UInputModifierSwizzleAxis* Sw = NewObject<UInputModifierSwizzleAxis>(IMC);
		Sw->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Sw);
		UInputModifierDeadZone* DZ = NewObject<UInputModifierDeadZone>(IMC);
		DZ->Type = EDeadZoneType::Radial;
		DZ->LowerThreshold = 0.16f;
		DZ->UpperThreshold = 0.95f;
		Mapping.Modifiers.Add(DZ);
	}

	IA_Fire = NewObject<UInputAction>(this, TEXT("IA_Fire"));
	IA_Fire->ValueType = EInputActionValueType::Boolean;
	IMC->MapKey(IA_Fire, EKeys::LeftMouseButton);
	IMC->MapKey(IA_Fire, EKeys::Gamepad_RightTrigger);
	IMC->MapKey(IA_Fire, EKeys::Gamepad_FaceButton_Right);

	IA_Look = NewObject<UInputAction>(this, TEXT("IA_Look"));
	IA_Look->ValueType = EInputActionValueType::Axis2D;
	IMC->MapKey(IA_Look, EKeys::Gamepad_Right2D);

	IA_Dash = NewObject<UInputAction>(this, TEXT("IA_Dash"));
	IA_Dash->ValueType = EInputActionValueType::Boolean;
	IMC->MapKey(IA_Dash, EKeys::SpaceBar);
	IMC->MapKey(IA_Dash, EKeys::LeftShift);
	IMC->MapKey(IA_Dash, EKeys::Gamepad_FaceButton_Bottom);
	IMC->MapKey(IA_Dash, EKeys::Gamepad_LeftShoulder);

	// 流派主动技：Q / 右键。右键不占用开火（开火只有左键），
	// 所以"一手射击一手放招"是同时成立的，不用切手指
	IA_Ability = NewObject<UInputAction>(this, TEXT("IA_Ability"));
	IA_Ability->ValueType = EInputActionValueType::Boolean;
	IMC->MapKey(IA_Ability, EKeys::Q);
	IMC->MapKey(IA_Ability, EKeys::RightMouseButton);
	IMC->MapKey(IA_Ability, EKeys::Gamepad_FaceButton_Top);

	IA_Confirm = NewObject<UInputAction>(this, TEXT("IA_Confirm"));
	IA_Confirm->ValueType = EInputActionValueType::Boolean;
	IMC->MapKey(IA_Confirm, EKeys::R);
	IMC->MapKey(IA_Confirm, EKeys::Enter);
	IMC->MapKey(IA_Confirm, EKeys::Gamepad_Special_Right);

	// 通关后的"继续无尽"。单独一个 action 而不是复用 Confirm：
	// R 是"重开"，E 是"接着打"，两个结果的差别很大，不该共用一个键。
	IA_Endless = NewObject<UInputAction>(this, TEXT("IA_Endless"));
	IA_Endless->ValueType = EInputActionValueType::Boolean;
	IMC->MapKey(IA_Endless, EKeys::E);
	IMC->MapKey(IA_Endless, EKeys::Gamepad_FaceButton_Right);
}

void AFSPlayerController::BeginPlay()
{
	Super::BeginPlay();

	EnsureInput();

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsys->AddMappingContext(IMC, 0);
		}
	}

	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
	bShowMouseCursor = true;
}

void AFSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	EnsureInput();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		return;
	}

	EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AFSPlayerController::OnMove);
	EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AFSPlayerController::OnLook);
	EIC->BindAction(IA_Look, ETriggerEvent::Completed, this, &AFSPlayerController::OnLook);
	EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &AFSPlayerController::OnMove);
	EIC->BindAction(IA_Fire, ETriggerEvent::Started, this, &AFSPlayerController::OnFireStart);
	EIC->BindAction(IA_Fire, ETriggerEvent::Completed, this, &AFSPlayerController::OnFireStop);
	EIC->BindAction(IA_Dash, ETriggerEvent::Started, this, &AFSPlayerController::OnDash);
	EIC->BindAction(IA_Ability, ETriggerEvent::Started, this, &AFSPlayerController::OnAbility);
	EIC->BindAction(IA_Confirm, ETriggerEvent::Started, this, &AFSPlayerController::OnConfirm);
	EIC->BindAction(IA_Endless, ETriggerEvent::Started, this, &AFSPlayerController::OnEndless);
}

AFSPlayerChar* AFSPlayerController::GetChar() const
{
	return Cast<AFSPlayerChar>(GetPawn());
}

void AFSPlayerController::OnMove(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	PendingMove = V;
	if (AFSPlayerChar* C = GetChar())
	{
		C->SetMoveInput(V);
	}
}

void AFSPlayerController::OnLook(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	StickLook = V;
	if (V.SizeSquared() > 0.06f)
	{
		StickLookTime = 0.55f;
	}
}

void AFSPlayerController::OnFireStart(const FInputActionValue& Value)
{
	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		if (GM->bPaused)
		{
			return;
		}
		if (GM->Phase == EFSPhase::Title)
		{
			GM->StartGame();
			return;
		}
		if (GM->Phase == EFSPhase::GameOver || GM->Phase == EFSPhase::Victory)
		{
			GM->RestartAll();
			return;
		}
	}
	if (AFSPlayerChar* C = GetChar())
	{
		C->SetFiring(true);
	}
}

void AFSPlayerController::OnFireStop(const FInputActionValue& Value)
{
	if (AFSPlayerChar* C = GetChar())
	{
		C->SetFiring(false);
	}
}

void AFSPlayerController::OnDash(const FInputActionValue& Value)
{
	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		if (GM->Phase != EFSPhase::Playing || GM->bPaused)
		{
			return;
		}
	}
	if (AFSPlayerChar* C = GetChar())
	{
		C->TryDash();
	}
}

void AFSPlayerController::OnAbility(const FInputActionValue& Value)
{
	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		// 暂停、结算、选卡时一律不响应：放招不该能在菜单里偷跑
		if (GM->Phase != EFSPhase::Playing || GM->bPaused || GM->bChoosing)
		{
			return;
		}
	}
	if (AFSPlayerChar* C = GetChar())
	{
		C->TryAbility();
	}
}

void AFSPlayerController::OnConfirm(const FInputActionValue& Value)
{
	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		if (GM->bPaused)
		{
			return;
		}
		if (GM->Phase == EFSPhase::GameOver || GM->Phase == EFSPhase::Victory)
		{
			GM->RestartAll();
		}
		else if (GM->Phase == EFSPhase::Title)
		{
			GM->StartGame();
		}
	}
}

void AFSPlayerController::OnEndless(const FInputActionValue& Value)
{
	// 只有通关界面上的 E 才有意义。Playing 时按 E 不做任何事——
	// 留白比"随手一个键触发一个模式切换"安全。
	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		if (GM->Phase == EFSPhase::Victory)
		{
			GM->ContinueEndless();
		}
	}
}

bool AFSPlayerController::ComputeAim(FVector& OutPoint) const
{
	FVector WorldLoc, WorldDir;
	if (!DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return false;
	}
	const APawn* P = GetPawn();
	const float PlaneZ = P ? P->GetActorLocation().Z : 92.f;
	if (FMath::Abs(WorldDir.Z) < 0.001f)
	{
		return false;
	}
	const float T = (PlaneZ - WorldLoc.Z) / WorldDir.Z;
	if (T <= 0.f)
	{
		return false;
	}
	OutPoint = WorldLoc + WorldDir * T;
	return true;
}

void AFSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	AFSGameMode* GM = AFSGameMode::Get(this);

	// ---- 暂停菜单：ESC / 手柄 Back 开关
	// 注意：不能用手柄 Start，它已经绑给 IA_Confirm（开始 / 重开），会一开局就弹菜单
	if (GM && (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::Gamepad_Special_Left)))
	{
		GM->TogglePause();
		return;
	}

	// ---- 暂停中：只处理菜单导航，不处理瞄准与射击
	if (GM && GM->bPaused)
	{
		if (WasInputKeyJustPressed(EKeys::Up) || WasInputKeyJustPressed(EKeys::W)
			|| WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up))
		{
			GM->MenuMove(-1);
		}
		else if (WasInputKeyJustPressed(EKeys::Down) || WasInputKeyJustPressed(EKeys::S)
			|| WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down))
		{
			GM->MenuMove(1);
		}

		if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar)
			|| WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom))
		{
			GM->MenuActivate();
		}

		if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			int32 VX = 0, VY = 0;
			GetViewportSize(VX, VY);
			float MX = 0.f, MY = 0.f;
			if (GetMousePosition(MX, MY))
			{
				GM->MenuClickAt(MX, MY, (float)VX, (float)VY);
			}
		}
		return;
	}

	AFSPlayerChar* C = GetChar();
	if (!C)
	{
		return;
	}

	// ---- 赐福卡选择中：只处理选择，不瞄准不开枪
	if (GM && GM->IsChoosing())
	{
		// 点卡片时鼠标左键同时也会触发开火，这里压住
		C->SetFiring(false);
		ClearMoveInput();

		if (WasInputKeyJustPressed(EKeys::One))
		{
			GM->BlessingPick(0);
		}
		else if (WasInputKeyJustPressed(EKeys::Two))
		{
			GM->BlessingPick(1);
		}
		else if (WasInputKeyJustPressed(EKeys::Three))
		{
			GM->BlessingPick(2);
		}
		else
		{
			if (WasInputKeyJustPressed(EKeys::Left) || WasInputKeyJustPressed(EKeys::A)
				|| WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left))
			{
				GM->BlessingMove(-1);
			}
			else if (WasInputKeyJustPressed(EKeys::Right) || WasInputKeyJustPressed(EKeys::D)
				|| WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right))
			{
				GM->BlessingMove(1);
			}

			if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar)
				|| WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom))
			{
				GM->BlessingPick(GM->ChoiceSel);
			}
		}

		if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			int32 VX = 0, VY = 0;
			GetViewportSize(VX, VY);
			float MX = 0.f, MY = 0.f;
			if (GetMousePosition(MX, MY))
			{
				GM->BlessingClickAt(MX, MY, (float)VX, (float)VY);
			}
		}
		return;
	}

	// M 键切换静音（手柄 Back 已让给暂停菜单，手柄静音改由菜单里的音量项控制）
	if (WasInputKeyJustPressed(EKeys::M))
	{
		FSAudio::ToggleMute(this);
		C->AddNotice(FSAudio::IsMuted(this)
			? FSUtil::L(TEXT("已静音（M 恢复）"), TEXT("MUTED (M TO UNMUTE)"))
			: FSUtil::L(TEXT("声音已开启"), TEXT("SOUND ON")), 1.2f);
	}

	// L 键快速切换中英文（暂停菜单里也有同一项）
	if (GM && WasInputKeyJustPressed(EKeys::L))
	{
		GM->CycleLanguage();
		C->AddNotice(FSUtil::bChinese()
			? FSUtil::L(TEXT("语言：中文"), TEXT("LANGUAGE: CHINESE"))
			: FSUtil::L(TEXT("语言：English"), TEXT("LANGUAGE: ENGLISH")), 1.2f);
	}

	StickLookTime = FMath::Max(0.f, StickLookTime - DeltaTime);

	FVector Aim;
	const bool bStickAim = StickLookTime > 0.f && StickLook.SizeSquared() > 0.04f;
	if (bStickAim)
	{
		// 右摇杆：屏幕上方 = 世界 +X，屏幕右方 = 世界 +Y
		const FVector Dir = FVector(StickLook.Y, StickLook.X, 0.f).GetSafeNormal();
		C->SetAimWorld(C->GetActorLocation() + Dir * 1600.f);
	}
	else if (ComputeAim(Aim))
	{
		C->SetAimWorld(Aim);
	}
	else
	{
		C->SetAimWorld(C->GetActorLocation() + FVector(1000.f, 0.f, 0.f));
	}
	C->SetMoveInput(PendingMove);
}
