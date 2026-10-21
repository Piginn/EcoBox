// EcoBox - 控制面板实现
#include "EcoBoxHUDWidget.h"

#include "EcoBoxPlayerController.h"
#include "EcoBoxSimSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/Widget.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Styling/CoreStyle.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"

namespace EcoUI
{
	/**
	 * 界面字体。
	 *
	 * ⚠ 不要用 FCoreStyle::GetDefaultFontStyle —— 那套是 Roboto，**不含中文字形**。
	 * 也**不要**用 "/Engine/Content/Slate/Fonts/DroidSans.tps" 这类路径构造：
	 * .tps 是 Slate 的复合字体**描述文件**，不是字体本体，运行时会报
	 * "Failed to read file ... DroidSans.tps" 然后全部字形取不到 → 中文乱码。
	 *
	 * 正解：把 Windows 的中文字体（微软雅黑 msyh.ttc / 黑体 simhei.ttf）导入成
	 * UFont 资产，指派给 UEcoBoxHUDBase::UIFont，这里就会用它。
	 * 没指派时退回 Roboto（英文正常、中文乱码，但不会崩）。
	 */
	FSlateFontInfo GetUIFont(const UEcoBoxHUDBase* Owner, int32 Size, bool /*bBold*/ = false)
	{
		if (!Owner)
		{
			return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
		}
		return Owner->GetFont(Size);
	}

	const FLinearColor TextBright(0.96f, 0.96f, 0.94f, 1.f);
	const FLinearColor TextDim(0.70f, 0.74f, 0.78f, 1.f);
	const FLinearColor TextAccent(0.55f, 0.85f, 0.55f, 1.f);
	const FLinearColor TextWarn(0.92f, 0.62f, 0.32f, 1.f);
	/** 买不起的时候用，暗到明显"按不动"。 */
	const FLinearColor TextLocked(0.45f, 0.47f, 0.50f, 1.f);

	const FLinearColor ButtonNormal(0.16f, 0.18f, 0.22f, 1.f);
	const FLinearColor ButtonSelected(0.20f, 0.42f, 0.24f, 1.f);

	const FLinearColor TabNormal(0.13f, 0.15f, 0.19f, 1.f);
	const FLinearColor TabOpen(0.17f, 0.31f, 0.22f, 1.f);

	/** 上下两条横条的底色，半透明深色，别挡太多画面。 */
	const FLinearColor BarBg(0.03f, 0.04f, 0.06f, 0.80f);
	const FLinearColor ChipBg(0.13f, 0.15f, 0.20f, 0.92f);

	const FLinearColor GoalFill(0.35f, 0.72f, 0.35f, 1.f);
}

// =====================================================================

TSharedRef<SWidget> UEcoBoxHUDWidget::RebuildWidget()
{
	// 纯 C++ 控件必须在有 WidgetTree 的前提下才能建树，这里兜一层
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}

	if (!bLayoutBuilt && WidgetTree)
	{
		BuildLayout();
		bLayoutBuilt = true;
	}
	return Super::RebuildWidget();
}

bool UEcoBoxHUDWidget::IsPointerOverPanel() const
{
	// 现在是上下两条，鼠标压在任一条上都算"在 UI 上"。
	// 注意局部变量别叫 Cursor —— UUserWidget 自己有个 Cursor 成员，会撞名报 C4458。
	const FVector2D MousePos = FSlateApplication::Get().GetCursorPos();

	auto Hits = [&MousePos](const UWidget* W) -> bool
	{
		if (!W) { return false; }
		const FGeometry& Geo = W->GetCachedGeometry();
		if (Geo.GetLocalSize().IsNearlyZero()) { return false; }
		return Geo.IsUnderLocation(MousePos);
	};

	return Hits(TopBar) || Hits(BottomBar);
}

void UEcoBoxHUDWidget::ShowMessage(const FString& Text)
{
	if (!MessageText) { return; }

	MessageText->SetText(FText::FromString(Text));
	// 用 Hidden 而不是 Collapsed：Hidden 仍然占着那一行的高度，
	// 提示出现/消失时底栏不会忽高忽低地跳。
	MessageText->SetVisibility(ESlateVisibility::HitTestInvisible);

	const UWorld* World = GetWorld();
	MessageExpireTime = (World ? World->GetTimeSeconds() : 0.f) + MessageSeconds;
}

// =====================================================================
// 控件工厂
// =====================================================================

UTextBlock* UEcoBoxHUDWidget::MakeText(const TCHAR* Name, int32 FontSize, const FLinearColor& Color, bool bBold)
{
	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	T->SetFont(EcoUI::GetUIFont(this, FontSize, bBold));
	T->SetColorAndOpacity(FSlateColor(Color));
	T->SetShadowOffset(FVector2D(1.f, 1.f));
	T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	return T;
}

UTextBlock* UEcoBoxHUDWidget::MakeLabel(const TCHAR* Name, const TCHAR* Text)
{
	UTextBlock* T = MakeText(Name, 10, EcoUI::TextDim);
	T->SetText(FText::FromString(Text));
	return T;
}

UButton* UEcoBoxHUDWidget::MakeButton(const TCHAR* Name, const FString& Label)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	B->SetBackgroundColor(EcoUI::ButtonNormal);

	UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Pad->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	Pad->SetPadding(FMargin(12.f, 5.f));

	UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	T->SetFont(EcoUI::GetUIFont(this, 13));
	T->SetText(FText::FromString(Label));
	T->SetColorAndOpacity(FSlateColor(EcoUI::TextBright));

	Pad->SetContent(T);
	B->AddChild(Pad);

	return B;
}

UTextBlock* UEcoBoxHUDWidget::GetButtonText(const UButton* Button)
{
	if (!Button) { return nullptr; }
	if (const UBorder* Pad = Cast<UBorder>(Button->GetContent()))
	{
		return Cast<UTextBlock>(Pad->GetContent());
	}
	return nullptr;
}

UHorizontalBox* UEcoBoxHUDWidget::MakeRow(const TCHAR* Name)
{
	return WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), Name);
}

