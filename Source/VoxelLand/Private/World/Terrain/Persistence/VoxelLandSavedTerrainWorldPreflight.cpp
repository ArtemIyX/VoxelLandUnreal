#include "World/Terrain/Persistence/VoxelLandSavedTerrainWorldPreflight.h"

#include "VoxelLandSavedGraphTotals.h"
#include "Data/Terrain/HeightGraph/VoxelLandHeightStageValidation.h"
#include "Data/Terrain/Persistence/VoxelLandSavedTerrainWorldRecord.h"

class FVoxelLandM4SavedWorldPreflightInternals final
{
public:
	static constexpr int32 MaxNameLength = 128;
	static constexpr int32 MaxTooltipLength = 2048;
	static constexpr int32 MaxPathLength = 2048;
	static constexpr int32 MaxNodes = 128;
	static constexpr int32 MaxLinks = 2048;
	static constexpr int32 MaxPins = 1024;
	static constexpr int32 MaxPinsPerNode = 64;
	static constexpr int32 MaxFunctions = 16;
	static constexpr int32 MaxFunctionInputs = 63;
	static constexpr int32 MaxParameters = 128;
	static constexpr int32 MaxCurveEntries = 4097;
	static constexpr int32 MaxInstructions = 2048;
	static constexpr int32 MaxConstants = 4096;
	static constexpr int32 MaxSeeds = 256;
	static constexpr int32 MaxNoiseEntries = 256;
	static constexpr int32 MaxHorizontalRadius = 8;
	static constexpr int32 MaxVerticalRadius = 4;
	static constexpr int32 MinIdentityBytes = 8;
	static constexpr int32 MaxIdentityBytes = 8 * 1024 * 1024;

	static bool M4SavedParametersBounded(const TArray<FVoxelLandGraphParameter>& Parameters, int32 Limit);
	static bool M4SavedGraphBounded(const TArray<FVoxelLandGraphParameter>& Parameters,
		const TArray<FVoxelLandGraphNodeRecord>& Nodes, const TArray<FVoxelLandGraphLinkRecord>& Links,
		int32 ParameterLimit, FVoxelLandM4SavedGraphTotals& Totals);
	static bool M4SavedChannelBounded(const FVoxelLandM4ControlSnapshot& Channel);
	static bool M4SavedSourceBounded(const FVoxelLandGraphSnapshot& Source);
};

