#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FSCommon.h"
#include "FSEnemy.h"
#include "FSPlayer.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

UCLASS()
class AFSPlayerChar : public ACharacter
{
	GENERATED_BODY()

public:
	AFSPlayerChar();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	void SetMoveInput(const FVector2D& In);
	void SetAimWorld(const FVector& In);
	void SetFiring(bool b);
	void TryDash();
	/**
	 * 流派主动技。
	 * 充能靠"造成伤害"攒——不是冷却计时，所以它奖励的是敢打，而不是敢等。
	 */
	void TryAbility();
	/** 造成伤害时回调（敌人受击处调用）。只算玩家自己打出去的那部分 */
	void AddAbilityCharge(float DamageDealt);
	/** 当前绑定的主动技：取流派层数最高的那条；一条都没选时为 None */
	EFSAbility GetAbilityKind() const;
	bool IsAbilityReady() const { return AbilityCharge >= AbilityChargeMax && !IsAbilityActive(); }
	/** 放完后的充能冷却剩余（秒），HUD 用它解释"为什么条不涨" */
	float GetAbilityLock() const { return AbilityLock; }
	float GetAbilityChargePct() const { return FMath::Clamp(AbilityCharge / AbilityChargeMax, 0.f, 1.f); }
	bool IsAbilityActive() const { return AbilityTimeLeft > 0.f; }
	float GetAbilityTimeLeft() const { return AbilityTimeLeft; }
	float GetAbilityPct() const;
	/** 主动技的伤害乘区（焚场期间 > 1） */
	float GetAbilityDmgMul() const;
	/** 主动技的射速乘区（时缓觉醒期间 > 1） */
	float GetAbilityRofMul() const;
	/** 护盾剩余比例（0 = 没有护盾） */
	float GetShieldPct() const { return ShieldHpMax > 0.f ? FMath::Clamp(ShieldHP / ShieldHpMax, 0.f, 1.f) : 0.f; }
	bool HasShield() const { return ShieldHP > 0.f; }
	/** 绑定主动技的那条流派是否已觉醒（决定招式强度档位） */
	bool IsAbilityAwakened() const;
	/** 主动技伤害窗口：敌人受击时读它，决定这次伤害要不要给充能 */
	bool IsArtDamageLocked() const { return bArtDamageLock; }
	/** 调试（自检 / 摆拍用）：直接把某条流派设为 N 层，并把觉醒外观刷出来 */
	void DebugSetPathStacks(EFSPath P, int32 N);
	// 摆拍/排查用：把脚下那个 ArtRing 的实际半径与材质颜色读回来，
	// 免得靠截图猜"到底有没有生效"
	float DebugGetArtRadius() const;
	FLinearColor DebugGetArtColor() const;
	void DebugSetAbilityCharge(float V) { AbilityCharge = FMath::Clamp(V, 0.f, AbilityChargeMax); }
	int32 GetAbilityCasts() const { return AbilityCasts; }
	void TakeWeapon(EFSWeapon W);
	void Heal(float Amount);
	void AddShake(float Amount);
	void AddNotice(const FString& Text, float Duration = 1.6f);
	void EnableBot() { bBot = true; }
	bool IsBot() const { return bBot; }

	/**
	 * 局内成长：由 GameMode 在升级时调用。
	 * Rarity 决定效果倍率；bCountPath 为 false 时不计流派层数（金币自动升级走这条路，
	 * 保证"流派"只反映玩家主动做的选择）。
	 */
	void ApplyUpgrade(int32 Index, int32 Rarity = 0, bool bCountPath = true);
	/** 流派觉醒后继续叠加时的溢出加成（ApplyUpgrade 内部调用） */
	void ApplyPathOverflow(EFSPath P, float RarityMul);
	/** 增益名称（中/英），HUD 与提示共用 */
	static void UpgradeName(int32 Index, const TCHAR*& OutCN, const TCHAR*& OutEN);
	const FString& GetNotice() const { return NoticeText; }
	float GetNoticeAlpha() const;

