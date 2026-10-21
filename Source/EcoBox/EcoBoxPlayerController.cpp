// EcoBox - 玩家控制器实现
#include "EcoBoxPlayerController.h"

#include "EcoBoxCameraPawn.h"
#include "EcoBoxRenderer.h"
#include "EcoBoxSettings.h"
#include "EcoBoxSimSubsystem.h"
#include "EcoBoxHUDWidget.h"
#include "EcoBoxHUDBase.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Blueprint/UserWidget.h"

AEcoBoxPlayerController::AEcoBoxPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AEcoBoxPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	bShowMouseCursor = true;

	// 游戏 + UI：既能用鼠标点地面，也能点面板上的按钮
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);

	// HUD 有两套，优先级：你在编辑器里做的 UMG 蓝图 > 代码内置的纯 C++ 面板。
	//
	// 指派方式：选中关卡里的 EcoBoxPlayerController（或在 GameMode 的默认
	// PlayerControllerClass 上），细节面板 → EcoBox|UI → HUD 蓝图类。
	// 清空就退回内置面板，不用改代码。
	if (HUDBlueprintClass)
	{
		ActiveHUD = CreateWidget<UEcoBoxHUDBase>(this, HUDBlueprintClass);
		if (ActiveHUD)
		{
			ActiveHUD->AddToViewport();
			UE_LOG(LogTemp, Log, TEXT("[EcoBox] 已加载蓝图 HUD：%s"), *HUDBlueprintClass->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[EcoBox] 蓝图 HUD 创建失败，退回内置面板"));
		}
	}

	if (!ActiveHUD)
	{
		// 内置面板：控件树全在 C++ 里搭。
		HUDWidget = CreateWidget<UEcoBoxHUDWidget>(this, UEcoBoxHUDWidget::StaticClass());
		if (HUDWidget)
		{
			HUDWidget->AddToViewport();
			// 内置面板同时也是 EcoBoxHUDBase，用它统一刷新。
			ActiveHUD = HUDWidget;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[EcoBox] 控制面板创建失败"));
		}
	}

	if (!GetSim())
	{
		UE_LOG(LogTemp, Error, TEXT("[EcoBox] 找不到模拟子系统"));
	}
}

// =====================================================================
// HUD 显示 / 隐藏 / 换实例
// =====================================================================

void AEcoBoxPlayerController::HideHUD()
{
	if (!ActiveHUD) { return; }

	// 只是从视口移除 —— **不动 ActiveHUD、也不销毁对象**。
	// 这样 PlayerTick 仍然每帧刷新它，回来时 ShowHUD() 立刻就是最新数值，
	// 而且所有控件状态（当前展开的分区、滚动条位置…）都还在。
	ActiveHUD->RemoveFromParent();
}

void AEcoBoxPlayerController::ShowHUD()
{
	if (!ActiveHUD) { return; }

	// AddToViewport 对"已经在视口里"的控件是安全的（不会重复添加）。
	ActiveHUD->AddToViewport();
}

void AEcoBoxPlayerController::SetActiveHUD(UEcoBoxHUDBase* NewHUD, bool bRemoveOldHUD)
{
	if (ActiveHUD == NewHUD)
	{
		return;
	}

	// 旧的先收掉：留着它既占渲染、又会被误当成"当前面板"参与 IsPointerOverPanel 判定。
	if (bRemoveOldHUD && ActiveHUD)
	{
		ActiveHUD->RemoveFromParent();
	}

	ActiveHUD = NewHUD;

	if (ActiveHUD)
	{
		ActiveHUD->AddToViewport();
	}
}

void AEcoBoxPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsLocalController())
	{
		return;
	}

	HandleCameraInput(DeltaTime);
	HandleToolInput(DeltaTime);
	UpdateHoverHighlight();

	if (ActiveHUD)
	{
		ActiveHUD->RefreshDisplay();
	}
}

// =====================================================================
// 输入事件（滚轮 / 鼠标按键）
// =====================================================================

