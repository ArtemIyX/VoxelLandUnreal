#include "World/Terrain/Worlds/VoxelLandAuthoredHeightProgram.h"

#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandFixedLattice.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"

class FVoxelLandAuthoredHeightProgramInternals final
{
public:
	static FString DescribeError(const FString& Error, const FGuid& Node, const FString& FunctionPath = {})
	{
		FString Message = Node.IsValid() ? FString::Printf(TEXT("[node=%s] %s"), *Node.ToString(), *Error) : Error;
		if (!FunctionPath.IsEmpty())
		{
			Message += FString::Printf(TEXT(" (function %s)"), *FunctionPath);
		}
		return Message;
	}

	static bool HasWiredResult(const FVoxelLandGraphSnapshot& Source)
	{
		for (const FVoxelLandGraphNodeRecord& Node : Source.Nodes)
		{
			if (Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput) && Node.Pins.Num() == 1)
			{
				return Source.Links.ContainsByPredicate([&Node](const FVoxelLandGraphLinkRecord& Link) {
					return Link.TargetNode == Node.Id && Link.TargetPin == Node.Pins[0].Id;
				});
			}
		}
		return false;
	}
};

bool VoxelLandAuthoredHeightProgram::CreateSource(const FVoxelLandM4WorldSnapshot& Snapshot,
	FVoxelLandGeneratorSettings& OutSettings, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutSource,
	FString& Error)
{
	if (IsInGameThread() || !Snapshot.AuthoredHeightOutputId.IsValid() || Snapshot.HeightScaleCm < 1
		|| Snapshot.HeightScaleCm > 2000000 || FMath::Abs(int64(Snapshot.BaseHeightQ8)) > 512000000)
	{
		Error = TEXT("Authored Height requires a worker, a named Float output and supported elevation settings.");
		return false;
	}
	if (!FVoxelLandAuthoredHeightProgramInternals::HasWiredResult(Snapshot.AuthoredHeightSource))
	{
		Error = TEXT("Authored Height requires a wired Float output. Connect a value and regenerate the world.");
		return false;
	}
	FVoxelLandProgramDraft Draft;
	FVoxelLandGraphDiagnostic Diagnostic;
	TArray<FVoxelLandGraphNodeOrigin> Origins;
	if (!FVoxelLandGraphCompiler::CompileWithInstructionOrigins(
			Snapshot.AuthoredHeightSource, Draft, Origins, Diagnostic))
	{
		Error = FVoxelLandAuthoredHeightProgramInternals::DescribeError(Diagnostic.Message, Diagnostic.NodeId);
		return false;
	}
	const auto DescribeInstruction = [&Origins](const FVoxelLandError& Failure) {
		const FVoxelLandGraphNodeOrigin* Origin =
			Origins.IsValidIndex(Failure.Instruction) ? &Origins[Failure.Instruction] : nullptr;
		return Origin ? FVoxelLandAuthoredHeightProgramInternals::DescribeError(
							Failure.Message, Origin->NodeId, Origin->FunctionPath)
					  : Failure.Message;
	};
	FVoxelLandGeneratorSettings Settings;
	if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Settings.Program))
	{
		Error = DescribeInstruction(Failure);
		return false;
	}
	if (const FVoxelLandError Failure = VoxelLandM4ControlGrid::ValidateProgram(*Settings.Program))
	{
		Error = DescribeInstruction(Failure);
		return false;
	}
	Settings.WorldSeed = uint64(Snapshot.Seed);
	Settings.Profile.VoxelSizeCm = float(Snapshot.VoxelSizeCm);
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Source;
	if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Source))
	{
		Error = DescribeInstruction(Failure);
		return false;
	}
	if (Snapshot.Domain.Mode == EVoxelLandM4DomainMode::Unlimited)
	{
		if (const FVoxelLandError Failure = VoxelLandM4FixedLattice::Validate(*Source))
		{
			Error = TEXT("Infinite Height fixed-lattice backend: ") + DescribeInstruction(Failure);
			return false;
		}
	}
	OutSettings = MoveTemp(Settings);
	OutSource = MoveTemp(Source);
	return true;
}

bool VoxelLandM4Preparation::ValidateAuthoredHeight(const FVoxelLandM4WorldSnapshot& Snapshot, FString& Error)
{
	FVoxelLandGeneratorSettings Settings;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Source;
	return VoxelLandAuthoredHeightProgram::CreateSource(Snapshot, Settings, Source, Error);
}

TArray<uint8> VoxelLandAuthoredHeightProgram::BuildPrepassKey(
	const FVoxelLandM4WorldSnapshot& S, const FVoxelLandProgram& Program)
{
	FCanonicalWriter W;
	W.U32(0x484c5656);
	W.U32(1);
	const int64 Wide[] = {S.Seed, S.Domain.OriginXCm, S.Domain.OriginYCm, S.Domain.GridSpacingCm,
		S.Erosion.CapacityMaxQ8, S.Erosion.TalusQ8, S.Erosion.MaxTransferQ8};
	for (const int64 V : Wide)
	{
		W.U32(uint32(V));
		W.U32(uint32(uint64(V) >> 32));
	}
	const uint32 Values[] = {S.AuthoredHeightOutputId.A, S.AuthoredHeightOutputId.B, S.AuthoredHeightOutputId.C,
		S.AuthoredHeightOutputId.D, uint32(S.HeightScaleCm), uint32(S.BaseHeightQ8), uint32(S.Domain.CellsX),
		uint32(S.Domain.CellsY), uint32(S.Domain.bGlobalErosion), S.ErosionStageId.A, S.ErosionStageId.B,
		S.ErosionStageId.C, S.ErosionStageId.D, uint32(S.ErosionSalt), uint32(S.DropletCount),
		uint32(S.Erosion.MaxSteps), uint32(S.Erosion.ThermalIterations)};
	for (const uint32 V : Values)
	{
		W.U32(V);
	}
	const uint64 Fingerprint = Program.GetFingerprint();
	W.U32(uint32(Fingerprint));
	W.U32(uint32(Fingerprint >> 32));
	return MoveTemp(W.Bytes);
}
