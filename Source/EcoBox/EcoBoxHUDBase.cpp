// EcoBox - UMG 蓝图 HUD 基类实现
#include "EcoBoxHUDBase.h"

#include "EcoBoxPlayerController.h"
#include "EcoBoxSimSubsystem.h"
#include "EcoBoxSettings.h"

#include "Engine/Font.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Components/Widget.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

UEcoBoxSimSubsystem* UEcoBoxHUDBase::GetSim() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetSubsystem<UEcoBoxSimSubsystem>();
	}
	return nullptr;
}

AEcoBoxPlayerController* UEcoBoxHUDBase::GetEcoController() const
{
	return Cast<AEcoBoxPlayerController>(GetOwningPlayer());
}

// =====================================================================
// 读数
// =====================================================================

namespace
{
	/** 没拿到子系统时返回的一份空统计，保证蓝图不会读到野值。 */
	const FEEcoStats& EmptyStats()
	{
		static const FEEcoStats Stats;
		return Stats;
	}
}

int32 UEcoBoxHUDBase::GetCoins() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetEconomy().Coins : 0;
}

int32 UEcoBoxHUDBase::GetLifetimeCoins() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetEconomy().LifetimeCoins : 0;
}

float UEcoBoxHUDBase::GetGoalProgress() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return 0.f; }

	const FEEcoStats& St = Sim->GetStats();
	if (St.GoalCoins <= 0) { return 0.f; }
	return FMath::Clamp((float)St.LifetimeCoins / (float)St.GoalCoins, 0.f, 1.f);
}

float UEcoBoxHUDBase::GetCoinsPerMinute() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetStats().CoinsPerMinute : 0.f;
}

int32 UEcoBoxHUDBase::GetRabbits() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetStats().Rabbits : 0;
}

int32 UEcoBoxHUDBase::GetFoxes() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetStats().Foxes : 0;
}

float UEcoBoxHUDBase::GetGrassCoverage() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetStats().GrassCoverage : 0.f;
}

float UEcoBoxHUDBase::GetSimSpeed() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetSimSpeed() : 0.f;
}

bool UEcoBoxHUDBase::IsPaused() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->IsPaused() : true;
}

float UEcoBoxHUDBase::GetRain() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetWeather().Rain : 0.f;
}

float UEcoBoxHUDBase::GetTemperature() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetWeather().Temperature : 0.f;
}

FString UEcoBoxHUDBase::GetClockText() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetStats().ClockText : FString();
}

EPlayerTool UEcoBoxHUDBase::GetCurrentTool() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetTool() : EPlayerTool::None;
}

int32 UEcoBoxHUDBase::GetWaterCanLevel() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetEconomy().WaterCanLevel : 0;
}

float UEcoBoxHUDBase::GetWaterCanRadius() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetWaterCanRadius() : 0.f;
}

int32 UEcoBoxHUDBase::GetWaterCanUpgradeCost() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return INDEX_NONE; }
	return Sim->GetWaterCanUpgradeCost();
}

int32 UEcoBoxHUDBase::GetScytheLevel() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetEconomy().ScytheLevel : 0;
}

float UEcoBoxHUDBase::GetScytheRadius() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetScytheRadius() : 0.f;
}

int32 UEcoBoxHUDBase::GetScytheUpgradeCost() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return INDEX_NONE; }
	return Sim->GetScytheUpgradeCost();
}

int32 UEcoBoxHUDBase::GetCoinBaseLevel() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetEconomy().CoinBaseLevel : 0;
}

int32 UEcoBoxHUDBase::GetDoubleChanceLevel() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetEconomy().DoubleChanceLevel : 0;
}

int32 UEcoBoxHUDBase::GetCoinBaseUpgradeCost() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return INDEX_NONE; }
	return Sim->GetCoinUpgradeCost(Sim->GetEconomy().CoinBaseLevel);
}

int32 UEcoBoxHUDBase::GetDoubleUpgradeCost() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return INDEX_NONE; }
	return Sim->GetCoinUpgradeCost(Sim->GetEconomy().DoubleChanceLevel);
}

int32 UEcoBoxHUDBase::GetRainLevel() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetEconomy().RainLevel : 0;
}

int32 UEcoBoxHUDBase::GetRainUpgradeCost() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetRainUpgradeCost() : INDEX_NONE;
}

