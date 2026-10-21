// EcoBox - 渲染层实现
#include "EcoBoxRenderer.h"
#include "EcoBoxSimSubsystem.h"
#include "EcoBoxSettings.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

// =====================================================================
// 构造
// =====================================================================

AEcoBoxRenderer::AEcoBoxRenderer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// 注意：UE 5.8 的 FActorTickFunction 已经没有 bTickInEditor 了，
	// 编辑器里的预览靠 OnConstruction 驱动（见下方），运行时靠正常 Tick。

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 全部组件都用普通 ISM。理由见头文件顶部注释：
	// HISM 的 cluster tree 是异步构建的，而我们的实例池每帧都在改，
	// 那些更新会不断把没建完的树作废 —— 结果就是实例一会儿不画、
	// 一会儿又要等树建好才出现。普通 ISM 没有这棵树，写进去当帧就可见。
	auto MakeISM = [this](const TCHAR* Name) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* Comp =
			CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		SetupComponent(Comp);
		return Comp;
	};

	GroundISM    = MakeISM(TEXT("GroundISM"));
	GrassISM     = MakeISM(TEXT("GrassISM"));
	Rabbit3DISM  = MakeISM(TEXT("Rabbit3DISM"));
	Fox3DISM     = MakeISM(TEXT("Fox3DISM"));
	Rabbit2DISM  = MakeISM(TEXT("Rabbit2DISM"));
	Fox2DISM     = MakeISM(TEXT("Fox2DISM"));
	HighlightISM = MakeISM(TEXT("HighlightISM"));
	WetISM       = MakeISM(TEXT("WetISM"));

	// ---- 背景穹顶：超大号球体，法线朝内，把相机整个包住 ----
	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	if (SkyDome)
	{
		SkyDome->SetupAttachment(RootComponent);
		SkyDome->SetMobility(EComponentMobility::Movable);
		SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SkyDome->SetCanEverAffectNavigation(false);
		SkyDome->SetGenerateOverlapEvents(false);
		SkyDome->SetCastShadow(false);
		SkyDome->bUseAsOccluder = false;
		SkyDome->bReceivesDecals = false;
		// 关键：法线翻转 → 从**球内侧**看才渲染。
		// 不去改材质的 TwoSided，因为那是材质自身的属性，改它得动资产；
		// 组件级的 ReverseCulling 效果一样而且只影响这一个组件。
		SkyDome->SetReverseCulling(true);
		// 不做距离剔除 —— 它就是"无限远"的背景，被剔掉就直接露黑。
		// 注意 `SetCullDistances(min,max)` 是 ISM 专有的；普通组件只有单参数版本。
		SkyDome->SetCullDistance(0.f);
		SkyDome->bAllowCullDistanceVolume = false;
		SkyDome->SetRenderCustomDepth(false);
		SkyDome->bRenderInMainPass = true;
	}

	// 引擎自带的基础形体做占位，之后可以在细节面板里换成真模型
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		GroundMesh = CubeFinder.Object;
		// 草也用方块 —— 一格一块、铺满整个地块的"方形草皮"。
		// 之前用的锥体（Cone）只在格子中间戳一丛，怎么放大都像"格心长了个尖"，
		// 玩家看到的是"草只长在中间"，与"草覆盖地块"的预期不符。
		GrassMesh = CubeFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeFinder.Succeeded())
	{
		// 狐狸的默认网格先给个占位锥体，下面会用导入的 fox_3d 覆盖掉。
		Fox3DMesh = ConeFinder.Object;
	}

	// 导入的三维模型（用户自己拖进来的）。找不到就保留上面的占位形体，
	// 这样模型被删掉 / 改路径时不会直接崩成空网格。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FoxMeshFinder(
		TEXT("/Game/3d/fox/低模3d狐狸/StaticMeshes/fox_3d.fox_3d"));
	if (FoxMeshFinder.Succeeded())
	{
		Fox3DMesh = FoxMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		// 兔子的默认网格先给个球体占位，下面会用导入的 rabbit_3d 覆盖掉。
		Rabbit3DMesh = SphereFinder.Object;
		// 背景穹顶也用球 —— 会被拉到半径 200000cm，法线朝内。
		SkyDomeMesh = SphereFinder.Object;
	}

	// 导入的兔子三维模型（用户自己拖进来的）。找不到就保留上面的球体占位。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RabbitMeshFinder(
		TEXT("/Game/3d/rabbit/低模3d兔子/StaticMeshes/rabbit_3d.rabbit_3d"));
	if (RabbitMeshFinder.Succeeded())
	{
		Rabbit3DMesh = RabbitMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded())
	{
		BillboardMesh = PlaneFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicMatFinder.Succeeded())
	{
		FallbackMaterial = BasicMatFinder.Object;
	}
}

void AEcoBoxRenderer::SetupComponent(UInstancedStaticMeshComponent* Comp)
{
	if (!Comp) { return; }

	Comp->SetupAttachment(RootComponent);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetGenerateOverlapEvents(false);
	Comp->SetCastShadow(true);
	Comp->bUseAsOccluder = false;
	// 上帝视角下不做视锥剔除会浪费，但也别让远处生物被剔掉
	Comp->SetCullDistances(0, 0);
}

// =====================================================================
// 生命周期
// =====================================================================

void AEcoBoxRenderer::BeginPlay()
{
	Super::BeginPlay();
	RebuildAll();
}

#if WITH_EDITOR
void AEcoBoxRenderer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 在编辑器里拖动参数时立刻看到效果
	RebuildAll();
}
#endif

