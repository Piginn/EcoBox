// EcoBox - 模拟层：固定步长的生态推演核心
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EcoBoxTypes.h"
#include "EcoBoxSimSubsystem.generated.h"

/**
 * 生态模拟子系统。
 *
 * 只负责「数据怎么变」，完全不碰渲染。所有状态都是数组，没有任何 Actor，
 * 所以 4096 格 + 上千只生物也只是内存里的一串字节。
 *
 * 时间模型：逻辑以固定步长 StepSeconds 推进，速度档位只决定每帧跑几步。
 * 这保证了 8 倍速下生物的行为与 1 倍速完全一致，只是发生得更快。
 */
UCLASS()
class ECOBOX_API UEcoBoxSimSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ---------------- USubsystem ----------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---------------- FTickableGameObject ----------------
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** 在编辑器视口、PIE、独立运行三种世界里都启用，这样摆好 Actor 就能直接看到生态在动。 */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game
			|| WorldType == EWorldType::PIE
			|| WorldType == EWorldType::Editor;
	}

	// ---------------- 只读访问（渲染层每帧读） ----------------
	const TArray<FECell>& GetCells() const { return Cells; }
	const TArray<FECreature>& GetCreatures() const { return Creatures; }
	const FEWeather& GetWeather() const { return Weather; }
	const FEEcoStats& GetStats() const { return Stats; }
	const FEEconomy& GetEconomy() const { return Economy; }
	float GetSimSpeed() const { return SimSpeed; }
	bool IsPaused() const { return SimSpeed <= 0.f; }

	// ---------------- 生态币与经济 ----------------

	/** 加币（同时也计入累计，用于 1000 币目标）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	void AddCoins(int32 Amount);

	/** 够了就扣掉并返回 true；不够返回 false 且什么都不改。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	bool TrySpendCoins(int32 Amount);

	/** 当前水壶的浇水半径（格，含小数）。由等级和项目设置算出。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	float GetWaterCanRadius() const;

	/** 水壶升到下一级要多少币；已满级返回 INDEX_NONE。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	int32 GetWaterCanUpgradeCost() const;

	/** 当前镰刀的收割半径（格，含小数）。0 = 只割光标那一格。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	float GetScytheRadius() const;

	/** 镰刀升到下一级要多少币；已满级返回 INDEX_NONE。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	int32 GetScytheUpgradeCost() const;

	/** 升级线 A / B 升到下一级要多少币；已满级返回 INDEX_NONE。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	int32 GetCoinUpgradeCost(int32 CurrentLevel) const;

	/** 降雨升级到下一级要多少币；已满级返回 INDEX_NONE。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	int32 GetRainUpgradeCost() const;

	/**
	 * 最近一段时间的产币速率（币/分钟）。
	 * 用 60 个"1 模拟秒"的桶算滑动窗口；开局窗口还没铺满时按已过时间折算，
	 * 不然刚赚了 5 个币会显示成 5/分钟。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	float GetCoinsPerMinute() const;

	/** 现在割一次草 / 捕食一次各产多少币（已经把升级线 A 的加成算进去了）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	int32 GetHarvestYield() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	int32 GetPredationYield() const;

	/** 现在"收获翻倍"的概率 0-1（升级线 B）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	float GetDoubleChance() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	bool UpgradeWaterCan();

	/** 镰刀升级：每级扩大一次收割的圆形范围。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	bool UpgradeScythe();

	/** 升级线 A：每级让「狐狸吃成年兔」的收益基数 +1。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	bool UpgradeCoinBase();

	/** 升级线 B：每级让「收获翻倍」的概率 +5%。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	bool UpgradeCoinDouble();

	/** 降雨升级：花币把降雨量 +1%（默认），雨越大草地越自己保湿。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	bool UpgradeRain();

	/** 买一只生物放到指定格。买不起 / 格子被占都返回 false。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	bool BuyCreature(ECreatureType Type, int32 CellIdx);

	UFUNCTION(BlueprintCallable, Category = "EcoBox|经济")
	int32 GetCreaturePrice(ECreatureType Type) const;

	// ---------------- 玩家工具 ----------------

	UFUNCTION(BlueprintCallable, Category = "EcoBox|工具")
	void SetTool(EPlayerTool InTool);

	EPlayerTool GetTool() const { return CurrentTool; }

	/**
	 * 收集以 CenterCell 为圆心、半径 Radius（格，欧氏距离）的**圆形**范围内的地块下标。
	 *
	 * 和 WaterCircle 共用同一套判定，所以玩家看到的预览圈和实际浇到的格子永远一致
	 * （预览就是拿它算的）。OutCells 会先被清空。
	 */
	void GetCircleCells(int32 CenterCell, float Radius, TArray<int32>& OutCells) const;

	/**
	 * 以 CenterCell 为圆心、半径 Radius（格，欧氏距离）浇水，返回浇到的地块数。
	 * 范围内的地块变成湿润并重置干燥倒计时。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|工具")
	int32 WaterCircle(int32 CenterCell, float Radius);

	/** 用当前水壶半径浇水。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|工具")
	int32 WaterWithCurrentCan(int32 CenterCell);

	/** 割掉这一格的成熟草，返回得到的生态币（0 = 没草或还没成熟）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|工具")
	int32 HarvestGrass(int32 CellIdx);

	/**
	 * 以 CenterCell 为圆心、半径 Radius（格）收割一整片成熟草，返回得到的生态币总量。
	 * 和水壶一样走 GetCircleCells 的圆形判定，所以预览圈 = 实际割到的格子。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|工具")
	int32 HarvestCircle(int32 CenterCell, float Radius);

	/** 用当前镰刀范围收割。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|工具")
	int32 HarvestWithCurrentScythe(int32 CenterCell);

	// ---------------- 玩家可操纵项 ----------------

	/** 设置时间速度倍率。0 表示暂停。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void SetSimSpeed(float InSpeed);

	/** 在速度档位之间循环。Dir 为 +1 / -1。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void CycleSimSpeed(int32 Dir);

	/** 降雨增减，自动夹在 0-1。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void AddRain(float Delta);

	/** 温度增减，自动夹在 -10~45 度。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void AddTemperature(float Delta);

	/** 昼夜长度增减。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void AddDayLength(float Delta);

	/** 手动种草 / 清草。Coverage 是覆盖率 0-1。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void SeedGrass(float Coverage);

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void ClearGrass();

	/** 清空所有生物与草，从头开始。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void ResetWorld(bool bWithGrass = true);

	/** 在指定格生成一只生物，返回池下标。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	int32 SpawnCreature(ECreatureType Type, int32 CellIdx);

	/** 在随机空地生成 N 只生物，返回实际生成数量。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	int32 SpawnCreatureRandom(ECreatureType Type, int32 Count);

	/** 在随机有草的地块生成 N 只兔子（更符合直觉的"投放"）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	int32 SpawnOnGrass(ECreatureType Type, int32 Count);

	// ---------------- 存档 ----------------
	//
	// 存档是"一整局"的快照：经济 + 天气 + 全部 4096 格 + 全部生物。
	// 槽名固定，蓝图里不用管路径 —— 文件落在 项目/Saved/SaveGames/ 下。

	/** 默认存档槽名。 */
	static const TCHAR* GetDefaultSaveSlot() { return TEXT("EcoBoxSave"); }

	/** 把当前整局写进存档槽。成功返回 true。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "保存游戏"))
	bool SaveGame();

	/**
	 * 从存档槽读回整局。
	 * 没有存档 / 版本对不上 / 地块数不符都返回 false（且**不改动**当前世界）。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "加载游戏"))
	bool LoadGame();

	/** 存档槽里有没有东西。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "有存档吗"))
	bool HasSaveGame() const;

	/** 删掉存档（"新游戏"用）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "删除存档"))
	bool DeleteSaveGame();

	/** 存档时间文字（如 "2026-10-16 20:30"）。没有存档返回空串。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "存档时间"))
	FString GetSaveTimeText() const;

	/**
	 * 有存档就加载，返回 true 表示"这一局是读档来的"。
	 * 开局自动检查走的就是它 —— 蓝图里也可以用它做菜单的"继续游戏"。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "有存档就加载"))
	bool LoadIfExists();

	// ---------------- 坐标换算 ----------------
	static int32 CellIndex(int32 X, int32 Y) { return Y * EcoBoxConst::GridSize + X; }
	static int32 CellX(int32 Idx) { return Idx % EcoBoxConst::GridSize; }
	static int32 CellY(int32 Idx) { return Idx / EcoBoxConst::GridSize; }
	static bool IsValidCoord(int32 X, int32 Y)
	{
		return X >= 0 && Y >= 0 && X < EcoBoxConst::GridSize && Y < EcoBoxConst::GridSize;
	}
	static FVector CellToWorld(int32 Idx);
	static int32 WorldToCell(const FVector& World);

	/** 生物的渲染用世界坐标（含移动插值）。 */
	FVector GetCreatureWorldPos(const FECreature& Creature) const;

	/**
	 * 同上，额外输出「跳跃相位」：
	 *   OutHopAlpha ∈ [-1, 1]，0 = 踩在地面，1 = 腾空最高点，-1 = 落地压扁最低点。
	 * 渲染层用它做伸缩/前倾。非兔子、没在移动、或关了跳跃时一律给 0。
	 */
	FVector GetCreatureWorldPos(const FECreature& Creature, float& OutHopAlpha) const;

	/**
	 * 生物在某个格子内部的「落点偏移」（相对格心的 XY 偏移，厘米）。
	 * 用 `Creature.WanderPhase` + 格子坐标哈希生成一条互不同步的李萨如轨迹。
	 *
	 * @param bTimeAnimated  true = 偏移随 SimTime 变化（**静止**时用在原地溜达）；
	 *                       false = 冻掉时间项、只由格坐标决定（**移动**时用，走确定斜线）。
	 * 纯视觉，不参与逻辑。关掉 `bCreatureWander` 时恒返回零向量。
	 */
	FVector GetWanderOffset(int32 CellIdx, float Phase, float RadiusScale, bool bTimeAnimated) const;

	static FString FormatClock(float TimeOfDay);

