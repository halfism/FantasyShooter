#include "FSAudio.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	constexpr float FsTwoPi = 6.28318531f;

	FORCEINLINE float FsNoise(FFSVoice& V)
	{
		V.Seed ^= V.Seed << 13;
		V.Seed ^= V.Seed >> 17;
		V.Seed ^= V.Seed << 5;
		return (float)(V.Seed & 0x00FFFFFFu) * (2.f / 16777215.f) - 1.f;
	}

	FORCEINLINE float FsSine(float& P, float Freq, float Dt)
	{
		P += Freq * Dt;
		P -= FMath::FloorToFloat(P);
		return FMath::Sin(P * FsTwoPi);
	}

	FORCEINLINE float FsSaw(float& P, float Freq, float Dt)
	{
		P += Freq * Dt;
		P -= FMath::FloorToFloat(P);
		return 2.f * P - 1.f;
	}

	FORCEINLINE float FsEnv(float T, float K)
	{
		return FMath::Exp(-T * K);
	}

	FORCEINLINE float FsAtk(float T, float K)
	{
		return FMath::Min(1.f, T * K);
	}

	/** 单个发声单元的采样值（同时推进相位与时间） */
	float VoiceSample(FFSVoice& V, float Dt)
	{
		const float t = FMath::Max(0.f, V.T);
		float s = 0.f;

		switch (V.Type)
		{
		case EFSSfx::Shot:
		{
			const float F = (160.f + 980.f * FsEnv(t, 34.f)) * V.Pitch;
			const float Tone = FsSine(V.P1, F, Dt);
			const float Sqr = FsSine(V.P2, F * 1.004f, Dt) > 0.f ? 1.f : -1.f;
			const float Noi = FsNoise(V) * FsEnv(t, 55.f) * 0.5f;
			s = (Tone * 0.62f + Sqr * 0.15f + Noi) * FsEnv(t, 23.f);
			break;
		}
		case EFSSfx::ShotHeavy:
		{
			const float Noi = FsNoise(V);
			V.Lp = FMath::Lerp(V.Lp, Noi, 0.18f);
			const float Thump = FsSine(V.P1, (68.f + 62.f * FsEnv(t, 18.f)) * V.Pitch, Dt);
			s = (V.Lp * 0.85f + Thump * 0.72f) * FsEnv(t, 8.5f);
			break;
		}
		case EFSSfx::Laser:
		{
			const float F = (320.f + 1700.f * FsEnv(t, 15.f)) * V.Pitch;
			s = (FsSine(V.P1, F, Dt) * 0.75f + FsSine(V.P2, F * 2.02f, Dt) * 0.20f) * FsEnv(t, 9.5f);
			break;
		}
		case EFSSfx::Bow:
		{
			const float F = 420.f * V.Pitch;
			const float Pluck = FsAtk(t, 220.f) * FsEnv(t, 17.f);
			s = (FsSine(V.P1, F, Dt) * 0.8f + FsSine(V.P2, F * 2.71f, Dt) * 0.24f) * Pluck
				+ FsNoise(V) * FsEnv(t, 90.f) * 0.3f;
			break;
		}
		case EFSSfx::Launcher:
		{
			const float F = (48.f + 52.f * FsEnv(t, 7.f)) * V.Pitch;
			const float Noi = FsNoise(V);
			V.Lp = FMath::Lerp(V.Lp, Noi, 0.10f);
			s = (FsSine(V.P1, F, Dt) * 0.85f + V.Lp * 0.8f) * FsAtk(t, 80.f) * FsEnv(t, 5.2f);
			break;
		}
		case EFSSfx::Hit:
		{
			const float Noi = FsNoise(V);
			V.Lp = FMath::Lerp(V.Lp, Noi, 0.62f);
			s = (Noi - V.Lp) * FsEnv(t, 58.f) * 0.95f
				+ FsSine(V.P1, 1400.f * V.Pitch, Dt) * 0.16f * FsEnv(t, 70.f);
			break;
		}
		case EFSSfx::Kill:
		{
			const float F = (130.f + 190.f * FsEnv(t, 9.f)) * V.Pitch;
			const float Noi = FsNoise(V);
			V.Lp = FMath::Lerp(V.Lp, Noi, 0.25f);
			s = (FsSine(V.P1, F, Dt) * 0.7f + V.Lp * 0.7f) * FsEnv(t, 12.f);
			break;
		}
		case EFSSfx::Hurt:
		{
			const float Vib = 1.f + 0.06f * FMath::Sin(t * 62.f);
			const float F = 148.f * V.Pitch * Vib;
			const float Noi = FsNoise(V);
			V.Lp = FMath::Lerp(V.Lp, Noi, 0.30f);
			s = (FsSaw(V.P1, F, Dt) * 0.42f + FsSaw(V.P2, F * 1.5f, Dt) * 0.20f + V.Lp * 0.35f)
				* FsAtk(t, 120.f) * FsEnv(t, 6.2f);
			break;
		}
		case EFSSfx::Coin:
		{
			const bool bHigh = t > 0.055f;
			const float Local = bHigh ? (t - 0.055f) : t;
			const float F = (bHigh ? 1568.f : 1046.f) * V.Pitch;
			s = (FsSine(V.P1, F, Dt) * 0.8f + FsSine(V.P2, F * 2.f, Dt) * 0.2f)
				* FsAtk(Local, 400.f) * FsEnv(Local, 32.f) * 0.62f;
			break;
		}
		case EFSSfx::Rune:
		{
			const float Step = 0.085f;
			const int32 Idx = FMath::Min(3, (int32)(t / Step));
			const float Local = t - Idx * Step;
			static const float Notes[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
			const float F = Notes[Idx] * V.Pitch;
			s = (FsSine(V.P1, F, Dt) * 0.7f + FsSine(V.P2, F * 2.f, Dt) * 0.18f)
				* FsAtk(Local, 300.f) * FsEnv(Local, 10.5f) * 0.72f;
			break;
		}
		case EFSSfx::Upgrade:
		{
			const float F = 300.f * FMath::Pow(4.f, FMath::Min(1.f, t / 0.32f)) * V.Pitch;
			s = (FsSine(V.P1, F, Dt) * 0.58f + FsSine(V.P2, F * 1.5f, Dt) * 0.24f)
				* FsAtk(t, 60.f) * FsEnv(t, 6.0f);
			break;
		}
		case EFSSfx::Heal:
		{
			const float F = (t > 0.09f ? 880.f : 660.f) * V.Pitch;
			s = FsSine(V.P1, F, Dt) * FsAtk(t, 90.f) * FsEnv(t, 8.f) * 0.5f;
			break;
		}
		case EFSSfx::WaveStart:
		{
			const float F = 110.f * V.Pitch;
			const float Noi = FsNoise(V);
			V.Lp = FMath::Lerp(V.Lp, Noi, 0.05f);
			s = (FsSine(V.P1, F, Dt) * 0.55f + FsSine(V.P2, F * 1.5f, Dt) * 0.34f
				+ FsSine(V.P3, F * 2.f, Dt) * 0.22f + V.Lp * 0.45f)
				* FsAtk(t, 6.f) * FsEnv(t, 1.5f);
			break;
		}
		case EFSSfx::Boss:
		{
			const float Vib = 1.f + 0.045f * FMath::Sin(t * 31.f);
			const float F = 52.f * V.Pitch * Vib;
			const float Noi = FsNoise(V);
			V.Lp = FMath::Lerp(V.Lp, Noi, 0.08f);
			s = (FsSaw(V.P1, F, Dt) * 0.52f + FsSaw(V.P2, F * 1.99f, Dt) * 0.24f + V.Lp * 0.55f)
				* FsAtk(t, 4.f) * FsEnv(t, 1.1f);
			break;
		}
		case EFSSfx::Dash:
		{
			const float Noi = FsNoise(V);
			const float Cut = 0.05f + 0.5f * FsEnv(t, 12.f);
			V.Lp = FMath::Lerp(V.Lp, Noi, Cut);
			s = V.Lp * FsAtk(t, 90.f) * FsEnv(t, 8.f) * 1.15f;
			break;
		}
		case EFSSfx::Guardian:
		{
			s = (FsSine(V.P1, 880.f * V.Pitch, Dt) * 0.34f
				+ FsSine(V.P2, 1320.f * V.Pitch, Dt) * 0.24f
				+ FsSine(V.P3, 1760.f * V.Pitch, Dt) * 0.17f)
				* FsAtk(t, 40.f) * FsEnv(t, 3.4f);
			break;
		}
		case EFSSfx::GuardianShot:
		{
			const float F = (1500.f - 700.f * FMath::Min(1.f, t / 0.09f)) * V.Pitch;
			s = FsSine(V.P1, F, Dt) * FsEnv(t, 26.f) * 0.45f;
			break;
		}
		case EFSSfx::GameOver:
		{
			const float Step = 0.24f;
			const int32 Idx = FMath::Min(3, (int32)(t / Step));
			const float Local = t - Idx * Step;
			static const float Notes[4] = { 440.f, 349.23f, 261.63f, 196.f };
			const float F = Notes[Idx] * V.Pitch;
			s = (FsSine(V.P1, F, Dt) * 0.7f + FsSine(V.P2, F * 0.5f, Dt) * 0.28f)
				* FsAtk(Local, 60.f) * FsEnv(Local, 4.2f) * 0.75f;
			break;
		}
		case EFSSfx::Beep:
		{
			s = FsSine(V.P1, 880.f * V.Pitch, Dt) * FsAtk(t, 200.f) * FsEnv(t, 22.f) * 0.35f;
			break;
		}
		case EFSSfx::ArtCast:
		{
			// 三招共用这一个"起手"：低频砸地告诉玩家"按到了"，
			// 上升扫频把动作往前推，金属尾给一点仪式感。性格差异只靠 Pitch 分。
			const float Thump = FsSine(V.P1, 64.f * V.Pitch, Dt) * FsEnv(t, 8.5f) * 0.85f;
			const float SweepF = (180.f + 820.f * FMath::Min(1.f, t / 0.14f)) * V.Pitch;
			const float Sweep = FsSine(V.P2, SweepF, Dt) * FsAtk(t, 45.f) * FsEnv(t, 4.6f) * 0.42f;
			const float Tail = FsSine(V.P3, 1560.f * V.Pitch, Dt) * FsAtk(t, 90.f) * FsEnv(t, 3.0f) * 0.16f;
			// 一点点噪声爆点：纯正弦起手会"太干净"，像 UI 音效而不是打击
			const float Crunch = FsNoise(V) * FsEnv(t, 26.f) * 0.22f;
			s = Thump + Sweep + Tail + Crunch;
			break;
		}
		default:
			break;
		}

		V.T += Dt;
		return s;
	}

	float VoiceDuration(EFSSfx Type)
	{
		switch (Type)
		{
		case EFSSfx::Shot: return 0.14f;
		case EFSSfx::ShotHeavy: return 0.30f;
		case EFSSfx::Laser: return 0.22f;
		case EFSSfx::Bow: return 0.18f;
		case EFSSfx::Launcher: return 0.50f;
		case EFSSfx::Hit: return 0.08f;
		case EFSSfx::Kill: return 0.30f;
		case EFSSfx::Hurt: return 0.34f;
		case EFSSfx::Coin: return 0.18f;
		case EFSSfx::Rune: return 0.55f;
		case EFSSfx::Upgrade: return 0.42f;
		case EFSSfx::Heal: return 0.26f;
		case EFSSfx::WaveStart: return 1.20f;
		case EFSSfx::Boss: return 1.50f;
		case EFSSfx::Dash: return 0.28f;
		case EFSSfx::Guardian: return 1.00f;
		case EFSSfx::GuardianShot: return 0.12f;
		case EFSSfx::GameOver: return 1.10f;
		case EFSSfx::Beep: return 0.10f;
		case EFSSfx::ArtCast: return 0.85f;
		default: return 0.2f;
		}
	}
}