float UEcoBoxHUDBase::GetRainPerLevel() const
{
	return EcoParams::S().RainPerLevel;
}

int32 UEcoBoxHUDBase::GetRabbitPrice() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetCreaturePrice(ECreatureType::Rabbit) : 0;
}

int32 UEcoBoxHUDBase::GetFoxPrice() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetCreaturePrice(ECreatureType::Fox) : 0;
}

FEEcoStats UEcoBoxHUDBase::GetStatsSnapshot() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetStats() : EmptyStats();
}

// =====================================================================
// 操作
// =====================================================================

void UEcoBoxHUDBase::SetTool(EPlayerTool InTool)
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->SetTool(InTool); }
}

void UEcoBoxHUDBase::CycleSpeed(int32 Dir)
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->CycleSpeed(Dir); }
}

void UEcoBoxHUDBase::TogglePause()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->TogglePause(); }
}

void UEcoBoxHUDBase::AdjustRain(float Delta)
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustRain(Delta); }
}

void UEcoBoxHUDBase::AdjustTemperature(float Delta)
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustTemperature(Delta); }
}

void UEcoBoxHUDBase::AdjustDayLength(float Delta)
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustDayLength(Delta); }
}

void UEcoBoxHUDBase::BuyRabbit()
{
	SetTool(EPlayerTool::PlaceRabbit);
}

void UEcoBoxHUDBase::BuyFox()
{
	SetTool(EPlayerTool::PlaceFox);
}

void UEcoBoxHUDBase::UpgradeWaterCan()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	const int32 Cost = Sim->GetWaterCanUpgradeCost();
	if (Cost == INDEX_NONE)
	{
		ShowPanelMessage(TEXT("水壶已经满级了"));
		return;
	}
	if (Sim->UpgradeWaterCan())
	{
		ShowPanelMessage(FString::Printf(TEXT("水壶升到 Lv.%d，半径 %.1f 格（-%d 币）"),
			Sim->GetEconomy().WaterCanLevel, Sim->GetWaterCanRadius(), Cost));
	}
	else
	{
		ShowPanelMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"),
			Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDBase::UpgradeScythe()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	const int32 Cost = Sim->GetScytheUpgradeCost();
	if (Cost == INDEX_NONE)
	{
		ShowPanelMessage(TEXT("镰刀已经满级了"));
		return;
	}
	if (Sim->UpgradeScythe())
	{
		ShowPanelMessage(FString::Printf(TEXT("镰刀升到 Lv.%d，收割半径 %.1f 格（-%d 币）"),
			Sim->GetEconomy().ScytheLevel, Sim->GetScytheRadius(), Cost));
	}
	else
	{
		ShowPanelMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"),
			Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDBase::UpgradeCoinBase()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	const int32 Cost = Sim->GetCoinUpgradeCost(Sim->GetEconomy().CoinBaseLevel);
	if (Cost == INDEX_NONE)
	{
		ShowPanelMessage(TEXT("升级线 A 已经满级了"));
		return;
	}
	if (Sim->UpgradeCoinBase())
	{
		ShowPanelMessage(FString::Printf(TEXT("升级线 A → Lv.%d：割草 %d / 捕食 %d（-%d 币）"),
			Sim->GetEconomy().CoinBaseLevel, Sim->GetHarvestYield(), Sim->GetPredationYield(), Cost));
	}
	else
	{
		ShowPanelMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"),
			Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDBase::UpgradeCoinDouble()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	const int32 Cost = Sim->GetCoinUpgradeCost(Sim->GetEconomy().DoubleChanceLevel);
	if (Cost == INDEX_NONE)
	{
		ShowPanelMessage(TEXT("升级线 B 已经满级了"));
		return;
	}
	if (Sim->UpgradeCoinDouble())
	{
		ShowPanelMessage(FString::Printf(TEXT("升级线 B → Lv.%d：翻倍概率 %.0f%%（-%d 币）"),
			Sim->GetEconomy().DoubleChanceLevel, Sim->GetDoubleChance() * 100.f, Cost));
	}
	else
	{
		ShowPanelMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"),
			Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDBase::UpgradeRain()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	const int32 Cost = Sim->GetRainUpgradeCost();
	if (Cost == INDEX_NONE)
	{
		ShowPanelMessage(TEXT("降雨已经升满级了"));
		return;
	}
	if (Sim->UpgradeRain())
	{
		ShowPanelMessage(FString::Printf(TEXT("降雨 → Lv.%d：当前雨量 %.0f%%（-%d 币）"),
			Sim->GetEconomy().RainLevel, Sim->GetWeather().Rain * 100.f, Cost));
	}
	else
	{
		ShowPanelMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"),
			Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDBase::SeedGrass()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->SeedGrassField(); }
}

void UEcoBoxHUDBase::ResetWorld()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->ResetWorld(); }
}

void UEcoBoxHUDBase::ShowPanelMessage(const FString& Text)
{
	// 默认什么都不做 —— 想在蓝图里弹提示就覆写 On Refresh 之外的这个事件。
	// 这里留一个纯虚感觉太重，直接 broadcast 给蓝图更自然。
	OnPanelMessage(Text);
}

// =====================================================================
// 存档（纯转发到模拟子系统）
// =====================================================================

bool UEcoBoxHUDBase::SaveGame()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->SaveGame() : false;
}

