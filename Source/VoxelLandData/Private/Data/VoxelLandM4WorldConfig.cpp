#include "Data/Assets/VoxelLandM4WorldConfig.h"

#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include <limits>

namespace
{
constexpr int64 M4AssetHeightLimit = 512000000;
constexpr int64 M4AssetExactFloatLimit = 16777216;

// Bound every control before copying any of the three snapshots. Function
// expansion and external operators are not supported by the current M4 2D
// control VM, so reject them before they can allocate dependency snapshots.
// These limits constrain copied records and strings; they are not a measurement
// of allocator, engine, or native-noise overhead and do not by themselves prove
// an exact process-memory bound.
bool M4AssetWithinCaptureLimits(const FVoxelLandM4ControlConfig& Control, FString& Error)
{
	if (!Control.Graph || Control.Graph->IsA<UVoxelLandGraphFunction>())
	{
		Error = TEXT("M4 control requires a supported terrain graph.");
		return false;
	}
	const UVoxelLandTerrainGraph& Graph = *Control.Graph;
	const FVoxelLandSerializedProgram& Payload = Graph.CompiledProgram;
	if (Graph.Nodes.Num() > 128 || Graph.Links.Num() > 2048 || Graph.Parameters.Num() > 128
		|| Control.Overrides.Num() > 128 || Control.CurveKeys.Num() > 4097 || Payload.Instructions.Num() > 2048
		|| Payload.Constants.Num() > 4096 || Payload.Parameters.Num() > 128 || Payload.Seeds.Num() > 256
		|| Payload.Noise.Num() > 256 || !Payload.ExternalOperators.IsEmpty()
		|| Payload.ExternalFilterIndex != INDEX_NONE)
	{
		Error = TEXT("M4 control exceeds capture limits or uses an unsupported external operator.");
		return false;
	}
	int32 PinCount = 0;
	for (const FVoxelLandGraphNodeRecord& Node : Graph.Nodes)
	{
		if (!Node.FunctionPath.IsNull() || !Node.Configuration.IsEmpty() || Node.Pins.Num() > 64
			|| PinCount > 1024 - Node.Pins.Num())
		{
			Error = TEXT("M4 control exceeds pin limits or uses an unsupported function or operator configuration.");
			return false;
		}
		PinCount += Node.Pins.Num();
		for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			if (Pin.Name.Len() > 128)
			{
				Error = TEXT("M4 control pin name exceeds capture limits.");
				return false;
			}
		}
	}
	for (const FVoxelLandGraphParameter& Parameter : Graph.Parameters)
	{
		if (Parameter.Name.Len() > 128)
		{
			Error = TEXT("M4 control parameter name exceeds capture limits.");
			return false;
		}
	}
	for (const FVoxelLandSerializedParameter& Parameter : Payload.Parameters)
	{
		if (Parameter.Name.Len() > 128)
		{
			Error = TEXT("M4 cooked parameter name exceeds capture limits.");
			return false;
		}
	}
	return true;
}

