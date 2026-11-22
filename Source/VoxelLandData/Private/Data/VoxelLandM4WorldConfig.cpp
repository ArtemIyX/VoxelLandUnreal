#include "Data/Assets/VoxelLandM4WorldConfig.h"

#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Hash/xxhash.h"
#include <limits>
#if WITH_EDITOR
#include "UObject/ObjectSaveContext.h"
#endif

#if WITH_EDITOR
void UVoxelLandM4WorldConfig::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	EnforceCookPreparation(Ar);
}

void UVoxelLandM4WorldConfig::EnforceCookPreparation(FArchive& Ar) const
{
	// Only reject the actual export write. Harvest and other archive traversals
	// may serialize the object but are not the package writer's error channel.
	const FObjectSavePackageSerializeContext* Context = Ar.GetSavePackageSerializeContext();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && Ar.IsSaving() && Ar.IsCooking() && Context
		&& Context->GetPhase() == EObjectSaveContextPhase::Write && !bCookPreparationSucceeded)
	{
		Ar.SetError();
	}
}

void UVoxelLandM4WorldConfig::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);
	if (ObjectSaveContext.IsLastConcurrentSave())
	{
		bCookPreparationSucceeded = false;
	}
}
#endif

namespace
{
constexpr int64 M4AssetHeightLimit = 512000000;
constexpr int64 M4AssetExactFloatLimit = 16777216;

// Inspect dependency assets before CaptureSnapshot copies them. The existing
// M3 capture verifies the function interface, recursion and source topology;
// this tighter M4 budget bounds all captured records for one control channel.
class FM4AssetFunctionPreflight
{
public:
	bool Run(const UVoxelLandTerrainGraph& Root, FString& OutError)
	{
		Error = &OutError;
		return VisitGraph(Root, 0);
	}

private:
	bool Fail(const TCHAR* Message) const
	{
		*Error = Message;
		return false;
	}

	bool VisitGraph(const UVoxelLandTerrainGraph& Graph, int32 Depth)
	{
		if (Depth > 16 || Graph.SourceVersion != 1 || Graph.Parameters.Num() > 128
			|| TotalNodes + Graph.Nodes.Num() > 128 || TotalLinks + Graph.Links.Num() > 2048)
		{
			return Fail(TEXT("M4 control function dependencies exceed graph limits."));
		}
		TotalNodes += Graph.Nodes.Num();
		TotalLinks += Graph.Links.Num();
		for (const FVoxelLandGraphParameter& Parameter : Graph.Parameters)
		{
			if (Parameter.Name.Len() > 128)
			{
				return Fail(TEXT("M4 function parameter name exceeds capture limits."));
			}
		}
		for (const FVoxelLandGraphNodeRecord& Node : Graph.Nodes)
		{
			if (!Node.Configuration.IsEmpty() || Node.Pins.Num() > 64 || TotalPins > 1024 - Node.Pins.Num())
			{
				return Fail(TEXT("M4 control function exceeds pin limits or uses unsupported operator configuration."));
			}
			TotalPins += Node.Pins.Num();
			for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
			{
				if (Pin.Name.Len() > 128)
				{
					return Fail(TEXT("M4 control pin name exceeds capture limits."));
				}
			}
			const bool bCall = Node.OperatorId == VoxelLandGraphFunctionCallId();
			if (!bCall && !Node.FunctionPath.IsNull())
			{
				return Fail(TEXT("Only an M4 graph function call may reference a function asset."));
			}
			if (!bCall)
			{
				continue;
			}
			if (Node.FunctionPath.IsNull() || Node.FunctionPath.ToString().Len() > 2048)
			{
				return Fail(TEXT("M4 function call has a missing or oversized asset path."));
			}
			UVoxelLandGraphFunction* Function = Cast<UVoxelLandGraphFunction>(Node.FunctionPath.TryLoad());
			if (!Function)
			{
				return Fail(TEXT("M4 control function is missing or has an incompatible class."));
			}
			const FString Path = Function->GetPathName();
			if (Visiting.Contains(Path))
			{
				return Fail(TEXT("Recursive M4 control function dependency detected."));
			}
			if (Visited.Contains(Path))
			{
				continue;
			}
			if (Visited.Num() + Visiting.Num() >= 16 || Function->Parameters.Num() > 63)
			{
				return Fail(TEXT("M4 control function count or interface exceeds capture limits."));
			}
			Visiting.Add(Path);
			if (!VisitGraph(*Function, Depth + 1))
			{
				return false;
			}
			Visiting.Remove(Path);
			Visited.Add(Path);
		}
		return true;
	}

