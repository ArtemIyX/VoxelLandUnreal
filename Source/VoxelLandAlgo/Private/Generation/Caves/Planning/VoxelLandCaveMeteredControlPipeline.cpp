#include "Generation/Caves/Planning/VoxelLandCaveMeteredControlPipeline.h"

#include "Data/Terrain/Domains/VoxelLandTerrainDomainMode.h"
#include "Data/Terrain/Domains/VoxelLandTerrainDomainSettings.h"
#include "Data/Terrain/Erosion/VoxelLandErosionSettings.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "Generation/Terrain/Controls/VoxelLandControlPipeline.h"
#include "Generation/Terrain/Controls/VoxelLandControlPipelineInputs.h"
#include "Generation/Terrain/Domains/VoxelLandTerrainDomain.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"

class FVoxelLandM5MeteredControlPipelineInternals final
{
public:
	// Three uint16 controls, base/final/working int32 heights, int64 thermal
	// deltas, hash bytes and droplet starts fit in 32 bytes per sample plus
	// eight per droplet. Double these logical payloads and add fixed batch and
	// metadata allowance. Worker/provider allocations and allocator overhead
	// remain separate. Work units count bounded sample/instruction and erosion
	// visits, not machine instructions or elapsed time.
	static constexpr int64 GridBytesPerSample = 8;
	static constexpr int64 ScratchBytesPerSample = 64;
	static constexpr int64 ScratchBytesPerDroplet = 16;
	static constexpr int64 FixedBytes = 65536;
	static constexpr int64 FixedWork = 65536;
	static constexpr int64 SampleWork = 128;
	static constexpr int64 InstructionWork = 64;
	static constexpr int64 ThermalWork = 128;
	static constexpr int64 DropletWork = 1024;
	static constexpr int64 DropletStepWork = 128;

	class FGridLease final : public FVoxelLandM4GridLease
	{
	public:
		FGridLease(TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> InProcess,
			TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> InBudget)
			: Process(MoveTemp(InProcess))
			, Budget(MoveTemp(InBudget))
		{
		}

