// EcoBox - 全部生态参数，集中在这里改
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/UObjectGlobals.h"
#include "EcoBoxSettings.generated.h"

/**
 * 生态箱的可调参数（v1.0 增量玩法：浇水 → 草 → 兔 → 狐 → 生态币 → 升级）。
 *
 * 在哪改：编辑器菜单 Edit → Project Settings → 搜索框打 "EcoBox"
 * （位置在 Project Settings 的 Game 分类下，名字叫「EcoBox 生态箱」）。
 *
 * 为什么要做成这样：这些值**不是编译期常量**，模拟层每一步都会重新读一遍，
 * 所以在 PIE 运行中一边玩一边改，改完当帧就生效 —— 不用停 PIE、不用重编译。
 * 改动会自动写进 Config/DefaultGame.ini，下次打开还在。
 *
 * 想恢复出厂值：在 ini 里删掉 [/Script/EcoBox.EcoBoxSettings] 那一段即可。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "EcoBox 生态箱"))
class ECOBOX_API UEcoBoxSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// ====================== 开局 ======================

	/**
	 * 开局自动投放的兔子数量。设为 0 就是完全空白的草地。
	 *
	 * ⚠ 文档写的是 2，但实测 2 兔 1 狐**跑不起来**：狐狸是捕食者，兔子上到几百只
	 * 需要好几分钟，而它 50 秒不进食就饿死 —— 开局 60 秒内狐狸必死，
	 * 也就永远产不出生态币（币恒为 0）。所以默认值上调到了 16。
	 * 实测（16 兔 2 狐）：
	 *   t= 30s 兔  41 狐  2 币   1
	 *   t=150s 兔 212 狐  5 币  13
	 *   t=360s 兔  44 狐 63 币 264   ← 捕食-被捕食的正常振荡
	 * 开局投放的都是**成年**兔，否则狐狸一上来只能吃幼兔（+20），更撑不住。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "开局",
		meta = (DisplayName = "开局兔子数", ClampMin = "0", UIMin = "0", UIMax = "200"))
	int32 InitialRabbits = 16;

	/** 开局自动投放的狐狸数量。太小的话它撑不到兔子变多。 */
	UPROPERTY(Config, EditAnywhere, Category = "开局",
		meta = (DisplayName = "开局狐狸数", ClampMin = "0", UIMin = "0", UIMax = "100"))
	int32 InitialFoxes = 2;

	/**
	 * 开局预置多少比例的地块是「湿润 + 成熟草」，相当于白送一次全图浇水。
	 * 设为 0 就是完全干燥、需要玩家自己浇水的开端。
	 *
	 * 别设太小：地块 25 秒就干了，预置的草 30 秒后也会枯萎，
	 * 4%（文档值）实测 66 秒内世界就全灭 —— 玩家刚进游戏看到的是一片荒地。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "开局",
		meta = (DisplayName = "开局湿润草覆盖", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float InitialGrassCoverage = 0.15f;

	/**
	 * 开局降雨强度 0-1。降雨会自动浇湿地块（等于免费洒水器，见「天气」分类）。
	 *
	 * 默认 0：开局不下雨，草地是干的，玩家得自己用水壶浇 ——
	 * 这样"水壶"和"降雨调节"才有存在意义。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "开局",
		meta = (DisplayName = "开局降雨", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float InitialRain = 0.f;

	// ====================== 草 ======================

	/** 湿润地块上，草从无到成熟需要多少模拟秒（文档基线 = 5）。 */
	UPROPERTY(Config, EditAnywhere, Category = "草",
		meta = (DisplayName = "生长秒数", ClampMin = "0.25", UIMin = "1.0", UIMax = "30.0"))
	float GrassGrowSeconds = 5.f;

	/**
	 * 成熟之后没人吃、也没人割，多少模拟秒后枯萎消失（文档基线 = 30）。
	 *
	 * 注意：枯萎之后如果地块**还是湿的**，会立刻开始重新长一茬 —— 所以
	 * 「浇水」的收益 = 这块地保持湿润期间能产出多少茬草，而不是一次性一颗。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "草",
		meta = (DisplayName = "枯萎秒数", ClampMin = "1.0", UIMin = "5.0", UIMax = "180.0"))
	float GrassWiltSeconds = 30.f;

	/**
	 * 浇水之后地块多久变干（文档基线 = 10）。
	 *
	 * **这个值直接决定水壶的价值**：草长一茬要 GrassGrowSeconds，
	 * 所以一块地一次浇水最多能产 DryTime / GrowTime 茬草。
	 * 10 秒只能产 2 茬，玩家会一直在点水壶；提到 25~30 秒手感会好很多。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "草",
		meta = (DisplayName = "地块变干秒数", ClampMin = "1.0", UIMin = "5.0", UIMax = "180.0"))
	float TileDrySeconds = 25.f;

	/** 成熟草枯萎前的预警时间：这段时间里草会发黄变矮，提醒玩家赶紧收。 */
	UPROPERTY(Config, EditAnywhere, Category = "草",
		meta = (DisplayName = "枯萎预警秒数", ClampMin = "0.0", UIMin = "0.0", UIMax = "15.0"))
	float GrassWiltWarnSeconds = 5.f;

	// ====================== 动作节奏 ======================

	/** 兔子走一格要多少模拟秒（个体基因会在这个基准上 ±25% 浮动）。 */
	UPROPERTY(Config, EditAnywhere, Category = "动作节奏",
		meta = (DisplayName = "兔子每格秒数", ClampMin = "0.05", UIMin = "0.1", UIMax = "3.0"))
	float RabbitMoveSecondsPerCell = 1.0f;

	/**
	 * 狐狸走一格要多少模拟秒。
	 * 默认让它比兔子快一点（0.85 vs 1.0）—— 不然在双方都以 1 格/秒同速移动时，
	 * 狐狸永远追不上猎物，只能靠"兔子停下来进食"那 1 秒的窗口碰运气。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "动作节奏",
		meta = (DisplayName = "狐狸每格秒数", ClampMin = "0.05", UIMin = "0.1", UIMax = "3.0"))
	float FoxMoveSecondsPerCell = 0.85f;

	/**
	 * 吃一口要多少模拟秒（兔子吃草 / 狐狸吃肉都用它，将来播动画也用这个时长）。
	 * 这段时间里生物**停在原地不动**，所以加长它 = 加强捕食压力。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "动作节奏",
		meta = (DisplayName = "进食秒数", ClampMin = "0.05", UIMin = "0.1", UIMax = "5.0"))
	float EatSeconds = 1.0f;

	// ====================== 外观 ======================

	/**
	 * 兔子是否用"一蹦一蹦"的方式移动。
	 * 这**纯粹是渲染层**的视觉偏移：只改画出来的 Z 高度，不动逻辑格子。
	 * 兔子的真实位置、吃草判定、狐狸的追击距离全部照旧按地面直线算。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "兔子跳跃移动"))
	bool bRabbitHopMove = true;

	/** 兔子每次跳跃最高离地多少厘米。 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "跳跃高度(cm)", ClampMin = "0.0", UIMin = "0.0", UIMax = "200.0"))
	float RabbitHopHeight = 30.f;

	/** 走一格的过程中跳几下。2 就是"蹦两下过去"，落地瞬间在格子边界上。 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "每格跳几下", ClampMin = "1", UIMin = "1", UIMax = "6"))
	int32 RabbitHopCount = 2;

	/**
	 * 每一次跳跃里"腾空"占的比例（0.55 = 前半段腾空、后半段贴地滑）。
	 * 调小 → 跳得更高更利落；调大 → 更接近连续滑动。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "腾空占比", ClampMin = "0.15", UIMin = "0.15", UIMax = "1.0"))
	float RabbitHopAirRatio = 0.6f;

	/**
	 * 跳跃时的"拉伸"：腾空最高点把身体稍微拉长、落地时压扁一点。
	 * 0 = 完全关闭；0.15 = 最高点拉长 15%、落地压扁 15%。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "跳跃伸缩量", ClampMin = "0.0", UIMin = "0.0", UIMax = "0.6"))
	float RabbitHopSquashStretch = 0.12f;

	/**
	 * 跳跃时上半身往前倾的幅度（度）。兔子起跳时是往前扑的，给一点俯仰更有生气。
	 * 正数 = 朝行进方向低头。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "跳跃前倾角度", ClampMin = "0.0", UIMin = "0.0", UIMax = "45.0"))
	float RabbitHopPitchDeg = 12.f;

	// -------------------- 格内游走 --------------------

	/**
	 * 生物是否在格子内部"随便走"（而不是所有个体都踩着格子中心）。
	 *
	 * ⚠ 这**纯粹是渲染层**的视觉偏移：逻辑格子 `CellIdx` 完全不变，
	 * 吃草、捕食、占格判定全部照旧。关掉就退回"格心到格心"的原始走法。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "生物格内游走"))
	bool bCreatureWander = true;

	/**
	 * 站着不动的时候，是否也在格子内慢慢挪（原地溜达）。
	 * 关掉（推荐）：**停下就是真的停住**，一动不动 —— 只在真的要移动时才挪位置。
	 * 开着：即使发呆/进食也会在小范围里晃。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "静止时也游走"))
	bool bWanderWhenIdle = false;

	/**
	 * 兔子是否"只在跳跃腾空时前进"。
	 * 开着（推荐）：一次跳跃分两段 —— 腾空段整体往前冲，落地段**停住**等地下一跳。
	 *   是最标准的"一蹦一蹦"，而且落地那一下纹丝不动。
	 * 关掉：XY 匀速前进，跳跃只是叠在 Z 上（看起来像"飘着跳"）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "兔子只在腾空时前进"))
	bool bRabbitMoveOnlyWhenAirborne = true;

	/**
	 * 格内游走的半径，占**半格**的比例。
	 * 0.7 ≈ 最远能走到离格心 70% 半格处，肉眼能明显看出"不在格心"。
	 * 调到 0.9 会贴着格子边缘走，太大就会出现"看起来跨格了、逻辑还在原来格子"的错觉。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "格内游走半径", ClampMin = "0.0", UIMin = "0.0", UIMax = "0.95"))
	float CreatureWanderRadius = 0.70f;

	/**
	 * 格内游走的快慢（每秒走多少个来回）。数值越大晃得越勤。
	 * 0.25 大约 4 秒绕一圈 —— 松弛自然，不会像苍蝇一样乱窜。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "格内游走速度", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float CreatureWanderSpeed = 0.25f;

	/**
	 * 移动路径的弯曲幅度（占**半格**的比例）。
	 * 0 = 从前一格落点走直线到下一格落点；0.4 = 路径中段会向一侧拱出去，
	 * 看起来是"绕着走"而不是"直挺挺地走直线"。
	 * 纯视觉 —— 逻辑上仍然是那一格到那一格。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "外观",
		meta = (DisplayName = "移动路径弯曲", ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float CreatureWanderCurve = 0.35f;

	// ====================== 兔子 ======================

	/** 兔子出生时的能量（文档基线 = 100）。 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "出生能量", ClampMin = "1.0", UIMin = "10.0", UIMax = "100.0"))
	float RabbitStartEnergy = 100.f;

	/** 吃一口成熟草回多少能量（文档基线 = 25）。一口吃掉整格草。 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "每口回能量", ClampMin = "1.0", UIMin = "5.0", UIMax = "100.0"))
	float RabbitEnergyPerBite = 25.f;

	/**
	 * 每秒消耗的能量（文档基线 = 2）。
	 * 满能量 100 / 2 = **50 秒不吃就饿死**；一口草 +25 = 12.5 秒续航。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "代谢", ClampMin = "0.05", UIMin = "0.2", UIMax = "10.0"))
	float RabbitMetabolism = 2.0f;

	/** 长到成年需要多少模拟秒（文档基线 = 30）。体型会在这个过程里逐渐变大。 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "成年秒数", ClampMin = "0.5", UIMin = "5.0", UIMax = "180.0"))
	float RabbitGrowSeconds = 30.f;

	/** 繁殖所需能量门槛（必须 ≤ 100）。 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "繁殖门槛", ClampMin = "10.0", UIMin = "20.0", UIMax = "100.0"))
	float RabbitReproEnergy = 80.f;

	/** 生一只要扣掉多少能量。 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "繁殖花费", ClampMin = "0.0", UIMin = "0.0", UIMax = "100.0"))
	float RabbitReproCost = 35.f;

	/** 幼兔出生时带多少能量。 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "幼崽能量", ClampMin = "1.0", UIMin = "10.0", UIMax = "100.0"))
	float RabbitChildEnergy = 50.f;

	/** 两次繁殖之间的最短间隔（模拟秒），繁殖倾向基因会缩放它。 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "繁殖冷却", ClampMin = "0.0", UIMin = "0.0", UIMax = "300.0"))
	float RabbitReproCooldown = 20.f;

	/**
	 * 寿命（模拟秒）。文档里只写了「能量耗尽即死亡」，
	 * 这里保留一个很长的上限，主要是防止种群无限堆积。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "兔子",
		meta = (DisplayName = "寿命", ClampMin = "10.0", UIMin = "60.0", UIMax = "3600.0"))
	float RabbitMaxAge = 600.f;

	// ====================== 狐狸 ======================

	/** 狐狸出生时的能量（文档基线 = 100）。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "出生能量", ClampMin = "1.0", UIMin = "10.0", UIMax = "100.0"))
	float FoxStartEnergy = 100.f;

	/** 吃掉一只**幼兔**回多少能量（文档基线 = 20）。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "吃幼兔回能量", ClampMin = "1.0", UIMin = "5.0", UIMax = "100.0"))
	float FoxEnergyPerYoungRabbit = 20.f;

	/** 吃掉一只**成年兔**回多少能量（文档基线 = 60），同时产生态币。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "吃成年兔回能量", ClampMin = "1.0", UIMin = "5.0", UIMax = "100.0"))
	float FoxEnergyPerAdultRabbit = 60.f;

	/**
	 * 每秒消耗的能量。
	 *
	 * ⚠ 文档写的是 3.0，实测那个值下狐狸活不下去，默认值下调到了 1.5。
	 * 实测数据（16 兔 2 狐起步、地块靠降雨维持湿润）：
	 *   - 3.0（文档值）：33 秒窗口，60 秒内一次都没吃到，狐狸必饿死，**币恒为 0**。
	 *   - 1.5（当前默认）：100/1.5 ≈ 67 秒窗口。狐狸 2 → 63 只，360 秒累计 264 币，
	 *     兔群在 41 ↔ 248 之间正常振荡。
	 *   - 1.0：窗口 100 秒，狐狸过强（10 只起步能涨到 319 只），会把兔子压崩。
	 * 想自己调就改这个数，改完当帧生效，不用重编译。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "代谢", ClampMin = "0.05", UIMin = "0.2", UIMax = "10.0"))
	float FoxMetabolism = 1.5f;

	/**
	 * 狐狸的**感知半径**（格）。
	 *
	 * 这个值不是装饰，是让狐狸真的能当捕食者的关键：
	 * 只看 8 邻居的话，狐狸要走到兔子旁边才发现猎物，而兔子也在动 ——
	 * 实测（2 兔 1 狐起步）狐狸 50 秒里一次都吃不到，必然饿死，经济也就产不出币。
	 * 给它 4 格视野 + 朝猎物走，才追得上。
	 * 81 格/步的扫描开销可以忽略（几百只狐狸 × 每秒 4 步）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "感知半径", ClampMin = "1", UIMin = "1", UIMax = "12"))
	int32 FoxSenseRadius = 4;

	/** 长到成年需要多少模拟秒（成年与否只影响被吃时的能量收益）。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "成年秒数", ClampMin = "0.5", UIMin = "5.0", UIMax = "180.0"))
	float FoxGrowSeconds = 30.f;

	/** 繁殖所需能量门槛（必须 ≤ 100，否则永远不会繁殖）。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "繁殖门槛", ClampMin = "10.0", UIMin = "20.0", UIMax = "100.0"))
	float FoxReproEnergy = 90.f;

	/** 生一只要扣掉多少能量。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "繁殖花费", ClampMin = "0.0", UIMin = "0.0", UIMax = "100.0"))
	float FoxReproCost = 40.f;

	/** 幼狐出生时带多少能量。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "幼崽能量", ClampMin = "1.0", UIMin = "10.0", UIMax = "100.0"))
	float FoxChildEnergy = 60.f;

	/** 两次繁殖之间的最短间隔（模拟秒）。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "繁殖冷却", ClampMin = "0.0", UIMin = "0.0", UIMax = "300.0"))
	float FoxReproCooldown = 40.f;

	/** 寿命（模拟秒）。 */
	UPROPERTY(Config, EditAnywhere, Category = "狐狸",
		meta = (DisplayName = "寿命", ClampMin = "10.0", UIMin = "60.0", UIMax = "3600.0"))
	float FoxMaxAge = 900.f;

	// ====================== 水壶 ======================

	/** 水壶 0 级时的浇水半径（格）。1 格半径的圆大约覆盖 5 格地。 */
	UPROPERTY(Config, EditAnywhere, Category = "水壶",
		meta = (DisplayName = "初始半径", ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float WaterCanBaseRadius = 1.f;

	/** 每升一级半径加多少格（文档用的是 √2 ≈ 1.41）。 */
	UPROPERTY(Config, EditAnywhere, Category = "水壶",
		meta = (DisplayName = "每级加半径", ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float WaterCanRadiusPerLevel = 1.4142f;

	/** 水壶最高等级。1 + 9×√2 ≈ 13.7 格半径。 */
	UPROPERTY(Config, EditAnywhere, Category = "水壶",
		meta = (DisplayName = "最高等级", ClampMin = "0", UIMin = "0", UIMax = "30"))
	int32 WaterCanMaxLevel = 10;

	/** 水壶升级价格 = Base × Growth^当前等级。 */
	UPROPERTY(Config, EditAnywhere, Category = "水壶",
		meta = (DisplayName = "升级基准价", ClampMin = "1", UIMin = "1", UIMax = "200"))
	int32 WaterCanUpgradeBaseCost = 10;

	UPROPERTY(Config, EditAnywhere, Category = "水壶",
		meta = (DisplayName = "升级涨价系数", ClampMin = "1.0", UIMin = "1.0", UIMax = "3.0"))
	float WaterCanUpgradeCostGrowth = 1.6f;

	// ====================== 镰刀 ======================
	//
	// 和水壶完全对称：0 级只割光标那一格，升级之后一次割一片圆。
	// 水壶管"种草"，镰刀管"收草"，两条升级线对着走，玩家才会有"先扩哪边"的取舍。

	/** 镰刀 0 级时的收割半径（格）。0 = 只割光标那一格。 */
	UPROPERTY(Config, EditAnywhere, Category = "镰刀",
		meta = (DisplayName = "初始半径", ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float ScytheBaseRadius = 0.f;

	/** 每升一级收割半径加多少格。默认比水壶小一档（割草是收益动作，范围太大就太强）。 */
	UPROPERTY(Config, EditAnywhere, Category = "镰刀",
		meta = (DisplayName = "每级加半径", ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float ScytheRadiusPerLevel = 1.0f;

	/** 镰刀最高等级。1 + 8×1 = 9 格半径，割一片能覆盖全图 1/45 左右。 */
	UPROPERTY(Config, EditAnywhere, Category = "镰刀",
		meta = (DisplayName = "最高等级", ClampMin = "0", UIMin = "0", UIMax = "30"))
	int32 ScytheMaxLevel = 8;

	/** 镰刀升级价格 = Base × Growth^当前等级。 */
	UPROPERTY(Config, EditAnywhere, Category = "镰刀",
		meta = (DisplayName = "升级基准价", ClampMin = "1", UIMin = "1", UIMax = "200"))
	int32 ScytheUpgradeBaseCost = 15;

	UPROPERTY(Config, EditAnywhere, Category = "镰刀",
		meta = (DisplayName = "升级涨价系数", ClampMin = "1.0", UIMin = "1.0", UIMax = "3.0"))
	float ScytheUpgradeCostGrowth = 1.6f;

	// ====================== 生态币 ======================

	/** 用镰刀割一颗成熟草得多少币（文档基线 = 1）。 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "割草收益", ClampMin = "0", UIMin = "0", UIMax = "50"))
	int32 CoinPerGrassHarvest = 1;

	/**
	 * 狐狸吃一只成年兔的**基础**收益（文档基线 = 1）。
	 * 实际收益 = (基础 + 升级线 A 等级 × CoinPerBaseLevel) × 可能翻倍。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "捕食基础收益", ClampMin = "0", UIMin = "0", UIMax = "50"))
	int32 CoinPerAdultPredation = 1;

	/** 升级线 A 每级的收益加成（文档写「+1」，这里就是 +1）。 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "升级线 A 每级 +", ClampMin = "0", UIMin = "0", UIMax = "20"))
	int32 CoinPerBaseLevel = 1;

	/** 升级线 B 每级增加的「收获翻倍」概率。文档写 1%，但那样 10 级才 10%，太弱。 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "升级线 B 每级 +", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.5"))
	float DoubleChancePerLevel = 0.05f;

	/**
	 * 两条升级线的最高等级。
	 *
	 * 必须设上限：没有上限的话收益会一直涨到"点一下就通关"，
	 * 玩家也就没有"该不该升"的取舍了。10 级封顶 = 每级 +1 收益 / +5% 翻倍率。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "升级线最高等级", ClampMin = "1", UIMin = "1", UIMax = "50"))
	int32 CoinUpgradeMaxLevel = 10;

	/** 升级线 A / B 的升级基准价与涨价系数。第一次升级花这个价，之后按系数递增。 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "升级线基准价", ClampMin = "1", UIMin = "1", UIMax = "100000"))
	int32 CoinUpgradeBaseCost = 1000;

	/**
	 * 升级线涨价系数。
	 *
	 * 现在是 1.15：配合 1000 的基准价走成
	 * 1000 / 1150 / 1322 / 1520 / 1748 …
	 * 前几级便宜、越往后越贵，节奏比固定倍数平滑。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "升级线涨价系数", ClampMin = "1.0", UIMin = "1.0", UIMax = "4.0"))
	float CoinUpgradeCostGrowth = 1.15f;

	/**
	 * 每买一只同种生物，价格乘这个系数。
	 *
	 * 价格 = 基准价 × 本系数^该物种已买数量：
	 * 兔子 30 起 → 34 → 40 → 46 …；狐狸 150 起 → 172 → 198 …
	 * 这样越往后扩张越贵，玩家必须在"多养"和"升级"之间取舍。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "购买涨价系数", ClampMin = "1.0", UIMin = "1.0", UIMax = "3.0"))
	float CreaturePriceGrowth = 1.15f;

	/** 买一只兔子多少币（文档基线 = 5）。这是**第一只**的价格，之后按涨价系数递增。 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "兔子基准价", ClampMin = "0", UIMin = "0", UIMax = "200"))
	int32 RabbitPrice = 5;

	/** 买一只狐狸多少币（文档基线 = 20）。这是**第一只**的价格，之后按涨价系数递增。 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "狐狸基准价", ClampMin = "0", UIMin = "0", UIMax = "500"))
	int32 FoxPrice = 20;

	/** 通关里程碑：累计获得这么多生态币（文档基线 = 1000）。 */
	UPROPERTY(Config, EditAnywhere, Category = "生态币",
		meta = (DisplayName = "目标币数", ClampMin = "1", UIMin = "10", UIMax = "100000"))
	int32 GoalCoins = 1000;

	// ====================== 天气 ======================

	/**
	 * 满降雨时每秒随机浇湿多少格。降雨仍然由玩家调，
	 * 相当于一台「免费的自动洒水器」。
	 * 0 = 下雨不影响地块（纯视觉）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "天气",
		meta = (DisplayName = "下雨浇水速度", ClampMin = "0.0", UIMin = "0.0", UIMax = "500.0"))
	float RainWaterPerSecond = 60.f;

	// ---------------------- 降雨升级 ----------------------
	//
	// 开局不下雨（InitialRain=0），想让草自己长就得花币"买雨"。
	// 每级 +1%，100 级拉满 —— 是个长期投入，和"收益线 A/B"抢同一份币。

	/** 每次升级提升的降雨量（0.01 = 1%）。 */
	UPROPERTY(Config, EditAnywhere, Category = "天气",
		meta = (DisplayName = "降雨每级 +", ClampMin = "0.001", ClampMax = "1.0", UIMin = "0.001", UIMax = "0.1"))
	float RainPerLevel = 0.01f;

	/** 降雨升级的最高等级（默认 100 级 = 最多 +100%）。 */
	UPROPERTY(Config, EditAnywhere, Category = "天气",
		meta = (DisplayName = "降雨升级最高等级", ClampMin = "1", UIMin = "1", UIMax = "100"))
	int32 RainUpgradeMaxLevel = 100;

	/** 降雨升级的基准价（第一次升级花这个价）。 */
	UPROPERTY(Config, EditAnywhere, Category = "天气",
		meta = (DisplayName = "降雨升级基准价", ClampMin = "1", UIMin = "1", UIMax = "100000"))
	int32 RainUpgradeBaseCost = 1000;

	/** 降雨升级的涨价系数：每升一级，下一次价格乘这个数。 */
	UPROPERTY(Config, EditAnywhere, Category = "天气",
		meta = (DisplayName = "降雨升级涨价系数", ClampMin = "1.0", UIMin = "1.0", UIMax = "3.0"))
	float RainUpgradeCostGrowth = 1.15f;

	// ====================== 相机 ======================
	//
	// 为什么相机的参数也放在这里：相机 Pawn 是 GameMode 用 DefaultPawnClass
	// **运行时生成**的，它自己的 UPROPERTY 在细节面板里根本够不着 ——
	// 放在 Pawn 上等于"只能改代码再重编译"。放这里才能一边玩一边调。

	/**
	 * 水平视场角（度）。
	 *
	 * ⚠ 它和渲染层的 3D/2D 切换阈值是**绑定**的：
	 * 可见范围 ≈ 2 × 摇臂长 × tan(FOV/2)，相机高度 = 摇臂长 × sin(|俯角|)。
	 * 改 FOV 就必须去渲染器上重新算 LODSwitchFarHeight / LODSwitchNearHeight，
	 * 否则一进游戏就直接切成 2D 贴片。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "视场角 FOV", ClampMin = "20.0", ClampMax = "120.0", UIMin = "30.0", UIMax = "90.0"))
	float CameraFOV = 50.f;

	/** 俯角（度，负值 = 从上往下看）。改它同样会影响相机高度和 LOD 阈值。 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "俯角", ClampMin = "-89.0", ClampMax = "-5.0", UIMin = "-75.0", UIMax = "-25.0"))
	float CameraPitch = -55.f;

	/** 开局摇臂长度（厘米）。越大看得越远，但拖拽的"每像素多少厘米"会等比变大。 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "开局摇臂长", ClampMin = "100.0", UIMin = "2000.0", UIMax = "20000.0"))
	float CameraStartArmLength = 9500.f;

	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "最近摇臂长", ClampMin = "50.0", UIMin = "100.0", UIMax = "3000.0"))
	float CameraMinArmLength = 400.f;

	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "最远摇臂长", ClampMin = "1000.0", UIMin = "5000.0", UIMax = "60000.0"))
	float CameraMaxArmLength = 22000.f;

	/** 开局偏航角（度）。45 度是"斜 45 度看"的经典上帝视角。 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "开局朝向", ClampMin = "0.0", ClampMax = "360.0", UIMin = "0.0", UIMax = "360.0"))
	float CameraStartYaw = 45.f;

	/** WASD 平移速度（厘米/秒，会随摇臂长度等比缩放，拉远走得快）。 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "键盘平移速度", ClampMin = "100.0", UIMin = "500.0", UIMax = "20000.0"))
	float CameraPanSpeed = 3500.f;

	/** Q/E 旋转速度（度/秒）。 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "旋转速度", ClampMin = "5.0", UIMin = "20.0", UIMax = "180.0"))
	float CameraRotateSpeed = 60.f;

	/**
	 * 右键拖拽灵敏度的倍率。
	 *
	 * 1.0 = 严格「抓地」：光标按住的那个地面点会一直停在光标底下（1:1 跟手）。
	 * 这是最"正确"的手感，但视野拉远时你会觉得慢 —— 横穿 128 米的地图要拖将近两个屏幕宽。
	 * 觉得慢就往 1.3 / 1.5 调，觉得飘就往下调。**每帧读，改完当帧生效。**
	 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "拖拽灵敏度", ClampMin = "0.2", UIMin = "0.5", UIMax = "3.0"))
	float CameraDragSensitivity = 1.25f;

	/** 滚轮一格等于多少步（每一步 = 1.18 倍摇臂长度）。 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "滚轮缩放步长", ClampMin = "0.1", UIMin = "0.2", UIMax = "3.0"))
	float CameraWheelZoomStep = 1.0f;

	/** 相机可以移动出地图边界多少厘米（防止把镜头推到看不见地图的地方）。 */
	UPROPERTY(Config, EditAnywhere, Category = "相机",
		meta = (DisplayName = "出界余量", ClampMin = "0.0", UIMin = "0.0", UIMax = "20000.0"))
	float CameraPanMargin = 3000.f;

	// ====================== 生物 ======================

	/**
	 * 一个格子最多能站几只生物。
	 *
	 * 原来是硬编码的"一格一只"（上限 = 格数 4096）。放开之后同一片地能挤更多，
	 * 但**别开太大**：
	 *  - 渲染上同格的多只靠各自的随机相位错开，挤太多视觉上会糊成一团；
	 *  - 生态上草的生长是"被踩着长一半"，一格挤满会让草几乎长不起来。
	 * 4 只 / 格 = 最多约 1.6 万只，已经比原来多 4 倍。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "生物",
		meta = (DisplayName = "单格生物上限", ClampMin = "1", UIMin = "1", UIMax = "32"))
	int32 MaxCreaturesPerCell = 4;

	// ====================== 基因 ======================

	/**
	 * 繁殖时的变异幅度（相对值）。
	 * 0.12 = 子代的每个基因在父母基础上 ±12% 随机浮动。
	 * 调 0 就是完全克隆，调大则种群个性差异更极端。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "基因",
		meta = (DisplayName = "变异幅度", ClampMin = "0.0", UIMin = "0.0", UIMax = "0.5"))
	float MutateSigma = 0.12f;
};

/**
 * 取生态箱设置的单例入口。
 *
 * 所有可调数值都放在 UEcoBoxSettings 里
 * （编辑器 -> Project Settings -> Game -> EcoBox 生态箱）。
 *
 * 每次用到时实时读一遍，所以在项目设置里改完**当帧就生效** ——
 * 不用停 PIE，更不用重新编译。
 */
namespace EcoParams
{
	inline const UEcoBoxSettings& S()
	{
		return *GetDefault<UEcoBoxSettings>();
	}
}