	FString* Error = nullptr;
	int32 TotalNodes = 0;
	int32 TotalLinks = 0;
	int32 TotalPins = 0;
	TSet<FString> Visiting;
	TSet<FString> Visited;
};

// Bound every control before copying any of the three snapshots. Function
// expansion is bounded before capture; external providers remain unsupported.
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
		|| Control.CookedCurve.SourceKeys.Num() > 4097 || Control.CookedCurve.SamplesQ8.Num() > 4097
		|| Payload.Constants.Num() > 4096 || Payload.Parameters.Num() > 128 || Payload.Seeds.Num() > 256
		|| Payload.Noise.Num() > 256 || !Payload.ExternalOperators.IsEmpty()
		|| Payload.ExternalFilterIndex != INDEX_NONE)
	{
		Error = TEXT("M4 control exceeds capture limits or uses an unsupported external operator.");
		return false;
	}
	if (!FM4AssetFunctionPreflight().Run(Graph, Error))
	{
		return false;
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
	for (FVoxelLandGraphFunctionSnapshot& Function : Temp.Source.Functions)
	{
		for (FVoxelLandGraphParameter& Parameter : Function.Inputs)
		{
			Parameter.Tooltip = FText::GetEmpty();
		}
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
	Temp.CookedCurve = Control.CookedCurve;
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
	if (A.CookedCurve.BakeVersion != B.CookedCurve.BakeVersion || A.CookedCurve.SamplesHash != B.CookedCurve.SamplesHash
		|| A.CookedCurve.SamplesQ8 != B.CookedCurve.SamplesQ8
		|| A.CookedCurve.SourceKeys.Num() != B.CookedCurve.SourceKeys.Num())
	{
		return false;
	}
	for (int32 I = 0; I < A.CookedCurve.SourceKeys.Num(); ++I)
	{
		if (A.CookedCurve.SourceKeys[I].InputQ16 != B.CookedCurve.SourceKeys[I].InputQ16
			|| A.CookedCurve.SourceKeys[I].OutputQ8 != B.CookedCurve.SourceKeys[I].OutputQ8)
		{
			return false;
		}
	}
	return true;
}

int64 M4AssetCurveHash(TConstArrayView<int32> Samples)
{
	FCanonicalWriter Writer;
	for (int32 Value : Samples)
	{
		Writer.U32(uint32(Value));
	}
	const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
	int64 Bits = 0;
	FMemory::Memcpy(&Bits, &Hash, sizeof(Bits));
	return Bits;
}

bool M4AssetCookedValid(const FVoxelLandM4ControlConfig& Control)
{
	const FVoxelLandM4CookedCurve& Cooked = Control.CookedCurve;
	if (Cooked.BakeVersion != 1 || Cooked.SamplesQ8.Num() != 4097 || Cooked.SourceKeys.Num() < 2
		|| Cooked.SourceKeys.Num() != Control.CurveKeys.Num())
	{
		return false;
	}
	for (int32 I = 0; I < Cooked.SourceKeys.Num(); ++I)
	{
		if (Cooked.SourceKeys[I].InputQ16 != Control.CurveKeys[I].InputQ16
			|| Cooked.SourceKeys[I].OutputQ8 != Control.CurveKeys[I].OutputQ8)
		{
			return false;
		}
	}
	for (const int32 Value : Cooked.SamplesQ8)
	{
		if (int64(Value) < -M4AssetHeightLimit || int64(Value) > M4AssetHeightLimit)
		{
			return false;
		}
	}
	return Cooked.SamplesHash == M4AssetCurveHash(Cooked.SamplesQ8);
}
}

bool UVoxelLandM4WorldConfig::ValidateBakedCurves(FString& OutError) const
{
	OutError.Reset();
	if (!IsInGameThread() || !M4AssetCookedValid(Continental) || !M4AssetCookedValid(Mountain)
		|| !M4AssetCookedValid(Peaks))
	{
		OutError = TEXT("M4 cooked curves are missing, stale or have an unsupported bake version.");
		return false;
	}
	return true;
}

bool UVoxelLandM4WorldConfig::SetBakedCurves(
	TConstArrayView<int32> C, TConstArrayView<int32> M, TConstArrayView<int32> P, int32 BakeVersion, FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread() || BakeVersion != 1 || C.Num() != 4097 || M.Num() != 4097 || P.Num() != 4097
		|| !ValidateSettings(OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("Invalid M4 baked curves.");
		}
		return false;
	}
	const TConstArrayView<int32> Inputs[] = {C, M, P};
	FVoxelLandM4ControlConfig* Controls[] = {&Continental, &Mountain, &Peaks};
	FVoxelLandM4CookedCurve Results[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		for (const int32 Value : Inputs[Index])
		{
			if (int64(Value) < -M4AssetHeightLimit || int64(Value) > M4AssetHeightLimit)
			{
				OutError = TEXT("M4 baked curve sample is outside the supported range.");
				return false;
			}
		}
		Results[Index].BakeVersion = BakeVersion;
		Results[Index].SourceKeys = Controls[Index]->CurveKeys;
		Results[Index].SamplesQ8.Append(Inputs[Index].GetData(), Inputs[Index].Num());
		Results[Index].SamplesHash = M4AssetCurveHash(Results[Index].SamplesQ8);
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Controls[Index]->CookedCurve = MoveTemp(Results[Index]);
	}
	OutError.Reset();
	return true;
}