// ------------------------------------------------------------------ 合成器

UFSynthComp::UFSynthComp(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bAllowSpatialization = false;
	bAutoDestroy = false;
	bStopWhenOwnerDestroyed = true;
	NumChannels = 2;
}

bool UFSynthComp::Init(int32& SampleRate)
{
	Sr = (SampleRate > 0) ? SampleRate : 48000;
	NumChannels = 2;
	Voices.Reset();
	Voices.Reserve(64);
	AmbGain = 0.f;
	return true;
}

void UFSynthComp::Push(EFSSfx Type, float Vol, float Pan, float Pitch)
{
	FFSVoice V;
	V.Type = Type;
	V.Vol = FMath::Clamp(Vol, 0.f, 2.f);
	V.Pan = FMath::Clamp(Pan, -1.f, 1.f);
	V.Pitch = FMath::Clamp(Pitch, 0.5f, 2.f);
	V.Dur = VoiceDuration(Type);
	V.Seed = ((uint32)FMath::RandRange(1, 0x7FFFFFFF) * 2654435761u) | 1u;
	Pending.Enqueue(V);
}

void UFSynthComp::SetAmbience(bool bOn, float Tension)
{
	AmbOn = bOn ? 1 : 0;
	TensionPct = FMath::Clamp(FMath::RoundToInt(Tension * 100.f), 0, 100);
}

