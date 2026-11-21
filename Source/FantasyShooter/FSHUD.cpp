#include "FSHUD.h"
#include "FSUtil.h"
#include "FSGameMode.h"
#include "FSPlayer.h"
#include "FSEnemy.h"
#include "FSPickup.h"
#include "FSCommon.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

void AFSHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	SW = Canvas->SizeX;
	SH = Canvas->SizeY;
	const float U = FMath::Clamp(SH / 700.f, 0.75f, 3.6f);

	Font = FSUtil::UIFont();

	AFSGameMode* GM = AFSGameMode::Get(this);
	AFSPlayerChar* P = Cast<AFSPlayerChar>(GetOwningPawn());

	if (!GM)
	{
		return;
	}

	DrawVignette(U, GM->DamageFlash, P ? P->GetHealthPct() : 1.f,
		P && P->IsAlive() && GM->Phase == EFSPhase::Playing);

	if (GM->Phase == EFSPhase::Title)
	{
		DrawTitle(GM, U);
	}
	else
	{
		if (P)
		{
			DrawPlaying(GM, P, U);

			// 免死绿闪压在 HUD 之上：这一下要和"普通挨打"彻底区分开
			if (P->IsAlive() && GM->Phase == EFSPhase::Playing)
			{
				DrawSaveFlash(U, P->GetDeathSaveFlash());
			}
		}
		if (GM->Phase == EFSPhase::GameOver)
		{
			DrawGameOver(GM, U);
		}
	}

	// 赐福卡盖在战场之上，但暂停面板要压在最上面
	if (GM->IsChoosing() && !GM->bPaused)
	{
		DrawBlessing(GM, U);
	}

	// 暂停面板永远画在最上层（标题页也可以调出，方便退出）
	if (GM->bPaused)
	{
		DrawPauseMenu(GM, U);
	}
}

void AFSHUD::Txt(const FString& S, float X, float Y, float Scale, const FLinearColor& Color,
	bool bCenter, bool bShadow)
{
	if (!Font || S.IsEmpty())
	{
		return;
	}
	float W = 0.f, H = 0.f;
	GetTextSize(S, W, H, Font, Scale);
	const float DX = bCenter ? X - W * 0.5f : X;
	if (bShadow)
	{
		DrawText(S, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.55f), DX + 1.6f, Y + 2.0f, Font, Scale, false);
	}
	DrawText(S, Color, DX, Y, Font, Scale, false);
}

float AFSHUD::TxtW(const FString& S, float Scale)
{
	if (!Font)
	{
		return 0.f;
	}
	float W = 0.f, H = 0.f;
	GetTextSize(S, W, H, Font, Scale);
	return W;
}

void AFSHUD::Bar(float X, float Y, float W, float H, float Pct, const FLinearColor& Fill, const FLinearColor& Bg)
{
	DrawRect(Bg, X, Y, W, H);
	const float FW = W * FMath::Clamp(Pct, 0.f, 1.f);
	if (FW > 0.5f)
	{
		DrawRect(Fill, X, Y, FW, H);
	}
}

void AFSHUD::DrawVignette(float U, float Flash, float HpPct, bool bLowHpWarn)
{
	// 受击红边
	const float Edge = 26.f * U;
	if (Flash > 0.01f)
	{
		const FLinearColor Base(0.75f, 0.05f, 0.10f, 0.16f * Flash);
		DrawRect(Base, 0.f, 0.f, SW, Edge * 2.f);
		DrawRect(Base, 0.f, SH - Edge * 2.f, SW, Edge * 2.f);
		DrawRect(Base, 0.f, 0.f, Edge * 2.f, SH);
		DrawRect(Base, SW - Edge * 2.f, 0.f, Edge * 2.f, SH);
	}

	// 低血量：心跳式红边。一次性受伤闪只能提示"刚被打"，这个能持续提醒"快死了"
	if (bLowHpWarn && HpPct < 0.30f)
	{
		const float K = FMath::Clamp(1.f - HpPct / 0.30f, 0.f, 1.f);
		const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		const float Pulse = 0.55f + 0.45f * FMath::Sin(T * (6.f + 5.f * K));
		const float A = (0.08f + 0.14f * K) * Pulse;

		// 6 层由内向外递减，凑出柔和渐隐，整块纯色会显得很生硬
		const float Thick = 9.f * U;
		for (int32 i = 0; i < 6; ++i)
		{
			const float Off = i * Thick;
			const FLinearColor C(0.85f, 0.06f, 0.10f, A * (1.f - (float)i / 6.f));
			DrawRect(C, 0.f, Off, SW, Thick);
			DrawRect(C, 0.f, SH - Off - Thick, SW, Thick);
			DrawRect(C, Off, 0.f, Thick, SH);
			DrawRect(C, SW - Off - Thick, 0.f, Thick, SH);
		}
	}
}

void AFSHUD::DrawSaveFlash(float U, float A)
{
	if (A <= 0.01f)
	{
		return;
	}
	const FLinearColor C(0.32f, 1.00f, 0.55f);

	// 整屏极淡的绿雾：单独画边缘会像"又被打了一下",加一层全面罩才读得出"被护住了"
	DrawRect(FLinearColor(C.R, C.G, C.B, 0.085f * A), 0.f, 0.f, SW, SH);

	// 边缘由内向外递减，和红闪同构但更亮、更薄，绿和红的观感差别来自这里
	const float Thick = 11.f * U;
	for (int32 i = 0; i < 7; ++i)
	{
		const float Off = i * Thick;
		const FLinearColor K(C.R, C.G, C.B, 0.30f * A * (1.f - (float)i / 7.f));
		DrawRect(K, 0.f, Off, SW, Thick);
		DrawRect(K, 0.f, SH - Off - Thick, SW, Thick);
		DrawRect(K, Off, 0.f, Thick, SH);
		DrawRect(K, SW - Off - Thick, 0.f, Thick, SH);
	}
}

void AFSHUD::DrawSurgeArc(float CX, float CY, float Pct, float U)
{
	if (Pct <= 0.01f)
	{
		return;
	}

	// 圆环半径随剩余时间往外张：窗口刚打开时紧贴准星，快结束时甩到最外圈，
	// 玩家余光就能看出"还剩多久"，不用盯着数字。
	const float R = (17.f + 13.f * (1.f - Pct)) * U;
	const float Alpha = 0.45f + 0.50f * Pct;
	const FLinearColor C(0.35f, 0.92f, 0.90f, Alpha);

	// 从正上方顺时针画一段占 Pct 的弧；用连续线段拼，和雷达环同一套画法
	const int32 Seg = 32;
	const int32 Lit = FMath::Max(1, FMath::RoundToInt(Seg * Pct));
	const float Start = -PI * 0.5f;
	const float Step = (2.f * PI) / Seg;
	for (int32 i = 0; i < Lit; ++i)
	{
		const float A0 = Start + Step * i;
		const float A1 = A0 + Step;
		// 尾部淡出：那一小段快没了，视觉上自然收口
		const float K = (float)i / (float)Lit;
		DrawLine(CX + FMath::Cos(A0) * R, CY + FMath::Sin(A0) * R,
			CX + FMath::Cos(A1) * R, CY + FMath::Sin(A1) * R,
			FLinearColor(C.R, C.G, C.B, C.A * (0.45f + 0.55f * K)), 2.2f * U);
	}
}

