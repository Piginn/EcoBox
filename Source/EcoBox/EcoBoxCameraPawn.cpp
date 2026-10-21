// EcoBox - 上帝视角相机实现
#include "EcoBoxCameraPawn.h"

#include "EcoBoxSettings.h"
#include "EcoBoxTypes.h"

#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"

/**
 * 相机参数全部来自项目设置（Game → EcoBox 生态箱 → 「相机」），
 * 每次用到时实时读一遍，所以在 PIE 里改完**当帧就生效**，不用停 PIE 也不用重编译。
 * （只有 FOV / 摇臂长度 / 俯角 / 开局朝向是建构造时应用的，那几个改完要重进 PIE。）
 */
namespace EcoCam
{
	inline const UEcoBoxSettings& S()
	{
		return *GetDefault<UEcoBoxSettings>();
	}
}

AEcoBoxCameraPawn::AEcoBoxCameraPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	PivotRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PivotRoot"));
	SetRootComponent(PivotRoot);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(PivotRoot);
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = false;
	// 绝对旋转，这样"旋转视角"就是直接改世界 Yaw，不受 Pawn 朝向影响
	SpringArm->SetUsingAbsoluteRotation(true);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// 这里只能用字面量（和项目设置里的默认值一致），**不读 GetDefault<UEcoBoxSettings>()**：
	// 构造函数会在模块静态初始化时替 CDO 跑一遍，那个时间点不该去碰别的 CDO。
	// 真实值一律在 BeginPlay 里应用。
	SpringArm->TargetArmLength = 9500.f;
	SpringArm->SetRelativeRotation(FRotator(-55.f, 45.f, 0.f));
	Camera->SetFieldOfView(50.f);
}

void AEcoBoxCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	// 从这个时间点起读项目设置是安全的，而且改完重进 PIE 就生效
	const UEcoBoxSettings& S = EcoCam::S();

	SpringArm->TargetArmLength = FMath::Clamp(
		S.CameraStartArmLength, S.CameraMinArmLength, S.CameraMaxArmLength);
	SpringArm->SetRelativeRotation(FRotator(S.CameraPitch, S.CameraStartYaw, 0.f));
	Camera->SetFieldOfView(S.CameraFOV);

	// 相机参数是"手感问题"里最先被怀疑的对象，开机就把实际生效的值打出来 ——
	// 尤其是当项目设置被 ini 里的坏值压住的时候（全 0 / 全最小值），一眼就能看出来。
	UE_LOG(LogTemp, Log,
		TEXT("[EcoBox] 相机就绪：臂长 %.0f（%.0f~%.0f）  FOV %.1f  俯角 %.1f  朝向 %.1f  拖拽灵敏度 %.2f  滚轮步长 %.2f"),
		SpringArm->TargetArmLength,
		S.CameraMinArmLength, S.CameraMaxArmLength,
		S.CameraFOV, S.CameraPitch, S.CameraStartYaw,
		S.CameraDragSensitivity, S.CameraWheelZoomStep);
}

float AEcoBoxCameraPawn::GetRotateSpeed() const
{
	return EcoCam::S().CameraRotateSpeed;
}

void AEcoBoxCameraPawn::GetPlanarAxes(FVector& OutFwd, FVector& OutRight) const
{
	// 直接用相机自己的右/上向量，再压到地平面上。
	// 这样不管摇臂转了多少度（Q/E 旋转）、俯角是多少，
	// 平移和拖拽的方向都和屏幕上看到的一致，不会出现"往右拖却往上下跑"。
	const FVector CamRight = Camera ? Camera->GetRightVector() : FVector::YAxisVector;
	const FVector CamUp = Camera ? Camera->GetUpVector() : FVector::XAxisVector;

	OutRight = FVector(CamRight.X, CamRight.Y, 0.f).GetSafeNormal();
	OutFwd = FVector(CamUp.X, CamUp.Y, 0.f).GetSafeNormal();

	// 相机正好垂直朝下时"上"在地面上会退化成零向量，给个兜底
	if (OutRight.IsNearlyZero()) { OutRight = FVector::YAxisVector; }
	if (OutFwd.IsNearlyZero()) { OutFwd = FVector::XAxisVector; }
}

void AEcoBoxCameraPawn::PanBy(const FVector2D& Input, float DeltaTime)
{
	if (Input.IsNearlyZero() || DeltaTime <= 0.f)
	{
		return;
	}

	FVector Fwd = FVector::ZeroVector;
	FVector Right = FVector::ZeroVector;
	GetPlanarAxes(Fwd, Right);

	// 拉得越远，平移越快，否则拉到最远时挪一格要半天
	const float Scale = SpringArm->TargetArmLength / 9500.f;

	ApplyWorldDelta((Fwd * Input.Y + Right * Input.X) * EcoCam::S().CameraPanSpeed * Scale * DeltaTime);
}

/**
 * 注视点处「屏幕 1 像素 = 地面多少厘米」。
 *
 * 横向：注视点离相机斜距是 d（= 摇臂长度），水平视场角 FOV。
 *       → 横向 1 像素 = 2 * d * tan(FOV/2) / 视口宽度。
 * 纵向：屏幕竖轴对应的是地面上的「远近」方向。地面相对视线是倾斜的，
 *       同样的角度变化在地面上要多走 1/sin(俯角) 倍的距离。
 *       → 纵向 1 像素 = 横向 1 像素 / sin(俯角)。
 *
 * 这两个数乘上鼠标像素位移，就是"光标下的地面点保持不动"所需的相机位移。
 */
