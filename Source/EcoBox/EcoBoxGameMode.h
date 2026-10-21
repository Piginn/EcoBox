// EcoBox - 游戏模式：把相机、控制器、面板串起来
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EcoBoxGameMode.generated.h"

/**
 * 默认 Pawn 用上帝视角相机，控制器用我们自己的。
 * 关卡里没有 PlayerStart 时会自动在原点造一个，这样打开关卡就能直接跑。
 */
UCLASS()
class ECOBOX_API AEcoBoxGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEcoBoxGameMode();

	virtual void BeginPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/**
	 * 分辨率自适应：把窗口设成当前显示器的原生分辨率 + 无边框全屏。
	 *
	 * 每次启动都检查一遍（已经一致就跳过），所以换显示器 / 换机器都不用改配置。
	 */
	void ApplyAdaptiveResolution();

	// 开局投放数量不在这里 —— 已经挪到
	// 项目设置 → Game → EcoBox 生态箱 里的「开局兔子数 / 开局狐狸数」，
	// 那样改完立刻生效、也不用重新编译。
};
