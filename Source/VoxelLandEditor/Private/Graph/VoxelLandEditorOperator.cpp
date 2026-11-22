#include "Graph/VoxelLandEditorOperator.h"
#include "Operators/VoxelLandOperator.h"

class FVoxelLandEditorOperatorInternals final
{
public:
	inline static TMap<FGuid, TMap<int32, FVoxelLandEditorOperatorPresentation>> RegistryPresentations;
	static constexpr int32 MaxRegistryPresentations = 4096;
	inline static uint64 PresentationRevision = 0;

	static bool IsValidType(EVoxelLandGraphValueType Type)
	{
		return Type == EVoxelLandGraphValueType::Float || Type == EVoxelLandGraphValueType::Int32
			|| Type == EVoxelLandGraphValueType::Bool;
	}

	static bool ValidateInputs(
		const FVoxelLandEditorOperatorPresentation& Presentation, const IVoxelLandOperator& Provider, FString& Error)
	{
		const TArray<EVoxelLandGraphValueType> Types = Provider.InputTypes();
		if (Types.IsEmpty() || Types.Num() > 3 || Presentation.Inputs.Num() != Types.Num()
			|| (!Presentation.InputTooltips.IsEmpty() && Presentation.InputTooltips.Num() != Types.Num()))
		{
			Error = TEXT("Presentation input count must match the supported provider signature.");
			return false;
		}
		TSet<FString> Names;
		for (int32 Index = 0; Index < Types.Num(); ++Index)
		{
			const FVoxelLandGraphPinRecord& Pin = Presentation.Inputs[Index];
			const FString Key = Pin.Name.ToLower();
			if (Pin.bOutput || Pin.bInvalidDefault || !IsValidType(Pin.Type) || Pin.Type != Types[Index]
				|| Pin.Name.IsEmpty() || Pin.Name.Len() > 128 || Names.Contains(Key)
				|| !FMath::IsFinite(Pin.FloatDefault))
			{
				Error = TEXT("Presentation has an invalid, duplicate or mismatched input pin.");
				return false;
			}
			Names.Add(Key);
		}
		return true;
	}
};

bool FVoxelLandEditorOperatorRegistry::Register(const FVoxelLandEditorOperatorPresentation& Presentation,
	FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("Editor presentations must be registered on the game thread.");
		return false;
	}
	const TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider =
		FVoxelLandOperatorRegistry::Resolve(Presentation.OperatorId, Presentation.Version);
	const bool bReservedId = Presentation.OperatorId.A == 0x564c4d33 && Presentation.OperatorId.B == 1 && Presentation.
	                         OperatorId.C == 0;
	if (!Provider || !Presentation.OperatorId.IsValid() || bReservedId || Presentation.Version < 1
		|| Presentation.DisplayName.IsEmpty() || Presentation.Category.IsEmpty() || Presentation.OutputName.IsEmpty()
		|| Presentation.OutputName.Len() > 128 || Presentation.DefaultConfiguration.Num() > 65536
		|| !FVoxelLandEditorOperatorInternals::IsValidType(Provider->OutputType())
		|| Presentation.OutputType != Provider->OutputType())
	{
		OutError = TEXT("Presentation has an invalid ID, version, label, output or missing runtime provider.");
		return false;
	}
	if (!FVoxelLandEditorOperatorInternals::ValidateInputs(Presentation, *Provider, OutError)
		|| !Provider->ValidateConfiguration(Presentation.DefaultConfiguration, OutError))
	{
		return false;
	}
	if (const TMap<int32, FVoxelLandEditorOperatorPresentation>* Versions =
			FVoxelLandEditorOperatorInternals::RegistryPresentations.Find(Presentation.OperatorId))
	{
		if (Versions->Contains(Presentation.Version))
		{
			OutError = TEXT("Duplicate editor presentation ID and version.");
			return false;
		}
	}
	int32 Count = 0;
	for (const auto& Entry : FVoxelLandEditorOperatorInternals::RegistryPresentations)
	{
		Count += Entry.Value.Num();
	}
	if (Count >= FVoxelLandEditorOperatorInternals::MaxRegistryPresentations)
	{
		OutError = TEXT("Editor presentation limit exceeded.");
		return false;
	}
	FVoxelLandEditorOperatorInternals::RegistryPresentations.FindOrAdd(Presentation.OperatorId)
		.Add(Presentation.Version, Presentation);
	++FVoxelLandEditorOperatorInternals::PresentationRevision;
	return true;
}

bool FVoxelLandEditorOperatorRegistry::Unregister(const FGuid& OperatorId, int32 Version)
{
	if (!IsInGameThread())
	{
		return false;
	}
	TMap<int32, FVoxelLandEditorOperatorPresentation>* Versions =
		FVoxelLandEditorOperatorInternals::RegistryPresentations.Find(OperatorId);
	if (!Versions || Versions->Remove(Version) == 0)
	{
		return false;
	}
	if (Versions->IsEmpty())
	{
		FVoxelLandEditorOperatorInternals::RegistryPresentations.Remove(OperatorId);
	}
	++FVoxelLandEditorOperatorInternals::PresentationRevision;
	return true;
}

uint64 FVoxelLandEditorOperatorRegistry::GetRevision()
{
	check(IsInGameThread());
	return FVoxelLandEditorOperatorInternals::PresentationRevision;
}

bool FVoxelLandEditorOperatorRegistry::FindPresentation(const FGuid& OperatorId, int32 Version,
	FVoxelLandEditorOperatorPresentation& OutPresentation)
{
	check(IsInGameThread());
	const TMap<int32, FVoxelLandEditorOperatorPresentation>* Versions =
		FVoxelLandEditorOperatorInternals::RegistryPresentations.Find(OperatorId);
	const FVoxelLandEditorOperatorPresentation* Found = Versions ? Versions->Find(Version) : nullptr;
	if (!Found || !FVoxelLandOperatorRegistry::Resolve(OperatorId, Version))
	{
		return false;
	}
	OutPresentation = *Found;
	return true;
}

void FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged()
{
	check(IsInGameThread());
	++FVoxelLandEditorOperatorInternals::PresentationRevision;
}

void FVoxelLandEditorOperatorRegistry::GetPresentations(
	TArray<FVoxelLandEditorOperatorPresentation>& OutRegistryPresentations)
{
	OutRegistryPresentations.Reset();
	if (!IsInGameThread())
	{
		return;
	}
	for (const auto& Entry : FVoxelLandEditorOperatorInternals::RegistryPresentations)
	{
		for (const auto& Version : Entry.Value)
		{
			if (FVoxelLandOperatorRegistry::Resolve(Entry.Key, Version.Key))
			{
				OutRegistryPresentations.Add(Version.Value);
			}
		}
	}
	OutRegistryPresentations.Sort(
		[](const FVoxelLandEditorOperatorPresentation& A, const FVoxelLandEditorOperatorPresentation& B) {
			if (A.OperatorId.A != B.OperatorId.A) { return A.OperatorId.A < B.OperatorId.A; }
			if (A.OperatorId.B != B.OperatorId.B) { return A.OperatorId.B < B.OperatorId.B; }
			if (A.OperatorId.C != B.OperatorId.C) { return A.OperatorId.C < B.OperatorId.C; }
			if (A.OperatorId.D != B.OperatorId.D) { return A.OperatorId.D < B.OperatorId.D; }
			return A.Version < B.Version;
		});
}

void FVoxelLandEditorOperatorRegistry::Clear()
{
	check(IsInGameThread());
	FVoxelLandEditorOperatorInternals::RegistryPresentations.Reset();
	++FVoxelLandEditorOperatorInternals::PresentationRevision;
}