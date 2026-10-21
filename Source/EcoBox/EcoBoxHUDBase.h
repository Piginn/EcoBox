// EcoBox - UMG 蓝图 HUD 的基类
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EcoBoxTypes.h"
#include "EcoBoxHUDBase.generated.h"

class UEcoBoxSimSubsystem;
class AEcoBoxPlayerController;
class UWidget;
class UCanvasPanel;
class UCanvasPanelSlot;
class UImage;
class UTexture2D;

/**
 * 跟随鼠标的"当前工具"图标层（水壶 / 镰刀）。
 *
 * 为什么单独做一个 Widget：
 * 游戏里实际用的是**蓝图 HUD**（`WBP_EcoBoxHUD`，父类就是 `UEcoBoxHUDBase`）。
 * C++ **没法往蓝图已经拼好的控件树里插节点**，所以只能自己建一层 Widget
 * 叠到视口上（`AddToViewport(100)`），专门画那张跟着鼠标走的图。
 *
 * 整个层是 `HitTestInvisible` —— 它只负责画，绝不能挡住 HUD 按钮的点击。
 */
UCLASS()
class UEcoBoxToolCursorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 换图标；传 nullptr 等于隐藏。 */
	void SetIcon(UTexture2D* Texture);

	/** 把图标移到视口坐标 ViewportPos（图标中心对准该点）。 */
	void SetCursorPosition(const FVector2D& ViewportPos);

	/** 显隐（隐藏时不做任何绘制）。 */
	void SetCursorVisible(bool bVisible);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Layer;
	UPROPERTY(Transient) TObjectPtr<UImage> IconImage;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> IconSlot;
};

/**
 * 用编辑器（UMG 蓝图）自己做 HUD 时的基类。
 *
 * 它**不搭任何控件树** —— 控件树完全由你在蓝图的 Designer 里拖出来。
 * 代码这边只负责两件事：
 *   1. 提供一组 `BlueprintCallable` 的**数据读取**与**操作**函数（见下面），
 *      你在蓝图里拿这些返回值去 Set Text / 绑按钮 OnClicked；
 *   2. 每帧调一次 `K2_OnRefresh`（蓝图事件），你可以把它连到自己的刷新逻辑上。
 *
 * 用法：Content Browser 右键 → User Interface → Widget Blueprint →
 *      **父类选 EcoBoxHUDBase**，然后在 Designer 里搭界面、在 Graph 里接线。
 * 建好后去 EcoBoxPlayerController 的细节面板，把 HUD 蓝图类指派上。
 */