int32 UFSynthComp::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (!OutAudio || NumSamples <= 0)
	{
		return 0;
	}

	const int32 Ch = FMath::Clamp((int32)NumChannels, 1, 2);
	const int32 Frames = NumSamples / Ch;
	if (Frames <= 0)
	{
		return 0;
	}
	FMemory::Memzero(OutAudio, sizeof(float) * NumSamples);

	FFSVoice In;
	while (Pending.Dequeue(In))
	{
		if (Voices.Num() < 64)
		{
			Voices.Add(In);
		}
	}

	const float Dt = 1.f / (float)Sr;
	const bool bMute = Muted.Load() != 0;
	const float AmbVol = (float)AmbVolPct.Load() / 100.f;
	const float AmbTarget = (bMute || AmbOn.Load() == 0 || AmbVol <= 0.001f)
		? 0.f
		: (0.105f + 0.10f * (float)TensionPct.Load() / 100.f) * AmbVol;
	const float MuteGain = bMute ? 0.f : 1.f;

	for (int32 i = 0; i < Frames; ++i)
	{
		float L = 0.f;
		float R = 0.f;

		for (int32 v = 0; v < Voices.Num(); ++v)
		{
			FFSVoice& V = Voices[v];
			const float S = VoiceSample(V, Dt) * V.Vol;
			const float Ang = (V.Pan + 1.f) * 0.25f * PI;
			L += S * FMath::Cos(Ang) * 1.41421356f;
			R += S * FMath::Sin(Ang) * 1.41421356f;
		}

		// 环境底噪：低频持续音 + 心跳式脉冲
		AmbGain = FMath::Lerp(AmbGain, AmbTarget, 1.f - FMath::Exp(-Dt * 1.8f));
		if (AmbGain > 0.0008f)
		{
			AmbT += Dt;
			constexpr float Root = 55.f;
			const float Drone = FsSine(AmbP1, Root, Dt) * 0.50f
				+ FsSine(AmbP2, Root * 1.4983f, Dt) * 0.20f
				+ FsSine(AmbP3, Root * 2.007f, Dt) * 0.09f;
			const float BeatPh = FMath::Fmod(AmbT, 0.56f);
			const float Pulse = FMath::Exp(-BeatPh * 10.f)
				* FsSine(AmbP4, 52.f + 42.f * FMath::Exp(-BeatPh * 24.f), Dt) * 0.7f;
			const float Trem = 0.72f + 0.28f * FMath::Sin(AmbT * 0.6f);
			AmbLp = FMath::Lerp(AmbLp, Drone * Trem + Pulse, 0.35f);

			const float A = AmbLp * AmbGain;
			L += A;
			R += A * 0.93f;
		}

		OutAudio[i * Ch] = FMath::Clamp(L * MuteGain, -1.f, 1.f);
		if (Ch > 1)
		{
			OutAudio[i * Ch + 1] = FMath::Clamp(R * MuteGain, -1.f, 1.f);
		}
	}

	// 清理已结束的音
	for (int32 v = Voices.Num() - 1; v >= 0; --v)
	{
		if (Voices[v].T >= Voices[v].Dur)
		{
			Voices.RemoveAtSwap(v, 1, EAllowShrinking::No);
		}
	}

	return NumSamples;
}

