#include "World/VoxelLandM5FinitePrepass.h"

#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4ControlPipelineInputs.h"
#include "Generation/VoxelLandM5MeteredControlPipeline.h"
#include "World/VoxelLandM5FiniteControls.h"

class FVoxelLandM5FinitePrepassInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FinitePrepass");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::FinitePrepass::Generate(const FVoxelLandM4WorldSnapshot& Snapshot,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ProcessLease,
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, TUniquePtr<FVoxelLandWorkerContext>&)> CreateWorker,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	uint64& OutHash)
{
	if (IsCancelled())
	{
		return FVoxelLandM5FinitePrepassInternals::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Finite prepass cancelled"));
	}
	if (!ProcessLease)
	{
		return FVoxelLandM5FinitePrepassInternals::Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Finite prepass requires a process lease"));
	}
	TSharedPtr<const FVoxelLandM5FiniteControls, ESPMode::ThreadSafe> Controls;
	FVoxelLandM5Diagnostic Result = FVoxelLandM5FiniteControls::Build(Snapshot, Budget, IsCancelled, Controls);
	if (Result.Code != EVoxelLandM5ErrorCode::None)
	{
		return Result;
	}
	if (!Controls)
	{
		return FVoxelLandM5FinitePrepassInternals::Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Finite controls are missing"));
	}
	// Controls outlive workers; all worker destruction occurs before returning.
	TUniquePtr<FVoxelLandWorkerContext> Workers[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5FinitePrepassInternals::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Finite prepass cancelled"));
		}
		Result = CreateWorker(Controls->GetGenerator(Index), Budget, IsCancelled, Workers[Index]);
		if (Result.Code != EVoxelLandM5ErrorCode::None)
		{
			return Result;
		}
		if (!Workers[Index])
		{
			return FVoxelLandM5FinitePrepassInternals::Error(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Worker factory returned no worker"));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5FinitePrepassInternals::Error(EVoxelLandM5ErrorCode::Cancelled, TEXT("Finite prepass cancelled"));
	}
	const FVoxelLandM4ControlPipelineInputs Inputs{Snapshot.Domain,
		{Controls->GetGenerator(0), *Workers[0], Controls->GetCurve(0)},
		{Controls->GetGenerator(1), *Workers[1], Controls->GetCurve(1)},
		{Controls->GetGenerator(2), *Workers[2], Controls->GetCurve(2)}, Snapshot.BaseHeightQ8,
		uint64(Snapshot.Seed), Snapshot.ErosionStageId, uint32(Snapshot.ErosionSalt), Snapshot.DropletCount,
		Snapshot.Erosion};
	return VoxelLand::M5::MeteredControlPipeline::Generate(Inputs, Budget, MoveTemp(ProcessLease), IsCancelled,
		OutGrid, OutHash);
}