	/** 某类赐福已经叠了几层，卡片上要显示"已有 N 层" */
	int32 GetUpgradeStacks(int32 Index) const
	{
		return (Index >= 0 && Index < (int32)EFSUpgrade::Count) ? UpStacks[Index] : 0;
	}
	/** 某条流派累计了几层 */
	int32 GetPathStacks(EFSPath P) const
	{
		return ((int32)P >= 0 && (int32)P < (int32)EFSPath::Count) ? PathStacks[(int32)P] : 0;
	}
	bool IsPathAwakened(EFSPath P) const { return GetPathStacks(P) >= FSDefs::PathAwakenStacks; }
	/** 身上光环代表的流派（-1 = 还没觉醒过任何一条） */
	int32 GetAwakenPath() const { return AwakenPath; }
	/** 刚选过某流派赐福的脉冲（1→0），HUD 用来闪一下对应的流派条 */
	float GetPathPulse(EFSPath P) const
	{
		const int32 I = (int32)P;
		return (I >= 0 && I < (int32)EFSPath::Count) ? PathPulse[I] : 0.f;
	}
	/**
	 * 觉醒之后又叠到的层数。
	 * 觉醒是"质变"，该系赐福随即退出抽签池；剩下的层数不再换新被动，
	 * 但也不能白给——按流派折算成小额常驻加成，HUD 以 "+N" 显示。
	 */
	int32 GetOverflowStacks(EFSPath P) const
	{
		const int32 I = (int32)P;
		return (I >= 0 && I < (int32)EFSPath::Count) ? OverflowStacks[I] : 0;
	}
	float GetMagnetScale() const { return UpMagnet; }
	/** 击杀回血（嗜血赐福），GameMode 结算击杀时读 */
	float GetLeechPerKill() const { return UpLeech; }
	/** 受伤减免倍率：1 = 无减免 */
	float GetDamageTakenScale() const { return DmgTakenScale; }
	/** 还有多久能再次"不屈"免死（0 = 已就绪） */
	float GetDeathSaveLeft() const { return DeathSaveCd; }
	/** 不屈冷却进度：1 = 就绪，0 = 刚被判死又活下来 */
	float GetDeathSavePct() const;
	/** 免死瞬间的全屏闪光（1→0），HUD 用来画那一下绿闪 */
	float GetDeathSaveFlash() const { return DeathSaveFlash; }
	/** 残影射速窗口剩余比例：1 = 刚闪避，0 = 窗口结束。未觉醒恒为 0 */
	float GetSurgePct() const;
	/** 短暂无敌（换卡时的喘息，免得刚选着就被摸死） */
	void GrantInvuln(float T) { Invuln = FMath::Max(Invuln, T); }

	bool IsAlive() const { return bAlive && HP > 0.f; }
	float GetHealthPct() const { return FMath::Clamp(HP / MaxHP, 0.f, 1.f); }
	EFSWeapon GetWeapon() const { return Weapon; }
	int32 GetWeaponLevel() const { return WeaponLevel; }
	int32 GetAmmo() const { return Ammo; }
	float GetAimYaw() const { return AimYaw; }
	const FVector& GetAimPoint() const { return AimPoint; }
	float GetDashCdPct() const { return FMath::Clamp(DashCooldown / (DashCdMax * DashCdScale), 0.f, 1.f); }
	int32 GetUpgradeCount() const { return UpgradeCount; }

protected:
	void BuildBody();
	void FireOnce();
	void Die();
	FVector MuzzleLocation() const;

	// ---- 流派主动技
	/** 建好三套共用的地面光环（平时全隐藏，施放时按招式调半径与颜色） */
	void BuildAbilityFx();
	void CastAbility(EFSAbility A);
	/** 招式结束：收掉特效、把敌人时间流速还给世界 */
	void EndAbility();
	/** 每帧推进：持续时间、焚场跳伤、护盾计时 */
	void TickAbility(float DeltaSeconds);
	/** 护盾被打穿（或到期）时的环形爆发 */
	void BurstShield();
	/** 以自身为圆心的伤害判定，返回命中数 */
	int32 DamageAroundSelf(float Radius, float Damage);

	/** 觉醒后把脚下光环改色、亮出环绕碎晶；未觉醒时全隐藏 */
	void RefreshAwakenBody();
	/** 每帧推进觉醒光环的旋转与呼吸 */
	void TickAwakenBody(float DeltaSeconds);
	/** 残影窗口的脚下收拢环：剩余时间越少，环收得越紧 */
	void TickSurgeRing();

