// EcoBox - 渲染层：把模拟数据画出来，并在拉远时切成 2D 贴片
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EcoBoxTypes.h"
#include "EcoBoxRenderer.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UEcoBoxSimSubsystem;

/**
 * 渲染器。
 *
 * 模拟层只管数据，这里负责把 4096 格和几百只生物变成实例化网格。
 * 每个组件都是一个普通 ISM，所以整块地面 + 草地 + 全部生物只有个位数的 draw call。
 *
 * 为什么全部用普通 ISM 而不是 HISM：
 * HISM 会用一棵**异步构建**的 cluster tree 做层级剔除，而我们的实例池
 * 每帧都在改（生物在移动、草在生长），这些更新会把还没建完的树反复作废，
 * 结果就是整组实例一会儿画不出来、一会儿又要等树建好才出现。
 * 普通 ISM 没有这棵树，变换写进去当帧就生效。地图才 128m、实例几千个，
 * 本来也用不上 HISM 那套分级剔除。
 *
 * 3D / 2D 切换：准备两套 ISM（三维网格 / 二维贴片），
 * 相机高度越过阈值时整组互换可见性，两个阈值之间做迟滞防止边界闪烁。
 */
UCLASS()
class ECOBOX_API AEcoBoxRenderer : public AActor
{
	GENERATED_BODY()

public:
	AEcoBoxRenderer();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
#endif

	// ---------------- 组件 ----------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UInstancedStaticMeshComponent> GroundISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UInstancedStaticMeshComponent> GrassISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UInstancedStaticMeshComponent> Rabbit3DISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UInstancedStaticMeshComponent> Fox3DISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UInstancedStaticMeshComponent> Rabbit2DISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UInstancedStaticMeshComponent> Fox2DISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UInstancedStaticMeshComponent> HighlightISM;

