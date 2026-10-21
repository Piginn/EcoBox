// EcoBox - 存档对象
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "EcoBoxTypes.h"
#include "EcoBoxSaveGame.generated.h"

/**
 * 一局的完整存档：经济进度 + 天气 + 全部地块 + 全部生物。
 *
 * ⚠ 每个字段都必须写 **`UPROPERTY(SaveGame)`**，不能写成普通的 `UPROPERTY()`。
 * 引擎 `FProperty::ShouldSerializeValue()`（Property.cpp）里有这么一段：
 *
 *     if (!(PropertyFlags & CPF_SaveGame) && Ar.IsSaveGame()) { return false; }
 *
 * 也就是说"存盘"这个动作**只序列化带 CPF_SaveGame 标志的属性**。
 * 写成普通 `UPROPERTY()` 的后果是：存档能写成功、不报任何错，
 * 但读回来是全空的 —— 非常难查。
 */
UCLASS()
class ECOBOX_API UEcoBoxSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * 结构版本号。
	 * 以后往这里增删字段就把它 +1，加载时对不上可以拒绝（而不是读出一堆错位数据）。
	 */
	UPROPERTY(SaveGame) int32 SaveVersion = 1;

	/** 保存时刻，UI 上可以显示"上次保存于 …"。 */
	UPROPERTY(SaveGame) FDateTime SavedAt;

	// -------------------- 进度 / 经济 --------------------
	UPROPERTY(SaveGame) FEEconomy Economy;
	UPROPERTY(SaveGame) FEWeather Weather;
	UPROPERTY(SaveGame) EPlayerTool CurrentTool = EPlayerTool::WaterCan;
	UPROPERTY(SaveGame) float SimTime = 0.f;
	UPROPERTY(SaveGame) float SimSpeed = 1.f;

	// -------------------- 世界 --------------------
	/** 全部 4096 格地块。 */
	UPROPERTY(SaveGame) TArray<FECell> Cells;

	/**
	 * 生物池。
	 *
	 * **必须连空槽一起原样存** —— `FECell::Occupant` 里存的是这个数组的**下标**，
	 * 如果把 `bAlive == false` 的槽压缩掉，所有格子都会指向错误的生物。
	 */
	UPROPERTY(SaveGame) TArray<FECreature> Creatures;

	/**
	 * 空闲槽位表。
	 * 不存的话，加载后新生成的生物会直接盖掉还活着的那些。
	 */
	UPROPERTY(SaveGame) TArray<int32> FreeSlots;

	// -------------------- 统计 --------------------
	UPROPERTY(SaveGame) int32 TotalBirths = 0;
	UPROPERTY(SaveGame) int32 TotalDeaths = 0;
	UPROPERTY(SaveGame) int32 TotalStarved = 0;
	UPROPERTY(SaveGame) int32 TotalEaten = 0;
};
