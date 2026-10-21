// EcoBox - 模拟层实现
#include "EcoBoxSimSubsystem.h"

#include "EcoBoxSaveGame.h"
#include "EcoBoxSettings.h"

#include "Kismet/GameplayStatics.h"


// =====================================================================
// 生命周期
// =====================================================================

void UEcoBoxSimSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Rng.Initialize((int32)(FDateTime::Now().GetTicks() & 0x7FFFFFFF));

	Cells.Init(FECell(), EcoBoxConst::CellCount);
	Creatures.Reserve(EcoBoxConst::MaxCreatures);
	FreeSlots.Reserve(256);
	RebuildCellOccupants();   // 空世界也要把反查表清干净

	Weather = FEWeather();
	// 开局降雨默认给一点点（见项目设置「开局降雨」）：
	// 这点雨只够养活很有限的草，让世界不至于一分钟内全干死；想扩大规模还得自己浇。
	Weather.Rain = EcoParams::S().InitialRain;
	Weather.Temperature = 20.f;
	Weather.TimeOfDay = 0.30f;
	Weather.DayLengthSeconds = 120.f;
	Weather.Sunlight = FMath::Clamp(FMath::Sin(Weather.TimeOfDay * PI), 0.f, 1.f);

	SimSpeed = 1.f;
	Accum = 0.f;
	SimTime = 0.f;
	TotalBirths = 0;
	TotalDeaths = 0;
	TotalStarved = 0;
	TotalEaten = 0;

	Economy = FEEconomy();
	CurrentTool = EPlayerTool::WaterCan;

	CoinBuckets.Init(0.f, IncomeBuckets);
	CoinBucketIndex = 0;
	CoinBucketTimer = 0.f;

	SeedGrass(EcoParams::S().InitialGrassCoverage);
	RefreshStats();

	bReady = true;

	const UEcoBoxSettings& S = EcoParams::S();
	UE_LOG(LogTemp, Warning,
		TEXT("[EcoBox] 模拟子系统就绪：%d x %d = %d 格；草 %.1fs 生长 / %.1fs 枯萎 / 地块 %.1fs 变干；开局湿润草 %.1f%%"),
		EcoBoxConst::GridSize, EcoBoxConst::GridSize, EcoBoxConst::CellCount,
		S.GrassGrowSeconds, S.GrassWiltSeconds, S.TileDrySeconds,
		S.InitialGrassCoverage * 100.f);
}

void UEcoBoxSimSubsystem::Deinitialize()
{
	bReady = false;
	Cells.Empty();
	Creatures.Empty();
	FreeSlots.Empty();
	Super::Deinitialize();
}

TStatId UEcoBoxSimSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEcoBoxSimSubsystem, STATGROUP_Tickables);
}

// =====================================================================
// 时间推进：固定步长，与帧率解耦
// =====================================================================

void UEcoBoxSimSubsystem::Tick(float DeltaTime)
{
	if (!bReady || SimSpeed <= 0.f)
	{
		return;
	}

	Accum += DeltaTime * SimSpeed;

	int32 Steps = 0;
	while (Accum >= EcoBoxConst::StepSeconds && Steps < EcoBoxConst::MaxStepsPerFrame)
	{
		StepSim(EcoBoxConst::StepSeconds);
		Accum -= EcoBoxConst::StepSeconds;
		++Steps;
	}

	// 追不上就丢帧，绝不让累积时间无限堆积
	if (Steps >= EcoBoxConst::MaxStepsPerFrame)
	{
		Accum = 0.f;
	}
}

void UEcoBoxSimSubsystem::StepSim(float Dt)
{
	SimTime += Dt;
	AdvanceIncomeWindow(Dt);
	UpdateWeatherSim(Dt);
	UpdateGrassSim(Dt);
	UpdateCreaturesSim(Dt);

	// 统计比较贵（要扫全图），每 4 步刷新一次，即每 1 模拟秒
	static int32 StatCounter = 0;
	if ((++StatCounter & 3) == 0)
	{
		RefreshStats();

		// 每 30 模拟秒打一条生态快照，方便在 Output Log 里看种群与经济曲线
		if (SimTime - LastStatLogTime >= 30.f)
		{
			LastStatLogTime = SimTime;
			UE_LOG(LogTemp, Log,
				TEXT("[EcoBox] t=%.0fs  兔 %d(成年 %d)  狐 %d  成熟草 %d  湿润 %d  币 %d(累计 %d, %.0f/分)  生 %d 死 %d(饿 %d 被吃 %d)  升级 A%d/B%d"),
				SimTime, Stats.Rabbits, Stats.AdultRabbits, Stats.Foxes,
				Stats.MatureGrass, Stats.WetTiles,
				Economy.Coins, Economy.LifetimeCoins, Stats.CoinsPerMinute,
				Stats.Births, Stats.Deaths, Stats.Starved, Stats.Eaten,
				Economy.CoinBaseLevel, Economy.DoubleChanceLevel);
		}
	}
}

void UEcoBoxSimSubsystem::UpdateWeatherSim(float Dt)
{
	if (Weather.DayLengthSeconds > 1.f)
	{
		Weather.TimeOfDay = FMath::Fmod(
			Weather.TimeOfDay + Dt / Weather.DayLengthSeconds, 1.f);
		if (Weather.TimeOfDay < 0.f) { Weather.TimeOfDay += 1.f; }
	}

	// 日照强度：正午 1，午夜 0
	Weather.Sunlight = FMath::Clamp(FMath::Sin(Weather.TimeOfDay * PI), 0.f, 1.f);
}

/**
 * 草与地块。
 *
 * **每步全量更新 4096 格。**
 * 以前这里是「轮转更新」（每 16 步才处理 1/16 的格子）来省性能，
 * 但那样每个格子的计时精度只有 16 × 0.25s = 4 秒，
 * 而新玩法要求精确到「5 秒生长 / 30 秒枯萎 / 10 秒变干」—— 精度根本不够。
 * 每步 4096 次浮点减法 + 分支，开销可以忽略，直接全量算。
 */
void UEcoBoxSimSubsystem::UpdateGrassSim(float Dt)
{
	// 下雨 = 免费的自动洒水器
	ApplyRainWatering(Dt);

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		TickCell(i, Dt);
	}
}

/** 按当前降雨强度随机浇湿一批地块。 */
void UEcoBoxSimSubsystem::ApplyRainWatering(float Dt)
{
	const UEcoBoxSettings& S = EcoParams::S();
	if (S.RainWaterPerSecond <= 0.f || Weather.Rain <= 0.01f) { return; }

	const float PerStep = Weather.Rain * S.RainWaterPerSecond * Dt;
	int32 Count = FMath::FloorToInt(PerStep);
	// 小数部分按概率补一格，低降雨时也能缓慢润湿（不会一次都不浇）
	if (Rng.FRand() < (PerStep - (float)Count)) { ++Count; }

	for (int32 k = 0; k < Count; ++k)
	{
		WaterCell(Rng.RandRange(0, EcoBoxConst::CellCount - 1));
	}
}

void UEcoBoxSimSubsystem::WaterCell(int32 CellIdx)
{
	if (!Cells.IsValidIndex(CellIdx)) { return; }

	FECell& Cell = Cells[CellIdx];
	Cell.bWet = true;
	Cell.WetTimeLeft = FMath::Max(0.25f, EcoParams::S().TileDrySeconds);
}

void UEcoBoxSimSubsystem::SetGrassStage(int32 CellIdx, EGrassStage Stage)
{
	if (!Cells.IsValidIndex(CellIdx)) { return; }

	Cells[CellIdx].Stage = Stage;
	Cells[CellIdx].StageTime = 0.f;
	RefreshCellVisual(Cells[CellIdx]);
}

void UEcoBoxSimSubsystem::ClearGrassAt(int32 CellIdx)
{
	SetGrassStage(CellIdx, EGrassStage::Empty);
}

/** 由阶段和阶段计时推出视觉草量，渲染层直接读 FECell::Grass。 */
void UEcoBoxSimSubsystem::RefreshCellVisual(FECell& Cell) const
{
	const UEcoBoxSettings& S = EcoParams::S();

	switch (Cell.Stage)
	{
	case EGrassStage::Growing:
	{
		const float T = FMath::Clamp(Cell.StageTime / FMath::Max(0.05f, S.GrassGrowSeconds), 0.f, 1.f);
		Cell.Grass = (uint8)FMath::Clamp(FMath::RoundToFloat(EcoBoxConst::MaxGrass * T), 0.f, EcoBoxConst::MaxGrass);
		break;
	}

	case EGrassStage::Mature:
	{
		// 枯萎前的那几秒把视觉量压下来（变矮），给玩家一个"赶紧收"的信号
		const float Warn = FMath::Max(0.f, S.GrassWiltWarnSeconds);
		const float Left = FMath::Max(0.f, S.GrassWiltSeconds - Cell.StageTime);
		const float T = (Warn > 0.f) ? FMath::Clamp(Left / Warn, 0.f, 1.f) : 1.f;
		const float V = EcoBoxConst::MaxGrass * (0.35f + 0.65f * T);
		Cell.Grass = (uint8)FMath::Clamp(FMath::RoundToFloat(V), 0.f, EcoBoxConst::MaxGrass);
		break;
	}

	default:
		Cell.Grass = 0;
		break;
	}
}