void AFSHUD::DrawPlaying(AFSGameMode* GM, AFSPlayerChar* P, float U)
{
	const float M = 28.f * U;

	// ---------------- 左上：波次 / 击杀 / 金币
	Txt(FSUtil::L(TEXT("波次"), TEXT("WAVE")), M, M, 0.85f * U, FSPal::Dim);
	const FString WaveStr = FString::FromInt(GM->Wave);
	Txt(WaveStr, M, M + 16.f * U, 2.4f * U, FSPal::White);

	// 本波还剩多少：已出生的 + 还没刷出来的，免得玩家在空场里干等
	if (GM->bWaveActive)
	{
		const FString RemStr = FS_FMT(TEXT("剩余 %d"), TEXT("%d LEFT"), GM->GetWaveRemaining());
		Txt(RemStr, M + TxtW(WaveStr, 2.4f * U) + 12.f * U, M + 30.f * U, 0.9f * U,
			FLinearColor(0.72f, 0.78f, 0.88f));
	}

	const float InfoY = M + 78.f * U;
	Txt(FS_FMT(TEXT("击杀  %d"), TEXT("KILLS  %d"), GM->Kills), M, InfoY, 1.0f * U, FSPal::White);
	Txt(FS_FMT(TEXT("金币  %d"), TEXT("COINS  %d"), GM->Coins), M, InfoY + 22.f * U, 1.0f * U, FSPal::Gold);

	// ---------------- 等级 / 经验（金币既是分数也是经验）
	{
		const float LvY = InfoY + 50.f * U;
		const float Pop = 1.f + GM->LevelPulse * 0.18f;
		const FString LvStr = FString::Printf(TEXT("Lv.%d"), GM->GetLevel());
		const FLinearColor LvColor = GM->LevelPulse > 0.02f
			? FSUtil::Lerp(FSPal::Cyan, FLinearColor(1.f, 1.f, 1.f), GM->LevelPulse)
			: FSPal::Cyan;
		Txt(LvStr, M, LvY - 2.f * U, 1.05f * U * Pop, LvColor);

		const float LvW = TxtW(LvStr, 1.05f * U * Pop);
		const float EX = M + LvW + 16.f * U;
		const float EW = 104.f * U;
		Bar(EX, LvY + 4.f * U, EW, 6.f * U, GM->GetLevelPct(), FSPal::Gold,
			FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Txt(FS_FMT(TEXT("%d/%d"), TEXT("%d/%d"), GM->GetXp(), GM->GetXpNeed()),
			EX + EW + 10.f * U, LvY + 1.f * U, 0.7f * U, FLinearColor(0.55f, 0.60f, 0.70f));
	}

	Txt(FSUtil::L(TEXT("ESC 暂停 / 设置"), TEXT("ESC  PAUSE / SETTINGS")), M, InfoY + 78.f * U,
		0.72f * U, FLinearColor(0.45f, 0.50f, 0.58f));

	// ---------------- 右上：天气 / 最佳
	const TCHAR* WeatherName = FSUtil::L(FSDefs::WeatherNameCN(GM->Weather), FSDefs::WeatherNameEN(GM->Weather));
	FLinearColor WeatherColor = FSPal::Dim;
	switch (GM->Weather)
	{
	case EFSWeather::Clear:     WeatherColor = FLinearColor(0.95f, 0.92f, 0.80f); break;
	case EFSWeather::Rain:      WeatherColor = FLinearColor(0.55f, 0.75f, 1.00f); break;
	case EFSWeather::Storm:     WeatherColor = FLinearColor(0.60f, 0.70f, 1.00f); break;
	case EFSWeather::BloodMoon: WeatherColor = FLinearColor(1.00f, 0.36f, 0.34f); break;
	}
	Txt(WeatherName, SW - M, M, 1.15f * U, WeatherColor, false);
	const FString BestStr = FS_FMT(TEXT("最佳 %d 波"), TEXT("BEST WAVE %d"), GM->BestWave);
	Txt(BestStr, SW - M - TxtW(BestStr, 0.9f * U), M + 26.f * U, 0.9f * U, FSPal::Dim);

	// ---------------- 连击：连续击杀才显示，平时不占视线
	if (GM->Combo >= 2)
	{
		const int32 C = GM->Combo;
		FLinearColor CC = FSPal::Cyan;
		if (C >= 20)      { CC = FLinearColor(1.00f, 0.42f, 0.24f); }
		else if (C >= 10) { CC = FSPal::Gold; }

		// 刚击杀时数字轻微放大一下，给出打击反馈
		const float Pop = 1.f + GM->ComboPulse * 0.22f;
		const FString ComboStr = FString::Printf(TEXT("x%d"), C);
		const float Cy = M + 56.f * U;
		const float CW = TxtW(ComboStr, 2.0f * U * Pop);
		Txt(ComboStr, SW - M - CW, Cy, 2.0f * U * Pop, CC);
		const FString CLabel = FSUtil::L(TEXT("连 击"), TEXT("COMBO"));
		Txt(CLabel, SW - M - CW - TxtW(CLabel, 0.85f * U) - 8.f * U, Cy + 14.f * U, 0.85f * U,
			FLinearColor(CC.R, CC.G, CC.B, 0.85f));

		// 剩余连击时间，掉光即清零
		const float Pct = FMath::Clamp(GM->ComboTimer / AFSGameMode::ComboWindow, 0.f, 1.f);
		const float CW2 = 96.f * U;
		Bar(SW - M - CW2, Cy + 36.f * U, CW2, 4.f * U, Pct, CC, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	}

	// ---------------- BOSS 血条
	AFSEnemy* Boss = nullptr;
	for (const TWeakObjectPtr<AFSEnemy>& W : GM->Enemies)
	{
		AFSEnemy* E = W.Get();
		if (E && !E->IsDead() && E->IsBoss())
		{
			Boss = E;
			break;
		}
	}
	if (Boss)
	{
		const float BW = SW * 0.44f;
		const float BH = 13.f * U;
		const float BX = (SW - BW) * 0.5f;
		const float BY = SH * 0.062f;
		const int32 Ph = FMath::Clamp(Boss->GetBossPhase(), 1, 3);
		const bool bShift = Boss->IsPhaseShifting();
		const bool bWarlord = (Boss->GetKind() == EFSEnemyKind::Warlord);

		// 阶段配色：越往后越刺眼。
		// 两种 BOSS 各用一套色相，玩家扫一眼血槽就知道现在打的是谁。
		FLinearColor Fill(0.95f, 0.22f, 0.35f);
		if (bWarlord)
		{
			// 铁灰 → 熔橙 → 白热
			Fill = (Ph == 1) ? FLinearColor(0.72f, 0.44f, 0.26f)
				: ((Ph == 2) ? FLinearColor(1.00f, 0.45f, 0.12f) : FLinearColor(1.00f, 0.78f, 0.40f));
		}
		else
		{
			if (Ph == 2) { Fill = FLinearColor(1.00f, 0.44f, 0.18f); }
			else if (Ph >= 3) { Fill = FLinearColor(1.00f, 0.17f, 0.62f); }
		}

		const FFSEnemyDef& BD = FSDefs::Enemy(Boss->GetKind());
		const FLinearColor NameCol = bWarlord ? FLinearColor(1.f, 0.72f, 0.42f)
			: FLinearColor(1.f, 0.55f, 0.55f);
		Txt(FSUtil::L(BD.NameCN, BD.NameEN), SW * 0.5f, BY - 26.f * U, 1.05f * U, NameCol, true);

		Bar(BX, BY, BW, BH, Boss->GetHealthPct(), Fill, FLinearColor(0.f, 0.f, 0.f, 0.58f));
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.12f), BX, BY, BW, 2.f * U);

		// 两道阶段分界：越过它就会变身，提前让玩家心里有数
		// 分界要压在空血槽上也看得见，所以用"暗底 + 亮线"两层，不能只画一条黑线
		for (int32 i = 1; i <= 2; ++i)
		{
			const float SX = BX + BW * AFSEnemy::PhaseGate(i);
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.75f), SX - 1.5f * U, BY - 2.f * U, 3.f * U, BH + 4.f * U);
			DrawRect(FLinearColor(1.f, 0.88f, 0.55f, 0.75f), SX - 0.5f * U, BY - 2.f * U, 1.f * U, BH + 4.f * U);
		}

		// 右侧阶段点：当前阶段亮，已过的阶段留个暗印
		for (int32 i = 1; i <= 3; ++i)
		{
			const float PX = BX + BW + 14.f * U + (i - 1) * 15.f * U;
			const bool bOn = (i == Ph);
			const bool bPast = (i < Ph);
			const float S = 7.f * U;
			const FLinearColor C = bOn ? FLinearColor(1.f, 1.f, 1.f, 0.95f)
				: (bPast ? FLinearColor(Fill.R, Fill.G, Fill.B, 0.35f) : FLinearColor(0.30f, 0.32f, 0.38f, 0.55f));
			DrawRect(C, PX - S * 0.5f, BY + BH * 0.5f - S * 0.5f, S, S);
		}

		// ---- 正面甲：君王这条是"能不能打穿"的进度，不是血量。
		// 打穿之后正面全伤 + 易伤，所以它比血条更值得盯——这是玩家决定"继续硬打还是绕背"的依据。
		if (Boss->HasArmor())
		{
			const float AW = BW * 0.58f;
			const float AH = 6.f * U;
			const float AX = (SW - AW) * 0.5f;
			const float AY = BY + BH + 11.f * U;
			const bool bBroken = Boss->IsArmorBroken();
			const float APct = Boss->GetArmorPct();

			// 做成 8 段"甲片"而不是一根连续条：血条是连续的，甲是分片的，
			// 视觉上一眼就能分清"这条是结构、上面那条是命"。
			const int32 Segs = 8;
			const float Gap = 4.f * U;
			const float SegW = (AW - Gap * (Segs - 1)) / Segs;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), AX - Gap, AY - Gap * 0.5f,
				AW + Gap * 2.f, AH + Gap);
			for (int32 i = 0; i < Segs; ++i)
			{
				const float SX = AX + i * (SegW + Gap);
				const bool bOn = APct > (float)i / (float)Segs + 0.001f;
				const FLinearColor C = bOn
					? FLinearColor(1.00f, 0.62f, 0.20f, 0.98f)
					: FLinearColor(0.24f, 0.12f, 0.06f, 0.75f);
				DrawRect(C, SX, AY, SegW, AH);
				if (bOn)
				{
					DrawRect(FLinearColor(1.f, 0.88f, 0.60f, 0.60f), SX, AY, SegW, 1.f);
				}
			}

			if (bBroken)
			{
				Txt(FSUtil::L(TEXT("甲 片 崩 落 · 正 面 全 伤"), TEXT("ARMOR BROKEN - FULL DAMAGE")),
					SW * 0.5f, AY + 8.f * U, 0.78f * U, FLinearColor(1.f, 0.72f, 0.34f, 1.f), true);
			}
			else
			{
				Txt(FSUtil::L(TEXT("甲"), TEXT("ARM")), AX - 12.f * U, AY - 2.f * U, 0.70f * U,
					FLinearColor(1.f, 0.80f, 0.45f, 0.90f), true);
			}
		}

		// 阶段切换：整条血条过一遍白光，配合"护盾"字样，明确告诉玩家现在打不动
		if (bShift)
		{
			const float K = Boss->GetPhaseShiftPct();       // 1 → 0
			const float Sway = FMath::Sin(K * PI);
			DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.10f + 0.18f * Sway), BX, BY, BW, BH);
			const float ShiftY = BY + BH + (Boss->HasArmor() ? 26.f : 8.f) * U;
			Txt(FSUtil::L(TEXT("护 盾 展 开"), TEXT("SHIELD UP")), SW * 0.5f, ShiftY,
				0.95f * U, FLinearColor(1.f, 0.92f, 0.55f, 0.55f + 0.45f * Sway), true);
		}
	}

	// ---------------- 左下：生命 / 闪避 / 守护兽
	const float BarW = 268.f * U;
	const float BarH = 20.f * U;
	const float BX = M;
	const float BY = SH - M - BarH - 44.f * U;

	// ---------------- 左下（生命条上方）：流派路线
	// 选卡界面里的三条进度条只在挑卡时看得到，挑完就"忘了自己走哪条路"。
	// 这里常驻一条紧凑版：颜色即流派，三点即进度，觉醒后直接写被动名。
	{
		const float ChipH = 18.f * U;
		const float ChipY = BY - 48.f * U;
		float CX = M;

		for (int32 pi = 0; pi < (int32)EFSPath::Count; ++pi)
		{
			const EFSPath Pth = (EFSPath)pi;
			const FLinearColor PC = FSDefs::PathColor(Pth);
			const int32 St = P->GetPathStacks(Pth);
			const bool bAwe = St >= FSDefs::PathAwakenStacks;
			const float Pulse = P->GetPathPulse(Pth);

			const FString Name = FSUtil::L(FSDefs::PathShortCN(Pth), FSDefs::PathShortEN(Pth));
			const float NameW = TxtW(Name, 0.72f * U);

			// 觉醒后把点数换成被动名——"狂怒/残影/不屈"才是真收益，
			// 三个点只能说明"到了"，说不出"有什么用"
			// 溢出层数接在被动名后面（+2 / +5）：这是"觉醒后还在涨"的唯一可见证据
			const int32 Ovf = bAwe ? P->GetOverflowStacks(Pth) : 0;
			FString Tail = bAwe
				? FSUtil::L(FSDefs::PathAwakenNameCN(Pth), FSDefs::PathAwakenNameEN(Pth))
				: FString();
			if (bAwe && Ovf > 0)
			{
				Tail += FString::Printf(TEXT(" +%d"), Ovf);
			}
			const float TailW = bAwe ? TxtW(Tail, 0.66f * U) : 0.f;
			const float PipsW = bAwe ? 0.f : (3.f * 6.f + 2.f * 3.f) * U;

			// 不屈有 45 秒冷却，玩家必须知道"现在还能不能免死"——
			// 不然免死会变成"随缘"，冷却期间被摸死只会觉得系统没生效。
			const bool bWardChip = (Pth == EFSPath::Ward);
			const float SaveLeft = bWardChip ? P->GetDeathSaveLeft() : 0.f;
			const bool bCooling = bAwe && bWardChip && SaveLeft > 0.05f;
			const FString CoolStr = bCooling
				? FString::Printf(TEXT("%ds"), FMath::CeilToInt(SaveLeft))
				: FString();
			// 冷却中写秒数，就绪时只留一个小绿点（"不屈 就绪"太占宽度）
			const float CoolW = (bAwe && bWardChip) ? (bCooling ? 4.f * U + TxtW(CoolStr, 0.62f * U) : 8.f * U) : 0.f;

			const float ChipW = 3.f * U + 5.f * U + NameW + 5.f * U
				+ (bAwe ? TailW : PipsW) + CoolW + 6.f * U;

			// 刚选过这条流派：整块亮一下再褪，把"我选了哪条"和"它涨了"连起来
			if (Pulse > 0.01f)
			{
				DrawRect(FLinearColor(PC.R, PC.G, PC.B, 0.20f * Pulse),
					CX - 3.f * U, ChipY - 3.f * U, ChipW + 6.f * U, ChipH + 6.f * U);
			}

			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.42f), CX, ChipY, ChipW, ChipH);
			if (bAwe)
			{
				DrawRect(FLinearColor(PC.R, PC.G, PC.B, 0.14f + 0.28f * Pulse), CX, ChipY, ChipW, ChipH);
			}
			DrawRect(FLinearColor(PC.R, PC.G, PC.B, bAwe ? 0.95f : (St > 0 ? 0.85f : 0.40f)),
				CX, ChipY, 2.5f * U, ChipH);

			Txt(Name, CX + 8.f * U, ChipY + 3.f * U, 0.72f * U,
				FLinearColor(PC.R * 0.55f + 0.45f, PC.G * 0.55f + 0.45f, PC.B * 0.55f + 0.45f,
					bAwe ? 1.f : 0.86f));

			float PX = CX + 8.f * U + NameW + 5.f * U;
			if (bAwe)
			{
				// 冷却中的"不屈"压成灰蓝，一眼就能看出"这个被动现在不顶用"
				Txt(Tail, PX, ChipY + 4.f * U, 0.66f * U,
					bCooling ? FLinearColor(0.56f, 0.62f, 0.72f, 0.80f)
					         : FLinearColor(1.f, 0.93f, 0.70f, 0.95f));
				PX += TailW;

				if (bWardChip)
				{
					PX += 4.f * U;
					if (bCooling)
					{
						Txt(CoolStr, PX, ChipY + 4.5f * U, 0.62f * U,
							FLinearColor(0.60f, 0.66f, 0.75f, 0.92f));
					}
					else
					{
						// 就绪：一个小绿点 + 轻微呼吸，提醒"这口免死已经攒回来了"
						const float K = 0.5f + 0.5f * FMath::Sin(
							(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) * 4.f);
						const float S = 5.f * U;
						DrawRect(FLinearColor(0.42f, 1.00f, 0.56f, 0.55f + 0.45f * K),
							PX, ChipY + ChipH * 0.5f - S * 0.5f, S, S);
					}
				}
			}
			else
			{
				for (int32 k = 0; k < FSDefs::PathAwakenStacks; ++k)
				{
					const bool bOn = k < St;
					DrawRect(bOn ? FLinearColor(PC.R, PC.G, PC.B, 0.95f)
						: FLinearColor(0.42f, 0.48f, 0.58f, 0.40f),
						PX, ChipY + ChipH * 0.5f - 3.f * U, 6.f * U, 6.f * U);
					PX += 9.f * U;
				}
			}

			CX += ChipW + 8.f * U;
		}
	}

	const float HpPct = P->GetHealthPct();
	const FLinearColor HpColor = FSUtil::Lerp(FLinearColor(0.95f, 0.20f, 0.22f), FLinearColor(0.30f, 0.92f, 0.45f), HpPct);
	Txt(FS_FMT(TEXT("生命  %d"), TEXT("HP  %d"), FMath::RoundToInt(P->GetHealthPct() * 100.f)),
		BX, BY - 24.f * U, 0.95f * U, FSPal::White);
	Bar(BX, BY, BarW, BarH, HpPct, HpColor, FLinearColor(0.f, 0.f, 0.f, 0.55f));
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.12f), BX, BY, BarW, 2.f * U);

	// 闪避冷却
	const float DashPct = 1.f - P->GetDashCdPct();
	const float DY = BY + BarH + 8.f * U;
	Bar(BX, DY, BarW * 0.62f, 7.f * U, DashPct,
		DashPct >= 1.f ? FSPal::Cyan : FLinearColor(0.30f, 0.45f, 0.60f), FLinearColor(0.f, 0.f, 0.f, 0.45f));
	Txt(FSUtil::L(TEXT("闪避"), TEXT("DASH")), BX + BarW * 0.62f + 8.f * U, DY - 5.f * U, 0.8f * U,
		DashPct >= 1.f ? FSPal::Cyan : FSPal::Dim);

	// 守护兽剩余时间
	if (GM->GuardianLeft > 0.f)
	{
		const float GY = DY + 16.f * U;
		Bar(BX, GY, BarW * 0.62f, 7.f * U, GM->GuardianLeft / 22.f,
			FSPal::Magic, FLinearColor(0.f, 0.f, 0.f, 0.45f));
		Txt(FSUtil::L(TEXT("守护兽"), TEXT("GUARDIAN")), BX + BarW * 0.62f + 8.f * U, GY - 5.f * U,
			0.8f * U, FSPal::Magic);
	}

	// ---------------- 右下：武器
	const FFSWeaponDef& WD = FSDefs::Weapon(P->GetWeapon());
	const FString WeaponName = FSUtil::L(WD.NameCN, WD.NameEN);
	const FString LvStr = FString::Printf(TEXT("Lv.%d"), P->GetWeaponLevel());
	// 无限弹药显示为 ∞（用 UCN 转义，避免源码编码问题）
	const FString AmmoStr = (WD.AmmoMax > 0) ? FString::FromInt(P->GetAmmo()) : FString(TEXT("\u221E"));

	const float RX = SW - M;
	Txt(AmmoStr, RX - TxtW(AmmoStr, 2.2f * U), SH - M - 40.f * U, 2.2f * U,
		WD.AmmoMax > 0 && P->GetAmmo() < 10 ? FSPal::Danger : FSPal::White);
	Txt(WeaponName, RX - TxtW(WeaponName, 1.05f * U), SH - M - 62.f * U, 1.05f * U, WD.Color);
	Txt(LvStr, RX - TxtW(WeaponName, 1.05f * U) - TxtW(LvStr, 0.85f * U) - 8.f * U,
		SH - M - 60.f * U, 0.85f * U, FSPal::Gold);

	// ---------------- 底部中间：符石
	const float RuneY = SH - M - 22.f * U;
	for (int32 i = 0; i < 3; ++i)
	{
		const float CX = SW * 0.5f + (i - 1) * 34.f * U;
		const bool bOn = i < GM->RuneCount;
		const float S = 13.f * U;
		DrawRect(bOn ? FSPal::Magic : FLinearColor(0.25f, 0.28f, 0.36f, 0.75f),
			CX - S * 0.5f, RuneY - S * 0.5f, S, S);
		if (bOn)
		{
			DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.75f), CX - S * 0.16f, RuneY - S * 0.16f, S * 0.32f, S * 0.32f);
		}
	}
	Txt(FSUtil::L(TEXT("符石"), TEXT("RUNES")), SW * 0.5f, RuneY - 32.f * U, 0.8f * U, FSPal::Dim, true);

	// ---------------- 准星（暂停时不画，免得和菜单抢视线）
	float MX = 0.f, MY = 0.f;
	if (!GM->bPaused && PlayerOwner && PlayerOwner->GetMousePosition(MX, MY))
	{
		// 残影窗口里准星换青色并往外张开一圈：射速 +35% 只写在注释里没人知道，
		// 玩家能察觉的只有"准星变了个样、子弹变粗了"。
		const float Surge = P->IsAlive() ? P->GetSurgePct() : 0.f;
		const float G = (5.f + 2.5f * Surge) * U;
		const float L = (9.f + 3.5f * Surge) * U;
		const FLinearColor CC = !P->IsAlive() ? FSPal::Dim
			: (Surge > 0.01f ? FLinearColor(0.42f, 0.98f, 0.96f, 0.92f)
			                 : FLinearColor(1.f, 1.f, 1.f, 0.85f));
		DrawLine(MX - G - L, MY, MX - G, MY, CC, 1.6f * U);
		DrawLine(MX + G, MY, MX + G + L, MY, CC, 1.6f * U);
		DrawLine(MX, MY - G - L, MX, MY - G, CC, 1.6f * U);
		DrawLine(MX, MY + G, MX, MY + G + L, CC, 1.6f * U);
		DrawRect(CC, MX - 1.f * U, MY - 1.f * U, 2.f * U, 2.f * U);

		if (Surge > 0.01f)
		{
			DrawSurgeArc(MX, MY, Surge, U);
		}
	}

	// ---------------- 拾取/升级提示（角色身上的短提示，飘在准星下方居中）
	{
		const float NA = P->GetNoticeAlpha();
		if (NA > 0.01f)
		{
			Txt(P->GetNotice(), SW * 0.5f, SH * 0.64f, 1.25f * U,
				FLinearColor(1.f, 1.f, 1.f, NA), true);
		}
	}

	// ---------------- 战场雷达 + 贴身威胁提示
	DrawRadar(GM, P, U);
	DrawThreatArrows(GM, P, U);

	// ---------------- 中央横幅
	if (GM->BannerTime > 0.f && !GM->BannerText.IsEmpty())
	{
		const float Alpha = GM->BannerTime > 5.f ? 1.f : FMath::Clamp(GM->BannerTime * 1.6f, 0.f, 1.f);
		const float BY2 = SH * 0.30f;

		// 横幅会压在爆散特效上，纯描边在亮团里根本看不清，所以垫一条暗底
		{
			float TextW = TxtW(GM->BannerText, 2.6f * U);
			if (!GM->BannerSub.IsEmpty())
			{
				TextW = FMath::Max(TextW, TxtW(GM->BannerSub, 1.0f * U));
			}
			const float BW2 = TextW + 64.f * U;
			const float BH2 = GM->BannerSub.IsEmpty() ? 54.f * U : 86.f * U;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.42f * Alpha), SW * 0.5f - BW2 * 0.5f, BY2 - 12.f * U, BW2, BH2);
		}

		Txt(GM->BannerText, SW * 0.5f, BY2, 2.6f * U, FLinearColor(1.f, 1.f, 1.f, Alpha), true);
		if (!GM->BannerSub.IsEmpty())
		{
			Txt(GM->BannerSub, SW * 0.5f, BY2 + 52.f * U, 1.0f * U, FLinearColor(0.78f, 0.83f, 0.94f, Alpha), true);
		}
	}
}