// ------------------------------------------------------------------ 承载 Actor

AFSAudioRig::AFSAudioRig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	Synth = CreateDefaultSubobject<UFSynthComp>(TEXT("Synth"));
	if (Synth)
	{
		Synth->SetupAttachment(Root);
	}
}

// ------------------------------------------------------------------ 对外接口

namespace
{
	AFSAudioRig* GetRig(const UObject* Ctx)
	{
		if (!Ctx || !GEngine)
		{
			return nullptr;
		}
		UWorld* W = GEngine->GetWorldFromContextObject(Ctx, EGetWorldErrorMode::ReturnNull);
		if (!W || W->bIsTearingDown)
		{
			return nullptr;
		}

		static TWeakObjectPtr<AFSAudioRig> Cached;
		if (Cached.IsValid() && Cached->GetWorld() == W)
		{
			return Cached.Get();
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AFSAudioRig* Rig = W->SpawnActor<AFSAudioRig>(AFSAudioRig::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, Params);
		Cached = Rig;
		if (Rig && Rig->Synth)
		{
			Rig->Synth->Start();
		}
		return Rig;
	}
}

namespace
{
	/** 主音量（进程级，改完立即对所有后续音效生效） */
	float GFS_MasterVolume = 1.f;
}

void FSAudio::Play(const UObject* WorldContext, EFSSfx Sfx, const FVector& Loc, float Volume, float Pitch)
{
	Volume *= GFS_MasterVolume;
	if (Volume <= 0.001f)
	{
		return;
	}

	AFSAudioRig* Rig = GetRig(WorldContext);
	if (!Rig || !Rig->Synth)
	{
		return;
	}

	float Pan = 0.f;
	float Atten = 1.f;
	if (const UWorld* W = Rig->GetWorld())
	{
		if (const APlayerController* PC = W->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* CM = PC->PlayerCameraManager)
			{
				const FVector CamLoc = CM->GetCameraLocation();
				const FRotator CamRot = CM->GetCameraRotation();
				const FVector Right = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y);
				const FVector Dir = (Loc - CamLoc).GetSafeNormal();
				Pan = FVector::DotProduct(Dir, Right) * 0.8f;
				Atten = 1.f / (1.f + FVector::Dist(CamLoc, Loc) / 2600.f);
			}
		}
	}

	Rig->Synth->Push(Sfx, Volume * FMath::Clamp(Atten, 0.20f, 1.f), Pan, Pitch);
}

