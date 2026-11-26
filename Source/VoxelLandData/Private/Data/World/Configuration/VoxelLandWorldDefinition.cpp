#include "Data/Assets/Worlds/VoxelLandWorldDefinition.h"

#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Assets/Worlds/VoxelLandInfiniteWorld.h"
#include "Data/Assets/Graphs/VoxelLandGraphFunction.h"
#include "Data/Terrain/Controls/VoxelLandControlGraph.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandGraphFunctionId.h"
#include "Data/Terrain/Controls/VoxelLandTerrainControlChannelKind.h"
#include "Data/Caves/Planning/VoxelLandCavePlanningMode.h"
#include "Data/Graph/Sources/VoxelLandGraphFunctionSnapshot.h"
#include "Data/World/Configuration/VoxelLandWorldCommonSnapshot.h"
#include "Data/World/Configuration/VoxelLandWorldSourceSnapshot.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "UObject/ObjectSaveContext.h"
#endif

TSharedPtr<const FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe> UVoxelLandWorldSource::CaptureSource(
	FString& OutError) const
{
	if (!IsInGameThread())
	{
		OutError = TEXT("World settings must be captured on the game thread.");
		return nullptr;
	}
	if (const auto* Finite = Cast<UVoxelLandFiniteWorldSource>(this))
	{
		FVoxelLandFiniteWorldSnapshot Snapshot;
		if (Finite->CaptureSnapshot(Snapshot, OutError))
		{
			return MakeShared<FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe>(MoveTemp(Snapshot));
		}
		return nullptr;
	}
	if (const auto* Infinite = Cast<UVoxelLandInfiniteWorldSource>(this))
	{
		FVoxelLandInfiniteWorldSnapshot Snapshot;
		if (Infinite->CaptureSnapshot(Snapshot, OutError))
		{
			return MakeShared<FVoxelLandWorldSourceSnapshot, ESPMode::ThreadSafe>(MoveTemp(Snapshot));
		}
		return nullptr;
	}
	OutError = TEXT("Unsupported world definition type.");
	return nullptr;
}

bool UVoxelLandWorldSource::HasRepresentableVerticalExtent(int64 Minimum, int64 Maximum)
{
	return Minimum < Maximum && (Minimum >= 0 || Maximum <= MAX_int64 + Minimum);
}

const TCHAR* UVoxelLandWorldSource::GetPreparationBlocker() const
{
	if (IsA<UVoxelLandFiniteWorldSource>())
	{
		return TEXT("Finite world preparation is blocked on a metered bounded prepass and final-field certification. "
					"Cooked reconstruction also requires a current descriptor.");
	}
	if (IsA<UVoxelLandInfiniteWorldSource>())
	{
		return TEXT("Infinite world preparation is blocked on metered local sampling and certified neighboring plan "
					"sets. Cooked reconstruction also requires a current descriptor.");
	}
	return TEXT("Unsupported world definition type.");
}

void UVoxelLandWorldSource::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
#if WITH_EDITOR
	EnforceCookPreparation(Ar);
#endif
}

#if WITH_EDITOR
EDataValidationResult UVoxelLandWorldSource::IsDataValid(FDataValidationContext& Context) const
{
	if (!IsInGameThread())
	{
		Context.AddWarning(FText::FromString(TEXT("World source validation requires the game thread.")));
		return EDataValidationResult::NotValidated;
	}
	FString Error;
	if (!CaptureSource(Error))
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	Context.AddWarning(FText::FromString(GetPreparationBlocker()));
	return EDataValidationResult::NotValidated;
}

void UVoxelLandWorldSource::EnforceCookPreparation(FArchive& Ar) const
{
	const FObjectSavePackageSerializeContext* Context = Ar.GetArchiveState().GetSavePackageSerializeContext();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && Ar.IsSaving() && Ar.IsCooking() && Context
		&& Context->GetPhase() == EObjectSaveContextPhase::Write)
	{
		// Current world definitions have no cooked generator reconstruction path.
		// Do not publish a valid-looking but unusable world into a cooked build.
		Ar.SetError();
	}
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
class FVoxelLandWorldDefinitionLoadHook final
{
public:
	inline static TFunction<void()> WorldDefinitionBeforeLoadHook;
};

void VoxelLandWorldDefinitionSetBeforeLoadHook(TFunction<void()>&& Hook)
{
	check(IsInGameThread());
	FVoxelLandWorldDefinitionLoadHook::WorldDefinitionBeforeLoadHook = MoveTemp(Hook);
}
#endif