void AFSHUD::DrawTitle(AFSGameMode* GM, float U)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.01f, 0.62f), 0.f, 0.f, SW, SH);

	const float CX = SW * 0.5f;
	Txt(FSUtil::L(TEXT("奇  幻  射  手"), TEXT("FANTASY  SHOOTER")), CX, SH * 0.17f, 3.6f * U,
		FLinearColor(1.f, 0.98f, 0.92f), true);
	Txt(FSUtil::L(TEXT("俯 视 群 怪 射 击  ·  复 刻 版"), TEXT("TOP-DOWN HORDE SHOOTER  -  REMAKE")),
		CX, SH * 0.17f + 76.f * U, 1.1f * U, FSPal::Cyan, true);

	// 本局场景
	const FString MapName = GM->GetMapName();
	if (!MapName.IsEmpty())
	{
		Txt(FS_FMT(TEXT("场 景  ·  %s"), TEXT("ARENA  -  %s"), *MapName),
			CX, SH * 0.17f + 108.f * U, 1.0f * U, FSPal::Dim, true);
	}

	// 操作说明：两条横排，避开画面中央的角色
	const float RowY = SH * 0.575f;
	Txt(FSUtil::L(TEXT("键鼠    WASD 移动   ·   鼠标 瞄准   ·   左键 连射   ·   空格 闪避"),
		TEXT("KEYBOARD    WASD MOVE  ·  MOUSE AIM  ·  LMB FIRE  ·  SPACE DASH")),
		CX, RowY, 0.95f * U, FSPal::White, true);
	Txt(FSUtil::L(TEXT("手柄    左摇杆 移动   ·   右摇杆 瞄准   ·   RT 射击   ·   A 闪避"),
		TEXT("GAMEPAD    L-STICK MOVE  ·  R-STICK AIM  ·  RT FIRE  ·  A DASH")),
		CX, RowY + 30.f * U, 0.95f * U, FSPal::White, true);
	Txt(FSUtil::L(TEXT("集齐 3 颗符石召唤守护兽        M 静音        ESC 暂停"),
		TEXT("COLLECT 3 RUNES TO SUMMON A GUARDIAN        M = MUTE        ESC = PAUSE")),
		CX, RowY + 66.f * U, 0.9f * U, FSPal::Gold, true);
	Txt(FSUtil::L(TEXT("金币也会累积等级 - 升级随机强化 伤害 / 射速 / 移速 / 生命 / 闪避"),
		TEXT("COINS ALSO GRANT LEVELS - UPGRADES BOOST DAMAGE / RATE / SPEED / HP / DASH")),
		CX, RowY + 92.f * U, 0.82f * U, FSPal::Dim, true);

	const float Pulse = 0.65f + 0.35f * FMath::Sin(GetWorld()->GetTimeSeconds() * 3.4f);
	Txt(FSUtil::L(TEXT("点击 左键 / 按 RT 开始"), TEXT("CLICK OR PRESS RT TO START")), CX, SH * 0.775f, 1.9f * U,
		FLinearColor(1.f, 1.f, 1.f, Pulse), true);

	Txt(FS_FMT(TEXT("历史最佳：%d 波    累计击杀 %d    最高连击 x%d"), TEXT("BEST: WAVE %d    TOTAL KILLS %d    BEST COMBO x%d"),
		GM->BestWave, GM->TotalKills, GM->BestCombo), CX, SH * 0.86f, 1.0f * U, FSPal::Dim, true);
}