void UEcoBoxHUDWidget::AddToBottomBox(UWidget* W, float TopPad)
{
	if (!BottomBox || !W) { return; }
	if (UVerticalBoxSlot* VSlot = BottomBox->AddChildToVerticalBox(W))
	{
		VSlot->SetPadding(FMargin(0.f, TopPad, 0.f, 0.f));
		VSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void UEcoBoxHUDWidget::AppendLabel(UHorizontalBox* Row, const TCHAR* Name, const TCHAR* Text, float LeftPad)
{
	if (!Row) { return; }
	UTextBlock* L = MakeLabel(Name, Text);
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(L))
	{
		S->SetPadding(FMargin(LeftPad, 0.f, 8.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}
}

void UEcoBoxHUDWidget::AddStatChip(UHorizontalBox* Row, const TCHAR* Name, const FString& Label,
	TObjectPtr<UTextBlock>& OutValue)
{
	if (!Row) { return; }

	UBorder* Chip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
	Chip->SetBrushColor(EcoUI::ChipBg);
	Chip->SetPadding(FMargin(8.f, 3.f, 9.f, 3.f));

	UHorizontalBox* Inner = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Chip->SetContent(Inner);

	// 小号暗色标签
	UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	L->SetFont(EcoUI::GetUIFont(this, 9));
	L->SetText(FText::FromString(Label));
	L->SetColorAndOpacity(FSlateColor(EcoUI::TextDim));
	if (UHorizontalBoxSlot* S = Inner->AddChildToHorizontalBox(L))
	{
		S->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}

	// 大号亮色数值
	UTextBlock* V = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	V->SetFont(EcoUI::GetUIFont(this, 14, true));
	V->SetColorAndOpacity(FSlateColor(EcoUI::TextBright));
	V->SetText(FText::FromString(TEXT("-")));
	Inner->AddChildToHorizontalBox(V);

	OutValue = V;

	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Chip))
	{
		S->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}
}

UButton* UEcoBoxHUDWidget::MakeTabButton(UHorizontalBox* Row, const TCHAR* Name, const FString& Title,
	TObjectPtr<UTextBlock>& OutSummary)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	B->SetBackgroundColor(EcoUI::TabNormal);

	UBorder* Pad = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Pad->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	Pad->SetPadding(FMargin(11.f, 5.f));

	UHorizontalBox* Inner = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Pad->SetContent(Inner);
	B->AddChild(Pad);

	// UButton 的内容槽默认居中，不改成 Fill 的话内容会缩在按钮中间
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(B->GetContentSlot()))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetVerticalAlignment(VAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 0.f));
	}

	UTextBlock* TitleBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	TitleBlock->SetFont(EcoUI::GetUIFont(this, 12, true));
	TitleBlock->SetText(FText::FromString(Title));
	TitleBlock->SetColorAndOpacity(FSlateColor(EcoUI::TextBright));
	if (UHorizontalBoxSlot* S = Inner->AddChildToHorizontalBox(TitleBlock))
	{
		S->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* Summary = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Summary->SetFont(EcoUI::GetUIFont(this, 10));
	Summary->SetColorAndOpacity(FSlateColor(EcoUI::TextDim));
	Summary->SetText(FText::FromString(TEXT("")));
	if (UHorizontalBoxSlot* S = Inner->AddChildToHorizontalBox(Summary))
	{
		S->SetPadding(FMargin(9.f, 0.f, 0.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}

	OutSummary = Summary;
	// 标题 / 摘要按加入顺序存起来，索引和标签编号一一对应
	SectionTitles.Add(TitleBlock);
	SectionSummaries.Add(Summary);

	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(B))
	{
		S->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}

	return B;
}

UHorizontalBox* UEcoBoxHUDWidget::AddSectionBody(const TCHAR* Name)
{
	UHorizontalBox* Body = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), Name);
	Body->SetVisibility(ESlateVisibility::Collapsed);

	// 插在标签行之前，所以展开是往上长，标签行永远贴着屏幕底边
	AddToBottomBox(Body, 0.f);

	SectionBodies.Add(Body);
	SectionOpen.Add(false);
	return Body;
}

// =====================================================================
// 搭面板
// =====================================================================

void UEcoBoxHUDWidget::BuildLayout()
{
	if (!WidgetTree) { return; }

	// 根用 Overlay：子项可以单独指定"横向撑满 + 纵向贴顶/贴底 + 高度由内容决定"
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	WidgetTree->RootWidget = Root;

	BuildTopBar(Root);
	BuildBottomBar(Root);

	UpdateToolButtonStyles();

	// 打一行日志，用来确认控件树真的建起来了
	//（纯 C++ 的 HUD 如果没显示，第一件事就是看这行在不在）
	UE_LOG(LogTemp, Log, TEXT("[EcoBox] 控制面板已搭建：%d 个分区 TIME/WEATHER/TOOLS/SHOP/UPGRADE/FIELD"),
		(int32)SecCount);
}

// ---------- 顶部读数条 ----------
void UEcoBoxHUDWidget::BuildTopBar(UOverlay* Root)
{
	TopBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TopBar"));
	TopBar->SetBrushColor(EcoUI::BarBg);
	TopBar->SetPadding(FMargin(16.f, 8.f));

	// HAlign_Fill 让横条铺满窗口宽度；VAlign_Top 让它贴住上边缘、高度由内容决定
	if (UOverlaySlot* S = Root->AddChildToOverlay(TopBar))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Top);
	}

	// 顶栏是"读数那一行 + 下面压一条目标进度条"两行，所以外面套一个纵向盒
	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TopStack"));
	TopBar->SetContent(Stack);

	TopBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TopBox"));
	Stack->AddChildToVerticalBox(TopBox);

	// 左起一字排开的读数。
	// 币后面紧跟"币/分钟"：增量游戏里"我现在赚得多快"比"我现在有多少"更重要 ——
	// 升级值不值、该不该多买狐狸，全靠这个数决定。
	AddStatChip(TopBox, TEXT("StatCoins"),  TEXT("生态币"),   StatCoins);
	AddStatChip(TopBox, TEXT("StatIncome"), TEXT("币/分钟"),   StatIncome);
	AddStatChip(TopBox, TEXT("StatRabbit"), TEXT("兔子"),      StatRabbit);
	AddStatChip(TopBox, TEXT("StatFox"),    TEXT("狐狸"),      StatFox);
	AddStatChip(TopBox, TEXT("StatGrass"),  TEXT("草地"),      StatGrass);
	AddStatChip(TopBox, TEXT("StatSpeed"),  TEXT("速度"),      StatSpeed);
	AddStatChip(TopBox, TEXT("StatRain"),   TEXT("降雨"),      StatRain);
	AddStatChip(TopBox, TEXT("StatTemp"),   TEXT("温度"),      StatTemp);
	AddStatChip(TopBox, TEXT("StatSun"),    TEXT("日照"),      StatSun);

	// 右端：时钟与累计统计。占满剩余宽度并把文字右对齐，自动顶到屏幕右上角
	DetailText = MakeText(TEXT("DetailText"), 10, EcoUI::TextDim);
	DetailText->SetJustification(ETextJustify::Right);
	if (UHorizontalBoxSlot* S = TopBox->AddChildToHorizontalBox(DetailText))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		S->SetPadding(FMargin(12.f, 0.f, 6.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}

	// 目标进度条：增量游戏最核心的一条反馈，一眼看到"离 1000 还差多少"
	GoalBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("GoalBar"));
	GoalBar->SetPercent(0.f);
	GoalBar->SetFillColorAndOpacity(EcoUI::GoalFill);
	if (UVerticalBoxSlot* S = Stack->AddChildToVerticalBox(GoalBar))
	{
		S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		S->SetHorizontalAlignment(HAlign_Fill);
	}
}