bool FVoxelLandM4SavedWorldPreflightInternals::M4SavedParametersBounded(
	const TArray<FVoxelLandGraphParameter>& Parameters, int32 Limit)
{
	if (Parameters.Num() > Limit)
	{
		return false;
	}
	for (const FVoxelLandGraphParameter& P : Parameters)
	{
		if (P.Name.Len() > MaxNameLength || P.Tooltip.ToString().Len() > MaxTooltipLength)
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM4SavedWorldPreflightInternals::M4SavedGraphBounded(const TArray<FVoxelLandGraphParameter>& Parameters,
	const TArray<FVoxelLandGraphNodeRecord>& Nodes, const TArray<FVoxelLandGraphLinkRecord>& Links,
	int32 ParameterLimit, FVoxelLandM4SavedGraphTotals& Totals)
{
	if (!M4SavedParametersBounded(Parameters, ParameterLimit) || Nodes.Num() > MaxNodes - Totals.Nodes
		|| Links.Num() > MaxLinks - Totals.Links)
	{
		return false;
	}
	Totals.Nodes += Nodes.Num();
	Totals.Links += Links.Num();
	for (const FVoxelLandGraphNodeRecord& Node : Nodes)
	{
		if (!Node.Configuration.IsEmpty() || Node.Pins.Num() > MaxPinsPerNode || Node.Pins.Num() > MaxPins - Totals.Pins
			|| Node.FunctionPath.ToString().Len() > MaxPathLength)
		{
			return false;
		}
		Totals.Pins += Node.Pins.Num();
		for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			if (Pin.Name.Len() > MaxNameLength)
			{
				return false;
			}
		}
	}
	return true;
}

bool FVoxelLandM4SavedWorldPreflightInternals::M4SavedChannelBounded(const FVoxelLandM4ControlSnapshot& Channel)
{
	const auto& Source = Channel.Source;
	const auto& Program = Channel.Payload;
	if (Source.Functions.Num() > MaxFunctions || Channel.Overrides.Num() > MaxParameters
		|| Channel.CurveKeys.Num() > MaxCurveEntries || Channel.CookedCurve.SourceKeys.Num() > MaxCurveEntries
		|| Channel.CookedCurve.SamplesQ8.Num() > MaxCurveEntries || Program.Instructions.Num() > MaxInstructions
		|| Program.Constants.Num() > MaxConstants || Program.Parameters.Num() > MaxParameters
		|| Program.Seeds.Num() > MaxSeeds || Program.Noise.Num() > MaxNoiseEntries
		|| !Program.ExternalOperators.IsEmpty() || Program.ExternalFilterIndex != INDEX_NONE)
	{
		return false;
	}
	if (!M4SavedSourceBounded(Source))
	{
		return false;
	}
	for (const auto& Parameter : Program.Parameters)
	{
		if (Parameter.Name.Len() > MaxNameLength)
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM4SavedWorldPreflightInternals::M4SavedSourceBounded(const FVoxelLandGraphSnapshot& Source)
{
	FVoxelLandM4SavedGraphTotals Totals;
	if (Source.Functions.Num() > MaxFunctions
		|| !M4SavedGraphBounded(Source.Parameters, Source.Nodes, Source.Links, MaxParameters, Totals))
	{
		return false;
	}
	for (const auto& Function : Source.Functions)
	{
		if (Function.AssetPath.Len() > MaxPathLength
			|| !M4SavedGraphBounded(Function.Inputs, Function.Nodes, Function.Links, MaxFunctionInputs, Totals))
		{
			return false;
		}
	}
	return true;
}

bool VoxelLandM4SavedWorldPreflight::ValidateSnapshot(const FVoxelLandM4WorldSnapshot& S, FString& Error)
{
	if (S.HorizontalRadius < 1 || S.HorizontalRadius > FVoxelLandM4SavedWorldPreflightInternals::MaxHorizontalRadius
		|| S.VerticalRadius < 1 || S.VerticalRadius > FVoxelLandM4SavedWorldPreflightInternals::MaxVerticalRadius
		|| S.MaterialSetupPath.ToString().Len() > FVoxelLandM4SavedWorldPreflightInternals::MaxPathLength
		|| !FVoxelLandM4SavedWorldPreflightInternals::M4SavedChannelBounded(S.Continental)
		|| !FVoxelLandM4SavedWorldPreflightInternals::M4SavedChannelBounded(S.Mountain)
		|| !FVoxelLandM4SavedWorldPreflightInternals::M4SavedChannelBounded(S.Peaks)
		|| !FVoxelLandM4SavedWorldPreflightInternals::M4SavedSourceBounded(S.AuthoredHeightSource))
	{
		Error = TEXT("M4 saved world snapshot exceeds admission limits");
		return false;
	}
	if (S.AuthoredHeightOutputId.IsValid()
		&& (S.HeightScaleCm < 1 || S.HeightScaleCm > 2000000 || FMath::Abs(int64(S.BaseHeightQ8)) > 512000000
			|| S.AuthoredHeightSource.Nodes.IsEmpty() || S.HeightStage.Version != 0))
	{
		Error = TEXT("Saved authored Height has an invalid graph source or elevation settings.");
		return false;
	}
	return VoxelLandM4HeightStageValidation::Validate(S, Error);
}

bool VoxelLandM4SavedWorldPreflight::Validate(const FVoxelLandM4SavedWorld& Saved, FString& Error)
{
	if (Saved.FormatVersion == 1 || !Saved.Snapshot.AuthoredHeightOutputId.IsValid())
	{
		Error = TEXT(
			"Legacy three-channel Height saves require reauthoring a named Float output and regenerating the world.");
		return false;
	}
	if (Saved.FormatVersion != FVoxelLandM4SavedWorld::AuthoredFormatVersion
		|| Saved.Identity.CanonicalBytes.Num() < FVoxelLandM4SavedWorldPreflightInternals::MinIdentityBytes
		|| Saved.Identity.CanonicalBytes.Num() > FVoxelLandM4SavedWorldPreflightInternals::MaxIdentityBytes)
	{
		Error = TEXT("M4 saved world identity exceeds admission limits or has an unsupported version");
		return false;
	}
	return ValidateSnapshot(Saved.Snapshot, Error);
}