void AEcoBoxRenderer::RebuildAll()
{
	ResolveMaterials();

	// 材质赋值同样只在覆盖模式开着时做（关掉之后就完全用组件上的材质）。
	// 另外每个组件都是「非空才覆盖」，清空某一项 Actor 属性即可让它退回组件自己的材质。
	if (bOverrideComponentAssets)
	{
		if (GroundMaterial)    { ApplyMaterialColor(GroundISM,    GroundMaterial,   GroundColor,   GroundMID); }
		if (GrassMaterial)     { ApplyMaterialColor(GrassISM,     GrassMaterial,    GrassColor,    GrassMID); }
		// 兔 / 狐用**导入模型自带的 PBR 材质**（带 basecolor/metallic/normal 贴图），
		// 直接原样贴上去，**绝不走 ApplyMaterialColor**。
		//
		// 原因：ApplyMaterialColor 会创建一个 MID 并往里写 Color / BaseColor 参数。
		// 自建的 Unlit 材质参数名是 "Color"，但导入的 PBR 材质里恰好有 "BaseColor"
		// → SetVectorParameterValue("BaseColor", 纯色) 命中，把 basecolor 贴图整个盖掉，
		// 模型就变成一块纯色。贴图一旦被 MID 参数盖住，改材质资产也救不回来。
		if (RabbitMaterial && Rabbit3DISM) { RabbitMID = nullptr; Rabbit3DISM->SetMaterial(0, RabbitMaterial); }
		if (FoxMaterial && Fox3DISM)       { FoxMID    = nullptr; Fox3DISM->SetMaterial(0, FoxMaterial); }
		if (BillboardMaterial) { ApplyMaterialColor(Rabbit2DISM,  BillboardMaterial, RabbitColor,  Rabbit2DMID); }
		if (BillboardMaterial) { ApplyMaterialColor(Fox2DISM,     BillboardMaterial, FoxColor,     Fox2DMID); }
		if (BillboardMaterial) { ApplyMaterialColor(HighlightISM, BillboardMaterial, HighlightColor, HighlightMID); }
		if (WetMaterial)       { ApplyMaterialColor(WetISM,       WetMaterial,      WetColor,      WetMID); }
	}

	// ---- 背景穹顶 ----
	// 它不走上面那套"实例 + 颜色 MID"的流程：只有一个实例，
	// 而且材质必须在**没开 bOverrideComponentAssets** 时也能显示，
	// 所以单独放在这里、单独判断。
	SetupSkyDome();

	// 网格赋值：只有在 bOverrideComponentAssets 开着、且这个属性**非空**的时候才覆盖组件。
	// 非空才覆盖这一点很重要 —— 把 Actor 上的属性清空（设成 None）就等于"这一项我不指定"，
	// 组件上自己设的网格会被保留下来，不会被清成默认形体。
	if (bOverrideComponentAssets)
	{
		if (GroundISM    && GroundMesh)    { GroundISM->SetStaticMesh(GroundMesh); }
		if (GrassISM     && GrassMesh)     { GrassISM->SetStaticMesh(GrassMesh); }
		if (Rabbit3DISM  && Rabbit3DMesh)  { Rabbit3DISM->SetStaticMesh(Rabbit3DMesh); }
		if (Fox3DISM     && Fox3DMesh)     { Fox3DISM->SetStaticMesh(Fox3DMesh); }
		if (Rabbit2DISM  && BillboardMesh) { Rabbit2DISM->SetStaticMesh(BillboardMesh); }
		if (Fox2DISM     && BillboardMesh) { Fox2DISM->SetStaticMesh(BillboardMesh); }
		if (HighlightISM && GroundMesh)    { HighlightISM->SetStaticMesh(GroundMesh); }
		if (WetISM       && GroundMesh)    { WetISM->SetStaticMesh(GroundMesh); }
	}

	// 整个生态箱**不投射任何阴影**（卡通风：纯平面色块 + 贴图，不要影子）。
	//
	// 之前只关了地面/草/高亮/湿地块，**生物那一组的投影还开着** ——
	// 于是画面里唯一的阴影就是兔狐在地上拖的一个影子，
	// 配着周围完全没有明暗的 Unlit 地面，看着很违和。
	// 这里把 3D 和 2D 贴片一起关掉，画面就只剩纯平面的颜色。
	//
	// 注意 `SetCastShadow(false)` 只关"投射"、不影响"接收"；
	// 现在没有任何东西投影，所以也不会有阴影可接收。
	if (GroundISM)    { GroundISM->SetCastShadow(false); }
	if (GrassISM)     { GrassISM->SetCastShadow(false); }
	if (HighlightISM) { HighlightISM->SetCastShadow(false); }
	if (WetISM)       { WetISM->SetCastShadow(false); }
	if (Rabbit3DISM)  { Rabbit3DISM->SetCastShadow(false); }
	if (Fox3DISM)     { Fox3DISM->SetCastShadow(false); }
	if (Rabbit2DISM)  { Rabbit2DISM->SetCastShadow(false); }
	if (Fox2DISM)     { Fox2DISM->SetCastShadow(false); }

	// 把实际用上的三维网格和它的原始尺寸打进日志。
	// 换模型时经常会怀疑"到底生效没有"，这行日志能直接回答；
	// 尺寸如果离 100 很远，说明模型导入比例不对（归一化能救，但原点位置救不了）。
	{
		auto Describe = [](const UStaticMesh* Mesh) -> FString
		{
			if (!Mesh) { return TEXT("<空>"); }
			const float Height = Mesh->GetBounds().BoxExtent.Z * 2.f;
			return FString::Printf(TEXT("%s（原始高 %.1f cm）"), *Mesh->GetName(), Height);
		};
		UE_LOG(LogTemp, Log, TEXT("[EcoBox] 三维网格：兔 %s  狐 %s  归一化=%s"),
			*Describe(Rabbit3DMesh), *Describe(Fox3DMesh),
			bNormalizeCreature3DSize ? TEXT("开") : TEXT("关"));
	}

	bGroundBuilt = false;
	BuildGround();

	// 湿地块的池子要跟着重铺（材质/开关都可能变了）
	WetPool.Reset();
	WetVisual.Reset();
	if (WetISM) { WetISM->ClearInstances(); }

	LastGrassRebuildTime = -1000.f;
	UpdateGrass(0.f);
	UpdateWetTiles();
	UpdateCreatures();
	UpdateHighlight();
}