class FVoxelLandWorldDefinitionValidation final
{
public:
	static bool ValidateGenerationSettings(const FVoxelLandWorldCommonSettings& Authored, FString& OutError);
	static bool CaptureSharedHeightStage(
		const FVoxelLandWorldCommonSettings& Authored, FVoxelLandM4HeightStageManifest& HeightStage, FString& OutError);
	static void CopyGenerationSettings(const FVoxelLandWorldCommonSettings& Authored, EVoxelLandWorldKind Kind,
		FVoxelLandWorldCommonSnapshot& Scratch);

	static constexpr int32 WorldDefinitionMaxRecords = 128;
	static constexpr int32 WorldDefinitionMaxInstructions = 2048;
	static constexpr int32 WorldDefinitionMaxConstants = 4096;
	static constexpr int64 WorldDefinitionHeightLimitQ8 = 512000000;

	// A stamp owns bounded copies and a strong reference. No node, parameter or
	// program reference from a live graph is kept across a function load.
	class FWorldDefinitionGraphStamp
	{
	public:
		explicit FWorldDefinitionGraphStamp(UVoxelLandTerrainGraph& Graph)
			: Asset(&Graph)
			, AssetPath(&Graph)
		{
		}

		bool Capture(FString& Error)
		{
			const UVoxelLandTerrainGraph& Graph = *Asset.Get();
			if (Graph.Parameters.Num() > 128 || Graph.Nodes.Num() > 128 || Graph.Links.Num() > 2048)
			{
				Error = TEXT("Control graph exceeds capture limits.");
				return false;
			}
			int64 ConfigurationBytes = 0;
			int32 PinCount = 0;
			TSet<FGuid> ParameterIds;
			TSet<FString> ParameterNames;
			for (const FVoxelLandGraphParameter& Parameter : Graph.Parameters)
			{
				if (!FVoxelLandWorldDefinitionValidation::WorldDefinitionParameterValid(Parameter)
					|| ParameterIds.Contains(Parameter.Id) || ParameterNames.Contains(Parameter.Name.ToLower()))
				{
					Error = TEXT("Control graph or function parameters are invalid or duplicated.");
					return false;
				}
				ParameterIds.Add(Parameter.Id);
				ParameterNames.Add(Parameter.Name.ToLower());
			}
			for (const auto& Node : Graph.Nodes)
			{
				const FTopLevelAssetPath FunctionAssetPath = Node.FunctionPath.GetAssetPath();
				const uint64 PathLength = uint64(FunctionAssetPath.GetPackageName().GetStringLength())
										+ uint64(FunctionAssetPath.GetAssetName().GetStringLength())
										+ uint64(Node.FunctionPath.GetSubPathUtf8String().Len()) + 2;
				if (Node.Configuration.Num() > 65536 || ConfigurationBytes > 1048576 - Node.Configuration.Num()
					|| Node.Pins.Num() > 64 || Node.Pins.Num() > 1024 - PinCount || PathLength > 2048)
				{
					Error = TEXT("Control graph pins or configuration exceed capture limits.");
					return false;
				}
				ConfigurationBytes += Node.Configuration.Num();
				PinCount += Node.Pins.Num();
				for (const auto& Pin : Node.Pins)
				{
					if (Pin.Name.Len() > 128)
					{
						Error = TEXT("Control graph pin name exceeds capture limits.");
						return false;
					}
				}
			}
			Source.SourceVersion = Graph.SourceVersion;
			Source.Parameters.Reserve(Graph.Parameters.Num());
			for (const FVoxelLandGraphParameter& Parameter : Graph.Parameters)
			{
				// Do not retain or evaluate editor text histories in the native snapshot.
				auto& Copy = Source.Parameters.AddDefaulted_GetRef();
				Copy.Id = Parameter.Id;
				Copy.Name = Parameter.Name;
				Copy.Type = Parameter.Type;
				Copy.FloatDefault = Parameter.FloatDefault;
				Copy.IntDefault = Parameter.IntDefault;
				Copy.BoolDefault = Parameter.BoolDefault;
				Copy.bHasRange = Parameter.bHasRange;
				Copy.FloatMin = Parameter.FloatMin;
				Copy.FloatMax = Parameter.FloatMax;
				Copy.IntMin = Parameter.IntMin;
				Copy.IntMax = Parameter.IntMax;
			}
			Source.Nodes = Graph.Nodes;
			Source.Links = Graph.Links;
			if (const auto* Function = Cast<UVoxelLandGraphFunction>(&Graph))
			{
				bFunction = true;
				FunctionOutputType = Function->OutputType;
				FunctionOutputNode = Function->OutputNode;
				FunctionOutputPin = Function->OutputPin;
			}
			if (const auto* Named = Cast<UVoxelLandM4ControlGraph>(&Graph))
			{
				bNamed = true;
				HeightOutput = Named->HeightOutputId;
				ContinentalOutput = Named->ContinentalOutputId;
				MountainOutput = Named->MountainOutputId;
				PeaksOutput = Named->PeaksOutputId;
			}
			return true;
		}