bool M4AssetParameterValid(const FVoxelLandGraphParameter& P)
{
	if (!P.Id.IsValid() || P.Name.IsEmpty())
	{
		return false;
	}
	for (int32 I = 0; I < P.Name.Len(); ++I)
	{
		const TCHAR C = P.Name[I];
		if (!((C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || C == TEXT('_')
				|| (I > 0 && C >= TEXT('0') && C <= TEXT('9'))))
		{
			return false;
		}
	}
	switch (P.Type)
	{
		case EVoxelLandGraphValueType::Float:
			return FMath::IsFinite(P.FloatDefault)
				&& (!P.bHasRange
					|| (FMath::IsFinite(P.FloatMin) && FMath::IsFinite(P.FloatMax) && P.FloatMin <= P.FloatMax
						&& P.FloatDefault >= P.FloatMin && P.FloatDefault <= P.FloatMax));
		case EVoxelLandGraphValueType::Int32:
			return !P.bHasRange || (P.IntMin <= P.IntMax && P.IntDefault >= P.IntMin && P.IntDefault <= P.IntMax);
		case EVoxelLandGraphValueType::Bool:
			return !P.bHasRange;
		default:
			return false;
	}
}

bool M4AssetCaptureControl(const FVoxelLandM4ControlConfig& Control, FVoxelLandM4ControlSnapshot& Out, FString& Error)
{
	if (!Control.Graph || Control.Graph->IsA<UVoxelLandGraphFunction>() || Control.Graph->SourceVersion != 1)
	{
		Error = TEXT("M4 control requires a supported terrain graph.");
		return false;
	}
	const FVoxelLandSerializedProgram& Payload = Control.Graph->CompiledProgram;
	if (Control.Graph->Parameters.Num() > 4096 || Control.Overrides.Num() > 4096 || Payload.Instructions.Num() > 8192
		|| Payload.Constants.Num() > 65536 || Payload.Parameters.Num() > 4096 || Payload.Seeds.Num() > 4096
		|| Payload.Noise.Num() > 4096 || Payload.ExternalOperators.Num() > 4096)
	{
		Error = TEXT("M4 control graph or cooked program exceeds record limits.");
		return false;
	}
	int64 ConfigurationBytes = 0;
	for (const FVoxelLandExternalOperator& Operator : Payload.ExternalOperators)
	{
		ConfigurationBytes += Operator.Configuration.Num();
		if (Operator.Configuration.Num() > 65536 || ConfigurationBytes > 1048576 || Operator.Inputs.Num() > 3)
		{
			Error = TEXT("M4 control provider configuration exceeds record limits.");
			return false;
		}
	}
	if (Control.CurveKeys.Num() < 2 || Control.CurveKeys.Num() > 65536 || Control.CurveKeys[0].InputQ16 != 0
		|| Control.CurveKeys.Last().InputQ16 != 65535)
	{
		Error = TEXT("M4 curve needs ordered keys including input endpoints 0 and 65535.");
		return false;
	}
	for (int32 I = 0; I < Control.CurveKeys.Num(); ++I)
	{
		const FVoxelLandM4CurveAuthoringKey& K = Control.CurveKeys[I];
		if (K.InputQ16 < 0 || K.InputQ16 > 65535 || int64(K.OutputQ8) < -M4AssetHeightLimit
			|| int64(K.OutputQ8) > M4AssetHeightLimit || (I > 0 && Control.CurveKeys[I - 1].InputQ16 >= K.InputQ16))
		{
			Error = TEXT("M4 curve has duplicate, unordered or out-of-range keys.");
			return false;
		}
	}
	FVoxelLandM4ControlSnapshot Temp;
	if (!Control.Graph->CaptureSnapshot(Temp.Source, Error))
	{
		return false;
	}
	// Tooltips are editor presentation, are not used by the compiler or source
	// fingerprint, and must not retain arbitrary localized text in worker input.
	for (FVoxelLandGraphParameter& Parameter : Temp.Source.Parameters)
	{
		Parameter.Tooltip = FText::GetEmpty();
	}
	TMap<FGuid, const FVoxelLandGraphParameter*> Parameters;
	TSet<FString> Names;
	for (const FVoxelLandGraphParameter& P : Temp.Source.Parameters)
	{
		if (!M4AssetParameterValid(P) || Parameters.Contains(P.Id) || Names.Contains(P.Name.ToLower()))
		{
			Error = TEXT("M4 control graph has invalid or duplicate parameter declarations.");
			return false;
		}
		Parameters.Add(P.Id, &P);
		Names.Add(P.Name.ToLower());
	}
	TSet<FGuid> Seen;
	for (const FVoxelLandParameterOverride& O : Control.Overrides)
	{
		const FVoxelLandGraphParameter* const* P = Parameters.Find(O.ParameterId);
		if (!P || Seen.Contains(O.ParameterId) || O.Type != (*P)->Type
			|| (O.Type == EVoxelLandGraphValueType::Float
				&& (!FMath::IsFinite(O.FloatValue)
					|| ((*P)->bHasRange && (O.FloatValue < (*P)->FloatMin || O.FloatValue > (*P)->FloatMax))))
			|| (O.Type == EVoxelLandGraphValueType::Int32 && (*P)->bHasRange
				&& (O.IntValue < (*P)->IntMin || O.IntValue > (*P)->IntMax)))
		{
			Error = TEXT("M4 control override has an unknown or duplicate ID, incompatible type, or invalid value.");
			return false;
		}
		Seen.Add(O.ParameterId);
	}
	Temp.Payload = Payload;
	Temp.Overrides = Control.Overrides;
	Temp.CurveKeys = Control.CurveKeys;
	Out = MoveTemp(Temp);
	return true;
}

bool M4AssetSameControl(const FVoxelLandM4ControlConfig& A, const FVoxelLandM4ControlSnapshot& B)
{
	if (!A.Graph || A.Overrides.Num() != B.Overrides.Num() || A.CurveKeys.Num() != B.CurveKeys.Num())
	{
		return false;
	}
	for (int32 I = 0; I < A.Overrides.Num(); ++I)
	{
		const FVoxelLandParameterOverride& L = A.Overrides[I];
		const FVoxelLandParameterOverride& R = B.Overrides[I];
		if (L.ParameterId != R.ParameterId || L.Type != R.Type)
		{
			return false;
		}
		switch (L.Type)
		{
			case EVoxelLandGraphValueType::Float:
				if (L.FloatValue != R.FloatValue)
				{
					return false;
				}
				break;
			case EVoxelLandGraphValueType::Int32:
				if (L.IntValue != R.IntValue)
				{
					return false;
				}
				break;
			case EVoxelLandGraphValueType::Bool:
				if (L.BoolValue != R.BoolValue)
				{
					return false;
				}
				break;
			default:
				return false;
		}
	}
	for (int32 I = 0; I < A.CurveKeys.Num(); ++I)
	{
		if (A.CurveKeys[I].InputQ16 != B.CurveKeys[I].InputQ16 || A.CurveKeys[I].OutputQ8 != B.CurveKeys[I].OutputQ8)
		{
			return false;
		}
	}
	return true;
}
}

