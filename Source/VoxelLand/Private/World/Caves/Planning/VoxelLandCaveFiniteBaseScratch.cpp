#include "World/Caves/Planning/VoxelLandCaveFiniteBaseScratch.h"

#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Graph/Serialization/VoxelLandCaveProgramLoader.h"
#include "World/Terrain/Controls/VoxelLandOverrideValidation.h"
#include "World/Caves/Planning/VoxelLandCaveFinitePrepass.h"

class FVoxelLandM5FiniteBaseScratchInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FiniteBaseScratch");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic FVoxelLandM5FiniteBaseScratch::Build(const FVoxelLandM4WorldSnapshot& Snapshot,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ProcessLease,
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)>
		CreateWorker,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::Cancelled, TEXT("Finite base cancelled"));
	}
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	FVoxelLandM5Diagnostic Result = VoxelLand::M5::FinitePrepass::Generate(
		Snapshot, Budget, MoveTemp(ProcessLease), CreateWorker, IsCancelled, Grid, Hash);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Grid)
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::Unproven, TEXT("Finite prepass returned no grid"));
	}
	const FVoxelLandM4ControlSnapshot& Channel = Snapshot.Continental;
	Result = VoxelLandM4OverrideValidation::ValidateMetered(Channel, Budget, IsCancelled);
	if (Result.Code != E::None)
	{
		return Result;
	}
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	Result = FVoxelLandM5ProgramLoader::Load(Channel.Source, Channel.Payload, Budget, IsCancelled, Program);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Program)
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::Unproven, TEXT("Continental program missing"));
	}
	// Finite controls already validated these bounded counts and names. Admit
	// profile construction and hash-table collision work before making copies.
	const int64 Overrides = Channel.Overrides.Num(), Parameters = Channel.Payload.Parameters.Num();
	if (Overrides > 128 || Parameters > 4096)
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(
			E::InvalidInput, TEXT("Continental profile exceeds limits"));
	}
	const int64 Work = Overrides * (Parameters + 1) + Overrides * Overrides * 516 + 17412;
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(
			E::BudgetExceeded, TEXT("Continental profile work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(4ll * 1024 * 1024, Lease))
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::BudgetExceeded, TEXT("Finite base memory exhausted"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::Cancelled, TEXT("Finite base cancelled"));
	}
	FVoxelLandGeneratorSettings Settings;
	Settings.Program = MoveTemp(Program);
	Settings.WorldSeed = uint64(Snapshot.Seed);
	VoxelLandM4OverrideValidation::BuildProfile(Channel, Snapshot.VoxelSizeCm, Settings.Profile);
	if (IsCancelled())
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::Cancelled, TEXT("Finite base cancelled"));
	}
	FVoxelLandM4OverhangConfig Overhang;
	Overhang.Displacement.bEnabled = Snapshot.bOverhangEnabled;
	Overhang.Displacement.AmplitudeQ8 = Snapshot.OverhangAmplitudeQ8;
	Overhang.Displacement.BandHalfWidthQ8 = Snapshot.OverhangBandQ8;
	Overhang.FrequencyPerCm = Snapshot.OverhangFrequencyPerCm;
	Overhang.NodeId = Snapshot.OverhangNodeId;
	Overhang.Salt = uint32(Snapshot.OverhangSalt);
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	Result = FVoxelLandGenerator::CreateM4HeightFieldMetered(Settings, Grid, Snapshot.bNormalizeSlope, Overhang,
		Snapshot.Smooth, Snapshot.HeightDetail, Budget, IsCancelled, Generator);
	if (Result.Code != E::None)
	{
		return Result;
	}
	if (!Generator)
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::Unproven, TEXT("Finite generator missing"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::Cancelled, TEXT("Finite base cancelled"));
	}
	TSharedPtr<FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe> Scratch =
		MakeShareable(new FVoxelLandM5FiniteBaseScratch(MoveTemp(Lease)));
	Scratch->Grid = MoveTemp(Grid);
	Scratch->Generator = MoveTemp(Generator);
	Scratch->Hash = Hash;
	if (IsCancelled())
	{
		return FVoxelLandM5FiniteBaseScratchInternals::Error(E::Cancelled, TEXT("Finite base cancelled"));
	}
	Out = MoveTemp(Scratch);
	return {};
}