/**
 * 滚轮缩放**必须走事件**，不能每帧轮询 —— 这是之前两个毛病的共同根源。
 *
 * 引擎只在"这一帧有采样"时才把轴值从累加器搬到 KeyState：
 *
 *     if (KeyState->SampleCountAccumulator > 0 || Key.ShouldUpdateAxisWithoutSamples())
 *     { KeyState->RawValue = KeyState->RawValueAccumulator; }
 *     KeyState->RawValueAccumulator = FVector::ZeroVector;      // 累加器每帧清零
 *
 * 而 `FKey::ShouldUpdateAxisWithoutSamples()` 只对 MouseX / MouseY 返回 true
 * （InputCoreTypes.cpp 里的 EKeyFlags::UpdateAxisWithoutSamples）。
 * 也就是：**滚轮一旦滚过，轴值就永远停在最后那个增量上，不滚动也不会回零。**
 *
 * 于是原来那种写法
 *     const float Wheel = GetInputAnalogKeyState(EKeys::MouseWheelAxis);
 *     if (Wheel != 0) { Cam->ZoomBy(-Wheel); }
 * 就变成了**每一帧都在放大**：视角突然被拉近，而且拉近之后
 * "每像素对应多少厘米"跟着变小，拖拽也变得非常慢、像跟不上手。
 * 用户看到的"突然放大 + 瞬移感 + 拖动很慢"就是这一个 bug 的三种表现。
 *
 * 所以这里改成收事件：一次滚轮事件 = 一次缩放。
 */
bool AEcoBoxPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// ---- 滚轮缩放 ----
	if (Params.Key == EKeys::MouseWheelAxis)
	{
		const float Wheel = Params.AmountDepressed;
		if (FMath::Abs(Wheel) > KINDA_SMALL_NUMBER)
		{
			if (AEcoBoxCameraPawn* Cam = GetEcoPawn())
			{
				Cam->ZoomBy(-Wheel * GetDefault<UEcoBoxSettings>()->CameraWheelZoomStep);
			}
		}
	}

	// ---- 拖拽的起止完全由按键事件决定 ----
	// 按下 → 开始新的一段（PlayerTick 那一帧只记基准、不产生位移）
	// 抬起 → 立刻结束，下一次按下必定是重新开始，不会拿旧基准算出一个大位移
	if (Params.Key == EKeys::RightMouseButton)
	{
		if (Params.Event == IE_Pressed)
		{
			bDragging = false;
			bRightPressed = true;

			// 记下按下那一刻的光标位置，抬起时用它判断"这是点击还是拖视角"。
			float X = 0.f;
			float Y = 0.f;
			if (GetMousePosition(X, Y))
			{
				RightPressMouseX = X;
				RightPressMouseY = Y;
			}
		}
		else if (Params.Event == IE_Released)
		{
			bDragging = false;

			if (bRightPressed)
			{
				bRightPressed = false;

				// 为什么不能"右键按下就取消选择"：
				// 右键同时是**拖拽平移视角**的操作，按下就清工具的话，
				// 每拖一次视角手里的水壶 / 镰刀就被撸掉一次，没法玩了。
				// 所以只认"几乎没动过"的那种右键 —— 也就是点击。
				float X = 0.f;
				float Y = 0.f;
				// 拿不到当前光标（跑出视口之类）就当没动过：宁可漏一次取消，也不要乱清。
				const float Moved = GetMousePosition(X, Y)
					? FVector2D::Distance(FVector2D(X, Y), FVector2D(RightPressMouseX, RightPressMouseY))
					: 0.f;

				const bool bCancel = (Moved <= RightClickCancelMaxPixels);

				// 留一条能查的痕迹：右键松开时到底被当成"点击"还是"拖视角"。
				// 排查"右键取消好像没反应"就看这一行。
				UE_LOG(LogTemp, Log, TEXT("[EcoBox] 右键松开：位移 %.1f px / 阈值 %.0f → %s"),
					Moved, RightClickCancelMaxPixels,
					bCancel ? TEXT("判为点击，取消选择") : TEXT("判为拖视角，不动"));

				if (bCancel)
				{
					ClearTool();
				}
			}
		}
	}
	else if (Params.Key == EKeys::MiddleMouseButton)
	{
		if (Params.Event == IE_Pressed || Params.Event == IE_Released)
		{
			bDragging = false;
		}
	}

	return Super::InputKey(Params);
}

// =====================================================================
// 相机
// =====================================================================