		bool IsCurrent() const
		{
			const UVoxelLandTerrainGraph* Graph = Asset.Get();
			if (!Graph || FSoftObjectPath(Graph) != AssetPath || Graph->SourceVersion != Source.SourceVersion
				|| Graph->Parameters.Num() != Source.Parameters.Num() || Graph->Nodes.Num() != Source.Nodes.Num()
				|| Graph->Links.Num() != Source.Links.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < Source.Parameters.Num(); ++Index)
			{
				const auto& A = Graph->Parameters[Index];
				const auto& B = Source.Parameters[Index];
				if (A.Id != B.Id || A.Name != B.Name || A.Type != B.Type || A.FloatDefault != B.FloatDefault
					|| A.IntDefault != B.IntDefault || A.BoolDefault != B.BoolDefault || A.bHasRange != B.bHasRange
					|| A.FloatMin != B.FloatMin || A.FloatMax != B.FloatMax || A.IntMin != B.IntMin
					|| A.IntMax != B.IntMax)
				{
					return false;
				}
			}
			for (int32 Index = 0; Index < Source.Nodes.Num(); ++Index)
			{
				if (!FVoxelLandGraphNodeRecord::StaticStruct()->CompareScriptStruct(
						&Graph->Nodes[Index], &Source.Nodes[Index], 0))
				{
					return false;
				}
			}
			for (int32 Index = 0; Index < Source.Links.Num(); ++Index)
			{
				if (!FVoxelLandGraphLinkRecord::StaticStruct()->CompareScriptStruct(
						&Graph->Links[Index], &Source.Links[Index], 0))
				{
					return false;
				}
			}
			if (bFunction)
			{
				const auto* Function = Cast<UVoxelLandGraphFunction>(Graph);
				if (!Function || Function->OutputType != FunctionOutputType
					|| Function->OutputNode != FunctionOutputNode || Function->OutputPin != FunctionOutputPin)
				{
					return false;
				}
			}
			if (bNamed)
			{
				const auto* Named = Cast<UVoxelLandM4ControlGraph>(Graph);
				if (!Named || Named->HeightOutputId != HeightOutput || Named->ContinentalOutputId != ContinentalOutput
					|| Named->MountainOutputId != MountainOutput || Named->PeaksOutputId != PeaksOutput)
				{
					return false;
				}
			}
			return true;
		}

		TStrongObjectPtr<UVoxelLandTerrainGraph> Asset;
		FSoftObjectPath AssetPath;
		FVoxelLandGraphSnapshot Source;
		bool bFunction = false;
		bool bNamed = false;
		EVoxelLandGraphValueType FunctionOutputType = EVoxelLandGraphValueType::Float;
		FGuid FunctionOutputNode;
		FGuid FunctionOutputPin;
		FGuid HeightOutput;
		FGuid ContinentalOutput;
		FGuid MountainOutput;
		FGuid PeaksOutput;
	};

	class FWorldDefinitionFunctionPreflight
	{
	public:
		bool PrepareRoot(UVoxelLandTerrainGraph& Graph, FString& OutError)
		{
			Error = &OutError;
			if (!AddGraph(Graph))
			{
				return false;
			}
			return Graph.ValidateStructure(OutError);
		}

		bool Run(TFunctionRef<bool()> IsCaptureCurrent)
		{
			Captured = Stamps[0]->Source;
			int32 Height = 0;
			return Visit(Captured.Nodes, 0, Height, IsCaptureCurrent);
		}

		const FVoxelLandGraphSnapshot& GetCaptured() const { return Captured; }

		bool IsCurrent() const
		{
			for (const auto& Stamp : Stamps)
			{
				if (!Stamp->IsCurrent())
				{
					return false;
				}
			}
			return true;
		}

	private:
		bool Fail(const TCHAR* Message) const
		{
			*Error = Message;
			return false;
		}

		bool AddGraph(UVoxelLandTerrainGraph& Graph)
		{
			if (Graph.SourceVersion != 1 || Graph.Nodes.Num() > 128 - TotalNodes
				|| Graph.Links.Num() > 2048 - TotalLinks)
			{
				return Fail(TEXT("Control graph dependencies exceed capture limits."));
			}
			for (const auto& Node : Graph.Nodes)
			{
				if (TotalConfig > 1048576 - Node.Configuration.Num() || Node.Pins.Num() > 1024 - TotalPins)
				{
					return Fail(TEXT("Control graph pins or operator configuration exceed capture limits."));
				}
				TotalConfig += Node.Configuration.Num();
				TotalPins += Node.Pins.Num();
			}
			TUniquePtr<FWorldDefinitionGraphStamp> Stamp = MakeUnique<FWorldDefinitionGraphStamp>(Graph);
			if (!Stamp->Capture(*Error))
			{
				return false;
			}
			TotalNodes += Graph.Nodes.Num();
			TotalLinks += Graph.Links.Num();
			Stamps.Add(MoveTemp(Stamp));
			return true;
		}