void AEcoBoxRenderer::ResolveMaterials()
{
	if (!FallbackMaterial)
	{
		FallbackMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	auto Resolve = [this](TObjectPtr<UMaterialInterface>& Slot, const TCHAR* GamePath) -> UMaterialInterface*
	{
		// 代码里写死的路径是**权威**：只要它能加载，就覆盖属性上已有的值。
		//
		// 为什么不能"Slot 有值就优先"（原来的写法）：
		// 渲染器在编辑器里跑 OnConstruction 时会把当时解析出来的材质写回这些属性，
		// 一旦有人在编辑器里保存过关卡，那份材质就被**固化进 umap** 了。
		// baseBox.umap 里就有一个手摆的 EcoBoxRenderer 实例 —— 它的
		// RabbitMaterial/FoxMaterial 存的是早期的 M_Rabbit/M_Fox，
		// 于是无论代码里怎么写新路径都盖不过它（2026-10-17 被这个坑住的：
		// 实际一直在用旧材质，而旧材质缺 Nanite usage flag → 引擎换成默认灰）。
		if (UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, GamePath))
		{
			Slot = Loaded;
			return Slot;
		}

		// 新路径加载不到才退回属性上的旧值，再不行用兜底。
		if (Slot) { return Slot; }
		return FallbackMaterial;
	};

	GroundMaterial    = Resolve(GroundMaterial,    TEXT("/Game/EcoBox/Materials/M_Ground.M_Ground"));
	GrassMaterial     = Resolve(GrassMaterial,     TEXT("/Game/EcoBox/Materials/M_Grass.M_Grass"));
	BillboardMaterial = Resolve(BillboardMaterial, TEXT("/Game/EcoBox/Materials/M_Billboard.M_Billboard"));

	// ---------------- 兔 / 狐：优先用「Unlit 版」材质 ----------------
	//
	// 为什么需要单独的 Unlit 版：
	// 导入的 `M_rabbit` / `Material_001` 是 **glTF 生成的材质实例**，父材质在引擎插件里
	// （`/InterchangeAssets/gltf/Substrate/M_GLTF`），**改不了连线**。只要走 PBR，
	// 就一定有"受光面亮 / 背光面暗"的自身明暗——用户不要这个效果。
	//
	// 所以在工程里找一份同名 + `_unlit` 后缀的**自己做的 Material**
	// （Unlit + 贴图接 Emissive）。找不到就退回导入的原材质，至少还能看到贴图。
	auto ResolveCreatureMaterial = [this](TObjectPtr<UMaterialInterface>& Slot,
		const TCHAR* UnlitPath, const TCHAR* ImportedPath) -> UMaterialInterface*
	{
		if (UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, UnlitPath))
		{
			Slot = Unlit;
			return Slot;
		}
		if (UMaterialInterface* Imported = LoadObject<UMaterialInterface>(nullptr, ImportedPath))
		{
			Slot = Imported;
			return Slot;
		}
		if (Slot) { return Slot; }
		return FallbackMaterial;
	};

	RabbitMaterial = ResolveCreatureMaterial(RabbitMaterial,
		TEXT("/Game/3d/rabbit/低模3d兔子/Materials/M_rabbit_unlit.M_rabbit_unlit"),
		TEXT("/Game/3d/rabbit/低模3d兔子/Materials/M_rabbit.M_rabbit"));

	FoxMaterial = ResolveCreatureMaterial(FoxMaterial,
		TEXT("/Game/3d/fox/低模3d狐狸/Materials/Material_001_unlit.Material_001_unlit"),
		TEXT("/Game/3d/fox/低模3d狐狸/Materials/Material_001.Material_001"));

	// ---------------- 主动补齐材质的 Usage ----------------
	//
	// UE5.8 把材质上的那两个勾选框**废弃**了：
	//     UE_DEPRECATED(5.8, "Always use the GetUsageByFlag() accessor.")
	//     uint8 bUsedWithInstancedStaticMeshes : 1;   // 编辑器里已经不显示
	//     uint32 bUsedWithNanite : 1;
	// 改成运行时自动收集。但收集是**异步**的：`FStaticMeshSceneProxy` 创建时会调
	// `CheckMaterialUsage_Concurrent(MATUSAGE_InstancedStaticMeshes)`，如果对应 shader
	// 还没编译完就直接 `Section.Material = UMaterial::GetDefaultMaterial(MD_Surface)`
	// —— 表现就是"第一次进关卡，兔狐是一坨默认灰"。
	//
	// 所以在**给组件赋材质之前**主动把这些 usage 排进编译队列。
	// 编辑器里会现场编译（第一次稍慢），打包版直接用烘焙好的 shader，几乎零成本。
	auto EnsureCreatureUsage = [](UMaterialInterface* Mat, const TCHAR* Label)
	{
		if (!Mat) { return; }

#if WITH_EDITOR
		// UE5.8 把材质上的 `bUsedWithInstancedStaticMeshes` / `bUsedWithNanite` 废弃了
		// （都标了 UE_DEPRECATED(5.8, "Always use the GetUsageByFlag() accessor.")），
		// 改成"运行时自动收集"。但收集是**异步**的：场景代理创建时 shader 还没编译好，
		// `CheckMaterialUsage_Concurrent` 就失败，引擎直接
		// `Section.Material = UMaterial::GetDefaultMaterial(MD_Surface)` —— 画面变黑/灰。
		//
		// 编辑器里可以绕过这层"验证"：
		//   SetUsageByFlag()  强制写 usage 位（不校验、不触发编译）
		//   CacheShaders()    再同步编译一次，让 shader 真的带上 instancing 变体
		// 顺序不能反：先写位，再编译，否则编译出来的还是旧变体。
		if (UMaterial* M = Cast<UMaterial>(Mat))
		{
			M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
			M->SetUsageByFlag(MATUSAGE_Nanite, true);
			M->CacheShaders(EMaterialShaderPrecompileMode::Default);

			UE_LOG(LogTemp, Warning,
				TEXT("[EcoBox] 材质 usage（%s）：已强制写入 ISM/Nanite 并重编译 shader"), Label);
			return;
		}
#endif

		// 打包后 shader 是烘焙好的，走正常检查即可（此时应该直接返回 true）。
		const bool bOkISM    = Mat->CheckMaterialUsage(MATUSAGE_InstancedStaticMeshes);
		const bool bOkNanite = Mat->CheckMaterialUsage(MATUSAGE_Nanite);
		UE_LOG(LogTemp, Warning,
			TEXT("[EcoBox] 材质 usage（%s）：ISM=%s  Nanite=%s"),
			Label,
			bOkISM    ? TEXT("可用") : TEXT("不可用!"),
			bOkNanite ? TEXT("可用") : TEXT("不可用!"));
	};
	EnsureCreatureUsage(RabbitMaterial, TEXT("兔"));
	EnsureCreatureUsage(FoxMaterial,   TEXT("狐"));

	// 湿地块薄片：先看工程里有没有专门的 M_Wet，没有就复用 2D 贴片材质
	//（同样是 Unlit + Color→Emissive），免得为一个纯色薄片再单独做一个资产。
	WetMaterial = Resolve(WetMaterial, TEXT("/Game/EcoBox/Materials/M_Wet.M_Wet"));
	if (WetMaterial == FallbackMaterial) { WetMaterial = BillboardMaterial; }

	// 背景穹顶：没有专门的 M_SkyDome 就复用 2D 贴片材质 —— 关键要求只有"
	// 参数名是 Color、输出到 Emissive、Unlit"，M_Billboard 三条全占。
	SkyDomeMaterial = Resolve(SkyDomeMaterial, TEXT("/Game/EcoBox/Materials/M_SkyDome.M_SkyDome"));
	if (SkyDomeMaterial == FallbackMaterial) { SkyDomeMaterial = BillboardMaterial; }

	// 把实际生效的材质打进日志。
	// 排查"模型是纯色 / 纯灰"时，第一个要看的就是这一行 ——
	// 如果兔/狐显示的是 M_Rabbit / M_Fox（或 BasicShapeMaterial），
	// 说明走的是旧材质或兜底，而不是导入的 M_rabbit / Material_001。
	UE_LOG(LogTemp, Log, TEXT("[EcoBox] 材质：兔 %s  狐 %s  草 %s  地面 %s"),
		*GetNameSafe(RabbitMaterial),
		*GetNameSafe(FoxMaterial),
		*GetNameSafe(GrassMaterial),
		*GetNameSafe(GroundMaterial));
}

