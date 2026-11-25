#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"

#if WITH_EDITOR
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Program/Execution/VoxelLandProgramDraft.h"
#include "Misc/DataValidation.h"

EDataValidationResult UVoxelLandM4WorldConfig::IsDataValid(FDataValidationContext& Context) const
{
	FString Error;
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!CaptureSnapshot(Snapshot, Error) || !ValidateBakedCurves(Error))
	{
		Context.AddError(FText::FromString(Error.IsEmpty() ? TEXT("Invalid M4 terrain configuration.") : Error));
		return EDataValidationResult::Invalid;
	}
	const FVoxelLandM4ControlSnapshot* Controls[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	for (const FVoxelLandM4ControlSnapshot* Control : Controls)
	{
		const FVoxelLandSerializedProgram& Program = Control->Payload;
		if (Program.EnvelopeVersion != 1
			|| Program.ProgramVersion != int32(FVoxelLandProgramDraft::CurrentFormatVersion)
			|| Program.CompilerSemanticVersion != int32(FVoxelLandProgramDraft::CompilerSemanticVersion)
			|| Program.RegisterCount < 1 || Program.RegisterCount > 1024 || Program.Instructions.IsEmpty())
		{
			Context.AddError(FText::FromString(
				TEXT("An M4 control graph has no supported compiled program; recompile and save it.")));
			return EDataValidationResult::Invalid;
		}
	}
	return EDataValidationResult::Valid;
}
#endif