	private:
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Process;
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Budget;
	};

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.MeteredControlPipeline");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::MeteredControlPipeline::Generate(const FVoxelLandM4ControlPipelineInputs& Inputs,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ProcessLease, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5MeteredControlPipelineInternals::Error(E::Cancelled, TEXT("Control pipeline cancelled"));
	}
	if (IsInGameThread() || !ProcessLease.IsValid() || Inputs.Domain.Mode != EVoxelLandM4DomainMode::Finite
		|| VoxelLandM4Domain::Validate(Inputs.Domain) || Inputs.DropletCount < 0 || Inputs.DropletCount > 65536
		|| (Inputs.Domain.bGlobalErosion
			&& (Inputs.Erosion.MaxSteps < 0 || Inputs.Erosion.MaxSteps > 128 || Inputs.Erosion.ThermalIterations < 0
				|| Inputs.Erosion.ThermalIterations > 32)))
	{
		return FVoxelLandM5MeteredControlPipelineInternals::Error(
			E::InvalidInput, TEXT("Invalid finite pipeline input"));
	}
	const FVoxelLandGenerator* Generators[] = {
		&Inputs.Continental.Generator, &Inputs.Mountain.Generator, &Inputs.Peaks.Generator};
	int64 Instructions = 0;
	for (const FVoxelLandGenerator* Generator : Generators)
	{
		const int32 Count = Generator->GetProgram().GetData().Instructions.Num();
		if (!TryAdd(Instructions, Count, Instructions))
		{
			return FVoxelLandM5MeteredControlPipelineInternals::Error(
				E::ArithmeticOverflow, TEXT("Instruction count overflow"));
		}
	}
	if (!Budget->TryChargeWork(Instructions))
	{
		return FVoxelLandM5MeteredControlPipelineInternals::Error(
			E::BudgetExceeded, TEXT("Program inventory budget exhausted"));
	}
	for (const FVoxelLandGenerator* Generator : Generators)
	{
		if (IsCancelled())
		{
			return FVoxelLandM5MeteredControlPipelineInternals::Error(E::Cancelled, TEXT("Control pipeline cancelled"));
		}
		if (VoxelLandM4ControlGrid::ValidateProgram(Generator->GetProgram()))
		{
			return FVoxelLandM5MeteredControlPipelineInternals::Error(
				E::Unproven, TEXT("Unsupported control program or provider"));
		}
	}
	int64 Count = 0, GridBytes = 0, ScratchBytes = 0, DropletBytes = 0;
	int64 PerSample = 0, InstructionUnits = 0, ThermalUnits = 0, SampleUnits = 0;
	int64 PerDroplet = 0, DropletUnits = 0, StepUnits = 0, Work = 0;
	const int64 Thermal = Inputs.Domain.bGlobalErosion ? Inputs.Erosion.ThermalIterations : 0;
	const int64 Steps = Inputs.Domain.bGlobalErosion ? Inputs.Erosion.MaxSteps : 0;
	const int64 Droplets = Inputs.Domain.bGlobalErosion ? Inputs.DropletCount : 0;
	if (!TryMultiplyNonNegative(int64(Inputs.Domain.CellsX) + 1, int64(Inputs.Domain.CellsY) + 1, Count)
		|| !TryMultiplyNonNegative(Count, FVoxelLandM5MeteredControlPipelineInternals::GridBytesPerSample, GridBytes)
		|| !TryAdd(GridBytes, FVoxelLandM5MeteredControlPipelineInternals::FixedBytes, GridBytes)
		|| !TryMultiplyNonNegative(
			Count, FVoxelLandM5MeteredControlPipelineInternals::ScratchBytesPerSample, ScratchBytes)
		|| !TryMultiplyNonNegative(
			Droplets, FVoxelLandM5MeteredControlPipelineInternals::ScratchBytesPerDroplet, DropletBytes)
		|| !TryAdd(ScratchBytes, DropletBytes, ScratchBytes)
		|| !TryAdd(ScratchBytes, FVoxelLandM5MeteredControlPipelineInternals::FixedBytes, ScratchBytes)
		|| !TryMultiplyNonNegative(
			Instructions, FVoxelLandM5MeteredControlPipelineInternals::InstructionWork, InstructionUnits)
		|| !TryMultiplyNonNegative(Thermal, FVoxelLandM5MeteredControlPipelineInternals::ThermalWork, ThermalUnits)
		|| !TryAdd(FVoxelLandM5MeteredControlPipelineInternals::SampleWork, InstructionUnits, PerSample)
		|| !TryAdd(PerSample, ThermalUnits, PerSample) || !TryMultiplyNonNegative(Count, PerSample, SampleUnits)
		|| !TryMultiplyNonNegative(Steps, FVoxelLandM5MeteredControlPipelineInternals::DropletStepWork, StepUnits)
		|| !TryAdd(FVoxelLandM5MeteredControlPipelineInternals::DropletWork, StepUnits, PerDroplet)
		|| !TryMultiplyNonNegative(Droplets, PerDroplet, DropletUnits) || !TryAdd(SampleUnits, DropletUnits, Work)
		|| !TryAdd(Work, FVoxelLandM5MeteredControlPipelineInternals::FixedWork, Work))
	{
		return FVoxelLandM5MeteredControlPipelineInternals::Error(
			E::ArithmeticOverflow, TEXT("Pipeline admission overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5MeteredControlPipelineInternals::Error(
			E::BudgetExceeded, TEXT("Pipeline work budget exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> GridBudget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ScratchBudget;
	if (!Budget->TryReserveBytes(GridBytes, GridBudget) || !Budget->TryReserveBytes(ScratchBytes, ScratchBudget))
	{
		return FVoxelLandM5MeteredControlPipelineInternals::Error(
			E::BudgetExceeded, TEXT("Pipeline memory budget exhausted"));
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Combined =
		MakeShared<FVoxelLandM5MeteredControlPipelineInternals::FGridLease, ESPMode::ThreadSafe>(
			MoveTemp(ProcessLease), MoveTemp(GridBudget));
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	const FVoxelLandError Result = VoxelLandM4ControlPipeline::Generate(Inputs, Grid, Hash, IsCancelled, Combined);
	const bool bCancelled = IsCancelled();
	if (Result || !Grid.IsValid() || !Grid->RetainsLease(Combined) || bCancelled)
	{
		FVoxelLandM5Diagnostic Failure = FVoxelLandM5MeteredControlPipelineInternals::Error(
			bCancelled || Result.Code == EVoxelLandErrorCode::Cancelled ? E::Cancelled : E::Unproven,
			TEXT("Finite control pipeline failed"));
		if (Result)
		{
			Failure.Message = Result.Message;
		}
		return Failure;
	}
	OutGrid = MoveTemp(Grid);
	OutHash = Hash;
	return {};
}
