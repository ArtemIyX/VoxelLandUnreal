#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Generation/Meshing/Lighting/VoxelLandTerrainAmbientOcclusion.h"
#include "Generation/Meshing/VoxelLandMaterialMeshing.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Rendering/Components/VoxelLandChunkComponent.h"
#include "Rendering/Materials/VoxelLandTerrainMaterial.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkyLightComponent.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "TextureCompiler.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

class FVoxelLandOutdoorLightingFixture final
{
public:
	explicit FVoxelLandOutdoorLightingFixture(FAutomationTestBase& InTest)
		: Test(InTest)
		, Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false))
		, Folder(FPaths::ProjectSavedDir() / TEXT("Automation/VoxelLand/OutdoorLighting"))
	{
	}

	bool Build(const UVoxelLandMaterialPalette& Palette)
	{
		const IConsoleVariable* VirtualShadows =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
		if (!Test.TestTrue(TEXT("Virtual shadow maps enabled"), VirtualShadows && VirtualShadows->GetInt() == 1))
		{
			return false;
		}

		const IConsoleVariable* ShadowQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShadowQuality"));
		if (!Test.TestTrue(TEXT("Dynamic shadow quality enabled"), ShadowQuality && ShadowQuality->GetInt() > 0))
		{
			return false;
		}
		Record(FString::Printf(TEXT("Virtual shadows enabled, r.ShadowQuality = %d."), ShadowQuality->GetInt()));

		UMaterial* Parent = Palette.MasterMaterial->GetMaterial();
		Parent->CacheShaders(EMaterialShaderPrecompileMode::Default);
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		const FMaterialResource* Resource = Parent->GetMaterialResource(GMaxRHIShaderPlatform);
		if (!Test.TestTrue(TEXT("Terrain master shader map is complete"),
				Resource && Resource->IsGameThreadShaderMapComplete() && Resource->GetCompileErrors().IsEmpty()))
		{
			return false;
		}

		const TArray<UTexture*> Textures = {
			Palette.BaseColorArray.Get(), Palette.NormalArray.Get(), Palette.ORMHArray.Get()};
		FTextureCompilingManager::Get().FinishCompilation(Textures);
		for (UTexture* Texture : Textures)
		{
			Texture->WaitForPendingInitOrStreaming();
			if (!Test.TestTrue(TEXT("Baked terrain texture array is ready"),
					Texture->GetResource() && !Texture->IsDefaultTexture()))
			{
				return false;
			}
		}
		Material.Reset(FVoxelLandTerrainMaterial::CreateInstance(GetTransientPackage(), Palette));
		if (!Test.TestNotNull(TEXT("Terrain material instance"), Material.Get()))
		{
			return false;
		}

		FVoxelLandMaterialAppearance Appearance;
		Appearance.SlotCount = 2;
		FVoxelLandResolvedMaterialRule& Rule = Appearance.Rules.AddDefaulted_GetRef();
		Rule.Slot = 1;
		Rule.Input = EVoxelLandMaterialRuleInput::ElevationCm;
		Rule.Threshold = 1050.0;
		Rule.BlendWidth = 500.0;
		if (!AddChunk(-1, Appearance) || !AddChunk(0, Appearance))
		{
			return false;
		}

		UTextureCube* SkyCubemap = LoadObject<UTextureCube>(
			nullptr, TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap"));
		if (!Test.TestNotNull(TEXT("Reference sky cubemap"), SkyCubemap))
		{
			return false;
		}
		const TArray<UTexture*> SkyTextures = {SkyCubemap};
		FTextureCompilingManager::Get().FinishCompilation(SkyTextures);
		SkyCubemap->WaitForPendingInitOrStreaming();
		Scene.DirectionalLight->SetMobility(EComponentMobility::Movable);
		Scene.DirectionalLight->SetCastShadows(true);
		Scene.SkyLight->SetMobility(EComponentMobility::Movable);
		Scene.SetSkyCubemap(SkyCubemap);
		Scene.SetSkyBrightness(4.0f);
		Scene.UpdateCaptureContents();

		Target.Reset(NewObject<UTextureRenderTarget2D>());
		Target->ClearColor = FLinearColor::Black;
		Target->InitCustomFormat(Width, Width, PF_B8G8R8A8, true);
		Target->UpdateResourceImmediate(true);
		Capture = NewObject<USceneCaptureComponent2D>();
		Capture->TextureTarget = Target.Get();
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->bAlwaysPersistRenderingState = true;
		Capture->ShowFlags.SetDynamicShadows(true);
		Capture->FOVAngle = 55.0f;
		Capture->OrthoWidth = OrthoWidthCm;
		ConfigureExposure();
		Scene.AddComponent(Capture, CameraPose(0.0));
		return Test.TestTrue(TEXT("Create outdoor capture folder"), IFileManager::Get().MakeDirectory(*Folder, true));
	}

	bool Verify()
	{
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->SetWorldLocationAndRotation(FVector(0.0, BankY, 6500.0), FRotator(-90.0, -90.0, 0.0));
		Capture->CaptureSource = SCS_BaseColor;
		TArray<FColor> Mask;
		TArray<FColor> Reference;
		if (!ReadCapture(TEXT("GeometryAO"), 1.f, 4.f, Mask)
			|| !ReadCapture(TEXT("GeometryAONeutral"), 0.f, 4.f, Reference))
		{
			return false;
		}
		VerifyMask(Mask, Reference);
		if (!ReadCapture(TEXT("CombinedAO"), 1.f, 5.f, Reference) || !VerifyFarMask(Mask))
		{
			return false;
		}

		Capture->CaptureSource = SCS_FinalColorLDR;
		Capture->ProjectionType = ECameraProjectionMode::Perspective;
		Capture->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(OrthoWidthCm * 0.5f / 5650.f));
		Capture->PostProcessSettings.AutoExposureBias = 2.f;
		SetLumen(false, false);
		Scene.SetLightDirection(FRotator(-15.0, -90.0, 0.0));
		Scene.SetLightBrightness(16.0f);
		Scene.SetSkyBrightness(0.0f);
		TArray<FColor> Pixels;
		if (!ReadCapture(TEXT("RasterLowSunOverhead"), 1.f, 0.f, Pixels))
		{
			return false;
		}
		Capture->ShowFlags.SetDynamicShadows(false);
		if (!ReadCapture(TEXT("RasterLowSunOverheadNoShadows"), 1.f, 0.f, Reference))
		{
			return false;
		}
		Capture->ShowFlags.SetDynamicShadows(true);
		Scene.SetSkyBrightness(4.0f);
		const double ShadowedBank = RegionBrightness(Pixels, Width / 2 - 36, Width / 2 + 36, 96, 108);
		const double UnshadowedBank = RegionBrightness(Reference, Width / 2 - 36, Width / 2 + 36, 96, 108);
		int32 MaxDarkening = 0;
		int32 SignificantPixels = 0;
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			const FColor WithShadows = Pixels[Index];
			const FColor WithoutShadows = Reference[Index];
			const int32 Delta =
				(WithoutShadows.R + WithoutShadows.G + WithoutShadows.B - WithShadows.R - WithShadows.G - WithShadows.B)
				/ 3;
			MaxDarkening = FMath::Max(MaxDarkening, Delta);
			SignificantPixels += Delta >= 8;
		}
		Record(FString::Printf(TEXT("Low sun floor at bank, shadowed %.2f and no shadows %.2f (8-bit brightness)."),
			ShadowedBank, UnshadowedBank));
		Record(FString::Printf(TEXT("Shadow pair max darkening %d, pixels with at least 8 levels of darkening %d."),
			MaxDarkening, SignificantPixels));
		Test.TestTrue(
			TEXT("The terrace casts a low-sun shadow onto the lower floor"), ShadowedBank + 0.5 < UnshadowedBank);

		Capture->FOVAngle = 55.0f;
		Capture->PostProcessSettings.AutoExposureBias = 5.f;
		Capture->SetWorldTransform(CameraPose(0.0));
		if (!CapturePair(TEXT("RasterLowSun")) || !CaptureMotion())
		{
			return false;
		}
		Capture->SetWorldTransform(CameraPose(0.0));
		Scene.SetLightDirection(FRotator(-55.0, -45.0, 0.0));
		if (!CapturePair(TEXT("RasterHighSun")))
		{
			return false;
		}
		Scene.SetLightBrightness(0.0f);
		if (!CapturePair(TEXT("RasterSkyOnly")))
		{
			return false;
		}
		Scene.SetLightDirection(FRotator(-15.0, -90.0, 0.0));
		Scene.SetLightBrightness(16.0f);
		SetLumen(true, true);
		if (!CapturePair(TEXT("LumenScreenTracesOn"), 8))
		{
			return false;
		}
		SetLumen(true, false);
		if (!CapturePair(TEXT("LumenScreenTracesOff"), 8))
		{
			return false;
		}
		Test.AddInfo(FString::Printf(TEXT("Outdoor captures saved to %s."), *Folder));
		const FString MetricsPath = Folder / TEXT("Metrics.txt");
		return Test.TestTrue(
			TEXT("Save outdoor capture measurements"), FFileHelper::SaveStringArrayToFile(Metrics, *MetricsPath));
	}