// ---------- 底部操作条 ----------
void UEcoBoxHUDWidget::BuildBottomBar(UOverlay* Root)
{
	BottomBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BottomBar"));
	BottomBar->SetBrushColor(EcoUI::BarBg);
	BottomBar->SetPadding(FMargin(16.f, 8.f));

	// 同样铺满宽度，但贴住下边缘。展开分区时条往上长，标签行始终贴着屏幕底
	if (UOverlaySlot* S = Root->AddChildToOverlay(BottomBar))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Bottom);
	}

	BottomBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BottomBox"));
	BottomBar->SetContent(BottomBox);

	// 瞬时提示那一行。用 Hidden 占位，所以它出现/消失不会让底栏高度跳动。
	MessageText = MakeText(TEXT("MessageText"), 13, EcoUI::TextAccent, true);
	MessageText->SetJustification(ETextJustify::Center);
	MessageText->SetVisibility(ESlateVisibility::Hidden);
	AddToBottomBox(MessageText, 2.f);

	BuildSections();
}

void UEcoBoxHUDWidget::BuildSections()
{
	// 1) 先建五行内容体（默认隐藏）。它们先加入 BottomBox，
	//    所以展开时出现在标签行上方，底栏往上长。
	UHorizontalBox* Body = nullptr;

	Body = AddSectionBody(TEXT("TimeBody"));
	BuildSectionTime(Body);

	Body = AddSectionBody(TEXT("WeatherBody"));
	BuildSectionWeather(Body);

	Body = AddSectionBody(TEXT("ToolsBody"));
	BuildSectionTools(Body);

	Body = AddSectionBody(TEXT("ShopBody"));
	BuildSectionShop(Body);

	Body = AddSectionBody(TEXT("UpgradeBody"));
	BuildSectionUpgrade(Body);

	Body = AddSectionBody(TEXT("FieldBody"));
	BuildSectionField(Body);

	// 2) 标签行（永远贴着屏幕底边）
	UHorizontalBox* TabRow = MakeRow(TEXT("TabRow"));
	AddToBottomBox(TabRow, 8.f);

	TObjectPtr<UTextBlock> Summary;

	SectionTabs.Add(MakeTabButton(TabRow, TEXT("TabTime"),    TEXT("时间"),    Summary));
	SecTimeSummary = Summary;

	SectionTabs.Add(MakeTabButton(TabRow, TEXT("TabWeather"), TEXT("天气"),    Summary));
	SecWeatherSummary = Summary;

	SectionTabs.Add(MakeTabButton(TabRow, TEXT("TabTools"),   TEXT("工具"),    Summary));
	SecToolsSummary = Summary;

	SectionTabs.Add(MakeTabButton(TabRow, TEXT("TabShop"),    TEXT("商店"),    Summary));
	SecShopSummary = Summary;

	SectionTabs.Add(MakeTabButton(TabRow, TEXT("TabUpgrade"), TEXT("升级"),    Summary));
	SecUpgradeSummary = Summary;

	SectionTabs.Add(MakeTabButton(TabRow, TEXT("TabField"),   TEXT("场地"),    Summary));
	SecFieldSummary = Summary;

	// 3) 标签行右端：操作提示
	HintText = MakeText(TEXT("HintText"), 10, EcoUI::TextDim);
	HintText->SetText(FText::FromString(
		TEXT("1 水壶 | 2 镰刀 | 3 买兔 | 4 买狐 | 左键使用（按住可连续）| 右键拖动平移 | WASD 平移 | 滚轮缩放 | Q/E 旋转 | F 回中")));
	HintText->SetJustification(ETextJustify::Right);
	if (UHorizontalBoxSlot* S = TabRow->AddChildToHorizontalBox(HintText))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		S->SetPadding(FMargin(16.f, 0.f, 4.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}

	// 4) 绑定。AddDynamic 的函数名是编译期从字面 token 裁出来的，
	//    第二个参数必须直接写 &UClass::Func，不能过变量/循环/lambda。
	SectionTabs[SecTime]->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnToggleTime);
	SectionTabs[SecWeather]->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnToggleWeather);
	SectionTabs[SecTools]->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnToggleTools);
	SectionTabs[SecShop]->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnToggleShop);
	SectionTabs[SecUpgrade]->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnToggleUpgrade);
	SectionTabs[SecField]->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnToggleField);

	// 全部收起
	for (int32 i = 0; i < SecCount; ++i)
	{
		SetSectionOpen(i, false);
	}
}

// ---------- TIME：速度 ----------
void UEcoBoxHUDWidget::BuildSectionTime(UHorizontalBox* Body)
{
	UButton* Down = MakeButton(TEXT("SpeedDown"), TEXT("-"));
	Down->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnSpeedDown);
	Body->AddChildToHorizontalBox(Down);

	UButton* Pause = MakeButton(TEXT("PauseButton"), TEXT("暂停"));
	Pause->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnPauseToggle);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(Pause)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }

	UButton* Up = MakeButton(TEXT("SpeedUp"), TEXT("+"));
	Up->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnSpeedUp);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(Up)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }

	AppendLabel(Body, TEXT("SpeedNote"), TEXT("档位：0 / 0.5 / 1 / 2 / 4 / 8 / 16 倍"), 14.f);
}

