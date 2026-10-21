// EcoBox - 游戏模式实现
#include "EcoBoxGameMode.h"

#include "EcoBoxCameraPawn.h"
#include "EcoBoxPlayerController.h"
#include "EcoBoxRenderer.h"
#include "EcoBoxSettings.h"
#include "EcoBoxSimSubsystem.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Light.h"
#include "Engine/SkyLight.h"
#include "UnrealClient.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "Scalability.h"
#include "Misc/CommandLine.h"
#include "HAL/IConsoleManager.h"

AEcoBoxGameMode::AEcoBoxGameMode()
{
	DefaultPawnClass = AEcoBoxCameraPawn::StaticClass();
	PlayerControllerClass = AEcoBoxPlayerController::StaticClass();
	bStartPlayersAsSpectators = false;
}

void AEcoBoxGameMode::ApplyAdaptiveResolution()
{
	// 用**静态**访问器，不要用 `GEngine->GetGameUserSettings()`：
	// 后者在 `AGameModeBase::BeginPlay` 这个时机可能还是 null（还没被初始化），
	// 静态版会自动创建并缓存。
	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();

	// 直接问操作系统要主显示器的原生分辨率
	FDisplayMetrics Metrics;
	FDisplayMetrics::RebuildDisplayMetrics(Metrics);

	// ⚠ 这里有个大坑：`PrimaryDisplayWidth/Height` 是**逻辑分辨率**，
	// 已经被 Windows 的 DPI 缩放除过了 —— 2560x1600 + 150% 缩放 → 拿到 1707x1067。
	// 用逻辑尺寸当窗口大小，引擎会先按 1707x1067 渲染再拉伸到 2560x1600，
	// 结果就是"UI 清楚、3D 糊成一片"。
	// 所以优先从 MonitorInfo 里取**物理像素**（NativeWidth/NativeHeight）。
	int32 Width  = Metrics.PrimaryDisplayWidth;
	int32 Height = Metrics.PrimaryDisplayHeight;

	const FMonitorInfo* PrimaryMonitor = nullptr;
	for (const FMonitorInfo& M : Metrics.MonitorInfo)
	{
		if (M.bIsPrimary && M.NativeWidth > 0 && M.NativeHeight > 0)
		{
			PrimaryMonitor = &M;
			break;
		}
	}
	// 有些平台不给 bIsPrimary，退一步用第一个能报出物理尺寸的
	if (!PrimaryMonitor)
	{
		for (const FMonitorInfo& M : Metrics.MonitorInfo)
		{
			if (M.NativeWidth > 0 && M.NativeHeight > 0)
			{
				PrimaryMonitor = &M;
				break;
			}
		}
	}

	if (PrimaryMonitor)
	{
		Width  = PrimaryMonitor->NativeWidth;
		Height = PrimaryMonitor->NativeHeight;
	}

	// ---- 诊断输出 ----
	// 打包的 Shipping 版不写日志，所以把探测结果直接打屏幕上（30 秒后消失）。
	// 看到这几行就知道到底是"探测错了"还是"设置没生效"。
	if (GEngine)
	{
		const FIntPoint Cur = Settings ? Settings->GetScreenResolution() : FIntPoint::ZeroValue;
		float Pct = -1.f;
		if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			Pct = CV->GetFloat();
		}

		FIntPoint ViewSize(0, 0);
		if (GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			ViewSize = GEngine->GameViewport->Viewport->GetSizeXY();
		}

		GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Yellow, FString::Printf(
			TEXT("[Res] 逻辑 %dx%d | 物理 %dx%d | 显示器数 %d | 设置 %dx%d | 视口 %dx%d | 渲染比例 %.0f%%"),
			Metrics.PrimaryDisplayWidth, Metrics.PrimaryDisplayHeight,
			Width, Height, Metrics.MonitorInfo.Num(),
			Cur.X, Cur.Y, ViewSize.X, ViewSize.Y, Pct));

		// 同一份信息也进日志。
		// ⚠ 必须用 Warning 级别：`LogTemp` 的 Log 级别在 `-Unattended` 下会被过滤掉，
		// 用 Log 写等于没写（这次排查就是被这个坑耽误的）。
		UE_LOG(LogTemp, Warning, TEXT("[EcoBox][Res] 逻辑 %dx%d | 物理 %dx%d | 显示器数 %d | 设置 %dx%d | 视口 %dx%d | 渲染比例 %.0f%%"),
			Metrics.PrimaryDisplayWidth, Metrics.PrimaryDisplayHeight,
			Width, Height, Metrics.MonitorInfo.Num(),
			Cur.X, Cur.Y, ViewSize.X, ViewSize.Y, Pct);
	}

	// 取不到（无头 / 服务器 / -nullrhi）或者拿不到设置对象就别动
	if (!Settings || Width <= 0 || Height <= 0)
	{
		return;
	}

	const FIntPoint Want(Width, Height);

	// 已经是目标分辨率就不用重建交换链（会黑一下）
	const bool bAlreadyCorrect =
		(Settings->GetScreenResolution() == Want) &&
		(Settings->GetFullscreenMode() == EWindowMode::WindowedFullscreen);

	if (!bAlreadyCorrect)
	{
		Settings->SetScreenResolution(Want);
		Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		Settings->ApplyResolutionSettings(/*bCheckForCommandLineOverrides=*/false);
	}

	// 渲染比例这一段**不能**放进上面的 if 里。
	// 坑：分辨率一旦设对，`bAlreadyCorrect` 每次都为 true → 后续启动直接跳过整个分支，
	// 于是渲染比例永远停在 3%（实测就是这样：窗口 2560x1600，画面却糊）。
	// 它和分辨率是两件事，必须每次都查。
	if (Settings->ScalabilityQuality.ResolutionQuality < 3)
	{
		Settings->ScalabilityQuality.ResolutionQuality = 3;
		Settings->ApplyNonResolutionSettings();
	}

	// 直接把 r.ScreenPercentage 钉死 100%。
	//
	// 实测（日志 [EcoBox][Res]）：窗口已经是 2560x1600，但 `r.ScreenPercentage` 只有 **3%** ——
	// 3D 内容按 3% 渲染再放大，糊到看不清；而 UI 走 Slate 矢量绘制完全不受影响，
	// 所以看起来就是"界面清楚、场景糊"。
	if (IConsoleVariable* ScreenPctCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		ScreenPctCVar->Set(100.f, ECVF_SetByGameSetting);
		UE_LOG(LogTemp, Warning, TEXT("[EcoBox][Res] 渲染比例已设为 %.0f%%"),
			ScreenPctCVar->GetFloat());
	}

	UE_LOG(LogTemp, Warning, TEXT("[EcoBox] 分辨率自适应：%d x %d（无边框全屏）"),
		Width, Height);
}

void AEcoBoxGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 分辨率自适应：按**当前显示器**的原生分辨率铺满屏幕。
	//
	// 为什么不靠 ini：`UGameUserSettings::ApplyResolutionSettings()` 只是把
	// `ResolutionSizeX/Y` 丢给 `RequestResolutionChange()`，**不会自己去问显示器**。
	// 所以换一台机器就得改配置，否则又会退回 1280x720（或者用上一台机器的值）。
	ApplyAdaptiveResolution();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 关卡里没手动摆渲染器时，自动生成一个。
	// 用指针而不是 bool —— 后面读档完要让渲染器重建一次。
	AEcoBoxRenderer* Renderer = nullptr;
	for (TActorIterator<AEcoBoxRenderer> It(World); It; ++It)
	{
		Renderer = *It;
		break;
	}

	if (!Renderer)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Renderer = World->SpawnActor<AEcoBoxRenderer>(
			AEcoBoxRenderer::StaticClass(), FTransform::Identity, Params);
		if (Renderer)
		{
			UE_LOG(LogTemp, Log, TEXT("[EcoBox] 已自动生成渲染器 %s"), *Renderer->GetName());
		}
	}

	UEcoBoxSimSubsystem* Sim = World->GetSubsystem<UEcoBoxSimSubsystem>();
	if (!Sim)
	{
		UE_LOG(LogTemp, Error, TEXT("[EcoBox] 找不到模拟子系统，跳过开局初始化"));
		return;
	}

	// =====================================================================
	// 调试用自测开关：命令行带 -EcoBoxSaveSelfTest 时，开局立刻做一次
	// "保存 → 读回"的对账，用来验证存档序列化是否真的把数据写进去了。
	// 正常游戏不会带这个参数，所以不影响任何行为。
	// =====================================================================
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("EcoBoxSaveSelfTest")))
	{
		UE_LOG(LogTemp, Log, TEXT("[EcoBox] ===== 存档自测 开始 ====="));
		Sim->SaveGame();
		Sim->LoadGame();
		UE_LOG(LogTemp, Log, TEXT("[EcoBox] ===== 存档自测 结束 ====="));
	}

	// ---- 诊断：把场景里的光源状态打出来 ----
	//
	// 为什么需要这个：兔狐用的是**导入的 PBR 材质（Lit）**，必须有光才看得见；
	// 一旦场景里没有有效的动态光源，它们就是纯黑（而之前的自建 Unlit 材质不受影响，
	// 所以"以前能看见、换了材质就黑"往往不是材质坏，是没光）。
	//
	// 注意 `r.AllowStaticLighting=False`（见 DefaultEngine.ini）：
	// **Static 光源在这个项目里完全不参与光照**，只有 Movable / Stationary 算数。
	{
		int32 LightCount = 0;
		for (TActorIterator<ALight> It(World); It; ++It)
		{
			const ALight* L = *It;
			const ULightComponent* LC = L ? L->GetLightComponent() : nullptr;
			if (!LC) { continue; }

			++LightCount;
			const TCHAR* MobilityText =
				(LC->Mobility == EComponentMobility::Static)     ? TEXT("Static(本项目无效!)") :
				(LC->Mobility == EComponentMobility::Stationary) ? TEXT("Stationary") : TEXT("Movable");

			UE_LOG(LogTemp, Warning,
				TEXT("[EcoBox] 光源 %s：强度 %.2f  Mobility=%s  可见=%d  影响世界=%d"),
				*L->GetClass()->GetName(),
				LC->Intensity,
				MobilityText,
				LC->IsVisible() ? 1 : 0,
				LC->bAffectsWorld ? 1 : 0);
		}
		UE_LOG(LogTemp, Warning, TEXT("[EcoBox] 场景光源总数：%d"), LightCount);

		// SkyLight 单独统计：`ASkyLight` 继承自 AInfo，**不是 ALight 的子类**，
		// 上面的 TActorIterator<ALight> 看不到它。
		//
		// SkyLight 提供"环境光"（天光）。PBR 材质的暗部全靠它 —— 如果它没在工作
		// （Static 且没烘焙、或者没 Recapture），阴影面就是纯黑，整体会明显偏暗。
		int32 SkyLightCount = 0;
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			const ASkyLight* S = *It;
			const USkyLightComponent* SC = S ? S->GetLightComponent() : nullptr;
			if (!SC) { continue; }

			++SkyLightCount;
			const TCHAR* MobilityText =
				(SC->Mobility == EComponentMobility::Static)     ? TEXT("Static") :
				(SC->Mobility == EComponentMobility::Stationary) ? TEXT("Stationary") : TEXT("Movable");

			UE_LOG(LogTemp, Warning,
				TEXT("[EcoBox] SkyLight：强度 %.2f  Mobility=%s  实时捕获=%d  可见=%d  影响世界=%d"),
				SC->Intensity,
				MobilityText,
				SC->bRealTimeCapture ? 1 : 0,
				SC->IsVisible() ? 1 : 0,
				SC->bAffectsWorld ? 1 : 0);
		}
		UE_LOG(LogTemp, Warning, TEXT("[EcoBox] SkyLight 总数：%d"), SkyLightCount);

		// 曝光：本项目关了自动曝光（r.DefaultFeature.AutoExposure=False，见 DefaultEngine.ini），
		// 所以画面亮度是个**固定值**，不会随场景自动适应。PBR 材质暗不暗和它直接相关。
		{
			auto GetCVarFloat = [](const TCHAR* Name) -> float
			{
				if (const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					return CVar->GetFloat();
				}
				return -999.f;
			};
			UE_LOG(LogTemp, Warning,
				TEXT("[EcoBox] 曝光：AutoExposure=%s  EyeAdaptation.ExposureCompensation=%.2f  DirectionalLightIntensityScale=%.2f"),
				GetCVarFloat(TEXT("r.DefaultFeature.AutoExposure")) > 0.5f ? TEXT("开") : TEXT("关(固定曝光)"),
				GetCVarFloat(TEXT("r.EyeAdaptation.ExposureCompensation")),
				GetCVarFloat(TEXT("r.LightIntensityScale")));
		}
	}