		bool Visit(TArray<FVoxelLandGraphNodeRecord>& CapturedNodes, int32 Depth, int32& OutHeight,
			TFunctionRef<bool()> IsCaptureCurrent)
		{
			OutHeight = 0;
			if (Depth > 16)
			{
				return Fail(TEXT("Control graph dependencies exceed capture limits."));
			}
			for (auto& Node : CapturedNodes)
			{
				const bool bCall = Node.OperatorId == VoxelLandGraphFunctionCallId();
				if (!bCall && !Node.FunctionPath.IsNull())
				{
					return Fail(TEXT("Only a graph function call may reference a function asset."));
				}
				if (!bCall)
				{
					continue;
				}
				if (Node.FunctionPath.IsNull() || Node.FunctionPath.ToString().Len() > 2048)
				{
					return Fail(TEXT("Function call has a missing or oversized asset path."));
				}
#if WITH_DEV_AUTOMATION_TESTS
				if (FVoxelLandWorldDefinitionLoadHook::WorldDefinitionBeforeLoadHook)
				{
					FVoxelLandWorldDefinitionLoadHook::WorldDefinitionBeforeLoadHook();
				}
#endif
				TStrongObjectPtr<UVoxelLandGraphFunction> Function(
					Cast<UVoxelLandGraphFunction>(Node.FunctionPath.TryLoad()));
				if (!IsCaptureCurrent())
				{
					return Fail(TEXT("World graph or settings changed during function loading."));
				}
				if (!Function.Get())
				{
					*Error = FString::Printf(
						TEXT("Control graph function is unavailable: %s."), *Node.FunctionPath.ToString());
					return false;
				}
				const FString Path = Function->GetPathName();
				Node.FunctionPath = FSoftObjectPath(Function.Get());
				if (Visiting.Contains(Path))
				{
					*Error = FString::Printf(TEXT("Recursive control graph function dependency: %s."), *Path);
					return false;
				}
				if (const int32* ExistingHeight = Heights.Find(Path))
				{
					if (Depth + *ExistingHeight > 16)
					{
						return Fail(TEXT("Control graph function nesting exceeds capture limits."));
					}
					OutHeight = FMath::Max(OutHeight, *ExistingHeight);
					continue;
				}
				if (Heights.Num() + Visiting.Num() >= 16)
				{
					return Fail(TEXT("Control graph function count exceeds capture limits."));
				}
				constexpr int32 MaxFunctionInputs = 63;
				if (Function->Parameters.Num() > MaxFunctionInputs)
				{
					return Fail(TEXT("Function input count exceeds capture limits."));
				}
				if (!AddGraph(*Function.Get()))
				{
					return false;
				}
				const FWorldDefinitionGraphStamp& FunctionStamp = *Stamps.Last();
				FVoxelLandGraphFunctionSnapshot Copy;
				Copy.AssetPath = Path;
				Copy.SourceVersion = FunctionStamp.Source.SourceVersion;
				Copy.Inputs = FunctionStamp.Source.Parameters;
				Copy.OutputType = FunctionStamp.FunctionOutputType;
				Copy.OutputNode = FunctionStamp.FunctionOutputNode;
				Copy.OutputPin = FunctionStamp.FunctionOutputPin;
				Copy.Nodes = FunctionStamp.Source.Nodes;
				Copy.Links = FunctionStamp.Source.Links;
				Visiting.Add(Path);
				int32 ChildHeight = 0;
				if (!Visit(Copy.Nodes, Depth + 1, ChildHeight, IsCaptureCurrent))
				{
					return false;
				}
				Visiting.Remove(Path);
				const int32 FunctionHeight = 1 + ChildHeight;
				Heights.Add(Path, FunctionHeight);
				OutHeight = FMath::Max(OutHeight, FunctionHeight);
				Captured.Functions.Add(MoveTemp(Copy));
			}
			return true;
		}

		FString* Error = nullptr;
		int32 TotalNodes = 0;
		int32 TotalLinks = 0;
		int32 TotalPins = 0;
		int64 TotalConfig = 0;
		TSet<FString> Visiting;
		TMap<FString, int32> Heights;
		TArray<TUniquePtr<FWorldDefinitionGraphStamp>> Stamps;
		FVoxelLandGraphSnapshot Captured;
	};