/** 单个地块推进一个步长：湿度倒计时 + 草的生命周期。 */
void UEcoBoxSimSubsystem::TickCell(int32 CellIdx, float Dt)
{
	const UEcoBoxSettings& S = EcoParams::S();
	FECell& Cell = Cells[CellIdx];

	// ---- 地块湿度 ----
	if (Cell.bWet)
	{
		Cell.WetTimeLeft -= Dt;
		if (Cell.WetTimeLeft <= 0.f)
		{
			Cell.bWet = false;
			Cell.WetTimeLeft = 0.f;
		}
	}

	// ---- 草的生命周期 ----
	switch (Cell.Stage)
	{
	case EGrassStage::Empty:
		// 只要地还湿就自己开始长一茬。
		// 「被兔子吃掉之后重新长」也是靠这一条 —— 吃完变 Empty，湿着就立刻重来。
		if (Cell.bWet)
		{
			Cell.Stage = EGrassStage::Growing;
			Cell.StageTime = 0.f;
			RefreshCellVisual(Cell);
		}
		break;

	case EGrassStage::Growing:
	{
		// 被生物踩着的格子长得慢一半（牧食压力），但不是完全停
		// 一格可多只：只要这格里有生物就算"被踩"
		const float Rate = (Cell.OccupantCount > 0) ? 0.5f : 1.f;
		Cell.StageTime += Dt * Rate;

		if (Cell.StageTime >= S.GrassGrowSeconds)
		{
			Cell.Stage = EGrassStage::Mature;
			Cell.StageTime = 0.f;
		}
		RefreshCellVisual(Cell);
		break;
	}

	case EGrassStage::Mature:
		Cell.StageTime += Dt;

		// 成熟后一直没人吃也没人割 → 枯萎消失。
		// 地块还湿的话，下一个 TickCell 会立刻重新长一茬。
		if (Cell.StageTime >= S.GrassWiltSeconds)
		{
			Cell.Stage = EGrassStage::Empty;
			Cell.StageTime = 0.f;
		}
		RefreshCellVisual(Cell);
		break;
	}
}

void UEcoBoxSimSubsystem::UpdateCreaturesSim(float Dt)
{
	// 快照数量：本步新生的个体留到下一步再更新
	const int32 Count = FMath::Min(Creatures.Num(), EcoBoxConst::MaxCreatures);

	for (int32 i = 0; i < Count; ++i)
	{
		if (Creatures.IsValidIndex(i) && Creatures[i].bAlive)
		{
			UpdateCreature(i, Dt);
		}
	}
}

/**
 * 单只生物的一步。
 *
 * 动作是**互斥**的：同一时刻只做一件事（走 / 吃 / 待机），
 * 每件事有固定时长，做完才选下一件。这样：
 *   - 每种动作都能对上一段动画；
 *   - 高倍速下动作的**次序**和 1 倍速完全一样，只是发生得更快；
 *   - "吃"需要时间，所以进食期间生物是停在原地的（也就能被狐狸逮到）。
 */
void UEcoBoxSimSubsystem::UpdateCreature(int32 Idx, float Dt)
{
	// ---- 第一段：老化、成长、代谢、死亡 ----
	{
		FECreature& C = Creatures[Idx];

		C.Age += Dt;
		C.Energy -= C.Metabolism * Dt;
		C.ReproCooldown = FMath::Max(0.f, C.ReproCooldown - Dt);

		// 成长：Age 到 GrowSeconds 就算成年；体型同步从 0.55 长到 1.0。
		// 成年与否只影响一件事 —— 被狐狸吃掉时给 60 还是 20 能量、产不产币。
		const float Grow = FMath::Max(0.05f, C.GrowSeconds);
		C.bAdult = (C.Age >= Grow);
		C.GrowScale = FMath::Lerp(0.55f, 1.f, FMath::Clamp(C.Age / Grow, 0.f, 1.f));

		if (C.Energy <= 0.f || C.Age >= C.MaxAge)
		{
			KillCreature(Idx);
			return;
		}
	}

	// 走路途中不繁殖；站定（进食 / 待机）时才考虑。
	// 注意 TryReproduce 可能给 Creatures 扩容，之后必须重新取引用。
	if (Creatures[Idx].State != ECreatureState::Moving)
	{
		TryReproduce(Idx);
		if (!Creatures[Idx].bAlive) { return; }
	}

	// ---- 第二段：动作状态机 ----
	switch (Creatures[Idx].State)
	{
	case ECreatureState::Moving:
	{
		FECreature& C = Creatures[Idx];
		const float Duration = FMath::Max(0.05f, C.MoveSeconds);

		C.StateTime += Dt;
		C.MoveProgress = FMath::Min(1.f, C.MoveProgress + Dt / Duration);
		if (C.MoveProgress < 1.f)
		{
			return;   // 还在路上
		}

		// 到格了
		C.MoveProgress = 0.f;
		const int32 Dest = C.TargetCell;
		C.TargetCell = INDEX_NONE;

		// 狐狸咬到兔子会直接返回 true（那一口要停下来吃完）
		const bool bPreyMeal = Cells.IsValidIndex(Dest) && EnterCell(Idx, Dest);
		if (!Creatures[Idx].bAlive) { return; }   // 半路被狐狸吃了

		if (bPreyMeal) { return; }

		// 兔子：落在有草的格子上就停下来吃一口
		if (BeginGrassMealIfAny(Idx)) { return; }

		StartNextMove(Idx);
		return;
	}

	case ECreatureState::Eating:
	{
		FECreature& C = Creatures[Idx];
		C.StateTime += Dt;

		if (C.StateTime < FMath::Max(0.05f, C.EatSeconds))
		{
			return;   // 还没吃完
		}

		FinishMeal(Idx);
		if (!Creatures[Idx].bAlive) { return; }

		StartNextMove(Idx);
		return;
	}

	default:
		// Idle：上一步八面被围，这一步再试一次
		StartNextMove(Idx);
		return;
	}
}

/**
 * 选下一格并开始走。
 * 顺带把朝向定下来 —— 按「当前格 → 目标格」的方向算 Yaw，
 * 整个移动过程中保持不变（所以生物是**面朝前进方向**走的，而不是固定朝一个方向）。
 */
void UEcoBoxSimSubsystem::StartNextMove(int32 Idx)
{
	if (!Creatures.IsValidIndex(Idx) || !Creatures[Idx].bAlive)
	{
		return;
	}

	FECreature& C = Creatures[Idx];
	const int32 Next = PickTargetCell(C);

	if (Next == INDEX_NONE)
	{
		// 八面都被占了，原地待命，下一步再试
		C.State = ECreatureState::Idle;
		C.StateTime = 0.f;
		C.TargetCell = INDEX_NONE;
		C.MoveProgress = 0.f;
		return;
	}

	if (Cells.IsValidIndex(C.CellIdx))
	{
		const FVector Dir = (CellToWorld(Next) - CellToWorld(C.CellIdx)).GetSafeNormal2D();
		if (!Dir.IsNearlyZero())
		{
			C.FacingYaw = Dir.Rotation().Yaw;
		}
	}

	C.TargetCell = Next;
	C.MoveProgress = 0.f;
	C.StateTime = 0.f;
	C.State = ECreatureState::Moving;
}

/** 兔子落在**成熟**草格上就停下来吃。返回是否进入进食。 */
bool UEcoBoxSimSubsystem::BeginGrassMealIfAny(int32 Idx)
{
	if (!Creatures.IsValidIndex(Idx) || !Creatures[Idx].bAlive)
	{
		return false;
	}

	FECreature& C = Creatures[Idx];
	if (C.Type != ECreatureType::Rabbit) { return false; }
	if (!Cells.IsValidIndex(C.CellIdx)) { return false; }

	// 只有长成熟的草能吃。生长中的不能吃（否则玩家刚浇完水就被啃光，看不到过程）。
	if (Cells[C.CellIdx].Stage != EGrassStage::Mature) { return false; }

	C.State = ECreatureState::Eating;
	C.StateTime = 0.f;
	return true;
}

/**
 * 吃完一口，结算收益。
 * 兔子的草是**在这时候**才扣掉的 —— 它站在这一格上吃，期间不可能有别的生物来抢
 * （一格只允许一个占位），所以不会算错。
 */
void UEcoBoxSimSubsystem::FinishMeal(int32 Idx)
{
	if (!Creatures.IsValidIndex(Idx) || !Creatures[Idx].bAlive)
	{
		return;
	}

	FECreature& C = Creatures[Idx];

	if (C.Type == ECreatureType::Rabbit && Cells.IsValidIndex(C.CellIdx))
	{
		FECell& Cell = Cells[C.CellIdx];

		if (Cell.Stage == EGrassStage::Mature)
		{
			C.Energy = FMath::Min(EcoBoxConst::MaxEnergy,
				C.Energy + EcoParams::S().RabbitEnergyPerBite);

			// 一口吃掉整格草。地块要是还湿着，下一步会自动重新长一茬。
			Cell.Stage = EGrassStage::Empty;
			Cell.StageTime = 0.f;
			RefreshCellVisual(Cell);
		}
	}

	// 狐狸的能量在咬死猎物的那一刻就已经给了（见 EnterCell），这里不用再给
	C.State = ECreatureState::Idle;
	C.StateTime = 0.f;
}

/**
 * 8 邻居打分选目标。
 * 不用 A*，因为每格最多一个生物、视野只有一格，贪心就已经足够"像活的"。
 * 那个 FRand 噪声是必须的：纯取最大值会让所有兔子沿直线排成一列往前走。
 */