UCLASS(Abstract)
class ECOBOX_API UEcoBoxHUDBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 模拟子系统（拿细胞/生物/天气/经济数据的入口）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|数据")
	UEcoBoxSimSubsystem* GetSim() const;

	/** 玩家控制器（切工具、调速度/天气、升升级的入口）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|数据")
	AEcoBoxPlayerController* GetEcoController() const;
	// ---------------- 便捷读数（蓝图里直接拖出来用） ----------------

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "生态币"))
	int32 GetCoins() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "累计生态币"))
	int32 GetLifetimeCoins() const;

	/** 离通关目标还差多少（0 表示已达成）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "目标进度 0-1"))
	float GetGoalProgress() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "币/分钟"))
	float GetCoinsPerMinute() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "兔子数"))
	int32 GetRabbits() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "狐狸数"))
	int32 GetFoxes() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "草覆盖率 0-1"))
	float GetGrassCoverage() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "时间速度倍率"))
	float GetSimSpeed() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "是否暂停"))
	bool IsPaused() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "降雨 0-1"))
	float GetRain() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "温度"))
	float GetTemperature() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "时钟文字"))
	FString GetClockText() const;

	/** 当前工具。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "当前工具"))
	EPlayerTool GetCurrentTool() const;

	/** 水壶等级 / 当前半径 / 下一级价格（已满级时价格返回 -1）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetWaterCanLevel() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") float GetWaterCanRadius() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetWaterCanUpgradeCost() const;

	/** 镰刀等级 / 当前半径 / 下一级价格（已满级时价格返回 -1）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetScytheLevel() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") float GetScytheRadius() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetScytheUpgradeCost() const;

	/** 升级线 A/B 的等级与价格（已满级时价格返回 -1）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetCoinBaseLevel() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetDoubleChanceLevel() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetCoinBaseUpgradeCost() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetDoubleUpgradeCost() const;

	/** 降雨升级：等级 / 下一级价格（已满级时价格返回 -1）/ 每级加多少降雨。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetRainLevel() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetRainUpgradeCost() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") float GetRainPerLevel() const;

	/** 兔子 / 狐狸价格。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetRabbitPrice() const;
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数") int32 GetFoxPrice() const;

	/** 把完整统计结构一次性取出来（想一次读很多值时用，省得逐个调）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "取统计快照"))
	FEEcoStats GetStatsSnapshot() const;

	// ---------------- 操作（蓝图按钮 OnClicked 里直接调） ----------------

	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void SetTool(EPlayerTool InTool);
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void CycleSpeed(int32 Dir);
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void TogglePause();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void AdjustRain(float Delta);
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void AdjustTemperature(float Delta);
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void AdjustDayLength(float Delta);
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void BuyRabbit();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void BuyFox();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void UpgradeWaterCan();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void UpgradeScythe();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void UpgradeCoinBase();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void UpgradeCoinDouble();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void UpgradeRain();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void SeedGrass();
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作") void ResetWorld();

	/**
	 * 在底栏弹一行瞬时提示（和旧面板同一套机制）。
	 * 蓝图里可以直接调它显示"买不起 / 已满级"之类。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|操作")
	void ShowPanelMessage(const FString& Text);

	// ---------------- 存档 ----------------
	//
	// 这几个只是转发到模拟子系统，纯粹为了让 HUD 蓝图里少一步「先 Get Sim」。
	// 想直接用子系统的话，`Get Sim` 之后调同名节点效果完全一样。

	/** 保存当前整局（经济 + 天气 + 全部地块 + 全部生物）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "保存游戏"))
	bool SaveGame();

	/** 从存档读回整局。没有存档 / 版本不符返回 false。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "加载游戏"))
	bool LoadGame();

	/** 有没有存档。菜单里的「继续游戏」按钮可以用它决定是否置灰。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "有存档吗"))
	bool HasSaveGame() const;

	/** 删档（配一个「新游戏」按钮）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "删除存档"))
	bool DeleteSaveGame();

	/** 存档时间文字，如 "2026-10-16 20:30"。没有存档返回空串。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|存档", meta = (DisplayName = "存档时间"))
	FString GetSaveTimeText() const;

	// ---------------- 点地面 / 点界面 的边界 ----------------

	/**
	 * 挡住"点地面"的控件（顶部栏 / 底部栏这类面板根控件）。
	 *
	 * **蓝图 HUD 必须登记**，否则点勾选框 / 按钮的那一下会顺带在地块上放一只兔子。
	 * 原因：左键结算是 PlayerController 直接读按键状态做的，UMG 吃不掉这个事件；
	 * 而鼠标压在 UI 上时，视口缓存的鼠标位置是**冻住的**（最后一次停在地图上的位置），
	 * 于是射线会打回那个旧格子 —— 表现出来就是"点一下 UI，地图上莫名多只兔子，
	 * 而且总是出现在同一个位置"。
	 *
	 * 登记方式：蓝图 Event Construct → 拖出本变量 → Add（把你的顶部栏 / 底部栏塞进去）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|UI",
		meta = (DisplayName = "挡住点地面的控件"))
	TArray<TObjectPtr<UWidget>> ClickBlockingWidgets;

	/**
	 * 鼠标是不是压在上面那些控件上。压在界面上时左键不作用于地块。
	 * 由 PlayerController 每帧调用；内置 C++ 面板覆写了它，走自己那套判定。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|读数", meta = (DisplayName = "鼠标是否压在界面上"))
	virtual bool IsPointerOverPanel() const;

	// ---------------- 给 PlayerController 调用的钩子 ----------------

	/** 每帧由 PlayerController 调用，里面会广播 On Refresh 事件。 */
	virtual void RefreshDisplay();

	/**
	 * 界面用的字体资产。**要在细节面板里指派一个含中文字形的字体**，
	 * 否则中文会显示成方框/乱码。
	 *
	 * 怎么准备：把 C:\Windows\Fonts\msyh.ttc（微软雅黑）或 simhei.ttf（黑体）
	 * 拖进 Content Browser → 导入为 Font → 生成一个 UFont 资产，然后在
	 * 蓝图/面板的细节面板里把这里指向它。
	 *
	 * 留空时退回引擎 Roboto（只显示英文/数字正常，中文会乱码）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|字体",
		meta = (DisplayName = "界面字体（需含中文）"))
	TObjectPtr<class UFont> UIFont;

	/** 把上面那个字体打包成 Slate 能用的字体信息（指定尺寸）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|字体", meta = (DisplayName = "取界面字体"))
	FSlateFontInfo GetFont(int32 Size) const;

protected:
	/**
	 * 每帧刷新事件。在蓝图里实现它（Graph → 右键 → Add Event → On Refresh），
	 * 里面去 Set Text / 更新进度条。
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "EcoBox", meta = (DisplayName = "On Refresh"))
	void K2_OnRefresh();

	// ---------------- 工具光标（水壶 / 镰刀顶替鼠标） ----------------

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 每帧维护：手里拿着水壶/镰刀时，用物品图顶替系统光标。 */
	void UpdateToolCursor();

	/** 懒创建的光标层（叠在视口上）。 */
	UPROPERTY(Transient) TObjectPtr<UEcoBoxToolCursorWidget> ToolCursorWidget;

	UPROPERTY(Transient) TObjectPtr<UTexture2D> WateringCanIcon;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> SickleIcon;

	/** 上一次的工具，用来判断要不要换图（避免每帧重建 Brush）。 */
	EPlayerTool LastCursorTool = EPlayerTool::None;

	/** 上一次是否隐藏了系统光标（只在变化时调 SetShowMouseCursor）。 */
	bool bCursorHiddenByTool = false;

	/** 弹提示时触发（你在蓝图里接这个事件去做自己的提示条动画）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "EcoBox", meta = (DisplayName = "On Panel Message"))
	void OnPanelMessage(const FString& Text);
};
