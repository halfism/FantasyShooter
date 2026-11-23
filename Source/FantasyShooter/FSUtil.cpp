#include "FSUtil.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
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

	UStaticMesh* Prop(const TCHAR* Name)
	{
		if (!Name || !*Name)
		{
			return nullptr;
		}
		static TMap<FString, UStaticMesh*> Cache;
		const FString Key(Name);
		if (UStaticMesh** Found = Cache.Find(Key))
		{
			return *Found;
		}

		// "survival_kit/rock_a" -> "/Game/Art/Props/survival_kit/rock_a.rock_a"
		FString Dir, Leaf;
		FString Full(Name);
		if (!Full.Split(TEXT("/"), &Dir, &Leaf, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			Leaf = Full;
		}
		const FString Path = FString::Printf(TEXT("/Game/Art/Props/%s.%s"), *Full, *Leaf);

		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path);
		if (M)
		{
			M->AddToRoot();
		}
		else
		{
			// 道具是纯装饰，缺了不该影响战斗，只留一条日志
			UE_LOG(LogTemp, Warning, TEXT("[FSPROP] missing %s"), *Path);
		}
		Cache.Add(Key, M);
		return M;
	}

	namespace
	{
		/** 当前竞技场主题（0 幽蓝 / 1 霜封 / 2 熔岩），只用来挑石材色。 */
		int32 GArtTheme = 0;

		/**
		 * 按模型名给一个纯色。工程本身是"纯色自发光几何"的风格，Kenney 的 colormap
		 * 贴图在这条无界面导入链路上渲不出来（GPU 侧永远是 32x32 占位），所以改成
		 * 按类别上色：植被绿 / 旗暗红 / 木作棕 / 金属冷灰 / 石作（按主题微调）。
		 * 顺序有讲究：iron_fence 要先命中"金属"，不能先被 fence 的"木"抢走。
		 */
		FLinearColor PropColor(const TCHAR* InName)
		{
			const FString N = FString(InName).ToLower();
			auto Has = [&N](const TCHAR* K) { return N.Contains(K); };

			if (Has(TEXT("iron")) || Has(TEXT("metal")) || Has(TEXT("anvil")) || Has(TEXT("gate")))
			{
				return FLinearColor(0.055f, 0.060f, 0.074f);   // 金属：冷灰
			}
			if (Has(TEXT("tree")) || Has(TEXT("pine")) || Has(TEXT("grass")) || Has(TEXT("hedge"))
				|| Has(TEXT("leaf")) || Has(TEXT("pumpkin")) || Has(TEXT("hay")) || Has(TEXT("bush")))
			{
				return FLinearColor(0.028f, 0.098f, 0.036f);   // 植被：暗绿
			}
			if (Has(TEXT("flag")) || Has(TEXT("banner")) || Has(TEXT("pennant")))
			{
				return FLinearColor(0.120f, 0.026f, 0.023f);   // 旗：暗红
			}
			if (Has(TEXT("box")) || Has(TEXT("barrel")) || Has(TEXT("chest")) || Has(TEXT("plank"))
				|| Has(TEXT("wood")) || Has(TEXT("fence")) || Has(TEXT("bench")) || Has(TEXT("table"))
				|| Has(TEXT("cart")) || Has(TEXT("coffin")) || Has(TEXT("door")) || Has(TEXT("log"))
				|| Has(TEXT("trunk")) || Has(TEXT("bedroll")))
			{
				return FLinearColor(0.068f, 0.039f, 0.020f);   // 木作：暗棕
			}
			// 石作：比地板亮一档，但仍压在"暗色场景里的岩块"这一档上。
			// M_Art 是 Unlit，线性值直接当亮度；实测屏幕曝光约 ×1.35，所以
			// 0.078 线性 ≈ 109 sRGB —— 比地板 (22,27,39) 亮，又不刺眼。
			// 熔岩场景的地面和裂缝是暖橙，冷灰石头会像"贴上去的灰纸"，所以换成暖褐灰。
			switch (GArtTheme)
			{
			case 1:  return FLinearColor(0.082f, 0.090f, 0.104f);   // 霜封：偏冷的浅灰
			case 2:  return FLinearColor(0.112f, 0.072f, 0.048f);   // 熔岩：暖褐灰
			default: return FLinearColor(0.078f, 0.084f, 0.105f);   // 幽蓝：蓝灰
			}
		}
	}

	void SetArtTheme(int32 Theme)
	{
		GArtTheme = FMath::Clamp(Theme, 0, 2);
	}

	UMaterialInterface* ArtMat(const TCHAR* PropName, UObject* Outer, const FLinearColor& Tint)
	{
		if (!PropName || !*PropName)
		{
			return nullptr;
		}
		// "survival_kit/tree" -> 包名 survival_kit
		FString Pack(PropName);
		int32 Slash = INDEX_NONE;
		if (Pack.FindChar(TEXT('/'), Slash))
		{
			Pack.LeftInline(Slash);
		}

		static TMap<FString, UMaterialInterface*> BaseCache;
		UMaterialInterface** FoundBase = BaseCache.Find(Pack);
		if (!FoundBase)
		{
			UMaterialInterface* B = nullptr;
			const FString MatPath = FString::Printf(TEXT("/Game/Art/Materials/M_Art_%s.M_Art_%s"), *Pack, *Pack);
			B = LoadObject<UMaterialInterface>(nullptr, *MatPath);
			if (B)
			{
				B->AddToRoot();
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSPROP] material missing %s"), *MatPath);
			}
			FoundBase = &BaseCache.Add(Pack, B);
		}
		UMaterialInterface* Base = *FoundBase;
		if (!Base)
		{
			return nullptr;
		}

		// 白色 = 用默认类别色；否则用调用方指定的颜色（染色）
		const FLinearColor C = Tint.Equals(FLinearColor::White, 0.001f) ? PropColor(PropName) : Tint;

		// 同色共享一个 MID（既保持 draw call 紧凑，又让每个类别有独立颜色）
		const FString Key = FString::Printf(TEXT("%s_%.3f_%.3f_%.3f"), *Pack, C.R, C.G, C.B);
		static TMap<FString, UMaterialInstanceDynamic*> ColorMids;
		if (UMaterialInstanceDynamic** Hit = ColorMids.Find(Key))
		{
			return *Hit;
		}
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, GetTransientPackage()))
		{
			MID->SetVectorParameterValue(TEXT("Tint"), C);
			MID->AddToRoot();
			ColorMids.Add(Key, MID);
			return MID;
		}
		return Base;
	}

	UStaticMeshComponent* AddProp(
		AActor* Owner, USceneComponent* Parent, const TCHAR* Name,
		const FVector& BaseLoc, float TargetHeight, float YawDeg, const FLinearColor& Tint)
	{
		UStaticMesh* M = Prop(Name);
		if (!M || !Owner)
		{
			return nullptr;
		}

		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
		if (!Comp)
		{
			return nullptr;
		}
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(M);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(false);
		Comp->bReceivesDecals = false;
		Comp->SetupAttachment(Parent);
		Comp->RegisterComponent();
		Comp->SetRelativeLocation(BaseLoc);
		Comp->SetRelativeRotation(FRotator(0.f, YawDeg, 0.f));

		// 枢轴在底面，所以高度就是 Z 包围盒的全高（GetBoundingBox 是 FBox，用 Max-Min）
		float S = 1.f;
		if (TargetHeight > 0.f)
		{
			const FBox Box = M->GetBoundingBox();
			const float H = static_cast<float>(Box.Max.Z - Box.Min.Z);
			S = (H > 1.f) ? (TargetHeight / H) : 1.f;
		}
		Comp->SetRelativeScale3D(FVector(S));

		Owner->AddInstanceComponent(Comp);

		// 材质：默认用按包共享的 MID（同材质可合批）；要染色时再派生一个专属 MID
		if (UMaterialInterface* PackMat = ArtMat(Name, Comp, Tint))
		{
			Comp->SetMaterial(0, PackMat);
		}
		else if (UMaterialInterface* Fallback = M->GetMaterial(0))
		{
			Comp->SetMaterial(0, Fallback);
		}

		UE_LOG(LogTemp, Verbose, TEXT("[FSPROP] add %s loc=(%.0f,%.0f,%.0f) scale=%.2f"),
			Name, BaseLoc.X, BaseLoc.Y, BaseLoc.Z, S);
		return Comp;
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

	FFSRect ShrineRowRect(float ScreenW, float ScreenH, int32 Index)
	{
		// 整套尺寸都以 U 为单位（U = ScreenH/700）：这样 1080p 与 4K 下面板
		// 的"行高 / 行距 / 顶部留白"比例完全一致，不会出现 4K 下挤出屏幕。
		// 172*U 是第一行的顶：上面还要放标题、副标题、余额、累计四行。
		const float U = UIScale(ScreenH);
		FFSRect R;
		R.W = 760.f * U;
		R.H = 66.f * U;

		R.X = ScreenW * 0.5f - R.W * 0.5f;
		R.Y = 172.f * U + Index * (R.H + 10.f * U);
		return R;
	}
}
