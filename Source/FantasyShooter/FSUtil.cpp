#include "FSUtil.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace FSUtil
{
	UStaticMesh* Mesh(const TCHAR* Name)
	{
		static TMap<FString, UStaticMesh*> Cache;
		const FString Key(Name);
		if (UStaticMesh** Found = Cache.Find(Key))
		{
			return *Found;
		}

		// 自有低模资产（ArtSource/Meshes/*.obj -> /Game/Art/Meshes，由 Tools/import_art.py 导入）。
		// 前四个是 BasicShapes 的对位替换：尺寸与枢轴逐位对齐，所以换了不影响任何摆放。
		// 后两个是"纯新增"部件，引擎里没有对位物，回退名写在 FallbackMap 里。
		static const TMap<FString, FString> ArtMap = {
			{ TEXT("Cube"),     TEXT("/Game/Art/Meshes/SM_BevelBox.SM_BevelBox") },
			{ TEXT("Cylinder"), TEXT("/Game/Art/Meshes/SM_Prism24.SM_Prism24") },
			{ TEXT("Sphere"),   TEXT("/Game/Art/Meshes/SM_IcoSphere.SM_IcoSphere") },
			{ TEXT("Cone"),     TEXT("/Game/Art/Meshes/SM_Cone8.SM_Cone8") },
			{ TEXT("Rock"),     TEXT("/Game/Art/Meshes/SM_Rock.SM_Rock") },
			{ TEXT("Shard"),    TEXT("/Game/Art/Meshes/SM_Shard.SM_Shard") },
		};
		static const TMap<FString, FString> FallbackMap = {
			{ TEXT("Rock"),  TEXT("Cube") },
			{ TEXT("Shard"), TEXT("Cone") },
		};

		UStaticMesh* M = nullptr;
		// -fsbasicshapes：强制退回引擎基础体，专供 A/B 截图比对新旧观感
		static const bool bUseArt = !FParse::Param(FCommandLine::Get(), TEXT("fsbasicshapes"));
		if (bUseArt)
		{
			if (const FString* ArtPath = ArtMap.Find(Key))
			{
				M = LoadObject<UStaticMesh>(nullptr, **ArtPath);
			}
		}
		if (!M)
		{
			const FString* FbName = FallbackMap.Find(Key);
			const TCHAR* Fb = FbName ? **FbName : Name;
			const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Fb, Fb);
			M = LoadObject<UStaticMesh>(nullptr, *Path);
		}
		if (M)
		{
			M->AddToRoot();
		}
		Cache.Add(Key, M);
		return M;
	}

	UMaterialInterface* MatBase()
	{
		static UMaterialInterface* Cached = nullptr;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Base.M_Base"));
			if (Cached)
			{
				Cached->AddToRoot();
			}
			if (!Cached)
			{
				Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			}
		}
		return Cached;
	}

	UMaterialInterface* MatGlow()
	{
		static UMaterialInterface* Cached = nullptr;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Glow.M_Glow"));
			if (Cached)
			{
				Cached->AddToRoot();
			}
			if (!Cached)
			{
				Cached = MatBase();
			}
		}
		return Cached;
	}

	UMaterialInstanceDynamic* MakeMID(UObject* Outer, FLinearColor Color, bool bGlow, float GlowIntensity)
	{
		UMaterialInterface* Parent = bGlow ? MatGlow() : MatBase();
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Outer);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		}
		return MID;
	}

	UMaterialInstanceDynamic* MakeMIDFx(UObject* Outer, FLinearColor Color, float Intensity)
	{
		static UMaterialInterface* Cached = nullptr;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Fx.M_Fx"));
			if (Cached)
			{
				Cached->AddToRoot();
			}
		}
		if (!Cached)
		{
			return MakeMID(Outer, Color, true, Intensity);
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Cached, Outer);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
			MID->SetScalarParameterValue(TEXT("Opacity"), 1.f);
		}
		return MID;
	}

	UTexture2D* DetailTex()
	{
		static UTexture2D* Cached = nullptr;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			Cached = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/T_DetailGrid.T_DetailGrid"));
			if (Cached)
			{
				Cached->AddToRoot();
			}
			else
			{
				// 贴图缺失不该让游戏崩，只是没有花纹——所以这里只记一条日志
				UE_LOG(LogTemp, Warning, TEXT("[FSDETAIL] /Game/Textures/T_DetailGrid missing"));
			}
		}
		return Cached;
	}

	void ApplyDetail(UStaticMeshComponent* Comp, float Tiling, float AddScale)
	{
		if (!Comp || Tiling <= 0.f)
		{
			return;
		}
		UTexture2D* Tex = DetailTex();
		if (!Tex)
		{
			return;
		}

		// AddShape 建出来的组件本来就各自持有独立 MID；少数共享材质的构件就地转一次。
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0));
		if (!MID)
		{
			UMaterialInterface* Cur = Comp->GetMaterial(0);
			if (!Cur)
			{
				return;
			}
			MID = UMaterialInstanceDynamic::Create(Cur, Comp);
			if (!MID)
			{
				return;
			}
			Comp->SetMaterial(0, MID);
		}
		MID->SetTextureParameterValue(TEXT("DetailTex"), Tex);
		MID->SetScalarParameterValue(TEXT("Tiling"), Tiling);
		MID->SetScalarParameterValue(TEXT("AddScale"), AddScale);
	}

	UStaticMeshComponent* AddShape(
		AActor* Owner, USceneComponent* Parent, const TCHAR* MeshName,
		const FVector& RelLoc, const FVector& Scale, const FLinearColor& Color,
		bool bGlow, float GlowIntensity, const FRotator& RelRot)
	{
		if (!Owner)
		{
			return nullptr;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
		if (!Comp)
		{
			return nullptr;
		}
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Mesh(MeshName));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(false);
		Comp->bReceivesDecals = false;
		Comp->SetupAttachment(Parent);
		Comp->RegisterComponent();
		Comp->SetRelativeLocation(RelLoc);
		Comp->SetRelativeRotation(RelRot);
		Comp->SetRelativeScale3D(Scale);
		Owner->AddInstanceComponent(Comp);
		if (UMaterialInstanceDynamic* MID = MakeMID(Owner, Color, bGlow, GlowIntensity))
		{
			Comp->SetMaterial(0, MID);
		}
		return Comp;
	}

	void SetShapeColor(UStaticMeshComponent* Comp, FLinearColor Color, float GlowIntensity)
	{
		if (!Comp)
		{
			return;
		}
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0)))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Intensity"), GlowIntensity);
		}
	}

	UFont* CJKFont()
	{
		static bool bTried = false;
		static UFont* Cached = nullptr;
		if (bTried)
		{
			return Cached;
		}
		bTried = true;

		const FString Relative(TEXT("Fonts/simhei.ttf"));
		FString Full = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / Relative);
		if (!FPaths::FileExists(Full))
		{
			// 打包后的可执行目录 / Content / Fonts
			FString ExeDir = FPaths::ConvertRelativePathToFull(FPaths::LaunchDir());
			FString Alt = FPaths::Combine(ExeDir, TEXT("Content"), Relative);
			if (FPaths::FileExists(Alt))
			{
				Full = Alt;
			}
			else
			{
				return nullptr;
			}
		}

		UFont* F = NewObject<UFont>(GetTransientPackage());
		if (!F)
		{
			return nullptr;
		}
		F->FontCacheType = EFontCacheType::Runtime;
		F->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Emplace(
			FName(TEXT("Regular")), Full, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		F->AddToRoot();
		Cached = F;
		return Cached;
	}

	UFont* UIFont()
	{
		if (UFont* F = CJKFont())
		{
			return F;
		}
		static UFont* Cached = nullptr;
		if (!Cached)
		{
			Cached = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
			if (Cached)
			{
				Cached->AddToRoot();
			}
			if (!Cached && GEngine)
			{
				Cached = GEngine->GetMediumFont();
			}
		}
		return Cached;
	}

	// 0 = 自动（看中文字体是否可用），1 = 强制中文，2 = 强制英文
	static int32 GLanguageMode = 0;

	int32 GetLanguageMode()
	{
		return GLanguageMode;
	}

	void SetLanguageMode(int32 Mode)
	{
		GLanguageMode = FMath::Clamp(Mode, 0, 2);
	}

	bool bChinese()
	{
		if (GLanguageMode == 2)
		{
			return false;
		}
		// 强制中文时若字体缺失也只能回退英文，否则满屏方块
		return CJKFont() != nullptr;
	}

	const TCHAR* L(const TCHAR* CN, const TCHAR* EN)
	{
		return bChinese() ? CN : EN;
	}

	FLinearColor Lerp(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(
			FMath::Lerp(A.R, B.R, Alpha),
			FMath::Lerp(A.G, B.G, Alpha),
			FMath::Lerp(A.B, B.B, Alpha),
			FMath::Lerp(A.A, B.A, Alpha));
	}

	void DebugSphere(const UWorld* World, const FVector& Center, float Radius, const FLinearColor& Color, float Life)
	{
		if (World)
		{
			DrawDebugSphere(World, Center, Radius, 12, Color.ToFColor(true), false, Life, 0, 1.5f);
		}
	}

	float UIScale(float ScreenH)
	{
		return FMath::Clamp(ScreenH / 700.f, 0.75f, 3.6f);
	}

	FFSRect MenuItemRect(float ScreenW, float ScreenH, int32 Index, int32 Count)
	{
		const float U = UIScale(ScreenH);
		FFSRect R;
		R.W = 380.f * U;
		R.H = 46.f * U;

		const float Step = 54.f * U;
		const float TotalH = (Count > 0) ? (Count * Step) : 0.f;
		// 列表整体垂直居中略偏上，给标题留位置
		const float Top = ScreenH * 0.5f - TotalH * 0.5f + 34.f * U;

		R.X = ScreenW * 0.5f - R.W * 0.5f;
		R.Y = Top + Index * Step;
		return R;
	}

	FFSRect BlessingCardRect(float ScreenW, float ScreenH, float U, int32 Index, int32 Count)
	{
		FFSRect R;
		R.W = 268.f * U;
		R.H = 336.f * U;

		const float Gap = 26.f * U;
		const float TotalW = Count * R.W + FMath::Max(0, Count - 1) * Gap;
		const float Left = ScreenW * 0.5f - TotalW * 0.5f;

		// 竖直中心略低于屏幕中线：上方要留出标题与提示，下方留出超时条
		R.X = Left + Index * (R.W + Gap);
		R.Y = ScreenH * 0.56f - R.H * 0.5f;
		return R;
	}
}