void AFSHUD::DrawGameOver(AFSGameMode* GM, float U)
{
	DrawRect(FLinearColor(0.02f, 0.005f, 0.01f, 0.72f), 0.f, 0.f, SW, SH);

	const float CX = SW * 0.5f;
	Txt(FSUtil::L(TEXT("你 倒 下 了"), TEXT("YOU DIED")), CX, SH * 0.24f, 3.0f * U,
		FLinearColor(0.95f, 0.25f, 0.28f), true);

	Txt(FS_FMT(TEXT("存活波次    %d"), TEXT("WAVE REACHED   %d"), GM->Wave),
		CX, SH * 0.40f, 1.3f * U, FSPal::White, true);
	Txt(FS_FMT(TEXT("击杀数量    %d"), TEXT("KILLS          %d"), GM->Kills),
		CX, SH * 0.40f + 34.f * U, 1.3f * U, FSPal::White, true);
	Txt(FS_FMT(TEXT("获得金币    %d"), TEXT("COINS          %d"), GM->Coins),
		CX, SH * 0.40f + 68.f * U, 1.3f * U, FSPal::Gold, true);
	Txt(FS_FMT(TEXT("存活时长    %.1f 秒"), TEXT("TIME           %.1f s"), GM->RunTime),
		CX, SH * 0.40f + 102.f * U, 1.3f * U, FSPal::Dim, true);
	Txt(FS_FMT(TEXT("最高连击    x%d"), TEXT("BEST COMBO     x%d"), GM->RunBestCombo),
		CX, SH * 0.40f + 136.f * U, 1.3f * U, FSPal::Cyan, true);
	Txt(FS_FMT(TEXT("最终等级    Lv.%d"), TEXT("LEVEL          %d"), GM->GetLevel()),
		CX, SH * 0.40f + 170.f * U, 1.3f * U, FSPal::Gold, true);

	const float Pulse = 0.6f + 0.4f * FMath::Sin(GetWorld()->GetTimeSeconds() * 3.2f);
	Txt(FSUtil::L(TEXT("按 R 或 点击左键 重新开始"), TEXT("PRESS R OR CLICK TO RETRY")), CX, SH * 0.70f,
		1.4f * U, FLinearColor(1.f, 1.f, 1.f, Pulse), true);

	Txt(FSUtil::L(TEXT("ESC 暂停 / 退出"), TEXT("ESC = PAUSE / QUIT")), CX, SH * 0.94f,
		1.0f * U, FSPal::Dim, true);
}

