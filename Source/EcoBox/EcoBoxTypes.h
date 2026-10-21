// EcoBox - 生态箱核心数据类型
#pragma once

#include "CoreMinimal.h"
#include "EcoBoxTypes.generated.h"

namespace EcoBoxConst
{
	/** 网格边长（格）。 */
	constexpr int32 GridSize = 64;
	/** 总格数 4096。 */
	constexpr int32 CellCount = GridSize * GridSize;
	/** 单格边长（厘米）。64 * 200 = 12800cm = 128 米见方。 */
	constexpr float TileSize = 200.f;
	/** 世界半宽，把格子中心映射到以原点为中心的世界坐标。 */
	constexpr float HalfExtent = GridSize * TileSize * 0.5f;
	/** 模拟固定步长（模拟秒）。速度档位只改变每帧跑几步，不改变步长。 */
	constexpr float StepSeconds = 0.25f;
	/** 每帧最多追多少步，防止高倍速下卡死主线程。 */
	constexpr int32 MaxStepsPerFrame = 240;
	/**
	 * 生物池容量上限。
	 *
	 * 现在允许一格多只（`UEcoBoxSettings::MaxCreaturesPerCell`，默认 4），
	 * 所以池子要给到格子数的好几倍，否则"每格 4 只"根本放不下。
	 * 8 倍 = 32768，够覆盖默认配置，又不至于白占内存
	 * （`FECreature` 大约一百多字节，32768 只 ≈ 4MB，可以接受）。
	 */
	constexpr int32 MaxCreatures = CellCount * 8;
	/** 能量上限。 */
	constexpr float MaxEnergy = 100.f;
	/** 草的视觉满值（草的高度按它换算）。 */
	constexpr float MaxGrass = 255.f;
}

UENUM(BlueprintType)
enum class ECreatureType : uint8
{
	Rabbit	UMETA(DisplayName = "兔子"),
	Fox		UMETA(DisplayName = "狐狸")
};

/**
 * 生物当前在做什么。
 *
 * 一个动作占用一段固定时长（移动 = MoveSeconds，进食 = EatSeconds），
 * 做完才换下一个动作 —— 这样每种动作都能对上一段动画，
 * 而且高倍速下行为的**次序**和 1 倍速完全一致，只是发生得更快。
 */
UENUM(BlueprintType)
enum class ECreatureState : uint8
{
	Idle	UMETA(DisplayName = "待机"),
	Moving	UMETA(DisplayName = "移动中"),
	Eating	UMETA(DisplayName = "进食中")
};

/** 草的成长阶段。浇水后地块变湿润，草才开始长。 */
UENUM(BlueprintType)
enum class EGrassStage : uint8
{
	Empty	UMETA(DisplayName = "空地"),
	Growing	UMETA(DisplayName = "生长中"),
	Mature	UMETA(DisplayName = "成熟可吃")
};

/**
 * 玩家手里的工具。左键点地面就按当前工具结算。
 *
 * 前两个是"改造地面"（浇水 / 割草），后两个是"花钱买生物然后放下去"。
 * 放置类和前两个的手感不一样：割草浇水要能**按住拖动连续作用**，
 * 而买生物必须只在**按下那一下**结算，否则按住左键会一口气把钱花光。
 */
UENUM(BlueprintType)
enum class EPlayerTool : uint8
{
	None		UMETA(DisplayName = "无"),
	WaterCan	UMETA(DisplayName = "水壶"),
	Scythe		UMETA(DisplayName = "镰刀"),
	PlaceRabbit	UMETA(DisplayName = "放置兔子"),
	PlaceFox	UMETA(DisplayName = "放置狐狸")
};

/** 工具相关的判定，放在一处免得散落各文件。 */
namespace EcoBoxTool
{
	/** 是不是"购买并在光标处放置"类工具。 */
	inline bool IsPlacement(EPlayerTool Tool)
	{
		return Tool == EPlayerTool::PlaceRabbit || Tool == EPlayerTool::PlaceFox;
	}

	/** 放置类工具对应的生物；非放置类返回兔子（调用前先判 IsPlacement）。 */
	inline ECreatureType PlacementType(EPlayerTool Tool)
	{
		return (Tool == EPlayerTool::PlaceFox) ? ECreatureType::Fox : ECreatureType::Rabbit;
	}
}

/** 单格地块。4096 格，全部常驻缓存。 */
USTRUCT(BlueprintType)
struct FECell
{
	GENERATED_BODY()