	/**
	 * 湿润地块上面的那层深色薄片。
	 * 浇水是核心操作，玩家必须一眼看出"哪块地是湿的"，否则整套循环没有反馈。
	 * 用的是地面方块压扁的薄片（不是单面片），这样在低角度下也不会看不见。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UInstancedStaticMeshComponent> WetISM;

	/**
	 * 背景穹顶：一个超大号球体，**法线朝内**（SetReverseCulling(true)），
	 * 把相机整个包在里面，于是"看哪儿都是它"。
	 *
	 * 为什么需要它：`SkyAtmosphere` 渲染出来的**地平线以下**是很暗的颜色
	 * （接近黑），俯视时画面下方那一大片就是它，看着像个大坑。
	 * 这里用一个 Unlit 的纯色穹顶把这一片兜住 —— 颜色完全由我们指定，
	 * 想要多白就多白、想要多柔就多柔，不会被光照/曝光搞出意外。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EcoBox|组件")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	// ---------------- 网格资源 ----------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|网格")
	TObjectPtr<UStaticMesh> GroundMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|网格")
	TObjectPtr<UStaticMesh> GrassMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|网格")
	TObjectPtr<UStaticMesh> Rabbit3DMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|网格")
	TObjectPtr<UStaticMesh> Fox3DMesh;

	/** 2D 贴片用的网格，必须是一个朝 +Z 的平面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|网格")
	TObjectPtr<UStaticMesh> BillboardMesh;

	/** 背景穹顶用的网格。必须是一个球（会被拉到超大、翻转法线）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|网格")
	TObjectPtr<UStaticMesh> SkyDomeMesh;

	// ---------------- 材质 ----------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|材质")
	TObjectPtr<UMaterialInterface> GroundMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|材质")
	TObjectPtr<UMaterialInterface> GrassMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|材质")
	TObjectPtr<UMaterialInterface> RabbitMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|材质")
	TObjectPtr<UMaterialInterface> FoxMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|材质")
	TObjectPtr<UMaterialInterface> BillboardMaterial;

	/** 湿地块薄片的材质。留空就用 BillboardMaterial。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|材质")
	TObjectPtr<UMaterialInterface> WetMaterial;

	/**
	 * 背景穹顶的材质。留空就用 BillboardMaterial（Unlit + Color 参数 → Emissive）。
	 * 必须是 **Unlit** 的，否则会被场景光照/曝光影响，颜色就不准了。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|材质")
	TObjectPtr<UMaterialInterface> SkyDomeMaterial;

	// ---------------- 背景穹顶 ----------------
	/** 是否显示背景穹顶。关掉就恢复成"地平线以下是天空大气的暗色"。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|天空")
	bool bShowSkyDome = true;

	/**
	 * 穹顶颜色 —— 也就是"天际线以下"那片区域的颜色。
	 *
	 * 默认 #E8E9EB（约 0.91,0.913,0.922）：**柔和的米白，不刺眼**。
	 * 因为是 Unlit 直接当 Emissive 输出，这个值就是屏幕上看到的最终亮度 ——
	 * 觉得亮了就往下调（0.85 左右就很柔和），想更干净就往上加到 0.95。
	 * > sRGB 的 #E8E9EB 换算成线性大约是 (0.82,0.83,0.84)，这里给的是**线性值**。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|天空")
	FLinearColor SkyDomeColor = FLinearColor(0.82f, 0.83f, 0.84f);

	/**
	 * 穹顶球半径（厘米）。要**大到把相机最远的位置也包进去**，
	 * 否则相机会"穿出"球外，看到球的外壁被剔除后的一片黑。
	 * 相机摇臂上限 22000cm、俯角最大 -5°，离原点最远约 22m；这里给 2000m，绰绰有余。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|天空")
	float SkyDomeRadius = 200000.f;

	/** 材质里颜色参数的名字。常用的有 "Color" / "BaseColor"。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|材质")
	FName ColorParameterName = TEXT("Color");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|颜色")
	FLinearColor GroundColor = FLinearColor(0.28f, 0.20f, 0.13f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|颜色")
	FLinearColor GrassColor = FLinearColor(0.22f, 0.55f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|颜色")
	FLinearColor RabbitColor = FLinearColor(0.92f, 0.90f, 0.86f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|颜色")
	FLinearColor FoxColor = FLinearColor(0.90f, 0.35f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|颜色")
	FLinearColor HighlightColor = FLinearColor(1.f, 0.95f, 0.3f);

	/**
	 * 湿润地块的颜色。地面是棕色 (0.28,0.20,0.13)，这里用发蓝的暗色 ——
	 * 对比度够又不至于把地面完全盖掉，一眼能分出"浇过 / 没浇过"。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|颜色")
	FLinearColor WetColor = FLinearColor(0.10f, 0.16f, 0.26f);

	// ---------------- 尺寸 ----------------
	/** 地面每格厚度的半高（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	float GroundThickness = 10.f;

	/**
	 * 草长满一格时的高度倍率（相对网格原始高度 100cm）。
	 * 现在默认 0.26 → 满高约 26cm。比原来的 0.45（45cm）矮一截，
	 * 目的是**别把地上的湿地块整个糊住** —— 草矮了以后，俯视时能从
	 * 草的间隙和矮下去的边缘看到底下的蓝色湿地图层。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	float GrassMaxScaleZ = 0.26f;

	/**
	 * 草丛的 XY 缩放（相对网格原始尺寸）。
	 * 网格 100cm 宽、格子 200cm → 2.0 = 正好铺满一格。
	 * 现在默认 **1.55**（原来 1.95）：草皮从"铺满整格"收窄到约 78%，
	 * **四周留出一圈格边的地面/湿地块** —— 这样即使草长到最满，
	 * 也能从每格边缘的缝里看出这块地有没有浇过水。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	float GrassScaleXY = 1.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	float RabbitScale = 0.80f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	float FoxScale = 0.95f;

	/**
	 * 三维生物网格归一化之后的目标高度（厘米）。
	 * 换真模型时会用得上：导入的模型原始尺寸五花八门（1 单位高 / 100 单位高都有），
	 * 开着下面的开关就会自动折算，画出来始终是这个高度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	float CreatureMeshTargetHeight = 100.f;

	/**
	 * 是否把三维生物网格自动归一到 CreatureMeshTargetHeight。
	 * 开着（默认）：不管模型导入时多大，画出来都一样大，RabbitScale/FoxScale 表示"占目标高度的比例"。
	 * 关掉：按模型原始尺寸 × RabbitScale/FoxScale，模型多大就多大。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	bool bNormalizeCreature3DSize = true;

	/**
	 * 兔子模型自身朝向的修正角（度）。
	 * 模拟层给的朝向以 UE 的 +X 为前方；导入的模型正前方经常不是 +X，
	 * 兔子横着走 / 倒着走的时候就用它转回来。
	 *
	 * 符号约定：**正值 = 画面上顺时针，负值 = 画面上逆时针**（从上帝视角看）。
	 * 当前 -90 = 逆时针 90 度。转反了就改成 +90。
	 * 改完立刻生效（每帧都读），编辑器视口里拖一下就能看到。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|朝向")
	float RabbitMeshYawOffset = -90.f;

	/** 狐狸模型自身朝向的修正角（度）。含义同上。当前 -90 = 逆时针 90 度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|朝向")
	float FoxMeshYawOffset = -90.f;

	/**
	 * 是否用上面那些"网格 / 材质"属性去覆盖组件自己的设置。
	 *
	 * 开着（默认）：Actor 上的属性是唯一赋值源 —— 直接改组件上的 StaticMesh / 材质
	 * 会在下一次 RebuildAll（保存、Play、重跑 OnConstruction）时被顶回去。
	 * 关掉：渲染器不再碰组件的网格和材质，你愿意在哪个组件上改就在哪改。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|资源")
	bool bOverrideComponentAssets = true;

	/** 草和生物离地抬高多少厘米，避免和地面共面产生 z-fighting 闪烁。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	float GroundClearance = 2.f;

	/** 2D 贴片在原有尺寸上的放大倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|尺寸")
	float BillboardBoost = 1.5f;

	// ---------------- 3D / 2D 切换 ----------------
	// 阈值跟相机的 FOV 是绑定的：可见范围 ≈ 2 * ArmLength * tan(FOV/2)，
	// 而相机高度 = ArmLength * sin(55°)。
	// FOV 从 90 收到 50 之后，要看到整张 12800cm 的地图得站到约 11200 高，
	// 所以这里的阈值也跟着抬高（否则一进游戏就直接是 2D 贴片）。
	/** 相机高度超过这个值就切成 2D。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|LOD")
	float LODSwitchFarHeight = 11500.f;

	/** 相机高度低于这个值才切回 3D。与上面的差值就是迟滞区间。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|LOD")
	float LODSwitchNearHeight = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|LOD")
	bool bEnableDistanceLOD = true;

	/** 编辑器视口里没有玩家相机，用它模拟一个高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|LOD")
	float EditorFallbackCameraHeight = 3000.f;

	// ---------------- 开关 ----------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|开关")
	bool bShowGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|开关")
	bool bShowGrass = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|开关")
	bool bShowCreatures = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|开关")
	bool bShowHighlight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|开关")
	bool bShowWet = true;

	/** 草实例每秒最多重建几次。重建全图 4096 个实例不便宜，限一下频。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EcoBox|性能")
	float GrassRebuildRate = 8.f;

	// ---------------- 对外接口 ----------------
	/**
	 * 鼠标悬停时高亮哪些格子。
	 *
	 * 一颗种子是"单格"（镰刀 / 放置），水壶给的是一整圈圆的格子 ——
	 * 所以这里收的是**数组**而不是单格。传空数组就是全部熄灭。
	 * 内部会和上一帧比对，内容没变就不碰实例，避免每帧重建。
	 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void SetHighlightCells(const TArray<int32>& InCells);

	/** 当前是否处于 2D 贴片模式。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	bool IsUsing2D() const { return bUsing2D; }

	/** 强制刷新所有实例（改了资源之后调用）。 */
	UFUNCTION(BlueprintCallable, Category = "EcoBox")
	void RebuildAll();

private:
	UEcoBoxSimSubsystem* GetSim() const;
	void SetupComponent(UInstancedStaticMeshComponent* Comp);
	void ApplyMaterialColor(UInstancedStaticMeshComponent* Comp,
		UMaterialInterface* Material, const FLinearColor& Color,
		TObjectPtr<UMaterialInstanceDynamic>& OutMID);
	void ResolveMaterials();
	/** 摆好背景穹顶（尺寸 / 位置 / 颜色）。颜色变了重跑一次即可。 */
	void SetupSkyDome();
	FVector GetCameraLocation() const;