private:
	static constexpr int32 Width = 256;
	static constexpr float VoxelSizeCm = 100.f;
	static constexpr float OrthoWidthCm = 2400.f;
	static constexpr double BankY = 1650.0;
	static constexpr double LowFloorZ = 850.0;
	static constexpr int32 DensityUnitsPerVoxel = 1024;

	static FVoxelLandError SampleTerrace(const FInt64Vector& Cell, int16& Density)
	{
		// The upper half space reaches the lower plane along an exterior vertical bank.
		const int64 Lower = Cell.Z * DensityUnitsPerVoxel - 8 * DensityUnitsPerVoxel - DensityUnitsPerVoxel / 2;
		// Keep the upper surface inside the reference sun's 15-degree shadow footprint.
		const int64 Upper = Lower - 4 * DensityUnitsPerVoxel;
		const int64 Bank = 16 * DensityUnitsPerVoxel + DensityUnitsPerVoxel / 2 - Cell.Y * DensityUnitsPerVoxel;
		const int64 Value = FMath::Min(Lower, FMath::Max(Upper, Bank));
		Density = static_cast<int16>(FMath::Clamp<int64>(Value, MIN_int16, MAX_int16));
		return {};
	}

	static bool NeverCancel() { return false; }

	bool AddChunk(int32 ChunkX, const FVoxelLandMaterialAppearance& Appearance)
	{
		FVoxelLandChunkData Chunk;
		Chunk.Key.Coordinate.X = ChunkX;
		Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
		constexpr int32 Edge = FVoxelLandChunkData::SamplesPerAxis;
		for (int32 Z = 0; Z < Edge; ++Z)
		{
			for (int32 Y = 0; Y < Edge; ++Y)
			{
				for (int32 X = 0; X < Edge; ++X)
				{
					SampleTerrace(FInt64Vector(ChunkX * FVoxelLandChunkData::Cells + X - 1, Y - 1, Z - 1),
						Chunk.Density[X + Edge * (Y + Edge * Z)]);
				}
			}
		}

		const FVector3d Origin = VoxelLandMeshCoordinates::RenderOrigin(Chunk.Key.Coordinate, VoxelSizeCm);
		const TSharedPtr<FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh =
			MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>();
		FVoxelLandMeshingScratch Scratch;
		if (!Test.TestTrue(TEXT("Build and pack true Surface Nets mesh"),
				VoxelLandMeshing::BuildSurfaceNets(Chunk, VoxelSizeCm, Scratch, *Mesh, NeverCancel)
						== EVoxelLandMeshResult::Success
					&& !Mesh->Indices.IsEmpty() && FVoxelLandMaterialMeshing::Pack(*Mesh, Appearance, Origin)))
		{
			return false;
		}

		const double Start = FPlatformTime::Seconds();
		const FVoxelLandError Error =
			FVoxelLandTerrainAmbientOcclusion::Bake(*Mesh, VoxelSizeCm, Origin, SampleTerrace, NeverCancel);
		const double ElapsedMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		if (Error)
		{
			Test.AddError(Error.Message);
			return false;
		}
		if (!Test.TestEqual(
				TEXT("One visibility byte per mesh vertex"), Mesh->AmbientVisibility.Num(), Mesh->Vertices.Num()))
		{
			return false;
		}

		int32 BankCount = 0;
		int32 OpenCount = 0;
		int32 BankTotal = 0;
		int32 OpenTotal = 0;
		for (int32 Index = 0; Index < Mesh->Vertices.Num(); ++Index)
		{
			const FVector3d Position = Origin + FVector3d(Mesh->Vertices[Index].Position);
			if (FMath::Abs(Position.Z - LowFloorZ) > 25.0)
			{
				continue;
			}
			if (FMath::IsWithin(Position.Y, BankY - 260.0, BankY - 40.0))
			{
				++BankCount;
				BankTotal += Mesh->AmbientVisibility[Index];
			}
			if (FMath::IsWithin(Position.Y, BankY - 1250.0, BankY - 900.0))
			{
				++OpenCount;
				OpenTotal += Mesh->AmbientVisibility[Index];
			}
		}
		if (!Test.TestTrue(TEXT("Both lower-floor sample regions contain vertices"), BankCount > 0 && OpenCount > 0))
		{
			return false;
		}
		const double Bank = static_cast<double>(BankTotal) / BankCount;
		const double Open = static_cast<double>(OpenTotal) / OpenCount;
		Test.TestTrue(TEXT("The bank occludes more sky than the open floor"), Bank + 1.0 < Open);
		Record(FString::Printf(
			TEXT(
				"Chunk %d: %d vertices, %d triangles, %llu AO bytes, %.2f ms bake. Bank %.2f, open floor %.2f (of 255)."),
			ChunkX, Mesh->Vertices.Num(), Mesh->Indices.Num() / 3,
			static_cast<uint64>(Mesh->AmbientVisibility.GetAllocatedSize()), ElapsedMs, Bank, Open));

		UVoxelLandChunkComponent* Component = NewObject<UVoxelLandChunkComponent>();
		Component->SetMaterial(0, Material.Get());
		FVoxelLandTerrainMaterial::SetLogicalOrigin(*Component, Origin);
		if (!Test.TestTrue(TEXT("Publish outdoor terrain mesh"), Component->SetMesh(Mesh)))
		{
			return false;
		}
		Scene.AddComponent(Component, FTransform(Origin));
		return true;
	}

	void ConfigureExposure()
	{
		Capture->PostProcessBlendWeight = 1.f;
		FPostProcessSettings& Settings = Capture->PostProcessSettings;
		Settings.bOverride_AutoExposureMethod = true;
		Settings.AutoExposureMethod = AEM_Manual;
		Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Settings.AutoExposureApplyPhysicalCameraExposure = false;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 5.f;
		Settings.bOverride_LocalExposureHighlightContrastScale = true;
		Settings.LocalExposureHighlightContrastScale = 1.f;
		Settings.bOverride_LocalExposureShadowContrastScale = true;
		Settings.LocalExposureShadowContrastScale = 1.f;
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.f;
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = 0.f;
		Settings.bOverride_MotionBlurAmount = true;
		Settings.MotionBlurAmount = 0.f;
		Settings.bOverride_ReflectionMethod = true;
		Settings.ReflectionMethod = EReflectionMethod::None;
		SetLumen(false, false);
	}

	void SetLumen(bool bEnabled, bool bScreenTraces) const
	{
		FPostProcessSettings& Settings = Capture->PostProcessSettings;
		Settings.bOverride_DynamicGlobalIlluminationMethod = true;
		Settings.DynamicGlobalIlluminationMethod =
			bEnabled ? EDynamicGlobalIlluminationMethod::Lumen : EDynamicGlobalIlluminationMethod::None;
		Settings.bOverride_LumenFinalGatherScreenTraces = true;
		Settings.LumenFinalGatherScreenTraces = bScreenTraces;
	}

	static FTransform CameraPose(double X)
	{
		const FVector Location(X, -1550.0, 2850.0);
		const FVector LookAt(X, BankY, 1050.0);
		return FTransform((LookAt - Location).Rotation(), Location);
	}

	bool CaptureMotion()
	{
		TArray<FColor> Pixels;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Capture->SetWorldTransform(CameraPose(-1600.0 + 800.0 * Index));
			const FString Label = FString::Printf(TEXT("RasterMotion%02d"), Index);
			if (!ReadCapture(*Label, 1.f, 0.f, Pixels, 1, Index == 0))
			{
				return false;
			}
		}
		return true;
	}

	bool CapturePair(const TCHAR* Label, int32 Frames = 2)
	{
		TArray<FColor> Off;
		TArray<FColor> On;
		if (!ReadCapture(*(FString(Label) + TEXT("AOOff")), 0.f, 0.f, Off, Frames)
			|| !ReadCapture(*(FString(Label) + TEXT("AOOn")), 1.f, 0.f, On, Frames))
		{
			return false;
		}
		const double OffMean = RegionBrightness(Off, Width / 4, 3 * Width / 4, Width / 2, 3 * Width / 4);
		const double OnMean = RegionBrightness(On, Width / 4, 3 * Width / 4, Width / 2, 3 * Width / 4);
		Record(FString::Printf(
			TEXT("%s: central lower region brightness AO off %.2f and AO on %.2f (8-bit)."), Label, OffMean, OnMean));
		return Test.TestTrue(TEXT("Lit capture contains visible terrain"), OffMean > 0.0 && OnMean > 0.0);
	}

	bool ReadCapture(const TCHAR* Label, float Strength, float DebugMode, TArray<FColor>& Pixels, int32 Frames = 2,
		bool bCameraCut = true)
	{
		Material->SetScalarParameterValue(FVoxelLandTerrainMaterial::AmbientOcclusionStrengthParameter, Strength);
		Material->SetScalarParameterValue(FVoxelLandTerrainMaterial::DebugModeParameter, DebugMode);
		Capture->bCameraCutThisFrame = bCameraCut;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Capture->CaptureScene();
			FlushRenderingCommands();
		}
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(false);
		if (!Test.TestTrue(TEXT("Read outdoor capture from GPU"),
				Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, ReadFlags)
					&& Pixels.Num() == Width * Width))
		{
			return false;
		}
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(
			Width, Width, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		const FString Path = Folder / (FString(Label) + TEXT(".png"));
		return Test.TestTrue(TEXT("Save outdoor capture"), FFileHelper::SaveArrayToFile(Png, *Path));
	}

	static double RegionBrightness(const TArray<FColor>& Pixels, int32 X0, int32 X1, int32 Y0, int32 Y1)
	{
		int32 Sum = 0;
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				const FColor Pixel = Pixels[X + Width * Y];
				Sum += Pixel.R + Pixel.G + Pixel.B;
			}
		}
		return static_cast<double>(Sum) / (3 * (X1 - X0) * (Y1 - Y0));
	}

	bool VerifyFarMask(const TArray<FColor>& Geometry)
	{
		Material->SetScalarParameterValue(FVoxelLandTerrainMaterial::FarDistanceParameter, 1.f);
		TArray<FColor> FarCombined;
		const bool bRead = ReadCapture(TEXT("CombinedAOFar"), 1.f, 5.f, FarCombined);
		Material->SetScalarParameterValue(
			FVoxelLandTerrainMaterial::FarDistanceParameter, FVoxelLandTerrainMaterial::DefaultFarDistanceCm);
		if (!bRead)
		{
			return false;
		}
		int32 LargestDelta = 0;
		for (int32 Index = 0; Index < Geometry.Num(); ++Index)
		{
			LargestDelta = FMath::Max(LargestDelta, FMath::Abs(int32(Geometry[Index].R) - int32(FarCombined[Index].R)));
		}
		return Test.TestTrue(TEXT("Far combined AO matches the geometry mask"), LargestDelta <= 1);
	}

	void VerifyMask(const TArray<FColor>& Mask, const TArray<FColor>& Reference)
	{
		constexpr int32 Left = Width / 2 - 36;
		constexpr int32 Right = Width / 2 + 36;
		const double Bank = RegionBrightness(Mask, Left, Right, 96, 108);
		const double Open = RegionBrightness(Mask, Left, Right, 14, 26);
		const double Flat = RegionBrightness(Reference, Left, Right, 96, 108);
		int32 WorstSeam = 0;
		for (int32 Y = 14; Y < 120; ++Y)
		{
			const int32 Center = Width * Y + Width / 2;
			WorstSeam = FMath::Max(WorstSeam, FMath::Abs(int32(Mask[Center - 1].R) - int32(Mask[Center].R)));
		}
		Record(FString::Printf(TEXT("Geometry mask: bank %.2f, open %.2f, AO-off bank %.2f, seam max %d (8-bit)."),
			Bank, Open, Flat, WorstSeam));
		Test.TestTrue(TEXT("AO-disabled geometry mask stays neutral at the bank"), Flat >= 250.0);
		Test.TestTrue(TEXT("Rendered AO darkens the bank while leaving the open terrace brighter"), Bank + 1.0 < Open);
		Test.TestTrue(TEXT("Neighboring terrain chunks have no geometry-AO seam"), WorstSeam <= 3);
	}

	void Record(const FString& Measurement)
	{
		Test.AddInfo(Measurement);
		Metrics.Add(Measurement);
	}

	FAutomationTestBase& Test;
	FPreviewScene Scene;
	FString Folder;
	TArray<FString> Metrics;
	TStrongObjectPtr<UMaterialInstanceDynamic> Material;
	TStrongObjectPtr<UTextureRenderTarget2D> Target;
	USceneCaptureComponent2D* Capture = nullptr;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandOutdoorLightingCaptureTest, "VoxelLand.Lighting.OutdoorCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandOutdoorLightingCaptureTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender() || GUsingNullRHI || GMaxRHIShaderPlatform != SP_PCD3D_SM6)
	{
		AddWarning(TEXT("Outdoor capture requires D3D12 SM6 and commandlet rendering."));
		return true;
	}
	TStrongObjectPtr<UVoxelLandMaterialPalette> Palette(LoadObject<UVoxelLandMaterialPalette>(
		nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/DA_Palette_MeadowSlate.DA_Palette_MeadowSlate")));
	FText Error;
	if (!TestNotNull(TEXT("Shipped terrain palette"), Palette.Get()) || !Palette->ValidateBake(Error))
	{
		AddError(Error.ToString());
		return false;
	}
	FVoxelLandOutdoorLightingFixture Fixture(*this);
	return Fixture.Build(*Palette) && Fixture.Verify();
}

#endif
