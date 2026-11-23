#include "World/VoxelLandWorldValidation.h"

#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Data/Structs/VoxelLandM4CurveKey.h"
#include "Data/Structs/VoxelLandWorldCommonSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4CurveBake.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Generation/VoxelLandM4FixedLattice.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Operators/VoxelLandOperator.h"
#include "World/VoxelLandM4OverrideValidation.h"

bool VoxelLandWorldValidation::ValidateSnapshot(const FVoxelLandWorldCommonSnapshot& Snapshot,
	EVoxelLandWorldKind Kind, const FVoxelLandOperatorRegistrySnapshot& Providers,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled, FString& OutError)
{
	if (Kind != EVoxelLandWorldKind::Finite && Kind != EVoxelLandWorldKind::Infinite)
	{
		OutError = TEXT("Unsupported world kind.");
		return false;
	}
	const FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	const TCHAR* Names[] = {TEXT("Continental"), TEXT("Mountain"), TEXT("Peaks")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Channels); ++Index)
	{
		if (IsCancelled())
		{
			OutError = TEXT("World validation was cancelled.");
			return false;
		}
		const FVoxelLandM4ControlSnapshot& Channel = *Channels[Index];
		if (Channel.CurveKeys.Num() < 2 || Channel.CurveKeys.Num() > 4097)
		{
			OutError = FString::Printf(TEXT("%s curve must have 2 to 4097 keys."), Names[Index]);
			return false;
		}
		FVoxelLandProgramDraft Draft;
		FVoxelLandWorldCapabilityDiagnostic Capability;
		FVoxelLandGraphDiagnostic Diagnostic;
		if (!FVoxelLandGraphCompiler::CompileForWorld(
				Channel.Source, Kind, Providers, Budget, IsCancelled, Draft, Capability, Diagnostic))
		{
			const TCHAR* Mode = Kind == EVoxelLandWorldKind::Finite ? TEXT("finite") : TEXT("infinite");
			const FString& Detail = Capability.Message.IsEmpty() ? Diagnostic.Message : Capability.Message;
			OutError = FString::Printf(TEXT("%s graph is invalid for a %s world: %s"), Names[Index], Mode, *Detail);
			const FGuid NodeId = Capability.NodeId.IsValid() ? Capability.NodeId : Diagnostic.NodeId;
			if (!Capability.FunctionPath.IsEmpty())
			{
				OutError += FString::Printf(TEXT(" [function=%s]"), *Capability.FunctionPath);
			}
			if (NodeId.IsValid())
			{
				OutError += FString::Printf(TEXT(" [node=%s]"), *NodeId.ToString());
			}
			return false;
		}
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (const FVoxelLandError Error = FVoxelLandProgram::Validate(Draft, Providers, Program))
		{
			OutError = FString::Printf(TEXT("%s graph program is invalid: %s"), Names[Index], *Error.Message);
			return false;
		}
		FString OverrideError;
		if (!VoxelLandM4OverrideValidation::Validate(Channel, OverrideError))
		{
			OutError = FString::Printf(TEXT("%s control overrides are invalid: %s"), Names[Index], *OverrideError);
			return false;
		}
		if (Kind == EVoxelLandWorldKind::Finite)
		{
			if (const FVoxelLandError Error = VoxelLandM4ControlGrid::ValidateProgram(*Program))
			{
				OutError = FString::Printf(TEXT("%s finite control graph cannot run: %s"), Names[Index], *Error.Message);
				return false;
			}
		}
		else
		{
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Program;
			VoxelLandM4OverrideValidation::BuildProfile(Channel, Snapshot.VoxelSizeCm, Settings.Profile);
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
			if (const FVoxelLandError Error = FVoxelLandGenerator::Create(Settings, Generator))
			{
				OutError = FString::Printf(TEXT("%s control generator is invalid: %s"), Names[Index], *Error.Message);
				return false;
			}
			if (const FVoxelLandError Error = VoxelLandM4FixedLattice::Validate(*Generator))
			{
				OutError = FString::Printf(
					TEXT("%s infinite fixed-lattice control cannot run: %s"), Names[Index], *Error.Message);
				return false;
			}
		}
		TArray<FVoxelLandM4CurveKey> Keys;
		Keys.Reserve(Channel.CurveKeys.Num());
		for (const FVoxelLandM4CurveAuthoringKey& Key : Channel.CurveKeys)
		{
			if (Key.InputQ16 < 0 || Key.InputQ16 > 65535)
			{
				OutError = FString::Printf(TEXT("%s curve input is outside [0, 65535]."), Names[Index]);
				return false;
			}
			FVoxelLandM4CurveKey& Target = Keys.AddDefaulted_GetRef();
			Target.InputQ16 = uint16(Key.InputQ16);
			Target.Output = Key.OutputQ8;
		}
		TArray<int32> Samples;
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		FString CurveError;
		if (!VoxelLandM4CurveBake::Bake(Keys, -512000000, 512000000, Samples, Curve, CurveError))
		{
			OutError = FString::Printf(TEXT("%s curve is invalid: %s"), Names[Index], *CurveError);
			return false;
		}
	}
	if (IsCancelled())
	{
		OutError = TEXT("World validation was cancelled.");
		return false;
	}
	OutError.Reset();
	return true;
}
