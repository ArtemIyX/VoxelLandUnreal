#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Generation/Terrain/Heightfield/VoxelLandTerrainPrepass.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4HeightDetailGenerationFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M4HeightDetailGenerationOverlap(const FVoxelLandChunkData& A, const FVoxelLandChunkData& B, int32 Axis)
{
	for (int32 I = 0; I < 35; ++I)
	{
		for (int32 J = 0; J < 35; ++J)
		{
			for (int32 P = 0; P < 3; ++P)
			{
				int32 L[3] = {I, J, P + 32};
				int32 R[3] = {I, J, P};
				if (Axis == 0)
				{
					L[0] = P + 32;
					R[0] = P;
					L[1] = R[1] = I;
					L[2] = R[2] = J;
				}
				if (Axis == 1)
				{
					L[0] = R[0] = I;
					L[1] = P + 32;
					R[1] = P;
					L[2] = R[2] = J;
				}
				if (A.Density[L[0] + 35 * (L[1] + 35 * L[2])] != B.Density[R[0] + 35 * (R[1] + 35 * R[2])])
				{
					return false;
				}
			}
		}
	}
	return true;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4HeightDetailGenerationTest, "VoxelLand.M4.HeightDetail.Generation", M4HeightDetailGenerationFlags)
bool FVoxelLandM4HeightDetailGenerationTest::RunTest(const FString& Parameters)
{
	bool bOverlap[2][3] = {};
	bool bDifferent = true, bIdentity = true, bCancelled = true, bInvalid = true, bUnsupported = false;
	bool bRepeat = true, bSeedChange = true, bZeroAmplitude = true, bOutsideNoiseRange = true;
	const bool bCompleted = Async(EAsyncExecution::ThreadPool, [&]() {
		TArray<int32> Heights;
		Heights.Init(0, 129 * 129);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-6400, -6400, 128, 128, 100, Heights, Grid))
		{
			return false;
		}
		FVoxelLandProgramDraft Draft;
		Draft.FormatVersion = 2;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Load;
		Load.Op = EVoxelLandOpcode::LoadZ;
		FVoxelLandInstruction Store;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Load, Store};
		Draft.Constants = {1.f, 0.5f};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> PlainProgram, SmoothProgram;
		if (FVoxelLandProgram::Validate(Draft, PlainProgram))
		{
			return false;
		}
		Draft.SmoothFilter.bPresent = true;
		Draft.SmoothFilter.Enabled.Index = 0;
		Draft.SmoothFilter.Strength.Index = 1;
		if (FVoxelLandProgram::Validate(Draft, SmoothProgram))
		{
			return false;
		}
		for (int32 Smooth = 0; Smooth < 2; ++Smooth)
		{
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Smooth ? SmoothProgram : PlainProgram;
			Settings.Profile.VoxelSizeCm = 100.f;
			Settings.WorldSeed = 12345;
			FVoxelLandM4HeightDetailConfig Config;
			FVoxelLandM4OverhangConfig NoOverhang;
			Config.bEnabled = true;
			Config.AmplitudeQ8 = 128000;
			Config.FrequencyPerCm = 0.004f;
			Config.NodeId = FGuid(11, 22, 33, 44);
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Active, Base, Disabled;
			if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, NoOverhang, Config, Active))
			{
				return false;
			}
			if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, Base))
			{
				return false;
			}
			// A grid may be valid beyond the exact-float range, but enabled native
			// noise must reject such chunks before they are dispatched.
			TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> OutsideGrid;
			if (!FVoxelLandM4HeightGrid::Create(16770000, -6400, 128, 128, 100, Heights, OutsideGrid))
			{
				return false;
			}
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> OutsideGenerator;
			if (FVoxelLandGenerator::CreateM4HeightField(
					Settings, OutsideGrid, true, NoOverhang, Config, OutsideGenerator))
			{
				return false;
			}
			FVoxelLandChunkKey OutsideKey;
			OutsideKey.Coordinate = FIntVector(5243, -1, -1);
			bOutsideNoiseRange &=
				OutsideGenerator->ValidateM4ChunkDemand(OutsideKey).Code == EVoxelLandErrorCode::NonFiniteCoordinate;
			Config.bEnabled = false;
			if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, NoOverhang, Config, Disabled))
			{
				return false;
			}
			TUniquePtr<FVoxelLandWorkerContext> W, BW, DW;
			FVoxelLandError Error = Active->CreateWorker(W);
			if (Error.Code == EVoxelLandErrorCode::BackendUnavailable)
			{
				bUnsupported = true;
				return true;
			}
			if (Error || Base->CreateWorker(BW) || Disabled->CreateWorker(DW))
			{
				return false;
			}
			FVoxelLandChunkKey Key;
			Key.Coordinate = FIntVector(-1, -1, -1);
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> A, B, D;
			if (Active->GenerateChunk(Key, *W, A) || Base->GenerateChunk(Key, *BW, B)
				|| Disabled->GenerateChunk(Key, *DW, D))
			{
				return false;
			}
			bDifferent &= A->Density != B->Density;
			bIdentity &= B->Density == D->Density;
			Config.bEnabled = true;
			Config.AmplitudeQ8 = 0;
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Zero;
			if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, NoOverhang, Config, Zero))
			{
				return false;
			}
			TUniquePtr<FVoxelLandWorkerContext> ZeroWorker;
			if (Zero->CreateWorker(ZeroWorker))
			{
				return false;
			}
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> ZeroChunk;
			if (Zero->GenerateChunk(Key, *ZeroWorker, ZeroChunk))
			{
				return false;
			}
			bZeroAmplitude &= B->Density == ZeroChunk->Density;
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Repeated;
			if (Active->GenerateChunk(Key, *W, Repeated))
			{
				return false;
			}
			bRepeat &= Repeated->Density == A->Density;
			FVoxelLandGeneratorSettings OtherSettings = Settings;
			OtherSettings.WorldSeed++;
			Config.bEnabled = true;
			Config.AmplitudeQ8 = 128000;
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Other;
			if (FVoxelLandGenerator::CreateM4HeightField(OtherSettings, Grid, true, NoOverhang, Config, Other))
			{
				return false;
			}
			TUniquePtr<FVoxelLandWorkerContext> OtherWorker;
			if (Other->CreateWorker(OtherWorker))
			{
				return false;
			}
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> OtherChunk;
			if (Other->GenerateChunk(Key, *OtherWorker, OtherChunk))
			{
				return false;
			}
			bSeedChange &= OtherChunk->Density != A->Density;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				FVoxelLandChunkKey Next = Key;
				if (Axis == 0)
				{
					++Next.Coordinate.X;
				}
				if (Axis == 1)
				{
					++Next.Coordinate.Y;
				}
				if (Axis == 2)
				{
					++Next.Coordinate.Z;
				}
				TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Adjacent;
				if (Active->GenerateChunk(Next, *W, Adjacent))
				{
					return false;
				}
				bOverlap[Smooth][Axis] = M4HeightDetailGenerationOverlap(*A, *Adjacent, Axis);
			}
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Existing = A;
			int32 Checks = 0;
			Error = Active->GenerateChunkCancellable(Key, *W, Existing, [&Checks] { return ++Checks > 5; });
			bCancelled &= Error.Code == EVoxelLandErrorCode::Cancelled && Existing == A;
			Config.FrequencyPerCm = 0.f;
			Existing.Reset();
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Invalid;
			bInvalid &=
				bool(FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, NoOverhang, Config, Invalid));
			Config.FrequencyPerCm = 0.004f;
			Config.bEnabled = false;
			Config.AlgorithmVersion = 2;
			Invalid = Active;
			bInvalid &=
				bool(FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, NoOverhang, Config, Invalid))
				&& Invalid == Active;
		}
		return true;
	}).Get();
	TestTrue(TEXT("worker evaluation completed"), bCompleted);
	if (bUnsupported)
	{
		AddWarning(TEXT("Native SSE2 unavailable; height detail generation was not exercised"));
		return true;
	}
	TestTrue(TEXT("2D height detail modifies density"), bDifferent);
	TestTrue(TEXT("disabled noise preserves density"), bIdentity);
	TestTrue(TEXT("zero amplitude preserves density"), bZeroAmplitude);
	TestTrue(TEXT("out-of-range noise coordinates rejected before dispatch"), bOutsideNoiseRange);
	for (int32 S = 0; S < 2; ++S)
	{
		for (int32 A = 0; A < 3; ++A)
		{
			TestTrue(TEXT("negative chunk overlap plane"), bOverlap[S][A]);
		}
	}
	TestTrue(TEXT("repeat uses same seed deterministically"), bRepeat);
	TestTrue(TEXT("world seed changes height detail"), bSeedChange);
	TestTrue(TEXT("cancellation preserves result"), bCancelled);
	TestTrue(TEXT("invalid frequency rejected"), bInvalid);
	return bCompleted;
}
#endif
