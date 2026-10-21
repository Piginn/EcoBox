// EcoBox - 上帝视角相机
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "EcoBoxCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;

/**
 * 上帝视角相机。
 *
 * 用 SpringArm 做摇臂：平移 = 移动 Pawn 的 XY（也就是镜头的注视点），
 * 缩放 = 改变摇臂长度，旋转 = 改变摇臂的 Yaw。
 *
 * 摇臂长度 * sin(俯角) 就是相机高度，渲染层用它来决定显示 3D 还是 2D。
 *
 * 注意：**所有手感参数都不在这个类上**，而在
 * 项目设置 → Game → EcoBox 生态箱 → 「相机」分类里。
 * 因为这个 Pawn 是 GameMode 通过 DefaultPawnClass 运行时生成的，
 * 它自己的 UPROPERTY 在细节面板里根本够不着 —— 摆在上面等于"只能改代码重编译"。
 */
UCLASS()
class ECOBOX_API AEcoBoxCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AEcoBoxCameraPawn();

	/**
	 * FOV / 摇臂长度 / 俯角 / 开局朝向在这里才应用（从项目设置读）。
	 *
	 * ⚠ 构造函数里**不要读 `GetDefault<UEcoBoxSettings>()`**：Actor 的 CDO 是在
	 * 模块静态初始化时构造的，在那个时间点去碰另一个 CDO 本身就不安全
	 * （可能把对方提前创建出来）。构造函数里只写字面量兜底，
	 * 真实值一律在 BeginPlay 覆盖 —— 反正这个 Pawn 是运行时生成的，差别只有一帧。
	 */
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|相机")
	TObjectPtr<USceneComponent> PivotRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|相机")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|相机")
	TObjectPtr<UCameraComponent> Camera;

	// ---------------- 操作 ----------------
	/** Input.X 右、Input.Y 前，范围 -1~1。用于键盘平移。 */
	void PanBy(const FVector2D& Input, float DeltaTime);

	/**
	 * 按鼠标在**视口上的像素位移**平移，用于右键 / 中键拖拽。
	 *
	 * ScreenDeltaPixels：X 向右为正，Y 向上为正（屏幕坐标方向，不是 UE 鼠标轴的原始值）。
	 * 调用方负责保证单位是视口像素 —— 这里要用它除以视口宽度换算成厘米。
	 *
	 * 内部按「抓住地面拖」来算：画面内容跟着光标走。
	 * 另外单次调用会**夹住位移上限**（最多一个视口宽/高），
	 * 这样不管上游给出多离谱的像素位移都不可能让镜头瞬移。
	 */
	void PanByPixels(const FVector2D& ScreenDeltaPixels);

	/** 正值拉远，负值拉近。 */
	void ZoomBy(float Steps);
	void YawBy(float Degrees);

	/** Q/E 旋转速度（度/秒），从项目设置读。 */
	float GetRotateSpeed() const;

	/** 相机当前世界高度。渲染层用它决定 3D / 2D。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	float GetCameraHeight() const;

	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void FocusOnOrigin();

private:
	void ApplyWorldDelta(const FVector& Delta);

	/** 地面上「屏幕的上 / 右」对应的两个方向（已归一化）。 */
	void GetPlanarAxes(FVector& OutFwd, FVector& OutRight) const;

	/**
	 * 注视点（Pawn 所在的地面点）处，屏幕上 1 像素对应地面多少厘米。
	 * OutCmPerPixelX = 横向，OutCmPerPixelY = 纵向。
	 * 纵向要除以 sin(俯角)，因为地面相对视线是斜的。
	 * OutViewportPx 一并带出视口的像素尺寸，调用方夹位移上限时要用。
	 */
	void ComputePixelMetrics(float& OutCmPerPixelX, float& OutCmPerPixelY, FVector2D& OutViewportPx) const;
};