	static bool WorldDefinitionParameterValid(const FVoxelLandGraphParameter& Parameter)
	{
		if (!Parameter.Id.IsValid() || Parameter.Name.IsEmpty() || Parameter.Name.Len() > 128)
		{
			return false;
		}
		for (int32 Index = 0; Index < Parameter.Name.Len(); ++Index)
		{
			const TCHAR C = Parameter.Name[Index];
			if (!((C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || C == TEXT('_')
					|| (Index > 0 && C >= TEXT('0') && C <= TEXT('9'))))
			{
				return false;
			}
		}
		switch (Parameter.Type)
		{
			case EVoxelLandGraphValueType::Float:
				return FMath::IsFinite(Parameter.FloatDefault)
					&& (!Parameter.bHasRange
						|| (FMath::IsFinite(Parameter.FloatMin) && FMath::IsFinite(Parameter.FloatMax)
							&& Parameter.FloatMin <= Parameter.FloatMax && Parameter.FloatDefault >= Parameter.FloatMin
							&& Parameter.FloatDefault <= Parameter.FloatMax));
			case EVoxelLandGraphValueType::Int32:
				return !Parameter.bHasRange
					|| (Parameter.IntMin <= Parameter.IntMax && Parameter.IntDefault >= Parameter.IntMin
						&& Parameter.IntDefault <= Parameter.IntMax);
			case EVoxelLandGraphValueType::Bool:
				return !Parameter.bHasRange;
			default:
				return false;
		}
	}

	static bool WorldDefinitionPreflightControl(const FVoxelLandM4ControlConfig& Control,
		EVoxelLandM4ControlChannel Channel, FVoxelLandSerializedProgram& OutProgram, FString& Error)
	{
		if (!Control.Graph || Control.Graph->IsA<UVoxelLandGraphFunction>())
		{
			Error = TEXT("Each control requires a terrain graph asset.");
			return false;
		}
		const UVoxelLandM4ControlGraph* Named = Cast<UVoxelLandM4ControlGraph>(Control.Graph);
		const FVoxelLandSerializedProgram& Program =
			Named ? Named->GetProgram(Channel) : Control.Graph->CompiledProgram;
		if (Control.Graph->Parameters.Num() > WorldDefinitionMaxRecords
			|| Control.Overrides.Num() > WorldDefinitionMaxRecords
			|| Program.Instructions.Num() > WorldDefinitionMaxInstructions
			|| Program.Constants.Num() > WorldDefinitionMaxConstants
			|| Program.Parameters.Num() > WorldDefinitionMaxRecords || Program.Seeds.Num() > 256
			|| Program.Noise.Num() > 256 || Program.ExternalOperators.Num() > 128 || Control.CurveKeys.Num() < 2
			|| Control.CurveKeys.Num() > 4097 || Control.CookedCurve.SourceKeys.Num() > 4097
			|| Control.CookedCurve.SamplesQ8.Num() > 4097)
		{
			Error = TEXT("A control graph, program or height curve exceeds capture limits.");
			return false;
		}
		int64 OperatorBytes = 0;
		for (const auto& Operator : Program.ExternalOperators)
		{
			if (Operator.Configuration.Num() > 65536 || Operator.Inputs.Num() > 3
				|| OperatorBytes > 1048576 - Operator.Configuration.Num())
			{
				Error = TEXT("Control provider configuration exceeds capture limits.");
				return false;
			}
			OperatorBytes += Operator.Configuration.Num();
		}
		for (const auto& Parameter : Program.Parameters)
		{
			if (Parameter.Name.Len() > 128)
			{
				Error = TEXT("Cooked control parameter name exceeds capture limits.");
				return false;
			}
		}
		for (int32 Index = 0; Index < Control.CurveKeys.Num(); ++Index)
		{
			const auto& Key = Control.CurveKeys[Index];
			if (Key.InputQ16 < 0 || Key.InputQ16 > 65535 || int64(Key.OutputQ8) < -WorldDefinitionHeightLimitQ8
				|| int64(Key.OutputQ8) > WorldDefinitionHeightLimitQ8
				|| (Index > 0 && Control.CurveKeys[Index - 1].InputQ16 >= Key.InputQ16))
			{
				Error = TEXT("Control curve keys must be ordered and within their supported ranges.");
				return false;
			}
		}
		if (Control.CurveKeys[0].InputQ16 != 0 || Control.CurveKeys.Last().InputQ16 != 65535)
		{
			Error = TEXT("Control curve keys must include both input endpoints.");
			return false;
		}
		OutProgram = Program;
		return true;
	}

	static bool WorldDefinitionCaptureControl(const FVoxelLandM4ControlConfig& Control,
		EVoxelLandM4ControlChannel Channel, const FVoxelLandSerializedProgram& Program,
		const FVoxelLandGraphSnapshot& Source, TFunctionRef<bool()> IsCaptureCurrent, FVoxelLandM4ControlSnapshot& Out,
		FString& Error)
	{
		FVoxelLandM4ControlSnapshot Scratch;
		Scratch.Source = Source;
		if (!IsCaptureCurrent())
		{
			Error = TEXT("World graph or settings changed during graph capture.");
			return false;
		}
		const UVoxelLandM4ControlGraph* Named = Cast<UVoxelLandM4ControlGraph>(Control.Graph);
		if (Named)
		{
			FVoxelLandGraphSnapshot Projected;
			if (!Named->ProjectSnapshot(Scratch.Source, Channel, Projected, Error))
			{
				return false;
			}
			Scratch.Source = MoveTemp(Projected);
		}
		// Tooltips are presentation only; workers do not retain localized text.
		for (auto& Parameter : Scratch.Source.Parameters)
		{
			Parameter.Tooltip = FText::GetEmpty();
		}
		for (auto& Function : Scratch.Source.Functions)
		{
			for (auto& Parameter : Function.Inputs)
			{
				Parameter.Tooltip = FText::GetEmpty();
			}
		}
		TMap<FGuid, const FVoxelLandGraphParameter*> Parameters;
		TSet<FString> Names;
		for (const FVoxelLandGraphParameter& Parameter : Scratch.Source.Parameters)
		{
			if (!WorldDefinitionParameterValid(Parameter) || Parameters.Contains(Parameter.Id)
				|| Names.Contains(Parameter.Name.ToLower()))
			{
				Error = TEXT("Control graph parameters are invalid or duplicated.");
				return false;
			}
			Parameters.Add(Parameter.Id, &Parameter);
			Names.Add(Parameter.Name.ToLower());
		}
		TSet<FGuid> Seen;
		for (const FVoxelLandParameterOverride& Override : Control.Overrides)
		{
			const FVoxelLandGraphParameter* const* Parameter = Parameters.Find(Override.ParameterId);
			if (!Parameter || Seen.Contains(Override.ParameterId) || Override.Type != (*Parameter)->Type
				|| (Override.Type == EVoxelLandGraphValueType::Float
					&& (!FMath::IsFinite(Override.FloatValue)
						|| ((*Parameter)->bHasRange
							&& (Override.FloatValue < (*Parameter)->FloatMin
								|| Override.FloatValue > (*Parameter)->FloatMax))))
				|| (Override.Type == EVoxelLandGraphValueType::Int32 && (*Parameter)->bHasRange
					&& (Override.IntValue < (*Parameter)->IntMin || Override.IntValue > (*Parameter)->IntMax)))
			{
				Error = TEXT("Control override has an unknown or duplicate ID, incompatible type, or invalid value.");
				return false;
			}
			Seen.Add(Override.ParameterId);
		}
		Scratch.Payload = Program;
		Scratch.Overrides = Control.Overrides;
		Scratch.CurveKeys = Control.CurveKeys;
		Scratch.CookedCurve = Control.CookedCurve;
		Out = MoveTemp(Scratch);
		return true;
	}
};

bool FVoxelLandWorldDefinitionValidation::ValidateGenerationSettings(
	const FVoxelLandWorldCommonSettings& Authored, FString& OutError)
{
	if (Authored.VoxelSizeCm < 1 || Authored.VoxelSizeCm > 134217
		|| int64(Authored.BaseHeightQ8) < -FVoxelLandWorldDefinitionValidation::WorldDefinitionHeightLimitQ8
		|| int64(Authored.BaseHeightQ8) > FVoxelLandWorldDefinitionValidation::WorldDefinitionHeightLimitQ8
		|| Authored.HorizontalRadius < 1 || Authored.HorizontalRadius > 8 || Authored.VerticalRadius < 1
		|| Authored.VerticalRadius > 4 || !FMath::IsFinite(Authored.SmoothStrength) || Authored.SmoothStrength < 0.f
		|| Authored.SmoothStrength > 1.f || Authored.HeightDetail.AlgorithmVersion != 1
		|| Authored.HeightDetail.AmplitudeQ8 < 0 || Authored.HeightDetail.AmplitudeQ8 > 1280000
		|| !FMath::IsFinite(Authored.HeightDetail.FrequencyPerCm) || Authored.HeightDetail.FrequencyPerCm <= 0.f
		|| Authored.HeightDetail.FrequencyPerCm > 1.f || !Authored.HeightDetail.NodeId.IsValid()
		|| Authored.OverhangAmplitudeQ8 < 0 || Authored.OverhangAmplitudeQ8 > 128000 || Authored.OverhangBandQ8 < 1
		|| Authored.OverhangBandQ8 > 1280000 || Authored.OverhangAmplitudeQ8 > Authored.OverhangBandQ8
		|| !FMath::IsFinite(Authored.OverhangFrequencyPerCm) || Authored.OverhangFrequencyPerCm <= 0.f
		|| Authored.OverhangFrequencyPerCm > 1.f || Authored.OverhangSalt < 0 || !Authored.OverhangNodeId.IsValid())
	{
		OutError = TEXT("Common world generation settings are invalid.");
		return false;
	}
	return true;
}

bool FVoxelLandWorldDefinitionValidation::CaptureSharedHeightStage(
	const FVoxelLandWorldCommonSettings& Authored, FVoxelLandM4HeightStageManifest& HeightStage, FString& OutError)
{
	const UVoxelLandM4ControlGraph* Height = Cast<UVoxelLandM4ControlGraph>(Authored.Continental.Graph);
	const UVoxelLandM4ControlGraph* Mountain = Cast<UVoxelLandM4ControlGraph>(Authored.Mountain.Graph);
	const UVoxelLandM4ControlGraph* Peaks = Cast<UVoxelLandM4ControlGraph>(Authored.Peaks.Graph);
	const bool bHasHeight = (Height && Height->HeightOutputId.IsValid())
						 || (Mountain && Mountain->HeightOutputId.IsValid())
						 || (Peaks && Peaks->HeightOutputId.IsValid());
	if (bHasHeight)
	{
		if (!Height || Height != Mountain || Height != Peaks || !Height->HeightOutputId.IsValid())
		{
			OutError = TEXT("A shared height output requires the same graph for all controls.");
			return false;
		}
		const FVoxelLandGraphNodeRecord* Node = Height->Nodes.FindByPredicate(
			[Height](const FVoxelLandGraphNodeRecord& Record) { return Record.Id == Height->HeightOutputId; });
		if (!Node || Node->Pins.Num() != 3)
		{
			OutError = TEXT("The shared height output must have three inputs.");
			return false;
		}
		HeightStage.Version = 1;
		HeightStage.OutputNodeId = Height->HeightOutputId;
		HeightStage.ContinentalInputId = Node->Pins[0].Id;
		HeightStage.MountainInputId = Node->Pins[1].Id;
		HeightStage.PeaksInputId = Node->Pins[2].Id;
	}
	return true;
}

void FVoxelLandWorldDefinitionValidation::CopyGenerationSettings(
	const FVoxelLandWorldCommonSettings& Authored, EVoxelLandWorldKind Kind, FVoxelLandWorldCommonSnapshot& Scratch)
{
	Scratch.Seed = Authored.Seed;
	Scratch.VoxelSizeCm = Authored.VoxelSizeCm;
	Scratch.BaseHeightQ8 = Authored.BaseHeightQ8;
	Scratch.bNormalizeSlope = Authored.bNormalizeSlope;
	Scratch.bSmoothEnabled = Authored.bSmoothEnabled;
	Scratch.SmoothStrength = Authored.SmoothStrength;
	Scratch.HeightDetail = Authored.HeightDetail;
	Scratch.bOverhangEnabled = Authored.bOverhangEnabled;
	Scratch.OverhangAmplitudeQ8 = Authored.OverhangAmplitudeQ8;
	Scratch.OverhangBandQ8 = Authored.OverhangBandQ8;
	Scratch.OverhangFrequencyPerCm = Authored.OverhangFrequencyPerCm;
	Scratch.OverhangNodeId = Authored.OverhangNodeId;
	Scratch.OverhangSalt = Authored.OverhangSalt;
	Scratch.MaterialSetupPath = Authored.MaterialSetup.ToSoftObjectPath();
	Scratch.HorizontalRadius = Authored.HorizontalRadius;
	Scratch.VerticalRadius = Authored.VerticalRadius;
	Scratch.Caves = Authored.Caves;
	Scratch.Caves.PlanningMode =
		Kind == EVoxelLandWorldKind::Finite ? EVoxelLandM5PlanningMode::Finite : EVoxelLandM5PlanningMode::Regional;
}

bool UVoxelLandWorldSource::CaptureCommon(EVoxelLandWorldKind Kind, TFunctionRef<bool()> IsModeCurrent,
	FVoxelLandWorldCommonSnapshot& OutSnapshot, FString& OutError) const
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("World settings must be captured on the game thread.");
		return false;
	}
	if (Kind != EVoxelLandWorldKind::Finite && Kind != EVoxelLandWorldKind::Infinite)
	{
		OutError = TEXT("Unsupported world kind.");
		return false;
	}
	TStrongObjectPtr<UVoxelLandWorldSource> Owner(const_cast<UVoxelLandWorldSource*>(this));
	const FSoftObjectPath& MaterialPath = Common.MaterialSetup.ToSoftObjectPath();
	const FTopLevelAssetPath MaterialAssetPath = MaterialPath.GetAssetPath();
	const uint64 MaterialPathLength = uint64(MaterialAssetPath.GetPackageName().GetStringLength())
									+ uint64(MaterialAssetPath.GetAssetName().GetStringLength())
									+ uint64(MaterialPath.GetSubPathUtf8String().Len()) + 2;
	if (MaterialPathLength > 2048)
	{
		OutError = TEXT("Material setup path exceeds capture limits.");
		return false;
	}
	const FVoxelLandM4ControlConfig* LiveControls[] = {&Common.Continental, &Common.Mountain, &Common.Peaks};
	const EVoxelLandM4ControlChannel Channels[] = {EVoxelLandM4ControlChannel::Continental,
		EVoxelLandM4ControlChannel::Mountain, EVoxelLandM4ControlChannel::Peaks};
	const TCHAR* ChannelNames[] = {TEXT("Continental"), TEXT("Mountain"), TEXT("Peaks")};
	FVoxelLandSerializedProgram Programs[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!FVoxelLandWorldDefinitionValidation::WorldDefinitionPreflightControl(
				*LiveControls[Index], Channels[Index], Programs[Index], OutError))
		{
			OutError = FString::Printf(TEXT("%s control: %s"), ChannelNames[Index], *OutError);
			return false;
		}
	}
	const FVoxelLandWorldCommonSettings Authored = Common;
	TStrongObjectPtr<UVoxelLandTerrainGraph> ContinentalGraph(Authored.Continental.Graph.Get());
	TStrongObjectPtr<UVoxelLandTerrainGraph> MountainGraph(Authored.Mountain.Graph.Get());
	TStrongObjectPtr<UVoxelLandTerrainGraph> PeaksGraph(Authored.Peaks.Graph.Get());
	const FVoxelLandM4ControlConfig* Controls[] = {&Authored.Continental, &Authored.Mountain, &Authored.Peaks};
	FVoxelLandWorldDefinitionValidation::FWorldDefinitionFunctionPreflight Preflights[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!Preflights[Index].PrepareRoot(*Controls[Index]->Graph.Get(), OutError))
		{
			OutError = FString::Printf(TEXT("%s control: %s"), ChannelNames[Index], *OutError);
			return false;
		}
	}
	const auto IsCaptureCurrent = [this, &Authored, &Controls, &Channels, &Programs, &Preflights, &IsModeCurrent]() {
		if (!IsModeCurrent()
			|| !FVoxelLandWorldCommonSettings::StaticStruct()->CompareScriptStruct(&Common, &Authored, 0))
		{
			return false;
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const UVoxelLandTerrainGraph* Graph = Controls[Index]->Graph.Get();
			if (!Graph || !Preflights[Index].IsCurrent())
			{
				return false;
			}
			const auto* Named = Cast<UVoxelLandM4ControlGraph>(Graph);
			const FVoxelLandSerializedProgram& Current =
				Named ? Named->GetProgram(Channels[Index]) : Graph->CompiledProgram;
			if (!FVoxelLandSerializedProgram::StaticStruct()->CompareScriptStruct(&Current, &Programs[Index], 0))
			{
				return false;
			}
		}
		return true;
	};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!Preflights[Index].Run(IsCaptureCurrent))
		{
			OutError = FString::Printf(TEXT("%s control: %s"), ChannelNames[Index], *OutError);
			return false;
		}
	}
	if (!FVoxelLandWorldDefinitionValidation::ValidateGenerationSettings(Authored, OutError))
	{
		return false;
	}
	FVoxelLandWorldCommonSnapshot Scratch;
	if (!FVoxelLandWorldDefinitionValidation::CaptureSharedHeightStage(Authored, Scratch.HeightStage, OutError))
	{
		return false;
	}
	FVoxelLandM4ControlSnapshot* CapturedControls[] = {&Scratch.Continental, &Scratch.Mountain, &Scratch.Peaks};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!FVoxelLandWorldDefinitionValidation::WorldDefinitionCaptureControl(*Controls[Index], Channels[Index],
				Programs[Index], Preflights[Index].GetCaptured(), IsCaptureCurrent, *CapturedControls[Index], OutError))
		{
			OutError = FString::Printf(TEXT("%s control: %s"), ChannelNames[Index], *OutError);
			return false;
		}
	}
	if (!IsCaptureCurrent())
	{
		OutError = TEXT("World settings changed while their graphs were captured.");
		return false;
	}
	FVoxelLandWorldDefinitionValidation::CopyGenerationSettings(Authored, Kind, Scratch);
	OutSnapshot = MoveTemp(Scratch);
	return true;
}
