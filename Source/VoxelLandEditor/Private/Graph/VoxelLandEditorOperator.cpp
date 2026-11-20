#include "Graph/VoxelLandEditorOperator.h"
#include "Operators/VoxelLandOperator.h"

namespace
{
TMap<FGuid, TMap<int32, FVoxelLandEditorOperatorPresentation>> Presentations;
constexpr int32 MaxPresentations = 4096;
uint64 PresentationRevision = 0;

bool IsValidType(EVoxelLandGraphValueType Type)
{
    return Type == EVoxelLandGraphValueType::Float || Type == EVoxelLandGraphValueType::Int32 || Type == EVoxelLandGraphValueType::Bool;
}
bool ValidateInputs(const FVoxelLandEditorOperatorPresentation& Presentation,
    const IVoxelLandOperator& Provider, FString& Error)
{
    const TArray<EVoxelLandGraphValueType> Types = Provider.InputTypes();
    if (Types.IsEmpty() || Types.Num() > 3 || Presentation.Inputs.Num() != Types.Num())
    {
        Error = TEXT("Presentation input count must match the supported provider signature.");
        return false;
    }
    TSet<FString> Names;
    for (int32 Index = 0; Index < Types.Num(); ++Index)
    {
        const FVoxelLandGraphPinRecord& Pin = Presentation.Inputs[Index];
        const FString Key = Pin.Name.ToLower();
        if (Pin.bOutput || Pin.bInvalidDefault || !IsValidType(Pin.Type) || Pin.Type != Types[Index] || Pin.Name.IsEmpty() || Pin.Name.Len() > 128 ||
            Names.Contains(Key) || !FMath::IsFinite(Pin.FloatDefault))
        {
            Error = TEXT("Presentation has an invalid, duplicate or mismatched input pin.");
            return false;
        }
        Names.Add(Key);
    }
    return true;
}
}

bool FVoxelLandEditorOperatorRegistry::Register(const FVoxelLandEditorOperatorPresentation& Presentation, FString& OutError)
{
    OutError.Reset();
    if (!IsInGameThread())
    {
        OutError = TEXT("Editor presentations must be registered on the game thread.");
        return false;
    }
    const TSharedPtr<const IVoxelLandOperator, ESPMode::ThreadSafe> Provider =
        FVoxelLandOperatorRegistry::Resolve(Presentation.OperatorId, Presentation.Version);
    const bool bReservedId = Presentation.OperatorId.A == 0x564c4d33 && Presentation.OperatorId.B == 1 && Presentation.OperatorId.C == 0;
    if (!Provider || !Presentation.OperatorId.IsValid() || bReservedId || Presentation.Version < 1 || Presentation.DisplayName.IsEmpty() ||
        Presentation.Category.IsEmpty() || Presentation.OutputName.IsEmpty() || Presentation.OutputName.Len() > 128 ||
        Presentation.DefaultConfiguration.Num() > 65536 || !IsValidType(Provider->OutputType()) ||
        Presentation.OutputType != Provider->OutputType())
    {
        OutError = TEXT("Presentation has an invalid ID, version, label, output or missing runtime provider.");
        return false;
    }
    if (!ValidateInputs(Presentation, *Provider, OutError) ||
        !Provider->ValidateConfiguration(Presentation.DefaultConfiguration, OutError))
    {
        return false;
    }
    if (const TMap<int32, FVoxelLandEditorOperatorPresentation>* Versions = Presentations.Find(Presentation.OperatorId))
    {
        if (Versions->Contains(Presentation.Version))
        {
            OutError = TEXT("Duplicate editor presentation ID and version.");
            return false;
        }
    }
    int32 Count = 0;
    for (const auto& Entry : Presentations)
    {
        Count += Entry.Value.Num();
    }
    if (Count >= MaxPresentations)
    {
        OutError = TEXT("Editor presentation limit exceeded.");
        return false;
    }
    Presentations.FindOrAdd(Presentation.OperatorId).Add(Presentation.Version, Presentation);
    ++PresentationRevision;
    return true;
}
bool FVoxelLandEditorOperatorRegistry::Unregister(const FGuid& OperatorId, int32 Version)
{
    if (!IsInGameThread())
    {
        return false;
    }
    TMap<int32, FVoxelLandEditorOperatorPresentation>* Versions = Presentations.Find(OperatorId);
    if (!Versions || Versions->Remove(Version) == 0)
    {
        return false;
    }
    if (Versions->IsEmpty())
    {
        Presentations.Remove(OperatorId);
    }
    ++PresentationRevision;
    return true;
}
uint64 FVoxelLandEditorOperatorRegistry::GetRevision()
{
    check(IsInGameThread());
    return PresentationRevision;
}
void FVoxelLandEditorOperatorRegistry::GetPresentations(TArray<FVoxelLandEditorOperatorPresentation>& OutPresentations)
{
    OutPresentations.Reset();
    if (!IsInGameThread())
    {
        return;
    }
    for (const auto& Entry : Presentations)
    {
        for (const auto& Version : Entry.Value)
        {
            if (FVoxelLandOperatorRegistry::Resolve(Entry.Key, Version.Key))
            {
                OutPresentations.Add(Version.Value);
            }
        }
    }
    OutPresentations.Sort([](const FVoxelLandEditorOperatorPresentation& A, const FVoxelLandEditorOperatorPresentation& B)
    {
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
    Presentations.Reset();
    ++PresentationRevision;
}
