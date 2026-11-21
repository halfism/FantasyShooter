#include "FSGameMode.h"
#include "FSUtil.h"
#include "FSArena.h"
#include "FSEnemy.h"
#include "FSPlayer.h"
#include "FSPickup.h"
#include "FSHUD.h"
#include "FSSaveGame.h"
#include "FSFx.h"
#include "FSAudio.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

static bool GFS_AutoStartNextRun = false;

static FVector FSRandomSpawnRing(UWorld* W, const FVector& PlayerLoc, float Half, float Radius)
{
	for (int32 i = 0; i < 14; ++i)
	{
		const float A = FMath::FRandRange(0.f, 6.2831f);
		const FVector P = PlayerLoc + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * Radius;
		if (FMath::Abs(P.X) < Half - 260.f && FMath::Abs(P.Y) < Half - 260.f)
		{
			return FVector(P.X, P.Y, 0.f);
		}
	}
	return FVector(FMath::FRandRange(-Half + 400.f, Half - 400.f),
		FMath::FRandRange(-Half + 400.f, Half - 400.f), 0.f);
}

AFSGameMode::AFSGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AFSPlayerChar::StaticClass();
	PlayerControllerClass = AFSPlayerController::StaticClass();
	HUDClass = AFSHUD::StaticClass();
	bStartPlayersAsSpectators = false;
}

AFSGameMode* AFSGameMode::Get(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}
	const UWorld* W = WorldContext->GetWorld();
	if (!W)
	{
		return nullptr;
	}
	return Cast<AFSGameMode>(W->GetAuthGameMode());
}

AFSPlayerChar* AFSGameMode::GetPlayerChar() const
{
	if (const UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			return Cast<AFSPlayerChar>(PC->GetPawn());
		}
	}
	return nullptr;
}

void AFSGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}
	// 地图里没有 PlayerStart，直接把角色放在场地中心
	RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 92.f)));
}

void AFSGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!bArenaBuilt)
	{
		bArenaBuilt = true;
		Arena = GetWorld()->SpawnActor<AFSArena>(AFSArena::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Arena)
		{
			Arena->Build(ArenaHalf);
		}
	}

	LoadMeta();
	RefreshUnlocks();

	// 引擎默认 BloomQuality=4、强度 0.675，自发光物件再亮一点就会被糊成"奶白色光团"。
	// 这里放一个全局后处理只压泛光，不改曝光，保留发光颜色又不至于糊成一片。
	if (UWorld* W = GetWorld())
	{
		APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>();
		if (PP)
		{
			PP->bUnbound = true;
			PP->BlendWeight = 1.f;
			PP->Settings.bOverride_BloomIntensity = true;
			PP->Settings.BloomIntensity = 0.28f;
		}
	}

	// ---- 调试：-fslang=1 强制中文 / -fslang=2 强制英文，用来检查英文排版
	//          -fsvol=0.4 直接设定音量；两者都会落盘，方便验证设置持久化
	{
		bool bPersist = false;
		FString LangArg;
		if (FParse::Value(FCommandLine::Get(), TEXT("fslang="), LangArg))
		{
			LanguageMode = FMath::Clamp(FCString::Atoi(*LangArg), 0, 2);
			FSUtil::SetLanguageMode(LanguageMode);
			bPersist = true;
		}
		FString VolArg;
		if (FParse::Value(FCommandLine::Get(), TEXT("fsvol="), VolArg))
		{
			FSAudio::SetMasterVolume(this, FCString::Atof(*VolArg));
			bPersist = true;
		}
		if (bPersist)
		{
			PersistSettings();
		}
	}

	SetWeather(EFSWeather::Clear, false);
	WeatherTimer = 30.f;

	if (GFS_AutoStartNextRun)
	{
		GFS_AutoStartNextRun = false;
		bPendingAutoStart = true;
	}
	else
	{
		Phase = EFSPhase::Title;
		BannerText.Empty();
		BannerSub.Empty();
		BannerTime = 0.f;
	}
}

void AFSGameMode::LoadMeta()
{
	UFSSaveGame* S = Cast<UFSSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FS_Meta"), 0));
	if (!S)
	{
		return;
	}

	BestWave = S->BestWave;
	BestKills = S->BestKills;
	BestCombo = S->BestCombo;
	TotalKills = S->TotalKills;
	TotalCoins = S->TotalCoins;

	// 设置项：音量与语言在每次启动时恢复，不然玩家每次都得重调一遍
	if (S->MasterVolume >= 0.f)
	{
		FSAudio::SetMasterVolume(this, S->MasterVolume);
	}
	LanguageMode = FMath::Clamp(S->LanguageMode, 0, 2);
	FSUtil::SetLanguageMode(LanguageMode);
}

void AFSGameMode::PersistSettings()
{
	UFSSaveGame* S = Cast<UFSSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FS_Meta"), 0));
	if (!S)
	{
		S = Cast<UFSSaveGame>(UGameplayStatics::CreateSaveGameObject(UFSSaveGame::StaticClass()));
	}
	if (!S)
	{
		return;
	}
	S->MasterVolume = FSAudio::GetMasterVolume(this);
	S->LanguageMode = LanguageMode;
	UGameplayStatics::SaveGameToSlot(S, TEXT("FS_Meta"), 0);
}

void AFSGameMode::SaveMeta()
{
	UFSSaveGame* S = Cast<UFSSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("FS_Meta"), 0));
	if (!S)
	{
		S = Cast<UFSSaveGame>(UGameplayStatics::CreateSaveGameObject(UFSSaveGame::StaticClass()));
	}
	if (!S)
	{
		return;
	}
	S->BestWave = FMath::Max(S->BestWave, Wave);
	S->BestKills = FMath::Max(S->BestKills, Kills);
	S->BestCombo = FMath::Max(S->BestCombo, BestCombo);
	S->TotalKills += Kills;
	S->TotalCoins += Coins;
	S->Runs += 1;

	// 战绩保存时顺手把设置一起带上，避免两份存档互相覆盖
	S->MasterVolume = FSAudio::GetMasterVolume(this);
	S->LanguageMode = LanguageMode;

	UGameplayStatics::SaveGameToSlot(S, TEXT("FS_Meta"), 0);

	BestWave = S->BestWave;
	BestKills = S->BestKills;
	BestCombo = S->BestCombo;
	TotalKills = S->TotalKills;
	TotalCoins = S->TotalCoins;
	RefreshUnlocks();
}

void AFSGameMode::RefreshUnlocks()
{
	bUnlocked[0] = true;
	bUnlocked[1] = true;
	bUnlocked[2] = TotalKills >= 20;
	bUnlocked[3] = TotalKills >= 70;
	bUnlocked[4] = TotalKills >= 150;
	bUnlocked[5] = TotalKills >= 280;
}

void AFSGameMode::StartGame()
{
	Phase = EFSPhase::Playing;
	Wave = 0;
	Kills = 0;
	Coins = 0;
	RuneCount = 0;
	RunTime = 0.f;
	SpawnQueue.Reset();
	InterWave = 1.4f;
	bWaveActive = false;
	HPScale = 1.f;
	GuardianLeft = 0.f;
	bGuardianOn = false;
	DamageFlash = 0.f;
	Combo = 0;
	RunBestCombo = 0;
	ComboTimer = 0.f;
	ComboPulse = 0.f;

	Level = 1;
	Xp = 0;
	XpNeed = 60;
	UpgradeIdx = 0;
	LevelPulse = 0.f;

	bChoosing = false;
	ChoiceSel = 0;
	ChoiceElapsed = 0.f;
	Choices.Reset();
	ChoiceRarity.Reset();
	bBossBlessGuarantee = false;

	for (const TWeakObjectPtr<AFSEnemy>& W : Enemies)
	{
		if (AFSEnemy* E = W.Get())
		{
			E->Destroy();
		}
	}
	Enemies.Reset();

	SetWeather(EFSWeather::Clear, false);
	WeatherTimer = 32.f;

	SetBanner(FSUtil::L(TEXT("生   存"), TEXT("SURVIVE")),
		FSUtil::L(TEXT("活下来，打得越久分越高"), TEXT("STAY ALIVE AS LONG AS YOU CAN")), 2.4f);
	FSAudio::Play2D(this, EFSSfx::WaveStart, 0.8f);
}

void AFSGameMode::EndGame()
{
	if (Phase != EFSPhase::Playing)
	{
		return;
	}
	Phase = EFSPhase::GameOver;
	SpawnQueue.Reset();
	SaveMeta();

	BannerTime = 0.f;

	if (AFSPlayerChar* P = GetPlayerChar())
	{
		P->AddShake(24.f);
	}
}

void AFSGameMode::RestartAll()
{
	// 重开前必须把时间膨胀恢复，否则新关卡会一直处于暂停态
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(W, 1.f);
	}
	bHitStop = false;
	HitStopEndReal = 0.0;
	bPaused = false;

	SaveMeta();
	GFS_AutoStartNextRun = true;
	UGameplayStatics::OpenLevel(this, TEXT("/Game/Maps/Arena"));
}

// ------------------------------------------------------------------ 暂停菜单

void AFSGameMode::TogglePause()
{
	SetPaused(!bPaused);
}

void AFSGameMode::SetPaused(bool b)
{
	if (bPaused == b)
	{
		return;
	}
	bPaused = b;
	if (b)
	{
		MenuIndex = 0;
	}

	// 用极小时间膨胀冻结世界，而不是引擎暂停：
	// 这样 HUD 照常绘制、鼠标照常响应，暂停菜单才点得动
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(W, b ? 0.0001f : 1.f);
	}

	if (AFSPlayerChar* P = GetPlayerChar())
	{
		P->SetFiring(false);
		P->SetMoveInput(FVector2D::ZeroVector);
	}

	if (UWorld* W = GetWorld())
	{
		if (AFSPlayerController* PC = Cast<AFSPlayerController>(W->GetFirstPlayerController()))
		{
			PC->ClearMoveInput();
			PC->bShowMouseCursor = true;
		}
	}
}

void AFSGameMode::MenuMove(int32 Delta)
{
	if (!bPaused)
	{
		return;
	}
	MenuIndex = (MenuIndex + Delta + MenuItemCount) % MenuItemCount;
	FSAudio::Play2D(this, EFSSfx::Beep, 0.5f, 1.15f);
}

void AFSGameMode::CycleVolume()
{
	const float Cur = FSAudio::GetMasterVolume(this);
	float Next = 1.f;
	if (Cur > 0.85f)      { Next = 0.70f; }
	else if (Cur > 0.55f) { Next = 0.40f; }
	else if (Cur > 0.25f) { Next = 0.15f; }
	else if (Cur > 0.02f) { Next = 0.f; }
	else                  { Next = 1.f; }

	FSAudio::SetMasterVolume(this, Next);
	FSAudio::Play2D(this, EFSSfx::Beep, 0.9f, 1.0f);
	PersistSettings();
}

void AFSGameMode::CycleLanguage()
{
	LanguageMode = (LanguageMode + 1) % 3;
	FSUtil::SetLanguageMode(LanguageMode);
	FSAudio::Play2D(this, EFSSfx::Beep, 0.9f, 1.05f);
	PersistSettings();
}

void AFSGameMode::ToggleDisplayMode()
{
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!S)
	{
		return;
	}
	if (S->GetFullscreenMode() == EWindowMode::Windowed)
	{
		S->SetFullscreenMode(EWindowMode::WindowedFullscreen);
	}
	else
	{
		S->SetFullscreenMode(EWindowMode::Windowed);
		S->SetScreenResolution(FIntPoint(1280, 720));
	}
	S->ApplyResolutionSettings(false);
	// 记到引擎自己的配置里，下次启动保持上次选择的显示模式
	S->SaveSettings();
	FSAudio::Play2D(this, EFSSfx::Beep, 0.9f, 1.0f);
}