// ---------- WEATHER：降雨 / 温度 / 昼夜，横排一行 ----------
void UEcoBoxHUDWidget::BuildSectionWeather(UHorizontalBox* Body)
{
	// 说明：三组刻意写开，没有抽成循环。
	// AddDynamic 的函数名是编译期从源码字面 token 里裁出来的
	// （UE_PRIVATE_STATIC_FUNCTION_FNAME → TrimmedMemberFunctionName），
	// 第二个参数必须是直接写出来的 &UClass::Func，不能经过变量 / 循环 / lambda。

	// ---- 降雨 ----
	AppendLabel(Body, TEXT("RainLabel"), TEXT("降雨量"), 0.f);

	UButton* RainDown = MakeButton(TEXT("RainDown"), TEXT("-"));
	RainDown->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnRainDown);
	Body->AddChildToHorizontalBox(RainDown);

	UButton* RainUp = MakeButton(TEXT("RainUp"), TEXT("+"));
	RainUp->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnRainUp);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(RainUp)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }

	// ---- 温度 ----
	AppendLabel(Body, TEXT("TempLabel"), TEXT("温度"), 22.f);

	UButton* TempDown = MakeButton(TEXT("TempDown"), TEXT("-"));
	TempDown->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnTempDown);
	Body->AddChildToHorizontalBox(TempDown);

	UButton* TempUp = MakeButton(TEXT("TempUp"), TEXT("+"));
	TempUp->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnTempUp);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(TempUp)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }

	// ---- 昼夜长度 ----
	AppendLabel(Body, TEXT("DayLabel"), TEXT("昼夜长度"), 22.f);

	UButton* DayDown = MakeButton(TEXT("DayDown"), TEXT("-"));
	DayDown->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnDayDown);
	Body->AddChildToHorizontalBox(DayDown);

	UButton* DayUp = MakeButton(TEXT("DayUp"), TEXT("+"));
	DayUp->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnDayUp);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(DayUp)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }
}

// ---------- TOOLS：水壶 / 镰刀 + 两把工具的升级 ----------
void UEcoBoxHUDWidget::BuildSectionTools(UHorizontalBox* Body)
{
	AppendLabel(Body, TEXT("ToolPickLabel"), TEXT("手持工具"), 0.f);

	ToolWaterButton = MakeButton(TEXT("ToolWaterCan"), TEXT("水壶"));
	ToolWaterButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnToolWaterCan);
	ToolWaterLabel = GetButtonText(ToolWaterButton);
	Body->AddChildToHorizontalBox(ToolWaterButton);

	ToolScytheButton = MakeButton(TEXT("ToolScythe"), TEXT("镰刀"));
	ToolScytheButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnToolScythe);
	ToolScytheLabel = GetButtonText(ToolScytheButton);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(ToolScytheButton)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }

	// 水壶升级：等级 / 当前半径 / 下一级价格都写在按钮上，
	// 否则玩家根本不知道自己买的是什么、还差多少钱。
	AppendLabel(Body, TEXT("CanLabel"), TEXT("水壶升级"), 22.f);

	UpgradeCanButton = MakeButton(TEXT("UpgradeWaterCan"), TEXT("升级"));
	UpgradeCanButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnUpgradeCan);
	UpgradeCanLabel = GetButtonText(UpgradeCanButton);
	Body->AddChildToHorizontalBox(UpgradeCanButton);

	ToolInfoText = MakeText(TEXT("ToolInfo"), 10, EcoUI::TextDim);
	ToolInfoText->SetText(FText::FromString(TEXT("Lv.0  半径 0.0 格")));
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(ToolInfoText))
	{
		S->SetPadding(FMargin(9.f, 0.f, 0.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}

	// 镰刀升级：和水壶对称，每级扩大一次收割的圆形范围。
	AppendLabel(Body, TEXT("ScytheLabel"), TEXT("镰刀升级"), 22.f);

	UpgradeScytheButton = MakeButton(TEXT("UpgradeScythe"), TEXT("升级"));
	UpgradeScytheButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnUpgradeScythe);
	UpgradeScytheLabel = GetButtonText(UpgradeScytheButton);
	Body->AddChildToHorizontalBox(UpgradeScytheButton);

	ScytheInfoText = MakeText(TEXT("ScytheInfo"), 10, EcoUI::TextDim);
	ScytheInfoText->SetText(FText::FromString(TEXT("Lv.0  半径 0.0 格")));
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(ScytheInfoText))
	{
		S->SetPadding(FMargin(9.f, 0.f, 0.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}
}

// ---------- SHOP：花钱买兔子 / 狐狸 ----------
void UEcoBoxHUDWidget::BuildSectionShop(UHorizontalBox* Body)
{
	AppendLabel(Body, TEXT("BuyLabel"), TEXT("购买（再点地面放下）"), 0.f);

	BuyRabbitButton = MakeButton(TEXT("BuyRabbit"), TEXT("兔子 5"));
	BuyRabbitButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnBuyRabbit);
	BuyRabbitLabel = GetButtonText(BuyRabbitButton);
	Body->AddChildToHorizontalBox(BuyRabbitButton);

	BuyFoxButton = MakeButton(TEXT("BuyFox"), TEXT("狐狸 20"));
	BuyFoxButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnBuyFox);
	BuyFoxLabel = GetButtonText(BuyFoxButton);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(BuyFoxButton)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }

	// 免费投放：留着调试用（想看种群爆炸、想快速验证捕食链路时比赚钱快得多）
	AppendLabel(Body, TEXT("DebugSpawnLabel"), TEXT("调试 · 免费"), 22.f);

	UButton* SpawnR = MakeButton(TEXT("SpawnRabbits"), TEXT("+5 兔子"));
	SpawnR->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnSpawnRabbits);
	Body->AddChildToHorizontalBox(SpawnR);

	UButton* SpawnF = MakeButton(TEXT("SpawnFoxes"), TEXT("+3 狐狸"));
	SpawnF->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnSpawnFoxes);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(SpawnF)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }
}

