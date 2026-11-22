#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FSCommon.h"
#include "FSGameMode.generated.h"

class AFSEnemy;
class AFSPlayerChar;
class AFSGuardian;
class AFSArena;
class UFSSaveGame;

UCLASS()
class AFSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFSGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	static AFSGameMode* Get(const UObject* WorldContext);
	AFSPlayerChar* GetPlayerChar() const;

	// ---- 局内状态
	EFSPhase Phase = EFSPhase::Title;
	int32 Wave = 0;
	int32 Kills = 0;
	int32 Coins = 0;
	int32 RuneCount = 0;
	float RunTime = 0.f;

	// ---- 连击：短时间内连续击杀，金币有额外加成
	int32 Combo = 0;
	int32 BestCombo = 0;
	int32 RunBestCombo = 0;
	float ComboTimer = 0.f;
	float ComboPulse = 0.f;
	static constexpr float ComboWindow = 2.4f;
	int32 GetCombo() const { return Combo; }

	// ---- 局内成长：金币同时当经验用，攒够就升级并随机给一项增益
	int32 Level = 1;
	int32 Xp = 0;
	int32 XpNeed = 60;
	int32 UpgradeIdx = 0;
	float LevelPulse = 0.f;
	int32 GetLevel() const { return Level; }
	int32 GetXp() const { return Xp; }
	int32 GetXpNeed() const { return XpNeed; }
	float GetLevelPct() const { return XpNeed > 0 ? FMath::Clamp((float)Xp / (float)XpNeed, 0.f, 1.f) : 0.f; }
	/** 本波还剩多少敌人：已刷出未死的 + 排队中的 */
	int32 GetWaveRemaining() const;

	/**
	 * 敌人时间流速（1 = 正常）。玩家主动技「时缓」直接改它。
	 * 只影响敌人与敌人弹幕——玩家自己保持原速，所以这是"时间差"而不是"整体变慢"。
	 */
	float EnemyTimeScale = 1.f;

	// ---- 清波赐福：每清完一波弹三张卡，玩家挑一张。
	// 选卡期间波次推进被挂起（InterWave 不递减），所以不会出现"一边选一边刷怪"
	bool bChoosing = false;
	int32 ChoiceSel = 0;
	float ChoiceElapsed = 0.f;
	TArray<EFSUpgrade> Choices;
	/** 与 Choices 同序的稀有度 */
	TArray<EFSRarity> ChoiceRarity;
	/** BOSS 波清空时置位：下一次赐福至少保底一张传世 */
	bool bBossBlessGuarantee = false;
	/** 调试：-fsrarity=N 强制所有卡为该稀有度（-1 = 正常投骰） */
	int32 ForceRarity = -1;
	/** 调试：-fspath=N 让机器人在选卡时优先挑第 N 条流派（-1 = 随机） */
	int32 ForcePath = -1;

	static constexpr int32 ChoiceSlots = 3;
	/** 超时保护：没人操作就自动选当前高亮项，避免录像/挂机卡死 */
	static constexpr float ChoiceLimit = 15.f;

	bool IsChoosing() const { return bChoosing; }
	/** 入场动画进度 0→1 */
	float GetChoiceIn() const { return FMath::Clamp(ChoiceElapsed * 5.f, 0.f, 1.f); }
	float GetChoiceLeftPct() const { return FMath::Clamp(1.f - ChoiceElapsed / ChoiceLimit, 0.f, 1.f); }
	float GetChoiceLeftTime() const { return FMath::Max(0.f, ChoiceLimit - ChoiceElapsed); }

	void OpenBlessing();
	void BlessingMove(int32 Delta);
	void BlessingPick(int32 Slot);
	/** 鼠标点卡：坐标是视口像素，命中则选走 */
	bool BlessingClickAt(float MouseX, float MouseY, float ScreenW, float ScreenH);
	/**
	 * 稀有度投骰：波次越高越容易出好卡，走在自己流派上的卡再升一档。
	 * 抽签与测试都走这里，保证"卡面写的"和"实际给的"是同一次结果。
	 */
	EFSRarity RollRarity(EFSPath Path, EFSPath Dominant, int32 DominantStacks) const;
	/** 某条流派累计到 3 层：横幅 + 特效 + 音效 */
	void AnnouncePathAwaken(EFSPath P);

	// ---- 展示
	FString BannerText;
	FString BannerSub;
	float BannerTime = 0.f;
	float DamageFlash = 0.f;

	/**
	 * 开招时的屏幕色脉冲：1 → 0 在 0.42 秒内褪完，颜色由主动技绑定的流派决定。
	 * 放在 GameMode 而不是 Player，是因为它属于"屏幕状态"，和 DamageFlash 同一层，
	 * HUD 只需要一个来源；谁放招谁负责把它置 1（见 AFSPlayerChar::CastAbility）。
	 */
	float ArtFlash = 0.f;
	FLinearColor ArtFlashColor = FLinearColor::White;

	// ---- 波次
	TArray<EFSEnemyKind> SpawnQueue;
	/** 与 SpawnQueue 同长：每一项该刷成什么词缀（None = 普通） */
	TArray<EFSElite> SpawnEliteQueue;
	/** 本波精英数量，HUD 用来在开波横幅里提一句 */
	int32 WaveElites = 0;
	float SpawnTimer = 0.f;
	float InterWave = 0.f;
	float HPScale = 1.f;
	bool bWaveActive = false;

	// ---- 天气
	EFSWeather Weather = EFSWeather::Clear;
	float WeatherTimer = 30.f;
	float EnemySpeedMul = 1.f;
	FLinearColor LightColorTarget = FLinearColor(1.f, 0.96f, 0.90f);
	float LightIntensityTarget = 7.f;

	// ---- 守护兽
	float GuardianLeft = 0.f;
	bool bGuardianOn = false;

	// ---- 存档
	int32 BestWave = 0;
	int32 BestKills = 0;
	int32 TotalKills = 0;
	int32 TotalCoins = 0;
	bool bUnlocked[6] = { true, true, false, false, false, false };
	/** 通关次数与最快通关用时（秒，0 = 还没通关过） */
	int32 ClearCount = 0;
	float BestClearTime = 0.f;

	// ---- 终局
	/** 打完这一波即通关。叫"终局波"而不是"最终波"，因为它只结束"首次通关"，之后还能继续无尽 */
	static constexpr int32 MaxWaves = 30;
	/** 这一局已经通关过（继续无尽后不再重复判定，也不会重复结算） */
	bool bRunCleared = false;
	bool IsFinalWave() const { return Wave >= MaxWaves && !bRunCleared; }
	bool IsEndless() const { return bRunCleared; }
	/** 通关：第 30 波清空时调用。不写"死亡"那套，否则玩家分不清自己是赢了还是输了 */
	void WinGame();
	/** 通关后继续打：Phase 回到 Playing，波次继续往上，但不再有终点 */
	void ContinueEndless();

	// ---- 怪物列表（存活）
	TArray<TWeakObjectPtr<AFSEnemy>> Enemies;

	const float ArenaHalf = 3000.f;

	// ---- 暂停菜单
	bool bPaused = false;
	int32 MenuIndex = 0;
	int32 LanguageMode = 0;
	static constexpr int32 MenuItemCount = 7;

	void TogglePause();
	void SetPaused(bool b);
	void MenuMove(int32 Delta);
	void MenuActivate();
	/** 鼠标点击命中检测（坐标是视口像素） */
	bool MenuClickAt(float MouseX, float MouseY, float ScreenW, float ScreenH);
	FString GetMenuItemLabel(int32 Index) const;
	void ToggleDisplayMode();
	void CycleVolume();
	void CycleLanguage();
	void BackToTitle();
	/** 把音量/语言写进存档（不动战绩数据） */
	void PersistSettings();

	FString GetMapName() const;

	void StartGame();
	void EndGame();
	void RestartAll();
	void OnPlayerHurt();

	/**
	 * 顿帧：在一段真实时间里把世界时间压慢，用来给"免死"这种判决性瞬间加冲击力。
	 * 时长按真实时间计（不受当前膨胀影响），所以连按几次也只是拉长不停叠加。
	 */
	void RequestHitStop(float Scale, float RealSeconds);
	void StopHitStop();
	bool IsHitStopActive() const { return bHitStop; }
	void OnEnemyKilled(AFSEnemy* E, const FVector& Loc);
	/** BOSS 进入下一阶段：换横幅文案、全屏闪一下、补音效。两种 BOSS 的文案不同，所以要把本体传进来 */
	void AnnounceBossPhase(AFSEnemy* Boss, int32 Phase, const FVector& Loc);
	/** 返回生成的实例，便于调试脚本直接对它施加伤害验证机制 */
	AFSEnemy* SpawnEnemyAt(EFSEnemyKind Kind, const FVector& Loc, float HpMul = 1.f, float SizeMul = 1.f,
		EFSElite Elite = EFSElite::None);
	void AddCoins(int32 C);
	void OnRune();
	void SummonGuardian();
	int32 CountAlive() const;
	EFSWeapon PickRandomWeapon() const;

protected:
	void BuildArena();
	void StartNextWave();
	void SetWeather(EFSWeather W, bool bAnnounce);
	void TickWeather(float Dt);
	void TickRain(float Dt);
	void SetBanner(const FString& Main, const FString& Sub, float Duration);
	void SpawnDrop(const FVector& Loc, EFSPickupKind Kind, EFSWeapon Weapon, int32 Value);
	void LoadMeta();
	void SaveMeta();
	void RefreshUnlocks();
	void GrantUpgrade();

	UPROPERTY()
	AFSArena* Arena = nullptr;

	UPROPERTY()
	AFSGuardian* Guardian = nullptr;

	bool bArenaBuilt = false;
	bool bPendingAutoStart = false;
	/** 本局的战绩是否已经写进存档。防止"通关结算 + 阵亡结算"把同一局的击杀算两遍 */
	bool bMetaCounted = false;

	// 顿帧状态：用真实时钟计时，不受 SetGlobalTimeDilation 影响，所以慢放本身
	// 不会把"何时恢复"这件事也一起拖慢。
	bool bHitStop = false;
	double HitStopEndReal = 0.0;
	float HitStopScale = 1.f;
};