void AEcoBoxPlayerController::HandleCameraInput(float DeltaTime)
{
	AEcoBoxCameraPawn* Cam = GetEcoPawn();
	if (!Cam)
	{
		return;
	}

	// ---- WASD 平移 ----
	FVector2D Move = FVector2D::ZeroVector;
	if (IsInputKeyDown(EKeys::W)) { Move.Y += 1.f; }
	if (IsInputKeyDown(EKeys::S)) { Move.Y -= 1.f; }
	if (IsInputKeyDown(EKeys::D)) { Move.X += 1.f; }
	if (IsInputKeyDown(EKeys::A)) { Move.X -= 1.f; }
	if (Move.SizeSquared() > 1.f) { Move.Normalize(); }
	Cam->PanBy(Move, DeltaTime);

	// ---- Q / E 旋转视角 ----
	float YawDir = 0.f;
	if (IsInputKeyDown(EKeys::Q)) { YawDir -= 1.f; }
	if (IsInputKeyDown(EKeys::E)) { YawDir += 1.f; }
	if (YawDir != 0.f)
	{
		Cam->YawBy(YawDir * Cam->GetRotateSpeed() * DeltaTime);
	}

	// ---- 右键 / 中键拖拽平移 ----
	const bool bPanHeld = IsInputKeyDown(EKeys::RightMouseButton)
	                   || IsInputKeyDown(EKeys::MiddleMouseButton);

	// 光标压在上下任一条 UI 上时不算数：那些鼠标移动事件会被 UMG 吃掉，
	// 视口缓存的鼠标位置就**冻住不动**，等光标回到地图上时第一帧会算出
	// 一整段距离的巨大位移 —— 这就是"视角瞬移"的来源。
	const bool bOverPanel = ActiveHUD && ActiveHUD->IsPointerOverPanel();

	float MouseX = 0.f;
	float MouseY = 0.f;

	if (bPanHeld && !bOverPanel && GetMousePosition(MouseX, MouseY))
	{
		if (bDragging)
		{
			// GetMousePosition 的 Y 是"向下为正"，转成相机那边约定的"向上为正"
			const FVector2D PixelDelta(MouseX - LastMouseX, -(MouseY - LastMouseY));

			if (PixelDelta.SizeSquared() <= FMath::Square(MaxDragJumpPixels))
			{
				Cam->PanByPixels(PixelDelta);
			}
			// 超阈值就只重新对齐基准、不 pan，避免病态值把镜头甩飞
		}

		LastMouseX = MouseX;
		LastMouseY = MouseY;
		bDragging = true;   // 从这一帧起才算是"已经在拖着走"
	}
	else
	{
		// 没按着、压着 UI、或者光标不在视口里 —— 一律重新开始，
		// 绝不拿上一次的旧位置去算位移
		bDragging = false;
	}

	// ---- 键盘兜底（没有滚轮时用） ----
	if (IsInputKeyDown(EKeys::PageUp))   { Cam->ZoomBy(-18.f * DeltaTime); }
	if (IsInputKeyDown(EKeys::PageDown)) { Cam->ZoomBy(18.f * DeltaTime); }
	if (IsInputKeyDown(EKeys::F))        { Cam->FocusOnOrigin(); }
}

// =====================================================================
// 工具：切工具 + 左键作用于地块
// =====================================================================

void AEcoBoxPlayerController::SetTool(EPlayerTool InTool)
{
	if (UEcoBoxSimSubsystem* Sim = GetSim())
	{
		Sim->SetTool(InTool);
	}
}

EPlayerTool AEcoBoxPlayerController::GetTool() const
{
	if (const UEcoBoxSimSubsystem* Sim = GetSim())
	{
		return Sim->GetTool();
	}
	return EPlayerTool::None;
}

void AEcoBoxPlayerController::ClearTool()
{
	// 已经是"无"就直接返回 —— 免得在空手上连点右键一直弹提示。
	if (GetTool() == EPlayerTool::None)
	{
		UE_LOG(LogTemp, Log, TEXT("[EcoBox] 取消选择：当前本来就是「无」，不处理"));
		return;
	}

	SetTool(EPlayerTool::None);

	// 地块高亮不用在这里清：下一帧 UpdateHoverHighlight 会因为"没拿工具"而清空它。
	Notify(TEXT("已取消选择"));
}