int32 UEcoBoxSimSubsystem::PickTargetCell(const FECreature& Creature)
{
	static const int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int32 DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

	if (!Cells.IsValidIndex(Creature.CellIdx))
	{
		return INDEX_NONE;
	}

	const int32 CX = CellX(Creature.CellIdx);
	const int32 CY = CellY(Creature.CellIdx);

	// ---- 狐狸：先在感知半径内找最近的兔子 ----
	// 只靠 8 邻居是不够的：狐狸走到兔子旁边才发现猎物，而兔子也在动，
	// 结果就是永远追不上（实测 50 秒一次都吃不到）。有了这个"视野"，
	// 狐狸才会朝猎物移动，形成真正的追击。
	bool bHasPrey = false;
	int32 PreyX = 0;
	int32 PreyY = 0;

	if (Creature.Type == ECreatureType::Fox)
	{
		const int32 R = FMath::Clamp(EcoParams::S().FoxSenseRadius, 1, EcoBoxConst::GridSize);
		int32 BestD2 = TNumericLimits<int32>::Max();

		for (int32 dy = -R; dy <= R; ++dy)
		{
			for (int32 dx = -R; dx <= R; ++dx)
			{
				const int32 X = CX + dx;
				const int32 Y = CY + dy;
				if (!IsValidCoord(X, Y)) { continue; }

				// 一格可多只：这格里只要有一只活的兔子就算"看见猎物"
				if (FindLiveCreatureInCell(CellIndex(X, Y), ECreatureType::Rabbit) == INDEX_NONE) { continue; }

				const int32 D2 = dx * dx + dy * dy;
				if (D2 < BestD2)
				{
					BestD2 = D2;
					PreyX = X;
					PreyY = Y;
					bHasPrey = true;
				}
			}
		}
	}

	int32 BestIdx = INDEX_NONE;
	float BestScore = -FLT_MAX;

	// 一格可多只：目标格的"还能不能再进一只"按这个上限判断
	const int32 MaxPerCell = FMath::Max(1, EcoParams::S().MaxCreaturesPerCell);

	for (int32 d = 0; d < 8; ++d)
	{
		const int32 NX = CX + DX[d];
		const int32 NY = CY + DY[d];
		if (!IsValidCoord(NX, NY)) { continue; }

		const int32 N = CellIndex(NX, NY);
		const FECell& Cell = Cells[N];
		const bool bCellFull = (Cell.OccupantCount >= MaxPerCell);

		if (Creature.Type == ECreatureType::Rabbit)
		{
			if (bCellFull) { continue; }   // 挤满了就不去

			// 成熟草最优先；正在长的草给一点点分，让兔子愿意往那边走等着。
			// 那个 FRand 噪声是必须的：纯取最大值会让所有兔子沿直线排成一列往前走。
			float Score = Rng.FRand() * 3.f;
			if (Cell.Stage == EGrassStage::Mature)       { Score += 100.f; }
			else if (Cell.Stage == EGrassStage::Growing) { Score += 5.f; }

			if (Score > BestScore) { BestScore = Score; BestIdx = N; }
		}
		else
		{
			// 贴脸有兔子 → 直接扑过去（不管格子挤不挤，先吃）
			if (FindLiveCreatureInCell(N, ECreatureType::Rabbit) != INDEX_NONE)
			{
				return N;
			}
			if (bCellFull) { continue; }

			// 没贴脸就走空格。看得见猎物时朝它走（离得越近分越高），
			// 看不见就随机游走。噪声保证不会一群狐狸走成一条直线。
			float Score = Rng.FRand();
			if (bHasPrey)
			{
				const float Dist = FMath::Sqrt((float)((NX - PreyX) * (NX - PreyX) + (NY - PreyY) * (NY - PreyY)));
				Score += 200.f - Dist * 10.f;
			}

			if (Score > BestScore) { BestScore = Score; BestIdx = N; }
		}
	}

	return BestIdx;   // INDEX_NONE 表示八面被围，原地待命
}

/**
 * 走进新格：释放旧格、占据新格。
 *
 * 狐狸进格时会**当场咬死**该格的兔子并立刻拿到能量 —— 猎物的死亡必须是瞬时的，
 * 否则"兔子在狐狸嘴边还能继续走路"会很怪。返回值告诉调用者
 * 「咬到了，接下来要停下来花 EatSeconds 把它吃完」。
 *
 * 兔子不在这里吃草：草是停下来慢慢啃的，收益放到 FinishMeal 结算。
 */
bool UEcoBoxSimSubsystem::EnterCell(int32 Idx, int32 NewCell)
{
	if (!Cells.IsValidIndex(NewCell) || !Creatures.IsValidIndex(Idx))
	{
		return false;
	}

	FECreature& C = Creatures[Idx];
	const int32 OldCell = C.CellIdx;

	// 目标格就是当前格：什么都不用做（防止下面的"咬死"误伤自己）
	if (OldCell == NewCell)
	{
		return false;
	}

	bool bPreyMeal = false;

	if (C.Type == ECreatureType::Fox)
	{
		// 一格可多只：从这一格里挑一只活的兔子吃掉
		const int32 Prey = FindLiveCreatureInCell(NewCell, ECreatureType::Rabbit);
		if (Prey != INDEX_NONE)
		{
			const bool bPreyAdult = Creatures[Prey].bAdult;
			KillCreature(Prey, /*bEaten=*/true);   // 会顺手清掉 NewCell 的占位

			const UEcoBoxSettings& S = EcoParams::S();
			const float Gain = bPreyAdult ? S.FoxEnergyPerAdultRabbit : S.FoxEnergyPerYoungRabbit;
			C.Energy = FMath::Min(EcoBoxConst::MaxEnergy, C.Energy + Gain);
			bPreyMeal = true;

			// 只有吃到成年兔才产币。
			// 收益 = (基础 + 升级线 A 等级 × 每级加成) × （按升级线 B 的概率）翻倍。
			// 变量名刻意不叫 Gain —— 上面那个能量 Gain 就在外层作用域，
			// 同名会触发 C4456（局部变量遮蔽），而 UE 把它当错误。
			if (bPreyAdult)
			{
				const int32 Base = GetPredationYield();
				const bool bDouble = Rng.FRand() < GetDoubleChance();
				const int32 CoinGain = bDouble ? Base * 2 : Base;

				AddCoins(CoinGain);
				Economy.CoinsFromPredation += CoinGain;
				++Economy.Predations;
			}
		}
	}

	// 先判断新格能不能进 —— **必须在离开旧格之前判断**。
	// 反过来的话，新格满了就会"旧格已经摘掉、新格没加上"，
	// 这只生物从此不在任何格子的反查表里：狐狸找不到它、草也不再算被踩。
	const int32 MaxPerCell = FMath::Max(1, EcoParams::S().MaxCreaturesPerCell);
	const bool bCanEnter = Cells.IsValidIndex(NewCell) && Cells[NewCell].OccupantCount < MaxPerCell;

	if (bCanEnter)
	{
		// 离开旧格（这一格可能还站着别的生物，所以按"具体这一只"来摘）
		if (Cells.IsValidIndex(OldCell))
		{
			RemoveCreatureFromCell(OldCell, Idx);
		}

		AddCreatureToCell(NewCell, Idx);
		C.CellIdx = NewCell;
	}

	if (bPreyMeal)
	{
		C.MoveProgress = 0.f;
		C.State = ECreatureState::Eating;
		C.StateTime = 0.f;
	}

	return bPreyMeal;
}

void UEcoBoxSimSubsystem::TryReproduce(int32 Idx)
{
	if (!Creatures.IsValidIndex(Idx) || !Creatures[Idx].bAlive)
	{
		return;
	}

	const bool bRabbit = (Creatures[Idx].Type == ECreatureType::Rabbit);

	const float Threshold  = bRabbit ? EcoParams::S().RabbitReproEnergy  : EcoParams::S().FoxReproEnergy;
	const float Cost       = bRabbit ? EcoParams::S().RabbitReproCost    : EcoParams::S().FoxReproCost;
	const float ChildE     = bRabbit ? EcoParams::S().RabbitChildEnergy  : EcoParams::S().FoxChildEnergy;
	const float BaseCool   = bRabbit ? EcoParams::S().RabbitReproCooldown: EcoParams::S().FoxReproCooldown;

	if (Creatures[Idx].ReproCooldown > 0.f) { return; }
	if (Creatures[Idx].Energy < Threshold) { return; }

	// Fertility 用缩放冷却时间来实现，而不是每步掷骰子。
	// 以前每步掷一次、一步 0.25 秒（一秒掷四次），几乎必然通过，基因等于没作用。
	// 现在繁殖倾向高的个体冷却短、生得多，基因才真正参与筛选。
	const float Cooldown = BaseCool * FMath::Clamp(1.7f - Creatures[Idx].Fertility, 0.5f, 1.8f);

	const int32 Free = FindFreeNeighbor(Creatures[Idx].CellIdx);
	if (Free == INDEX_NONE) { return; }

	const int32 Slot = AllocSlot();
	if (Slot == INDEX_NONE) { return; }

	// 先把父母基因拷贝出来，避免 AllocSlot 扩容后引用失效
	const FECreature Parent = Creatures[Idx];

	FECreature& Child = Creatures[Slot];
	Child = FECreature();
	Child.Type = Parent.Type;
	InitGenes(Child, &Parent);
	Child.Energy = ChildE;
	Child.Age = 0.f;
	Child.bAdult = false;          // 幼崽：要长 GrowSeconds 才算成年
	Child.GrowScale = 0.55f;
	Child.ReproCooldown = Cooldown * 0.5f;
	Child.State = ECreatureState::Idle;
	Child.StateTime = 0.f;
	Child.MoveProgress = 0.f;
	Child.TargetCell = INDEX_NONE;
	Child.Offspring = 0;
	Child.bAlive = true;
	Child.CellIdx = Free;
	AddCreatureToCell(Free, Slot);

	Creatures[Idx].Energy -= Cost;
	Creatures[Idx].ReproCooldown = Cooldown;
	Creatures[Idx].Offspring += 1;
	++TotalBirths;
}