	UPROPERTY()
	USpringArmComponent* SpringArm = nullptr;

	/** 相机俯角基准。调试 -fscam 会改它，正常运行时固定 -55 */
	float CamPitchBase = -55.f;

	UPROPERTY()
	UCameraComponent* Camera = nullptr;

	UPROPERTY()
	USceneComponent* BodyRoot = nullptr;

	UPROPERTY()
	UStaticMeshComponent* GunMesh = nullptr;

	TArray<FFSBodyPart> Parts;

	FVector2D MoveInput = FVector2D::ZeroVector;
	FVector AimPoint = FVector::ZeroVector;
	float AimYaw = 0.f;

	float HP = 100.f;
	float MaxHP = 100.f;
	bool bAlive = true;
	bool bFiring = false;

	EFSWeapon Weapon = EFSWeapon::Pistol;
	int32 WeaponLevel = 1;
	int32 Ammo = 0;
	float FireCooldown = 0.f;
	float WeaponSwitchFlash = 0.f;

	float DashCooldown = 0.f;
	float DashTime = 0.f;
	float Invuln = 0.f;
	FVector DashDir = FVector::ForwardVector;

	// ---- 局内成长（金币升级 / 清波赐福带来的乘区）
	float UpDmg = 1.f;
	float UpRof = 1.f;
	float UpMove = 1.f;
	float DashCdScale = 1.f;
	float UpPierce = 0.f;      // 加算到武器自带的穿透数
	float UpCrit = 0.f;        // 暴击概率 0..1
	float UpMagnet = 1.f;      // 拾取半径与吸取速度的倍率
	float DmgTakenScale = 1.f; // 护壁：受到伤害的乘区（越小越硬）
	float UpLeech = 0.f;       // 嗜血：每次击杀回复的生命
	int32 UpgradeCount = 0;
	int32 UpStacks[(int32)EFSUpgrade::Count] = {};

	// ---- 流派：同类赐福累计到 3 层触发觉醒，给一条专属被动
	int32 PathStacks[(int32)EFSPath::Count] = {};
	float DeathSaveCd = 0.f;   // 壁垒觉醒"不屈"的冷却
	float DeathSaveCdMax = 45.f;               // 免死冷却总长，HUD 算进度用
	float DeathSaveFlash = 0.f;                // 免死瞬间的全屏闪光（1→0）
	float DashSurge = 0.f;     // 疾影觉醒"残影"：闪避后的强化射速窗口
	float DashSurgeMax = 0.80f;                // 窗口总长，HUD 算进度用
	float SurgeTick = 0.f;                     // 窗口期内的时间累加，驱动准星弧的角度
	float PathPulse[(int32)EFSPath::Count] = {};  // 选卡反馈脉冲
	int32 OverflowStacks[(int32)EFSPath::Count] = {};  // 觉醒后继续叠加的溢出层数

	// 觉醒外观：脚下光环改流派色 + 三个环绕碎晶。AwakenPath < 0 时全部隐藏
	int32 AwakenPath = -1;
	float AwakenT = 0.f;
	float HaloT = 0.f;         // 光环本身的呼吸相位（未觉醒也在走）

	UPROPERTY()
	UStaticMeshComponent* HaloOuter = nullptr;

	UPROPERTY()
	UStaticMeshComponent* HaloInner = nullptr;

	UPROPERTY()
	UStaticMeshComponent* AwakenRing = nullptr;

	/** 压在觉醒环上的暗盘：把它掏成环，否则整块是发光圆饼 */
	UPROPERTY()
	UStaticMeshComponent* AwakenRingCore = nullptr;

	UPROPERTY()
	TArray<UStaticMeshComponent*> AwakenShards;

	/** 残影窗口的外显：一圈会向内收的青色细环（外 + 暗芯两层） */
	UPROPERTY()
	UStaticMeshComponent* SurgeRing = nullptr;

	UPROPERTY()
	UStaticMeshComponent* SurgeRingCore = nullptr;

	float Shake = 0.f;
	float ShakeT = 0.f;
	float HitFlash = 0.f;

