#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "FSSaveGame.generated.h"

UCLASS()
class UFSSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 BestWave = 0;

	UPROPERTY()
	int32 BestKills = 0;

	UPROPERTY()
	int32 BestCombo = 0;

	/** 音量设置；负数表示还没保存过，走默认值 */
	UPROPERTY()
	float MasterVolume = -1.f;

	/** 语言：0 自动跟随字体 1 强制中文 2 强制英文 */
	UPROPERTY()
	int32 LanguageMode = 0;

	UPROPERTY()
	int32 TotalKills = 0;

	UPROPERTY()
	int32 TotalCoins = 0;

	UPROPERTY()
	int32 Runs = 0;

	/** 通关次数（打完第 30 波）。0 = 还没赢过 */
	UPROPERTY()
	int32 ClearCount = 0;

	/** 最快通关用时（秒）。0 = 还没有记录 */
	UPROPERTY()
	float BestClearTime = 0.f;
};