void AEcoBoxRenderer::SetupSkyDome()
{
	if (!SkyDome) { return; }

	// 关掉时直接隐藏组件。**不动材质**，下次开回来不用重建。
	SkyDome->SetVisibility(bShowSkyDome);
	if (!bShowSkyDome)
	{
		return;
	}

	if (SkyDomeMesh)
	{
		SkyDome->SetStaticMesh(SkyDomeMesh);
	}

	// 半径按网格原始尺寸折算。引擎的 Sphere 半径是 50cm（直径 100），
	// 所以缩放 = 目标半径 / 50，才能让**球面**正好落在目标半径上。
	const float RawRadius = SkyDomeMesh ? FMath::Max(1.f, SkyDomeMesh->GetBounds().BoxExtent.X) : 50.f;
	const float Scale = FMath::Max(1.f, SkyDomeRadius) / RawRadius;

	// 球心放在世界原点：地图是 128m 见方的生态箱，球半径 2000m，
	// 相机再怎么跑都在球心附近，"球心对齐原点"已经足够。
	SkyDome->SetWorldScale3D(FVector(Scale));
	SkyDome->SetWorldLocation(FVector::ZeroVector);
	SkyDome->SetWorldRotation(FRotator::ZeroRotator);

	// Unlit + Color → Emissive。颜色值就是屏幕上的最终颜色，不受光照/曝光影响。
	if (SkyDomeMaterial)
	{
		SkyDomeMID = MakeColorMID(SkyDomeMaterial, SkyDomeColor);
		UMaterialInterface* UseMat = SkyDomeMID
			? static_cast<UMaterialInterface*>(SkyDomeMID)
			: static_cast<UMaterialInterface*>(SkyDomeMaterial);
		SkyDome->SetMaterial(0, UseMat);
	}
}

UMaterialInstanceDynamic* AEcoBoxRenderer::MakeColorMID(UMaterialInterface* Material, const FLinearColor& Color)
{
	if (!Material) { return nullptr; }

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
	if (MID)
	{
		// 不同基础材质参数名不一样，两个都写一遍，不存在的那个会被忽略
		MID->SetVectorParameterValue(ColorParameterName, Color);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
	}
	return MID;
}

void AEcoBoxRenderer::ApplyMaterialColor(UInstancedStaticMeshComponent* Comp,
	UMaterialInterface* Material, const FLinearColor& Color,
	TObjectPtr<UMaterialInstanceDynamic>& OutMID)
{
	if (!Comp) { return; }

	OutMID = MakeColorMID(Material, Color);
	Comp->SetMaterial(0, OutMID ? (UMaterialInterface*)OutMID : Material);
}

// =====================================================================
// 每帧
// =====================================================================

void AEcoBoxRenderer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bGroundBuilt)
	{
		BuildGround();
	}

	// ---- 3D / 2D 切换：两个阈值之间是迟滞区，防止在边界反复闪 ----
	if (bEnableDistanceLOD)
	{
		const float CamH = GetCameraHeight();
		if (bUsing2D && CamH < LODSwitchNearHeight)
		{
			bUsing2D = false;
		}
		else if (!bUsing2D && CamH > LODSwitchFarHeight)
		{
			bUsing2D = true;
		}
	}
	else
	{
		bUsing2D = false;
	}

	UpdateGrass(0.f);
	UpdateWetTiles();
	UpdateCreatures();
	UpdateHighlight();
}

// =====================================================================
// 湿地块
// =====================================================================