	/**
	 * 视觉草量 0-255。
	 * 这是**推导值**（由 Stage 和 StageTime 算出来的），渲染层直接用它算草的高度，
	 * 所以它不等于"这格能提供多少食物"—— 能不能吃只看 Stage 是不是 Mature。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox")
	uint8 Grass = 0;

	/** 草当前处于哪个阶段。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox")
	EGrassStage Stage = EGrassStage::Empty;

	/** 当前阶段已经过了多少模拟秒。Growing 看长到哪，Mature 看还剩多久枯萎。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox")
	float StageTime = 0.f;

	/** 地块是否湿润。湿润才长草，草被吃掉后也能重新长。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox")
	bool bWet = false;

	/** 距离地块变干还有多少模拟秒。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox")
	float WetTimeLeft = 0.f;

	/**
	 * 该格当前站着几只生物。
	 *
	 * 允许一格多只（上限见 `UEcoBoxSettings::MaxCreaturesPerCell`），所以这里存的是**数量**，
	 * 不再是"某一只的下标"。
	 *
	 * 需要"找出这格里的某只生物"（比如狐狸找兔子吃、繁殖找配偶）时，
	 * 用 `UEcoBoxSimSubsystem::FindLiveCreatureInCell()` —— 它查的是
	 * `CellOccupants` 这张"格 → 生物下标列表"的表。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox")
	int32 OccupantCount = 0;
};

/** 一只生物：基因 + 状态。基因出生随机，繁殖时继承并变异。 */
USTRUCT(BlueprintType)
struct FECreature
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox")
	ECreatureType Type = ECreatureType::Rabbit;

	// ---------------- 基因 ----------------

	/**
	 * 走一格要多少模拟秒（1 倍速下约 1 秒，个体间有 ±25% 差异）。
	 * 用"每格秒数"而不是"每秒格数"，是为了让速度基因的方向更直观：
	 * 数值越小走得越快。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float MoveSeconds = 1.0f;
	/** 吃一口要多少模拟秒。这段时间里生物停在原地不动。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float EatSeconds = 1.0f;
	/** 代谢：能量 / 模拟秒。越大越容易饿。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float Metabolism = 1.2f;
	/** 繁殖倾向 0-1，越高越容易生。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float Fertility = 0.5f;
	/** 寿命（模拟秒）。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float MaxAge = 600.f;
	/** 体型，只影响渲染缩放。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float Size = 1.f;
	/** 长到成年需要多少模拟秒。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float GrowSeconds = 30.f;

	// ---------------- 状态 ----------------

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float Energy = 100.f;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float Age = 0.f;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float ReproCooldown = 0.f;

	/**
	 * 是否已成年（Age >= GrowSeconds）。
	 * 狐狸吃成年兔才给 60 能量、才产生态币；吃幼兔只有 20。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") bool bAdult = false;

	/** 成长进度 0-1 对应的渲染缩放：幼崽小、成年到 1。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float GrowScale = 1.f;

	/** 当前在做什么。渲染层据此选动画。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") ECreatureState State = ECreatureState::Idle;
	/** 当前这个动作已经进行了多少模拟秒（配合 MoveSeconds / EatSeconds 可算 0-1 进度）。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float StateTime = 0.f;

	/**
	 * 朝向（世界 Yaw，度）。在**开始移动时**按"当前格 → 目标格"算出来，
	 * 整个移动过程中保持不变，进食时沿用上一次的方向。
	 * 模型自身的正前方如果不是 UE 的 +X，用渲染器的 RabbitMeshYawOffset / FoxMeshYawOffset 补。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float FacingYaw = 0.f;

	/**
	 * 「格内游走」的随机相位（弧度）。出生时随机给一个，
	 * 渲染层用它生成一条互不同步的小圆周轨迹 —— 于是每只动物在整个格子里的
	 * 落点都不一样，看着就是"在草地上随便走"，而不是所有动物都踩着格子中心。
	 *
	 * 注意：**纯视觉**。它不参与任何逻辑（吃草、捕食、占格判定全部照旧按 `CellIdx`）。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float WanderPhase = 0.f;

	/** 格内游走的半径倍率（0-1，相对半格）。个体间有差异，避免整群走得一样大。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float WanderRadiusScale = 1.f;

	/** 0-1，当前格与目标格之间的移动插值，渲染用。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float MoveProgress = 0.f;
	/** 出生时的格子，用于渲染插值的起点。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 CellIdx = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 TargetCell = INDEX_NONE;
	/** 已繁殖次数，仅用于统计展示。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Offspring = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") bool bAlive = false;
};

/** 全局天气。玩家唯一能操纵的环境变量。 */
USTRUCT(BlueprintType)
struct FEWeather
{
	GENERATED_BODY()

