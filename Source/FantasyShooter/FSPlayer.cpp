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
	const float DmgMul = (1.f + (WeaponLevel - 1) * 0.17f) * UpDmg;
	float RofMul = (1.f + (WeaponLevel - 1) * 0.07f) * UpRof;

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

	if (SpringArm)
	{
		const float OX = Shake * FMath::Sin(ShakeT * 41.f) * 0.55f;
		const float OY = Shake * FMath::Cos(ShakeT * 37.f) * 0.55f;
		SpringArm->SetRelativeRotation(FRotator(CamPitchBase + OY, OX, 0.f));
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

	IA_Confirm = NewObject<UInputAction>(this, TEXT("IA_Confirm"));
	IA_Confirm->ValueType = EInputActionValueType::Boolean;
	IMC->MapKey(IA_Confirm, EKeys::R);
	IMC->MapKey(IA_Confirm, EKeys::Enter);
	IMC->MapKey(IA_Confirm, EKeys::Gamepad_Special_Right);
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
	EIC->BindAction(IA_Confirm, ETriggerEvent::Started, this, &AFSPlayerController::OnConfirm);
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
		if (GM->Phase == EFSPhase::GameOver)
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

void AFSPlayerController::OnConfirm(const FInputActionValue& Value)
{
	if (AFSGameMode* GM = AFSGameMode::Get(this))
	{
		if (GM->bPaused)
		{
			return;
		}
		if (GM->Phase == EFSPhase::GameOver)
		{
			GM->RestartAll();
		}
		else if (GM->Phase == EFSPhase::Title)
		{
			GM->StartGame();
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