#endif

	// =====================================================================
	// 开局第一步：检查存档。
	//
	// 这一步必须在"投放开局生物"**之前** —— 否则会先随机撒一批兔狐，
	// 紧接着又被存档整个覆盖掉，白跑一遍随机数。
	//
	// 读档成功就直接 return：这一局的世界已经完全来自存档，
	// 再投放就变成"存档 + 一份新的开局生物"了。
	// =====================================================================
	if (Sim->LoadIfExists())
	{
		UE_LOG(LogTemp, Log, TEXT("[EcoBox] 检测到存档，已加载（跳过开局投放）"));

		// 读档是把 Cells / Creatures 整块换掉，渲染器的增量更新（按档位比对）
		// 虽然也能自己追上，但那样开场会有一小段"草还没长出来"的空窗。
		// 这里直接让它从头重建一次，第一帧就是完整的存档世界。
		if (Renderer)
		{
			Renderer->RebuildAll();
		}
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[EcoBox] 没有存档，按新开局创建世界"));

	// ---- 新开局：投放一小群生物，否则一进游戏只有草，什么都看不到 ----
	// 数量在 项目设置 → Game → EcoBox 生态箱 里改（设为 0 就是完全空白的开端）。
	{
		const UEcoBoxSettings& Settings = *GetDefault<UEcoBoxSettings>();

		if (Settings.InitialRabbits > 0)
		{
			const int32 Made = Sim->SpawnOnGrass(ECreatureType::Rabbit, Settings.InitialRabbits);
			UE_LOG(LogTemp, Log, TEXT("[EcoBox] 开局投放兔子 %d 只"), Made);
		}
		if (Settings.InitialFoxes > 0)
		{
			// 狐狸投在有草的地方，也就是兔子所在的位置。
			// 随机撒的话它们会在找到猎物之前就饿死。
			const int32 Made = Sim->SpawnOnGrass(ECreatureType::Fox, Settings.InitialFoxes);
			UE_LOG(LogTemp, Log, TEXT("[EcoBox] 开局投放狐狸 %d 只"), Made);
		}

		// 开局数据全是 0 会得到一张"完全空白的棕色平地"。
		// 因为地面材质是 Unlit 的（纯色，没有明暗渐变），空地上**拖动视角和缩放都看不出
		// 任何变化**，非常容易被误判成"相机坏了 / 视角被固定住了"。
		// 所以这里主动吼一声，省得下次又去查相机。
		if (Settings.InitialRabbits <= 0 && Settings.InitialFoxes <= 0
			&& Settings.InitialGrassCoverage <= 0.f)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[EcoBox] ⚠ 开局数据全是 0（兔/狐/草都设成了 0）：世界会是完全空白的平地，")
				TEXT("拖动和缩放都看不出变化。要改回来的话：")
				TEXT("① 项目设置 → Game → EcoBox 生态箱 → 「开局」；")
				TEXT("② 或者删掉 Config/DefaultGame.ini 里的 [/Script/EcoBox.EcoBoxSettings] 段")
				TEXT("（ini 里的值会压住代码默认值）。"));
		}
	}
}

AActor* AEcoBoxGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// 关卡里放了 PlayerStart 就用它
	if (AActor* Found = Super::ChoosePlayerStart_Implementation(Player))
	{
		return Found;
	}

	// 没有就在原点临时造一个，保证摄像机落在生态箱正中间
	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;

		if (APlayerStart* Start = World->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(), FTransform::Identity, Params))
		{
			UE_LOG(LogTemp, Log, TEXT("[EcoBox] 关卡里没有 PlayerStart，已在原点创建一个"));
			return Start;
		}
	}

	return nullptr;
}