	/** 降雨 0-1，促进草生长。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Weather") float Rain = 0.5f;
	/** 温度（摄氏度），过冷过热都会抑制草生长。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Weather") float Temperature = 20.f;
	/** 一天中的时刻 0-1，0.5 为正午。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Weather") float TimeOfDay = 0.3f;
	/** 一个昼夜循环占多少模拟秒。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Weather") float DayLengthSeconds = 120.f;
	/** 由 TimeOfDay 推出的日照强度 0-1，每步刷新。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Weather") float Sunlight = 0.f;
};

/** 生态币与升级。产出事件由模拟层触发，消费由 UI / 工具触发。 */
USTRUCT(BlueprintType)
struct FEEconomy
{
	GENERATED_BODY()

	/** 当前持有的生态币。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Coins = 0;

	/** 累计获得过多少（花掉的不扣）。1000 币目标看的是这个。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 LifetimeCoins = 0;

	/** 水壶等级 0..WaterCanMaxLevel，决定浇水半径。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 WaterCanLevel = 0;

	/** 镰刀等级 0..ScytheMaxLevel，决定一次割多大一片圆。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 ScytheLevel = 0;

	/** 升级线 A：每级让"狐狸吃成年兔"的基数 +1。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 CoinBaseLevel = 0;

	/** 升级线 B：每级让"收获翻倍"的概率 +5%。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 DoubleChanceLevel = 0;

	/**
	 * 降雨升级等级：每级让降雨量 +RainPerLevel（默认 1%，即 +0.01）。
	 *
	 * 降雨是"免费洒水器"—— 雨越大，草地自己就越湿，越不依赖水壶。
	 * 所以这条线本质是"用币换省事"，和另外两条收益线形成取舍。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 RainLevel = 0;

	/** 累计割草次数 / 捕食次数，仅统计。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Harvests = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Predations = 0;

	/**
	 * 累计买过多少只兔子 / 狐狸。
	 *
	 * 价格不是固定的，而是 `基准价 × 涨价系数^已买数量`：
	 * 第 1 只按基准价，之后每买一只都乘一次系数（默认 1.15）。
	 * 所以这两个计数必须存档，否则读档后价格会掉回起点。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 RabbitsBought = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 FoxesBought = 0;

	/**
	 * 累计币的两个来源分开记。
	 * 增量游戏里"钱从哪来"是要给玩家看的 —— 只看到总数的话，
	 * 玩家没法判断该升割草线还是该养狐狸。
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 CoinsFromHarvest = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 CoinsFromPredation = 0;
};

/** 生态实时统计，供 UI 显示。 */
USTRUCT(BlueprintType)
struct FEEcoStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Rabbits = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Foxes = 0;
	/** 成年 / 幼年兔子数量，用来确认成长阶段真的生效。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 AdultRabbits = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 YoungRabbits = 0;
	/** 当前正在进食的数量，用来确认"进食要花时间"真的生效了。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Eating = 0;
	/** 成熟草 / 生长中的草 / 湿润地块。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 MatureGrass = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 GrowingGrass = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 WetTiles = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 GrassTiles = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float GrassCoverage = 0.f;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Births = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Deaths = 0;
	/** 饿死 / 老死的拆分，调数值时很好用。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Starved = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Eaten = 0;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 Coins = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 LifetimeCoins = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 GoalCoins = 1000;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 WaterCanLevel = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 ScytheLevel = 0;

	/** 币的两个来源（累计），用来在面板上拆开显示。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 CoinsFromHarvest = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 CoinsFromPredation = 0;

	/** 最近一分钟的产币速率（币/分钟）。增量游戏里最重要的一个读数。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float CoinsPerMinute = 0.f;

	/** 升级线状态与**实际生效**的收益，直接显示给玩家看。 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 CoinBaseLevel = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 DoubleChanceLevel = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 HarvestYield = 1;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") int32 PredationYield = 1;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float DoubleChance = 0.f;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") float SimTime = 0.f;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") FString TimeText;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "EcoBox") FString ClockText;
};