// ---------- UPGRADE：生态币的两条升级线 ----------
void UEcoBoxHUDWidget::BuildSectionUpgrade(UHorizontalBox* Body)
{
	// 升级线 A：每级让"割草 / 捕食"的收益 +1。
	// 刻意**两条都加成**（不只是捕食）—— 否则 1000 币要靠割 1000 次草，节奏撑不住。
	AppendLabel(Body, TEXT("BaseLineLabel"), TEXT("收益加成"), 0.f);

	UpgradeBaseButton = MakeButton(TEXT("UpgradeCoinBase"), TEXT("线路 A  升级"));
	UpgradeBaseButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnUpgradeBase);
	UpgradeBaseLabel = GetButtonText(UpgradeBaseButton);
	Body->AddChildToHorizontalBox(UpgradeBaseButton);

	UpgradeBaseInfo = MakeText(TEXT("UpgradeBaseInfo"), 10, EcoUI::TextDim);
	UpgradeBaseInfo->SetText(FText::FromString(TEXT("Lv.0  割草 1  捕食 1")));
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(UpgradeBaseInfo))
	{
		S->SetPadding(FMargin(9.f, 0.f, 0.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}

	// 升级线 B：每级 +5% 概率让这一次收获翻倍。
	AppendLabel(Body, TEXT("DoubleLineLabel"), TEXT("翻倍概率"), 22.f);

	UpgradeDoubleButton = MakeButton(TEXT("UpgradeCoinDouble"), TEXT("线路 B  升级"));
	UpgradeDoubleButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnUpgradeDouble);
	UpgradeDoubleLabel = GetButtonText(UpgradeDoubleButton);
	Body->AddChildToHorizontalBox(UpgradeDoubleButton);

	UpgradeDoubleInfo = MakeText(TEXT("UpgradeDoubleInfo"), 10, EcoUI::TextDim);
	UpgradeDoubleInfo->SetText(FText::FromString(TEXT("Lv.0  当前 0%")));
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(UpgradeDoubleInfo))
	{
		S->SetPadding(FMargin(9.f, 0.f, 0.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}

	// 降雨升级：每级 +1% 雨量。雨越大，草地越能自己保湿，越不依赖水壶。
	AppendLabel(Body, TEXT("RainLineLabel"), TEXT("降雨量"), 22.f);

	UpgradeRainButton = MakeButton(TEXT("UpgradeRain"), TEXT("降雨  升级"));
	UpgradeRainButton->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnUpgradeRain);
	UpgradeRainLabel = GetButtonText(UpgradeRainButton);
	Body->AddChildToHorizontalBox(UpgradeRainButton);

	UpgradeRainInfo = MakeText(TEXT("UpgradeRainInfo"), 10, EcoUI::TextDim);
	UpgradeRainInfo->SetText(FText::FromString(TEXT("Lv.0  雨量 0%")));
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(UpgradeRainInfo))
	{
		S->SetPadding(FMargin(9.f, 0.f, 0.f, 0.f));
		S->SetVerticalAlignment(VAlign_Center);
	}
}

// ---------- FIELD：场地 ----------
void UEcoBoxHUDWidget::BuildSectionField(UHorizontalBox* Body)
{
	UButton* Seed = MakeButton(TEXT("SeedGrass"), TEXT("种草"));
	Seed->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnSeedGrass);
	Body->AddChildToHorizontalBox(Seed);

	UButton* Reset = MakeButton(TEXT("ResetWorld"), TEXT("重置世界"));
	Reset->OnClicked.AddDynamic(this, &UEcoBoxHUDWidget::OnReset);
	if (UHorizontalBoxSlot* S = Body->AddChildToHorizontalBox(Reset)) { S->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f)); }

	AppendLabel(Body, TEXT("FieldNote"), TEXT("种草铺满草地 / 重置世界（生态币也清零）"), 14.f);
}

// =====================================================================
// 折叠
// =====================================================================

void UEcoBoxHUDWidget::ToggleSection(int32 Index)
{
	if (!SectionOpen.IsValidIndex(Index)) { return; }

	// 手风琴：打开一个就把别的收起。
	// 这样底栏高度恒定，不会因为开了好几个分区把视野吃掉一大块。
	const bool bWasOpen = SectionOpen[Index];
	for (int32 i = 0; i < SectionOpen.Num(); ++i)
	{
		SetSectionOpen(i, (!bWasOpen && i == Index));
	}
}