int32 UEcoBoxSimSubsystem::FindFreeNeighbor(int32 CellIdx)
{
	static const int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int32 DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

	if (!Cells.IsValidIndex(CellIdx)) { return INDEX_NONE; }

	const int32 CX = CellX(CellIdx);
	const int32 CY = CellY(CellIdx);

	int32 Candidates[8];
	int32 NumCandidates = 0;

	for (int32 d = 0; d < 8; ++d)
	{
		const int32 NX = CX + DX[d];
		const int32 NY = CY + DY[d];
		if (!IsValidCoord(NX, NY)) { continue; }

		const int32 N = CellIndex(NX, NY);
		if (Cells[N].OccupantCount == 0)
		{
			Candidates[NumCandidates++] = N;
		}
	}

	if (NumCandidates == 0) { return INDEX_NONE; }
	return Candidates[Rng.RandRange(0, NumCandidates - 1)];
}

void UEcoBoxSimSubsystem::KillCreature(int32 Idx, bool bEaten)
{
	if (!Creatures.IsValidIndex(Idx) || !Creatures[Idx].bAlive)
	{
		return;
	}

	FECreature& C = Creatures[Idx];

	if (Cells.IsValidIndex(C.CellIdx))
	{
		RemoveCreatureFromCell(C.CellIdx, Idx);
	}

	C.bAlive = false;
	C.TargetCell = INDEX_NONE;
	C.CellIdx = INDEX_NONE;

	FreeSlots.Add(Idx);
	++TotalDeaths;
	if (bEaten) { ++TotalEaten; } else { ++TotalStarved; }
}

int32 UEcoBoxSimSubsystem::AllocSlot()
{
	while (FreeSlots.Num() > 0)
	{
		const int32 Slot = FreeSlots.Pop();
		if (Creatures.IsValidIndex(Slot)) { return Slot; }
	}

	if (Creatures.Num() >= EcoBoxConst::MaxCreatures)
	{
		return INDEX_NONE;
	}
	return Creatures.AddDefaulted();
}

void UEcoBoxSimSubsystem::InitGenes(FECreature& Creature, const FECreature* Parent)
{
	const bool bRabbit = (Creature.Type == ECreatureType::Rabbit);
	const UEcoBoxSettings& S = EcoParams::S();

	const float BaseMove = bRabbit ? S.RabbitMoveSecondsPerCell : S.FoxMoveSecondsPerCell;
	const float BaseMeta = bRabbit ? S.RabbitMetabolism : S.FoxMetabolism;
	const float BaseAge  = bRabbit ? S.RabbitMaxAge : S.FoxMaxAge;

	// 进食时间和成年时长不做成基因：动画时长和成长节奏都要可预测
	Creature.EatSeconds  = S.EatSeconds;
	Creature.GrowSeconds = bRabbit ? S.RabbitGrowSeconds : S.FoxGrowSeconds;

	if (Parent == nullptr)
	{
		// 初代：种族基准值上叠一层随机个性
		Creature.MoveSeconds = BaseMove * Rng.FRandRange(0.80f, 1.25f);
		Creature.Metabolism  = BaseMeta * Rng.FRandRange(0.80f, 1.25f);
		Creature.MaxAge      = BaseAge  * Rng.FRandRange(0.80f, 1.25f);
		Creature.Fertility   = Rng.FRandRange(0.25f, 0.85f);
		Creature.Size        = Rng.FRandRange(0.75f, 1.30f);
	}
	else
	{
		const float Sigma = S.MutateSigma;
		auto Mutate = [this, Sigma](float V)
		{
			return V * (1.f + Rng.FRandRange(-Sigma, Sigma));
		};

		Creature.MoveSeconds = Mutate(Parent->MoveSeconds);
		Creature.Metabolism  = Mutate(Parent->Metabolism);
		Creature.MaxAge      = Mutate(Parent->MaxAge);
		Creature.Fertility   = FMath::Clamp(Mutate(Parent->Fertility), 0.05f, 0.95f);
		Creature.Size        = FMath::Clamp(Mutate(Parent->Size), 0.60f, 1.50f);
	}

	// 注意：MoveSeconds 是"每格多少秒"，**越小走得越快**。
	// 上下限要跟着项目设置放宽，否则改大基准值会被这里夹回去。
	Creature.MoveSeconds = FMath::Clamp(Creature.MoveSeconds, 0.20f, 5.00f);
	Creature.Metabolism  = FMath::Clamp(Creature.Metabolism, 0.10f, 20.0f);
	Creature.MaxAge      = FMath::Clamp(Creature.MaxAge, 60.f, 3600.f);

	// ---- 格内游走（纯视觉）：每只动物给一个互不同步的相位 ----
	// 相位完全随机 → 同一格里的几只动物不会走成整齐的方阵。
	Creature.WanderPhase = Rng.FRandRange(0.f, 2.f * PI);
	// 半径倍率：让有的动物敢贴着格子边缘走，有的只在小范围里挪
	Creature.WanderRadiusScale = Rng.FRandRange(0.55f, 1.0f);
}

// =====================================================================
// 玩家可操纵项
// =====================================================================

void UEcoBoxSimSubsystem::SetSimSpeed(float InSpeed)
{
	SimSpeed = FMath::Clamp(InSpeed, 0.f, 32.f);
}

void UEcoBoxSimSubsystem::CycleSimSpeed(int32 Dir)
{
	static const float Speeds[] = { 0.f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f };
	constexpr int32 NumSpeeds = UE_ARRAY_COUNT(Speeds);

	// 找到当前最接近的档位下标
	int32 Current = 2;
	float BestDist = FLT_MAX;
	for (int32 i = 0; i < NumSpeeds; ++i)
	{
		const float Dist = FMath::Abs(Speeds[i] - SimSpeed);
		if (Dist < BestDist) { BestDist = Dist; Current = i; }
	}

	const int32 Next = FMath::Clamp(Current + (Dir >= 0 ? 1 : -1), 0, NumSpeeds - 1);
	SimSpeed = Speeds[Next];
}

void UEcoBoxSimSubsystem::AddRain(float Delta)
{
	Weather.Rain = FMath::Clamp(Weather.Rain + Delta, 0.f, 1.f);
}

void UEcoBoxSimSubsystem::AddTemperature(float Delta)
{
	Weather.Temperature = FMath::Clamp(Weather.Temperature + Delta, -10.f, 45.f);
}

void UEcoBoxSimSubsystem::AddDayLength(float Delta)
{
	Weather.DayLengthSeconds = FMath::Clamp(Weather.DayLengthSeconds + Delta, 10.f, 600.f);
}

/**
 * 播种：把 Coverage 比例的地块直接设成「湿润 + 成熟草」。
 * 相当于白送一次全图浇水，用来调数值 / 开局起手。
 */
void UEcoBoxSimSubsystem::SeedGrass(float Coverage)
{
	Coverage = FMath::Clamp(Coverage, 0.f, 1.f);
	const int32 Target = FMath::RoundToInt(EcoBoxConst::CellCount * Coverage);

	int32 Placed = 0;
	for (int32 Attempt = 0; Attempt < Target * 12 && Placed < Target; ++Attempt)
	{
		const int32 Cell = Rng.RandRange(0, EcoBoxConst::CellCount - 1);
		if (Cells[Cell].Stage == EGrassStage::Empty && !Cells[Cell].bWet)
		{
			WaterCell(Cell);
			SetGrassStage(Cell, EGrassStage::Mature);

			// 错开枯萎时间。不错开的话开局这批草是同一时刻创建的，
			// 30 秒后会**同时**全部消失，画面上像集体暴毙。
			Cells[Cell].StageTime = Rng.FRandRange(
				0.f, FMath::Max(0.f, EcoParams::S().GrassWiltSeconds * 0.6f));
			RefreshCellVisual(Cells[Cell]);

			++Placed;
		}
	}
	RefreshStats();
}

/** 清光所有草并把地块全部弄干。 */
void UEcoBoxSimSubsystem::ClearGrass()
{
	for (FECell& Cell : Cells)
	{
		Cell.Stage = EGrassStage::Empty;
		Cell.StageTime = 0.f;
		Cell.bWet = false;
		Cell.WetTimeLeft = 0.f;
		Cell.Grass = 0;
	}
	RefreshStats();
}

void UEcoBoxSimSubsystem::ResetWorld(bool bWithGrass)
{
	for (FECell& Cell : Cells)
	{
		Cell = FECell();
	}

	for (FECreature& C : Creatures)
	{
		C.bAlive = false;
		C.CellIdx = INDEX_NONE;
		C.TargetCell = INDEX_NONE;
	}

	FreeSlots.Reset();
	// 生物全清空，格子反查表也要跟着清（一格可多只之后必须显式维护）
	RebuildCellOccupants();
	TotalBirths = 0;
	TotalDeaths = 0;
	TotalStarved = 0;
	TotalEaten = 0;
	SimTime = 0.f;
	Accum = 0.f;

	// 重开也把经济清零，不然"重来一次"没有意义
	Economy = FEEconomy();
	CoinBuckets.Init(0.f, IncomeBuckets);
	CoinBucketIndex = 0;
	CoinBucketTimer = 0.f;

	if (bWithGrass)
	{
		SeedGrass(EcoParams::S().InitialGrassCoverage);
	}
	RefreshStats();
}

int32 UEcoBoxSimSubsystem::SpawnCreatureInternal(
	ECreatureType Type, int32 CellIdx, bool bAdult, const FECreature* Parent)
{
	if (!Cells.IsValidIndex(CellIdx)) { return INDEX_NONE; }
	// 一格可多只：只有挤到上限才拒绝生成
	if (Cells[CellIdx].OccupantCount >= FMath::Max(1, EcoParams::S().MaxCreaturesPerCell)) { return INDEX_NONE; }

	const int32 Slot = AllocSlot();
	if (Slot == INDEX_NONE) { return INDEX_NONE; }

	const bool bRabbit = (Type == ECreatureType::Rabbit);
	const UEcoBoxSettings& S = EcoParams::S();

	FECreature& C = Creatures[Slot];
	C = FECreature();
	C.Type = Type;
	InitGenes(C, Parent);

	C.Energy = bRabbit ? S.RabbitStartEnergy : S.FoxStartEnergy;
	C.ReproCooldown = (bRabbit ? S.RabbitReproCooldown : S.FoxReproCooldown) * 0.5f;

	if (bAdult)
	{
		// 开局投的 / 花钱买的都是成年：Age 刚好卡在成年线上，体型给满
		C.Age = C.GrowSeconds;
		C.bAdult = true;
		C.GrowScale = 1.f;
	}
	else
	{
		C.Age = 0.f;
		C.bAdult = false;
		C.GrowScale = 0.55f;
	}

	C.State = ECreatureState::Idle;
	C.StateTime = 0.f;
	C.MoveProgress = 0.f;
	C.TargetCell = INDEX_NONE;
	C.Offspring = 0;
	C.bAlive = true;
	C.CellIdx = CellIdx;

	AddCreatureToCell(CellIdx, Slot);
	return Slot;
}