private:
	// ---------------- 每一步模拟 ----------------
	void StepSim(float Dt);
	void UpdateWeatherSim(float Dt);
	void UpdateGrassSim(float Dt);
	void UpdateCreaturesSim(float Dt);
	void UpdateCreature(int32 Idx, float Dt);
	void RefreshStats();

	/** 8 邻居打分选目标格。 */
	int32 PickTargetCell(const FECreature& Creature);

	/**
	 * 走进新格：释放旧格、占据新格。
	 * 狐狸进格时会**当场咬死**该格的兔子（能量立刻结算），
	 * 返回值表示"这一口要不要再花时间吃完"（只有狐狸咬到兔子才是 true）。
	 * 被别的生物抢先占格时会走不进去，此时什么都不改。
	 */
	bool EnterCell(int32 Idx, int32 NewCell);

	/** 选下一个目标格并进入 Moving（同时按方向定朝向）；八面被围就转 Idle，下一步再试。 */
	void StartNextMove(int32 Idx);

	/** 兔子落在**成熟**草格上就进入 Eating。返回是否开始进食。 */
	bool BeginGrassMealIfAny(int32 Idx);

	/** 进食结束，结算这一口的收益（兔子扣草加能量；狐狸的能量在咬死时已经给了）。 */
	void FinishMeal(int32 Idx);

	// ---------------- 草与地块 ----------------

	/** 单个地块推进一个步长：湿度倒计时 + 草的生命周期。 */
	void TickCell(int32 CellIdx, float Dt);

	/** 把这一格浇湿（重置干燥倒计时）。 */
	void WaterCell(int32 CellIdx);

	/** 设置草阶段并重置阶段计时。 */
	void SetGrassStage(int32 CellIdx, EGrassStage Stage);

	/** 空地：清掉草并把视觉量归零。 */
	void ClearGrassAt(int32 CellIdx);

	/** 由 Stage / StageTime 推出视觉草量（渲染层直接读 FECell::Grass）。 */
	void RefreshCellVisual(FECell& Cell) const;

	/** 每步按降雨随机浇湿一批地块（下雨 = 免费洒水器）。 */
	void ApplyRainWatering(float Dt);

	/** 在 8 邻居里找一个空格。 */
	int32 FindFreeNeighbor(int32 CellIdx);
	/** 尝试繁殖。 */
	void TryReproduce(int32 Idx);
	/**
	 * 死亡，释放占格与槽位。
	 * bEaten = true 表示被狐狸吃掉（用于统计"饿死 vs 被捕食"）。
	 */
	void KillCreature(int32 Idx, bool bEaten = false);

	int32 AllocSlot();
	/** bAdult = 出生即成年（买来的、开局投的）；false 则从幼崽开始长。 */
	int32 SpawnCreatureInternal(ECreatureType Type, int32 CellIdx, bool bAdult, const FECreature* Parent);
	void InitGenes(FECreature& Creature, const FECreature* Parent);

	/** 把这一笔收入记进"最近一分钟"的滑动窗口。 */
	void PushCoinIncome(int32 Amount);
	/** 滑动窗口随模拟时间滚动。 */
	void AdvanceIncomeWindow(float Dt);

	// ---------------- 数据 ----------------
	TArray<FECell> Cells;
	TArray<FECreature> Creatures;
	TArray<int32> FreeSlots;

	/**
	 * "格 → 这格里有哪些生物"的反查表。
	 *
	 * 一格现在可以站多只，所以 `FECell::OccupantCount` 只能告诉你"有几只"，
	 * 想知道"是哪几只"（狐狸找猎物、繁殖找配偶、死亡/移动时清理）就得查这张表。
	 * 用 `TMap` 而不是定长数组：64×64 里绝大多数格子是空的。
	 *
	 * **不存档** —— 读档 / 重置世界 / 扩容后统一从 `Creatures` 重建
	 * （见 `RebuildCellOccupants()`），避免两份数据不同步。
	 */
	TMap<int32, TArray<int32>> CellOccupants;

	/** 把一只生物登记到它所在的格子里（顺手维护 OccupantCount）。 */
	void AddCreatureToCell(int32 CellIdx, int32 CreatureIdx);
	/** 把一只生物从格子里摘掉（顺手维护 OccupantCount）。 */
	void RemoveCreatureFromCell(int32 CellIdx, int32 CreatureIdx);
	/**
	 * 找出某格里的一只**活的、指定类型**的生物下标；没有返回 INDEX_NONE。
	 * 多只时取数组里第一个可用的 —— 捕食 / 繁殖只关心"有得吃 / 有对象"。
	 */
	int32 FindLiveCreatureInCell(int32 CellIdx, ECreatureType Type) const;
	/** 按当前 `Creatures` 全量重建 `CellOccupants` 和每格的 `OccupantCount`。 */
	void RebuildCellOccupants();

	FEWeather Weather;
	FEEcoStats Stats;
	FEEconomy Economy;

	/** 玩家当前手里的工具。左键点地面按它结算。 */
	EPlayerTool CurrentTool = EPlayerTool::WaterCan;

	/** 时间速度倍率。 */
	float SimSpeed = 1.f;
	/** 累积的未消耗时间。 */
	float Accum = 0.f;
	/** 模拟总时长（模拟秒）。 */
	float SimTime = 0.f;
	/** 统计累计。 */
	int32 TotalBirths = 0;
	int32 TotalDeaths = 0;
	int32 TotalStarved = 0;
	int32 TotalEaten = 0;
	/** 上次打生态快照日志的模拟时刻。 */
	float LastStatLogTime = 0.f;

	FRandomStream Rng;

	/**
	 * 产币速率的滑动窗口：每个桶装 1 模拟秒的收入，一共 60 个桶。
	 * 求和 = 最近 60 秒赚了多少 = 币/分钟。60 个 float 的开销可以忽略。
	 */
	static constexpr int32 IncomeBuckets = 60;
	TArray<float> CoinBuckets;
	int32 CoinBucketIndex = 0;
	float CoinBucketTimer = 0.f;

	/** 已初始化标记。 */
	bool bReady = false;
};
