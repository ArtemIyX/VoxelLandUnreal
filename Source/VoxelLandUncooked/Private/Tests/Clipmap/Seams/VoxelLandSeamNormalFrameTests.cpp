#if WITH_DEV_AUTOMATION_TESTS
#include "../Planning/VoxelLandPackedFrameTest.h"

#include "Data/Clipmap/Seams/VoxelLandPairedSeamNormalFrame.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Generation/Clipmap/Seams/VoxelLandSeamNormalFrames.h"
#include "Misc/AutomationTest.h"
#include <cmath>

class FVoxelLandM7NormalFrameFixture final
{
public:
	static bool CheckReference();
	static bool CheckFallbacks();
	static bool CheckPairApplication();

private:
	using FFrames = FVoxelLandM7NormalFrames;
	static int32 Index(int32 X, int32 Y, int32 Z)
	{
		constexpr int32 Axis = FFrames::StencilAxis;
		return (X + 1) + Axis * ((Y + 1) + Axis * (Z + 1));
	}
	static bool IsOutward(const FVoxelLandM7NormalFrame& Frame, const FVector3f& Expected, float MinDot)
	{
		return !Frame.bFallback && FVector3f::DotProduct(Frame.Normal, Expected) >= MinDot
			&& FMath::Abs(FVector3f::DotProduct(Frame.Normal, Frame.Tangent)) < 1.e-4f;
	}
};

bool FVoxelLandM7NormalFrameFixture::CheckReference()
{
	FVoxelLandM7NormalAnchor Anchor;
	// This negative position is within half a Q16 unit of the next base cell.
	if (!FFrames::MakeAnchor(FVector(-100.0001, 25.0, 75.0), 100, Anchor) || Anchor.BaseCell != FInt64Vector(-1, 0, 0)
		|| Anchor.FractionQ16 != FIntVector(0, 16384, 49152))
	{
		return false;
	}
	Anchor.FractionQ16.X = 16384;
	TArray<int16> Stencil;
	Stencil.SetNumUninitialized(FFrames::StencilSamples);
	for (int32 Z = -1; Z <= 2; ++Z)
	{
		for (int32 Y = -1; Y <= 2; ++Y)
		{
			for (int32 X = -1; X <= 2; ++X)
			{
				Stencil[Index(X, Y, Z)] = int16(100 * X * X + 10 * Y + 5 * Z);
			}
		}
	}

	// Raw central derivatives at the two X corners are 0 and 400.
	// Interpolation at X=1/4 therefore gives (100,20,10) before normalization.
	FVoxelLandM7NormalFrame Gradient;
	if (!FFrames::Evaluate(Stencil, Anchor, Gradient)
		|| !IsOutward(Gradient, FVector3f(100, 20, 10).GetSafeNormal(), 0.99999f))
	{
		return false;
	}

	Anchor.BaseCell = FInt64Vector(-4, 3, 2);
	Anchor.FractionQ16 = FIntVector(16384, 32768, 49152);
	const FVector Center(-0.2, 0.4, 0.1);
	for (int32 Z = -1; Z <= 2; ++Z)
	{
		for (int32 Y = -1; Y <= 2; ++Y)
		{
			for (int32 X = -1; X <= 2; ++X)
			{
				const FVector Lattice = FVector(-4 + X, 3 + Y, 2 + Z);
				Stencil[Index(X, Y, Z)] = int16(std::round(((Lattice - Center).Size() - 5.4) * 1024));
			}
		}
	}
	const FVector SpherePoint(-3.75, 3.5, 2.75);
	const FVector3f Analytic((SpherePoint - Center).GetSafeNormal());
	return FFrames::Evaluate(Stencil, Anchor, Gradient) && IsOutward(Gradient, Analytic, 0.995f);
}