	// ---- 流派主动技
	static constexpr float AbilityChargeMax = 100.f;
	/** 每点伤害换多少充能：约 1100 点伤害充满一次 */
	static constexpr float AbilityChargePerDamage = 0.09f;
	/** 时缓的敌人时间流速：未觉醒 / 觉醒 */
	static constexpr float SlowScaleBase = 0.32f;
	static constexpr float SlowScaleAwake = 0.24f;
	/** 焚场暗芯相对亮盘的半径比：留出的那一圈就是"火线"的宽度 */
	static constexpr float PyreCoreMul = 0.92f;
	/**
	 * 放完之后的"充能冷却"：这段时间里伤害不再攒充能。
	 * 没有它的话，伤害越高充得越快——实测后期每 8 秒就能放一次，
	 * 3.2 秒的时缓覆盖掉四成战斗时间，"大招"直接变成常驻光环。
	 */
	static constexpr float AbilityLockBase = 9.0f;
	static constexpr float AbilityLockAwake = 8.0f;

	float AbilityCharge = 0.f;
	float AbilityTimeLeft = 0.f;
	float AbilityDuration = 1.f;
	/** 放完后的充能冷却（秒），期间伤害不攒充能 */
	float AbilityLock = 0.f;
	EFSAbility ActiveAbility = EFSAbility::None;
	/** 焚场 / 护盾的剩余量，两个招式各有解读 */
	float ShieldHP = 0.f;
	float ShieldHpMax = 0.f;
	float ShieldAbsorbed = 0.f;
	float ArtTickAcc = 0.f;      // 焚场的跳伤计时
	float ArtPulse = 0.f;        // 特效呼吸相位
	float ArtCastFlash = 0.f;    // 施放瞬间的闪光（HUD 用）
	int32 AbilityCasts = 0;
	/** 主动技自身伤害期间置位：这段时间玩家不攒充能，否则焚场会自己养自己 */
	bool bArtDamageLock = false;

	/** 招式脚下光环：外圈亮色 + 内圈暗芯夹出描边，和精英环同一套做法 */
	UPROPERTY()
	UStaticMeshComponent* ArtRing = nullptr;

	UPROPERTY()
	UStaticMeshComponent* ArtCore = nullptr;

	FString NoticeText;
	float NoticeTime = 0.f;

	float StepPhase = 0.f;
	bool bBot = false;

	static constexpr float DashCdMax = 2.0f;
};

UCLASS()
class AFSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFSPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnFireStart(const FInputActionValue& Value);
	void OnFireStop(const FInputActionValue& Value);
	void OnDash(const FInputActionValue& Value);
	/** 流派主动技：Q / 右键 / 手柄 Y */
	void OnAbility(const FInputActionValue& Value);
	void OnConfirm(const FInputActionValue& Value);
	/** 通关后按 E：不进新局，接着打无尽。只在 Victory 阶段有效 */
	void OnEndless(const FInputActionValue& Value);

	/** 暂停/恢复时清掉残留的移动输入，避免恢复瞬间角色自己跑 */
	void ClearMoveInput() { PendingMove = FVector2D::ZeroVector; }

	AFSPlayerChar* GetChar() const;

protected:
	void EnsureInput();
	bool ComputeAim(FVector& OutPoint) const;

	UPROPERTY()
	UInputMappingContext* IMC = nullptr;

	UPROPERTY()
	UInputAction* IA_Move = nullptr;

	UPROPERTY()
	UInputAction* IA_Fire = nullptr;

	UPROPERTY()
	UInputAction* IA_Dash = nullptr;

	UPROPERTY()
	UInputAction* IA_Ability = nullptr;

	UPROPERTY()
	UInputAction* IA_Endless = nullptr;

	UPROPERTY()
	UInputAction* IA_Confirm = nullptr;

	UPROPERTY()
	UInputAction* IA_Look = nullptr;

	bool bInputReady = false;
	FVector2D PendingMove = FVector2D::ZeroVector;

	/** 手柄右摇杆瞄准（幅值 > 阈值时接管准星） */
	FVector2D StickLook = FVector2D::ZeroVector;
	float StickLookTime = 0.f;
	float MuteToast = 0.f;
};