	void BuildGround();
	void UpdateGrass(float SimTime);
	void UpdateCreatures();
	void UpdateHighlight();
	void UpdateWetTiles();

	/** 当前相机高度。 */
	float GetCameraHeight() const;

	UMaterialInstanceDynamic* MakeColorMID(UMaterialInterface* Material, const FLinearColor& Color);

	/** 找不到专用材质时的兜底。 */
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FallbackMaterial;

	// 运行时状态
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GroundMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GrassMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RabbitMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FoxMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Rabbit2DMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Fox2DMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HighlightMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WetMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SkyDomeMID;

	bool bGroundBuilt = false;
	bool bUsing2D = false;
	bool bAssetsResolved = false;

	/** 本帧希望高亮的格子（由 PlayerController 每帧喂进来）。 */
	TArray<int32> HighlightCells;
	/** 上一次真正写进组件的格子，用来判断"要不要重写实例"。 */
	TArray<int32> AppliedHighlightCells;
	UPROPERTY(Transient) TArray<FTransform> HighlightPool;

	float LastGrassRebuildTime = -1000.f;

	/**
	 * 固定实例池。
	 * 每次重建都 ClearInstances()+AddInstances() 会强制重建整个渲染代理，
	 * 画面就会闪。改成一次性铺满池子、之后只批量更新变换，代理不再变动。
	 */
	UPROPERTY(Transient) TArray<FTransform> GrassPool;
	UPROPERTY(Transient) TArray<FTransform> RabbitPool;
	UPROPERTY(Transient) TArray<FTransform> FoxPool;
	UPROPERTY(Transient) TArray<FTransform> BillboardRabbitPool;
	UPROPERTY(Transient) TArray<FTransform> BillboardFoxPool;