void UVoxelLandM4WorldConfig::ClearBakedCurves()
{
	check(IsInGameThread());
	Continental.CookedCurve = {};
	Mountain.CookedCurve = {};
	Peaks.CookedCurve = {};
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
	if (SmoothVersion != 1 || !FMath::IsFinite(SmoothStrength) || SmoothStrength < 0.f || SmoothStrength > 1.f)
	{
		OutError = TEXT("M4 smooth filter version or strength is invalid.");
		return false;
	}
	if (HeightDetail.AlgorithmVersion != 1 || HeightDetail.AmplitudeQ8 < 0 || HeightDetail.AmplitudeQ8 > 1280000
		|| !FMath::IsFinite(HeightDetail.FrequencyPerCm) || HeightDetail.FrequencyPerCm <= 0.f
		|| HeightDetail.FrequencyPerCm > 1.f || !HeightDetail.NodeId.IsValid())
	{
		OutError = TEXT("M4 height detail configuration is invalid.");
		return false;
	}
	if (FormatVersion != 1 || VoxelSizeCm < 1 || VoxelSizeCm > 2000000 || (bUnlimited && VoxelSizeCm > 134217)
		|| int64(BaseHeightQ8) < -M4AssetHeightLimit || int64(BaseHeightQ8) > M4AssetHeightLimit || HorizontalRadius < 1
		|| HorizontalRadius > 8 || VerticalRadius < 1 || VerticalRadius > 4)
	{
		OutError = TEXT("M4 voxel size, base height or demand radii are invalid.");
		return false;
	}
	if (!bUnlimited
		&& (CellsX < 1 || CellsX > 1024 || CellsY < 1 || CellsY > 1024 || GridSpacingCm < 1 || GridSpacingCm > 134217
			|| OriginXCm < -M4AssetExactFloatLimit || OriginYCm < -M4AssetExactFloatLimit
			|| OriginXCm > M4AssetExactFloatLimit || OriginYCm > M4AssetExactFloatLimit
			|| GridSpacingCm > (M4AssetExactFloatLimit - OriginXCm) / CellsX
			|| GridSpacingCm > (M4AssetExactFloatLimit - OriginYCm) / CellsY))
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
	if (bUnlimited && bGlobalErosion)
	{
		OutError = TEXT("Unlimited M4 worlds require global erosion to be disabled.");
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
	S.Domain.Mode = bUnlimited ? EVoxelLandM4DomainMode::Unlimited : EVoxelLandM4DomainMode::Finite;
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
	S.HeightDetail = HeightDetail;
	S.Smooth.Version = uint32(SmoothVersion);
	S.Smooth.bEnabled = bSmoothEnabled;
	S.Smooth.Strength = SmoothStrength;
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
		&& HeightDetail.bEnabled == S.HeightDetail.bEnabled
		&& HeightDetail.AlgorithmVersion == S.HeightDetail.AlgorithmVersion
		&& HeightDetail.AmplitudeQ8 == S.HeightDetail.AmplitudeQ8
		&& HeightDetail.FrequencyPerCm == S.HeightDetail.FrequencyPerCm && HeightDetail.NodeId == S.HeightDetail.NodeId
		&& HeightDetail.Salt == S.HeightDetail.Salt && SmoothVersion == int32(S.Smooth.Version)
		&& bSmoothEnabled == S.Smooth.bEnabled && (!bSmoothEnabled || SmoothStrength == S.Smooth.Strength)
		&& (bUnlimited ? S.Domain.Mode == EVoxelLandM4DomainMode::Unlimited
					   : S.Domain.Mode == EVoxelLandM4DomainMode::Finite)
		&& bGlobalErosion == S.Domain.bGlobalErosion && OriginXCm == S.Domain.OriginXCm
		&& OriginYCm == S.Domain.OriginYCm && CellsX == S.Domain.CellsX && CellsY == S.Domain.CellsY
		&& GridSpacingCm == S.Domain.GridSpacingCm && ErosionStageId == S.ErosionStageId && ErosionSalt == S.ErosionSalt
		&& DropletCount == S.DropletCount && MaxSteps == S.Erosion.MaxSteps
		&& ThermalIterations == S.Erosion.ThermalIterations && CapacityMaxQ8 == S.Erosion.CapacityMaxQ8
		&& TalusQ8 == S.Erosion.TalusQ8 && MaxTransferQ8 == S.Erosion.MaxTransferQ8
		&& bOverhangEnabled == S.bOverhangEnabled && OverhangAmplitudeQ8 == S.OverhangAmplitudeQ8
		&& OverhangBandQ8 == S.OverhangBandQ8 && OverhangFrequencyPerCm == S.OverhangFrequencyPerCm
		&& OverhangNodeId == S.OverhangNodeId && OverhangSalt == S.OverhangSalt
		&& HorizontalRadius == S.HorizontalRadius && VerticalRadius == S.VerticalRadius
		&& M4AssetSameControl(Continental, S.Continental) && M4AssetSameControl(Mountain, S.Mountain)
		&& M4AssetSameControl(Peaks, S.Peaks);
}