void AEcoBoxCameraPawn::ComputePixelMetrics(float& OutCmPerPixelX, float& OutCmPerPixelY,
	FVector2D& OutViewportPx) const
{
	const UEcoBoxSettings& S = EcoCam::S();

	// 相机到注视点（Pawn 所在的地面点）的斜距
	const float Dist = FMath::Max(1.f, SpringArm ? SpringArm->TargetArmLength : S.CameraStartArmLength);

	// 视口像素尺寸。拿不到就用一个常见值兜底 —— 只影响手感，不影响方向。
	OutViewportPx = FVector2D(1280.f, 720.f);

	const APlayerController* PC = GetController<APlayerController>();
	if (!PC)
	{
		if (const UWorld* World = GetWorld())
		{
			PC = World->GetFirstPlayerController();
		}
	}
	if (PC)
	{
		int32 SizeX = 0;
		int32 SizeY = 0;
		PC->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0 && SizeY > 0)
		{
			OutViewportPx = FVector2D((float)SizeX, (float)SizeY);
		}
	}

	// UCameraComponent::FieldOfView 是水平视场角
	const float HalfHFov = FMath::Tan(FMath::DegreesToRadians(S.CameraFOV * 0.5f));
	OutCmPerPixelX = 2.f * Dist * HalfHFov / OutViewportPx.X;

	// 俯角取摇臂当前实际的角度（比配置值更准，万一以后支持改俯角也不会算错）
	const float PitchDeg = SpringArm ? SpringArm->GetRelativeRotation().Pitch : S.CameraPitch;
	const float LookDownDeg = FMath::Clamp(-PitchDeg, 5.f, 89.f);
	OutCmPerPixelY = OutCmPerPixelX / FMath::Sin(FMath::DegreesToRadians(LookDownDeg));
}

void AEcoBoxCameraPawn::PanByPixels(const FVector2D& ScreenDeltaPixels)
{
	if (ScreenDeltaPixels.IsNearlyZero())
	{
		return;
	}

	FVector Fwd = FVector::ZeroVector;     // 地面上的「屏幕上方」
	FVector Right = FVector::ZeroVector;   // 地面上的「屏幕右方」
	GetPlanarAxes(Fwd, Right);

	float CmPerPixelX = 1.f;
	float CmPerPixelY = 1.f;
	FVector2D ViewportPx(1280.f, 720.f);
	ComputePixelMetrics(CmPerPixelX, CmPerPixelY, ViewportPx);

	// 单次调用的位移上限：一帧最多走"一个视口宽/高"那么多。
	// 这不是手感调整，是**兜底**：万一上游给出一个异常大的像素位移
	// （长帧、光标从窗口边缘跳回来、以后换了别的输入源），
	// 镜头最坏也就是走一屏，不会出现"一帧瞬移到地图另一头"。
	const FVector2D Px(
		FMath::Clamp(ScreenDeltaPixels.X, -ViewportPx.X, ViewportPx.X),
		FMath::Clamp(ScreenDeltaPixels.Y, -ViewportPx.Y, ViewportPx.Y));

	// 「抓住地面拖」：画面内容要跟着光标走，所以**相机**必须往光标的**反方向**移动。
	//   光标右移 → 地面右移 → 相机左移；
	//   光标上移 → 地面朝屏幕上移 → 相机往近处退。
	// 位移量用每像素对应的地面厘米数换算，这样光标下的那个点会一直待在光标底下。
	const FVector Delta = -(Right * Px.X * CmPerPixelX
	                      + Fwd   * Px.Y * CmPerPixelY) * EcoCam::S().CameraDragSensitivity;

	ApplyWorldDelta(Delta);
}

void AEcoBoxCameraPawn::ApplyWorldDelta(const FVector& Delta)
{
	FVector NewLoc = GetActorLocation() + Delta;
	const float Limit = EcoBoxConst::HalfExtent + EcoCam::S().CameraPanMargin;

	NewLoc.X = FMath::Clamp(NewLoc.X, -Limit, Limit);
	NewLoc.Y = FMath::Clamp(NewLoc.Y, -Limit, Limit);
	NewLoc.Z = 0.f;

	SetActorLocation(NewLoc);
}

void AEcoBoxCameraPawn::ZoomBy(float Steps)
{
	const UEcoBoxSettings& S = EcoCam::S();

	const float Factor = FMath::Pow(1.18f, Steps);
	SpringArm->TargetArmLength = FMath::Clamp(
		SpringArm->TargetArmLength * Factor, S.CameraMinArmLength, S.CameraMaxArmLength);
}

void AEcoBoxCameraPawn::YawBy(float Degrees)
{
	FRotator Rot = SpringArm->GetRelativeRotation();
	Rot.Yaw = FMath::UnwindDegrees(Rot.Yaw + Degrees);
	SpringArm->SetRelativeRotation(Rot);
}

float AEcoBoxCameraPawn::GetCameraHeight() const
{
	return Camera ? Camera->GetComponentLocation().Z : 0.f;
}

void AEcoBoxCameraPawn::FocusOnOrigin()
{
	const UEcoBoxSettings& S = EcoCam::S();

	SetActorLocation(FVector(0.f, 0.f, 0.f));
	SpringArm->TargetArmLength = FMath::Clamp(
		S.CameraStartArmLength, S.CameraMinArmLength, S.CameraMaxArmLength);

	FRotator Rot = SpringArm->GetRelativeRotation();
	Rot.Pitch = S.CameraPitch;
	Rot.Yaw = S.CameraStartYaw;
	Rot.Roll = 0.f;
	SpringArm->SetRelativeRotation(Rot);
}