int32 UEcoBoxSimSubsystem::SpawnCreature(ECreatureType Type, int32 CellIdx)
{
	// 玩家主动投放的一律成年，投放的当下就有产出
	return SpawnCreatureInternal(Type, CellIdx, /*bAdult=*/true, nullptr);
}

int32 UEcoBoxSimSubsystem::SpawnCreatureRandom(ECreatureType Type, int32 Count)
{
	int32 Made = 0;
	const int32 MaxAttempts = FMath::Max(Count * 24, 64);

	for (int32 i = 0; i < MaxAttempts && Made < Count; ++i)
	{
		const int32 Cell = Rng.RandRange(0, EcoBoxConst::CellCount - 1);
		// 优先铺到空地上（随机投放的本意是"撒开"），空的优先
		if (Cells[Cell].OccupantCount == 0)
		{
			if (SpawnCreature(Type, Cell) != INDEX_NONE) { ++Made; }
		}
	}
	return Made;
}

int32 UEcoBoxSimSubsystem::SpawnOnGrass(ECreatureType Type, int32 Count)
{
	TArray<int32> Candidates;
	Candidates.Reserve(512);

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		// 种草只种空地：有生物的格子没法种
		if (Cells[i].Grass > 0 && Cells[i].OccupantCount == 0)
		{
			Candidates.Add(i);
		}
	}

	// 洗牌，避免总是投在同一片
	for (int32 i = Candidates.Num() - 1; i > 0; --i)
	{
		const int32 j = Rng.RandRange(0, i);
		Candidates.Swap(i, j);
	}

	int32 Made = 0;
	for (int32 i = 0; i < Candidates.Num() && Made < Count; ++i)
	{
		if (SpawnCreature(Type, Candidates[i]) != INDEX_NONE) { ++Made; }
	}

	// 草不够就退化成随机投放
	if (Made < Count)
	{
		Made += SpawnCreatureRandom(Type, Count - Made);
	}
	return Made;
}

// =====================================================================
// 生态币与经济
// =====================================================================

void UEcoBoxSimSubsystem::AddCoins(int32 Amount)
{
	if (Amount <= 0) { return; }

	Economy.Coins += Amount;
	Economy.LifetimeCoins += Amount;   // 花掉的不扣，1000 币目标看的是累计

	PushCoinIncome(Amount);
}

void UEcoBoxSimSubsystem::PushCoinIncome(int32 Amount)
{
	if (Amount <= 0) { return; }
	if (CoinBuckets.Num() != IncomeBuckets) { return; }

	CoinBuckets[CoinBucketIndex] += (float)Amount;
}

void UEcoBoxSimSubsystem::AdvanceIncomeWindow(float Dt)
{
	if (CoinBuckets.Num() != IncomeBuckets) { return; }

	CoinBucketTimer += Dt;
	// 一帧可能追好几步，所以用 while 而不是 if
	while (CoinBucketTimer >= 1.f)
	{
		CoinBucketTimer -= 1.f;
		CoinBucketIndex = (CoinBucketIndex + 1) % IncomeBuckets;
		CoinBuckets[CoinBucketIndex] = 0.f;   // 滚出去的那一秒清零
	}
}

float UEcoBoxSimSubsystem::GetCoinsPerMinute() const
{
	if (CoinBuckets.Num() != IncomeBuckets) { return 0.f; }

	float Sum = 0.f;
	for (const float V : CoinBuckets) { Sum += V; }

	// 窗口还没铺满（刚开局）时按实际经过的时间折算，
	// 否则开局赚 5 个币会一路显示成"5 币/分钟"，看着像在骗人。
	const float Window = FMath::Clamp(SimTime, 1.f, (float)IncomeBuckets);
	return Sum / Window * 60.f;
}

int32 UEcoBoxSimSubsystem::GetHarvestYield() const
{
	const UEcoBoxSettings& S = EcoParams::S();
	return S.CoinPerGrassHarvest + Economy.CoinBaseLevel * S.CoinPerBaseLevel;
}

int32 UEcoBoxSimSubsystem::GetPredationYield() const
{
	const UEcoBoxSettings& S = EcoParams::S();
	return S.CoinPerAdultPredation + Economy.CoinBaseLevel * S.CoinPerBaseLevel;
}

float UEcoBoxSimSubsystem::GetDoubleChance() const
{
	const UEcoBoxSettings& S = EcoParams::S();
	return FMath::Clamp(Economy.DoubleChanceLevel * S.DoubleChancePerLevel, 0.f, 1.f);
}

bool UEcoBoxSimSubsystem::TrySpendCoins(int32 Amount)
{
	if (Amount < 0) { return false; }
	if (Economy.Coins < Amount) { return false; }

	Economy.Coins -= Amount;
	return true;
}

float UEcoBoxSimSubsystem::GetWaterCanRadius() const
{
	const UEcoBoxSettings& S = EcoParams::S();
	const int32 Lv = FMath::Clamp(Economy.WaterCanLevel, 0, S.WaterCanMaxLevel);

	// 保留小数：文档的每级 +√2 会被取整吃掉（1 → 2.41 → 3.83 …）
	return S.WaterCanBaseRadius + S.WaterCanRadiusPerLevel * (float)Lv;
}

int32 UEcoBoxSimSubsystem::GetWaterCanUpgradeCost() const
{
	const UEcoBoxSettings& S = EcoParams::S();
	if (Economy.WaterCanLevel >= S.WaterCanMaxLevel) { return INDEX_NONE; }

	return FMath::RoundToInt(
		S.WaterCanUpgradeBaseCost * FMath::Pow(S.WaterCanUpgradeCostGrowth, (float)Economy.WaterCanLevel));
}

int32 UEcoBoxSimSubsystem::GetCoinUpgradeCost(int32 CurrentLevel) const
{
	const UEcoBoxSettings& S = EcoParams::S();
	if (CurrentLevel >= S.CoinUpgradeMaxLevel) { return INDEX_NONE; }

	return FMath::RoundToInt(
		S.CoinUpgradeBaseCost * FMath::Pow(S.CoinUpgradeCostGrowth, (float)FMath::Max(0, CurrentLevel)));
}

int32 UEcoBoxSimSubsystem::GetRainUpgradeCost() const
{
	const UEcoBoxSettings& S = EcoParams::S();
	if (Economy.RainLevel >= S.RainUpgradeMaxLevel) { return INDEX_NONE; }

	return FMath::RoundToInt(
		S.RainUpgradeBaseCost * FMath::Pow(S.RainUpgradeCostGrowth, (float)Economy.RainLevel));
}

bool UEcoBoxSimSubsystem::UpgradeWaterCan()
{
	const int32 Cost = GetWaterCanUpgradeCost();
	if (Cost == INDEX_NONE) { return false; }
	if (!TrySpendCoins(Cost)) { return false; }

	++Economy.WaterCanLevel;
	return true;
}

float UEcoBoxSimSubsystem::GetScytheRadius() const
{
	const UEcoBoxSettings& S = EcoParams::S();
	const int32 Lv = FMath::Clamp(Economy.ScytheLevel, 0, S.ScytheMaxLevel);

	// 0 级半径 0 时，GetCircleCells 依然会把圆心那一格收进来（距离 0 <= 0），
	// 所以"没升级时只割一格"不需要特判。
	return S.ScytheBaseRadius + S.ScytheRadiusPerLevel * (float)Lv;
}

int32 UEcoBoxSimSubsystem::GetScytheUpgradeCost() const
{
	const UEcoBoxSettings& S = EcoParams::S();
	if (Economy.ScytheLevel >= S.ScytheMaxLevel) { return INDEX_NONE; }

	return FMath::RoundToInt(
		S.ScytheUpgradeBaseCost * FMath::Pow(S.ScytheUpgradeCostGrowth, (float)Economy.ScytheLevel));
}

bool UEcoBoxSimSubsystem::UpgradeScythe()
{
	const int32 Cost = GetScytheUpgradeCost();
	if (Cost == INDEX_NONE) { return false; }
	if (!TrySpendCoins(Cost)) { return false; }

	++Economy.ScytheLevel;
	return true;
}

bool UEcoBoxSimSubsystem::UpgradeCoinBase()
{
	const int32 Cost = GetCoinUpgradeCost(Economy.CoinBaseLevel);
	if (Cost == INDEX_NONE) { return false; }        // 已满级
	if (!TrySpendCoins(Cost)) { return false; }

	++Economy.CoinBaseLevel;
	return true;
}

bool UEcoBoxSimSubsystem::UpgradeCoinDouble()
{
	const int32 Cost = GetCoinUpgradeCost(Economy.DoubleChanceLevel);
	if (Cost == INDEX_NONE) { return false; }        // 已满级
	if (!TrySpendCoins(Cost)) { return false; }

	++Economy.DoubleChanceLevel;
	return true;
}

