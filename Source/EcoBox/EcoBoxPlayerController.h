// EcoBox - 玩家控制器：相机操作、点地投放、UI 接线
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "EcoBoxTypes.h"
#include "EcoBoxPlayerController.generated.h"

class UEcoBoxSimSubsystem;
class UEcoBoxHUDWidget;
class UEcoBoxHUDBase;
class AEcoBoxCameraPawn;
class AEcoBoxRenderer;

/**
 * 玩家的操作：切工具（水壶 / 镰刀）+ 左键作用于地块，
 * 另外还能调时间速度、天气、投放生物（调试用）。
 * 相机操作和点地操作都在这里统一处理。
 *
 * 输入用的是直接读键状态而不是 EnhancedInput，好处是零资产依赖，
 * 编译完就能跑。以后要换成 EnhancedInput 只需替换 PlayerTick 里的读键部分。
 */
UCLASS()
class ECOBOX_API AEcoBoxPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AEcoBoxPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * 处理**输入事件**（滚轮、鼠标按键按下/抬起）。
	 *
	 * 为什么滚轮不能用每帧轮询：引擎只给 MouseX / MouseY 做了"这一帧没有采样就归零"，
	 * 滚轮的轴值会一直停在最后一次的增量上，轮询就会变成每一帧都在缩放。
	 * 具体见 cpp 里的说明。
	 */
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	// ---------------- 给 UI 调用的接口 ----------------
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	UEcoBoxSimSubsystem* GetSim() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void SetTool(EPlayerTool InTool);

	/** 当前工具（None 表示没选任何工具）。 */
	EPlayerTool GetTool() const;

	/**
	 * 取消选择：把工具设回 None，同时清掉地块高亮。
	 *
	 * 两种触发方式：
	 *   1. **右键"点击"**（按下到抬起光标几乎没动）—— 见 InputKey 里的说明；
	 *   2. 蓝图里挂到按钮上（想加个"取消选择"按钮就调它）。
	 * 已经是 None 时什么也不做，不会重复弹提示。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void ClearTool();

	/** 在指定格应用当前工具（左键走的就是它）。 */
	void ApplyToolAt(int32 CellIdx);

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void CycleSpeed(int32 Dir);

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void TogglePause();

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void AdjustRain(float Delta);

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void AdjustTemperature(float Delta);

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void AdjustDayLength(float Delta);

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void QuickSpawn(ECreatureType Type, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void SeedGrassField();

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void ResetWorld();

	/** 鼠标射线与地面 Z=0 平面的交点。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	bool GetMouseGroundPoint(FVector& OutPoint) const;

	/** 鼠标底下是哪一格，INDEX_NONE 表示不在图上。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	int32 GetHoveredCell() const;

	// =====================================================================
	// HUD 的显示 / 隐藏 / 换实例
	//
	// 为什么要提供这几个函数：PlayerTick 每一帧只刷新 **ActiveHUD 指向的那一个**
	// 实例。如果你在蓝图里对 HUD 调了 RemoveFromParent，然后又 CreateWidget 建了
	// 个**新的**实例 AddToViewport —— 新实例没人刷新（屏幕上是它，但它收不到
	// On Refresh），而每帧被刷新的是那个已经不在视口里的旧实例。
	// 结果就是"页面回来了，数字不动了"。
	//
	// 正确做法有两种：
	//   ① 只是隐藏/显示 -> 用 HideHUD() / ShowHUD()，**同一个实例**来回切，
	//      引用不变、状态不丢、刷新不断。推荐。
	//   ② 确实要换成另一个实例 -> 建好之后调 SetActiveHUD() 把刷新目标改过去。
	// =====================================================================

	/** 当前正在被刷新的 HUD（蓝图 HUD 或内置面板）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|UI")
	UEcoBoxHUDBase* GetActiveHUD() const { return ActiveHUD; }

	/**
	 * 隐藏 HUD（从视口移除，但**保留**对象和刷新）。
	 * 回来时调 ShowHUD() 即可 —— 因为是同一个实例，所有数值和控件状态都还在。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|UI", meta = (DisplayName = "隐藏 HUD"))
	void HideHUD();

	/** 把 HUD 加回视口（重复调用安全）。会和 HideHUD 配对使用。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|UI", meta = (DisplayName = "显示 HUD"))
	void ShowHUD();

	/**
	 * 换一个 HUD 实例作为刷新目标。
	 *
	 * 用法：蓝图里 CreateWidget（**Owning Player 一定要传 Get Owning Player**）
	 * → AddToViewport → 调本函数把新实例交进来。旧的会自动从视口移除。
	 * 传 nullptr 等于"不用 HUD 了"。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox|UI", meta = (DisplayName = "换 HUD 实例"))
	void SetActiveHUD(UEcoBoxHUDBase* NewHUD, bool bRemoveOldHUD = true);

	/**
	 * 每格滚轮的缩放步长挪到项目设置了：
	 * 项目设置 → Game → EcoBox 生态箱 → 「相机 → 滚轮缩放步长」。
	 */

	/**
	 * 你自己做的 UMG HUD 蓝图类（父类必须选 EcoBoxHUDBase）。
	 *
	 * 指派了就用它，控件的显示和按钮全部由你的蓝图决定；
	 * **留空则自动退回代码里搭的那套纯 C++ 面板**（英文+中文混排那套）。
	 * 想临时关掉蓝图、用回旧面板，把这个属性清空（None）即可，不用改代码。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|UI",
		meta = (DisplayName = "HUD 蓝图类（留空用内置面板）"))
	TSubclassOf<class UEcoBoxHUDBase> HUDBlueprintClass;

	/** 批量种草时的覆盖率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|输入")
	float SeedCoverage = 0.30f;

	// 右键 / 中键拖拽：自己记录上一帧的光标位置来算像素位移。
	//
	// 刻意**不用** GetInputMouseDelta：它返回的是 Slate 的"虚拟像素"
	// （SceneViewport 里 CursorDelta 是原始事件位移，没乘 GetViewportLocalToPixelScale），
	// 而 GetViewportSize 返回的是真实像素。DPI 不是 100% 时两者差一个缩放倍数
	// （本机 150%，实测截图 1289px / Slate 859 单位），拿去换算"1 像素 = 多少厘米"会整体偏慢。
	// GetMousePosition 读的是 FSceneViewport::CachedCursorPos，和 GetViewportSize 同为视口像素，
	// 缩放因子自然抵消。
	/**
	 * 拖拽是不是处在"有效的一段"里。
	 *
	 * 由鼠标按键事件驱动（按下=开始、抬起=结束），**不靠"这一帧按键还按着吗"去推断**。
	 * 因为光标压在 UI 上或者跑出视口时，视口缓存的鼠标位置是**冻住**的，
	 * 靠轮询推断就会在光标回到地图上的第一帧算出一整段距离的巨大位移 —— 视角瞬移。
	 */
	bool bDragging = false;
	float LastMouseX = 0.f;
	float LastMouseY = 0.f;

	/**
	 * 单帧像素位移超过它就直接丢弃（当成异常值）。
	 *
	 * 原来是 800，**太小了**：开局加载那几秒帧很长，一帧里光标走的像素经常超过 800，
	 * 那些帧就被整帧丢掉 —— 表现出来就是"刚进游戏的时候拖动很慢、跟不上"。
	 * 提到 1500 只兜真正的异常值，长帧里的正常拖拽能完整保留。
	 * 更硬的兜底在 AEcoBoxCameraPawn::PanByPixels 里：单次最多走一屏。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|输入")
	float MaxDragJumpPixels = 1500.f;

	/**
	 * 右键算不算"点击"的位移上限（视口像素）。
	 *
	 * 右键同时是**拖拽平移视角**的操作，所以不能按下就取消选择 ——
	 * 只有"按下 → 抬起之间光标几乎没动"才当成点击，去取消当前选择。
	 *
	 * ⚠ 这个值按**物理像素**算（`GetMousePosition` 返回的是真实像素，本机 DPI 150%），
	 * 不是 Slate 虚拟单位。手指点一下鼠标按键时，鼠标本身会微动 1mm 左右，
	 * 按 1000 DPI 折算就是 ~40 像素 —— 所以 8 那种小值是点不中的，40 才刚好容错。
	 * 拖视角的位移是几百像素，不会误触发。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|输入")
	float RightClickCancelMaxPixels = 40.f;

private:
	void HandleCameraInput(float DeltaTime);
	/** 1/2/3/4 切工具、空格暂停 + 左键作用于地块（按住会以固定间隔重复，浇水才拖得顺手）。 */
	void HandleToolInput(float DeltaTime);
	/** 算出本帧要高亮的格子（水壶是一圈圆，其他是单格）并交给渲染器。 */
	void UpdateHoverHighlight();

	/** 在 HUD 上弹一行瞬时提示（买不起 / 放不下之类的即时反馈）。 */
	void Notify(const FString& Text);

	AEcoBoxCameraPawn* GetEcoPawn() const;
	AEcoBoxRenderer* GetRenderer() const;

	UPROPERTY(Transient) TObjectPtr<UEcoBoxHUDWidget> HUDWidget;
	/** 当前的 HUD 一定实现了这个基类（要么是蓝图，要么是内置面板），统一走它刷新。 */
	UPROPERTY(Transient) TObjectPtr<class UEcoBoxHUDBase> ActiveHUD;

	/**
	 * 右键是不是按着（配对按下 / 抬起用）。
	 * 和 bDragging 不是一回事：bDragging 表示"已经在拖了"，它只表示"键还按着"。
	 */
	bool bRightPressed = false;

	/** 右键按下那一刻的光标位置（视口像素）。抬起时和它比距离，判断是点击还是拖拽。 */
	float RightPressMouseX = 0.f;
	float RightPressMouseY = 0.f;

	// 上一帧的按键状态，用来做边沿检测
	bool bLeftWasDown = false;
	bool bKey1WasDown = false;
	bool bKey2WasDown = false;
	bool bKey3WasDown = false;
	bool bKey4WasDown = false;
	/** 空格：按下那一刻切一次"暂停 / 继续"，按住不会连闪。 */
	bool bSpaceWasDown = false;

	/** 本帧的预览格子。做成成员是为了复用那块内存，不每帧重新分配。 */
	TArray<int32> PreviewCells;

	/** 按住左键时的重复间隔（秒）。浇水要能拖着走。 */
	UPROPERTY(EditAnywhere, Category = "EcoBox|输入")
	float ToolRepeatInterval = 0.15f;

	float ToolRepeatTimer = 0.f;

	/** 暂停前的速度，恢复时用。 */
	float LastSpeed = 1.f;

	mutable TWeakObjectPtr<AEcoBoxRenderer> CachedRenderer;
};