void UEcoBoxHUDWidget::SetSectionOpen(int32 Index, bool bOpen)
{
	if (!SectionOpen.IsValidIndex(Index)) { return; }
	SectionOpen[Index] = bOpen;

	if (SectionBodies.IsValidIndex(Index) && SectionBodies[Index])
	{
		SectionBodies[Index]->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (SectionTabs.IsValidIndex(Index) && SectionTabs[Index])
	{
		SectionTabs[Index]->SetBackgroundColor(bOpen ? EcoUI::TabOpen : EcoUI::TabNormal);
	}
	if (SectionTitles.IsValidIndex(Index) && SectionTitles[Index])
	{
		SectionTitles[Index]->SetColorAndOpacity(FSlateColor(bOpen ? EcoUI::TextAccent : EcoUI::TextBright));
	}
}

void UEcoBoxHUDWidget::OnToggleTime()    { ToggleSection(SecTime); }
void UEcoBoxHUDWidget::OnToggleWeather() { ToggleSection(SecWeather); }
void UEcoBoxHUDWidget::OnToggleTools()   { ToggleSection(SecTools); }
void UEcoBoxHUDWidget::OnToggleShop()    { ToggleSection(SecShop); }
void UEcoBoxHUDWidget::OnToggleUpgrade() { ToggleSection(SecUpgrade); }
void UEcoBoxHUDWidget::OnToggleField()   { ToggleSection(SecField); }

// =====================================================================
// 样式
// =====================================================================

void UEcoBoxHUDWidget::UpdateToolButtonStyles()
{
	AEcoBoxPlayerController* PC = GetEcoController();
	const EPlayerTool Tool = PC ? PC->GetTool() : EPlayerTool::None;

	const bool bWater  = (Tool == EPlayerTool::WaterCan);
	const bool bScythe = (Tool == EPlayerTool::Scythe);

	if (ToolWaterButton)
	{
		ToolWaterButton->SetBackgroundColor(bWater ? EcoUI::ButtonSelected : EcoUI::ButtonNormal);
	}
	if (ToolScytheButton)
	{
		ToolScytheButton->SetBackgroundColor(bScythe ? EcoUI::ButtonSelected : EcoUI::ButtonNormal);
	}
	if (ToolWaterLabel)
	{
		ToolWaterLabel->SetColorAndOpacity(FSlateColor(bWater ? EcoUI::TextAccent : EcoUI::TextBright));
	}
	if (ToolScytheLabel)
	{
		ToolScytheLabel->SetColorAndOpacity(FSlateColor(bScythe ? EcoUI::TextAccent : EcoUI::TextBright));
	}
}

// =====================================================================
// 每帧刷新读数
// =====================================================================

void UEcoBoxHUDWidget::RefreshDisplay()
{
	AEcoBoxPlayerController* PC = GetEcoController();
	if (!PC) { return; }

	UEcoBoxSimSubsystem* Sim = PC->GetSim();
	if (!Sim) { return; }

	const FEEcoStats& St = Sim->GetStats();
	const FEWeather& W = Sim->GetWeather();
	const FEEconomy& Eco = Sim->GetEconomy();
	const EPlayerTool Tool = Sim->GetTool();

	const bool bPaused = Sim->IsPaused();
	const FString SpeedStr = bPaused ? TEXT("暂停") : FString::Printf(TEXT("%.3g 倍"), Sim->GetSimSpeed());

	// ---------- 顶部读数条 ----------
	if (StatCoins)
	{
		StatCoins->SetText(FText::FromString(FString::FromInt(Eco.Coins)));
		// 到目标就变绿
		StatCoins->SetColorAndOpacity(FSlateColor(
			St.LifetimeCoins >= St.GoalCoins ? EcoUI::TextAccent : EcoUI::TextBright));
	}
	if (StatIncome)
	{
		StatIncome->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), St.CoinsPerMinute)));
		// 有收入就绿色，断了就变灰 —— 一眼看出生态是不是在产出
		StatIncome->SetColorAndOpacity(FSlateColor(
			St.CoinsPerMinute >= 1.f ? EcoUI::TextAccent : EcoUI::TextLocked));
	}
	if (StatRabbit) { StatRabbit->SetText(FText::FromString(FString::FromInt(St.Rabbits))); }
	if (StatFox)    { StatFox->SetText(FText::FromString(FString::FromInt(St.Foxes))); }

	if (StatGrass)
	{
		StatGrass->SetText(FText::FromString(FString::Printf(TEXT("%.1f%%"), St.GrassCoverage * 100.f)));
	}
	if (StatSpeed)
	{
		StatSpeed->SetText(FText::FromString(SpeedStr));
		StatSpeed->SetColorAndOpacity(FSlateColor(bPaused ? EcoUI::TextWarn : EcoUI::TextBright));
	}
	if (StatRain)
	{
		StatRain->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), W.Rain * 100.f)));
		StatRain->SetColorAndOpacity(FSlateColor(W.Rain > 0.05f ? EcoUI::TextAccent : EcoUI::TextBright));
	}
	if (StatTemp)
	{
		const float T = W.Temperature;
		StatTemp->SetText(FText::FromString(FString::Printf(TEXT("%.0f℃"), T)));
		// 8~32 度是草的生长适温，超出就变色提醒
		StatTemp->SetColorAndOpacity(FSlateColor((T < 8.f || T > 32.f) ? EcoUI::TextWarn : EcoUI::TextBright));
	}
	if (StatSun)
	{
		StatSun->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), W.Sunlight * 100.f)));
	}

	// ---------- 目标进度条 ----------
	if (GoalBar)
	{
		const float Progress = (St.GoalCoins > 0)
			? FMath::Clamp((float)St.LifetimeCoins / (float)St.GoalCoins, 0.f, 1.f)
			: 0.f;
		GoalBar->SetPercent(Progress);
		GoalBar->SetFillColorAndOpacity(Progress >= 1.f ? EcoUI::TextAccent : EcoUI::GoalFill);
	}

	if (DetailText)
	{
		const float Progress = (St.GoalCoins > 0)
			? FMath::Clamp((float)St.LifetimeCoins / (float)St.GoalCoins, 0.f, 1.f)
			: 0.f;
		DetailText->SetText(FText::FromString(FString::Printf(
			TEXT("目标 %d/%d（%.0f%%）   收入 %.0f/分（割草 %d / 捕食 %d）   模拟 %.0fs   出生 %d   死亡 %d（饿死 %d / 被捕食 %d）   草 %d 成熟 / %d 生长   湿 %d 格"),
			St.LifetimeCoins, St.GoalCoins, Progress * 100.f,
			St.CoinsPerMinute, St.CoinsFromHarvest, St.CoinsFromPredation,
			St.SimTime, St.Births, St.Deaths, St.Starved, St.Eaten,
			St.MatureGrass, St.GrowingGrass, St.WetTiles)));
	}

	// ---------- 通关反馈：只弹一次 ----------
	if (!bGoalMessageShown && St.GoalCoins > 0 && St.LifetimeCoins >= St.GoalCoins)
	{
		bGoalMessageShown = true;
		ShowMessage(FString::Printf(TEXT("目标达成 —— 累计 %d 生态币！继续扩生态吧"),
			St.GoalCoins));
	}

	// ---------- 瞬时提示 ----------
	if (MessageText && MessageText->GetVisibility() != ESlateVisibility::Hidden)
	{
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.f;
		if (Now > MessageExpireTime)
		{
			MessageText->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	// ---------- TOOLS 面板 ----------
	UpdateToolButtonStyles();

	const float CanRadius = Sim->GetWaterCanRadius();
	const int32 CanCost = Sim->GetWaterCanUpgradeCost();

	if (ToolInfoText)
	{
		ToolInfoText->SetText(FText::FromString(FString::Printf(
			TEXT("Lv.%d  半径 %.1f 格"), Eco.WaterCanLevel, CanRadius)));
	}
	if (UpgradeCanButton && UpgradeCanLabel)
	{
		if (CanCost == INDEX_NONE)
		{
			UpgradeCanLabel->SetText(FText::FromString(TEXT("已满级")));
			UpgradeCanButton->SetBackgroundColor(EcoUI::ButtonNormal);
			UpgradeCanLabel->SetColorAndOpacity(FSlateColor(EcoUI::TextLocked));
		}
		else
		{
			const bool bAfford = Eco.Coins >= CanCost;
			UpgradeCanLabel->SetText(FText::FromString(FString::Printf(TEXT("升级 %d"), CanCost)));
			UpgradeCanButton->SetBackgroundColor(EcoUI::ButtonNormal);
			UpgradeCanLabel->SetColorAndOpacity(FSlateColor(bAfford ? EcoUI::TextAccent : EcoUI::TextLocked));
		}
	}

	// 镰刀升级：和水壶对称，每级扩大收割范围。
	const float ScytheRadius = Sim->GetScytheRadius();
	const int32 ScytheCost = Sim->GetScytheUpgradeCost();

	if (ScytheInfoText)
	{
		ScytheInfoText->SetText(FText::FromString(FString::Printf(
			TEXT("Lv.%d  半径 %.1f 格"), Eco.ScytheLevel, ScytheRadius)));
	}
	if (UpgradeScytheButton && UpgradeScytheLabel)
	{
		if (ScytheCost == INDEX_NONE)
		{
			UpgradeScytheLabel->SetText(FText::FromString(TEXT("已满级")));
			UpgradeScytheButton->SetBackgroundColor(EcoUI::ButtonNormal);
			UpgradeScytheLabel->SetColorAndOpacity(FSlateColor(EcoUI::TextLocked));
		}
		else
		{
			const bool bAfford = Eco.Coins >= ScytheCost;
			UpgradeScytheLabel->SetText(FText::FromString(FString::Printf(TEXT("升级 %d"), ScytheCost)));
			UpgradeScytheButton->SetBackgroundColor(EcoUI::ButtonNormal);
			UpgradeScytheLabel->SetColorAndOpacity(FSlateColor(bAfford ? EcoUI::TextAccent : EcoUI::TextLocked));
		}
	}

	// ---------- SHOP 面板 ----------
	const int32 RabbitPrice = Sim->GetCreaturePrice(ECreatureType::Rabbit);
	const int32 FoxPrice    = Sim->GetCreaturePrice(ECreatureType::Fox);

	auto StyleBuyButton = [&](UButton* Button, UTextBlock* Label, int32 Price,
		EPlayerTool PlaceTool, const TCHAR* Name)
	{
		if (!Button || !Label) { return; }

		const bool bAfford = (Eco.Coins >= Price);
		const bool bActive = (Tool == PlaceTool);

		Button->SetBackgroundColor(bActive ? EcoUI::ButtonSelected : EcoUI::ButtonNormal);
		Label->SetText(FText::FromString(bActive
			? FString::Printf(TEXT("%s %d  >>"), Name, Price)
			: FString::Printf(TEXT("%s %d"), Name, Price)));
		Label->SetColorAndOpacity(FSlateColor(
			bActive ? EcoUI::TextAccent : (bAfford ? EcoUI::TextBright : EcoUI::TextLocked)));
	};

	StyleBuyButton(BuyRabbitButton, BuyRabbitLabel, RabbitPrice,
		EPlayerTool::PlaceRabbit, TEXT("兔子"));
	StyleBuyButton(BuyFoxButton, BuyFoxLabel, FoxPrice,
		EPlayerTool::PlaceFox, TEXT("狐狸"));

	// ---------- UPGRADE 面板 ----------
	// 按钮上写"还差多少钱"，旁边写"升完实际是多少" ——
	// 只有把收益数字摆出来，玩家才能判断这条线值不值得投。
	auto StyleUpgradeButton = [&](UButton* Button, UTextBlock* Label, UTextBlock* Info,
		int32 Cost, const TCHAR* Name, const FString& InfoText)
	{
		if (!Button || !Label) { return; }

		if (Cost == INDEX_NONE)
		{
			Label->SetText(FText::FromString(FString::Printf(TEXT("%s  已满级"), Name)));
			Label->SetColorAndOpacity(FSlateColor(EcoUI::TextLocked));
		}
		else
		{
			const bool bAfford = Eco.Coins >= Cost;
			Label->SetText(FText::FromString(FString::Printf(TEXT("%s  升级 %d"), Name, Cost)));
			Label->SetColorAndOpacity(FSlateColor(bAfford ? EcoUI::TextAccent : EcoUI::TextLocked));
		}

		Button->SetBackgroundColor(EcoUI::ButtonNormal);
		if (Info) { Info->SetText(FText::FromString(InfoText)); }
	};

	const int32 BaseCost   = Sim->GetCoinUpgradeCost(Eco.CoinBaseLevel);
	const int32 DoubleCost = Sim->GetCoinUpgradeCost(Eco.DoubleChanceLevel);

	StyleUpgradeButton(UpgradeBaseButton, UpgradeBaseLabel, UpgradeBaseInfo, BaseCost, TEXT("线路 A"),
		FString::Printf(TEXT("Lv.%d  割草 %d  捕食 %d"),
			Eco.CoinBaseLevel, St.HarvestYield, St.PredationYield));

	StyleUpgradeButton(UpgradeDoubleButton, UpgradeDoubleLabel, UpgradeDoubleInfo, DoubleCost, TEXT("线路 B"),
		FString::Printf(TEXT("Lv.%d  当前 %.0f%%"),
			Eco.DoubleChanceLevel, St.DoubleChance * 100.f));

	const int32 RainCost = Sim->GetRainUpgradeCost();
	StyleUpgradeButton(UpgradeRainButton, UpgradeRainLabel, UpgradeRainInfo, RainCost, TEXT("降雨"),
		FString::Printf(TEXT("Lv.%d  雨量 %.0f%%"),
			Eco.RainLevel, W.Rain * 100.f));

	// ---------- 底部标签上的摘要（收起时也能看）----------
	if (SecTimeSummary)
	{
		SecTimeSummary->SetText(FText::FromString(SpeedStr));
		SecTimeSummary->SetColorAndOpacity(FSlateColor(bPaused ? EcoUI::TextWarn : EcoUI::TextDim));
	}
	if (SecWeatherSummary)
	{
		SecWeatherSummary->SetText(FText::FromString(FString::Printf(
			TEXT("雨 %.0f%%   %.0f℃   %s"),
			W.Rain * 100.f, W.Temperature, *St.ClockText)));
	}
	if (SecToolsSummary)
	{
		const TCHAR* const ToolName =
			(Tool == EPlayerTool::WaterCan)   ? TEXT("水壶")
			: (Tool == EPlayerTool::Scythe)   ? TEXT("镰刀")
			: (Tool == EPlayerTool::PlaceRabbit) ? TEXT("放兔")
			: (Tool == EPlayerTool::PlaceFox)    ? TEXT("放狐")
			: TEXT("无");

		SecToolsSummary->SetText(FText::FromString(FString::Printf(
			TEXT("%s   壶 Lv.%d 镰 Lv.%d"), ToolName, Eco.WaterCanLevel, Eco.ScytheLevel)));
	}
	if (SecShopSummary)
	{
		SecShopSummary->SetText(FText::FromString(FString::Printf(
			TEXT("兔 %d   狐 %d"), RabbitPrice, FoxPrice)));
	}
	if (SecUpgradeSummary)
	{
		SecUpgradeSummary->SetText(FText::FromString(FString::Printf(
			TEXT("A Lv.%d / B Lv.%d"), Eco.CoinBaseLevel, Eco.DoubleChanceLevel)));
	}
	if (SecFieldSummary)
	{
		SecFieldSummary->SetText(FText::FromString(FString::Printf(
			TEXT("草 %d   湿 %d"), St.MatureGrass, St.WetTiles)));
	}
}


// =====================================================================
// 按钮回调
// =====================================================================

void UEcoBoxHUDWidget::OnSpeedDown()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->CycleSpeed(-1); }
}