void AFSGameMode::BackToTitle()
{
	SaveMeta();
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(W, 1.f);
	}
	GFS_AutoStartNextRun = false;
	UGameplayStatics::OpenLevel(this, TEXT("/Game/Maps/Arena"));
}

void AFSGameMode::MenuActivate()
{
	if (!bPaused)
	{
		return;
	}
	switch (MenuIndex)
	{
	case 0:
		FSAudio::Play2D(this, EFSSfx::Beep, 0.8f, 1.2f);
		SetPaused(false);
		break;

	case 1:
		FSAudio::Play2D(this, EFSSfx::Beep, 0.8f, 0.9f);
		RestartAll();
		break;

	case 2:
		CycleVolume();
		break;

	case 3:
		CycleLanguage();
		break;

	case 4:
		ToggleDisplayMode();
		break;

	case 5:
		// 回标题页也会保留战绩与设置
		FSAudio::Play2D(this, EFSSfx::Beep, 0.8f, 1.1f);
		BackToTitle();
		break;

	case 6:
		// 退出前先把存档落盘
		SaveMeta();
		if (UWorld* W = GetWorld())
		{
			UGameplayStatics::SetGlobalTimeDilation(W, 1.f);
			UKismetSystemLibrary::QuitGame(this, W->GetFirstPlayerController(), EQuitPreference::Quit, false);
		}
		break;

	default:
		break;
	}
}

bool AFSGameMode::MenuClickAt(float MouseX, float MouseY, float ScreenW, float ScreenH)
{
	if (!bPaused)
	{
		return false;
	}
	for (int32 i = 0; i < MenuItemCount; ++i)
	{
		if (FSUtil::MenuItemRect(ScreenW, ScreenH, i, MenuItemCount).Contains(MouseX, MouseY))
		{
			MenuIndex = i;
			MenuActivate();
			return true;
		}
	}
	return false;
}

FString AFSGameMode::GetMenuItemLabel(int32 Index) const
{
	switch (Index)
	{
	case 0:
		return FSUtil::L(TEXT("继续游戏"), TEXT("RESUME"));

	case 1:
		return FSUtil::L(TEXT("重新开始"), TEXT("RESTART"));

	case 2:
		return FS_FMT(TEXT("音效音量   %d%%"), TEXT("SFX VOLUME   %d%%"),
			FMath::RoundToInt(FSAudio::GetMasterVolume(this) * 100.f));

	case 3:
		// 语言项显示当前值，方便判断按几次能到自己想要的语言
		if (LanguageMode == 1)
		{
			return FSUtil::L(TEXT("语言   中文"), TEXT("LANGUAGE   CHINESE"));
		}
		if (LanguageMode == 2)
		{
			return FSUtil::L(TEXT("语言   English"), TEXT("LANGUAGE   ENGLISH"));
		}
		return FSUtil::L(TEXT("语言   跟随系统"), TEXT("LANGUAGE   AUTO"));

	case 4:
		// 状态文字容易和实际窗口模式对不上（-windowed 等启动参数），这里只写动作
		return FSUtil::L(TEXT("切换   全屏 / 窗口"), TEXT("TOGGLE   FULLSCREEN"));

	case 5:
		return FSUtil::L(TEXT("返回标题"), TEXT("BACK TO TITLE"));

	case 6:
		return FSUtil::L(TEXT("退出游戏"), TEXT("QUIT GAME"));

	default:
		return FString();
	}
}

FString AFSGameMode::GetMapName() const
{
	if (!Arena)
	{
		return TEXT("");
	}
	return FSUtil::bChinese()
		? FString(Arena->GetThemeNameCN())
		: FString(Arena->GetThemeNameEN());
}

void AFSGameMode::OnPlayerHurt()
{
	DamageFlash = 1.f;
}

void AFSGameMode::RequestHitStop(float Scale, float RealSeconds)
{
	UWorld* W = GetWorld();
	if (!W || bPaused)
	{
		return;
	}

	bHitStop = true;
	HitStopScale = FMath::Clamp(Scale, 0.05f, 1.f);

	// 只把结束时间往后推、不再乘一次：连续两次免死也不会把世界越拖越慢
	const double End = FPlatformTime::Seconds() + FMath::Max(0.f, RealSeconds);
	HitStopEndReal = FMath::Max(HitStopEndReal, End);

	UGameplayStatics::SetGlobalTimeDilation(W, HitStopScale);

	// 免死是"挡下来了"而不是"挨了一下"，红色暗角会盖住绿闪，压到很低
	DamageFlash = FMath::Min(DamageFlash, 0.20f);
}

void AFSGameMode::StopHitStop()
{
	bHitStop = false;
	HitStopEndReal = 0.0;

	// 暂停本身就是用 0.0001 的膨胀实现的，这里直接写 1.0 会把暂停顶掉
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(W, bPaused ? 0.0001f : 1.f);
	}
	// 顿帧没恢复的话整局都会慢动作，而且很难一眼看出来，留一条可查的记录
	UE_LOG(LogTemp, Verbose, TEXT("[FSHITSTOP] released dilation=%.3f paused=%d"),
		GetWorld() ? UGameplayStatics::GetGlobalTimeDilation(GetWorld()) : -1.f, bPaused ? 1 : 0);
}

int32 AFSGameMode::CountAlive() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<AFSEnemy>& W : Enemies)
	{
		if (W.IsValid())
		{
			++N;
		}
	}
	return N;
}

int32 AFSGameMode::GetWaveRemaining() const
{
	return CountAlive() + SpawnQueue.Num();
}

void AFSGameMode::SetBanner(const FString& Main, const FString& Sub, float Duration)
{
	BannerText = Main;
	BannerSub = Sub;
	BannerTime = Duration;
}

void AFSGameMode::StartNextWave()
{
	Wave++;
	bWaveActive = true;
	HPScale = 1.f + (Wave - 1) * 0.13f;
	SpawnQueue.Reset();
	SpawnTimer = 0.f;

	// 每 5 波一个 BOSS，两种交替：第 5/15/25 波是巫师领主，第 10/20/30 波是熔铁君王。
	// 刻意做成完全不同节奏——一个逼你保持距离躲弹幕，一个逼你贴上去但必须绕背躲震波。
	const bool bBossWave = (Wave % 5 == 0);
	EFSEnemyKind BossKind = EFSEnemyKind::Boss;
	if (bBossWave)
	{
		BossKind = (((Wave / 5) % 2) == 1) ? EFSEnemyKind::Boss : EFSEnemyKind::Warlord;
		SpawnQueue.Add(BossKind);
	}
	FSAudio::Play2D(this, bBossWave ? EFSSfx::Boss : EFSSfx::WaveStart,
		bBossWave ? 1.0f : 0.55f, bBossWave ? 1.f : FMath::FRandRange(0.98f, 1.06f));

	const int32 Count = FMath::Min(7 + Wave * 3, 58);
	for (int32 i = 0; i < Count; ++i)
	{
		const float R = FMath::FRand();
		EFSEnemyKind K = EFSEnemyKind::Slime;

		// 区间必须互斥。这里原来写的是 "R > 0.70 就出急行鬼"，
		// 后面的术士(R>0.70&&R<0.83)、巨兽(R>0.83) 永远轮不到 —— R 已被前一支吃掉，
		// 所以这两类在有史以来任何一波里都没生成过。改成每档窄带，配比一眼可读。
		if (Wave >= 2 && R > 0.70f)
		{
			K = EFSEnemyKind::Runner;
		}
		else if (Wave >= 3 && R > 0.58f)
		{
			K = EFSEnemyKind::Grunt;
		}
		else if (Wave >= 4 && R > 0.48f)
		{
			K = EFSEnemyKind::Mage;
		}
		else if (Wave >= 4 && R > 0.40f)
		{
			K = EFSEnemyKind::Shielder;
		}
		else if (Wave >= 5 && R > 0.32f)
		{
			K = EFSEnemyKind::Splitter;
		}
		else if (Wave >= 6 && R > 0.24f)
		{
			K = EFSEnemyKind::Brute;
		}

		// 天气影响登场怪物
		if (Weather == EFSWeather::BloodMoon && FMath::FRand() < 0.34f)
		{
			static const EFSEnemyKind BM[3] = { EFSEnemyKind::Runner, EFSEnemyKind::Brute, EFSEnemyKind::Shielder };
			K = BM[FMath::RandRange(0, 2)];
		}
		else if (Weather == EFSWeather::Storm && FMath::FRand() < 0.26f)
		{
			K = FMath::RandBool() ? EFSEnemyKind::Runner : EFSEnemyKind::Splitter;
		}
		else if (Weather == EFSWeather::Rain && FMath::FRand() < 0.22f)
		{
			K = EFSEnemyKind::Slime;
		}

		SpawnQueue.Add(K);
	}

	// ---- 精英词缀：从第 3 波起往配比里插精英。
	// 数量按 1 + Wave/4 缓放（3 波 1 只、6 波 2 只、12 波 3 只、16 波起 4 只封顶），
	// 同时受"本波总数的四分之一"约束 —— 一波里全变精英会把节奏压成慢慢磨。
	// 词缀刻意按波次轮转而不是纯随机：这样每一局都能把四种词缀各见一遍，
	// 不会出现"打了二十波没见过荆棘"的情况。
	SpawnEliteQueue.Init(EFSElite::None, SpawnQueue.Num());
	WaveElites = 0;
	{
		static int32 ForceElite = -2;
		if (ForceElite == -2)
		{
			ForceElite = -1;
			FString Arg;
			if (FParse::Value(FCommandLine::Get(), TEXT("fselite="), Arg))
			{
				ForceElite = FMath::Clamp(FCString::Atoi(*Arg), 0, 4);
			}
		}

		int32 Want = 0;
		if (ForceElite >= 0)
		{
			Want = ForceElite;
		}
		else if (!bBossWave && Wave >= 3)
		{
			Want = FMath::Min(1 + Wave / 4, 4);
			Want = FMath::Min(Want, SpawnQueue.Num() / 4);
		}

		// 只从"打法上有区别"的兵种里挑：史莱姆太单薄，加了词缀也只是个大号史莱姆；
		// BOSS 不参与（它自己已经有阶段机制，再叠词缀会让读招变糊）。
		TArray<int32> Slots;
		for (int32 i = 0; i < SpawnQueue.Num(); ++i)
		{
			if (SpawnQueue[i] != EFSEnemyKind::Slime && SpawnQueue[i] != BossKind)
			{
				Slots.Add(i);
			}
		}
		for (int32 n = 0; n < Want && Slots.Num() > 0; ++n)
		{
			const int32 Pick = FMath::RandRange(0, Slots.Num() - 1);
			const int32 Slot = Slots[Pick];
			Slots.RemoveAtSwap(Pick);
			const EFSElite E = (EFSElite)(1 + ((Wave - 3 + n) % 4));
			SpawnEliteQueue[Slot] = E;
			++WaveElites;
			UE_LOG(LogTemp, Log, TEXT("[FSELITE] wave=%d slot=%d kind=%d elite=%s"),
				Wave, Slot, (int32)SpawnQueue[Slot], FSDefs::EliteNameCN(E));
		}
	}

	// 配比自检：日志直接列出这一波各类出了几只，避免再出现"某类从来没生成过"
	{
		int32 Kinds[(int32)EFSEnemyKind::Count] = {};
		for (const EFSEnemyKind K : SpawnQueue)
		{
			++Kinds[FMath::Clamp((int32)K, 0, (int32)EFSEnemyKind::Count - 1)];
		}
		FString Line;
		for (int32 i = 0; i < (int32)EFSEnemyKind::Count; ++i)
		{
			if (Kinds[i] > 0)
			{
				Line += FString::Printf(TEXT(" %d:%d"), i, Kinds[i]);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[FSWAVE] wave=%d total=%d elites=%d kinds:%s"),
			Wave, SpawnQueue.Num(), WaveElites, *Line);
	}

	if (bBossWave)
	{
		const FFSEnemyDef& BD = FSDefs::Enemy(BossKind);
		SetBanner(FS_FMT(TEXT("第 %d 波  ·  %s"), TEXT("WAVE %d  -  %s"), Wave, BD.NameCN),
			FSUtil::L(TEXT("BOSS 出现"), TEXT("A BOSS APPEARS")), 3.f);
		UE_LOG(LogTemp, Log, TEXT("[FSBOSS] wave=%d kind=%d name=%s"),
			Wave, (int32)BossKind, BD.NameCN);
	}
	else
	{
		// 有精英就在副标题里带一句：否则玩家要等它走到跟前才发现"这只怎么这么硬"
		const FString Sub = (WaveElites > 0)
			? FS_FMT(TEXT("%d 个敌人  ·  精英 %d"), TEXT("%d ENEMIES  -  %d ELITES"), SpawnQueue.Num(), WaveElites)
			: FS_FMT(TEXT("%d 个敌人"), TEXT("%d ENEMIES"), SpawnQueue.Num());
		SetBanner(FS_FMT(TEXT("第 %d 波"), TEXT("WAVE %d"), Wave), Sub, 2.f);
	}
}

AFSEnemy* AFSGameMode::SpawnEnemyAt(EFSEnemyKind Kind, const FVector& Loc, float HpMul, float SizeMul,
	EFSElite Elite)
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return nullptr;
	}
	AFSEnemy* E = W->SpawnActor<AFSEnemy>(AFSEnemy::StaticClass(), Loc, FRotator::ZeroRotator);
	if (!E)
	{
		return nullptr;
	}
	E->Setup(Kind, HPScale * HpMul, SizeMul, Elite);
	const float R = E->GetRadius();
	E->SetActorLocation(FVector(Loc.X, Loc.Y, R * 1.5f + 2.f));

	// 出生即朝向玩家。朝向有惯性（铁卫/君王转身慢），所以第一次朝向必须是对的，
	// 否则 BOSS 登场后会有几秒钟用后脑勺对着玩家，"正面减伤"也判定得莫名其妙。
	if (AFSPlayerChar* SpawnP = GetPlayerChar())
	{
		FVector ToP = SpawnP->GetActorLocation() - E->GetActorLocation();
		ToP.Z = 0.f;
		if (!ToP.IsNearlyZero())
		{
			E->SetActorRotation(FRotator(0.f, ToP.Rotation().Yaw, 0.f));
		}
	}

	Enemies.Add(E);

	const FFSEnemyDef& D = FSDefs::Enemy(Kind);
	AFSFx::Shock(W, FVector(Loc.X, Loc.Y, 4.f), R * 1.7f, D.Color, 0.42f);
	return E;
}