bool UVoxelLandM4WorldConfig::ValidateSettings(FString& OutError) const
{
	FVoxelLandM4WorldSnapshot Snapshot;
	return CaptureSnapshot(Snapshot, OutError);
}

bool UVoxelLandM4WorldConfig::CaptureSnapshot(FVoxelLandM4WorldSnapshot& OutSnapshot, FString& OutError) const
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("M4 settings must be captured on the game thread.");
		return false;
	}
	if (FormatVersion != 1 || VoxelSizeCm < 1 || VoxelSizeCm > 2000000 || int64(BaseHeightQ8) < -M4AssetHeightLimit
		|| int64(BaseHeightQ8) > M4AssetHeightLimit || HorizontalRadius < 1 || HorizontalRadius > 8
		|| VerticalRadius < 1 || VerticalRadius > 4 || CellsX < 1 || CellsX > 1024 || CellsY < 1 || CellsY > 1024
		|| GridSpacingCm < 1 || GridSpacingCm > 134217 || OriginXCm < -M4AssetExactFloatLimit
		|| OriginYCm < -M4AssetExactFloatLimit || OriginXCm > M4AssetExactFloatLimit
		|| OriginYCm > M4AssetExactFloatLimit || GridSpacingCm > (M4AssetExactFloatLimit - OriginXCm) / CellsX
		|| GridSpacingCm > (M4AssetExactFloatLimit - OriginYCm) / CellsY)
	{
		OutError = TEXT(
			"M4 domain, voxel size, base height or demand radii are invalid or outside exact control-grid coordinates.");
		return false;
	}
	if (DropletCount < 0 || DropletCount > 65536 || MaxSteps < 0 || MaxSteps > 128 || ThermalIterations < 0
		|| ThermalIterations > 32 || CapacityMaxQ8 < 0 || CapacityMaxQ8 > M4AssetHeightLimit || TalusQ8 < 0
		|| TalusQ8 > M4AssetHeightLimit || MaxTransferQ8 < 0 || MaxTransferQ8 > M4AssetHeightLimit || ErosionSalt < 0
		|| (bGlobalErosion && !ErosionStageId.IsValid()))
	{
		OutError = TEXT("M4 erosion configuration is invalid.");
		return false;
	}
	if (OverhangAmplitudeQ8 < 0 || OverhangAmplitudeQ8 > 128000 || OverhangBandQ8 < 1 || OverhangBandQ8 > 1280000
		|| OverhangAmplitudeQ8 > OverhangBandQ8 || !FMath::IsFinite(OverhangFrequencyPerCm)
		|| OverhangFrequencyPerCm <= 0.f || OverhangFrequencyPerCm > 1.f || OverhangSalt < 0
		|| !OverhangNodeId.IsValid())
	{
		OutError = TEXT("M4 overhang configuration is invalid.");
		return false;
	}
	// Check all three graphs before capturing any: a later oversized channel must
	// not cause earlier snapshot copies or function loads as a side effect.
	if (!M4AssetWithinCaptureLimits(Continental, OutError) || !M4AssetWithinCaptureLimits(Mountain, OutError)
		|| !M4AssetWithinCaptureLimits(Peaks, OutError))
	{
		return false;
	}
	FVoxelLandM4WorldSnapshot S;
	if (!M4AssetCaptureControl(Continental, S.Continental, OutError)
		|| !M4AssetCaptureControl(Mountain, S.Mountain, OutError) || !M4AssetCaptureControl(Peaks, S.Peaks, OutError))
	{
		return false;
	}
	S.FormatVersion = FormatVersion;
	S.Seed = Seed;
	S.VoxelSizeCm = VoxelSizeCm;
	S.BaseHeightQ8 = BaseHeightQ8;
	S.Domain.Mode = EVoxelLandM4DomainMode::Finite;
	S.Domain.bGlobalErosion = bGlobalErosion;
	S.Domain.OriginXCm = OriginXCm;
	S.Domain.OriginYCm = OriginYCm;
	S.Domain.CellsX = CellsX;
	S.Domain.CellsY = CellsY;
	S.Domain.GridSpacingCm = GridSpacingCm;
	S.ErosionStageId = ErosionStageId;
	S.ErosionSalt = ErosionSalt;
	S.DropletCount = DropletCount;
	S.Erosion.MaxSteps = MaxSteps;
	S.Erosion.ThermalIterations = ThermalIterations;
	S.Erosion.CapacityMaxQ8 = CapacityMaxQ8;
	S.Erosion.TalusQ8 = TalusQ8;
	S.Erosion.MaxTransferQ8 = MaxTransferQ8;
	S.bNormalizeSlope = bNormalizeSlope;
	S.bOverhangEnabled = bOverhangEnabled;
	S.OverhangAmplitudeQ8 = OverhangAmplitudeQ8;
	S.OverhangBandQ8 = OverhangBandQ8;
	S.OverhangFrequencyPerCm = OverhangFrequencyPerCm;
	S.OverhangNodeId = OverhangNodeId;
	S.OverhangSalt = OverhangSalt;
	S.HorizontalRadius = HorizontalRadius;
	S.VerticalRadius = VerticalRadius;
	S.MaterialSetupPath = MaterialSetup.ToSoftObjectPath();
	OutSnapshot = MoveTemp(S);
	return true;
}

