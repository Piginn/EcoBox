// EcoBox - 纯 C++ 搭建的控制面板
#pragma once

#include "CoreMinimal.h"
#include "EcoBoxHUDBase.h"
#include "EcoBoxTypes.h"
#include "EcoBoxHUDWidget.generated.h"

class UOverlay;
class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UButton;
class UBorder;
class UProgressBar;
class UWidget;
class UImage;
class UCanvasPanel;
class UCanvasPanelSlot;
class UTexture2D;
class AEcoBoxPlayerController;

/**
 * 控制面板。
 *
 * 布局是「上下两条横贯窗口的条，中间全空」：
 *   1. 顶部读数条 —— 生态币 / 币每分钟 / 生物数量 / 草覆盖 / 速度 / 天气，左起一字排开，
 *      下面压一条 1000 币目标进度条；右端是时钟与生死统计；
 *   2. 底部操作条 —— 一排分区标签（TIME / WEATHER / TOOLS / SHOP / UPGRADE / FIELD），
 *      标签上带当前值摘要，点开才在标签行**上方**展开具体按钮，
 *      所以底栏是往上长的，中间视野始终不被切碎。
 *
 * TOOLS 是水壶 / 镰刀的选择与升级，SHOP 是花钱买兔子和狐狸，UPGRADE 是生态币的两条升级线 ——
 * 这三个分区就是 v1.0 增量玩法的操作核心，TIME / WEATHER / FIELD 更像调参台。
 *
 * 整个控件树都在 C++ 里搭，不依赖任何蓝图资产，改完代码编译就能看到效果。
 * 想换成中文只需改 BuildLayout 里的字面量，但要注意 UMG 默认字体不含中文字形，
 * 换之前先确认工程里配了带 CJK 的字体资源。
 */
UCLASS()
class ECOBOX_API UEcoBoxHUDWidget : public UEcoBoxHUDBase
{
	GENERATED_BODY()

public:
	/** 由 PlayerController 每帧调用，刷新数值显示。 */
	virtual void RefreshDisplay() override;

	/** 鼠标是不是正压在上/下任一条上。压着的时候点地面不应该作用到地块。 */
	virtual bool IsPointerOverPanel() const override;

	/**
	 * 在底栏上方弹一行瞬时提示（割草收益 / 买不起 / 这一格被占了）。
	 * 2.5 秒后自动消失，不用玩家去关。
	 */
	void ShowMessage(const FString& Text);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** 底部标签的编号，顺序就是标签行上的排列顺序。 */
	enum EEcoSection : int32
	{
		SecTime = 0,
		SecWeather,
		SecTools,
		SecShop,
		SecUpgrade,
		SecField,
		SecCount
	};

	/** 提示文字的存活时长（秒）。 */
	static constexpr float MessageSeconds = 2.5f;

	void BuildLayout();
	void BuildTopBar(UOverlay* Root);
	void BuildBottomBar(UOverlay* Root);
	void BuildSections();

	void BuildSectionTime(UHorizontalBox* Body);
	void BuildSectionWeather(UHorizontalBox* Body);
	void BuildSectionTools(UHorizontalBox* Body);
	void BuildSectionShop(UHorizontalBox* Body);
	void BuildSectionUpgrade(UHorizontalBox* Body);
	void BuildSectionField(UHorizontalBox* Body);

	void UpdateToolButtonStyles();
	void SetSectionOpen(int32 Index, bool bOpen);
	void ToggleSection(int32 Index);

	// ---------------- 控件工厂 ----------------
	UTextBlock* MakeText(const TCHAR* Name, int32 FontSize, const FLinearColor& Color, bool bBold = false);
	/** 小号暗色说明文字。 */
	UTextBlock* MakeLabel(const TCHAR* Name, const TCHAR* Text);
	UButton* MakeButton(const TCHAR* Name, const FString& Label);
	UHorizontalBox* MakeRow(const TCHAR* Name);
	void AddToBottomBox(UWidget* W, float TopPad);
	void AddStatChip(UHorizontalBox* Row, const TCHAR* Name, const FString& Label,
		TObjectPtr<UTextBlock>& OutValue);
	/** 底部标签按钮：左边标题、右边当前值摘要。标题文字会存进 SectionTitles。 */
	UButton* MakeTabButton(UHorizontalBox* Row, const TCHAR* Name, const FString& Title,
		TObjectPtr<UTextBlock>& OutSummary);
	/** 建一行分区内容（默认隐藏），插在标签行之前，所以展开是往上长。 */
	UHorizontalBox* AddSectionBody(const TCHAR* Name);
	/** 给行里塞一个说明文字，带左间距。 */
	void AppendLabel(UHorizontalBox* Row, const TCHAR* Name, const TCHAR* Text, float LeftPad);
	/** 取按钮里的文字块（因为按钮内容是"内边距 Border → TextBlock"）。 */
	static UTextBlock* GetButtonText(const UButton* Button);

