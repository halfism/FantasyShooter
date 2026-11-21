#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FSCommon.h"
#include "FSEnemy.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class USceneComponent;

struct FFSBodyPart
{
	UStaticMeshComponent* Comp = nullptr;
	FLinearColor Color = FLinearColor::White;
	bool bGlow = false;
	float Glow = 4.f;
};

UCLASS()
class AFSEnemy : public AActor
{
	GENERATED_BODY()

public:
	AFSEnemy();

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	/** SizeMul 只影响体型（分裂出的小怪用它缩一圈），血量另由 HPScale 控制 */
	void Setup(EFSEnemyKind InKind, float HPScale, float SizeMul = 1.f, EFSElite InElite = EFSElite::None);

	bool IsDead() const { return bDying; }
	EFSEnemyKind GetKind() const { return Kind; }

	// ---- 精英词缀
	EFSElite GetElite() const { return Elite; }
	bool IsElite() const { return Elite != EFSElite::None; }
	/** 爆裂精英死后引信剩余时间（0 = 没有引爆中），HUD 可用来画爆炸预警 */
	float GetVolatileFuse() const { return VolatileFuse; }
	/** 引信总长，配合上面算进度 */
	static constexpr float VolatileFuseDur = 0.55f;
	static constexpr float VolatileRadius = 330.f;
	static constexpr float VolatileDamage = 26.f;
	/** 精英地面环的半径倍数（相对自身 Radius）。建模型与每帧呼吸都用这一份，改一处即可 */
	static constexpr float EliteRingScale = 1.55f;
	float GetHealthPct() const { return MaxHP > 0.f ? FMath::Clamp(HP / MaxHP, 0.f, 1.f) : 0.f; }
	/** 调试自检用：直接读血量，验证减伤是否真的生效 */
	float GetHealth() const { return HP; }
	float GetMaxHealth() const { return MaxHP; }
	float GetRadius() const { return Radius; }

	// ---- BOSS 多阶段
	bool IsBoss() const
	{
		return Kind == EFSEnemyKind::Boss || Kind == EFSEnemyKind::Warlord || Kind == EFSEnemyKind::Tyrant;
	}
	/** 第 30 波的终局 BOSS：HUD 要给它一套专属配色 */
	bool IsFinalBoss() const { return Kind == EFSEnemyKind::Tyrant; }
	int32 GetBossPhase() const { return BossPhase; }
	/** 阶段切换中的硬直/无敌窗口，HUD 依据它画护盾闪光 */
	bool IsPhaseShifting() const { return PhaseShift > 0.f; }
	float GetPhaseShiftPct() const { return PhaseShiftDur > 0.f ? FMath::Clamp(PhaseShift / PhaseShiftDur, 0.f, 1.f) : 0.f; }
	/** 进入下一阶段的血量比例：第一阶段 0.66，第二阶段 0.33 */
	static float PhaseGate(int32 Phase) { return (Phase <= 1) ? 0.66f : 0.33f; }

	// ---- 重甲：铁卫的盾牌 / 熔铁君王的胸甲都能被正面持续火力"打穿"
	// 甲按"贴到甲上的原始伤害"扣减，和减伤系数解耦——否则正面只吃三成的铁卫
	// 会因为自己减伤而永远打不穿自己，那种设计玩家永远发现不了。
	bool HasArmor() const { return bHasArmor; }
	bool IsArmorBroken() const { return bArmorBroken; }
	float GetArmorPct() const
	{
		return (bHasArmor && ArmorHPMax > 0.f) ? FMath::Clamp(ArmorHP / ArmorHPMax, 0.f, 1.f) : 0.f;
	}
	float GetArmorMaxHp() const { return ArmorHPMax; }
	/** 自检用（-fsprobe）：直接把甲打穿，不必真砸够伤害 */
	void DebugBreakArmor();

	// ---- 熔铁君王：地面震波（HUD / 自检读这两项）
	bool IsSlamWarning() const { return SlamTelegraph > 0.f; }
	bool IsSlamWaveActive() const { return bSlamWave; }
	float GetSlamWaveR() const { return SlamR; }
	float GetSlamWaveMax() const { return SlamRMax; }
	/** 自检用（-fsprobe）：让下一次震波立刻起手，不用真等 3 秒冷却 */
	void DebugForceSlam() { SlamCd = 0.f; }

protected:
	void BuildVisual();
	void Die();
	/** 阶段切换：卡血、无敌硬直、震屏、召唤阶段兵 */
	void EnterBossPhase(int32 NewPhase);
	void BossFireFan(int32 Shots, float Spread, float InSpeed, float Dmg, const FVector& Dir, const FVector& Muzzle);
	void BossFireRing(int32 Shots, float InSpeed, float Dmg, float AngleOffset, const FVector& Muzzle);

	// ---- 熔铁君王专用
	/** 用一圈薄方块拼出震波环（BasicShapes 没有圆环，而"看得见边缘"是这招唯一的公平性来源） */
	void BuildSlamRing();	/** 布环的当前半径与亮度：telegraph 阶段钉在最大半径当危险边界，放波阶段随波前推进 */
	void LayoutSlamRing(float R, float Intensity);
	void UpdateSlam(float DeltaSeconds, const FVector& Dir, float Dist);

