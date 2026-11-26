#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Generation/Collision/VoxelLandSdfCollision.h"

class FVoxelLandCapsuleReferenceFixture final
{
public:
	static constexpr double VoxelCm = 100.0;
	static constexpr double IslandX = 500.0;
	static constexpr double IslandZ = 550.0;
	static constexpr double IslandRadius = 275.0;
	static constexpr double ReferenceSpacingCm = 0.0625;

	static FVoxelLandCollisionField MakeField();
	static FVoxelLandCapsuleSweepRequest MakeRequest(double X, double EndZ);
	static TOptional<double> FindWitnessTime(const FVoxelLandCapsuleSweepRequest& Request);

private:
	static int16 Density(int32 X, int32 Z);
	static TOptional<double> FindColumnTop(double X);
};

int16 FVoxelLandCapsuleReferenceFixture::Density(int32 X, int32 Z)
{
	const double Distance = FVector2d(X * VoxelCm - IslandX, Z * VoxelCm - IslandZ).Size() - IslandRadius;
	return static_cast<int16>(FMath::Clamp(
		FMath::RoundToInt(Distance / VoxelCm * 1024.0), static_cast<int32>(MIN_int16), static_cast<int32>(MAX_int16)));
}

FVoxelLandCollisionField FVoxelLandCapsuleReferenceFixture::MakeField()
{
	constexpr int32 Side = FVoxelLandChunkData::SamplesPerAxis;
	const TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk =
		MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key.Coordinate = FIntVector::ZeroValue;
	Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
	for (int32 Z = 0; Z < Side; ++Z)
	{
		for (int32 Y = 0; Y < Side; ++Y)
		{
			for (int32 X = 0; X < Side; ++X)
			{
				Chunk->Density.Add(Density(X - 1, Z - 1));
			}
		}
	}
	FVoxelLandCollisionField::FChunkMap Chunks;
	Chunks.Add(Chunk->Key, Chunk);
	return FVoxelLandCollisionField(VoxelCm, MoveTemp(Chunks));
}

FVoxelLandCapsuleSweepRequest FVoxelLandCapsuleReferenceFixture::MakeRequest(double X, double EndZ)
{
	FVoxelLandCapsuleSweepRequest Result;
	Result.Start = FVector3d(X, VoxelCm * 4.0, IslandZ + IslandRadius + 400.0);
	Result.End = FVector3d(X, Result.Start.Y, EndZ);
	Result.RadiusCm = 37.0;
	Result.HalfHeightCm = 84.0;
	Result.SkinCm = 1.25;
	return Result;
}

TOptional<double> FVoxelLandCapsuleReferenceFixture::FindColumnTop(double X)
{
	const double XInVoxels = X / VoxelCm;
	const int32 Left = FMath::FloorToInt(XInVoxels);
	const double Alpha = XInVoxels - Left;
	double Upper = FMath::Lerp(double(Density(Left, FVoxelLandChunkData::Cells)),
		double(Density(Left + 1, FVoxelLandChunkData::Cells)), Alpha);
	for (int32 Z = FVoxelLandChunkData::Cells - 1; Z >= 0; --Z)
	{
		const double Lower = FMath::Lerp(double(Density(Left, Z)), double(Density(Left + 1, Z)), Alpha);
		if (Lower <= 0.0 && Upper > 0.0)
		{
			return VoxelCm * (Z - Lower / (Upper - Lower));
		}
		Upper = Lower;
	}
	return {};
}