void AFSGameMode::SpawnDrop(const FVector& Loc, EFSPickupKind Kind, EFSWeapon Weapon, int32 Value)
{
	// 临时诊断：-fshidepickup 不掉任何道具
	if (FParse::Param(FCommandLine::Get(), TEXT("fshidepickup")))
	{
		return;
	}
	UWorld* W = GetWorld();
	if (!W)
	{
		return;
	}
	const FVector Jitter(FMath::FRandRange(-55.f, 55.f), FMath::FRandRange(-55.f, 55.f), 0.f);
	// 玩家被夹在 ArenaHalf-300，掉在更外侧的道具就永远捡不到，所以落点也一起夹进来
	const float DropLim = ArenaHalf - 330.f;
	FVector DropLoc = Loc + Jitter;
	DropLoc.X = FMath::Clamp(DropLoc.X, -DropLim, DropLim);
	DropLoc.Y = FMath::Clamp(DropLoc.Y, -DropLim, DropLim);
	AFSPickup* P = W->SpawnActor<AFSPickup>(AFSPickup::StaticClass(), DropLoc, FRotator::ZeroRotator);
	if (P)
	{
		P->Setup(Kind, Weapon, Value);
	}
}

void AFSGameMode::OnEnemyKilled(AFSEnemy* E, const FVector& Loc)
{
	if (!E)
	{
		return;
	}
	Enemies.RemoveAll([E](const TWeakObjectPtr<AFSEnemy>& W)
	{
		return !W.IsValid() || W.Get() == E;
	});

	if (Phase != EFSPhase::Playing)
	{
		return;
	}

	Kills++;

	// 连击：两次击杀间隔不超过 ComboWindow 才累加，金币按连击档位给额外奖励
	++Combo;
	ComboTimer = ComboWindow;
	ComboPulse = 1.f;
	RunBestCombo = FMath::Max(RunBestCombo, Combo);
	BestCombo = FMath::Max(BestCombo, Combo);

	const EFSEnemyKind Kind = E->GetKind();
	const FFSEnemyDef& D = FSDefs::Enemy(Kind);

	AddCoins((D.Coins + Combo / 4) * (E->IsElite() ? 3 : 1));

	// 嗜血赐福：每次击杀回一点血。清小怪海时是主要的续航来源，所以放在最前面。
	if (AFSPlayerChar* LP = GetPlayerChar())
	{
		const float Leech = LP->GetLeechPerKill();
		if (Leech > 0.f)
		{
			LP->Heal(Leech);
		}
	}

	const bool bWasBoss = E->IsBoss();
	const bool bWasElite = E->IsElite();

	// 精英的掉落与金币都要明显更好，否则"多打一份风险"没有任何理由——
	// 玩家会学会绕开精英只清杂兵，整套词缀就白做了。
	const float RuneChance = bWasBoss ? 1.f : (bWasElite ? 0.30f : 0.085f);
	if (FMath::FRand() < RuneChance)
	{
		SpawnDrop(Loc, EFSPickupKind::Rune, EFSWeapon::Pistol, 1);
	}
	if (FMath::FRand() < (bWasElite ? 0.20f : 0.055f))
	{
		SpawnDrop(Loc, EFSPickupKind::Health, EFSWeapon::Pistol, 1);
	}
	if (FMath::FRand() < (bWasBoss ? 1.f : (bWasElite ? 0.26f : 0.06f)))
	{
		SpawnDrop(Loc, EFSPickupKind::Weapon, PickRandomWeapon(), 1);
	}
	if (bWasElite)
	{
		UE_LOG(LogTemp, Log, TEXT("[FSELITE] killed kind=%d elite=%d wave=%d"),
			(int32)Kind, (int32)E->GetElite(), Wave);
	}
	if (bWasBoss)
	{
		AddCoins(40);
		UE_LOG(LogTemp, Log, TEXT("[FSBOSS] defeated kind=%d at wave=%d"), (int32)Kind, Wave);
	}
}

void AFSGameMode::AddCoins(int32 C)
{
	if (C <= 0)
	{
		return;
	}
	Coins += C;

	// 金币同时是经验：这样金币不是单纯的计分板，而是"打得多就变强"的正反馈
	if (Phase != EFSPhase::Playing)
	{
		return;
	}
	Xp += C;

	// 一次进账可能连升多级（BOSS 掉 40 币时），用 while 消化并加上限防死循环
	int32 Guard = 0;
	while (Xp >= XpNeed && Guard++ < 8)
	{
		Xp -= XpNeed;
		++Level;
		XpNeed = FMath::RoundToInt(60.f * FMath::Pow(1.38f, (float)(Level - 1)));
		GrantUpgrade();
	}
}

void AFSGameMode::GrantUpgrade()
{
	AFSPlayerChar* P = GetPlayerChar();
	if (!P)
	{
		return;
	}
	const int32 Idx = UpgradeIdx++;
	P->ApplyUpgrade(Idx, 0, false);   // 金币升级是基础成长，不计流派层数

	const TCHAR* CN = nullptr;
	const TCHAR* EN = nullptr;
	AFSPlayerChar::UpgradeName(Idx, CN, EN);
	P->AddNotice(FString::Printf(TEXT("Lv.%d   %s"), Level, FSUtil::L(CN, EN)), 2.0f);

	LevelPulse = 1.f;
	FSAudio::Play2D(this, EFSSfx::Upgrade, 1.0f, 1.06f);
	if (UWorld* W = GetWorld())
	{
		AFSFx::Shock(W, P->GetActorLocation() - FVector(0.f, 0.f, 80.f), 340.f, FSPal::Gold, 0.5f);
	}
}

// ------------------------------------------------------------------ 清波赐福

void AFSGameMode::OpenBlessing()
{
	if (Phase != EFSPhase::Playing || bChoosing)
	{
		return;
	}

	AFSPlayerChar* P = GetPlayerChar();

	// 已觉醒的流派退出抽签池。觉醒一次给的是质变被动，代价是该系不再有后续成长 ——
	// 没有这条规则，"觉醒"只是数值表上多一行乘算，玩家会一直被 2.6 倍权重推着
	// 把同一条线堆到十几层，三条路之间的取舍根本不成立。
	auto PathAwakened = [P](EFSPath Pth)
	{
		return P && P->IsPathAwakened(Pth);
	};

	bool bAnyAwakened = false;
	for (int32 i = 0; i < (int32)EFSPath::Count; ++i)
	{
		if (PathAwakened((EFSPath)i))
		{
			bAnyAwakened = true;
			break;
		}
	}

	// 默认值取"第一个未觉醒流派"：三系都是 0 层时 dominant 不该停在一条已经退池的线上，
	// 否则日志里 dominant=0 会让人误以为权重还在偏向烈焰。
	EFSPath Dominant = EFSPath::Fury;
	for (int32 i = 0; i < (int32)EFSPath::Count; ++i)
	{
		if (!PathAwakened((EFSPath)i))
		{
			Dominant = (EFSPath)i;
			break;
		}
	}

	// 找出玩家已经投入最多的"未觉醒"流派。抽签偏向它，"选同样颜色"才真的能滚起雪球
	int32 BestStacks = 0;
	if (P)
	{
		for (int32 i = 0; i < (int32)EFSPath::Count; ++i)
		{
			const EFSPath Pth = (EFSPath)i;
			if (PathAwakened(Pth))
			{
				continue;
			}
			const int32 S = P->GetPathStacks(Pth);
			if (S > BestStacks)
			{
				BestStacks = S;
				Dominant = Pth;
			}
		}
	}

	// 加权轮盘抽三张不重复的：抽走就从池里删，天然不重复且一定能凑满三张
	const int32 Total = FSDefs::UpgradeCount();
	TArray<int32> Pool;
	Pool.Reserve(Total);
	for (int32 i = 0; i < Total; ++i)
	{
		if (!PathAwakened(FSDefs::PathOf((EFSUpgrade)i)))
		{
			Pool.Add(i);
		}
	}

	// 三系全部觉醒后池子会空。此时没有新路可走，只能退回全池继续堆数值 ——
	// 关键是不能让 Choices 为空：OpenBlessing 会在下面直接 return，bChoosing 永不置位，
	// 波次推进就死在这里了。
	const bool bPoolExhausted = (Pool.Num() == 0);
	if (bPoolExhausted)
	{
		for (int32 i = 0; i < Total; ++i)
		{
			Pool.Add(i);
		}
	}

	Choices.Reset();
	ChoiceRarity.Reset();

	for (int32 Slot = 0; Slot < ChoiceSlots && Pool.Num() > 0; ++Slot)
	{
		TArray<float> Weights;
		Weights.SetNum(Pool.Num());
		float TotalW = 0.f;
		for (int32 i = 0; i < Pool.Num(); ++i)
		{
			const EFSPath Pth = FSDefs::PathOf((EFSUpgrade)Pool[i]);
			float W = 1.f;
			if (BestStacks > 0 && Pth == Dominant)
			{
				W = 2.6f;
			}
			else if (bAnyAwakened && P)
			{
				// 第一条线走通之后，给第二近的那条一点引力：
				// 否则退池之后剩下的卡完全均匀，玩家想补哪条线只能靠运气。
				W = 1.f + 0.25f * (float)P->GetPathStacks(Pth);
			}
			Weights[i] = W;
			TotalW += W;
		}

		int32 Pick = Pool.Num() - 1;
		float Roll = FMath::FRand() * TotalW;
		for (int32 i = 0; i < Pool.Num(); ++i)
		{
			Roll -= Weights[i];
			if (Roll <= 0.f)
			{
				Pick = i;
				break;
			}
		}

		const EFSUpgrade U = (EFSUpgrade)Pool[Pick];
		Pool.RemoveAt(Pick);
		Choices.Add(U);
		ChoiceRarity.Add(RollRarity(FSDefs::PathOf(U), Dominant, BestStacks));
	}

	if (Choices.Num() == 0)
	{
		return;
	}

	// 打完 BOSS 清波的一份补偿：至少一张传世，否则"憋了五波"的奖励感撑不住
	if (bBossBlessGuarantee)
	{
		bBossBlessGuarantee = false;
		bool bHasEpic = false;
		for (const EFSRarity R : ChoiceRarity)
		{
			if (R == EFSRarity::Epic)
			{
				bHasEpic = true;
				break;
			}
		}
		if (!bHasEpic)
		{
			ChoiceRarity[FMath::RandRange(0, ChoiceRarity.Num() - 1)] = EFSRarity::Epic;
		}
	}

	bChoosing = true;
	ChoiceSel = 0;
	ChoiceElapsed = 0.f;

	// 清波横幅会正好压在卡片上方，先收掉
	BannerTime = 0.f;
	// 波次推进交给 BlessingPick 重新点火（选完给 1.1 秒缓冲）
	InterWave = 0.f;

	FSAudio::Play2D(this, EFSSfx::Upgrade, 0.85f, 0.90f);
	if (P)
	{
		// 选卡这一两秒里别被残留弹幕摸死
		P->GrantInvuln(1.4f);
	}

	UE_LOG(LogTemp, Log, TEXT("[FSBLESS] open wave=%d dominant=%d stacks=%d anyAwakened=%d poolExhausted=%d"),
		Wave, (int32)Dominant, BestStacks, bAnyAwakened ? 1 : 0, bPoolExhausted ? 1 : 0);
	for (int32 i = 0; i < Choices.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("[FSBLESS]   slot=%d kind=%d rarity=%d"),
			i, (int32)Choices[i], (int32)ChoiceRarity[i]);
	}
}

