#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandDirectDensityBoundsTest, "VoxelLand.WorldConfig.DirectDensityBounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandDirectDensityBoundsTest::RunTest(const FString& Parameters)
{
	struct FResults
	{
		FVoxelLandError Error;
		bool bBoundary = false;
		bool bSharedHalo = false;
		bool bOutsideRejected = false;
		bool bDeterministic = false;
		bool bIdentityDistinguishesExtent = false;
	};
	const FResults Results = Async(EAsyncExecution::ThreadPool, [] {
		FResults Result;
		FVoxelLandProgramDraft Draft;
		Draft.FormatVersion = 2;
		Draft.RegisterCount = 1;
		Draft.Constants = {-100.f};
		FVoxelLandInstruction Load;
		Load.Op = EVoxelLandOpcode::LoadConstant;
		FVoxelLandInstruction Store;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};

		FVoxelLandGeneratorSettings Settings;
		if ((Result.Error = FVoxelLandProgram::Validate(Draft, Settings.Program)))
		{
			return Result;
		}
		Settings.Profile.VoxelSizeCm = 100.f;
		Settings.DirectDensityBounds.Emplace();
		Settings.DirectDensityBounds->MinChunk = FIntVector::ZeroValue;
		Settings.DirectDensityBounds->MaxChunkExclusive = FIntVector(2, 1, 1);
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Bounded;
		if ((Result.Error = FVoxelLandGenerator::Create(Settings, Bounded)))
		{
			return Result;
		}
		Settings.DirectDensityBounds->MaxChunkExclusive.X = 3;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Wider;
		if ((Result.Error = FVoxelLandGenerator::Create(Settings, Wider)))
		{
			return Result;
		}
		Settings.DirectDensityBounds.Reset();
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Infinite;
		if ((Result.Error = FVoxelLandGenerator::Create(Settings, Infinite)))
		{
			return Result;
		}
		Result.bIdentityDistinguishesExtent =
			Bounded->GetDirectDensityIdentity() != Infinite->GetDirectDensityIdentity()
			&& Bounded->GetDirectDensityIdentity() != Wider->GetDirectDensityIdentity();
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if ((Result.Error = Bounded->CreateWorker(Worker)))
		{
			return Result;
		}
		FVoxelLandChunkKey LeftKey;
		FVoxelLandChunkKey RightKey;
		RightKey.Coordinate.X = 1;
		FVoxelLandChunkKey OutsideKey;
		OutsideKey.Coordinate.X = 2;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Left, Right, Repeated, Outside;
		if ((Result.Error = Bounded->GenerateChunk(RightKey, *Worker, Right))
			|| (Result.Error = Bounded->GenerateChunk(LeftKey, *Worker, Left))
			|| (Result.Error = Bounded->GenerateChunk(RightKey, *Worker, Repeated)))
		{
			return Result;
		}
		Result.bOutsideRejected = !Bounded->IncludesChunk(OutsideKey)
							   && bool(Bounded->GenerateChunk(OutsideKey, *Worker, Outside)) && !Outside
							   && Infinite->IncludesChunk(OutsideKey);
		Result.bDeterministic = Repeated->Hash == Right->Hash && Repeated->Density == Right->Density;
		constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
		const int32 Row = Axis * (16 + Axis * 16);
		Result.bBoundary = Left->Density[Row] > 0 && Left->Density[Row + 1] == 0 && Left->Density[Row + 2] < 0
						&& Right->Density[Row + 32] < 0 && Right->Density[Row + 33] == 0
						&& Right->Density[Row + 34] > 0;
		Result.bSharedHalo = true;
		for (int32 YZ = 0; YZ < Axis * Axis; ++YZ)
		{
			for (int32 X = 0; X < 3; ++X)
			{
				Result.bSharedHalo &=
					Left->Density[YZ * Axis + FVoxelLandChunkData::Cells + X] == Right->Density[YZ * Axis + X];
			}
		}
		return Result;
	}).Get();

	if (!TestFalse(TEXT("Generation succeeds"), bool(Results.Error)))
	{
		AddError(Results.Error.Message);
		return false;
	}
	TestTrue(TEXT("Solid density clips to the finite box on both ends"), Results.bBoundary);
	TestTrue(TEXT("Adjacent chunks agree on their full shared halo"), Results.bSharedHalo);
	TestTrue(TEXT("Outside requests are skipped only for the finite extent"), Results.bOutsideRejected);
	TestTrue(TEXT("Generation is deterministic across request order"), Results.bDeterministic);
	TestTrue(TEXT("Generation identity includes extent and finite bounds"), Results.bIdentityDistinguishesExtent);
	return true;
}

#endif
