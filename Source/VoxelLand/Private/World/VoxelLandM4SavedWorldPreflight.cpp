#include "World/VoxelLandM4SavedWorldPreflight.h"
#include "Data/VoxelLandM4HeightStageValidation.h"
#include "Data/Structs/VoxelLandM4SavedWorld.h"

namespace
{
bool M4SavedParametersBounded(const TArray<FVoxelLandGraphParameter>& Parameters, int32 Limit)
{
	if (Parameters.Num() > Limit)
	{
		return false;
	}
	for (const FVoxelLandGraphParameter& P : Parameters)
	{
		if (P.Name.Len() > 128 || P.Tooltip.ToString().Len() > 2048)
		{
			return false;
		}
	}
	return true;
}
bool M4SavedGraphBounded(const TArray<FVoxelLandGraphParameter>& Parameters,
	const TArray<FVoxelLandGraphNodeRecord>& Nodes, const TArray<FVoxelLandGraphLinkRecord>& Links,
	int32 ParameterLimit, int32& TotalNodes, int32& TotalLinks, int32& TotalPins)
{
	if (!M4SavedParametersBounded(Parameters, ParameterLimit) || Nodes.Num() > 128 - TotalNodes
		|| Links.Num() > 2048 - TotalLinks)
	{
		return false;
	}
	TotalNodes += Nodes.Num();
	TotalLinks += Links.Num();
	for (const FVoxelLandGraphNodeRecord& Node : Nodes)
	{
		if (!Node.Configuration.IsEmpty() || Node.Pins.Num() > 64 || Node.Pins.Num() > 1024 - TotalPins
			|| Node.FunctionPath.ToString().Len() > 2048)
		{
			return false;
		}
		TotalPins += Node.Pins.Num();
		for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			if (Pin.Name.Len() > 128)
			{
				return false;
			}
		}
	}
	return true;
}
bool M4SavedChannelBounded(const FVoxelLandM4ControlSnapshot& Channel)
{
	const auto& Source = Channel.Source;
	const auto& Program = Channel.Payload;
	if (Source.Functions.Num() > 16 || Channel.Overrides.Num() > 128 || Channel.CurveKeys.Num() > 4097
		|| Channel.CookedCurve.SourceKeys.Num() > 4097 || Channel.CookedCurve.SamplesQ8.Num() > 4097
		|| Program.Instructions.Num() > 2048 || Program.Constants.Num() > 4096 || Program.Parameters.Num() > 128
		|| Program.Seeds.Num() > 256 || Program.Noise.Num() > 256 || !Program.ExternalOperators.IsEmpty()
		|| Program.ExternalFilterIndex != INDEX_NONE)
	{
		return false;
	}
	int32 Nodes = 0, Links = 0, Pins = 0;
	if (!M4SavedGraphBounded(Source.Parameters, Source.Nodes, Source.Links, 128, Nodes, Links, Pins))
	{
		return false;
	}
	for (const auto& Function : Source.Functions)
	{
		if (Function.AssetPath.Len() > 2048
			|| !M4SavedGraphBounded(Function.Inputs, Function.Nodes, Function.Links, 63, Nodes, Links, Pins))
		{
			return false;
		}
	}
	for (const auto& Parameter : Program.Parameters)
	{
		if (Parameter.Name.Len() > 128)
		{
			return false;
		}
	}
	return true;
}
}
bool VoxelLandM4SavedWorldPreflight::ValidateSnapshot(const FVoxelLandM4WorldSnapshot& S, FString& Error)
{
	if (S.HorizontalRadius < 1 || S.HorizontalRadius > 8 || S.VerticalRadius < 1 || S.VerticalRadius > 4
		|| S.MaterialSetupPath.ToString().Len() > 2048 || !M4SavedChannelBounded(S.Continental)
		|| !M4SavedChannelBounded(S.Mountain) || !M4SavedChannelBounded(S.Peaks))
	{
		Error = TEXT("M4 saved world snapshot exceeds admission limits");
		return false;
	}
	return VoxelLandM4HeightStageValidation::Validate(S, Error);
}
bool VoxelLandM4SavedWorldPreflight::Validate(const FVoxelLandM4SavedWorld& Saved, FString& Error)
{
	if (Saved.FormatVersion != 1 || Saved.Identity.CanonicalBytes.Num() < 8
		|| Saved.Identity.CanonicalBytes.Num() > 8 * 1024 * 1024)
	{
		Error = TEXT("M4 saved world identity exceeds admission limits or has an unsupported version");
		return false;
	}
	return ValidateSnapshot(Saved.Snapshot, Error);
}