void FSAudio::Play2D(const UObject* WorldContext, EFSSfx Sfx, float Volume, float Pitch)
{
	Volume *= GFS_MasterVolume;
	if (Volume <= 0.001f)
	{
		return;
	}
	AFSAudioRig* Rig = GetRig(WorldContext);
	if (Rig && Rig->Synth)
	{
		Rig->Synth->Push(Sfx, Volume, 0.f, Pitch);
	}
}

void FSAudio::SetAmbience(const UObject* WorldContext, bool bOn, float Tension)
{
	AFSAudioRig* Rig = GetRig(WorldContext);
	if (Rig && Rig->Synth)
	{
		Rig->Synth->SetAmbience(bOn, Tension);
	}
}

void FSAudio::ToggleMute(const UObject* WorldContext)
{
	AFSAudioRig* Rig = GetRig(WorldContext);
	if (Rig && Rig->Synth)
	{
		Rig->Synth->SetMuted(!Rig->Synth->GetMuted());
	}
}

bool FSAudio::IsMuted(const UObject* WorldContext)
{
	AFSAudioRig* Rig = GetRig(WorldContext);
	return Rig && Rig->Synth ? Rig->Synth->GetMuted() : false;
}

void FSAudio::SetMasterVolume(const UObject* WorldContext, float InVolume)
{
	GFS_MasterVolume = FMath::Clamp(InVolume, 0.f, 1.f);

	// 立刻让环境底噪也跟着变，否则调音量时只能听到下一次音效
	if (AFSAudioRig* Rig = GetRig(WorldContext))
	{
		if (Rig->Synth)
		{
			Rig->Synth->SetAmbienceVolume(GFS_MasterVolume);
		}
	}
}

float FSAudio::GetMasterVolume(const UObject* WorldContext)
{
	return GFS_MasterVolume;
}
