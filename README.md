# EcoBox 生态箱

一个基于 **Unreal Engine 5.8** 的纯 C++ 生态模拟小游戏。64×64 的草场上，你浇水、种草、放兔子、放狐狸，
看着草 → 兔子 → 狐狸这条食物链自己跑起来，同时收菜攒金币解锁升级。

- 引擎版本：**UE 5.8**
- 语言：**纯 C++**（无蓝图业务逻辑，HUD 用 Widget Blueprint 做表现层）
- 许可证：[MIT](LICENSE)
- 平台：Windows（其他平台未验证）

---

## 玩法

| 工具 | 作用 |
| --- | --- |
| 水壶 WaterCan | 按半径给地块浇水，湿润的地块草才会生长 |
| 镰刀 Scythe | 收割成熟的草换金币，按半径一次割一片 |
| 放兔子 / 放狐狸 | 花金币把生物放进指定格子 |

草长到成熟 → 兔子来吃 → 兔子长大繁殖 → 狐狸捕食兔子。金币用来升级水壶半径、镰刀半径、
单次收益倍率、雨水与温度，以及生物价格成长曲线。

时间控制支持暂停 / 加速多档，也有自动降雨和昼夜长度调节。

---

## 核心设计

### 模拟层与渲染层解耦

```
UEcoBoxSimSubsystem (UTickableWorldSubsystem)   ← 模拟：只算数据
        │  GetCells() / GetCreatures()
        ▼
AEcoBoxRenderer (AActor + ISM)                  ← 渲染：只读数据、写实例变换
```

- 模拟以 **0.25 秒固定步长**推进，速度档位只改变「每帧跑几步」，不改变步长，因此高倍速下逻辑完全一致。
- 渲染层每帧从模拟层读快照，把结果写进 `UInstancedStaticMeshComponent` 的实例变换。
- 渲染器 Actor 由 GameMode 在运行时 Spawn，**网格与材质默认值写在 `EcoBoxRenderer.cpp` 的构造函数和 `ResolveMaterials()` 里**，
  改默认外观请改代码而不是细节面板（运行时 Spawn 的 Actor 存不下来）。

### 实例池只扩容不缩容

4096 个格子每帧都可能需要增删实例，反复 `AddInstance` / `RemoveInstance` 会带来严重的重建开销。
本项目采用固定容量实例池：**隐藏对象不删除，只把它挪到原点并缩放为 0**。

> 注意：不要用「挪到 Z = -1000000」这类做法隐藏实例，会撑爆包围盒导致剔除和阴影全部失效。
> 另外这里一律用普通 `UInstancedStaticMeshComponent`，不用 HISM——HISM 的 cluster tree 与逐帧批量更新冲突。

### 参数集中在 Project Settings

所有可调数值（草的生长/枯萎时间、兔子代谢与繁殖、狐狸捕食收益、相机边距、升级成本增长曲线等）
都定义在 `UEcoBoxSettings`，在 **Project Settings → Game → EcoBox 生态箱** 里改。
模拟层每步通过 `EcoParams::S()` 实时读取，改参数不需要重新编译。

> ⚠️ 这些值同时会被 `Config/DefaultGame.ini` 的 `[/Script/EcoBox.EcoBoxSettings]` 段覆盖。
> 如果发现「所有数值都变成了 0 或最小值」这种整齐的异常，先去看那个 ini 段。

---

## 目录结构

```
EcoBox/
├── Source/EcoBox/
│   ├── EcoBoxTypes.h            网格/生物/草的枚举与常量（GridSize=64, StepSeconds=0.25）
│   ├── EcoBoxSettings.h         UDeveloperSettings，所有可调参数
│   ├── EcoBoxSimSubsystem.*     模拟核心：地块、生物、经济、天气
│   ├── EcoBoxRenderer.*         ISM 渲染层 + 实例池
│   ├── EcoBoxGameMode.*         运行时生成渲染器、装载世界
│   ├── EcoBoxPlayerController.* 相机与输入、工具切换
│   ├── EcoBoxCameraPawn.*       上帝视角相机（边缘平移 + 缩放）
│   ├── EcoBoxHUDBase.* / EcoBoxHUDWidget.*  HUD 基类与 C++ 侧数据接口
│   └── EcoBoxSaveGame.*         存档
├── Config/                      引擎/游戏/输入配置
├── Content/
│   ├── EcoBox/                  关卡、材质、HUD 蓝图（WBP_EcoBoxHUD）
│   ├── map/                     start / baseBox 地图
│   ├── 3d/                      兔子、狐狸低模与贴图
│   ├── images/                  图标
│   ├── bgm/ sound/              音乐与音效
└── EcoBox.uproject
```

## 环境要求

- **Unreal Engine 5.8**（`EcoBox.uproject` 里 `EngineAssociation` 为 `5.8`）
- Visual Studio 2022，安装「使用 C++ 的游戏开发」工作负载
- Windows 10/11

## 构建与运行

1. 克隆仓库：

   ```bash
   git clone https://github.com/<your-name>/EcoBox.git
   ```

2. 右键 `EcoBox.uproject` → **Generate Visual Studio project files**
3. 用 VS 打开 `EcoBox.sln`，配置选 **Development Editor / Win64**，生成
4. 双击 `EcoBox.uproject` 打开编辑器，运行 `Content/map/start` 关卡

> 仓库里不包含 `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/` 和 RiderLink 插件，
> 首次打开必须自己生成工程文件并编译一遍，这是正常的。

## 已知限制

- 单格多生物（一格里叠多只）尚未完全支持，当前按单格单生物设计。
- 生物总数上限与网格格数绑定（4096），想突破需要改造生物存储结构。
- 只有 Windows 构建经过验证。
- `Config/DefaultGame.ini` 会覆盖代码默认值，调试参数时容易踩坑（见上文）。

## 第三方资源声明

`Content/3d/` 下的低模兔子、狐狸模型及其 PBR 贴图，`Content/bgm/` 下的背景音乐，
`Content/images/` 下的部分图标，均为**第三方素材**，版权归原作者所有，**不适用本仓库的 MIT 协议**。
本仓库仅出于学习与演示目的收录；如果你要商用或再分发，请自行确认原始授权。

代码部分（`Source/`、`Config/`）以 MIT 协议开源，详见 [LICENSE](LICENSE)。

## 参与贡献

欢迎提 Issue 和 PR。比较大的改动建议先开 Issue 聊一下方向，避免白做。

## License

[MIT](LICENSE) © 2026 Piginn