FTransform AEcoBoxRenderer::MakeWetTransform(int32 CellIdx, bool bWet) const
{
	const FVector Pos = UEcoBoxSimSubsystem::CellToWorld(CellIdx);

	if (!bWet)
	{
		// 缩放 0 = 隐藏。实例仍然留在池子里，所以浇水/变干不会触发任何重建。
		return FTransform(FRotator::ZeroRotator, Pos, FVector::ZeroVector);
	}

	// 比方块小一圈（0.88），这样格与格之间会露出地面的原色，
	// 看上去是一块块"浇过的地"而不是把整张图换成另一种底色。
	const float S = EcoBoxConst::TileSize / 100.f;

	return FTransform(
		FRotator::ZeroRotator,
		FVector(Pos.X, Pos.Y, 2.f + GroundClearance),
		FVector(S * 0.88f, S * 0.88f, 0.02f));
}

void AEcoBoxRenderer::UpdateWetTiles()
{
	if (!WetISM) { return; }

	if (!bShowWet || !GroundMesh)
	{
		if (WetISM->GetInstanceCount() > 0) { WetISM->ClearInstances(); }
		WetPool.Reset();
		WetVisual.Reset();
		return;
	}

	const UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	const TArray<FECell>& Cells = Sim->GetCells();
	if (Cells.Num() != EcoBoxConst::CellCount) { return; }

	// 和草完全一样的套路：首次一次性铺满 4096 个实例（**直接用真实变换**，
	// 不能先塞隐藏再逐个挪 —— 同一帧的后续更新会把刚发起的实例缓冲作废）。
	if (WetISM->GetInstanceCount() != EcoBoxConst::CellCount
		|| WetVisual.Num() != EcoBoxConst::CellCount)
	{
		WetPool.Reset();
		WetPool.SetNumUninitialized(EcoBoxConst::CellCount);
		WetVisual.SetNumUninitialized(EcoBoxConst::CellCount);

		for (int32 i = 0; i < EcoBoxConst::CellCount; ++i)
		{
			const uint8 Level = Cells[i].bWet ? 1 : 0;
			WetVisual[i] = Level;
			WetPool[i] = MakeWetTransform(i, Level != 0);
		}

		WetISM->ClearInstances();
		WetISM->AddInstances(WetPool, false, true);
		return;
	}

	// 增量：只有"干 ↔ 湿"翻转的那几格才重写
	for (int32 i = 0; i < EcoBoxConst::CellCount; ++i)
	{
		const uint8 Level = Cells[i].bWet ? 1 : 0;
		if (Level == WetVisual[i]) { continue; }

		WetVisual[i] = Level;
		WetPool[i] = MakeWetTransform(i, Level != 0);
		WetISM->UpdateInstanceTransform(i, WetPool[i], false, true, false);
	}
}

// =====================================================================
// 地面
// =====================================================================

void AEcoBoxRenderer::BuildGround()
{
	if (!GroundISM)
	{
		bGroundBuilt = true;
		return;
	}

	GroundISM->SetVisibility(bShowGround);

	if (!bShowGround || !GroundMesh)
	{
		GroundISM->ClearInstances();
		bGroundBuilt = true;
		return;
	}

	const float ScaleXY = EcoBoxConst::TileSize / 100.f;
	const float ScaleZ = GroundThickness / 100.f;
	const float Z = -GroundThickness * 0.5f;

	TArray<FTransform> Xforms;
	Xforms.Reserve(EcoBoxConst::CellCount);

	for (int32 i = 0; i < EcoBoxConst::CellCount; ++i)
	{
		const FVector Pos = UEcoBoxSimSubsystem::CellToWorld(i);
		Xforms.Emplace(
			FRotator::ZeroRotator,
			FVector(Pos.X, Pos.Y, Z),
			FVector(ScaleXY, ScaleXY, ScaleZ));
	}

	GroundISM->ClearInstances();
	GroundISM->AddInstances(Xforms, false, true);
	bGroundBuilt = true;
}

// =====================================================================
// 草
// =====================================================================

/**
 * 隐藏一个实例：缩放到 0。
 *
 * 位置刻意留在原点，而不是挪到极远处（比如 Z = -1000000）：
 * 实例位置会参与组件包围盒计算，扔太远会把整个组件的包围盒撑成一个
 * 覆盖上百万厘米的盒子，剔除判断就全乱套了。
 */
static FTransform MakeHiddenTransform()
{
	return FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::ZeroVector);
}

/**
 * 把池子里 [StartIndex, 末尾) 的位置全部写成"隐藏"。
 *
 * 池子改成只扩容之后，上一帧还活着、这一帧死掉的槽位不会自动消失，
 * 必须每帧显式隐藏，否则会留下一堆不动的"幽灵"。
 */
static void HideIdleSlots(TArray<FTransform>& Pool, int32 StartIndex)
{
	for (int32 i = FMath::Max(0, StartIndex); i < Pool.Num(); ++i)
	{
		Pool[i] = MakeHiddenTransform();
	}
}

/**
 * 把实例池调整到能装下 Desired 个实例。
 *
 * 关键：**只在需要扩容时**才重建实例数组，绝不因为数量小幅波动就 Clear + Add。
 * 重建会让渲染代理整个换掉，重建期间组件是空白的 —— 生物数量每帧都在变，
 * 如果每帧都走一遍，画面上就是"一直不显示"（兔子消失就是这么来的）。
 * 缩容不重建：多出来的槽位由 HideIdleSlots 每帧写成缩放 0 隐藏掉。
 */