EFSRarity AFSGameMode::RollRarity(EFSPath Path, EFSPath Dominant, int32 DominantStacks) const
{
	if (ForceRarity >= 0)
	{
		return (EFSRarity)FMath::Clamp(ForceRarity, 0, (int32)EFSRarity::Count - 1);
	}

	// 波次越高越容易出好卡：第 1 波基本全是寻常，第 9 波往后常见稀有、偶尔传世
	const float T = FMath::Clamp((float)(Wave - 1) / 8.f, 0.f, 1.f);
	float EpicP = 0.04f + 0.22f * T;
	float RareP = 0.26f + 0.24f * T;

	// 走在自己流派上的卡再升一档：投入越深，越容易抽到这一路的"好牌"
	if (DominantStacks > 0 && Path == Dominant)
	{
		EpicP *= 1.8f;
		RareP += 0.10f;
	}

	EpicP = FMath::Min(EpicP, 0.45f);
	RareP = FMath::Min(RareP, 1.f - EpicP - 0.05f);

	const float Roll = FMath::FRand();
	if (Roll < EpicP)
	{
		return EFSRarity::Epic;
	}
	if (Roll < EpicP + RareP)
	{
		return EFSRarity::Rare;
	}
	return EFSRarity::Common;
}

void AFSGameMode::AnnouncePathAwaken(EFSPath P)
{
	FString Main;
	FString Sub;
	switch (P)
	{
	case EFSPath::Swift:
		Main = FSUtil::L(TEXT("疾 影 觉 醒"), TEXT("SWIFT AWAKENED"));
		Sub = FSUtil::L(TEXT("残影   闪避后 0.8 秒射速 +35%"), TEXT("AFTERIMAGE   +35% FIRE RATE AFTER DASH"));
		break;
	case EFSPath::Ward:
		Main = FSUtil::L(TEXT("壁 垒 觉 醒"), TEXT("WARD AWAKENED"));
		Sub = FSUtil::L(TEXT("不屈   每 45 秒免死一次"), TEXT("UNDYING   SURVIVE ONE LETHAL HIT EVERY 45s"));
		break;
	default:
		Main = FSUtil::L(TEXT("烈 焰 觉 醒"), TEXT("FURY AWAKENED"));
		Sub = FSUtil::L(TEXT("狂怒   暴击伤害 2.2 → 2.65 倍"), TEXT("FRENZY   CRIT DAMAGE 2.2x -> 2.65x"));
		break;
	}

	SetBanner(Main, Sub, 2.6f);
	// 暗角闪红别开太重：0.5 以上会把整个战场染成橙色，只剩"红"看不出别的
	DamageFlash = 0.26f;
	FSAudio::Play2D(this, EFSSfx::Upgrade, 1.0f, 0.80f);
	UE_LOG(LogTemp, Warning, TEXT("[FSAWAKEN] path=%d awakened"), (int32)P);

	if (AFSPlayerChar* P2 = GetPlayerChar())
	{
		P2->AddShake(12.f);
		if (UWorld* W = GetWorld())
		{
			const FLinearColor Col = FSDefs::PathColor(P);
			AFSFx::Shock(W, P2->GetActorLocation() - FVector(0.f, 0.f, 84.f), 470.f, Col, 0.9f, 2.4f);
			AFSFx::Burst(W, P2->GetActorLocation() + FVector(0.f, 0.f, 80.f), 230.f, Col, 0.5f, 1.5f);
		}
	}
}

void AFSGameMode::BlessingMove(int32 Delta)
{
	if (!bChoosing || Choices.Num() == 0)
	{
		return;
	}
	const int32 N = Choices.Num();
	ChoiceSel = ((ChoiceSel + Delta) % N + N) % N;
	FSAudio::Play2D(this, EFSSfx::Beep, 0.30f, 1.35f);
}

void AFSGameMode::BlessingPick(int32 Slot)
{
	if (!bChoosing || !Choices.IsValidIndex(Slot))
	{
		return;
	}

	const EFSUpgrade U = Choices[Slot];
	const FFSUpgradeDef& D = FSDefs::Upgrade(U);
	const EFSRarity Rar = ChoiceRarity.IsValidIndex(Slot) ? ChoiceRarity[Slot] : EFSRarity::Common;
	const EFSPath Pth = D.Path;

	bChoosing = false;
	Choices.Reset();
	ChoiceRarity.Reset();

	AFSPlayerChar* P = GetPlayerChar();
	bool bAwakened = false;
	if (P)
	{
		P->ApplyUpgrade((int32)U, (int32)Rar);

		const FString DCN = FSDefs::UpgradeDescCN(U, Rar);
		const FString DEN = FSDefs::UpgradeDescEN(U, Rar);
		P->AddNotice(FString::Printf(TEXT("%s · %s   %s"),
			FSUtil::L(FSDefs::RarityNameCN(Rar), FSDefs::RarityNameEN(Rar)),
			FSUtil::L(D.NameCN, D.NameEN), FSUtil::L(*DCN, *DEN)), 2.6f);

		FSAudio::Play2D(this, EFSSfx::Upgrade, 1.0f, 1.12f);
		LevelPulse = 1.f;

		if (UWorld* W = GetWorld())
		{
			AFSFx::Shock(W, P->GetActorLocation() - FVector(0.f, 0.f, 80.f), 300.f, D.Color, 0.5f);
			AFSFx::Burst(W, P->GetActorLocation() + FVector(0.f, 0.f, 60.f), 180.f, D.Color, 0.32f);
		}

		// 刚好叠到阈值的那一下才觉醒，重复选同类不会反复播报
		bAwakened = (P->GetPathStacks(Pth) == FSDefs::PathAwakenStacks);
	}

	// 选完留一小段缓冲再放下一波，别让玩家刚点完就被糊脸
	InterWave = FMath::Max(InterWave, 1.1f);

	if (bAwakened)
	{
		AnnouncePathAwaken(Pth);
	}

	UE_LOG(LogTemp, Log, TEXT("[FSBLESS] pick slot=%d kind=%d rarity=%d stacks=%d path=%d pathstacks=%d level=%d"),
		Slot, (int32)U, (int32)Rar, P ? P->GetUpgradeStacks((int32)U) : 0,
		(int32)Pth, P ? P->GetPathStacks(Pth) : 0, Level);
}

bool AFSGameMode::BlessingClickAt(float MouseX, float MouseY, float ScreenW, float ScreenH)
{
	if (!bChoosing || Choices.Num() == 0)
	{
		return false;
	}
	const float U = FSUtil::UIScale(ScreenH);
	for (int32 i = 0; i < Choices.Num(); ++i)
	{
		if (FSUtil::BlessingCardRect(ScreenW, ScreenH, U, i, Choices.Num()).Contains(MouseX, MouseY))
		{
			BlessingPick(i);
			return true;
		}
	}
	return false;
}

void AFSGameMode::AnnounceBossPhase(AFSEnemy* Boss, int32 NewPhase, const FVector& Loc)
{
	const bool bWarlord = Boss && Boss->GetKind() == EFSEnemyKind::Warlord;
	if (bWarlord)
	{
		// 君王的两段变身文案都围绕"甲"和"震"，提示玩家该怎么应对，而不是只说变强了
		if (NewPhase == 2)
		{
			SetBanner(FSUtil::L(TEXT("第 二 阶 段"), TEXT("PHASE  II")),
				FSUtil::L(TEXT("甲 片 落 地 · 它 开 始 连 震"), TEXT("ARMOR SHED  -  DOUBLE QUAKE")), 2.4f);
		}
		else
		{
			SetBanner(FSUtil::L(TEXT("第 三 阶 段"), TEXT("PHASE  III")),
				FSUtil::L(TEXT("炉 心 全 开 · 绕 背 别 停"), TEXT("CORE OPEN  -  STAY BEHIND IT")), 2.4f);
		}
	}
	else if (NewPhase == 2)
	{
		SetBanner(FSUtil::L(TEXT("第 二 阶 段"), TEXT("PHASE  II")),
			FSUtil::L(TEXT("护盾爆散 · 它开始冲锋了"), TEXT("SHIELD BURST  -  IT CHARGES NOW")), 2.4f);
	}
	else
	{
		SetBanner(FSUtil::L(TEXT("第 三 阶 段"), TEXT("PHASE  III")),
			FSUtil::L(TEXT("环形弹幕 · 别 停 下"), TEXT("BULLET RING  -  KEEP MOVING")), 2.4f);
	}

	FSAudio::Play(this, EFSSfx::WaveStart, Loc, 0.95f, 0.78f);
	DamageFlash = FMath::Max(DamageFlash, 0.5f);

	// 调试：-fscapture 时每次阶段切换自动留图，方便核对"护盾/变身"那一下的画面
	if (FParse::Param(FCommandLine::Get(), TEXT("fscapture")))
	{
		FScreenshotRequest::RequestScreenshot(
			FPaths::ProjectSavedDir() / FString::Printf(TEXT("Boss_%s_P%d.png"),
				bWarlord ? TEXT("Warlord") : TEXT("Archmage"), NewPhase), false, false);
	}
}

void AFSGameMode::OnRune()
{
	RuneCount++;
	if (RuneCount >= 3)
	{
		RuneCount -= 3;
		SummonGuardian();
	}
	else if (AFSPlayerChar* P = GetPlayerChar())
	{
		P->AddNotice(FS_FMT(TEXT("符石 %d / 3"), TEXT("RUNE %d / 3"), RuneCount));
	}
}