	// ---------------- 按钮回调 ----------------
	UFUNCTION() void OnSpeedDown();
	UFUNCTION() void OnSpeedUp();
	UFUNCTION() void OnPauseToggle();
	UFUNCTION() void OnRainDown();
	UFUNCTION() void OnRainUp();
	UFUNCTION() void OnTempDown();
	UFUNCTION() void OnTempUp();
	UFUNCTION() void OnDayDown();
	UFUNCTION() void OnDayUp();
	UFUNCTION() void OnToolWaterCan();
	UFUNCTION() void OnToolScythe();
	UFUNCTION() void OnUpgradeCan();
	UFUNCTION() void OnUpgradeScythe();
	UFUNCTION() void OnBuyRabbit();
	UFUNCTION() void OnBuyFox();
	UFUNCTION() void OnUpgradeBase();
	UFUNCTION() void OnUpgradeDouble();
	UFUNCTION() void OnUpgradeRain();
	UFUNCTION() void OnSpawnRabbits();
	UFUNCTION() void OnSpawnFoxes();
	UFUNCTION() void OnSeedGrass();
	UFUNCTION() void OnReset();

	// 分区开关。AddDynamic 的函数名是编译期从字面 token 裁的，没法按 index 循环绑。
	UFUNCTION() void OnToggleTime();
	UFUNCTION() void OnToggleWeather();
	UFUNCTION() void OnToggleTools();
	UFUNCTION() void OnToggleShop();
	UFUNCTION() void OnToggleUpgrade();
	UFUNCTION() void OnToggleField();

	// ---------------- 控件 ----------------
	UPROPERTY(Transient) TObjectPtr<UBorder> TopBar;
	UPROPERTY(Transient) TObjectPtr<UBorder> BottomBar;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> TopBox;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> BottomBox;
	/** 1000 币目标进度条，压在顶部读数条下面。 */
	UPROPERTY(Transient) TObjectPtr<UProgressBar> GoalBar;

	// 顶部读数
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatCoins;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatIncome;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatRabbit;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatFox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatGrass;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatSpeed;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatRain;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatTemp;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatSun;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;

	// 底部标签行
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SectionTabs;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SectionTitles;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SectionSummaries;
	UPROPERTY(Transient) TArray<TObjectPtr<UHorizontalBox>> SectionBodies;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> SecTimeSummary;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SecWeatherSummary;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SecToolsSummary;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SecShopSummary;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SecUpgradeSummary;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SecFieldSummary;

	// TOOLS
	UPROPERTY(Transient) TObjectPtr<UButton> ToolWaterButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ToolScytheButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToolWaterLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToolScytheLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToolInfoText;
	UPROPERTY(Transient) TObjectPtr<UButton> UpgradeCanButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UpgradeCanLabel;
	/** 镰刀升级（和水壶对称，每级扩大收割范围）。 */
	UPROPERTY(Transient) TObjectPtr<UButton> UpgradeScytheButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UpgradeScytheLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ScytheInfoText;

	// SHOP
	UPROPERTY(Transient) TObjectPtr<UButton> BuyRabbitButton;
	UPROPERTY(Transient) TObjectPtr<UButton> BuyFoxButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyRabbitLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyFoxLabel;

	// UPGRADE：生态币两条升级线 + 降雨升级
	UPROPERTY(Transient) TObjectPtr<UButton> UpgradeBaseButton;
	UPROPERTY(Transient) TObjectPtr<UButton> UpgradeDoubleButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UpgradeBaseLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UpgradeDoubleLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UpgradeBaseInfo;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UpgradeDoubleInfo;

	/** 降雨升级：每级 +1% 雨量，越往后越贵。 */
	UPROPERTY(Transient) TObjectPtr<UButton> UpgradeRainButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UpgradeRainLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UpgradeRainInfo;

	/** 通关提示只弹一次。 */
	bool bGoalMessageShown = false;

	/** 提示文字的到期时刻（世界秒）。 */
	float MessageExpireTime = 0.f;

	/** 各分区是否展开。纯 C++ 状态，不需要反射。 */
	TArray<bool> SectionOpen;

	bool bLayoutBuilt = false;
};