void AEcoBoxRenderer::EnsurePoolSize(UInstancedStaticMeshComponent* Comp,
	TArray<FTransform>& Pool, int32 Desired)
{
	if (!Comp) { return; }

	if (Desired <= 0)
	{
		// 真归零才清掉，省渲染开销
		if (Comp->GetInstanceCount() > 0) { Comp->ClearInstances(); }
		Pool.Reset();
		return;
	}

	if (Pool.Num() >= Desired && Comp->GetInstanceCount() == Pool.Num())
	{
		return;   // 容量够用，什么都不用做
	}

	// 扩容：**只追加新槽位**，绝不 Clear + Add。
	//
	// `ClearInstances()` 会让整个渲染代理重建，重建那一帧组件是**空的** ——
	// 兔子繁殖到某个数量、池子正好跨过 2 的幂时，就会"突然消失一下"（偶尔还伴随
	// 位置跳变，因为重建期间旧变换已经失效）。原来这里就是 Clear + Add，
	// 所以个体数量增长的过程中会时不时闪一下。
	//
	// `AddInstances()` 是**追加**，不重建代理，所以加多少都不会闪。
	const int32 NewSize = FMath::Max(256, (int32)FMath::RoundUpToPowerOfTwo(Desired));
	const int32 OldSize = Pool.Num();

	// 正常情况下组件实例数和池子大小是一致的；万一不同步（比如上次重建失败），
	// 退回一次完整重建把两者对齐。
	if (Comp->GetInstanceCount() != OldSize)
	{
		Pool.Reset();
		Pool.SetNumUninitialized(NewSize);
		for (int32 i = 0; i < NewSize; ++i)
		{
			Pool[i] = MakeHiddenTransform();
		}
		Comp->ClearInstances();
		Comp->AddInstances(Pool, false, true);
		return;
	}

	// 追加路径：(NewSize - OldSize) 个新的隐藏槽位
	TArray<FTransform> NewSlots;
	NewSlots.SetNumUninitialized(NewSize - OldSize);
	for (FTransform& T : NewSlots)
	{
		T = MakeHiddenTransform();
	}

	Pool.SetNum(NewSize);
	for (int32 i = OldSize; i < NewSize; ++i)
	{
		Pool[i] = MakeHiddenTransform();
	}

	Comp->AddInstances(NewSlots, false, true);
}

uint8 AEcoBoxRenderer::GrassVisualLevel(uint8 Grass)
{
	if (Grass == 0) { return 0; }
	// 1..GrassLevels
	const int32 Lv = 1 + (int32)Grass * GrassLevels / (EcoBoxConst::MaxGrass + 1);
	return (uint8)FMath::Clamp(Lv, 1, GrassLevels);
}

FTransform AEcoBoxRenderer::MakeGrassTransform(int32 CellIdx, uint8 Level) const
{
	const FVector Pos = UEcoBoxSimSubsystem::CellToWorld(CellIdx);

	if (Level == 0)
	{
		// 缩放为 0 就是隐藏：实例仍在池子里，不会引起渲染代理重建
		return FTransform(FRotator::ZeroRotator, Pos, FVector::ZeroVector);
	}

	// 用档位而不是原始草量算高度，这样同一档内的微小生长不会触发重写
	const float T = (float)Level / (float)GrassLevels;
	const float ScaleZ = FMath::Max(0.06f, GrassMaxScaleZ * FMath::Sqrt(T));

	// 草是**方块草皮**：一格一块，XY 铺满整个地块（GrassScaleXY 默认 1.95，
	// 网格 100cm 宽 / 格子 200cm → 2.0 正好铺满，留一点缝能看出格与格的分界）。
	//
	// 按生长档位给一点点大小差异：长得高的格子铺得更满，
	// 整片草地才不像一张规规矩矩的棋盘。
	const float SizeJitter = 0.94f + 0.07f * T;
	const float ScaleXY = GrassScaleXY * SizeJitter;

	// 方块不需要随机朝向（转了也看不出来），保持 ZeroRotator 最省。
	return FTransform(
		FRotator::ZeroRotator,
		FVector(Pos.X, Pos.Y, 50.f * ScaleZ + GroundClearance),
		FVector(ScaleXY, ScaleXY, ScaleZ));
}

void AEcoBoxRenderer::UpdateGrass(float /*SimTime*/)
{
	if (!GrassISM) { return; }

	if (!bShowGrass)
	{
		if (GrassISM->GetInstanceCount() > 0) { GrassISM->ClearInstances(); }
		GrassPool.Reset();
		GrassVisual.Reset();
		return;
	}

	const UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	const TArray<FECell>& Cells = Sim->GetCells();
	if (Cells.Num() != EcoBoxConst::CellCount) { return; }

	// 草的视觉变化很慢，没必要每帧刷
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now - LastGrassRebuildTime < 1.f / FMath::Max(0.5f, GrassRebuildRate))
	{
		return;
	}
	LastGrassRebuildTime = Now;

	// 首次（或池子对不上）时一次性铺满整张图，**直接用真实变换**。
	// 不要"先加一堆隐藏实例、再逐个挪到真实位置" —— 那样同一帧里的后续更新
	// 会把刚发起的实例缓冲/树构建作废，草地就会一直画不出来。
	if (GrassISM->GetInstanceCount() != EcoBoxConst::CellCount
		|| GrassVisual.Num() != EcoBoxConst::CellCount)
	{
		GrassPool.Reset();
		GrassPool.SetNumUninitialized(EcoBoxConst::CellCount);
		GrassVisual.SetNumUninitialized(EcoBoxConst::CellCount);

		for (int32 i = 0; i < EcoBoxConst::CellCount; ++i)
		{
			GrassVisual[i] = GrassVisualLevel(Cells[i].Grass);
			GrassPool[i] = MakeGrassTransform(i, GrassVisual[i]);
		}

		GrassISM->ClearInstances();
		GrassISM->AddInstances(GrassPool, false, true);
		return;
	}

	// 增量：只有跨档（或者从有到无 / 从无到有）才重写那一个实例。
	// 这样每帧真正动到的实例从 4096 个降到几十个，也不会每帧重建渲染代理。
	for (int32 i = 0; i < EcoBoxConst::CellCount; ++i)
	{
		const uint8 Level = GrassVisualLevel(Cells[i].Grass);
		if (Level == GrassVisual[i]) { continue; }

		GrassVisual[i] = Level;
		GrassPool[i] = MakeGrassTransform(i, Level);
		GrassISM->UpdateInstanceTransform(i, GrassPool[i], false, true, false);
	}
}