void AEcoBoxPlayerController::HandleToolInput(float DeltaTime)
{
	// ---- 空格：暂停 / 继续模拟 ----
	// 只动 SimSpeed（0 = 停），相机、UI、鼠标操作照旧。
	// 边沿检测是必须的：轮询式读键每帧都返回 true，不做边沿会一秒切上百次。
	{
		const bool bSpace = IsInputKeyDown(EKeys::SpaceBar);
		if (bSpace && !bSpaceWasDown) { TogglePause(); }
		bSpaceWasDown = bSpace;
	}

	// ---- 1 / 2 / 3 / 4 切工具 ----
	{
		const bool b1 = IsInputKeyDown(EKeys::One);
		const bool b2 = IsInputKeyDown(EKeys::Two);
		const bool b3 = IsInputKeyDown(EKeys::Three);
		const bool b4 = IsInputKeyDown(EKeys::Four);

		if (b1 && !bKey1WasDown) { SetTool(EPlayerTool::WaterCan); }
		if (b2 && !bKey2WasDown) { SetTool(EPlayerTool::Scythe); }
		if (b3 && !bKey3WasDown) { SetTool(EPlayerTool::PlaceRabbit); }
		if (b4 && !bKey4WasDown) { SetTool(EPlayerTool::PlaceFox); }

		bKey1WasDown = b1;
		bKey2WasDown = b2;
		bKey3WasDown = b3;
		bKey4WasDown = b4;
	}

	// ---- 左键 ----
	const bool bDown = IsInputKeyDown(EKeys::LeftMouseButton);
	const bool bEdge = bDown && !bLeftWasDown;
	bLeftWasDown = bDown;

	if (!bDown)
	{
		ToolRepeatTimer = 0.f;
		return;
	}

	const EPlayerTool Tool = GetTool();

	if (bEdge)
	{
		ToolRepeatTimer = 0.f;
	}
	else if (EcoBoxTool::IsPlacement(Tool))
	{
		// 买生物**只在按下去那一下**结算。
		// 后面那套"按住重复"是给水壶/镰刀拖动用的，套到购买上会按住左键一口气把钱花光。
		return;
	}
	else
	{
		// 按住时以固定间隔重复：浇水要能拖着走，不然一次一格太累
		ToolRepeatTimer -= DeltaTime;
		if (ToolRepeatTimer > 0.f) { return; }
		ToolRepeatTimer = FMath::Max(0.02f, ToolRepeatInterval);
	}

	// 鼠标压在面板上时交给 UMG
	if (ActiveHUD && ActiveHUD->IsPointerOverPanel())
	{
		return;
	}

	const int32 Cell = GetHoveredCell();
	if (Cell == INDEX_NONE) { return; }

	ApplyToolAt(Cell);
}

void AEcoBoxPlayerController::ApplyToolAt(int32 CellIdx)
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	switch (Sim->GetTool())
	{
	case EPlayerTool::WaterCan:
		// 浇水不给提示：拖动时每 0.15 秒刷一行会把提示刷成走马灯，
		// 而"地块变蓝"本身就是最直接的反馈。
		Sim->WaterWithCurrentCan(CellIdx);
		break;

	case EPlayerTool::Scythe:
	{
		// 镰刀和水壶对称：升级之后一次割一片圆。0 级半径 0，只割光标那一格。
		const int32 Gain = Sim->HarvestWithCurrentScythe(CellIdx);
		if (Gain > 0)
		{
			Notify(FString::Printf(TEXT("+%d 生态币（共 %d）"), Gain, Sim->GetEconomy().Coins));
		}
		break;
	}

	case EPlayerTool::PlaceRabbit:
	case EPlayerTool::PlaceFox:
	{
		const ECreatureType Type = EcoBoxTool::PlacementType(Sim->GetTool());
		const int32 Price = Sim->GetCreaturePrice(Type);
		const TCHAR* const Name = (Type == ECreatureType::Fox) ? TEXT("狐狸") : TEXT("兔子");

		if (Sim->GetEconomy().Coins < Price)
		{
			Notify(FString::Printf(TEXT("生态币不够：%s 要 %d，现在只有 %d"),
				Name, Price, Sim->GetEconomy().Coins));
			break;
		}

		if (Sim->BuyCreature(Type, CellIdx))
		{
			Notify(FString::Printf(TEXT("买入%s -%d（余 %d）"), Name, Price, Sim->GetEconomy().Coins));
		}
		else
		{
			Notify(TEXT("这一格已经有生物了"));
		}
		break;
	}

	default:
		break;
	}
}

void AEcoBoxPlayerController::Notify(const FString& Text)
{
	// 内置面板有它自己的一行提示；蓝图 HUD 走 ShowPanelMessage（广播 On Panel Message 事件）。
	if (HUDWidget)
	{
		HUDWidget->ShowMessage(Text);
	}
	else if (ActiveHUD)
	{
		ActiveHUD->ShowPanelMessage(Text);
	}
	UE_LOG(LogTemp, Verbose, TEXT("[EcoBox] %s"), *Text);
}

void AEcoBoxPlayerController::UpdateHoverHighlight()
{
	AEcoBoxRenderer* Renderer = GetRenderer();
	if (!Renderer) { return; }

	PreviewCells.Reset();

	const int32 Cell = GetHoveredCell();

	const EPlayerTool Tool = GetTool();

	// 空手（None）时不给任何高亮：右键取消选择之后鼠标底下还留着一个框会很怪。
	if (Cell != INDEX_NONE && Tool != EPlayerTool::None)
	{
		if (const UEcoBoxSimSubsystem* Sim = GetSim())
		{
			if (Tool == EPlayerTool::WaterCan)
			{
				// 水壶：把**整个浇水圆**都亮出来（和实际浇到的格子同一套判定），
				// 玩家能提前看到这一下会浇到哪几格 —— 升级水壶之后范围变大也一眼可见。
				Sim->GetCircleCells(Cell, Sim->GetWaterCanRadius(), PreviewCells);
			}
			else if (Tool == EPlayerTool::Scythe)
			{
				// 镰刀：同样把**整个收割圆**亮出来。升级镰刀后范围变大也一眼可见。
				Sim->GetCircleCells(Cell, Sim->GetScytheRadius(), PreviewCells);
			}
			else
			{
				PreviewCells.Add(Cell);
			}
		}
	}

	Renderer->SetHighlightCells(PreviewCells);
}