void AFSGameMode::SummonGuardian()
{
	if (Guardian)
	{
		Guardian->Destroy();
		Guardian = nullptr;
	}
	AFSPlayerChar* P = GetPlayerChar();
	if (!P)
	{
		return;
	}
	Guardian = GetWorld()->SpawnActor<AFSGuardian>(AFSGuardian::StaticClass(), P->GetActorLocation(), FRotator::ZeroRotator);
	if (Guardian)
	{
		Guardian->Init(22.f);
		bGuardianOn = true;
		GuardianLeft = 22.f;
		FSAudio::Play2D(this, EFSSfx::Guardian, 1.0f);
		P->AddNotice(FSUtil::L(TEXT("守护兽降临！"), TEXT("GUARDIAN SUMMONED!")), 2.2f);
		P->AddShake(8.f);
		SetBanner(FSUtil::L(TEXT("守 护 兽"), TEXT("GUARDIAN")),
			FSUtil::L(TEXT("它会替你战斗 22 秒"), TEXT("IT FIGHTS FOR YOU FOR 22s")), 1.8f);
	}
}

EFSWeapon AFSGameMode::PickRandomWeapon() const
{
	TArray<EFSWeapon> Pool;
	for (int32 i = 1; i < 6; ++i)
	{
		if (bUnlocked[i])
		{
			Pool.Add((EFSWeapon)i);
		}
	}
	if (Pool.Num() == 0)
	{
		return EFSWeapon::SMG;
	}
	return Pool[FMath::RandRange(0, Pool.Num() - 1)];
}

void AFSGameMode::SetWeather(EFSWeather W, bool bAnnounce)
{
	Weather = W;
	WeatherTimer = 34.f;

	switch (W)
	{
	case EFSWeather::Clear:
		EnemySpeedMul = 1.0f;
		break;
	case EFSWeather::Rain:
		EnemySpeedMul = 1.05f;
		break;
	case EFSWeather::Storm:
		EnemySpeedMul = 1.14f;
		break;
	case EFSWeather::BloodMoon:
		EnemySpeedMul = 1.24f;
		break;
	}

	if (bAnnounce)
	{
		SetBanner(FSUtil::L(FSDefs::WeatherNameCN(W), FSDefs::WeatherNameEN(W)),
			FSUtil::L(TEXT("天候变化 — 怪物也随之改变"), TEXT("THE WEATHER SHIFTS - SO DO THE MONSTERS")), 2.4f);
	}
}

void AFSGameMode::TickWeather(float Dt)
{
	WeatherTimer -= Dt;
	if (WeatherTimer <= 0.f)
	{
		int32 Next = (int32)Weather;
		while (Next == (int32)Weather)
		{
			Next = FMath::RandRange(0, 3);
		}
		SetWeather((EFSWeather)Next, true);
	}
}

void AFSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ---- 顿帧收尾。用真实时钟判定，慢放本身不会把"何时恢复"一起拖慢。
	// 暂停期间不恢复（暂停用的是更小的膨胀），等退出暂停时 SetPaused 会自己复位。
	if (bHitStop && (bPaused || FPlatformTime::Seconds() >= HitStopEndReal))
	{
		StopHitStop();
	}

	// ---- 自动截图调试（-fscapture / -fsauto）
	{
		static bool bCapInit = false;
		static bool bCapture = false;
		static bool bAuto = false;
		static bool bBot = false;
		static float CapT = 0.f;
		static int32 CapIdx = 0;
		static bool bAutoFired = false;
		static const float CapTimes[6] = { 2.5f, 10.0f, 22.0f, 42.0f, 62.0f, 80.0f };

		if (!bCapInit)
		{
			bCapInit = true;
			bCapture = FParse::Param(FCommandLine::Get(), TEXT("fscapture"));
			bAuto = FParse::Param(FCommandLine::Get(), TEXT("fsauto"));
			bBot = FParse::Param(FCommandLine::Get(), TEXT("fsbot"));
		}
		if (bCapture || bAuto)
		{
			CapT += DeltaSeconds;
			if (bAuto && !bAutoFired && CapT > 4.0f && GetPlayerChar())
			{
				bAutoFired = true;
				StartGame();

				// 调试：-fswave=N 直接跳到第 N 波，省掉前面几分钟；配 -fsgod 用可以稳定测 BOSS
				FString WaveArg;
				if (FParse::Value(FCommandLine::Get(), TEXT("fswave="), WaveArg))
				{
					const int32 Target = FMath::Clamp(FCString::Atoi(*WaveArg), 1, 99);
					Wave = Target - 1;
					InterWave = 0.30f;
					HPScale = 1.f + (float)(Target - 1) * 0.13f;
				}
				if (bBot)
				{
					if (AFSPlayerChar* BP = GetPlayerChar())
					{
						BP->EnableBot();
					}
				}

				// 调试：-fsedge 把角色丢到西侧贴边处（不配 -fsbot 用），检查贴墙时相机会不会被墙挡住
				if (FParse::Param(FCommandLine::Get(), TEXT("fsedge")))
				{
					if (AFSPlayerChar* EP = GetPlayerChar())
					{
						EP->SetActorLocation(FVector(-(ArenaHalf - 305.f), 0.f, 140.f), false);
					}
				}
			}
			if (bCapture && CapIdx < 6 && CapT >= CapTimes[CapIdx])
			{
				const FString Name = FString::Printf(TEXT("%sCap_%02d.png"),
					*(FPaths::ProjectSavedDir() / TEXT("")), CapIdx);
				FScreenshotRequest::RequestScreenshot(Name, false, false);
				++CapIdx;
			}
		}
	}

	if (bPendingAutoStart && GetPlayerChar())
	{
		bPendingAutoStart = false;
		StartGame();
	}

	// ---- 无人值守开关：-fsautostart 跳过标题界面，-fsquit=<秒> 到点走正常退出流程。
	// 直接用 timeout kill 收进程会把日志缓冲一起丢掉（只剩引擎初始化的那几十行），
	// 所以打包版做自动验证必须让引擎自己退。
	{
		static bool bASInit = false;
		static bool bAS = false;
		static float QSec = -1.f;
		static double QStart = 0.0;
		if (!bASInit)
		{
			bASInit = true;
			bAS = FParse::Param(FCommandLine::Get(), TEXT("fsautostart"));
			QStart = FPlatformTime::Seconds();
			FString QArg;
			if (FParse::Value(FCommandLine::Get(), TEXT("fsquit="), QArg))
			{
				QSec = FCString::Atof(*QArg);
			}
		}
		if (bAS && Phase == EFSPhase::Title && GetPlayerChar())
		{
			bAS = false;
			UE_LOG(LogTemp, Log, TEXT("[FSAUTO] skip title, start run"));
			StartGame();
		}
		if (QSec >= 0.f && FPlatformTime::Seconds() - QStart > (double)QSec)
		{
			QSec = -1.f;
			UE_LOG(LogTemp, Log, TEXT("[FSAUTO] quit requested, flushing log"));
			if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			{
				PC->ConsoleCommand(TEXT("quit"));
			}
		}
	}

	// ---- 觉醒退池验证（-fsawakepreload=0/1/2）：开局就把某条流派叠满，
	// 让紧随其后的 -fsbless 在"已觉醒"状态下抽卡，确认该系赐福真的退出了池子。
	// 单独开一个参数而不复用 -fsawaken，是因为后者要等到 7.2 秒才注入，赶不上 fsbless 的 4.5 秒。
	{
		static bool bAPInit = false;
		static bool bAPDone = false;
		static int32 APPath = 0;
		static double APStart = 0.0;
		if (!bAPInit)
		{
			bAPInit = true;
			APStart = FPlatformTime::Seconds();
			FString APArg;
			if (FParse::Value(FCommandLine::Get(), TEXT("fsawakepreload="), APArg))
			{
				// 传负数表示三系全部觉醒，专门用来测"池子被抽空"那条兜底路径
				const int32 Raw = FCString::Atoi(*APArg);
				APPath = (Raw < 0) ? -1 : FMath::Clamp(Raw, 0, (int32)EFSPath::Count - 1);
			}
			else
			{
				bAPDone = true;
			}
		}
		if (!bAPDone && Phase == EFSPhase::Playing && FPlatformTime::Seconds() - APStart > 1.0)
		{
			if (AFSPlayerChar* AP = GetPlayerChar())
			{
				static const int32 RepKindPre[3] = { 0, 2, 3 };
				const int32 First = (APPath < 0) ? 0 : APPath;
				const int32 Last = (APPath < 0) ? (int32)EFSPath::Count - 1 : APPath;
				for (int32 p = First; p <= Last; ++p)
				{
					for (int32 i = 0; i < FSDefs::PathAwakenStacks; ++i)
					{
						AP->ApplyUpgrade(RepKindPre[p], 1, true);
					}
				}
				// 不调 AnnouncePathAwaken：横幅和音效会污染 Blessing_00 那张截图
				bAPDone = true;
				UE_LOG(LogTemp, Log, TEXT("[FSBLESS] preload awakened path=%d stacks=%d/%d/%d"),
					APPath,
					AP->GetPathStacks(EFSPath::Fury),
					AP->GetPathStacks(EFSPath::Swift),
					AP->GetPathStacks(EFSPath::Ward));
			}
		}
	}

	// ---- BOSS 定点验证（-fsboss=<序号>）：把指定 BOSS 直接摆到玩家正前方，
	// 配合 -fsbosshp=0.2 压血可以一次跑完三个阶段，配合 -fscapture 留图。
	// 有它就不必为了看第二种 BOSS 一路手打到第 10 波。
	{
		static bool bBSInit = false;
		static bool bBS = false;
		static int32 BSKind = 0;
		static bool bBSDOne = false;
		static double BSStart = 0.0;
		static bool bBA = false;
		static bool bBADone = false;
		static double BBreakAt = 0.0;
		static bool bBACap1 = false;
		static bool bBACap2 = false;
		if (!bBSInit)
		{
			bBSInit = true;
			BSStart = FPlatformTime::Seconds();
			bBA = FParse::Param(FCommandLine::Get(), TEXT("fsbreakarmor"));
			FString BSArg;
			if (FParse::Value(FCommandLine::Get(), TEXT("fsboss="), BSArg))
			{
				bBS = true;
				BSKind = FMath::Clamp(FCString::Atoi(*BSArg), 0, (int32)EFSEnemyKind::Count - 1);
			}
		}
		if (bBS && !bBSDOne && Phase == EFSPhase::Playing && FPlatformTime::Seconds() - BSStart > 1.2)
		{
			bBSDOne = true;
			SpawnQueue.Reset();
			AFSPlayerChar* BP = GetPlayerChar();
			const FVector Base = BP ? BP->GetActorLocation() : FVector::ZeroVector;
			const FVector Fwd = BP ? BP->GetActorRotation().Vector() : FVector::ForwardVector;
			const AFSEnemy* Spawned = SpawnEnemyAt((EFSEnemyKind)BSKind, Base + Fwd * 900.f);
			UE_LOG(LogTemp, Log, TEXT("[FSBOSSD] spawned kind=%d name=%s at 900"),
				BSKind, FSDefs::Enemy((EFSEnemyKind)BSKind).NameCN);
			(void)Spawned;
		}
		// -fsbreakarmor：2.2 秒后强制打穿甲片，专门用来截"崩落"和 HUD 破甲态
		if (bBS && bBA && bBSDOne && !bBADone && FPlatformTime::Seconds() - BSStart > 2.2)
		{
			bBADone = true;
			for (const TWeakObjectPtr<AFSEnemy>& W : Enemies)
			{
				AFSEnemy* E = W.Get();
				if (E && !E->IsDead() && E->HasArmor())
				{
					E->DebugBreakArmor();
					BBreakAt = FPlatformTime::Seconds();
					UE_LOG(LogTemp, Log, TEXT("[FSARMORD] forced break on %s armor=%.0f"),
						FSDefs::Enemy(E->GetKind()).NameCN, E->GetArmorMaxHp());
					break;
				}
			}
		}
		// 破甲瞬间 + 一秒后各留一张：验证"甲片飞出去"和 HUD 的破甲态
		if (bBA && BBreakAt > 0.0)
		{
			const double Since = FPlatformTime::Seconds() - BBreakAt;
			if (!bBACap1 && Since > 0.30)
			{
				bBACap1 = true;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Armor_Break.png"), false, false);
			}
			if (!bBACap2 && Since > 1.10)
			{
				bBACap2 = true;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Armor_Broken.png"), false, false);
			}
		}
	}

	// ---- 兵种图鉴（-fsmenagerie[=N]）：在玩家正前方摆一排 4 个兵种并冻住位移，
	// 配合 -fscapture 截图逐个核对体型与配色。N 是起始序号（0 与 4 两次刚好覆盖全部 8 种）。
	// 一次只摆 4 个是因为镜头可见纵深有限，摆 8 个第二排会被上边缘裁掉。
	{
		static bool bMGInit = false;
		static bool bMG = false;
		static int32 MGFrom = 0;
		static bool bMGDone = false;
		static double MGStart = 0.0;
		if (!bMGInit)
		{
			bMGInit = true;
			MGStart = FPlatformTime::Seconds();
			FString MGArg;
			if (FParse::Value(FCommandLine::Get(), TEXT("fsmenagerie="), MGArg))
			{
				bMG = true;
				MGFrom = FMath::Clamp(FCString::Atoi(*MGArg), 0, (int32)EFSEnemyKind::Count - 1);
			}
			else if (FParse::Param(FCommandLine::Get(), TEXT("fsmenagerie")))
			{
				bMG = true;
			}
		}
		if (bMG && !bMGDone && Phase == EFSPhase::Playing && FPlatformTime::Seconds() - MGStart > 1.2)
		{
			bMGDone = true;
			// 只留图鉴：把这一波还没刷出来的清掉，画面里就只剩一排兵
			SpawnQueue.Reset();
			AFSPlayerChar* MP = GetPlayerChar();
			const FVector Base = MP ? MP->GetActorLocation() : FVector::ZeroVector;
			const FVector Fwd = MP ? MP->GetActorRotation().Vector() : FVector::ForwardVector;
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
			const int32 N = FMath::Min(4, (int32)EFSEnemyKind::Count - MGFrom);
			for (int32 i = 0; i < N; ++i)
			{
				const FVector Sp = Base + Fwd * 620.f + Right * ((i - (N - 1) * 0.5f) * 300.f);
				SpawnEnemyAt((EFSEnemyKind)(MGFrom + i), Sp);
			}
			// 冻住位移，否则它们立刻冲上来，截图只能拍到一团重叠的轮廓
			EnemySpeedMul = 0.f;
			UE_LOG(LogTemp, Log, TEXT("[FSMENAGERIE] kinds %d..%d frozen for capture"), MGFrom, MGFrom + N - 1);
		}
	}

	// ---- 精英词缀展示（-fseliteshow）：四种词缀各摆一只、冻住位移。
	// 词缀的区分度全在颜色上，靠实战截图里碰运气核对太不可靠；
	// 摆成一排拍一张，能不能一眼分开就是一眼的事。
	{
		static bool bESInit = false;
		static bool bES = false;
		static bool bESDone = false;
		static double ESStart = 0.0;
		if (!bESInit)
		{
			bESInit = true;
			ESStart = FPlatformTime::Seconds();
			bES = FParse::Param(FCommandLine::Get(), TEXT("fseliteshow"));
		}
		if (bES && !bESDone && Phase == EFSPhase::Playing && FPlatformTime::Seconds() - ESStart > 1.2)
		{
			bESDone = true;
			SpawnQueue.Reset();
			AFSPlayerChar* EP = GetPlayerChar();
			const FVector Base = EP ? EP->GetActorLocation() : FVector::ZeroVector;
			const FVector Fwd = EP ? EP->GetActorRotation().Vector() : FVector::ForwardVector;
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();

			// 体型从瘦到胖排开，顺便也能看出"环的尺寸是跟着体型走的"
			static const EFSEnemyKind Kinds[4] =
			{
				EFSEnemyKind::Runner, EFSEnemyKind::Grunt, EFSEnemyKind::Shielder, EFSEnemyKind::Brute
			};
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector Sp = Base + Fwd * 700.f + Right * ((i - 1.5f) * 330.f);
				SpawnEnemyAt(Kinds[i], Sp, 1.f, 1.f, (EFSElite)(i + 1));
			}
			EnemySpeedMul = 0.f;
			UE_LOG(LogTemp, Log, TEXT("[FSELITE] showcase: 4 affixes frozen for capture"));
		}
	}

	// ---- 战斗机制自检（-fsprobe）：铁卫正/背面减伤、裂魂死后分裂、熔铁君王护甲与震波。
	// 全部写进日志——"看起来对"和"数值真的生效"是两件事，这个开关只做后者。
	{
		static bool bPBInit = false;
		static bool bPB = false;
		static bool bPBDone = false;
		static int32 PBStage = 0;
		static double PBTick = 0.0;
		static AFSEnemy* PBShield = nullptr;
		static AFSEnemy* PBSplit = nullptr;
		static AFSEnemy* PBWarlord = nullptr;
		static float PBHpBefore = 1.f;

		// 精英验证用：爆裂那只会在引信结束时销毁，所以只能拿弱引用
		static AFSEnemy* PBEliteNormal = nullptr;
		static AFSEnemy* PBEliteRef = nullptr;
		static AFSEnemy* PBThorn = nullptr;
		static TWeakObjectPtr<AFSEnemy> PBVolatile;
		static float PBPlayerHp = 1.f;
		if (!bPBInit)
		{
			bPBInit = true;
			PBTick = FPlatformTime::Seconds();
			bPB = FParse::Param(FCommandLine::Get(), TEXT("fsprobe"));
		}
		if (bPB && !bPBDone && Phase == EFSPhase::Playing)
		{
			AFSPlayerChar* PP = GetPlayerChar();
			const double El = FPlatformTime::Seconds() - PBTick;
			if (PP && PBStage == 0 && El > 1.5)
			{
				SpawnQueue.Reset();
				const FVector Base = PP->GetActorLocation();
				const FVector Fwd = PP->GetActorRotation().Vector();
				const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
				PBShield = SpawnEnemyAt(EFSEnemyKind::Shielder, Base + Fwd * 520.f + Right * 180.f);
				PBSplit = SpawnEnemyAt(EFSEnemyKind::Splitter, Base + Fwd * 520.f - Right * 180.f);
				PBStage = 1;
				PBTick = FPlatformTime::Seconds();
			}
			else if (PBStage == 1 && El > 0.7)
			{
				PBStage = 2;
				PBTick = FPlatformTime::Seconds();
				if (PBShield && !PBShield->IsDead())
				{
					const float A0 = PBShield->GetHealth();
					PBShield->TakeDamage(100.f, FDamageEvent(), nullptr, PP);   // 铁卫面朝玩家 → 正面
					const float A1 = PBShield->GetHealth();
					// 原地转 180°：让"从玩家方向来"变成背后命中（Tick 还没跑，朝向不会立刻被改回去）
					PBShield->SetActorRotation(PBShield->GetActorRotation() + FRotator(0.f, 180.f, 0.f));
					PBShield->TakeDamage(100.f, FDamageEvent(), nullptr, PP);   // 背面
					const float A2 = PBShield->GetHealth();
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] shielder front=%.0f back=%.0f (of 100 each) hp=%.0f/%.0f"),
						A0 - A1, A1 - A2, A2, PBShield->GetMaxHealth());

					// 破盾：另起一只满血铁卫（上面那只已经只剩 20 血，测不出"打穿前后"的差别）
					if (AFSEnemy* S2 = SpawnEnemyAt(EFSEnemyKind::Shielder,
						PBShield->GetActorLocation() + FVector(0.f, 230.f, 0.f)))
					{
						const float SArm = S2->GetArmorMaxHp();
						const float SH0 = S2->GetHealth();
						S2->DebugBreakArmor();
						S2->TakeDamage(100.f, FDamageEvent(), nullptr, PP);   // 正面
						UE_LOG(LogTemp, Log, TEXT("[FSPROBE] shielder armor=%.0f broken=%d front dmg after break=%.0f (of 100, expect 115)"),
							SArm, S2->IsArmorBroken() ? 1 : 0, SH0 - S2->GetHealth());
					}
				}
			}
			else if (PBStage == 2 && El > 0.7)
			{
				PBStage = 3;
				auto CountSlimes = [this]()
				{
					int32 N = 0;
					for (const TWeakObjectPtr<AFSEnemy>& W : Enemies)
					{
						AFSEnemy* E = W.Get();
						if (E && !E->IsDead() && E->GetKind() == EFSEnemyKind::Slime)
						{
							++N;
						}
					}
					return N;
				};
				const int32 Before = CountSlimes();
				if (PBSplit && !PBSplit->IsDead())
				{
					PBSplit->TakeDamage(9999.f, FDamageEvent(), nullptr, PP);
				}
				const int32 After = CountSlimes();
				UE_LOG(LogTemp, Log, TEXT("[FSPROBE] splitter -> slimes %d => %d (expect +2)"), Before, After);
			}
			// ---- 熔铁君王：先摆一只，测正面甲；再强起一记震波，测它到底打不打得到人
			else if (PBStage == 3 && El > 0.6)
			{
				PBStage = 4;
				PBTick = FPlatformTime::Seconds();
				const FVector Base = PP->GetActorLocation();
				const FVector Fwd = PP->GetActorRotation().Vector();
				PBWarlord = SpawnEnemyAt(EFSEnemyKind::Warlord, Base + Fwd * 820.f);
				UE_LOG(LogTemp, Log, TEXT("[FSPROBE] warlord spawned hp=%.0f radius=%.0f slamMax=%.0f"),
					PBWarlord ? PBWarlord->GetMaxHealth() : 0.f,
					PBWarlord ? PBWarlord->GetRadius() : 0.f,
					PBWarlord ? PBWarlord->GetSlamWaveMax() : 0.f);
			}
			else if (PBStage == 4 && El > 0.7)
			{
				PBStage = 5;
				PBTick = FPlatformTime::Seconds();
				if (PBWarlord && !PBWarlord->IsDead())
				{
					const float A0 = PBWarlord->GetHealth();
					PBWarlord->TakeDamage(100.f, FDamageEvent(), nullptr, PP);   // 正面甲
					const float A1 = PBWarlord->GetHealth();
					PBWarlord->SetActorRotation(PBWarlord->GetActorRotation() + FRotator(0.f, 180.f, 0.f));
					PBWarlord->TakeDamage(100.f, FDamageEvent(), nullptr, PP);   // 背面
					const float A2 = PBWarlord->GetHealth();
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] warlord front=%.0f back=%.0f (of 100 each) hp=%.0f/%.0f"),
						A0 - A1, A1 - A2, A2, PBWarlord->GetMaxHealth());

					// 震波数值要干净：先把其余小兵清掉，否则测出来的掉血里混着近战接触伤害
					for (const TWeakObjectPtr<AFSEnemy>& W : Enemies)
					{
						if (AFSEnemy* E = W.Get())
						{
							if (E != PBWarlord && !E->IsDead())
							{
								E->Destroy();
							}
						}
					}
					PBHpBefore = PP->GetHealthPct();
					PBWarlord->DebugForceSlam();
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] warlord slam forced, player hp=%.3f"), PBHpBefore);
				}
			}
			else if (PBStage == 5 && El > 2.4)
			{
				PBStage = 6;
				PBTick = FPlatformTime::Seconds();
				UE_LOG(LogTemp, Log, TEXT("[FSPROBE] slam result player hp %.3f => %.3f (expect lower)"),
					PBHpBefore, PP->GetHealthPct());
			}
			// ---- 破甲：甲打穿之后正面必须不再减伤，而且要多吃 15%
			else if (PBStage == 6 && El > 0.6)
			{
				PBStage = 7;
				PBTick = FPlatformTime::Seconds();
				if (PBWarlord && !PBWarlord->IsDead())
				{
					FVector F = PP->GetActorLocation() - PBWarlord->GetActorLocation();
					F.Z = 0.f;
					PBWarlord->SetActorRotation(F.Rotation());     // 先转回面对玩家
					const float H0 = PBWarlord->GetHealth();
					const float ArmMax = PBWarlord->GetArmorMaxHp();
					// 用真实的正面伤害把甲一点点削穿（不是 DebugBreakArmor）：
					// 这一条才是"玩家真的能打穿"的证据
					int32 Hits = 0;
					while (!PBWarlord->IsArmorBroken() && Hits < 30)
					{
						PBWarlord->TakeDamage(120.f, FDamageEvent(), nullptr, PP);
						++Hits;
					}
					const float H1 = PBWarlord->GetHealth();
					PBWarlord->TakeDamage(100.f, FDamageEvent(), nullptr, PP);   // 正面
					const float H2 = PBWarlord->GetHealth();
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] warlord armor=%.0f took %d front hits of 120 to break: broken=%d pct=%.0f%% front dmg after break=%.0f (expect 115) hp %.0f=>%.0f=>%.0f"),
						ArmMax, Hits, PBWarlord->IsArmorBroken() ? 1 : 0, PBWarlord->GetArmorPct() * 100.f,
						H1 - H2, H0, H1, H2);
				}
			}
			// ---- 换阶段要重新锻甲：否则第一阶段破甲后两阶段都变沙包
			else if (PBStage == 7 && El > 0.6)
			{
				PBStage = 8;
				PBTick = FPlatformTime::Seconds();
				if (PBWarlord && !PBWarlord->IsDead())
				{
					const float PctBefore = PBWarlord->GetArmorPct() * 100.f;
					PBWarlord->TakeDamage(9999.f, FDamageEvent(), nullptr, PP);  // 撞门槛 → 二阶段
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] reforge on phase %d: armor %.0f%% => %.0f%% (expect 100)"),
						PBWarlord->GetBossPhase(), PctBefore, PBWarlord->GetArmorPct() * 100.f);
				}
			}
			// ---- 精英词缀：四条效果各验一次数值，不靠"看起来对了"
			else if (PBStage == 8 && El > 0.6)
			{
				PBStage = 9;
				PBTick = FPlatformTime::Seconds();

				// 场上先清干净：上一段留了 BOSS 和一堆小兵，接触伤害会污染掉血读数
				Enemies.RemoveAll([](const TWeakObjectPtr<AFSEnemy>& W) { return !W.IsValid(); });
				for (const TWeakObjectPtr<AFSEnemy>& W : Enemies)
				{
					if (AFSEnemy* E = W.Get())
					{
						E->Destroy();
					}
				}
				Enemies.Reset();

				const FVector Base = PP->GetActorLocation();
				const FVector Fwd = PP->GetActorRotation().Vector();
				const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();

				// 同兵种对照：唯一的差别就是"精英"这一位，血量倍率一读即知
				PBEliteNormal = SpawnEnemyAt(EFSEnemyKind::Grunt, Base + Fwd * 1100.f - Right * 320.f);
				PBEliteRef = SpawnEnemyAt(EFSEnemyKind::Grunt, Base + Fwd * 1100.f + Right * 320.f,
					1.f, 1.f, EFSElite::Regen);
				PBThorn = SpawnEnemyAt(EFSEnemyKind::Brute, Base + Fwd * 380.f, 1.f, 1.f, EFSElite::Thorn);
				PBVolatile = SpawnEnemyAt(EFSEnemyKind::Slime, Base + Fwd * 250.f - Right * 90.f,
					1.f, 1.f, EFSElite::Volatile);

				const float NormalHp = PBEliteNormal ? PBEliteNormal->GetMaxHealth() : 0.f;
				const float EliteHp = PBEliteRef ? PBEliteRef->GetMaxHealth() : 0.f;
				UE_LOG(LogTemp, Log, TEXT("[FSPROBE] elite hp normal=%.0f elite=%.0f ratio=%.2f (expect 2.30) radius %.0f vs %.0f"),
					NormalHp, EliteHp, NormalHp > 0.f ? EliteHp / NormalHp : 0.f,
					PBEliteNormal ? PBEliteNormal->GetRadius() : 0.f,
					PBEliteRef ? PBEliteRef->GetRadius() : 0.f);

				// 再生：先削掉三成血，记下此刻血量；下一段看它有没有长回来
				if (PBEliteRef && !PBEliteRef->IsDead())
				{
					PBEliteRef->TakeDamage(PBEliteRef->GetMaxHealth() * 0.35f, FDamageEvent(), nullptr, PP);
					PBHpBefore = PBEliteRef->GetHealth();
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] regen wounded to %.0f/%.0f, waiting"),
						PBHpBefore, PBEliteRef->GetMaxHealth());
				}
			}
			else if (PBStage == 9 && El > 2.6)
			{
				PBStage = 10;
				PBTick = FPlatformTime::Seconds();

				// 再生：2.6 秒没挨打，应该已经回了一截
				if (PBEliteRef && !PBEliteRef->IsDead())
				{
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] regen after 2.6s %.0f => %.0f (expect higher)"),
						PBHpBefore, PBEliteRef->GetHealth());
				}

				// 荆棘：贴着它打一下，玩家必须掉血
				if (PBThorn && !PBThorn->IsDead())
				{
					FVector D = PBThorn->GetActorLocation() - PP->GetActorLocation();
					D.Z = 0.f;
					const float Before = PP->GetHealthPct();
					PBThorn->TakeDamage(40.f, FDamageEvent(), nullptr, PP);
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] thorn dist=%.0f player hp %.3f => %.3f (expect lower)"),
						D.Size(), Before, PP->GetHealthPct());
				}

				// 爆裂：贴脸打死它，记下击杀瞬间的血量，下一段核对引信是否真的炸了
				if (AFSEnemy* V = PBVolatile.Get())
				{
					FVector D = V->GetActorLocation() - PP->GetActorLocation();
					D.Z = 0.f;
					PBPlayerHp = PP->GetHealthPct();
					V->TakeDamage(99999.f, FDamageEvent(), nullptr, PP);
					UE_LOG(LogTemp, Log, TEXT("[FSPROBE] volatile killed at dist=%.0f fuse=%.2f player hp=%.3f"),
						D.Size(), V->GetVolatileFuse(), PBPlayerHp);
				}
			}
			else if (PBStage == 10 && El > 0.9)
			{
				PBStage = 11;
				bPBDone = true;
				UE_LOG(LogTemp, Log, TEXT("[FSPROBE] volatile after fuse: actorValid=%d player hp %.3f => %.3f (expect invalid + lower)"),
					PBVolatile.IsValid() ? 1 : 0, PBPlayerHp, PP->GetHealthPct());
			}
		}
	}

	// ---- 赐福卡验证（-fsbless）：开局直接弹一次三选一，截两张图再自动选掉
	// 用真实时间推进，避免和暂停 / 时间膨胀互相干扰
	{
		static bool bBLInit = false;
		static bool bBL = false;
		static int32 BLStage = 0;
		static double BLStart = 0.0;
		if (!bBLInit)
		{
			bBLInit = true;
			bBL = FParse::Param(FCommandLine::Get(), TEXT("fsbless"));
			BLStart = FPlatformTime::Seconds();

			// 调试：-fsrarity=0/1/2 把所有卡钉死在同一档，方便一次跑完三档配色核对
			FString RarArg;
			if (FParse::Value(FCommandLine::Get(), TEXT("fsrarity="), RarArg))
			{
				ForceRarity = FMath::Clamp(FCString::Atoi(*RarArg), 0, (int32)EFSRarity::Count - 1);
			}

			// 调试：-fspath=0/1/2 让机器人专挑某条流派，用来验证觉醒
			FString PathArg;
			if (FParse::Value(FCommandLine::Get(), TEXT("fspath="), PathArg))
			{
				ForcePath = FMath::Clamp(FCString::Atoi(*PathArg), 0, (int32)EFSPath::Count - 1);
			}
		}
		if (bBL)
		{
			const double El = FPlatformTime::Seconds() - BLStart;
			if (BLStage == 0 && El > 4.5 && Phase == EFSPhase::Playing && !bChoosing)
			{
				BLStage = 1;
				OpenBlessing();
			}
			else if (BLStage == 1 && El > 5.6)
			{
				BLStage = 2;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Blessing_00.png"), false, false);
			}
			else if (BLStage == 2 && El > 6.6)
			{
				BLStage = 3;
				BlessingMove(1);
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Blessing_01.png"), false, false);
			}
			else if (BLStage == 3 && El > 7.6)
			{
				BLStage = 4;

				// 顺便验证鼠标点击命中：三张卡里中间那张的中心正好是屏幕中心（布局对称）
				float VX = 1600.f, VY = 900.f;
				if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
				{
					int32 SX = 0, SY = 0;
					PC->GetViewportSize(SX, SY);
					if (SX > 0 && SY > 0)
					{
						VX = (float)SX;
						VY = (float)SY;
					}
				}
				const bool bHit = BlessingClickAt(VX * 0.5f, VY * 0.56f, VX, VY);
				UE_LOG(LogTemp, Log, TEXT("[FSBLESS] mouse click center hit=%d (viewport %.0fx%.0f)"),
					bHit ? 1 : 0, VX, VY);
			}
			else if (BLStage == 4 && El > 8.8)
			{
				BLStage = 5;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Blessing_02.png"), false, false);
			}
		}
	}

	// ---- 流派觉醒验证（-fsawaken=0/1/2）：开局直接把某条流派叠满并触发觉醒，便于截图
	{
		static bool bAWInit = false;
		static bool bAW = false;
		static int32 AWPath = 0;
		static int32 AWStage = 0;
		static double AWStart = 0.0;
		if (!bAWInit)
		{
			bAWInit = true;
			FString AWArg;
			bAW = FParse::Value(FCommandLine::Get(), TEXT("fsawaken="), AWArg);
			if (bAW)
			{
				AWPath = FMath::Clamp(FCString::Atoi(*AWArg), 0, (int32)EFSPath::Count - 1);
				AWStart = FPlatformTime::Seconds();
			}
		}
		if (bAW)
		{
			const double El = FPlatformTime::Seconds() - AWStart;
			if (AWStage == 0 && El > 7.2 && Phase == EFSPhase::Playing && !bChoosing)
			{
				AWStage = 1;
				// 每条流派的代表赐福（索引见 EFSUpgrade）：锋刃 / 疾风 / 铁躯
				static const int32 RepKind[3] = { 0, 2, 3 };
				if (AFSPlayerChar* AP = GetPlayerChar())
				{
					for (int32 i = 0; i < FSDefs::PathAwakenStacks; ++i)
					{
						AP->ApplyUpgrade(RepKind[AWPath], 1, true);
					}
					// 壁垒流派再多塞两层，好顺带验证护壁减伤与嗜血回血的日志
					if (AWPath == 2)
					{
						AP->ApplyUpgrade((int32)EFSUpgrade::Ward, 1, true);
						AP->ApplyUpgrade((int32)EFSUpgrade::Leech, 1, true);
					}
				}
				AnnouncePathAwaken((EFSPath)AWPath);
			}
			else if (AWStage == 1 && El > 7.45)
			{
				AWStage = 2;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Awaken.png"), false, false);
			}
			else if (AWStage == 2 && El > 10.2)
			{
				// 再来一张"演出散场后"的：7.45 那张正赶上冲击波铺满地面，
				// 看不出常驻光环和碎晶到底长什么样
				AWStage = 3;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("AwakenIdle.png"), false, false);
			}
		}
	}

	// ---- 不屈免死验证（-fsundying）：注入壁垒觉醒后自己吃一发致命伤，
	// 用真实时间推进（顿帧期间游戏时间几乎不走），截下绿闪那一刻
	{
		static bool bUDInit = false;
		static bool bUD = false;
		static int32 UDStage = 0;
		static double UDStart = 0.0;
		if (!bUDInit)
		{
			bUDInit = true;
			bUD = FParse::Param(FCommandLine::Get(), TEXT("fsundying"));
			UDStart = FPlatformTime::Seconds();
		}
		if (bUD)
		{
			const double El = FPlatformTime::Seconds() - UDStart;
			if (UDStage == 0 && El > 7.2 && Phase == EFSPhase::Playing && !bChoosing)
			{
				UDStage = 1;
				if (AFSPlayerChar* AP = GetPlayerChar())
				{
					for (int32 i = 0; i < FSDefs::PathAwakenStacks; ++i)
					{
						AP->ApplyUpgrade(3, 1, true);   // 铁躯 → 壁垒
					}
					AnnouncePathAwaken(EFSPath::Ward);
				}
			}
			else if (UDStage == 1 && El > 7.8)
			{
				UDStage = 2;
				if (AFSPlayerChar* AP = GetPlayerChar())
				{
					// 9999 的伤害一定要让它"必死"：不屈要能在真正的致命伤前生效，
					// 而不是只在擦伤时挡一下
					const float Ret = AP->TakeDamage(9999.f, FDamageEvent(), nullptr, nullptr);
					UE_LOG(LogTemp, Warning, TEXT("[FSUNDYING] lethal hit returned=%.1f hp=%.1f dilation=%.2f"),
						Ret, AP->GetHealthPct() * 100.f,
						GetWorld() ? UGameplayStatics::GetGlobalTimeDilation(GetWorld()) : -1.f);
				}
			}
			else if (UDStage == 2 && El > 7.95)
			{
				UDStage = 3;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Undying.png"), false, false);
			}
			else if (UDStage == 3 && El > 9.6)
			{
				UDStage = 4;
				// 冷却中的 chip 应该显示秒数并把"不屈"压暗
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("UndyingCd.png"), false, false);
			}
		}
	}

	// ---- 残影窗口验证（-fssurge）：注入疾影觉醒后主动闪避，
	// 在 0.8 秒窗口里截图，检查准星弧与弹丸加粗
	{
		static bool bSGInit = false;
		static bool bSG = false;
		static int32 SGStage = 0;
		static double SGStart = 0.0;
		if (!bSGInit)
		{
			bSGInit = true;
			bSG = FParse::Param(FCommandLine::Get(), TEXT("fssurge"));
			SGStart = FPlatformTime::Seconds();
		}
		if (bSG)
		{
			const double El = FPlatformTime::Seconds() - SGStart;
			if (SGStage == 0 && El > 7.2 && Phase == EFSPhase::Playing && !bChoosing)
			{
				SGStage = 1;
				if (AFSPlayerChar* AP = GetPlayerChar())
				{
					for (int32 i = 0; i < FSDefs::PathAwakenStacks; ++i)
					{
						AP->ApplyUpgrade(2, 1, true);   // 疾风 → 疾影
					}
					AnnouncePathAwaken(EFSPath::Swift);
				}
			}
			else if (SGStage == 1 && El > 7.5)
			{
				SGStage = 2;
				if (AFSPlayerChar* AP = GetPlayerChar())
				{
					AP->SetMoveInput(FVector2D(1.f, 0.f));
					AP->TryDash();
					UE_LOG(LogTemp, Warning, TEXT("[FSSURGE] dash surge=%.2f"), AP->GetSurgePct());
				}
			}
			else if (SGStage == 2 && El > 7.60)
			{
				SGStage = 3;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("Surge.png"), false, false);
			}
			else if (SGStage == 3 && El > 8.05)
			{
				// 第二张取窗口尾部：两张对比能看出脚下青环确实在往里收
				SGStage = 4;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("SurgeLate.png"), false, false);
			}
		}
	}

	// ---- 暂停菜单演示（-fspause）：自动弹出、截图、再自动恢复，用于验证整条链路
	// 用真实时间推进，因为暂停后游戏时间被压到几乎不动
	{
		static bool bPDInit = false;
		static bool bPD = false;
		static double PDStart = 0.0;
		static int32 PDStage = 0;
		if (!bPDInit)
		{
			bPDInit = true;
			bPD = FParse::Param(FCommandLine::Get(), TEXT("fspause"));
			PDStart = FPlatformTime::Seconds();
		}
		if (bPD)
		{
			const double El = FPlatformTime::Seconds() - PDStart;
			if (PDStage == 0 && El > 5.5)
			{
				PDStage = 1;
				SetPaused(true);
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("PauseMenu_00.png"), false, false);
			}
			else if (PDStage == 1 && El > 9.0)
			{
				PDStage = 2;
				SetPaused(false);
			}
			else if (PDStage == 2 && El > 10.5)
			{
				PDStage = 3;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("PauseMenu_01.png"), false, false);
			}
		}
	}

	// ---- 返回标题验证（-fsretitle）：暂停后自动触发"返回标题"，再截一张确认没卡住
	// 用真实时间推进；跨关卡重载后静态状态会延续，所以新关卡的 Tick 也能接着截图
	{
		static bool bRTInit = false;
		static bool bRT = false;
		static double RTStart = 0.0;
		static int32 RTStage = 0;
		static float RTDelay = 5.f;
		if (!bRTInit)
		{
			bRTInit = true;
			FString RTArg;
			bRT = FParse::Value(FCommandLine::Get(), TEXT("fsretitle="), RTArg)
				|| FParse::Param(FCommandLine::Get(), TEXT("fsretitle"));
			if (!RTArg.IsEmpty())
			{
				RTDelay = FMath::Max(1.f, FCString::Atof(*RTArg));
			}
			RTStart = FPlatformTime::Seconds();
		}
		if (bRT)
		{
			const double El = FPlatformTime::Seconds() - RTStart;
			if (RTStage == 0 && El > RTDelay)
			{
				RTStage = 1;
				SetPaused(true);
				MenuIndex = 5;
				MenuActivate();
			}
			else if (RTStage == 1 && El > RTDelay + 6.0)
			{
				RTStage = 2;
				FScreenshotRequest::RequestScreenshot(
					FPaths::ProjectSavedDir() / TEXT("BackTitle_00.png"), false, false);
			}
		}
	}

	DamageFlash = FMath::Max(0.f, DamageFlash - DeltaSeconds * 2.2f);
	BannerTime = FMath::Max(0.f, BannerTime - DeltaSeconds);
	ComboPulse = FMath::Max(0.f, ComboPulse - DeltaSeconds * 2.5f);
	LevelPulse = FMath::Max(0.f, LevelPulse - DeltaSeconds * 2.0f);

	// 连击计时用游戏时间推进：暂停时时间膨胀接近 0，计时自然停住
	if (Phase == EFSPhase::Playing && ComboTimer > 0.f)
	{
		ComboTimer -= DeltaSeconds;
		if (ComboTimer <= 0.f)
		{
			ComboTimer = 0.f;
			Combo = 0;
		}
	}

	const bool bPlay = (Phase == EFSPhase::Playing);
	if (bPlay)
	{
		RunTime += DeltaSeconds;
		TickWeather(DeltaSeconds);

		if (GuardianLeft > 0.f)
		{
			GuardianLeft -= DeltaSeconds;
			if (GuardianLeft <= 0.f)
			{
				bGuardianOn = false;
				Guardian = nullptr;
			}
		}

		if (bChoosing)
		{
			// 选卡期间冻结波次推进：InterWave 不递减，下一波不会在玩家还在挑的时候开
			ChoiceElapsed += DeltaSeconds;

			AFSPlayerChar* CP = GetPlayerChar();
			const bool bBotPick = CP && CP->IsBot() && ChoiceElapsed > 1.6f && Choices.Num() > 0;
			if (ChoiceElapsed >= ChoiceLimit)
			{
				BlessingPick(ChoiceSel);
			}
			else if (bBotPick)
			{
				// 机器人演示时随便挑一张，别让录像卡在选卡界面。
				// -fspath=N 时改成优先挑第 N 条流派，用来验证"叠满三层觉醒"。
				int32 Pick = FMath::RandRange(0, Choices.Num() - 1);
				if (ForcePath >= 0)
				{
					for (int32 i = 0; i < Choices.Num(); ++i)
					{
						if ((int32)FSDefs::PathOf(Choices[i]) == ForcePath)
						{
							Pick = i;
							break;
						}
					}
				}
				BlessingPick(Pick);
			}
		}
		else if (InterWave > 0.f)
		{
			InterWave -= DeltaSeconds;
			if (InterWave <= 0.f)
			{
				StartNextWave();
			}
		}
		else if (SpawnQueue.Num() > 0)
		{
			SpawnTimer -= DeltaSeconds;
			if (SpawnTimer <= 0.f)
			{
				const EFSEnemyKind K = SpawnQueue[0];
				const EFSElite KElite = SpawnEliteQueue.IsValidIndex(0) ? SpawnEliteQueue[0] : EFSElite::None;
				SpawnQueue.RemoveAt(0);
				if (SpawnEliteQueue.Num() > 0)
				{
					SpawnEliteQueue.RemoveAt(0);
				}
				AFSPlayerChar* P = GetPlayerChar();
				const FVector Around = P ? P->GetActorLocation() : FVector::ZeroVector;
				SpawnEnemyAt(K, FSRandomSpawnRing(GetWorld(), Around, ArenaHalf, 1500.f), 1.f, 1.f, KElite);
				SpawnTimer = 0.11f;
			}
		}
		else if (bWaveActive && CountAlive() == 0)
		{
			bWaveActive = false;
			InterWave = 3.2f;   // 兜底：如果赐福没弹出来（比如已经处于选择中），照常按原节奏推进
			SetBanner(FS_FMT(TEXT("第 %d 波 清 空"), TEXT("WAVE %d CLEAR"), Wave),
				FSUtil::L(TEXT("准备迎接下一波"), TEXT("BRACE FOR THE NEXT WAVE")), 1.8f);

			// 每清一波小回血
			if (AFSPlayerChar* P = GetPlayerChar())
			{
				P->Heal(6.f);
			}

			// 打完 BOSS 的那一波，赐福保底一张传世
			if (Wave % 5 == 0)
			{
				bBossBlessGuarantee = true;
			}

			// 清完一波给一次三选一赐福，选卡期间波次推进被挂起
			OpenBlessing();
		}
	}

	// 环境底噪跟着波次变紧张
	FSAudio::SetAmbience(this, Phase == EFSPhase::Playing,
		FMath::Clamp((float)Wave / 14.f, 0.f, 1.f));

	if (Arena)
	{
		Arena->TickWeatherVisuals(Weather, DeltaSeconds);
		const AFSPlayerChar* P = GetPlayerChar();

		float Wet = 0.f;
		if (Phase != EFSPhase::Title)
		{
			Wet = (Weather == EFSWeather::Storm) ? 1.f : (Weather == EFSWeather::Rain ? 0.62f : 0.f);
		}
		Arena->UpdateParticles(DeltaSeconds, P ? P->GetActorLocation() : FVector::ZeroVector, Wet);
	}
}
