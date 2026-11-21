#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SynthComponent.h"
#include "FSAudio.generated.h"

/** 音效种类（全部为运行时软件合成，不依赖任何音频资源） */
UENUM()
enum class EFSSfx : uint8
{
	Shot,           // 短枪
	ShotHeavy,      // 霰弹 / 火箭筒
	Laser,          // 激光
	Bow,            // 猎弓
	Launcher,       // 火箭筒
	Hit,            // 命中
	Kill,           // 击杀
	Hurt,           // 玩家受伤
	Coin,           // 金币
	Rune,           // 符石
	Upgrade,        // 拾取武器 / 升级
	Heal,           // 回血
	WaveStart,      // 新波次
	Boss,           // BOSS 登场
	Dash,           // 闪现
	Guardian,       // 守护兽降临
	GuardianShot,   // 守护兽开火
	GameOver,       // 结算
	Beep            // UI / 天气提示
};

/** 一个发声单元（纯 C++ 结构，供音频线程使用） */
struct FFSVoice
{
	EFSSfx Type = EFSSfx::Shot;
	float T = 0.f;
	float Dur = 0.2f;
	float Vol = 1.f;
	float Pan = 0.f;
	float Pitch = 1.f;
	float P1 = 0.f;
	float P2 = 0.f;
	float P3 = 0.f;
	float Lp = 0.f;
	uint32 Seed = 1;
};

/**
 * 软件合成器：把若干一次性音效 + 一层环境底噪混合成立体声输出。
 * 所有波形在音频线程实时计算，工程里不需要任何 wav 资源。
 */
UCLASS()
class UFSynthComp : public USynthComponent
{
	GENERATED_BODY()

public:
	UFSynthComp(const FObjectInitializer& ObjectInitializer);

	/** 游戏线程调用：入队一个音效 */
	void Push(EFSSfx Type, float Vol, float Pan, float Pitch);

	/** 环境底噪开关与紧张度（0~1，越高越吵） */
	void SetAmbience(bool bOn, float Tension);

	/** 总静音 */
	void SetMuted(bool bIn) { Muted = bIn ? 1 : 0; }
	bool GetMuted() const { return Muted.Load() != 0; }

	/** 环境底噪单独的音量（调主音量时立刻生效，不用等下一个音效） */
	void SetAmbienceVolume(float In)
	{
		AmbVolPct = FMath::Clamp(FMath::RoundToInt(In * 100.f), 0, 100);
	}

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	TQueue<FFSVoice, EQueueMode::Spsc> Pending;
	TArray<FFSVoice> Voices;

	TAtomic<int32> AmbOn{ 0 };
	TAtomic<int32> TensionPct{ 0 };
	TAtomic<int32> Muted{ 0 };
	TAtomic<int32> AmbVolPct{ 100 };

	float AmbGain = 0.f;
	float AmbT = 0.f;
	float AmbP1 = 0.f;
	float AmbP2 = 0.f;
	float AmbP3 = 0.f;
	float AmbP4 = 0.f;
	float AmbLp = 0.f;
	int32 Sr = 48000;
};

/** 承载合成器的隐藏 Actor（全局唯一） */
UCLASS()
class AFSAudioRig : public AActor
{
	GENERATED_BODY()

public:
	AFSAudioRig(const FObjectInitializer& ObjectInitializer);

	UPROPERTY()
	UFSynthComp* Synth = nullptr;
};

namespace FSAudio
{
	/** 在世界中播放一次音效（带距离衰减与左右声道定位） */
	void Play(const UObject* WorldContext, EFSSfx Sfx, const FVector& Loc, float Volume = 1.f, float Pitch = 1.f);

	/** 无位置音效（UI / 结算） */
	void Play2D(const UObject* WorldContext, EFSSfx Sfx, float Volume = 1.f, float Pitch = 1.f);

	/** 环境底噪：进战斗开，回标题关 */
	void SetAmbience(const UObject* WorldContext, bool bOn, float Tension = 0.f);

	void ToggleMute(const UObject* WorldContext);
	bool IsMuted(const UObject* WorldContext);

	/** 主音量 0..1（暂停菜单可调） */
	void SetMasterVolume(const UObject* WorldContext, float InVolume);
	float GetMasterVolume(const UObject* WorldContext);
}