bool UEcoBoxHUDBase::LoadGame()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->LoadGame() : false;
}

bool UEcoBoxHUDBase::HasSaveGame() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->HasSaveGame() : false;
}

bool UEcoBoxHUDBase::DeleteSaveGame()
{
	UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->DeleteSaveGame() : false;
}

FString UEcoBoxHUDBase::GetSaveTimeText() const
{
	const UEcoBoxSimSubsystem* Sim = GetSim();
	return Sim ? Sim->GetSaveTimeText() : FString();
}

bool UEcoBoxHUDBase::IsPointerOverPanel() const
{
	// 没登记阻挡控件 = 不挡。蓝图 HUD 不登记也能跑，代价就是点 UI 会穿透到地块。
	if (ClickBlockingWidgets.Num() == 0)
	{
		return false;
	}

	// 用 Slate 的光标位置 —— 和 UMG 自己做命中判定用的是同一套坐标，
	// 不要用 GetMousePosition()，那个读的是**视口缓存**，压在 UI 上时是冻住的旧值。
	const FVector2D MousePos = FSlateApplication::Get().GetCursorPos();

	for (const TObjectPtr<UWidget>& W : ClickBlockingWidgets)
	{
		if (!W)
		{
			continue;
		}

		const FGeometry& Geo = W->GetCachedGeometry();
		// 尺寸为 0 说明还没布局过、或者当前被隐藏 —— 都不该算"压在界面上"。
		if (Geo.GetLocalSize().IsNearlyZero())
		{
			continue;
		}

		if (Geo.IsUnderLocation(MousePos))
		{
			return true;
		}
	}

	return false;
}

void UEcoBoxHUDBase::RefreshDisplay()
{
	K2_OnRefresh();
}

FSlateFontInfo UEcoBoxHUDBase::GetFont(int32 Size) const
{
	// 指派了字体资产就用它（含中文的字体就靠这个）。
	// 没指派时退回引擎 Roboto —— **只保证英文/数字**，中文会乱码，
	// 但至少不会崩。
	if (UIFont)
	{
		return FSlateFontInfo(UIFont, (float)Size);
	}
	return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
}

// =====================================================================
// 跟随鼠标的工具图标（水壶 / 镰刀）
// =====================================================================

TSharedRef<SWidget> UEcoBoxToolCursorWidget::RebuildWidget()
{
	// 纯 C++ 控件：没有 WidgetTree 就自己造一个
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}

	if (!Layer && WidgetTree)
	{
		Layer = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CursorLayer"));
		// 只负责"画"，绝不参与点击
		Layer->SetVisibility(ESlateVisibility::HitTestInvisible);
		WidgetTree->RootWidget = Layer;

		IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CursorIcon"));
		IconImage->SetVisibility(ESlateVisibility::Collapsed);
		// 不透明：图标要**完全盖住**下面那个系统光标箭头。
		//
		// 为什么不做半透明：`FInputModeGameAndUI` 下系统光标在"视口还没捕获鼠标"时
		// 是强行显示的，`bShowMouseCursor=false` 要等到玩家点一下才生效。
		// 用不透明的图标把箭头盖住，选完工具当场就是"换了个光标"的观感，不用先点一下。
		IconImage->SetOpacity(1.0f);
		// 128 而不是 64/96：要盖住箭头，就得比箭头那一小块明显大一圈
		IconImage->SetDesiredSizeOverride(FVector2D(128.f, 128.f));

		IconSlot = Cast<UCanvasPanelSlot>(Layer->AddChild(IconImage));
		if (IconSlot)
		{
			IconSlot->SetAutoSize(true);
			// 图标**中心**对准鼠标点 —— 既然是顶替光标，就该以那个点为中心
			IconSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			IconSlot->SetPosition(FVector2D::ZeroVector);
		}
	}

	return Super::RebuildWidget();
}