	/** 复用的变换数组，避免每帧分配。 */
	TArray<FTransform> ScratchTransforms;

	/** 把池子调整到指定大小（只在需要扩容时真正重建）。 */
	static void EnsurePoolSize(UInstancedStaticMeshComponent* Comp, TArray<FTransform>& Pool, int32 Desired);

	/** 草的视觉高度分档数：把 0-255 压成几档，只有跨档才重写实例。 */
	static constexpr int32 GrassLevels = 16;

	/** 草量 -> 视觉档位（0 = 没有草）。 */
	static uint8 GrassVisualLevel(uint8 Grass);

	/** 按档位算出某一格的实例变换。 */
	FTransform MakeGrassTransform(int32 CellIdx, uint8 Level) const;

	/** 上一次写进 GrassISM 的档位，用来做增量更新。 */
	UPROPERTY(Transient) TArray<uint8> GrassVisual;

	/**
	 * 湿地块的实例池。和草一样：**固定铺满 4096 格**、只做增量更新，
	 * 干/湿两态用"缩放 0"表示隐藏。这样浇水时不会有任何重建、当帧就变。
	 */
	UPROPERTY(Transient) TArray<FTransform> WetPool;
	UPROPERTY(Transient) TArray<uint8> WetVisual;

	/** 按"湿 / 不湿"算出某一格的实例变换。 */
	FTransform MakeWetTransform(int32 CellIdx, bool bWet) const;
};