// =====================================================================
// 生物：3D 与 2D 二选一
// =====================================================================

void AEcoBoxRenderer::UpdateCreatures()
{
	if (!Rabbit3DISM || !Fox3DISM || !Rabbit2DISM || !Fox2DISM) { return; }

	const bool b2D = bUsing2D && bEnableDistanceLOD;

	UInstancedStaticMeshComponent* RComp = b2D ? Rabbit2DISM : Rabbit3DISM;
	UInstancedStaticMeshComponent* FComp = b2D ? Fox2DISM : Fox3DISM;
	UInstancedStaticMeshComponent* RIdle = b2D ? Rabbit3DISM : Rabbit2DISM;
	UInstancedStaticMeshComponent* FIdle = b2D ? Fox3DISM : Fox2DISM;

	// 当前不用的一对只关可见性，**不清空实例**。
	// 清空的话下次切回来必须先 AddInstances 重建一遍实例缓冲，切换瞬间会"空一下"；
	// 留着实例（反正不可见、不产生绘制），切回来时池子大小还是对的，
	// 直接批量写一遍变换就立刻出现了。
	RIdle->SetVisibility(false);
	FIdle->SetVisibility(false);

	TArray<FTransform>& RPool = b2D ? BillboardRabbitPool : RabbitPool;
	TArray<FTransform>& FPool = b2D ? BillboardFoxPool : FoxPool;

	if (!bShowCreatures)
	{
		RComp->SetVisibility(false);
		FComp->SetVisibility(false);
		EnsurePoolSize(RComp, RPool, 0);
		EnsurePoolSize(FComp, FPool, 0);
		return;
	}

	RComp->SetVisibility(true);
	FComp->SetVisibility(true);

	const UEcoBoxSimSubsystem* Sim = GetSim();
	if (!Sim) { return; }

	const FVector CamLoc = GetCameraLocation();
	const TArray<FECreature>& Creatures = Sim->GetCreatures();

	// 先数一遍存活数量，池子按这个大小准备（数量没变就不会重建）
	int32 NumR = 0;
	int32 NumF = 0;
	for (const FECreature& C : Creatures)
	{
		if (!C.bAlive) { continue; }
		if (C.Type == ECreatureType::Rabbit) { ++NumR; } else { ++NumF; }
	}

	EnsurePoolSize(RComp, RPool, NumR);
	EnsurePoolSize(FComp, FPool, NumF);

	// 三维网格的归一化参数：每帧只算一次，别在生物循环里反复取包围盒
	float RabbitHeightFit = 1.f;
	float FoxHeightFit = 1.f;
	float RabbitBaseHalfZ = 50.f;
	float FoxBaseHalfZ = 50.f;
	{
		auto ComputeFit = [this](const UStaticMesh* Mesh, float& OutFit, float& OutBaseHalfZ)
		{
			const float RawHalfZ = Mesh ? Mesh->GetBounds().BoxExtent.Z : 50.f;
			const float RawHeight = FMath::Max(1.f, RawHalfZ * 2.f);

			if (bNormalizeCreature3DSize)
			{
				// 把模型自身高度折算到目标高度：导入 1 单位高或 1000 单位高都一样大
				const float Target = FMath::Max(1.f, CreatureMeshTargetHeight);
				OutFit = Target / RawHeight;
				OutBaseHalfZ = Target * 0.5f;
			}
			else
			{
				OutFit = 1.f;
				OutBaseHalfZ = RawHalfZ;
			}
		};
		ComputeFit(Rabbit3DMesh, RabbitHeightFit, RabbitBaseHalfZ);
		ComputeFit(Fox3DMesh,    FoxHeightFit,    FoxBaseHalfZ);
	}

	int32 ri = 0;
	int32 fi = 0;

	for (const FECreature& C : Creatures)
	{
		if (!C.bAlive) { continue; }
		if (!Sim->GetCells().IsValidIndex(C.CellIdx)) { continue; }

		const bool bRabbit = (C.Type == ECreatureType::Rabbit);
		// HopAlpha：0 = 踩地，正 = 腾空（越接近 1 越高），负 = 落地压扁
		float HopAlpha = 0.f;
		FVector Pos = Sim->GetCreatureWorldPos(C, HopAlpha);

		// GetCreatureWorldPos 已经把"跳起来的高度"加进 Pos.Z 了。
		// 但下面 3D 分支会用"脚底贴地"的公式重新算 Pos.Z，会把那份高度冲掉，
		// 所以这里先把跳跃高度单独扣出来，算完基础位置再贴回去。
		float HopLift = 0.f;
		if (bRabbit && HopAlpha > 0.f)
		{
			HopLift = HopAlpha * EcoParams::S().RabbitHopHeight;
		}

		FTransform Xform;

		if (!b2D)
		{
			// 三维：按网格包围盒把脚底对齐到地面
			const float SizeFactor = bRabbit ? RabbitScale : FoxScale;
			const float Fit        = bRabbit ? RabbitHeightFit : FoxHeightFit;
			const float BaseHalfZ  = bRabbit ? RabbitBaseHalfZ : FoxBaseHalfZ;
			// C.GrowScale 是成年进度（幼崽 0.55 → 成年 1.0），所以兔子是"吃着草长大"的
			const float Scale      = SizeFactor * C.Size * C.GrowScale * Fit;
			Pos.Z = BaseHalfZ * Scale + GroundClearance + HopLift;

			const float YawOffset = bRabbit ? RabbitMeshYawOffset : FoxMeshYawOffset;
			float PitchDeg = 0.f;
			FVector ScaleVec(Scale);

			// 兔子跳跃时做一点"挤压拉伸"和"前倾"，动作才有生命感
			if (bRabbit && FMath::Abs(HopAlpha) > KINDA_SMALL_NUMBER)
			{
				const UEcoBoxSettings& HS = EcoParams::S();
				const float SS = HS.RabbitHopSquashStretch;
				if (SS > 0.f)
				{
					// 腾空（HopAlpha>0）：Z 拉长、XY 收窄；落地（<0）：反过来压扁
					ScaleVec.Z     = Scale * (1.f + SS * HopAlpha);
					const float XYSquash = 1.f - SS * 0.5f * HopAlpha;
					ScaleVec.X     = Scale * XYSquash;
					ScaleVec.Y     = Scale * XYSquash;
				}
				// 前倾：腾空时朝行进方向低头
				if (HopAlpha > 0.f)
				{
					PitchDeg = HS.RabbitHopPitchDeg * HopAlpha;
				}
			}

			Xform = FTransform(
				FRotator(PitchDeg, C.FacingYaw + YawOffset, 0.f),
				Pos, ScaleVec);
		}
		else
		{
			// 二维：贴片正面朝向相机，尺寸按世界厘米换算成缩放
			const float Grow = FMath::Max(0.05f, C.GrowScale);
			const float W = (bRabbit ? 55.f : 70.f) * BillboardBoost * C.Size * Grow;
			const float H = (bRabbit ? 70.f : 85.f) * BillboardBoost * C.Size * Grow;

			const FVector Dir = (CamLoc - Pos).GetSafeNormal();
			const FQuat Rot = FRotationMatrix::MakeFromZ(Dir).ToQuat();
			Pos.Z = H * 0.5f + GroundClearance + HopLift;

			// 2D 贴片也做伸缩：腾空时纵向拉长一点
			float HScale = 1.f;
			float WScale = 1.f;
			if (bRabbit && FMath::Abs(HopAlpha) > KINDA_SMALL_NUMBER)
			{
				const float SS = EcoParams::S().RabbitHopSquashStretch;
				HScale = 1.f + SS * HopAlpha;
				WScale = 1.f - SS * 0.5f * HopAlpha;
			}

			Xform = FTransform(Rot, Pos,
				FVector(W / 100.f * WScale, H / 100.f * HScale, 1.f));
		}

		if (bRabbit)
		{
			if (RPool.IsValidIndex(ri)) { RPool[ri++] = Xform; }
		}
		else
		{
			if (FPool.IsValidIndex(fi)) { FPool[fi++] = Xform; }
		}
	}

	// 池子只扩容不缩容，所以这一帧没写到的槽位要显式隐藏，
	// 否则上一帧还活着的个体会变成不动的"幽灵"留在场上
	HideIdleSlots(RPool, ri);
	HideIdleSlots(FPool, fi);

	// 整个池子一次性提交。普通 ISM 的批量更新是同步的：
	// 写进 PerInstanceSMData 后当帧就生效，没有异步构建要等
	if (RPool.Num() > 0) { RComp->BatchUpdateInstancesTransforms(0, RPool, false, true); }
	if (FPool.Num() > 0) { FComp->BatchUpdateInstancesTransforms(0, FPool, false, true); }
}