void UEcoBoxHUDWidget::OnSpeedUp()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->CycleSpeed(1); }
}

void UEcoBoxHUDWidget::OnPauseToggle()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->TogglePause(); }
}

void UEcoBoxHUDWidget::OnRainDown()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustRain(-0.1f); }
}

void UEcoBoxHUDWidget::OnRainUp()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustRain(0.1f); }
}

void UEcoBoxHUDWidget::OnTempDown()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustTemperature(-2.f); }
}

void UEcoBoxHUDWidget::OnTempUp()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustTemperature(2.f); }
}

void UEcoBoxHUDWidget::OnDayDown()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustDayLength(-20.f); }
}

void UEcoBoxHUDWidget::OnDayUp()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->AdjustDayLength(20.f); }
}

void UEcoBoxHUDWidget::OnToolWaterCan()
{
	if (AEcoBoxPlayerController* PC = GetEcoController())
	{
		PC->SetTool(EPlayerTool::WaterCan);
		ShowMessage(TEXT("水壶：点地面浇水（按住可以拖着浇）"));
	}
}

void UEcoBoxHUDWidget::OnToolScythe()
{
	if (AEcoBoxPlayerController* PC = GetEcoController())
	{
		PC->SetTool(EPlayerTool::Scythe);
		ShowMessage(TEXT("镰刀：点成熟的草收割，范围随等级扩大"));
	}
}

