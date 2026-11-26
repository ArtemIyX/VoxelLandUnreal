#if WITH_DEV_AUTOMATION_TESTS

#include "../../Materials/VoxelLandMaterialArrayBaker.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "Materials/VoxelLandMaterialPaletteBaker.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Misc/AutomationTest.h"

class FVoxelLandMaterialPaletteFixture final
{
public:
	static constexpr int32 Extent = 4;
	static constexpr uint8 Height = 96;
	static const FColor AlbedoA;
	static const FColor AlbedoB;

	[[nodiscard]] static UTexture2D* MakeSource(
		const FColor Color, const bool bIsSRGB, const int32 SourceExtent = Extent)
	{
		UTexture2D* Texture = NewObject<UTexture2D>();
		Texture->SRGB = bIsSRGB;
		TArray<FColor> Pixels;
		Pixels.Init(Color, SourceExtent * SourceExtent);
		Texture->Source.Init(
			SourceExtent, SourceExtent, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		return Texture;
	}

	[[nodiscard]] static UVoxelLandSurfaceMaterial* MakeSurface(const FColor Color)
	{
		UVoxelLandSurfaceMaterial* Surface = NewObject<UVoxelLandSurfaceMaterial>();
		Surface->BaseColor = MakeSource(Color, true);
		Surface->Normal = MakeSource(FColor(128, 128, 255), false);
		Surface->ORMH = MakeSource(FColor(255, 128, 0, Height), false);
		return Surface;
	}

	[[nodiscard]] static bool SliceStartsWith(UTexture2DArray& Array, const int32 Index, const FColor Color)
	{
		const FTextureSource::FMipLock Mip(FTextureSource::ELockState::ReadOnly, &Array.Source, 0);
		if (!Mip.IsValid())
		{
			return false;
		}
		const FColor* Pixels = static_cast<const FColor*>(Mip.GetRawData());
		return Pixels[Index * Extent * Extent] == Color;
	}
};

const FColor FVoxelLandMaterialPaletteFixture::AlbedoA(255, 0, 0);
const FColor FVoxelLandMaterialPaletteFixture::AlbedoB(0, 255, 0);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaterialPaletteBakeTest, "VoxelLand.Materials.Palette.Bake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaterialPaletteBakeTest::RunTest(const FString&)
{
	UVoxelLandMaterialPalette* Palette = NewObject<UVoxelLandMaterialPalette>();
	UVoxelLandSurfaceMaterial* First =
		FVoxelLandMaterialPaletteFixture::MakeSurface(FVoxelLandMaterialPaletteFixture::AlbedoA);
	UVoxelLandSurfaceMaterial* Second =
		FVoxelLandMaterialPaletteFixture::MakeSurface(FVoxelLandMaterialPaletteFixture::AlbedoB);
	Palette->Surfaces = {First, Second};
	UVoxelLandSurfaceMaterial* Duplicate = DuplicateObject<UVoxelLandSurfaceMaterial>(First, GetTransientPackage());
	TestTrue(
		TEXT("New assets have persistent nonempty IDs"), First->SurfaceId.IsValid() && Second->SurfaceId.IsValid());
	TestNotEqual(TEXT("Duplicate is a new definition"), Duplicate->SurfaceId, First->SurfaceId);

	FText Error;
	if (!TestTrue(TEXT("Two-slot palette bakes"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	TestTrue(TEXT("Published resources validate"), Palette->ValidateBake(Error));
	TestFalse(TEXT("New bake is current"), Palette->IsBakeStale());
	TestEqual(TEXT("Slot zero is the first surface"), Palette->BakedSlots[0].SurfaceId, First->SurfaceId);
	TestTrue(TEXT("Albedo slices follow ordered IDs"),
		FVoxelLandMaterialPaletteFixture::SliceStartsWith(
			*Palette->BaseColorArray, 0, FVoxelLandMaterialPaletteFixture::AlbedoA)
			&& FVoxelLandMaterialPaletteFixture::SliceStartsWith(
				*Palette->BaseColorArray, 1, FVoxelLandMaterialPaletteFixture::AlbedoB));
	TestTrue(TEXT("ORMH contains original linear channels including alpha"),
		FVoxelLandMaterialPaletteFixture::SliceStartsWith(
			*Palette->ORMHArray, 1, FColor(255, 128, 0, FVoxelLandMaterialPaletteFixture::Height)));
	TestEqual(TEXT("Normal array declares normal compression"), Palette->NormalArray->CompressionSettings,
		TEnumAsByte<TextureCompressionSettings>(TC_Normalmap));
	TestTrue(TEXT("Only Base Color is sRGB"),
		Palette->BaseColorArray->SRGB && !Palette->NormalArray->SRGB && !Palette->ORMHArray->SRGB);
	TestTrue(TEXT("ORMH requests retained alpha"),
		Palette->ORMHArray->CompressionForceAlpha && !Palette->ORMHArray->CompressionNoAlpha);
	TestTrue(TEXT("Arrays belong to the palette package"), Palette->ORMHArray->GetOuter() == Palette);

	const FGuid InitialLayout = Palette->LayoutIdentity;
	const FGuid InitialShading = Palette->ShadingIdentity;
	First->TilePeriodCm *= 2.0;
	TestTrue(TEXT("Changing surface metadata requires rebaking"), Palette->IsBakeStale());
	TestTrue(TEXT("Last successful bake remains valid while stale"), Palette->ValidateBake(Error));
	TestTrue(TEXT("Shading-only rebake succeeds"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	TestEqual(TEXT("Shading-only rebake preserves layout ID"), Palette->LayoutIdentity, InitialLayout);
	TestNotEqual(TEXT("Shading-only rebake updates shading ID"), Palette->ShadingIdentity, InitialShading);

	Palette->Surfaces.Swap(0, 1);
	TestTrue(TEXT("Reordering requires rebaking"), Palette->IsBakeStale());
	TestTrue(TEXT("Reordered palette bakes"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	TestEqual(TEXT("Reordered palette changes fallback"), Palette->BakedSlots[0].SurfaceId, Second->SurfaceId);
	TestNotEqual(TEXT("Reordering updates the layout ID"), Palette->LayoutIdentity, InitialLayout);

	const UVoxelLandMaterialSetup* Legacy = NewObject<UVoxelLandMaterialSetup>();
	TestTrue(
		TEXT("Legacy setups opt out of palette path by default"), Legacy->Palette.IsNull() && Legacy->Rules.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaterialPaletteRejectionTest, "VoxelLand.Materials.Palette.Rejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaterialPaletteRejectionTest::RunTest(const FString&)
{
	UVoxelLandMaterialPalette* Palette = NewObject<UVoxelLandMaterialPalette>();
	FText Error;
	TestFalse(TEXT("Empty palette is rejected"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	Palette->Surfaces.Add(nullptr);
	TestFalse(TEXT("Missing fallback is rejected"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	TestTrue(TEXT("Missing fallback diagnostic identifies slot zero"), Error.ToString().Contains(TEXT("slot 0")));

	UVoxelLandSurfaceMaterial* Surface =
		FVoxelLandMaterialPaletteFixture::MakeSurface(FVoxelLandMaterialPaletteFixture::AlbedoA);
	Palette->Surfaces.Reset();
	for (int32 Slot = 0; Slot < UVoxelLandMaterialPalette::MaxSlots; ++Slot)
	{
		Palette->Surfaces.Add(DuplicateObject<UVoxelLandSurfaceMaterial>(Surface, GetTransientPackage()));
	}
	if (!TestTrue(TEXT("A full palette bakes"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	const UTexture2DArray* InitialORMH = Palette->ORMHArray;
	const FGuid InitialLayout = Palette->LayoutIdentity;
	TestEqual(TEXT("Last slot is represented in source array"), Palette->ORMHArray->Source.GetNumSlices(),
		UVoxelLandMaterialPalette::MaxSlots);

	Palette->Surfaces.Add(Surface);
	TestFalse(TEXT("Slot 33 is rejected"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	Palette->Surfaces.Pop();
	UVoxelLandSurfaceMaterial* Last = Palette->Surfaces.Last();
	const FGuid OriginalId = Last->SurfaceId;
	Last->SurfaceId = Palette->Surfaces[0]->SurfaceId;
	TestFalse(TEXT("Repeated surface IDs are rejected"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	Last->SurfaceId = OriginalId;

	UTexture2D* OriginalNormal = Last->Normal;
	Last->Normal = FVoxelLandMaterialPaletteFixture::MakeSource(FColor(128, 128, 255), false, 8);
	TestFalse(TEXT("Mismatched dimensions are rejected"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	TestTrue(TEXT("Incompatible source diagnostics name the channel"), Error.ToString().Contains(TEXT("Normal")));
	Last->Normal = OriginalNormal;

	Last->ORMH = FVoxelLandMaterialPaletteFixture::MakeSource(FColor(255, 128, 0), false);
	TestFalse(
		TEXT("Unconfirmed opaque blend height is rejected"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	TestTrue(TEXT("Missing blend height identifies ORMH"), Error.ToString().Contains(TEXT("ORMH")));
	TestTrue(TEXT("Rejected bake retains the prior resources and layout"),
		Palette->ORMHArray == InitialORMH && Palette->LayoutIdentity == InitialLayout && Palette->ValidateBake(Error));

	Last->bAllowOpaqueBlendHeight = true;
	TestTrue(TEXT("Explicit constant full height is supported"), FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandMaterialPaletteSourceBudgetTest, "VoxelLand.Materials.Palette.SourceBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandMaterialPaletteSourceBudgetTest::RunTest(const FString&)
{
	constexpr int32 MaxSlots = UVoxelLandMaterialPalette::MaxSlots;
	constexpr int32 ArrayCount = 3;
	constexpr int64 ReferenceExtent = 1024;
	constexpr int64 OversizedExtent = 2048;
	constexpr int64 ReferenceBytes = ReferenceExtent * ReferenceExtent * sizeof(FColor);
	constexpr int64 OversizedBytes = OversizedExtent * OversizedExtent * sizeof(FColor);

	int64 UsedBytes = 0;
	for (int32 ArrayIndex = 0; ArrayIndex < ArrayCount; ++ArrayIndex)
	{
		TestTrue(TEXT("All three 32-slot 1024-square arrays fit"),
			FVoxelLandMaterialArrayBaker::TryAddSourceBytes(ReferenceBytes, MaxSlots, UsedBytes));
	}
	TestEqual(TEXT("A full 32-slot 1024-square palette reaches the source cap"), UsedBytes,
		FVoxelLandMaterialArrayBaker::MaxStagedSourceBytes);
	TestFalse(TEXT("A full 32-slot 2048-square array exceeds the cumulative limit"),
		FVoxelLandMaterialArrayBaker::TryAddSourceBytes(OversizedBytes, MaxSlots, UsedBytes));
	UsedBytes = 0;
	TestFalse(TEXT("Rejection also applies before staging the first oversized array"),
		FVoxelLandMaterialArrayBaker::TryAddSourceBytes(OversizedBytes, MaxSlots, UsedBytes));
	TestFalse(TEXT("Overflow-sized input is rejected before multiplication"),
		FVoxelLandMaterialArrayBaker::TryAddSourceBytes(MAX_int64, MaxSlots, UsedBytes));
	TestEqual(TEXT("Rejection does not consume staging budget"), UsedBytes, int64{0});

	constexpr int32 HalfSlots = MaxSlots / 2;
	TestTrue(TEXT("An individual 16-slot 2048-square array fits"),
		FVoxelLandMaterialArrayBaker::TryAddSourceBytes(OversizedBytes, HalfSlots, UsedBytes));
	TestFalse(TEXT("Two individually valid arrays can exceed the combined limit"),
		FVoxelLandMaterialArrayBaker::TryAddSourceBytes(OversizedBytes, HalfSlots, UsedBytes));
	TestEqual(TEXT("Rejection retains the first array's budget"), UsedBytes, OversizedBytes * HalfSlots);
	return true;
}

#endif