void AFSHUD::DrawPauseMenu(AFSGameMode* GM, float U)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.012f, 0.82f), 0.f, 0.f, SW, SH);

	const int32 Count = AFSGameMode::MenuItemCount;
	const float Top = FSUtil::MenuItemRect(SW, SH, 0, Count).Y;

	Txt(FSUtil::L(TEXT("已  暂  停"), TEXT("PAUSED")), SW * 0.5f, Top - 86.f * U, 2.6f * U,
		FLinearColor(1.f, 1.f, 1.f), true);
	Txt(FSUtil::L(TEXT("方向键 选择   ·   回车 确认   ·   也可以直接用鼠标点"),
		TEXT("ARROWS TO SELECT   ·   ENTER TO CONFIRM   ·   OR CLICK")),
		SW * 0.5f, Top - 34.f * U, 0.85f * U, FSPal::Dim, true);

	// 鼠标悬停项（鼠标用户不需要先按方向键）
	int32 Hover = -1;
	float MX = 0.f, MY = 0.f;
	if (PlayerOwner && PlayerOwner->GetMousePosition(MX, MY))
	{
		for (int32 i = 0; i < Count; ++i)
		{
			if (FSUtil::MenuItemRect(SW, SH, i, Count).Contains(MX, MY))
			{
				Hover = i;
				break;
			}
		}
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const FFSRect R = FSUtil::MenuItemRect(SW, SH, i, Count);
		const float X = R.X;
		const float Y = R.Y;
		const float W = R.W;
		const float H = R.H;

		const bool bSel = (i == GM->MenuIndex) || (i == Hover);
		const bool bQuit = (i == AFSGameMode::MenuItemCount - 1);

		FLinearColor Bg = bSel ? FLinearColor(0.052f, 0.130f, 0.200f, 0.94f)
			: FLinearColor(0.028f, 0.045f, 0.075f, 0.74f);
		if (bSel && bQuit)
		{
			Bg = FLinearColor(0.210f, 0.048f, 0.068f, 0.94f);
		}
		DrawRect(Bg, X, Y, W, H);

		const FLinearColor Edge = bSel
			? (bQuit ? FLinearColor(0.95f, 0.32f, 0.34f) : FSPal::Cyan)
			: FLinearColor(0.14f, 0.18f, 0.24f, 0.9f);
		const float T = bSel ? 2.0f * U : 1.0f * U;
		DrawRect(Edge, X, Y, W, T);
		DrawRect(Edge, X, Y + H - T, W, T);
		DrawRect(Edge, X, Y, T, H);
		DrawRect(Edge, X + W - T, Y, T, H);

		const FString Label = GM->GetMenuItemLabel(i);
		const float Scale = 1.05f * U;
		float TW = 0.f, TH = 0.f;
		if (Font)
		{
			GetTextSize(Label, TW, TH, Font, Scale);
		}
		const FLinearColor TextColor = bSel
			? FLinearColor(1.f, 1.f, 1.f)
			: FLinearColor(0.72f, 0.78f, 0.86f);
		Txt(Label, X + W * 0.5f, Y + (H - TH) * 0.5f, Scale, TextColor, true);

		if (bSel)
		{
			DrawRect(TextColor, X + 14.f * U, Y + H * 0.5f - 3.f * U, 9.f * U, 6.f * U);
		}
	}
}

// ------------------------------------------------------------------ 清波赐福卡

void AFSHUD::DrawUpgradeIcon(int32 Kind, float CX, float CY, float S, const FLinearColor& Col)
{
	// 图标全是现画的线段/矩形，工程里不需要任何贴图资源
	const float T = FMath::Max(1.5f, S * 0.085f);

	auto Line = [&](float X1, float Y1, float X2, float Y2)
	{
		DrawLine(CX + X1 * S, CY + Y1 * S, CX + X2 * S, CY + Y2 * S, Col, T);
	};
	auto Box = [&](float X, float Y, float BW, float BH)
	{
		DrawRect(Col, CX + (X - BW * 0.5f) * S, CY + (Y - BH * 0.5f) * S, BW * S, BH * S);
	};

	switch ((EFSUpgrade)Kind)
	{
	case EFSUpgrade::Damage:      // 剑：刃 / 护手 / 柄 / 柄头
		Line(-0.10f, -0.14f, 0.f, -0.68f);
		Line(0.10f, -0.14f, 0.f, -0.68f);
		Line(-0.10f, -0.14f, 0.10f, -0.14f);
		Line(-0.40f, -0.06f, 0.40f, -0.06f);
		Line(0.f, -0.06f, 0.f, 0.36f);
		Box(0.f, 0.45f, 0.20f, 0.15f);
		break;

	case EFSUpgrade::FireRate:    // 三道速度线
		for (int32 i = -1; i <= 1; ++i)
		{
			const float Y = i * 0.34f;
			const float EndX = (i == 0) ? 0.36f : 0.12f;
			Line(-0.55f, Y, EndX, Y);
			Line(EndX - 0.15f, Y - 0.11f, EndX, Y);
			Line(EndX, Y, EndX - 0.15f, Y + 0.11f);
		}
		break;

	case EFSUpgrade::MoveSpeed:   // 双箭头
		Line(-0.52f, -0.42f, -0.12f, 0.f);
		Line(-0.12f, 0.f, -0.52f, 0.42f);
		Line(0.04f, -0.42f, 0.44f, 0.f);
		Line(0.44f, 0.f, 0.04f, 0.42f);
		break;

	case EFSUpgrade::MaxHP:       // 十字
		Box(0.f, 0.f, 0.26f, 0.98f);
		Box(0.f, 0.f, 0.98f, 0.26f);
		break;

	case EFSUpgrade::DashCd:      // 冷却环 + 中心点
		DrawRing(CX, CY, 0.48f * S, Col, T);
		Box(0.f, 0.f, 0.20f, 0.20f);
		break;

	case EFSUpgrade::Pierce:      // 子弹穿过两层甲片
		Line(-0.58f, 0.f, 0.46f, 0.f);
		Line(0.30f, -0.13f, 0.50f, 0.f);
		Line(0.50f, 0.f, 0.30f, 0.13f);
		Line(-0.24f, -0.52f, -0.24f, 0.52f);
		Line(0.14f, -0.52f, 0.14f, 0.52f);
		break;

	case EFSUpgrade::Crit:        // 四角星
		Line(0.f, -0.62f, 0.f, 0.62f);
		Line(-0.62f, 0.f, 0.62f, 0.f);
		Line(-0.30f, -0.30f, 0.30f, 0.30f);
		Line(-0.30f, 0.30f, 0.30f, -0.30f);
		break;

	case EFSUpgrade::Magnet:      // U 形磁铁
		Line(-0.40f, -0.52f, -0.40f, 0.10f);
		Line(0.40f, -0.52f, 0.40f, 0.10f);
		for (int32 i = 0; i < 10; ++i)
		{
			const float A0 = PI * (float)i / 10.f;
			const float A1 = PI * (float)(i + 1) / 10.f;
			DrawLine(CX + FMath::Cos(A0) * 0.40f * S, CY + 0.10f * S + FMath::Sin(A0) * 0.40f * S,
				CX + FMath::Cos(A1) * 0.40f * S, CY + 0.10f * S + FMath::Sin(A1) * 0.40f * S, Col, T);
		}
		Box(-0.40f, -0.56f, 0.24f, 0.16f);
		Box(0.40f, -0.56f, 0.24f, 0.16f);
		break;

	case EFSUpgrade::Ward:        // 盾牌：外框 + 中间竖脊
		Line(-0.50f, -0.52f, 0.50f, -0.52f);
		Line(-0.50f, -0.52f, -0.50f, 0.06f);
		Line(0.50f, -0.52f, 0.50f, 0.06f);
		Line(-0.50f, 0.06f, 0.f, 0.58f);
		Line(0.50f, 0.06f, 0.f, 0.58f);
		Line(0.f, -0.34f, 0.f, 0.34f);
		Line(-0.24f, -0.10f, 0.24f, -0.10f);
		break;

	case EFSUpgrade::Leech:       // 血滴：上尖 + 下半圆
		Line(0.f, -0.68f, -0.34f, -0.02f);
		Line(0.f, -0.68f, 0.34f, -0.02f);
		for (int32 i = 0; i < 16; ++i)
		{
			const float A0 = PI * (float)i / 16.f;
			const float A1 = PI * (float)(i + 1) / 16.f;
			DrawLine(CX + FMath::Cos(A0) * 0.34f * S, CY + (-0.02f + FMath::Sin(A0) * 0.34f) * S,
				CX + FMath::Cos(A1) * 0.34f * S, CY + (-0.02f + FMath::Sin(A1) * 0.34f) * S, Col, T);
		}
		break;

	default:
		DrawRing(CX, CY, 0.45f * S, Col, T);
		break;
	}
}

