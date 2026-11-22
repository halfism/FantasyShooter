#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FSHUD.generated.h"

class UFont;
class AFSGameMode;
class AFSPlayerChar;

UCLASS()
class AFSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	void Txt(const FString& S, float X, float Y, float Scale, const FLinearColor& Color,
		bool bCenter = false, bool bShadow = true);
	void Bar(float X, float Y, float W, float H, float Pct, const FLinearColor& Fill, const FLinearColor& Bg);
	float TxtW(const FString& S, float Scale);

	void DrawPlaying(AFSGameMode* GM, AFSPlayerChar* P, float U);
	void DrawTitle(AFSGameMode* GM, float U);
	void DrawGameOver(AFSGameMode* GM, float U);
	/** 第 30 波通关的结算。和 GameOver 是两套画面——"赢"和"输"不该长得像 */
	void DrawVictory(AFSGameMode* GM, float U);
	void DrawPauseMenu(AFSGameMode* GM, float U);
	/** 清波后的三选一赐福卡 */
	void DrawBlessing(AFSGameMode* GM, float U);
	/** 赐福图标：全部用线段/矩形现画，不依赖贴图 */
	void DrawUpgradeIcon(int32 Kind, float CX, float CY, float S, const FLinearColor& Col);
	void DrawVignette(float U, float Flash, float HpPct, bool bLowHpWarn);
	/** 不屈免死的全屏绿闪：一次性，读玩家身上的 DeathSaveFlash */
	void DrawSaveFlash(float U, float A);
	/**
	 * 开招的屏幕色脉冲：一圈路径色暗角 + 极短的全屏白闪。
	 * 必须在所有 HUD 之前画（和时缓的边框带同一条规矩），否则会把
	 * 左上数据条与底部血条一起压暗。
	 */
	void DrawCastPulse(AFSGameMode* GM, float U);
	/** 准星外的残影射速环：Pct 是剩余时间比例（1 → 0） */
	void DrawSurgeArc(float CX, float CY, float Pct, float U);
	/** 底部中间：流派主动技的充能条 / 剩余时间 */
	void DrawAbilityBar(AFSPlayerChar* P, float U);
	/** 右下角战场雷达：以玩家为中心，显示敌人与掉落 */
	void DrawRadar(AFSGameMode* GM, AFSPlayerChar* P, float U);
	/** 贴身的、还在屏幕外的敌人 → 屏幕边缘红色箭头 */
	void DrawThreatArrows(AFSGameMode* GM, AFSPlayerChar* P, float U);
	void DrawRing(float CX, float CY, float R, const FLinearColor& Color, float Thickness);

	UPROPERTY()
	UFont* Font = nullptr;

	float SW = 1600.f;
	float SH = 900.f;

	/** 上一帧鼠标位置：用来判断"鼠标是否真的动了"，避免抢走键盘高亮 */
	float LastMouseX = -1.f;
	float LastMouseY = -1.f;
};