bool UEcoBoxSimSubsystem::UpgradeRain()
{
	const int32 Cost = GetRainUpgradeCost();
	if (Cost == INDEX_NONE) { return false; }        // 已满级
	if (!TrySpendCoins(Cost)) { return false; }

	++Economy.RainLevel;

	// 升级效果直接加到当前降雨上（不是加"上限"）：
	// 玩家能立刻看到雨变大，草地也开始自己变湿。
	// 玩家之后手动调 Rain 也照样有效 —— 两者共用同一个值，不会互相覆盖。
	Weather.Rain = FMath::Clamp(Weather.Rain + EcoParams::S().RainPerLevel, 0.f, 1.f);

	return true;
}

int32 UEcoBoxSimSubsystem::GetCreaturePrice(ECreatureType Type) const
{
	const UEcoBoxSettings& S = EcoParams::S();
	const bool bRabbit = (Type == ECreatureType::Rabbit);

	const int32 Base   = bRabbit ? S.RabbitPrice : S.FoxPrice;
	const int32 Bought = bRabbit ? Economy.RabbitsBought : Economy.FoxesBought;

	if (Bought <= 0) { return Base; }

	// 每买一只乘一次系数：第 1 只 = 基准价，第 2 只 = 基准 × 系数，依此类推。
	// 兔子、狐狸各自独立计数 —— 买兔子不会让狐狸变贵。
	const float Growth = FMath::Max(1.f, S.CreaturePriceGrowth);
	return FMath::RoundToInt((float)Base * FMath::Pow(Growth, (float)Bought));
}

bool UEcoBoxSimSubsystem::BuyCreature(ECreatureType Type, int32 CellIdx)
{
	if (!Cells.IsValidIndex(CellIdx)) { return false; }
	// 一格可多只：只要还没挤到上限就能买
	if (Cells[CellIdx].OccupantCount >= FMath::Max(1, EcoParams::S().MaxCreaturesPerCell)) { return false; }

	// 价格只算一次：先取价、再扣钱，失败时按同一个值退回 ——
	// 避免中途 Economy 变化导致"扣的钱和退的钱不一致"。
	const int32 Price = GetCreaturePrice(Type);
	if (!TrySpendCoins(Price)) { return false; }

	if (SpawnCreature(Type, CellIdx) == INDEX_NONE)
	{
		// 放不下（比如生物池满了）就把钱还回去，连累计值一起回滚。
		// 注意：**购买计数不加**，所以价格也不会涨。
		Economy.Coins += Price;
		Economy.LifetimeCoins -= Price;
		return false;
	}

	// 买成功了才涨价：下一次买同种生物会贵 (涨价系数) 倍。
	if (Type == ECreatureType::Rabbit) { ++Economy.RabbitsBought; }
	else                                { ++Economy.FoxesBought; }

	return true;
}

// =====================================================================
// 玩家工具
// =====================================================================

void UEcoBoxSimSubsystem::SetTool(EPlayerTool InTool)
{
	CurrentTool = InTool;

	static const TCHAR* const Names[] =
	{
		TEXT("无"), TEXT("水壶"), TEXT("镰刀"), TEXT("放置兔子"), TEXT("放置狐狸")
	};
	const int32 Index = (int32)InTool;

	UE_LOG(LogTemp, Log, TEXT("[EcoBox] 当前工具：%s"),
		Names[(Index >= 0 && Index < UE_ARRAY_COUNT(Names)) ? Index : 0]);
}

/**
 * 收集圆形范围内的格子。判定和 WaterCircle 完全一致 ——
 * 注意是**圆形**不是方形：半径 1 覆盖 5 格（十字），半径 √2 覆盖 9 格。
 */
void UEcoBoxSimSubsystem::GetCircleCells(int32 CenterCell, float Radius, TArray<int32>& OutCells) const
{
	OutCells.Reset();
	if (!Cells.IsValidIndex(CenterCell)) { return; }

	const int32 CX = CellX(CenterCell);
	const int32 CY = CellY(CenterCell);
	const float R = FMath::Max(0.f, Radius);
	const int32 RInt = FMath::CeilToInt(R);
	const float R2 = R * R;

	for (int32 dy = -RInt; dy <= RInt; ++dy)
	{
		for (int32 dx = -RInt; dx <= RInt; ++dx)
		{
			if ((float)(dx * dx + dy * dy) > R2) { continue; }
			if (!IsValidCoord(CX + dx, CY + dy)) { continue; }

			OutCells.Add(CellIndex(CX + dx, CY + dy));
		}
	}
}

int32 UEcoBoxSimSubsystem::WaterCircle(int32 CenterCell, float Radius)
{
	TArray<int32> Targets;
	GetCircleCells(CenterCell, Radius, Targets);

	for (const int32 Idx : Targets)
	{
		WaterCell(Idx);
	}
	return Targets.Num();
}

int32 UEcoBoxSimSubsystem::WaterWithCurrentCan(int32 CenterCell)
{
	return WaterCircle(CenterCell, GetWaterCanRadius());
}

/** 割草：只有成熟草能割，割完整格清空（地还湿就自己重长）。 */
int32 UEcoBoxSimSubsystem::HarvestGrass(int32 CellIdx)
{
	if (!Cells.IsValidIndex(CellIdx)) { return 0; }

	FECell& Cell = Cells[CellIdx];
	if (Cell.Stage != EGrassStage::Mature) { return 0; }

	const int32 Base = GetHarvestYield();
	const bool bDouble = Rng.FRand() < GetDoubleChance();
	const int32 Gain = bDouble ? Base * 2 : Base;

	AddCoins(Gain);
	Economy.CoinsFromHarvest += Gain;
	++Economy.Harvests;

	Cell.Stage = EGrassStage::Empty;
	Cell.StageTime = 0.f;
	RefreshCellVisual(Cell);

	return Gain;
}

/**
 * 圆形收割：和浇水完全对称，走同一套 GetCircleCells 判定。
 * 逐格调 HarvestGrass 拿各自的收益（含升级线 A / B 的加成和随机翻倍），累加返回。
 */
int32 UEcoBoxSimSubsystem::HarvestCircle(int32 CenterCell, float Radius)
{
	TArray<int32> Targets;
	GetCircleCells(CenterCell, Radius, Targets);

	int32 TotalGain = 0;
	for (const int32 Idx : Targets)
	{
		TotalGain += HarvestGrass(Idx);
	}
	return TotalGain;
}

int32 UEcoBoxSimSubsystem::HarvestWithCurrentScythe(int32 CenterCell)
{
	return HarvestCircle(CenterCell, GetScytheRadius());
}

// =====================================================================
// 坐标换算
// =====================================================================

FVector UEcoBoxSimSubsystem::CellToWorld(int32 Idx)
{
	const float X = (CellX(Idx) + 0.5f) * EcoBoxConst::TileSize - EcoBoxConst::HalfExtent;
	const float Y = (CellY(Idx) + 0.5f) * EcoBoxConst::TileSize - EcoBoxConst::HalfExtent;
	return FVector(X, Y, 0.f);
}

int32 UEcoBoxSimSubsystem::WorldToCell(const FVector& World)
{
	const int32 X = FMath::FloorToInt((World.X + EcoBoxConst::HalfExtent) / EcoBoxConst::TileSize);
	const int32 Y = FMath::FloorToInt((World.Y + EcoBoxConst::HalfExtent) / EcoBoxConst::TileSize);

	if (!IsValidCoord(X, Y)) { return INDEX_NONE; }
	return CellIndex(X, Y);
}

FVector UEcoBoxSimSubsystem::GetCreatureWorldPos(const FECreature& Creature) const
{
	float UnusedHopAlpha = 0.f;
	return GetCreatureWorldPos(Creature, UnusedHopAlpha);
}

/**
 * 算一只生物在某个格子内部的"落点偏移"（相对格心的 XY 偏移，厘米）。
 *
 * 用两个不同频率的 sin/cos（李萨如轨迹）在格子内画一条不定向的曲线：
 * 相位来自 `Creature.WanderPhase`（出生随机）+ 格子的坐标哈希，
 * 所以同一格里多只动物各有各的路线，同一只动物跨格之后落点也不一样。
 *
 * `bTimeAnimated` 是这个函数的关键开关：
 *  - **true（静止时用）**：偏移随 `SimTime` 持续变化 → 原地小范围溜达。
 *  - **false（移动时用）**：冻掉时间项，偏移**只由格坐标 + 相位决定**，
 *    一整段移动期间固定不变 → 生物走的是"从 A 格落点到 B 格落点"的一条确定斜线。
 *    > 这一点很重要：如果移动时也让偏移随时间变，主运动（格心→格心直线）
 *    > 会被额外甩来甩去，看起来就是"明明在往格心走、还莫名其妙地偏移"。
 *
 * ⚠ 纯视觉：返回值只叠到渲染坐标上，不参与任何逻辑判定。
 */
FVector UEcoBoxSimSubsystem::GetWanderOffset(int32 CellIdx, float Phase, float RadiusScale,
	bool bTimeAnimated) const
{
	if (!Cells.IsValidIndex(CellIdx))
	{
		return FVector::ZeroVector;
	}

	const UEcoBoxSettings& S = EcoParams::S();
	if (!S.bCreatureWander || S.CreatureWanderRadius <= 0.f)
	{
		return FVector::ZeroVector;
	}

	// 半格的可用半径，再乘"全局半径比例 × 个体勇气"
	const float MaxR = EcoBoxConst::TileSize * 0.5f * FMath::Clamp(S.CreatureWanderRadius, 0.f, 1.f);

	// 每格给一个固定的坐标相位，让相邻两格的偏移不同步 ——
	// 否则跨格时所有动物会走出"整齐的平移"。
	const float CellPhase = (float)((CellIdx * 2654435761u) & 0xFFFF) / 65536.f * 2.f * PI;

	// 静止时：时间项随时间走；移动时：时间项冻在 0（落点固定）
	const float T = bTimeAnimated
		? SimTime * FMath::Max(0.f, S.CreatureWanderSpeed) * 2.f * PI
		: 0.f;
	const float P = Phase + CellPhase;

	// X / Y 用两个不成整数倍关系的频率 → 轨迹不闭合、看起来像随便走
	const float OX = FMath::Sin(T + P) * 0.72f + FMath::Sin(T * 1.7f + P * 1.3f) * 0.28f;
	const float OY = FMath::Cos(T * 0.83f + P * 1.1f) * 0.72f + FMath::Cos(T * 1.43f + P * 0.7f) * 0.28f;

	const float R = MaxR * FMath::Clamp(RadiusScale, 0.f, 1.f);
	// 两条正弦叠加后最坏能到 ±1.0，直接乘 R 可能擦到格子边，稍微压一点
	return FVector(OX * R * 0.92f, OY * R * 0.92f, 0.f);
}