void UEcoBoxToolCursorWidget::SetIcon(UTexture2D* Texture)
{
	if (!IconImage) { return; }

	if (Texture)
	{
		IconImage->SetBrushFromTexture(Texture, false);
		IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		IconImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UEcoBoxToolCursorWidget::SetCursorPosition(const FVector2D& ViewportPos)
{
	if (IconSlot) { IconSlot->SetPosition(ViewportPos); }
}

void UEcoBoxToolCursorWidget::SetCursorVisible(bool bVisible)
{
	if (IconImage)
	{
		IconImage->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UEcoBoxHUDBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateToolCursor();
}

void UEcoBoxHUDBase::UpdateToolCursor()
{
	AEcoBoxPlayerController* PC = GetEcoController();
	if (!PC) { return; }

	// 这层是运行时自己加的：蓝图 HUD 的控件树里没法预置 C++ 控件，
	// 只能单独建一个 Widget 叠在视口最上层。
	if (!ToolCursorWidget)
	{
		ToolCursorWidget = CreateWidget<UEcoBoxToolCursorWidget>(this, UEcoBoxToolCursorWidget::StaticClass());
		if (!ToolCursorWidget) { return; }
		ToolCursorWidget->AddToViewport(100);
	}

	const EPlayerTool Tool = GetCurrentTool();

	// 只有"手里拿着水壶 / 镰刀"时才用物品图顶替光标；
	// 鼠标压到控制面板上时让位给系统光标 —— 不然玩家看不见点在哪。
	const bool bWantToolCursor =
		(Tool == EPlayerTool::WaterCan || Tool == EPlayerTool::Scythe) && !IsPointerOverPanel();

	if (!bWantToolCursor)
	{
		ToolCursorWidget->SetCursorVisible(false);

		PC->SetShowMouseCursor(true);
		SetCursor(EMouseCursor::Default);          // Slate 层恢复箭头
		bCursorHiddenByTool = false;
		LastCursorTool = EPlayerTool::None;
		return;
	}

	// 换图：只在工具变化时做，避免每帧重建 Brush
	if (Tool != LastCursorTool)
	{
		LastCursorTool = Tool;

		if (Tool == EPlayerTool::WaterCan)
		{
			if (!WateringCanIcon)
			{
				WateringCanIcon = LoadObject<UTexture2D>(nullptr, TEXT("/Game/images/watering_can.watering_can"));
			}
			ToolCursorWidget->SetIcon(WateringCanIcon);
		}
		else
		{
			if (!SickleIcon)
			{
				SickleIcon = LoadObject<UTexture2D>(nullptr, TEXT("/Game/images/sickle.sickle"));
			}
			ToolCursorWidget->SetIcon(SickleIcon);
		}
	}

	// 跟到鼠标（GetMousePositionOnViewport 已含 DPI 缩放，和 Canvas 坐标一致）
	ToolCursorWidget->SetCursorPosition(UWidgetLayoutLibrary::GetMousePositionOnViewport(this));
	ToolCursorWidget->SetCursorVisible(true);

	// ---- 让系统光标让位 ----
	//
	// 两道保险，**每帧都设**（成本极低，别用"只在变化时设"的缓存）：
	//   1. `SetShowMouseCursor(false)` —— 关掉 PlayerController 的光标层；
	//   2. `SetCursor(EMouseCursor::None)` —— 让 Slate 在 HUD 上不画那个箭头。
	//
	// 为什么需要 2：项目用的是 `FInputModeGameAndUI`，这个模式下光标由 Slate 的
	// UI 层负责绘制，单靠 `bShowMouseCursor` 有时候盖不住，会出现"物品图 + 箭头"
	// 两个光标叠在一起的观感。
	bCursorHiddenByTool = true;
	PC->SetShowMouseCursor(false);
	SetCursor(EMouseCursor::None);
}