void AFSHUD::DrawBlessing(AFSGameMode* GM, float U)
{
	const int32 N = GM->Choices.Num();
	if (N <= 0)
	{
		return;
	}

	const float In = GM->GetChoiceIn();
	if (In <= 0.001f)
	{
		return;
	}

	AFSPlayerChar* P = GM->GetPlayerChar();

	// 卡片上的流派短名统一走 FSDefs::PathShort*，和左下角常驻流派条共用一份，
	// 否则两处各写一份数组，早晚会改歪一个。

	// 鼠标真的动了才抢高亮：否则键盘切换完，下一帧又被停在卡片上的鼠标拽回去
	if (PlayerOwner)
	{
		float MX = 0.f, MY = 0.f;
		if (PlayerOwner->GetMousePosition(MX, MY))
		{
			const bool bMoved = (LastMouseX < 0.f)
				|| FMath::Abs(MX - LastMouseX) > 2.5f || FMath::Abs(MY - LastMouseY) > 2.5f;
			LastMouseX = MX;
			LastMouseY = MY;
			if (bMoved)
			{
				for (int32 i = 0; i < N; ++i)
				{
					if (FSUtil::BlessingCardRect(SW, SH, U, i, N).Contains(MX, MY))
					{
						GM->ChoiceSel = i;
						break;
					}
				}
			}
		}
	}

	// 压暗底：卡片要能压住战场
	DrawRect(FLinearColor(0.f, 0.004f, 0.012f, 0.68f * In), 0.f, 0.f, SW, SH);

	// 标题
	const float HeadY = SH * 0.175f - (1.f - In) * 22.f * U;
	Txt(FSUtil::L(TEXT("选 择 赐 福"), TEXT("CHOOSE A BLESSING")),
		SW * 0.5f, HeadY, 2.15f * U, FLinearColor(1.f, 0.96f, 0.86f, In), true);
	{
		// Wave 为 0 只可能出现在调试直弹卡的情况下，正常流程清波时一定 ≥ 1
		const FString Sub = (GM->Wave > 0)
			? FS_FMT(TEXT("第 %d 波 清 空   ·   挑 一 样 带 走"), TEXT("WAVE %d CLEAR   -   TAKE ONE"), GM->Wave)
			: FSUtil::L(TEXT("补   给   ·   挑 一 样 带 走"), TEXT("SUPPLY   -   TAKE ONE"));
		Txt(Sub, SW * 0.5f, HeadY + 48.f * U, 0.92f * U, FLinearColor(0.62f, 0.70f, 0.82f, In), true);
	}

	for (int32 i = 0; i < N; ++i)
	{
		const EFSUpgrade Kind = GM->Choices[i];
		const FFSUpgradeDef& D = FSDefs::Upgrade(Kind);
		const EFSRarity Rar = GM->ChoiceRarity.IsValidIndex(i) ? GM->ChoiceRarity[i] : EFSRarity::Common;
		const FLinearColor RarCol = FSDefs::RarityColor(Rar);
		// 稀有度越高，卡面自身色相的染色越足：传世卡在暗底上一眼就该看出来
		const float RarTint = (Rar == EFSRarity::Epic) ? 0.085f
			: (Rar == EFSRarity::Rare ? 0.052f : 0.022f);

		// 逐张错开入场
		const float K = FMath::Clamp((GM->ChoiceElapsed - i * 0.055f) * 4.6f, 0.f, 1.f);
		const float Ease = 1.f - FMath::Pow(1.f - K, 3.f);
		if (Ease <= 0.002f)
		{
			continue;
		}

		const bool bSel = (i == GM->ChoiceSel);
		const FFSRect Base = FSUtil::BlessingCardRect(SW, SH, U, i, N);

		const float Grow = bSel ? 4.f * U : 0.f;
		const float A = Ease;
		const float X = Base.X - Grow;
		const float Y = Base.Y + (1.f - Ease) * 78.f * U - (bSel ? 12.f * U : 0.f) - Grow;
		const float W = Base.W + Grow * 2.f;
		const float H = Base.H + Grow * 2.f;
		const FLinearColor Acc = D.Color;
		const FLinearColor PathCol = FSDefs::PathColor(D.Path);

		// 投影
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.52f * A), X + 7.f * U, Y + 10.f * U, W, H);

		// 卡面：Canvas 没有渐变笔刷，用几条横带叠出"上亮下暗"，比一整块纯色有厚度
		{
			const FLinearColor Base1(
				0.030f + Acc.R * RarTint, 0.040f + Acc.G * RarTint, 0.062f + Acc.B * RarTint);
			DrawRect(FLinearColor(Base1.R, Base1.G, Base1.B, 0.97f * A), X, Y, W, H);

			const int32 Bands = 10;
			const float BandH = H / (float)Bands;
			for (int32 g = 0; g < Bands; ++g)
			{
				const float T0 = (float)g / (float)Bands;
				const float Alpha = 0.34f * T0 * T0;
				if (Alpha > 0.004f)
				{
					DrawRect(FLinearColor(0.f, 0.f, 0.f, Alpha * A), X, Y + BandH * g, W, BandH + 1.f);
				}
			}

			// 顶部一层淡色：让卡片带一点自身色相，别通篇中性灰
			DrawRect(FLinearColor(Acc.R, Acc.G, Acc.B, 0.026f * A), X, Y + 9.f * U, W, H * 0.26f);
		}

		// 顶部色带用流派色（红=烈焰 / 青=疾影 / 绿=壁垒）。
		// 这里刻意不用赐福自身色：否则同为"烈焰"的穿透/致命会一黄一红，路线就看不出去了。
		DrawRect(FLinearColor(PathCol.R, PathCol.G, PathCol.B, 0.10f * A), X, Y, W, 16.f * U);
		DrawRect(FLinearColor(PathCol.R * 0.95f, PathCol.G * 0.95f, PathCol.B * 0.95f, A), X, Y, W, 8.f * U);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.30f * A), X, Y + 8.f * U, W, 2.f * U);

		// 边框用稀有度色：灰=寻常 / 蓝=稀有 / 金=传世。
		// 选中时外扩三层微光 + 实线；未选中也要靠描边强度能分出三档。
		if (bSel)
		{
			for (int32 g = 3; g >= 1; --g)
			{
				const float Pd = 2.4f * U * g;
				const FLinearColor Glow(RarCol.R, RarCol.G, RarCol.B, 0.20f * A / (float)g);
				DrawRect(Glow, X - Pd, Y - Pd, W + Pd * 2.f, Pd);
				DrawRect(Glow, X - Pd, Y + H, W + Pd * 2.f, Pd);
				DrawRect(Glow, X - Pd, Y, Pd, H);
				DrawRect(Glow, X + W, Y, Pd, H);
			}
			const float TB = 2.4f * U;
			const FLinearColor Ec(RarCol.R, RarCol.G, RarCol.B, A);
			DrawRect(Ec, X, Y, W, TB);
			DrawRect(Ec, X, Y + H - TB, W, TB);
			DrawRect(Ec, X, Y, TB, H);
			DrawRect(Ec, X + W - TB, Y, TB, H);
		}
		else
		{
			// 传世卡即使没选中也带一层外发光，避免"好东西藏在角落"
			if (Rar == EFSRarity::Epic)
			{
				for (int32 g = 2; g >= 1; --g)
				{
					const float Pd = 2.0f * U * g;
					const FLinearColor Glow(RarCol.R, RarCol.G, RarCol.B, 0.10f * A / (float)g);
					DrawRect(Glow, X - Pd, Y - Pd, W + Pd * 2.f, Pd);
					DrawRect(Glow, X - Pd, Y + H, W + Pd * 2.f, Pd);
					DrawRect(Glow, X - Pd, Y, Pd, H);
					DrawRect(Glow, X + W, Y, Pd, H);
				}
			}
			const float TB = (Rar == EFSRarity::Epic) ? 1.8f * U : 1.2f * U;
			const float BA = (Rar == EFSRarity::Epic) ? 0.88f
				: (Rar == EFSRarity::Rare ? 0.64f : 0.34f);
			const FLinearColor Ec(RarCol.R * 0.75f + 0.10f, RarCol.G * 0.75f + 0.10f,
				RarCol.B * 0.75f + 0.10f, BA * A);
			DrawRect(Ec, X, Y, W, TB);
			DrawRect(Ec, X, Y + H - TB, W, TB);
			DrawRect(Ec, X, Y, TB, H);
			DrawRect(Ec, X + W - TB, Y, TB, H);
		}

		// 左上角序号
		{
			const float BW = 27.f * U;
			const float BH = 25.f * U;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * A), X + 14.f * U, Y + 17.f * U, BW, BH);
			Txt(FString::Printf(TEXT("%d"), i + 1), X + 14.f * U + BW * 0.5f, Y + 20.f * U,
				0.95f * U, FLinearColor(0.82f, 0.88f, 0.96f, A), true, false);
		}

		// 右上角稀有度：文字 + 档位点（寻常 1 点 / 稀有 2 点 / 传世 3 点）
		{
			const FString RTxt = FSUtil::L(FSDefs::RarityNameCN(Rar), FSDefs::RarityNameEN(Rar));
			const float RS = 0.72f * U;
			const float RWd = TxtW(RTxt, RS);
			Txt(RTxt, X + W - 15.f * U - RWd * 0.5f, Y + 17.f * U, RS,
				FLinearColor(RarCol.R, RarCol.G, RarCol.B, A), true, false);

			const int32 Pips = (int32)Rar + 1;
			const float PS = 5.f * U;
			const float PGap = 4.f * U;
			const float TotalPW = Pips * PS + (Pips - 1) * PGap;
			float PX = X + W - 15.f * U - TotalPW;
			const float PY = Y + 41.f * U;
			for (int32 p = 0; p < Pips; ++p)
			{
				DrawRect(FLinearColor(RarCol.R, RarCol.G, RarCol.B, 0.92f * A), PX, PY, PS, PS);
				PX += PS + PGap;
			}
		}

		// 图标：圆底用横线填，避免矩形四角露出圆外
		{
			const float ICX = X + W * 0.5f;
			const float ICY = Y + H * 0.335f;
			const float IR = 68.f * U;

			const int32 Lines = 22;
			const float Step = (2.f * IR) / Lines;
			const FLinearColor Disc(0.012f + Acc.R * 0.05f, 0.020f + Acc.G * 0.05f,
				0.034f + Acc.B * 0.05f, 0.68f * A);
			for (int32 k = 0; k < Lines; ++k)
			{
				const float DY = -IR + (k + 0.5f) * Step;
				const float Half = FMath::Sqrt(FMath::Max(IR * IR - DY * DY, 0.f));
				DrawLine(ICX - Half, ICY + DY, ICX + Half, ICY + DY, Disc, Step + 1.f);
			}
			DrawRing(ICX, ICY, IR, FLinearColor(Acc.R, Acc.G, Acc.B, 0.5f * A), 1.5f * U);
			DrawUpgradeIcon((int32)Kind, ICX, ICY, IR * 0.76f, FLinearColor(Acc.R, Acc.G, Acc.B, A));
		}

		// 名称
		{
			const FString Name = FSUtil::L(D.NameCN, D.NameEN);
			float NS = 1.42f * U;
			const float MaxW = W - 26.f * U;
			const float NW = TxtW(Name, NS);
			if (NW > MaxW && NW > 0.f)
			{
				NS *= MaxW / NW;
			}
			Txt(Name, X + W * 0.5f, Y + H * 0.600f, NS, FLinearColor(1.f, 1.f, 1.f, A), true);
		}

		// 效果描述：必须按稀有度算实际数值，否则传世卡还写着"伤害 +14%"就是在骗人
		{
			const FString DCN = FSDefs::UpgradeDescCN(Kind, Rar);
			const FString DEN = FSDefs::UpgradeDescEN(Kind, Rar);
			const FString Desc = FSUtil::L(*DCN, *DEN);
			const float MaxW = W - 24.f * U;
			float DS = 0.84f * U;
			const float DW = TxtW(Desc, DS);
			if (DW > MaxW && DW > 0.f)
			{
				DS *= MaxW / DW;
			}
			// 传世卡描述带一点金色，强化"这张不一样"
			const FLinearColor DescCol = (Rar == EFSRarity::Epic)
				? FLinearColor(0.98f, 0.88f, 0.66f, A)
				: FLinearColor(0.74f, 0.80f, 0.90f, A);
			Txt(Desc, X + W * 0.5f, Y + H * 0.712f, DS, DescCol, true);
		}

		// 流派 + 已有层数
		{
			const int32 Stacks = P ? P->GetUpgradeStacks((int32)Kind) : 0;
			const FString PathStr = FSUtil::L(FSDefs::PathShortCN(D.Path), FSDefs::PathShortEN(D.Path));
			const FString Tag = (Stacks <= 0)
				? FString::Printf(TEXT("%s   ·   %s"), *PathStr, FSUtil::L(TEXT("新 赐 福"), TEXT("NEW")))
				: FString::Printf(TEXT("%s   ·   %s"), *PathStr,
					*FS_FMT(TEXT("已 叠 %d 层"), TEXT("STACKED x%d"), Stacks));
			const float TY = Y + H * 0.828f;
			Txt(Tag, X + W * 0.5f, TY, 0.76f * U,
				FLinearColor(PathCol.R * 0.55f + 0.38f, PathCol.G * 0.55f + 0.38f,
					PathCol.B * 0.55f + 0.38f, 0.95f * A), true);

			// 层数条：一圈小方块，最多显示 6 个
			const int32 Dots = FMath::Min(Stacks, 6);
			if (Dots > 0)
			{
				const float DS2 = 7.f * U;
				const float GapD = 5.f * U;
				const float TotalW = Dots * DS2 + (Dots - 1) * GapD;
				float DX = X + W * 0.5f - TotalW * 0.5f;
				const float DY = TY + 22.f * U;
				for (int32 d = 0; d < Dots; ++d)
				{
					DrawRect(FLinearColor(Acc.R, Acc.G, Acc.B, 0.85f * A), DX, DY, DS2, DS2);
					DX += DS2 + GapD;
				}
			}
		}

		// 底部短色条：把卡片下沿收住；用稀有度色，和边框一起构成"这张卡多好"的提示
		DrawRect(FLinearColor(RarCol.R, RarCol.G, RarCol.B, 0.85f * A),
			X + W * 0.5f - 55.f * U, Y + H - 6.f * U, 110.f * U, 3.f * U);
	}

	// 三条流派进度：让玩家看得见自己正在走哪条路、还差几层觉醒。
	// 放在标题与卡片之间——底部那一条已经挤着生命/护盾/操作提示，再塞进去会糊成一团。
	if (P)
	{
		const float BarY = SH * 0.842f;
		const float SegW = 205.f * U;
		const float GapX = 18.f * U;
		const float AllW = 3.f * SegW + 2.f * GapX;
		float BX0 = SW * 0.5f - AllW * 0.5f;

		// 极淡的暗底：流派条直接压战场会看不清
		DrawRect(FLinearColor(0.f, 0.005f, 0.014f, 0.40f * In),
			SW * 0.5f - AllW * 0.5f - 18.f * U, BarY - 10.f * U, AllW + 36.f * U, 44.f * U);

		for (int32 pi = 0; pi < (int32)EFSPath::Count; ++pi)
		{
			const EFSPath Pth = (EFSPath)pi;
			const FLinearColor PC = FSDefs::PathColor(Pth);
			const int32 St = P->GetPathStacks(Pth);
			const bool bAwe = St >= FSDefs::PathAwakenStacks;
			const float CXP = BX0 + SegW * 0.5f;

			const FString PN = FSUtil::L(FSDefs::PathShortCN(Pth), FSDefs::PathShortEN(Pth));
			Txt(PN, CXP, BarY, 0.80f * U,
				FLinearColor(PC.R * 0.70f + 0.30f, PC.G * 0.70f + 0.30f, PC.B * 0.70f + 0.30f,
					(bAwe ? 1.f : 0.92f) * In), true);

			const float CellW = 32.f * U;
			const float CellH = 5.f * U;
			const float CellGap = 6.f * U;
			const float CBW = 3.f * CellW + 2.f * CellGap;
			float CX2 = CXP - CBW * 0.5f;
			const float CY2 = BarY + 22.f * U;
			for (int32 k = 0; k < FSDefs::PathAwakenStacks; ++k)
			{
				const FLinearColor CellCol = (k < St)
					? FLinearColor(PC.R, PC.G, PC.B, 0.95f * In)
					: FLinearColor(0.46f, 0.53f, 0.64f, 0.34f * In);
				DrawRect(CellCol, CX2, CY2, CellW, CellH);
				CX2 += CellW + CellGap;
			}

			if (bAwe)
			{
				const int32 Ovf = P->GetOverflowStacks(Pth);
				const FString AwTxt = (Ovf > 0)
					? FSUtil::L(*FString::Printf(TEXT("觉  醒  +%d"), Ovf),
						*FString::Printf(TEXT("AWAKENED  +%d"), Ovf))
					: FSUtil::L(TEXT("觉  醒"), TEXT("AWAKENED"));
				Txt(AwTxt, CXP, CY2 + 8.f * U, 0.66f * U,
					FLinearColor(PC.R, PC.G, PC.B, 0.92f * In), true, false);
			}

			BX0 += SegW + GapX;
		}
	}

	// 底部操作提示 + 超时条
	{
		const float TipY = SH * 0.905f;
		Txt(FSUtil::L(TEXT("1 / 2 / 3 直接选    ·    左右键切换    ·    回车确认    ·    也可以点卡片"),
			TEXT("1 / 2 / 3 TO PICK   ·   LEFT / RIGHT   ·   ENTER   ·   OR CLICK A CARD")),
			SW * 0.5f, TipY, 0.84f * U, FLinearColor(0.60f, 0.68f, 0.78f, In), true);

		const float BW = 340.f * U;
		const float BH = 4.f * U;
		const float BX = SW * 0.5f - BW * 0.5f;
		const float BY = TipY + 30.f * U;
		const float Pct = GM->GetChoiceLeftPct();
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.10f * In), BX, BY, BW, BH);
		DrawRect(FLinearColor(0.95f, 0.72f, 0.30f, 0.80f * In), BX, BY, BW * Pct, BH);
	}
}