FVector UEcoBoxSimSubsystem::GetCreatureWorldPos(const FECreature& Creature, float& OutHopAlpha) const
{
	OutHopAlpha = 0.f;

	if (!Cells.IsValidIndex(Creature.CellIdx))
	{
		return FVector::ZeroVector;
	}

	const UEcoBoxSettings& S = EcoParams::S();
	FVector Pos = CellToWorld(Creature.CellIdx);

	// ---- 格内游走（纯视觉，XY 偏移）----
	const bool bMoving = (Creature.State == ECreatureState::Moving
		&& Cells.IsValidIndex(Creature.TargetCell));

	// 静止时的偏移：
	//  - 关了「静止时也游走」（默认）→ 用 `false`：**静态落点**（不含时间项）。
	//    于是站着时位置固定不动，而且和"上一段移动的终点"是同一个点，不会有突跳。
	//  - 开了 → 用 `true`：随 SimTime 飘，在原地小范围溜达。
	const bool bTimeAnimated = (!bMoving && S.bWanderWhenIdle);
	const FVector WanderHere = GetWanderOffset(
		Creature.CellIdx, Creature.WanderPhase, Creature.WanderRadiusScale, bTimeAnimated);
	Pos += WanderHere;

	if (bMoving)
	{
		// 逻辑每 StepSeconds 才推进一步，只按 MoveProgress 画的话
		// 每一格会分 4 次"跳"过去（1 秒走一格 = 每次跳 50cm），看着一顿一顿的。
		// 这里把"距离下一步已经累积了多少时间"（Accum）折算成进度补上，
		// 于是同一个 MoveSeconds 在不同帧率下画出来都是连续滑行。
		const float Duration = FMath::Max(0.05f, Creature.MoveSeconds);
		const float InterStep = (SimSpeed > 0.f)
			? FMath::Clamp(Accum, 0.f, EcoBoxConst::StepSeconds)
			: 0.f;

		const float Progress = FMath::Clamp(Creature.MoveProgress + InterStep / Duration, 0.f, 1.f);

		// ---------------- 跳跃（兔子） ----------------
		const bool bHop = (Creature.Type == ECreatureType::Rabbit)
			&& S.bRabbitHopMove && S.RabbitHopHeight > 0.f;

		const float Hops = bHop ? FMath::Max(1.f, (float)S.RabbitHopCount) : 1.f;
		const float AirRatio = FMath::Clamp(S.RabbitHopAirRatio, 0.05f, 1.f);

		// 当前这一次跳跃内部的进度 [0,1)
		const float Local = FMath::Frac(Progress * Hops);

		// ---- 前进量 Advance ----
		// 默认（不跳 / 或没开"只在腾空时前进"）：就是线性时间进度。
		// 开了跳跃：**只把腾空那段算进前进量，落地那段冻结**
		//   → 表现成"腾空时整体冲出去、落地了停住等地下一跳"，
		//     而不是"一边飘一边跳"（后者看着很假）。
		float Advance = Progress;
		float HopHeight01 = 0.f;   // 0..1，当前在第几成的腾空高度
		if (bHop)
		{
			const float FullHops = FMath::FloorToFloat(Progress * Hops);
			float AirPart;          // 当前这一跳里，腾空走了多少（0..1）
			if (Local < AirRatio)
			{
				AirPart = Local / AirRatio;
				HopHeight01 = FMath::Sin(AirPart * PI);       // 0→1→0 的抛物线
				OutHopAlpha = HopHeight01;
			}
			else
			{
				// 落地贴地：腾空进度停在 1（= 这一跳已经冲完了），高度为 0
				AirPart = 1.f;
				HopHeight01 = 0.f;
				// 落地那一下给个小的负值，让渲染压一下，更有分量
				const float GroundT = (Local - AirRatio) / FMath::Max(KINDA_SMALL_NUMBER, 1.f - AirRatio);
				OutHopAlpha = -0.35f * FMath::Sin(GroundT * PI);
			}

			const float AirAdvance = (FullHops + AirPart) / Hops;

			if (S.bRabbitMoveOnlyWhenAirborne)
			{
				// 只在腾空时前进：落地段 Advance 冻结，停住
				Advance = AirAdvance;
			}
			else
			{
				// 匀速前进（跳跃只叠在 Z 上）—— 会有"飘着跳"的感觉
				Advance = Progress;
			}
		}

		// 位移：从"当前格里的落点"走到"目标格里的落点"，两个落点都**冻结**。
		// → 这是一条确定的斜线，而不是"往格心走 + 乱飘"。
		const FVector From = CellToWorld(Creature.CellIdx) + WanderHere;
		const FVector To   = CellToWorld(Creature.TargetCell)
			+ GetWanderOffset(Creature.TargetCell, Creature.WanderPhase, Creature.WanderRadiusScale, false);

		Pos = FMath::Lerp(From, To, Advance);

		// ---- 路径弯曲（纯视觉）：让直线变成一条向一侧拱出的弧 ----
		// 逻辑上仍然是"这一格走到那一格"，只是画出来绕一点，
		// 否则几十只动物全在走笔直的格心连线，看着像在格子上跑轨道。
		// 用 Advance 而不是 Progress：这样弧线的进出和跳跃同步，不会"人停了弧还在动"。
		if (S.CreatureWanderCurve > 0.f)
		{
			const FVector Flat = To - From;
			const FVector Dir2D(Flat.X, Flat.Y, 0.f);
			const float Len = Dir2D.Size();
			if (Len > KINDA_SMALL_NUMBER)
			{
				// 垂直于行进方向的法线（在水平面内）
				const FVector Nrm(-Dir2D.Y / Len, Dir2D.X / Len, 0.f);
				// 拱哪一侧由格子坐标 + 生物相位决定，同一对格子之间不同动物拱不同边
				const float SideSign = ((Creature.CellIdx * 31 + Creature.TargetCell * 17) & 1) ? 1.f : -1.f;

				const float Bulge = EcoBoxConst::TileSize * 0.5f
					* FMath::Clamp(S.CreatureWanderCurve, 0.f, 1.f) * SideSign;
				// sin(pi*p)：两端为 0（起终点不受影响）、中段最大
				Pos += Nrm * (Bulge * FMath::Sin(Advance * PI));
			}
		}

		// ---- 跳跃的抬升（Z） ----
		if (bHop)
		{
			Pos.Z += S.RabbitHopHeight * HopHeight01;
		}
	}
	return Pos;
}

FString UEcoBoxSimSubsystem::FormatClock(float TimeOfDay)
{
	const float Hours = FMath::Frac(FMath::Max(0.f, TimeOfDay)) * 24.f;
	const int32 H = FMath::FloorToInt(Hours) % 24;
	const int32 M = FMath::FloorToInt((Hours - FMath::FloorToInt(Hours)) * 60.f);

	return FString::Printf(TEXT("%02d:%02d"), H, M);
}

// =====================================================================
// 统计
// =====================================================================

void UEcoBoxSimSubsystem::RefreshStats()
{
	const UEcoBoxSettings& S = EcoParams::S();

	int32 Rabbits = 0;
	int32 Foxes = 0;
	int32 Eating = 0;
	int32 AdultR = 0;
	int32 YoungR = 0;

	for (const FECreature& C : Creatures)
	{
		if (!C.bAlive) { continue; }

		if (C.Type == ECreatureType::Rabbit)
		{
			++Rabbits;
			if (C.bAdult) { ++AdultR; } else { ++YoungR; }
		}
		else
		{
			++Foxes;
		}

		if (C.State == ECreatureState::Eating) { ++Eating; }
	}

	int32 Mature = 0;
	int32 Growing = 0;
	int32 Wet = 0;
	int32 GrassTiles = 0;
	double GrassSum = 0.0;

	for (const FECell& Cell : Cells)
	{
		if (Cell.bWet) { ++Wet; }

		if (Cell.Stage == EGrassStage::Mature)       { ++Mature; ++GrassTiles; }
		else if (Cell.Stage == EGrassStage::Growing) { ++Growing; ++GrassTiles; }

		if (Cell.Grass > 0) { GrassSum += (double)Cell.Grass / EcoBoxConst::MaxGrass; }
	}

	Stats.Rabbits = Rabbits;
	Stats.Foxes = Foxes;
	Stats.AdultRabbits = AdultR;
	Stats.YoungRabbits = YoungR;
	Stats.Eating = Eating;
	Stats.MatureGrass = Mature;
	Stats.GrowingGrass = Growing;
	Stats.WetTiles = Wet;
	Stats.GrassTiles = GrassTiles;
	Stats.GrassCoverage = (float)(GrassSum / (double)EcoBoxConst::CellCount);
	Stats.Births = TotalBirths;
	Stats.Deaths = TotalDeaths;
	Stats.Starved = TotalStarved;
	Stats.Eaten = TotalEaten;

	Stats.Coins = Economy.Coins;
	Stats.LifetimeCoins = Economy.LifetimeCoins;
	Stats.GoalCoins = S.GoalCoins;
	Stats.WaterCanLevel = Economy.WaterCanLevel;
	Stats.ScytheLevel = Economy.ScytheLevel;

	Stats.CoinsFromHarvest = Economy.CoinsFromHarvest;
	Stats.CoinsFromPredation = Economy.CoinsFromPredation;
	Stats.CoinsPerMinute = GetCoinsPerMinute();
	Stats.CoinBaseLevel = Economy.CoinBaseLevel;
	Stats.DoubleChanceLevel = Economy.DoubleChanceLevel;
	Stats.HarvestYield = GetHarvestYield();
	Stats.PredationYield = GetPredationYield();
	Stats.DoubleChance = GetDoubleChance();

	Stats.SimTime = SimTime;
	Stats.ClockText = FormatClock(Weather.TimeOfDay);
	Stats.TimeText = FString::Printf(TEXT("%.0f 秒"), SimTime);
}

