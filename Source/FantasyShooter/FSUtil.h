#pragma once

#include "CoreMinimal.h"
#include "FSCommon.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class UFont;
class USceneComponent;

/** 屏幕矩形（像素），菜单绘制与命中检测共用 */
struct FFSRect
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;

	bool Contains(float PX, float PY) const
	{
		return PX >= X && PX <= X + W && PY >= Y && PY <= Y + H;
	}
};

namespace FSUtil
{
	/** 取引擎自带的基础几何体: Sphere / Cube / Cylinder / Cone / Plane */
	UStaticMesh* Mesh(const TCHAR* Name);

	/**
	 * 取 /Game/Art/Props 下的外部道具网格。Name 形如 "survival_kit/rock_a"。
	 * 这些是 kenney.nl 的 CC0 低模（Tools/fetch_art.py 下载、import_props.py 导入），
	 * 与基础体有两点关键差别：
	 *   1. **枢轴在底面中心**（不是几何中心）——所以摆放时传的就是"落地位置"；
	 *   2. 各单位原始高度差异很大（草 12cm / 房屋 311cm），所以摆放一律按目标高度缩放。
	 * 加载失败返回 nullptr（打包漏拷时装饰消失，但不会崩）。
	 */
	UStaticMesh* Prop(const TCHAR* Name);

	/**
	 * 取道具对应的材质。Tint 为白时返回按包共享的 MID（同材质可合批）；
	 * 需要染色时基于基础材质派生一个新 MID（注意：MID 不能当 MID 的父级）。
	 */
	UMaterialInterface* ArtMat(const TCHAR* PropName, UObject* Outer, const FLinearColor& Tint);

	/**
	 * 告诉道具配色当前是哪个竞技场主题（0 幽蓝 / 1 霜封 / 2 熔岩）。
	 * 只影响石材的基础色：熔岩场景是暖橙调，冷灰石块会像贴上去的灰纸。
	 */
	void SetArtTheme(int32 Theme);

	/** 摆一个外部道具。TargetHeight > 0 时按高度等比缩放，<= 0 用原尺寸 */
	UStaticMeshComponent* AddProp(
		AActor* Owner, USceneComponent* Parent, const TCHAR* Name,
		const FVector& BaseLoc, float TargetHeight, float YawDeg = 0.f,
		const FLinearColor& Tint = FLinearColor::White);

	UMaterialInterface* MatBase();
	UMaterialInterface* MatGlow();

	/** 按颜色生成动态材质。bGlow=true 时用无光照自发光材质 */
	UMaterialInstanceDynamic* MakeMID(UObject* Outer, FLinearColor Color, bool bGlow, float GlowIntensity = 4.f);
	/** 可淡出的半透明自发光材质（特效专用） */
	UMaterialInstanceDynamic* MakeMIDFx(UObject* Outer, FLinearColor Color, float Intensity = 2.f);

	/** 新建一个静态网格组件并挂到 Parent 下 */
	UStaticMeshComponent* AddShape(
		AActor* Owner, USceneComponent* Parent, const TCHAR* MeshName,
		const FVector& RelLoc, const FVector& Scale, const FLinearColor& Color,
		bool bGlow = false, float GlowIntensity = 4.f,
		const FRotator& RelRot = FRotator::ZeroRotator);

	void SetShapeColor(UStaticMeshComponent* Comp, FLinearColor Color, float GlowIntensity = 4.f);

	/** 细节贴图（/Game/Textures/T_DetailGrid）。没有就用 nullptr，一切照旧 */
	UTexture2D* DetailTex();

	/**
	 * 给组件套上细节贴图。Tiling <= 0 时什么都不做，所以旧调用点完全不受影响。
	 * AddScale 走 M_Glow 的加色通道（黑底场景里乘算等于无效，只有加色看得见），
	 * 对 M_Base 这类漫反射材质是乘算，AddScale 会被忽略。
	 */
	void ApplyDetail(UStaticMeshComponent* Comp, float Tiling = 8.f, float AddScale = 0.05f);

	/** 中文（黑体）运行时字体，失败返回 nullptr */
	UFont* CJKFont();
	UFont* UIFont();

	/** 中英文切换（字体可用则用中文） */
	bool bChinese();
	/** 语言偏好：0 自动（看字体）1 中文 2 英文 */
	int32 GetLanguageMode();
	void SetLanguageMode(int32 Mode);
	const TCHAR* L(const TCHAR* CN, const TCHAR* EN);

	FLinearColor Lerp(const FLinearColor& A, const FLinearColor& B, float Alpha);
	void DebugSphere(const UWorld* World, const FVector& Center, float Radius, const FLinearColor& Color, float Life = 1.f);

	/** 全局 UI 缩放（与 HUD 保持一致） */
	float UIScale(float ScreenH);

	/** 菜单项矩形：HUD 绘制与鼠标点击命中共用同一套坐标，避免错位 */
	FFSRect MenuItemRect(float ScreenW, float ScreenH, int32 Index, int32 Count);

	/** 赐福卡片矩形：同上，绘制与点击命中必须走同一份计算 */
	FFSRect BlessingCardRect(float ScreenW, float ScreenH, float U, int32 Index, int32 Count);

	/** 神龛面板里第 Index 行的矩形（5 项固定，纵向列表） */
	FFSRect ShrineRowRect(float ScreenW, float ScreenH, int32 Index);
}

/** 中/英两套格式化字符串，按字体可用性自动选择 */
#define FS_FMT(CN, EN, ...) (FSUtil::bChinese() ? FString::Printf(CN, __VA_ARGS__) : FString::Printf(EN, __VA_ARGS__))