// ------------------------------------------------------------------ 战场雷达

namespace
{
	/** 雷达的圆心与半径：绘制与"威胁箭头避让"共用同一套坐标，避免两边算不一样 */
	void FSRadarGeom(float ScreenW, float ScreenH, float U, float& OutCX, float& OutCY, float& OutR)
	{
		OutR = 82.f * U;
		OutCX = ScreenW - 28.f * U - OutR;
		OutCY = ScreenH - 28.f * U - 112.f * U - OutR;
	}
}

void AFSHUD::DrawRing(float CX, float CY, float R, const FLinearColor& Color, float Thickness)
{
	const int32 Seg = 48;
	for (int32 i = 0; i < Seg; ++i)
	{
		const float A0 = (2.f * PI * i) / Seg;
		const float A1 = (2.f * PI * (i + 1)) / Seg;
		DrawLine(CX + FMath::Cos(A0) * R, CY + FMath::Sin(A0) * R,
			CX + FMath::Cos(A1) * R, CY + FMath::Sin(A1) * R, Color, Thickness);
	}
}

void AFSHUD::DrawRadar(AFSGameMode* GM, AFSPlayerChar* P, float U)
{
	const float Range = 3000.f;   // 雷达半径对应的世界距离
	float CX = 0.f, CY = 0.f, R = 0.f;
	FSRadarGeom(SW, SH, U, CX, CY, R);

	const FVector PlayerLoc = P->GetActorLocation();

	// 底盘：用横向扫描线填一个圆（方底会从圆环四角露出来），再压一层圆环
	{
		const int32 Lines = 36;
		const float Step = (2.f * R) / Lines;
		const FLinearColor Fill(0.02f, 0.03f, 0.05f, 0.58f);
		for (int32 i = 0; i < Lines; ++i)
		{
			const float Y = -R + (i + 0.5f) * Step;
			const float Half = FMath::Sqrt(FMath::Max(R * R - Y * Y, 0.f));
			DrawLine(CX - Half, CY + Y, CX + Half, CY + Y, Fill, Step + 1.f);
		}
	}
	DrawRing(CX, CY, R, FLinearColor(0.30f, 0.42f, 0.55f, 0.85f), 1.3f * U);
	DrawRing(CX, CY, R * 0.5f, FLinearColor(0.24f, 0.34f, 0.46f, 0.55f), 1.0f * U);
	DrawLine(CX - R, CY, CX + R, CY, FLinearColor(0.22f, 0.30f, 0.40f, 0.45f), 1.f * U);
	DrawLine(CX, CY - R, CX, CY + R, FLinearColor(0.22f, 0.30f, 0.40f, 0.45f), 1.f * U);

	// 扫描线（纯装饰，让雷达有点"在跑"的感觉）
	{
		const float A = GetWorld()->GetTimeSeconds() * 1.5f;
		DrawLine(CX, CY, CX + FMath::Cos(A) * R, CY + FMath::Sin(A) * R,
			FLinearColor(0.35f, 0.95f, 1.0f, 0.35f), 1.6f * U);
	}

	// 世界坐标 → 雷达坐标：世界 +X 朝上，+Y 朝右
	auto ToRadar = [&](const FVector& World, bool& bOutOfRange)
	{
		FVector D = World - PlayerLoc;
		D.Z = 0.f;
		const float Len = D.Size();
		bOutOfRange = Len > Range;
		const float S = bOutOfRange ? (Range / FMath::Max(Len, 1.f)) : 1.f;
		return FVector2D(CX + D.Y * S / Range * R, CY - D.X * S / Range * R);
	};

	// 掉落物先画，敌人后画（敌人压在掉落物上面）
	for (TActorIterator<AFSPickup> It(GetWorld()); It; ++It)
	{
		AFSPickup* Item = *It;
		if (!Item || Item->IsCollected())
		{
			continue;
		}
		bool bFar = false;
		const FVector2D RP = ToRadar(Item->GetActorLocation(), bFar);
		if (bFar)
		{
			continue;
		}
		FLinearColor C = FSPal::Gold;
		float S = 2.4f * U;
		switch (Item->GetKind())
		{
		case EFSPickupKind::Health: C = FSPal::Green; break;
		case EFSPickupKind::Rune:   C = FSPal::Magic; S = 3.0f * U; break;
		case EFSPickupKind::Weapon: C = FSPal::Cyan;  S = 3.0f * U; break;
		default: break;
		}
		C.A = 0.9f;
		DrawRect(C, RP.X - S * 0.5f, RP.Y - S * 0.5f, S, S);
	}

	// 敌人：按种类上色，BOSS 额外套一圈红环
	for (const TWeakObjectPtr<AFSEnemy>& W : GM->Enemies)
	{
		AFSEnemy* E = W.Get();
		if (!E || E->IsDead())
		{
			continue;
		}
		bool bFar = false;
		const FVector2D RP = ToRadar(E->GetActorLocation(), bFar);
		const FFSEnemyDef& D = FSDefs::Enemy(E->GetKind());
		FLinearColor C = D.Color;
		const bool bBoss = E->IsBoss();
		const bool bElite = E->IsElite();
		float S = (bBoss ? 6.0f : (bElite ? 4.6f : 3.2f)) * U;
		C.A = bFar ? 0.45f : 1.f;
		DrawRect(C, RP.X - S * 0.5f, RP.Y - S * 0.5f, S, S);
		if (bElite)
		{
			// 精英在雷达上套一圈词缀色的环：远看就知道"这一片里有个麻烦的"，
			// 而且环的颜色直接对应它脚下那圈颜色，不用记图例。
			const FLinearColor EC = FSDefs::EliteRingColor(E->GetElite());
			DrawRing(RP.X, RP.Y, 6.6f * U,
				FLinearColor(EC.R, EC.G, EC.B, bFar ? 0.50f : 0.95f), 1.5f * U);
		}
		if (bBoss)
		{
			// 雷达上的红环也按 BOSS 种类换色，扫一眼就知道这波是谁
			const FLinearColor RingCol = (E->GetKind() == EFSEnemyKind::Warlord)
				? FLinearColor(1.f, 0.52f, 0.18f, 0.9f) : FLinearColor(1.f, 0.30f, 0.34f, 0.9f);
			DrawRing(RP.X, RP.Y, 8.f * U, RingCol, 1.6f * U);
		}
	}

	// 玩家：中间的三角，指向当前瞄准方向
	{
		const float Yaw = P->GetAimYaw();
		const float DX = FMath::Sin(Yaw);
		const float DY = -FMath::Cos(Yaw);
		const float PX = FMath::Cos(Yaw);
		const float PY = FMath::Sin(Yaw);
		const float L = 7.f * U;
		const float BW = 4.4f * U;
		const FLinearColor PC(1.f, 1.f, 1.f, 0.95f);
		const float TipX = CX + DX * L;
		const float TipY = CY + DY * L;
		const float B1X = CX - DX * L * 0.6f + PX * BW;
		const float B1Y = CY - DY * L * 0.6f + PY * BW;
		const float B2X = CX - DX * L * 0.6f - PX * BW;
		const float B2Y = CY - DY * L * 0.6f - PY * BW;
		DrawLine(TipX, TipY, B1X, B1Y, PC, 1.8f * U);
		DrawLine(TipX, TipY, B2X, B2Y, PC, 1.8f * U);
		DrawLine(B1X, B1Y, B2X, B2Y, PC, 1.8f * U);
	}
}

