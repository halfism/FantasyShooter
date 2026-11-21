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
}

/** 中/英两套格式化字符串，按字体可用性自动选择 */
#define FS_FMT(CN, EN, ...) (FSUtil::bChinese() ? FString::Printf(CN, __VA_ARGS__) : FString::Printf(EN, __VA_ARGS__))