void UEcoBoxHUDWidget::OnUpgradeCan()
{
	AEcoBoxPlayerController* PC = GetEcoController();
	UEcoBoxSimSubsystem* Sim = PC ? PC->GetSim() : nullptr;
	if (!Sim) { return; }

	const int32 Cost = Sim->GetWaterCanUpgradeCost();
	if (Cost == INDEX_NONE)
	{
		ShowMessage(TEXT("水壶已经满级了"));
		return;
	}

	if (Sim->UpgradeWaterCan())
	{
		ShowMessage(FString::Printf(TEXT("水壶升到 Lv.%d，半径 %.1f 格（-%d 币）"),
			Sim->GetEconomy().WaterCanLevel, Sim->GetWaterCanRadius(), Cost));
	}
	else
	{
		ShowMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"), Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDWidget::OnUpgradeScythe()
{
	AEcoBoxPlayerController* PC = GetEcoController();
	UEcoBoxSimSubsystem* Sim = PC ? PC->GetSim() : nullptr;
	if (!Sim) { return; }

	const int32 Cost = Sim->GetScytheUpgradeCost();
	if (Cost == INDEX_NONE)
	{
		ShowMessage(TEXT("镰刀已经满级了"));
		return;
	}

	if (Sim->UpgradeScythe())
	{
		ShowMessage(FString::Printf(TEXT("镰刀升到 Lv.%d，收割半径 %.1f 格（-%d 币）"),
			Sim->GetEconomy().ScytheLevel, Sim->GetScytheRadius(), Cost));
	}
	else
	{
		ShowMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"), Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDWidget::OnBuyRabbit()
{
	if (AEcoBoxPlayerController* PC = GetEcoController())
	{
		PC->SetTool(EPlayerTool::PlaceRabbit);
		ShowMessage(TEXT("兔子：点地面放下（每只 5 币）"));
	}
}

void UEcoBoxHUDWidget::OnBuyFox()
{
	if (AEcoBoxPlayerController* PC = GetEcoController())
	{
		PC->SetTool(EPlayerTool::PlaceFox);
		ShowMessage(TEXT("狐狸：点地面放下（每只 20 币）"));
	}
}

void UEcoBoxHUDWidget::OnUpgradeBase()
{
	AEcoBoxPlayerController* PC = GetEcoController();
	UEcoBoxSimSubsystem* Sim = PC ? PC->GetSim() : nullptr;
	if (!Sim) { return; }

	const int32 Cost = Sim->GetCoinUpgradeCost(Sim->GetEconomy().CoinBaseLevel);
	if (Cost == INDEX_NONE)
	{
		ShowMessage(TEXT("升级线 A 已经满级了"));
		return;
	}

	if (Sim->UpgradeCoinBase())
	{
		ShowMessage(FString::Printf(TEXT("升级线 A → Lv.%d：割草 %d / 捕食 %d（-%d 币）"),
			Sim->GetEconomy().CoinBaseLevel, Sim->GetHarvestYield(), Sim->GetPredationYield(), Cost));
	}
	else
	{
		ShowMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"), Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDWidget::OnUpgradeDouble()
{
	AEcoBoxPlayerController* PC = GetEcoController();
	UEcoBoxSimSubsystem* Sim = PC ? PC->GetSim() : nullptr;
	if (!Sim) { return; }

	const int32 Cost = Sim->GetCoinUpgradeCost(Sim->GetEconomy().DoubleChanceLevel);
	if (Cost == INDEX_NONE)
	{
		ShowMessage(TEXT("升级线 B 已经满级了"));
		return;
	}

	if (Sim->UpgradeCoinDouble())
	{
		ShowMessage(FString::Printf(TEXT("升级线 B → Lv.%d：翻倍概率 %.0f%%（-%d 币）"),
			Sim->GetEconomy().DoubleChanceLevel, Sim->GetDoubleChance() * 100.f, Cost));
	}
	else
	{
		ShowMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"), Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDWidget::OnUpgradeRain()
{
	AEcoBoxPlayerController* PC = GetEcoController();
	UEcoBoxSimSubsystem* Sim = PC ? PC->GetSim() : nullptr;
	if (!Sim) { return; }

	const int32 Cost = Sim->GetRainUpgradeCost();
	if (Cost == INDEX_NONE)
	{
		ShowMessage(TEXT("降雨已经升满级了"));
		return;
	}

	if (Sim->UpgradeRain())
	{
		ShowMessage(FString::Printf(TEXT("降雨 → Lv.%d：当前雨量 %.0f%%（-%d 币）"),
			Sim->GetEconomy().RainLevel, Sim->GetWeather().Rain * 100.f, Cost));
	}
	else
	{
		ShowMessage(FString::Printf(TEXT("升级要 %d 生态币，现在只有 %d"), Cost, Sim->GetEconomy().Coins));
	}
}

void UEcoBoxHUDWidget::OnSpawnRabbits()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->QuickSpawn(ECreatureType::Rabbit, 5); }
}

void UEcoBoxHUDWidget::OnSpawnFoxes()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->QuickSpawn(ECreatureType::Fox, 3); }
}

void UEcoBoxHUDWidget::OnSeedGrass()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->SeedGrassField(); }
}

void UEcoBoxHUDWidget::OnReset()
{
	if (AEcoBoxPlayerController* PC = GetEcoController()) { PC->ResetWorld(); }
}