// ------------------------------------------------------------------ 贴身威胁箭头

void AFSHUD::DrawThreatArrows(AFSGameMode* GM, AFSPlayerChar* P, float U)
{
	if (!PlayerOwner || !P->IsAlive() || GM->bPaused)
	{
		return;
	}

	const FVector PlayerLoc = P->GetActorLocation();
	const FVector2D Center(SW * 0.5f, SH * 0.5f);
	const float HX = SW * 0.5f - 44.f * U;
	const float HYTop = SH * 0.5f - 44.f * U;
	const float HYBot = SH * 0.5f - 96.f * U;   // 底边留多一点，别压在符石指示上
	const float T = GetWorld()->GetTimeSeconds();

	float RCX = 0.f, RCY = 0.f, RR = 0.f;
	FSRadarGeom(SW, SH, U, RCX, RCY, RR);

	for (const TWeakObjectPtr<AFSEnemy>& W : GM->Enemies)
	{
		AFSEnemy* E = W.Get();
		if (!E || E->IsDead())
		{
			continue;
		}

		const FVector ELoc = E->GetActorLocation();
		const float Dist = FVector::Dist2D(ELoc, PlayerLoc);
		if (Dist > 1300.f)
		{
			continue;   // 远的交给雷达，屏幕上不再加噪
		}

		FVector2D SP;
		if (!PlayerOwner->ProjectWorldLocationToScreen(ELoc, SP, false))
		{
			continue;
		}
		if (SP.X > 6.f && SP.X < SW - 6.f && SP.Y > 6.f && SP.Y < SH - 6.f)
		{
			continue;   // 已经在画面里，玩家自己看得见
		}

		FVector2D Dir = SP - Center;
		if (Dir.SizeSquared() < 1.f)
		{
			continue;
		}
		Dir.Normalize();

		const float HY = (Dir.Y > 0.f) ? HYBot : HYTop;
		const float TX = (FMath::Abs(Dir.X) > 1e-4f) ? HX / FMath::Abs(Dir.X) : 1e9f;
		const float TY = (FMath::Abs(Dir.Y) > 1e-4f) ? HY / FMath::Abs(Dir.Y) : 1e9f;
		const float TT = FMath::Min(TX, TY);
		FVector2D Pos = Center + Dir * TT;

		// 右侧箭头会正好落在雷达上，挪到雷达左侧去
		{
			const float Pad = RR + 14.f * U;
			const float DXp = Pos.X - RCX;
			const float DYp = Pos.Y - RCY;
			if (Pos.X > RCX && DXp * DXp + DYp * DYp < Pad * Pad)
			{
				Pos.X = RCX - Pad;
			}
		}

		// 越近越亮、闪得越快，作为"要被贴脸了"的提示
		const float Closeness = FMath::Clamp(1.f - Dist / 1300.f, 0.f, 1.f);
		const float Pulse = 0.55f + 0.45f * FMath::Sin(T * (6.f + 10.f * Closeness));
		const FLinearColor C(1.f, 0.28f, 0.30f, (0.45f + 0.5f * Closeness) * Pulse);

		const float PX = -Dir.Y;
		const float PY = Dir.X;
		const float L = 13.f * U;
		const float BW = 6.5f * U;

		// 实心三角：从底边往尖端逐层收窄，几条横线叠出填充效果
		const int32 ArrowLayers = 5;
		for (int32 k = 0; k < ArrowLayers; ++k)
		{
			const float T0 = (float)k / (float)ArrowLayers;
			const float W0 = BW * (1.f - T0);
			const float OX = Dir.X * (-L * 0.5f + L * T0);
			const float OY = Dir.Y * (-L * 0.5f + L * T0);
			DrawLine(Pos.X + OX - PX * W0, Pos.Y + OY - PY * W0,
				Pos.X + OX + PX * W0, Pos.Y + OY + PY * W0, C, L / ArrowLayers + 1.4f * U);
		}
	}
}