TOptional<double> FVoxelLandCapsuleReferenceFixture::FindWitnessTime(const FVoxelLandCapsuleSweepRequest& Request)
{
	// The fixture is constant in Y. Each sampled X column supplies its exact top
	// crossing. A descending upright capsule touches it with the bottom cap.
	const double Radius = Request.RadiusCm + Request.SkinCm;
	const double SpineHalfLength = Request.HalfHeightCm - Request.RadiusCm;
	const int32 Steps = FMath::CeilToInt(2.0 * Radius / ReferenceSpacingCm);
	TOptional<double> HighestContactCenter;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const double Offset = FMath::Lerp(-Radius, Radius, double(Step) / Steps);
		const TOptional<double> Top = FindColumnTop(Request.Start.X + Offset);
		if (!Top)
		{
			continue;
		}
		const double Cap = FMath::Sqrt(FMath::Max(0.0, Radius * Radius - Offset * Offset));
		const double Center = Top.GetValue() + SpineHalfLength + Cap;
		if (!HighestContactCenter || Center > HighestContactCenter.GetValue())
		{
			HighestContactCenter = Center;
		}
	}
	if (!HighestContactCenter || HighestContactCenter.GetValue() < Request.End.Z)
	{
		return {};
	}
	return (Request.Start.Z - HighestContactCenter.GetValue()) / (Request.Start.Z - Request.End.Z);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCapsuleReferenceTest, "VoxelLand.Collision.Capsule.CurvedReference",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandCapsuleReferenceTest::RunTest(const FString& Parameters)
{
	using FReferenceFixture = FVoxelLandCapsuleReferenceFixture;
	const FVoxelLandCollisionField Field = FReferenceFixture::MakeField();
	int32 VerifiedContacts = 0;
	int32 UnresolvedContacts = 0;
	int32 ClearTrials = 0;
	int32 FalseBlocks = 0;
	int32 LimitedUnresolved = 0;
	int32 LimitedFalseBlocks = 0;
	int32 PeakCells = 0;
	int32 PeakRegions = 0;
	double MaxErrorCm = 0.0;
	constexpr double ReferenceSlackCm = 0.05;
	for (const double Offset : {-270.0, -205.0, -120.0, -40.0, 0.0, 64.0, 146.0, 221.0, 271.0, 340.0, 405.0})
	{
		// The clear trials are far outside even the cylinder's intersecting cells.
		const FVoxelLandCapsuleSweepRequest Request =
			FReferenceFixture::MakeRequest(FReferenceFixture::IslandX + Offset, FReferenceFixture::IslandZ);
		const TOptional<double> WitnessTime = FReferenceFixture::FindWitnessTime(Request);
		const FVoxelLandCapsuleSweepResult Result = FVoxelLandSdfCollision::SweepCapsule(Field, Request);
		FVoxelLandCapsuleSweepRequest LimitedRequest = Request;
		LimitedRequest.SubdivisionDepth = 0;
		const FVoxelLandCapsuleSweepResult Limited = FVoxelLandSdfCollision::SweepCapsule(Field, LimitedRequest);
		PeakCells = FMath::Max(PeakCells, Result.CellsVisited);
		PeakRegions = FMath::Max(PeakRegions, Result.RegionsVisited);
		if (WitnessTime)
		{
			const double Travel = Request.Start.Z - Request.End.Z;
			LimitedUnresolved += Limited.Status == EVoxelLandCapsuleSweepStatus::Indeterminate ? 1 : 0;
			TestTrue(TEXT("Depth-limited query also stops before the reference contact"),
				Limited.Status != EVoxelLandCapsuleSweepStatus::Clear
					&& Limited.LastSafeTime <= WitnessTime.GetValue() + ReferenceSlackCm / Travel);
			TestTrue(FString::Printf(TEXT("Sweep at X offset %.1f blocks before reference contact"), Offset),
				Result.LastSafeTime <= WitnessTime.GetValue() + ReferenceSlackCm / Travel);
			if (Result.Status == EVoxelLandCapsuleSweepStatus::Contact)
			{
				++VerifiedContacts;
				const double ErrorCm = FMath::Abs(Result.Time - WitnessTime.GetValue()) * Travel;
				MaxErrorCm = FMath::Max(MaxErrorCm, ErrorCm);
				TestTrue(TEXT("Contact agrees with the independently sampled cylinder"),
					ErrorCm <= Request.ContactToleranceCm + ReferenceSlackCm);
			}
			else
			{
				++UnresolvedContacts;
				TestEqual(TEXT("An unresolved contact is explicitly blocked"), Result.Status,
					EVoxelLandCapsuleSweepStatus::Indeterminate);
			}
		}
		else
		{
			++ClearTrials;
			FalseBlocks += Result.Status == EVoxelLandCapsuleSweepStatus::Clear ? 0 : 1;
			LimitedFalseBlocks += Limited.Status == EVoxelLandCapsuleSweepStatus::Clear ? 0 : 1;
		}
	}
	TestEqual(TEXT("Every reference contact resolves within the normal work budget"), UnresolvedContacts, 0);
	TestEqual(TEXT("The open ledge controls produce no false blocks"), FalseBlocks, 0);
	TestEqual(TEXT("The depth-limited controls also clear the open ledge"), LimitedFalseBlocks, 0);
	TestTrue(TEXT("The depth-limited pass exercises unresolved contacts"), LimitedUnresolved > 0);
	AddInfo(
		FString::Printf(TEXT("Without subdivision, %d contacts stay unresolved and %d controls are falsely blocked."),
			LimitedUnresolved, LimitedFalseBlocks));
	AddInfo(FString::Printf(
		TEXT(
			"Curved reference at %.4f cm horizontal spacing. %d contacts, %d unresolved, %d clear trials, %d false blocks. Peak %d cells and %d regions. Largest contact error %.4f cm."),
		FReferenceFixture::ReferenceSpacingCm, VerifiedContacts, UnresolvedContacts, ClearTrials, FalseBlocks,
		PeakCells, PeakRegions, MaxErrorCm));
	return true;
}

#endif
