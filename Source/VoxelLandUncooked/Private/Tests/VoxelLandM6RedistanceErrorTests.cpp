#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM6RegionFilterTestFixture.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include <cmath>

class FVoxelLandM6RedistanceErrorFixture final
{
public:
	static TPair<bool, FString> Run();

private:
	using FBase = FVoxelLandM6RegionFilterTestFixture;
	using FFixture = FBase::FFixture;
	using FChunkPtr = FBase::FChunkPtr;
	using FGeneratorPtr = FBase::FGeneratorPtr;
	using FGridPtr = FBase::FGridPtr;

	static bool ExactPlane();
	static bool MeasureShape(const FVoxelLandM6Stamp& Stamp, FString& OutMeasurement);
	static double ReferenceDistanceCm(const FVoxelLandM6Stamp& Stamp, int32 X, int32 Y, int32 Z);
};

bool FVoxelLandM6RedistanceErrorFixture::ExactPlane()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	FGeneratorPtr Generator;
	Settings.M6Redistance.bEnabled = true;
	Settings.M6Redistance.BandVoxels = 2;
	FChunkPtr Chunk;
	if (!FFixture::CreateInputs(25, Settings, Grid) || !FBase::CreateFinite(Settings, Grid, {}, false, Generator)
		|| !FBase::Generate(*Generator, {}, Chunk))
	{
		return false;
	}
	for (int32 Z = -1; Z <= FFixture::Cells + 1; ++Z)
	{
		const int32 AnalyticQ = FMath::Clamp(Z * 1024 - 256, int32(MIN_int16), int32(MAX_int16));
		for (int32 Y = -1; Y <= FFixture::Cells + 1; ++Y)
		{
			for (int32 X = -1; X <= FFixture::Cells + 1; ++X)
			{
				if (Chunk->Density[FFixture::SampleIndex(FIntVector(X, Y, Z), FFixture::Samples)] != AnalyticQ)
				{
					return false;
				}
			}
		}
	}
	return true;
}

double FVoxelLandM6RedistanceErrorFixture::ReferenceDistanceCm(const FVoxelLandM6Stamp& Stamp,
	int32 X, int32 Y, int32 Z)
{
	const double DX = FMath::Abs(X * double(FFixture::VoxelSizeCm) - double(Stamp.OriginCm.X));
	const double DY = FMath::Abs(Y * double(FFixture::VoxelSizeCm) - double(Stamp.OriginCm.Y));
	const double DZ = FMath::Abs(Z * double(FFixture::VoxelSizeCm) - double(Stamp.OriginCm.Z));
	if (Stamp.Shape == EVoxelLandM6StampShape::Sphere)
	{
		return std::sqrt(DX * DX + DY * DY + DZ * DZ) - Stamp.RadiusCm;
	}
	const double GX = DX - Stamp.HalfExtentCm.X;
	const double GY = DY - Stamp.HalfExtentCm.Y;
	const double GZ = DZ - Stamp.HalfExtentCm.Z;
	if (GX <= 0.0 && GY <= 0.0 && GZ <= 0.0)
	{
		return FMath::Max3(GX, GY, GZ);
	}
	const double PX = FMath::Max(0.0, GX);
	const double PY = FMath::Max(0.0, GY);
	const double PZ = FMath::Max(0.0, GZ);
	return std::sqrt(PX * PX + PY * PY + PZ * PZ);
}

bool FVoxelLandM6RedistanceErrorFixture::MeasureShape(const FVoxelLandM6Stamp& Stamp, FString& OutMeasurement)
{
	FVoxelLandGeneratorSettings Settings;
	Settings.M6Stamps.Stamps = MakeArrayView(&Stamp, 1);
	FGridPtr Grid;
	FGeneratorPtr Baseline, Redistance;
	if (!FFixture::CreateInputs(-8000, Settings, Grid) || !FBase::CreateFinite(Settings, Grid, {}, false, Baseline))
	{
		return false;
	}
	Settings.M6Redistance.bEnabled = true;
	Settings.M6Redistance.BandVoxels = 2;
	FChunkPtr Before, After;
	if (!FBase::CreateFinite(Settings, Grid, {}, false, Redistance)
		|| !FBase::Generate(*Baseline, {}, Before) || !FBase::Generate(*Redistance, {}, After))
	{
		return false;
	}

	double PeakQ = 0.0;
	double SumSquaredQ = 0.0;
	int32 Count = 0;
	bool bChanged = false;
	for (int32 Z = -1; Z <= FFixture::Cells + 1; ++Z)
	{
		for (int32 Y = -1; Y <= FFixture::Cells + 1; ++Y)
		{
			for (int32 X = -1; X <= FFixture::Cells + 1; ++X)
			{
				const int32 Index = FFixture::SampleIndex(FIntVector(X, Y, Z), FFixture::Samples);
				const int16 Original = Before->Density[Index];
				const int16 Result = After->Density[Index];
				if ((Original < 0) != (Result < 0) || (Original == 0) != (Result == 0))
				{
					return false;
				}
				const double ReferenceCm = ReferenceDistanceCm(Stamp, X, Y, Z);
				if (FMath::Abs(ReferenceCm) <= FFixture::VoxelSizeCm)
				{
					const double DeltaQ = FMath::Abs(Result - ReferenceCm * 1024.0 / FFixture::VoxelSizeCm);
					PeakQ = FMath::Max(PeakQ, DeltaQ);
					SumSquaredQ += DeltaQ * DeltaQ;
					++Count;
					bChanged |= Original != Result;
				}
			}
		}
	}
	if (!bChanged || Count == 0)
	{
		return false;
	}
	OutMeasurement = FString::Printf(TEXT("%s: %d near-surface samples, peak %.3f voxels, RMS %.3f voxels"),
		Stamp.Shape == EVoxelLandM6StampShape::Sphere ? TEXT("Sphere") : TEXT("Box"),
		Count, PeakQ / 1024.0, std::sqrt(SumSquaredQ / Count) / 1024.0);
	// The reconstructed edge points sample the surface. This is a one-voxel
	// tolerance on these reference shapes, with no collision-distance claim.
	return PeakQ <= 1024.0;
}

TPair<bool, FString> FVoxelLandM6RedistanceErrorFixture::Run()
{
	if (!ExactPlane())
	{
		return {false, TEXT("Quarter-voxel plane must have zero density error")};
	}
	FVoxelLandM6Stamp Sphere;
	Sphere.Id = FGuid(0x4d36, 4, 0, 1);
	Sphere.OriginCm = FInt64Vector(1517, 1582, 1609);
	Sphere.RadiusCm = 375;
	FVoxelLandM6Stamp Box;
	Box.Id = FGuid(0x4d36, 4, 0, 2);
	Box.Shape = EVoxelLandM6StampShape::Box;
	Box.OriginCm = FInt64Vector(1553, 1629, 1491);
	Box.HalfExtentCm = FIntVector(382, 431, 357);
	FString SphereResult, BoxResult;
	const bool bSphere = MeasureShape(Sphere, SphereResult);
	const bool bBox = MeasureShape(Box, BoxResult);
	return {bSphere && bBox, FString::Printf(TEXT("Plane: zero error. %s. %s."), *SphereResult, *BoxResult)};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6RedistanceErrorTest, "VoxelLand.M6.Redistance.AnalyticError",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6RedistanceErrorTest::RunTest(const FString& Parameters)
{
	const TPair<bool, FString> Result = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6RedistanceErrorFixture::Run).Get();
	AddInfo(Result.Value);
	return TestTrue(TEXT("Sphere, box and plane reference error is within the fixture tolerance"), Result.Key);
}
#endif