// =====================================================================
// 存档
//
// 存档文件落在  项目目录/Saved/SaveGames/<槽名>.sav
// 槽名固定用 GetDefaultSaveSlot()，蓝图里不用关心路径。
// =====================================================================

bool UEcoBoxSimSubsystem::HasSaveGame() const
{
	return UGameplayStatics::DoesSaveGameExist(GetDefaultSaveSlot(), 0);
}

bool UEcoBoxSimSubsystem::SaveGame()
{
	UEcoBoxSaveGame* Save = Cast<UEcoBoxSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UEcoBoxSaveGame::StaticClass()));
	if (!Save)
	{
		UE_LOG(LogTemp, Error, TEXT("[EcoBox] 创建存档对象失败"));
		return false;
	}

	Save->SaveVersion = 1;
	Save->SavedAt     = FDateTime::Now();

	Save->Economy     = Economy;
	Save->Weather     = Weather;
	Save->CurrentTool = CurrentTool;
	Save->SimTime     = SimTime;
	Save->SimSpeed    = SimSpeed;

	// 世界原样拷贝：生物池**连空槽一起存**，否则 FECreature::CellIdx 和
	// 读档后重建出来的 CellOccupants 下标会对不上。
	Save->Cells       = Cells;
	Save->Creatures   = Creatures;
	Save->FreeSlots   = FreeSlots;

	Save->TotalBirths  = TotalBirths;
	Save->TotalDeaths  = TotalDeaths;
	Save->TotalStarved = TotalStarved;
	Save->TotalEaten   = TotalEaten;

	// 写入前的对账：源数据 vs 存档对象。
	const int32 SrcCells = Cells.Num();
	const int32 SrcCreatures = Creatures.Num();
	const int32 SrcCoins = Economy.Coins;

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, GetDefaultSaveSlot(), 0);

	// 自检：存完立刻读回来对一下。
	// "写成功了但存的是个空壳"是 SaveGame 最阴的一种失败（文件在、不报错、内容是空的），
	// 这两行日志能把它当场逮住。
	if (bOk)
	{
		if (USaveGame* VerifyRaw = UGameplayStatics::LoadGameFromSlot(GetDefaultSaveSlot(), 0))
		{
			if (const UEcoBoxSaveGame* Verify = Cast<UEcoBoxSaveGame>(VerifyRaw))
			{
				UE_LOG(LogTemp, Log,
					TEXT("[EcoBox] 存档自检：写入 Cells=%d(源 %d) Creatures=%d(源 %d) 币=%d(源 %d) → 读回 Cells=%d Creatures=%d 币=%d"),
					Save->Cells.Num(), SrcCells,
					Save->Creatures.Num(), SrcCreatures,
					Save->Economy.Coins, SrcCoins,
					Verify->Cells.Num(), Verify->Creatures.Num(), Verify->Economy.Coins);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[EcoBox] 保存游戏%s：兔 %d 狐 %d 币 %d 模拟时长 %.0f 秒"),
		bOk ? TEXT("成功") : TEXT("失败"),
		Stats.Rabbits, Stats.Foxes, Economy.Coins, SimTime);
	return bOk;
}

bool UEcoBoxSimSubsystem::LoadGame()
{
	USaveGame* Raw = UGameplayStatics::LoadGameFromSlot(GetDefaultSaveSlot(), 0);
	UEcoBoxSaveGame* Save = Cast<UEcoBoxSaveGame>(Raw);
	if (!Save)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EcoBox] 读档失败：槽里没有有效的存档对象"));
		return false;
	}

	if (Save->SaveVersion != 1)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EcoBox] 读档失败：存档版本是 %d，当前代码只认 1"), Save->SaveVersion);
		return false;
	}

	// 先校验再赋值 —— 失败时当前世界**一点都不动**。
	// 格子数对不上通常意味着存档是用别的网格尺寸做的，硬灌进去会让所有坐标错位。
	if (Save->Cells.Num() != EcoBoxConst::CellCount)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EcoBox] 读档失败：存档有 %d 格，当前世界是 %d 格"),
			Save->Cells.Num(), EcoBoxConst::CellCount);
		return false;
	}

	Economy     = Save->Economy;
	Weather     = Save->Weather;
	CurrentTool = Save->CurrentTool;
	SimTime     = Save->SimTime;
	SimSpeed    = Save->SimSpeed;

	Cells       = Save->Cells;
	Creatures   = Save->Creatures;
	FreeSlots   = Save->FreeSlots;

	// 反查表不存档（存了反而容易和 Creatures 不同步），读档后按生物重新建一遍。
	RebuildCellOccupants();

	TotalBirths  = Save->TotalBirths;
	TotalDeaths  = Save->TotalDeaths;
	TotalStarved = Save->TotalStarved;
	TotalEaten   = Save->TotalEaten;

	// 清掉"上一局没跑完的时间碎片"，否则读档后的第一帧会一口气多跑几步。
	Accum = 0.f;

	// 产币速率的滑动窗口是"最近 60 模拟秒"的滚动统计，不属于进度，重置即可。
	// 代价：刚读档时"币/分钟"会从 0 重新爬，一两秒内就正常了。
	CoinBuckets.Init(0.f, IncomeBuckets);
	CoinBucketIndex = 0;
	CoinBucketTimer = 0.f;
	LastStatLogTime = SimTime;

	// 让 Stats 立刻反映读回来的世界（不等下一步模拟）。
	RefreshStats();

	UE_LOG(LogTemp, Log, TEXT("[EcoBox] 已读档（存档时间 %s）：兔 %d 狐 %d 币 %d"),
		*Save->SavedAt.ToString(TEXT("%Y-%m-%d %H:%M")),
		Stats.Rabbits, Stats.Foxes, Economy.Coins);
	return true;
}

bool UEcoBoxSimSubsystem::DeleteSaveGame()
{
	if (!HasSaveGame())
	{
		return false;
	}

	const bool bOk = UGameplayStatics::DeleteGameInSlot(GetDefaultSaveSlot(), 0);
	UE_LOG(LogTemp, Log, TEXT("[EcoBox] 删除存档%s"), bOk ? TEXT("成功") : TEXT("失败"));
	return bOk;
}

FString UEcoBoxSimSubsystem::GetSaveTimeText() const
{
	USaveGame* Raw = UGameplayStatics::LoadGameFromSlot(GetDefaultSaveSlot(), 0);
	if (const UEcoBoxSaveGame* Save = Cast<UEcoBoxSaveGame>(Raw))
	{
		return Save->SavedAt.ToString(TEXT("%Y-%m-%d %H:%M"));
	}
	return FString();
}

bool UEcoBoxSimSubsystem::LoadIfExists()
{
	if (!HasSaveGame())
	{
		return false;
	}
	return LoadGame();
}


// =====================================================================
// 格子 ↔ 生物 的反查维护（一格可多只之后才需要）
// =====================================================================

void UEcoBoxSimSubsystem::AddCreatureToCell(int32 CellIdx, int32 CreatureIdx)
{
	if (!Cells.IsValidIndex(CellIdx) || CreatureIdx == INDEX_NONE) { return; }

	TArray<int32>& List = CellOccupants.FindOrAdd(CellIdx);
	List.AddUnique(CreatureIdx);
	Cells[CellIdx].OccupantCount = List.Num();
}

void UEcoBoxSimSubsystem::RemoveCreatureFromCell(int32 CellIdx, int32 CreatureIdx)
{
	if (!Cells.IsValidIndex(CellIdx)) { return; }

	TArray<int32>* List = CellOccupants.Find(CellIdx);
	if (!List)
	{
		Cells[CellIdx].OccupantCount = 0;
		return;
	}

	List->Remove(CreatureIdx);
	const int32 Remaining = List->Num();
	if (Remaining == 0)
	{
		// 空列表删掉，省得 TMap 里堆一堆空数组
		CellOccupants.Remove(CellIdx);
	}
	Cells[CellIdx].OccupantCount = Remaining;
}

int32 UEcoBoxSimSubsystem::FindLiveCreatureInCell(int32 CellIdx, ECreatureType Type) const
{
	const TArray<int32>* List = CellOccupants.Find(CellIdx);
	if (!List) { return INDEX_NONE; }

	for (int32 Idx : *List)
	{
		if (!Creatures.IsValidIndex(Idx)) { continue; }
		const FECreature& C = Creatures[Idx];
		if (!C.bAlive || C.Type != Type) { continue; }
		return Idx;
	}
	return INDEX_NONE;
}

void UEcoBoxSimSubsystem::RebuildCellOccupants()
{
	CellOccupants.Reset();
	for (FECell& Cell : Cells)
	{
		Cell.OccupantCount = 0;
	}

	for (int32 i = 0; i < Creatures.Num(); ++i)
	{
		const FECreature& C = Creatures[i];
		if (!C.bAlive || !Cells.IsValidIndex(C.CellIdx)) { continue; }
		AddCreatureToCell(C.CellIdx, i);
	}
}