bool FVoxelLandM7NormalFrameFixture::CheckFallbacks()
{
	const FVoxelLandM7NormalAnchor Anchor;
	TArray<int16> Stencil;
	Stencil.Init(MIN_int16, FFrames::StencilSamples);
	FVoxelLandM7NormalFrame Frame;
	if (!FFrames::Evaluate(Stencil, Anchor, Frame) || !Frame.bFallback || Frame.Normal != FVector3f::UpVector
		|| Frame.Tangent != -FVector3f::RightVector)
	{
		return false;
	}
	for (int32 Z = -1; Z <= 2; ++Z)
	{
		for (int32 Y = -1; Y <= 2; ++Y)
		{
			for (int32 X = -1; X <= 2; ++X)
			{
				// Opposite neighbors are equal, so all central differences vanish.
				// The first X edge still crosses from air (zero) to solid.
				Stencil[Index(X, Y, Z)] = X % 2 == 0 ? 0 : -1024;
			}
		}
	}
	if (!FFrames::Evaluate(Stencil, Anchor, Frame) || !Frame.bFallback || Frame.Normal != -FVector3f::ForwardVector)
	{
		return false;
	}
	const FVoxelLandM7NormalFrame Prior = Frame;
	Stencil.Pop();
	return !FFrames::Evaluate(Stencil, Anchor, Frame)
		&& FVoxelLandM7PackedFrameTest::Matches(Frame.Normal, Frame.Tangent, Prior.Normal, Prior.Tangent);
}

bool FVoxelLandM7NormalFrameFixture::CheckPairApplication()
{
	FVoxelLandMeshData Fine, Coarse;
	Fine.Vertices.SetNum(2);
	Coarse.Vertices.SetNum(2);
	Fine.Indices = {0, 1, 0};
	Coarse.Indices = {1, 0, 1};
	Fine.Vertices[0].Position = FVector3f(10, 20, 30);
	Coarse.Vertices[1].Position = FVector3f(-10, 2, 6);
	Fine.Vertices[0].UV = FVector2f(1, 2);
	Coarse.Vertices[1].Color = FColor::Blue;
	const FVoxelLandM7NormalFrame Interior;
	FVoxelLandM7NormalFrame Shared;
	Shared.Normal = FVector3f(-1.f, -2.f, 5.f).GetSafeNormal();
	Shared.Tangent = FFrames::TangentForNormal(Shared.Normal);

	FVoxelLandM7PairedNormalFrame Pair;
	Pair.FirstVertex = 0;
	Pair.SecondVertex = 1;
	Pair.Frame = Shared;
	FVoxelLandM7PairedNormalFrame Conflict = Pair;
	Conflict.SecondVertex = 0;
	Conflict.Frame = Interior;
	TArray<FVoxelLandM7PairedNormalFrame> Pairs = {Pair, Conflict};
	if (FFrames::ApplyPairedFrames(Pairs, Fine, Coarse) || Fine.Vertices[0].Normal != FVector3f::UpVector
		|| Coarse.Vertices[1].Normal != FVector3f::UpVector)
	{
		return false;
	}
	Pairs[1] = Pair;
	if (!FFrames::ApplyPairedFrames(Pairs, Fine, Coarse)
		|| !FVoxelLandM7PackedFrameTest::Matches(
			Fine.Vertices[0].Normal, Fine.Vertices[0].Tangent, Coarse.Vertices[1].Normal, Coarse.Vertices[1].Tangent)
		|| Fine.Vertices[0].Position != FVector3f(10, 20, 30) || Coarse.Vertices[1].Position != FVector3f(-10, 2, 6)
		|| Fine.Vertices[0].UV != FVector2f(1, 2) || Coarse.Vertices[1].Color != FColor::Blue || Fine.Indices[1] != 1
		|| Coarse.Indices[1] != 0)
	{
		return false;
	}
	const FVoxelLandM7NormalFrame AtRim = FFrames::BlendFromInterior(Interior, Shared, MAX_uint16);
	const FVoxelLandM7NormalFrame InBand = FFrames::BlendFromInterior(Interior, Shared, 32768);
	return FVoxelLandM7PackedFrameTest::Matches(AtRim.Normal, AtRim.Tangent, Shared.Normal, Shared.Tangent)
		&& InBand.Normal != Interior.Normal && InBand.Normal != Shared.Normal
		&& FMath::Abs(FVector3f::DotProduct(InBand.Normal, InBand.Tangent)) < 1.e-4f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7NormalFrameTest, "VoxelLand.M7.Normals.ReferenceAndPairs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM7NormalFrameTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Canonical rounding, unnormalized derivatives and analytic sphere orientation"),
		FVoxelLandM7NormalFrameFixture::CheckReference());
	TestTrue(TEXT("Ordered crossing and no-crossing fallbacks preserve output on rejection"),
		FVoxelLandM7NormalFrameFixture::CheckFallbacks());
	TestTrue(TEXT("Both terrain streams keep geometry and consume identical packed rim frames"),
		FVoxelLandM7NormalFrameFixture::CheckPairApplication());
	return true;
}
#endif