bool UVoxelLandM4WorldConfig::MatchesSnapshot(const FVoxelLandM4WorldSnapshot& S) const
{
	return IsInGameThread() && FormatVersion == S.FormatVersion && Seed == S.Seed && VoxelSizeCm == S.VoxelSizeCm
		&& BaseHeightQ8 == S.BaseHeightQ8 && bNormalizeSlope == S.bNormalizeSlope
		&& S.Domain.Mode == EVoxelLandM4DomainMode::Finite && bGlobalErosion == S.Domain.bGlobalErosion
		&& OriginXCm == S.Domain.OriginXCm && OriginYCm == S.Domain.OriginYCm && CellsX == S.Domain.CellsX
		&& CellsY == S.Domain.CellsY && GridSpacingCm == S.Domain.GridSpacingCm && ErosionStageId == S.ErosionStageId
		&& ErosionSalt == S.ErosionSalt && DropletCount == S.DropletCount && MaxSteps == S.Erosion.MaxSteps
		&& ThermalIterations == S.Erosion.ThermalIterations && CapacityMaxQ8 == S.Erosion.CapacityMaxQ8
		&& TalusQ8 == S.Erosion.TalusQ8 && MaxTransferQ8 == S.Erosion.MaxTransferQ8
		&& bOverhangEnabled == S.bOverhangEnabled && OverhangAmplitudeQ8 == S.OverhangAmplitudeQ8
		&& OverhangBandQ8 == S.OverhangBandQ8 && OverhangFrequencyPerCm == S.OverhangFrequencyPerCm
		&& OverhangNodeId == S.OverhangNodeId && OverhangSalt == S.OverhangSalt
		&& HorizontalRadius == S.HorizontalRadius && VerticalRadius == S.VerticalRadius
		&& M4AssetSameControl(Continental, S.Continental) && M4AssetSameControl(Mountain, S.Mountain)
		&& M4AssetSameControl(Peaks, S.Peaks);
}