	// ---- 重甲
	/** 甲被打穿：卸掉甲片、改判定、给一次明确的视听反馈 */
	void BreakArmor();
	/** 重新锻甲：BOSS 换阶段时整块甲重新长回来（否则破一次就白嫖三阶段） */
	void RefitArmor();
	/** 崩落动画 + 甲片被命中时的微光 */
	void TickArmor(float DeltaSeconds);

	// ---- 精英
	/** 脚下双层环 + 三片环绕碎晶。颜色取词缀色，挂在 Capsule 的地面高度 */
	void BuildEliteFx();
	/** 每帧推进环的呼吸与碎晶环绕；同时跑再生回血 */
	void TickElite(float DeltaSeconds);
	/** 荆棘：被打时若玩家贴身，反弹一次刺伤（有短冷却，免得霰弹一枪触发七次） */
	void EliteThornRetort();
	/** 爆裂：引信到点，炸开一圈并销毁自己 */
	void VolatileBlast();

	void AddPart(const TCHAR* MeshName, const FVector& Loc, const FVector& Scale,
		const FLinearColor& Color, bool bGlow = false, float Glow = 4.f,
		const FRotator& Rot = FRotator::ZeroRotator);

	UPROPERTY()
	UCapsuleComponent* Capsule = nullptr;

	UPROPERTY()
	USceneComponent* VisualRoot = nullptr;

	TArray<FFSBodyPart> Parts;

	EFSEnemyKind Kind = EFSEnemyKind::Slime;
	float HP = 30.f;
	float MaxHP = 30.f;
	float Speed = 300.f;
	float Radius = 45.f;
	float SizeScale = 1.f;
	bool bDying = false;

	float Time = 0.f;
	float Phase = 0.f;
	float FlashTimer = 0.f;
	float ContactCooldown = 0.f;
	float FireCooldown = 2.f;
	float SummonCooldown = 8.f;
	FVector Push = FVector::ZeroVector;
	bool bFlashApplied = false;

	// ---- BOSS 专用状态
	int32 BossPhase = 1;
	float PhaseShift = 0.f;
	float DashCd = 4.5f;
	float DashTelegraph = 0.f;
	float DashTime = 0.f;
	float RingCd = 6.f;
	FVector DashDir = FVector::ForwardVector;
	static constexpr float PhaseShiftDur = 1.4f;

	// ---- 熔铁君王：震波
	TArray<UStaticMeshComponent*> SlamRing;
	float SlamCd = 5.0f;        // 距下一次起手的冷却
	float SlamTelegraph = 0.f;  // > 0 = 正在蓄力（此时环钉在最大半径当危险边界）
	float SlamR = 0.f;          // 波前当前半径
	float SlamRMax = 950.f;
	bool bSlamWave = false;     // 波正在扩散
	bool bSlamHitPlayer = false;// 这一波是否已经打到人（避免同一波连击）
	int32 SlamPending = 0;      // 还欠几波（二阶段起是连震）
	float SlamNextTick = 0.f;   // 连震之间的间隔计时
	float RingIntensity = -1.f;
	bool bRingVisible = false;
	static constexpr float SlamWaveSpeed = 1120.f;
	static constexpr float SlamBand = 88.f;   // 波前判定厚度（单侧）

	// ---- 重甲：正面挨打时先削甲，削穿后正面不再减伤，还额外吃 15%
	bool bHasArmor = false;
	bool bArmorBroken = false;
	float ArmorHP = 0.f;          // 剩余甲值（按原始伤害扣）
	float ArmorHPMax = 0.f;
	float ArmorBreakT = 0.f;      // 崩落动画进度 1→0
	float ArmorFlash = 0.f;       // 甲片被砸中的微光
	float StaggerT = 0.f;         // 破甲后的踉跄：定身 + 压身，给玩家一个输出窗口
	TArray<UStaticMeshComponent*> ArmorParts;
	TArray<FVector> ArmorPartLoc;    // 各甲片的原始相对位置
	TArray<FVector> ArmorPartScale;  // 各甲片的原始缩放

	// ---- 精英词缀状态
	EFSElite Elite = EFSElite::None;
	/** 再生：上次受伤到现在的时间。超过阈值进入"高速回血"，所以是有窗口可趁的 */
	float SinceHurt = 999.f;
	/** 荆棘反伤的冷却 */
	float ThornCd = 0.f;
	/** 爆裂引信：> 0 表示已经死了但还没炸 */
	float VolatileFuse = 0.f;
	float EliteFxT = 0.f;
	/** 脚下那圈词缀色描边 */
	UPROPERTY()
	UStaticMeshComponent* EliteRing = nullptr;
	/** 压在描边上的暗盘：把亮盘掏成环，颜色才对得上"一圈线" */
	UPROPERTY()
	UStaticMeshComponent* EliteRingCore = nullptr;
	/** 爆裂预警环：引信期间从 0 扩到 VolatileRadius，是这招唯一的公平性来源 */
	UPROPERTY()
	UStaticMeshComponent* BlastRing = nullptr;
};
