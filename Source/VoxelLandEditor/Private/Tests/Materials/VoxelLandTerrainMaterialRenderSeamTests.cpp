#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Generation/Meshing/VoxelLandMaterialMeshing.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Rendering/Components/VoxelLandChunkComponent.h"
#include "Rendering/Materials/VoxelLandTerrainMaterial.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "TextureCompiler.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandTerrainMaterialRenderFixture final
{
public:
	static constexpr int32 Width = 256;
	static constexpr float VoxelSizeCm = 100.f;
	static constexpr float CaptureWidthCm = 4800.f;
	static constexpr int32 ChunkCells = FVoxelLandChunkData::Cells;
	static constexpr int32 ZAtSeam = 16;
	static constexpr int32 CellsPerRise = 4;
	static constexpr int32 DensityUnitsPerVoxel = 1024;

	explicit FVoxelLandTerrainMaterialRenderFixture(FAutomationTestBase& InTest)
		: Test(InTest)
		, Scene(FPreviewScene::ConstructionValues().AllowAudioPlayback(false))
	{
	}

	bool Build(const UVoxelLandMaterialPalette& Palette)
	{
		UMaterial* Parent = Palette.MasterMaterial->GetMaterial();
		Parent->CacheShaders(EMaterialShaderPrecompileMode::Default);
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		const FMaterialResource* Resource = Parent->GetMaterialResource(GMaxRHIShaderPlatform);
		if (!Test.TestNotNull(TEXT("Master material has a running-platform resource"), Resource))
		{
			return false;
		}
		if (!Resource->IsGameThreadShaderMapComplete() || !Resource->GetCompileErrors().IsEmpty())
		{
			for (const FString& CompileError : Resource->GetCompileErrors())
			{
				Test.AddError(CompileError);
			}
			Test.AddError(TEXT("Terrain master did not compile for the active shader platform."));
			return false;
		}
		const TArray<UTexture*> Arrays = {
			Palette.BaseColorArray.Get(), Palette.NormalArray.Get(), Palette.ORMHArray.Get()};
		FTextureCompilingManager::Get().FinishCompilation(Arrays);
		for (UTexture* Array : Arrays)
		{
			Array->WaitForPendingInitOrStreaming();
			if (!Test.TestTrue(TEXT("Palette array has a finished texture resource"),
					Array->GetResource() && !Array->IsDefaultTexture()))
			{
				return false;
			}
		}
		Material = FVoxelLandTerrainMaterial::CreateInstance(GetTransientPackage(), Palette);
		if (!Test.TestNotNull(TEXT("Shared MID resolves"), Material))
		{
			return false;
		}
		FVoxelLandMaterialAppearance Appearance;
		Appearance.SlotCount = 2;
		FVoxelLandResolvedMaterialRule& Rule = Appearance.Rules.AddDefaulted_GetRef();
		Rule.Slot = 1;
		Rule.Input = EVoxelLandMaterialRuleInput::ElevationCm;
		Rule.Threshold = ZAtSeam * VoxelSizeCm;
		Rule.BlendWidth = 1300.0;
		for (const int32 ChunkX : {-1, 0})
		{
			if (!AddChunk(ChunkX, Appearance))
			{
				return false;
			}
		}
		Target = NewObject<UTextureRenderTarget2D>();
		Target->ClearColor = FLinearColor::Black;
		Target->InitCustomFormat(Width, Width, PF_B8G8R8A8, true);
		Target->UpdateResourceImmediate(true);

		Capture = NewObject<USceneCaptureComponent2D>();
		Capture->TextureTarget = Target;
		Capture->CaptureSource = SCS_BaseColor;
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->OrthoWidth = CaptureWidthCm;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		for (UVoxelLandChunkComponent* Component : Components)
		{
			Capture->ShowOnlyComponent(Component);
		}
		const FVector Camera(0.0, ChunkCells * VoxelSizeCm * 0.5, 6500.0);
		Scene.AddComponent(Capture, FTransform(FRotator(-90.0, -90.0, 0.0), Camera));
		return true;
	}

	bool Read(TArray<FColor>& Pixels, const FVector3d& LogicalShift = FVector3d::ZeroVector)
	{
		for (UVoxelLandChunkComponent* Component : Components)
		{
			const FVector3d Origin = Component->GetComponentLocation() + LogicalShift;
			FVoxelLandTerrainMaterial::SetLogicalOrigin(*Component, Origin);
		}
		Capture->CaptureScene();
		FlushRenderingCommands();
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(false);
		return Test.TestTrue(TEXT("Read terrain base color from the GPU"),
			Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, ReadFlags)
				&& Pixels.Num() == Width * Width);
	}

	void TestImage(const TArray<FColor>& Pixels, const TCHAR* Label)
	{
		constexpr int32 EdgeWidth = 32;
		constexpr int32 Center = Width / 2;
		int32 LeftBalance = 0;
		int32 RightBalance = 0;
		int32 LargestSeamJump = 0;
		int32 Holes = 0;
		for (int32 Y = Center - EdgeWidth; Y < Center + EdgeWidth; ++Y)
		{
			for (int32 X = Center - 1; X <= Center; ++X)
			{
				const FColor Pixel = Pixels[X + Y * Width];
				if (Pixel.R == 0 && Pixel.G == 0 && Pixel.B == 0)
				{
					++Holes;
				}
			}
			const FColor A = Pixels[Center - 1 + Y * Width];
			const FColor B = Pixels[Center + Y * Width];
			LargestSeamJump = FMath::Max(LargestSeamJump, MaxChannelDelta(A, B));
			const FColor Left = Pixels[EdgeWidth + Y * Width];
			const FColor Right = Pixels[Width - EdgeWidth + Y * Width];
			LeftBalance += int32(Left.G) - int32(Left.B);
			RightBalance += int32(Right.G) - int32(Right.B);
		}
		Test.TestEqual(FString::Printf(TEXT("%s seam is covered"), Label), Holes, 0);
		Test.TestTrue(
			FString::Printf(TEXT("%s seam has no abrupt color discontinuity (%d/255)"), Label, LargestSeamJump),
			LargestSeamJump <= 24);
		Test.TestTrue(FString::Printf(TEXT("%s renders distinct meadow and slate slices (%d, %d)"), Label, LeftBalance,
						  RightBalance),
			FMath::Min(LeftBalance, RightBalance) < -64 && FMath::Max(LeftBalance, RightBalance) > 64);
	}

	void TestPhase(const TArray<FColor>& Reference, const TArray<FColor>& Shifted, const TCHAR* Label)
	{
		int64 SumDelta = 0;
		constexpr int32 Min = Width / 4;
		constexpr int32 Max = 3 * Width / 4;
		for (int32 Y = Min; Y < Max; ++Y)
		{
			for (int32 X = Min; X < Max; ++X)
			{
				const int32 Pixel = X + Y * Width;
				SumDelta += MaxChannelDelta(Reference[Pixel], Shifted[Pixel]);
			}
		}
		const double Mean = double(SumDelta) / FMath::Square(Max - Min);
		Test.TestTrue(FString::Printf(TEXT("%s integer-period shift preserves rendered UV phase (mean delta %.3f/255)"),
						  Label, Mean),
			Mean <= 1.5);
	}

	void ForceFarShading() { Material->SetScalarParameterValue(FVoxelLandTerrainMaterial::FarDistanceParameter, 1.f); }

	static bool SavePng(TArray<FColor>& Pixels, const TCHAR* Name)
	{
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/VoxelLand/TerrainMaterials");
		IFileManager::Get().MakeDirectory(*Folder, true);
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(
			Width, Width, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		return FFileHelper::SaveArrayToFile(Png, *(Folder / Name));
	}

private:
	static int32 MaxChannelDelta(const FColor& A, const FColor& B)
	{
		return FMath::Max3(FMath::Abs(int32(A.R) - int32(B.R)), FMath::Abs(int32(A.G) - int32(B.G)),
			FMath::Abs(int32(A.B) - int32(B.B)));
	}

	static bool NeverCancel() { return false; }

	static FVoxelLandChunkData MakeSlopeField(int32 ChunkX)
	{
		FVoxelLandChunkData Chunk;
		Chunk.Key.Coordinate.X = ChunkX;
		Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
		constexpr int32 Samples = FVoxelLandChunkData::SamplesPerAxis;
		for (int32 Z = 0; Z < Samples; ++Z)
		{
			for (int32 Y = 0; Y < Samples; ++Y)
			{
				for (int32 X = 0; X < Samples; ++X)
				{
					const int32 GlobalX = ChunkX * ChunkCells + X - 1;
					const int32 Elevation = (Z - 1 - ZAtSeam) * DensityUnitsPerVoxel;
					const int32 Density = Elevation - GlobalX * (DensityUnitsPerVoxel / CellsPerRise) - 128;
					Chunk.Density[X + Samples * (Y + Samples * Z)] = static_cast<int16>(Density);
				}
			}
		}
		return Chunk;
	}

	bool AddChunk(int32 ChunkX, const FVoxelLandMaterialAppearance& Appearance)
	{
		const FVoxelLandChunkData Chunk = MakeSlopeField(ChunkX);
		FVoxelLandMeshingScratch Scratch;
		TSharedPtr<FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh =
			MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>();
		const FVector3d Origin = VoxelLandMeshCoordinates::RenderOrigin(Chunk.Key.Coordinate, VoxelSizeCm);
		if (VoxelLandMeshing::BuildSurfaceNets(Chunk, VoxelSizeCm, Scratch, *Mesh, NeverCancel)
				!= EVoxelLandMeshResult::Success
			|| Mesh->Indices.IsEmpty() || !FVoxelLandMaterialMeshing::Pack(*Mesh, Appearance, Origin))
		{
			Test.AddError(FString::Printf(TEXT("Could not build a packed mesh for slope chunk %d."), ChunkX));
			return false;
		}
		UVoxelLandChunkComponent* Component = NewObject<UVoxelLandChunkComponent>();
		Component->SetMaterial(0, Material);
		FVoxelLandTerrainMaterial::SetLogicalOrigin(*Component, Origin);
		if (!Test.TestTrue(TEXT("Chunk component accepts packed terrain geometry"), Component->SetMesh(Mesh)))
		{
			return false;
		}
		Scene.AddComponent(Component, FTransform(Origin));
		Components.Add(Component);
		return true;
	}

	FAutomationTestBase& Test;
	FPreviewScene Scene;
	TArray<UVoxelLandChunkComponent*> Components;
	UMaterialInstanceDynamic* Material = nullptr;
	UTextureRenderTarget2D* Target = nullptr;
	USceneCaptureComponent2D* Capture = nullptr;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainMaterialRenderSeamTest, "VoxelLand.Materials.RenderSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandTerrainMaterialRenderSeamTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender() || GUsingNullRHI || GMaxRHIShaderPlatform != SP_PCD3D_SM6)
	{
		AddWarning(TEXT("Rendered terrain seam requires D3D12 SM6 and commandlet rendering."));
		return true;
	}
	TStrongObjectPtr<UVoxelLandMaterialPalette> Palette(LoadObject<UVoxelLandMaterialPalette>(
		nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/DA_Palette_MeadowSlate.DA_Palette_MeadowSlate")));
	FText Error;
	if (!TestNotNull(TEXT("Shipped terrain palette is present"), Palette.Get()) || !Palette->ValidateBake(Error))
	{
		AddError(Error.ToString());
		return false;
	}
	FVoxelLandTerrainMaterialRenderFixture Fixture(*this);
	if (!Fixture.Build(*Palette))
	{
		return false;
	}
	TArray<FColor> Near;
	TArray<FColor> Positive;
	TArray<FColor> Negative;
	const double WrappedDistance = 6.0 * Palette->BakedSlots[0].TilePeriodCm * Palette->BakedSlots[1].TilePeriodCm;
	if (!Fixture.Read(Near) || !Fixture.Read(Positive, FVector3d(WrappedDistance, WrappedDistance, 0.0))
		|| !Fixture.Read(Negative, FVector3d(-WrappedDistance, -WrappedDistance, 0.0)))
	{
		return false;
	}
	Fixture.TestImage(Near, TEXT("Near"));
	Fixture.TestPhase(Near, Positive, TEXT("Positive logical coordinates near 10 km"));
	Fixture.TestPhase(Near, Negative, TEXT("Negative logical coordinates near 10 km"));
	Fixture.ForceFarShading();
	TArray<FColor> Far;
	if (!Fixture.Read(Far))
	{
		return false;
	}
	Fixture.TestImage(Far, TEXT("Far albedo-only"));
	TestTrue(TEXT("Save near capture"), Fixture.SavePng(Near, TEXT("ChunkSeamNear.png")));
	TestTrue(TEXT("Save far capture"), Fixture.SavePng(Far, TEXT("ChunkSeamFar.png")));
	return true;
}

#endif