// =====================================================================
// 高亮
// =====================================================================

void AEcoBoxRenderer::SetHighlightCells(const TArray<int32>& InCells)
{
	HighlightCells = InCells;
}

void AEcoBoxRenderer::UpdateHighlight()
{
	if (!HighlightISM) { return; }

	const bool bShouldShow = bShowHighlight && GroundMesh && HighlightCells.Num() > 0;

	HighlightISM->SetVisibility(bShouldShow);

	if (!bShouldShow)
	{
		if (HighlightISM->GetInstanceCount() > 0) { HighlightISM->ClearInstances(); }
		HighlightPool.Reset();
		// 清掉"已写入"记录，下次再亮起来时才会重新铺
		AppliedHighlightCells.Reset();
		return;
	}

	// 内容没变就不碰实例。悬停这个输入是每帧都在刷新的，
	// 但鼠标不动的时候高亮格子其实没变，没必要每帧重写一遍实例缓冲。
	if (HighlightCells == AppliedHighlightCells)
	{
		return;
	}
	AppliedHighlightCells = HighlightCells;

	// 和生物一样用**只扩容**的池子：格数变少时多出来的槽位写成缩放 0 隐藏，
	// 绝不 ClearInstances + AddInstances —— 那会让渲染代理整个换掉，
	// 而水壶的预览圆在贴着地图边缘移动时格数是会来回变的。
	const int32 Desired = HighlightCells.Num();
	EnsurePoolSize(HighlightISM, HighlightPool, Desired);

	const float S = EcoBoxConst::TileSize / 100.f;

	for (int32 i = 0; i < HighlightPool.Num(); ++i)
	{
		if (i >= Desired)
		{
			HighlightPool[i] = MakeHiddenTransform();
			continue;
		}

		// 比方块略小一圈，让相邻的预览格之间留缝，看得出是一格一格
		const FVector Pos = UEcoBoxSimSubsystem::CellToWorld(HighlightCells[i]);
		HighlightPool[i] = FTransform(
			FRotator::ZeroRotator,
			FVector(Pos.X, Pos.Y, 6.f + GroundClearance),
			FVector(S * 0.90f, S * 0.90f, 0.04f));
	}

	if (HighlightPool.Num() > 0)
	{
		HighlightISM->BatchUpdateInstancesTransforms(0, HighlightPool, false, true);
	}
}

// =====================================================================
// 工具
// =====================================================================

UEcoBoxSimSubsystem* AEcoBoxRenderer::GetSim() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetSubsystem<UEcoBoxSimSubsystem>();
	}
	return nullptr;
}

float AEcoBoxRenderer::GetCameraHeight() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				return PC->PlayerCameraManager->GetCameraLocation().Z;
			}
		}
	}
	// 编辑器视口或没有玩家时用兜底高度
	return EditorFallbackCameraHeight;
}

FVector AEcoBoxRenderer::GetCameraLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				return PC->PlayerCameraManager->GetCameraLocation();
			}
		}
	}
	return FVector(0.f, 0.f, EditorFallbackCameraHeight);
}