// =====================================================================
// 坐标与射线
// =====================================================================

bool AEcoBoxPlayerController::GetMouseGroundPoint(FVector& OutPoint) const
{
	FVector Origin;
	FVector Dir;

	if (!DeprojectMousePositionToWorld(Origin, Dir))
	{
		return false;
	}

	// 和 Z=0 的地面平面求交，不需要任何碰撞体
	if (FMath::Abs(Dir.Z) < KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float T = -Origin.Z / Dir.Z;
	if (T <= 0.f)
	{
		return false;
	}

	OutPoint = Origin + Dir * T;
	OutPoint.Z = 0.f;
	return true;
}

int32 AEcoBoxPlayerController::GetHoveredCell() const
{
	if (ActiveHUD && ActiveHUD->IsPointerOverPanel())
	{
		return INDEX_NONE;
	}

	FVector Point;
	if (!GetMouseGroundPoint(Point))
	{
		return INDEX_NONE;
	}

	if (const UEcoBoxSimSubsystem* Sim = GetSim())
	{
		return Sim->WorldToCell(Point);
	}
	return INDEX_NONE;
}

// =====================================================================
// UI 接线
// =====================================================================

UEcoBoxSimSubsystem* AEcoBoxPlayerController::GetSim() const
{
	if (UWorld* World = GetWorld())
	{
		return World->GetSubsystem<UEcoBoxSimSubsystem>();
	}
	return nullptr;
}

void AEcoBoxPlayerController::CycleSpeed(int32 Dir)
{
	if (UEcoBoxSimSubsystem* Sim = GetSim())
	{
		Sim->CycleSimSpeed(Dir);
		LastSpeed = FMath::Max(Sim->GetSimSpeed(), 0.5f);
	}
}

void AEcoBoxPlayerController::TogglePause()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	if (Sim->IsPaused())
	{
		Sim->SetSimSpeed(LastSpeed > 0.f ? LastSpeed : 1.f);
	}
	else
	{
		LastSpeed = Sim->GetSimSpeed();
		Sim->SetSimSpeed(0.f);
	}
}

void AEcoBoxPlayerController::AdjustRain(float Delta)
{
	if (UEcoBoxSimSubsystem* Sim = GetSim()) { Sim->AddRain(Delta); }
}

void AEcoBoxPlayerController::AdjustTemperature(float Delta)
{
	if (UEcoBoxSimSubsystem* Sim = GetSim()) { Sim->AddTemperature(Delta); }
}

void AEcoBoxPlayerController::AdjustDayLength(float Delta)
{
	if (UEcoBoxSimSubsystem* Sim = GetSim()) { Sim->AddDayLength(Delta); }
}

void AEcoBoxPlayerController::QuickSpawn(ECreatureType Type, int32 Count)
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	// 兔子优先投在有草的地方，狐狸随便撒
	if (Type == ECreatureType::Rabbit)
	{
		Sim->SpawnOnGrass(Type, Count);
	}
	else
	{
		Sim->SpawnCreatureRandom(Type, Count);
	}
}

void AEcoBoxPlayerController::SeedGrassField()
{
	if (UEcoBoxSimSubsystem* Sim = GetSim())
	{
		Sim->SeedGrass(SeedCoverage);
	}
}

void AEcoBoxPlayerController::ResetWorld()
{
	if (UEcoBoxSimSubsystem* Sim = GetSim())
	{
		Sim->ResetWorld(true);
	}
}

// =====================================================================

AEcoBoxCameraPawn* AEcoBoxPlayerController::GetEcoPawn() const
{
	return Cast<AEcoBoxCameraPawn>(GetPawn());
}

AEcoBoxRenderer* AEcoBoxPlayerController::GetRenderer() const
{
	if (CachedRenderer.IsValid())
	{
		return CachedRenderer.Get();
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AEcoBoxRenderer> It(World); It; ++It)
		{
			CachedRenderer = *It;
			return *It;
		}
	}
	return nullptr;
}
