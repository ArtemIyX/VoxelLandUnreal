#include "Graph/VoxelLandEditorNode.h"
#include "Graph/VoxelLandEditorFloat.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/VoxelLandBuiltinCatalog.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "EdGraph/EdGraph.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "VoxelLandEditorNode"
namespace
{
const FName FloatPin(TEXT("float"));
const FName IntPin(TEXT("int"));
const FName BoolPin(TEXT("bool"));
const TCHAR* const NodeDisplayNames[] = {
    TEXT(""), TEXT("Density Output"), TEXT("Position X"), TEXT("Position Y"), TEXT("Position Z"),
    TEXT("Float Constant"), TEXT("Float Parameter"), TEXT("Add"), TEXT("Subtract"), TEXT("Multiply"),
    TEXT("Divide"), TEXT("Min"), TEXT("Max"), TEXT("Abs"), TEXT("Negate"), TEXT("Union"),
    TEXT("Subtract Shape"), TEXT("Intersect"), TEXT("Height to Density"), TEXT("Clamp"), TEXT("Saturate"),
    TEXT("Lerp"), TEXT("Int Constant"), TEXT("Bool Constant"), TEXT("Int Parameter"), TEXT("Bool Parameter"),
    TEXT("Int to Float"), TEXT("Less"), TEXT("Less Equal"), TEXT("Equal"), TEXT("And"), TEXT("Or"),
    TEXT("Not"), TEXT("Select Float"), TEXT("Select Int"), TEXT("Select Bool"), TEXT("Power"),
    TEXT("Plane"), TEXT("Sphere"), TEXT("Box"), TEXT("Perlin 2D"), TEXT("Perlin 3D"), TEXT("fBm 2D"),
    TEXT("fBm 3D"), TEXT("Value 2D"), TEXT("Value 3D"), TEXT("Simplex 2D"), TEXT("Simplex 3D"),
    TEXT("Reroute Float"), TEXT("Reroute Int"), TEXT("Reroute Bool"), TEXT("Smooth Density")
};
const TCHAR* const NodeDescriptions[] = {
    TEXT("Unknown operator."),
    TEXT("Final signed density in centimeters. Negative is solid; positive is empty. A graph requires exactly one output."),
    TEXT("World-space X coordinate in centimeters."), TEXT("World-space Y coordinate in centimeters."), TEXT("World-space Z coordinate in centimeters."),
    TEXT("A constant floating-point value."), TEXT("Reads a float parameter selected by its stable ID; world settings may override it."),
    TEXT("Adds inputs in their displayed order. Add, remove, or reorder inputs from the context menu."),
    TEXT("Subtracts B from A."), TEXT("Multiplies inputs in their displayed order. Add, remove, or reorder inputs from the context menu."),
    TEXT("Divides A by B. A zero divisor or non-finite result causes an evaluation error."),
    TEXT("Returns the smaller of A and B."), TEXT("Returns the larger of A and B."), TEXT("Returns the absolute value."), TEXT("Negates the input."),
    TEXT("Combines signed density fields with a hard union (minimum)."), TEXT("Cuts field B from field A using hard signed-density subtraction."),
    TEXT("Keeps the intersection of two signed density fields (maximum)."), TEXT("Converts a height in centimeters to density using world Z minus height."),
    TEXT("Restricts the value to the inclusive minimum and maximum. Minimum must not exceed maximum."), TEXT("Restricts the value to the range 0 to 1."),
    TEXT("Interpolates as A + (B - A) * Alpha. Alpha is not implicitly clamped."),
    TEXT("A constant signed 32-bit integer."), TEXT("A constant boolean value."), TEXT("Reads an integer parameter selected by stable ID."), TEXT("Reads a boolean parameter selected by stable ID."),
    TEXT("Explicitly converts a 32-bit integer to float; values that cannot be represented exactly cause an evaluation error."),
    TEXT("Returns true when A is less than B."), TEXT("Returns true when A is less than or equal to B."), TEXT("Returns true when A equals B."),
    TEXT("Returns true when both boolean inputs are true."), TEXT("Returns true when either boolean input is true."), TEXT("Inverts a boolean value."),
    TEXT("Returns True or False float input according to Condition."), TEXT("Returns True or False integer input according to Condition."), TEXT("Returns True or False boolean input according to Condition."),
    TEXT("Raises Base to Exponent. Invalid domains and non-finite results cause an evaluation error."),
    TEXT("Signed distance to a plane. The normal must be unit length; offset and position are in centimeters."),
    TEXT("Signed distance to a sphere centered at the origin; position and positive radius are in centimeters."),
    TEXT("Signed distance to an axis-aligned box centered at the origin; positive half extents and position are in centimeters."),
    TEXT("Samples independently seeded 2D Perlin noise at X and Y."), TEXT("Samples independently seeded 3D Perlin noise at X, Y and Z."),
    TEXT("Samples independently seeded 2D fractal Brownian Perlin noise."), TEXT("Samples independently seeded 3D fractal Brownian Perlin noise."),
    TEXT("Samples independently seeded 2D value noise."), TEXT("Samples independently seeded 3D value noise."),
    TEXT("Samples independently seeded 2D simplex noise."), TEXT("Samples independently seeded 3D simplex noise."),
    TEXT("Routes a float wire without changing its value."), TEXT("Routes an integer wire without changing its value."), TEXT("Routes a boolean wire without changing its value."),
    TEXT("Applies one-cell density smoothing before Density Output. Enabled and Strength are uniform controls; strength ranges from 0 to 1.")
};
static_assert(UE_ARRAY_COUNT(NodeDescriptions) == UE_ARRAY_COUNT(NodeDisplayNames));


}

FText UVoxelLandEditorNode::DisplayName(int32 InKind)
{
    return FText::FromString(InKind > 0 && InKind < UE_ARRAY_COUNT(NodeDisplayNames) ? NodeDisplayNames[InKind] : TEXT("Unknown operator"));
}
FText UVoxelLandEditorNode::GetNodeTitle(ENodeTitleType::Type) const
{
    if (Kind == 0 || Kind == int32(VoxelLandGraphFunctionCallId().D))
    {
        return ExternalTitle.IsEmpty() ? FText::FromString(FString::Printf(TEXT("External operator %s v%d"), *ExternalOperatorId.ToString(), OperatorVersion)) : ExternalTitle;
    }
    return DisplayName(Kind);
}
FLinearColor UVoxelLandEditorNode::GetNodeTitleColor() const
{
    if (Kind == int32(VoxelLandGraphFunctionCallId().D)) { return FLinearColor(0.38f, 0.22f, 0.48f); }
    if (Kind == 0)
    {
        FVoxelLandEditorOperatorPresentation Presentation;
        return FVoxelLandEditorOperatorRegistry::FindPresentation(ExternalOperatorId, OperatorVersion, Presentation)
            ? Presentation.NodeColor : FLinearColor(0.25f, 0.25f, 0.25f);
    }
    const EVoxelLandBuiltinNode Op = static_cast<EVoxelLandBuiltinNode>(Kind);
    if (Op == EVoxelLandBuiltinNode::DensityOutput) { return FLinearColor(0.55f, 0.16f, 0.13f); }
    if (Op == EVoxelLandBuiltinNode::SmoothDensity) { return FLinearColor(0.15f, 0.38f, 0.46f); }
    if (Kind >= int32(EVoxelLandBuiltinNode::Perlin2) && Kind <= int32(EVoxelLandBuiltinNode::Simplex3)) { return FLinearColor(0.48f, 0.30f, 0.12f); }
    if (Kind >= int32(EVoxelLandBuiltinNode::Plane) && Kind <= int32(EVoxelLandBuiltinNode::Box)) { return FLinearColor(0.15f, 0.42f, 0.30f); }
    if (Kind >= int32(EVoxelLandBuiltinNode::RerouteFloat) && Kind <= int32(EVoxelLandBuiltinNode::RerouteBool)) { return FLinearColor(0.25f, 0.25f, 0.25f); }
    if (Kind >= int32(EVoxelLandBuiltinNode::IntToFloat) && Kind <= int32(EVoxelLandBuiltinNode::SelectBool)) { return FLinearColor(0.32f, 0.22f, 0.50f); }
    if (Kind >= int32(EVoxelLandBuiltinNode::Union) && Kind <= int32(EVoxelLandBuiltinNode::Intersect)) { return FLinearColor(0.15f, 0.42f, 0.30f); }
    if ((Kind >= int32(EVoxelLandBuiltinNode::PositionX) && Kind <= int32(EVoxelLandBuiltinNode::FloatParameter)) ||
        (Kind >= int32(EVoxelLandBuiltinNode::IntConstant) && Kind <= int32(EVoxelLandBuiltinNode::BoolParameter))) { return FLinearColor(0.18f, 0.34f, 0.53f); }
    return FLinearColor(0.34f, 0.32f, 0.18f);
}
FText UVoxelLandEditorNode::GetTooltipText() const
{
    if (Kind == int32(VoxelLandGraphFunctionCallId().D))
    {
        return LOCTEXT("FunctionCallTooltip", "Calls a reusable terrain graph function. Refresh its interface after changing the function inputs or output type.");
    }
    if (Kind == 0)
    {
        FVoxelLandEditorOperatorPresentation Presentation;
        if (FVoxelLandEditorOperatorRegistry::FindPresentation(ExternalOperatorId, OperatorVersion, Presentation))
        {
            return Presentation.Tooltip;
        }
        return LOCTEXT("MissingProvider", "The editor presentation or runtime provider for this operator is unavailable.");
    }
    return FText::FromString(Kind > 0 && Kind < UE_ARRAY_COUNT(NodeDescriptions) ? NodeDescriptions[Kind] : NodeDescriptions[0]);
}
FText UVoxelLandEditorNode::GetPinDisplayName(const UEdGraphPin* Pin) const
{
    if (!Pin || Pin->Direction != EGPD_Input)
    {
        return Super::GetPinDisplayName(Pin);
    }
    const EVoxelLandBuiltinNode Op = static_cast<EVoxelLandBuiltinNode>(Kind);
    if (Op == EVoxelLandBuiltinNode::HeightToDensity && Pin->PinName == TEXT("Value"))
    {
        return LOCTEXT("HeightPin", "Height (cm)");
    }
    if ((Op == EVoxelLandBuiltinNode::SmoothDensity || Op == EVoxelLandBuiltinNode::DensityOutput) && Pin->PinName == TEXT("Density"))
    {
        return LOCTEXT("DensityPin", "Density (cm)");
    }
    if (Kind >= int32(EVoxelLandBuiltinNode::Perlin2) && Kind <= int32(EVoxelLandBuiltinNode::Simplex3) && Pin->PinName == TEXT("Frequency"))
    {
        return LOCTEXT("FrequencyPin", "Frequency (1/cm)");
    }
    if (Kind >= int32(EVoxelLandBuiltinNode::Plane) && Kind <= int32(EVoxelLandBuiltinNode::Box) &&
        (Pin->PinName == TEXT("Radius") || Pin->PinName == TEXT("Offset") || Pin->PinName.ToString().StartsWith(TEXT("Extent"))))
    {
        return FText::FromString(Pin->PinName.ToString() + TEXT(" (cm)"));
    }
    return Super::GetPinDisplayName(Pin);
}
void UVoxelLandEditorNode::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
    if (Kind == int32(VoxelLandGraphFunctionCallId().D))
    {
        const UVoxelLandGraphFunction* Function = Cast<UVoxelLandGraphFunction>(FunctionPath.ResolveObject());
        if (Pin.Direction == EGPD_Input && Function)
        {
            const FGuid InterfaceId = FunctionPinIds.FindRef(Pin.PinId);
            const FVoxelLandGraphParameter* Parameter = Function->Parameters.FindByPredicate([InterfaceId](const FVoxelLandGraphParameter& Item)
            {
                return Item.Id == InterfaceId;
            });
            if (Parameter && !Parameter->Tooltip.IsEmpty())
            {
                HoverTextOut = Parameter->Tooltip.ToString();
                return;
            }
        }
        HoverTextOut = Pin.Direction == EGPD_Output ? TEXT("The function's typed result.") :
            TEXT("Typed function input; an unconnected pin uses its displayed default.");
        return;
    }
    if (Kind == 0)
    {
        FVoxelLandEditorOperatorPresentation Presentation;
        if (FVoxelLandEditorOperatorRegistry::FindPresentation(ExternalOperatorId, OperatorVersion, Presentation))
        {
            if (Pin.Direction == EGPD_Output && !Presentation.OutputTooltip.IsEmpty())
            {
                HoverTextOut = Presentation.OutputTooltip.ToString();
                return;
            }
            for (int32 Index = 0; Index < Presentation.Inputs.Num(); ++Index)
            {
                if (Pin.Direction == EGPD_Input && Pin.PinName == FName(*Presentation.Inputs[Index].Name) &&
                    Presentation.InputTooltips.IsValidIndex(Index) && !Presentation.InputTooltips[Index].IsEmpty())
                {
                    HoverTextOut = Presentation.InputTooltips[Index].ToString();
                    return;
                }
            }
        }
        HoverTextOut = FString::Printf(TEXT("%s %s. %s"), *Pin.PinType.PinCategory.ToString(),
            Pin.Direction == EGPD_Output ? TEXT("output") : TEXT("input"), *GetTooltipText().ToString());
        return;
    }
    const FString Name = Pin.PinName.ToString();
    if (Pin.Direction == EGPD_Output)
    {
        HoverTextOut = FString::Printf(TEXT("%s output. %s"), *Pin.PinType.PinCategory.ToString(), *GetTooltipText().ToString());
    }
    else if (Name == TEXT("X") || Name == TEXT("Y") || Name == TEXT("Z"))
    {
        HoverTextOut = TEXT("Coordinate in centimeters; connect a world position or another coordinate field.");
    }
    else if (Name == TEXT("Frequency")) { HoverTextOut = TEXT("Noise frequency in inverse centimeters; must be positive and uniform."); }
    else if (Name == TEXT("Octaves")) { HoverTextOut = TEXT("Uniform integer octave count from 1 to 16."); }
    else if (Name == TEXT("Gain")) { HoverTextOut = TEXT("Uniform amplitude multiplier per octave, from 0 to 1."); }
    else if (Name == TEXT("Lacunarity")) { HoverTextOut = TEXT("Uniform frequency multiplier per octave; at least 1."); }
    else if (Name == TEXT("Density")) { HoverTextOut = TEXT("Signed density in centimeters: negative is solid and positive is empty."); }
    else if (Name == TEXT("Strength")) { HoverTextOut = TEXT("Uniform filter blend from 0 (original density) to 1 (full smoothing)."); }
    else if (Name == TEXT("Enabled")) { HoverTextOut = TEXT("Uniform boolean controlling whether the filter is applied."); }
    else if (Name == TEXT("Radius")) { HoverTextOut = TEXT("Sphere radius in centimeters; must be positive."); }
    else if (Name.StartsWith(TEXT("Extent"))) { HoverTextOut = TEXT("Box half extent in centimeters; must be positive."); }
    else if (Name.StartsWith(TEXT("Normal"))) { HoverTextOut = TEXT("Plane normal component; the complete normal must have unit length."); }
    else if (Name == TEXT("Offset")) { HoverTextOut = TEXT("Plane offset in centimeters."); }
    else if (Name == TEXT("Alpha")) { HoverTextOut = TEXT("Interpolation factor; values outside 0 to 1 extrapolate."); }
    else if (Name == TEXT("Condition")) { HoverTextOut = TEXT("Selects the True input when true; otherwise selects False."); }
    else if (static_cast<EVoxelLandBuiltinNode>(Kind) == EVoxelLandBuiltinNode::HeightToDensity) { HoverTextOut = TEXT("Surface height in centimeters."); }
    else { HoverTextOut = FString::Printf(TEXT("%s input. %s"), *Pin.PinType.PinCategory.ToString(), *GetTooltipText().ToString()); }
}
void UVoxelLandEditorNode::PinDefaultValueChanged(UEdGraphPin*)
{
    if (GetGraph())
    {
        GetGraph()->NotifyGraphChanged();
    }
}
void UVoxelLandEditorNode::PinConnectionListChanged(UEdGraphPin*)
{
    if (GetGraph())
    {
        GetGraph()->NotifyGraphChanged();
    }
}
void UVoxelLandEditorNode::AllocateDefaultPins()
{
    if (Kind == 0 || Kind == int32(VoxelLandGraphFunctionCallId().D))
    {
        for (const FVoxelLandGraphPinRecord& Spec : ExternalPins)
        {
            const FName Category = Spec.Type == EVoxelLandGraphValueType::Float ? FloatPin :
                Spec.Type == EVoxelLandGraphValueType::Int32 ? IntPin : BoolPin;
            UEdGraphPin* Pin = CreatePin(Spec.bOutput ? EGPD_Output : EGPD_Input, Category, FName(*Spec.Name));
            if (Spec.InterfaceId.IsValid())
            {
                FunctionPinIds.Add(Pin->PinId, Spec.InterfaceId);
            }
            if (!Spec.bOutput)
            {
                if (Spec.Type == EVoxelLandGraphValueType::Float)
                {
                    Pin->DefaultValue = FVoxelLandEditorFloat::Format(Spec.FloatDefault);
                }
                else if (Spec.Type == EVoxelLandGraphValueType::Int32)
                {
                    Pin->DefaultValue = LexToString(Spec.IntDefault);
                }
                else
                {
                    Pin->DefaultValue = Spec.BoolDefault ? TEXT("true") : TEXT("false");
                }
                Pin->AutogeneratedDefaultValue = Pin->DefaultValue;
            }
        }
        if (GetGraph())
        {
            GetGraph()->NotifyGraphChanged();
        }
        return;
    }
    const EVoxelLandBuiltinNode Op = static_cast<EVoxelLandBuiltinNode>(Kind);
    FVoxelLandBuiltinNodeSpec Spec;
    if (!FVoxelLandBuiltinCatalog::Find(Op, Spec, FMath::Clamp(VariadicInputs, 2, 32)))
    {
        if (GetGraph())
        {
            GetGraph()->NotifyGraphChanged();
        }
        return;
    }
    for (const FVoxelLandBuiltinPinSpec& PinSpec : Spec.Pins)
    {
        const FName Category = PinSpec.Type == EVoxelLandGraphValueType::Float ? FloatPin :
            PinSpec.Type == EVoxelLandGraphValueType::Int32 ? IntPin : BoolPin;
        UEdGraphPin* Pin = CreatePin(PinSpec.bOutput ? EGPD_Output : EGPD_Input, Category, FName(*PinSpec.Name));
        if (!PinSpec.bOutput)
        {
            Pin->DefaultValue = PinSpec.DefaultLiteral;
            Pin->AutogeneratedDefaultValue = PinSpec.DefaultLiteral;
        }
    }
    if (GetGraph())
    {
        GetGraph()->NotifyGraphChanged();
    }
}
void UVoxelLandEditorNode::AutowireNewNode(UEdGraphPin* FromPin)
{
    if (Kind != int32(VoxelLandGraphFunctionCallId().D) || !FromPin || !GetGraph() ||
        !FromPin->GetOwningNode() || FromPin->GetOwningNode()->GetGraph() != GetGraph())
    {
        return;
    }
    const UEdGraphSchema* Schema = GetGraph()->GetSchema();
    if (!Schema) { return; }
    for (UEdGraphPin* Pin : Pins)
    {
        if (!Pin || Pin->Direction == FromPin->Direction || Pin->PinType.PinCategory != FromPin->PinType.PinCategory ||
            Schema->CanCreateConnection(FromPin, Pin).Response != CONNECT_RESPONSE_MAKE)
        {
            continue;
        }
        GetGraph()->Modify();
        Modify();
        Pin->Modify();
        FromPin->GetOwningNode()->Modify();
        FromPin->Modify();
        if (Schema->TryCreateConnection(FromPin, Pin))
        {
            return;
        }
    }
}
bool UVoxelLandEditorNode::ValidateFunctionInterface(const UVoxelLandGraphFunction* Function) const
{
    if (!Function || Function->SourceVersion != 1 || Function->Parameters.Num() > 63 ||
        !Function->OutputNode.IsValid() || !Function->OutputPin.IsValid() ||
        (Function->OutputType != EVoxelLandGraphValueType::Float && Function->OutputType != EVoxelLandGraphValueType::Int32 &&
         Function->OutputType != EVoxelLandGraphValueType::Bool))
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot refresh function call: function or interface is invalid."));
        return false;
    }
    TSet<FGuid> Ids;
    for (const FVoxelLandGraphParameter& Parameter : Function->Parameters)
    {
        if (!Parameter.Id.IsValid() || Parameter.Name.IsEmpty() || Ids.Contains(Parameter.Id) ||
            (Parameter.Type != EVoxelLandGraphValueType::Float && Parameter.Type != EVoxelLandGraphValueType::Int32 &&
             Parameter.Type != EVoxelLandGraphValueType::Bool))
        {
            UE_LOG(LogTemp, Warning, TEXT("Cannot refresh function call: invalid input interface."));
            return false;
        }
        Ids.Add(Parameter.Id);
    }
    const FVoxelLandGraphNodeRecord* FunctionOutput = Function->Nodes.FindByPredicate([Function](const FVoxelLandGraphNodeRecord& Node)
    {
        return Node.Id == Function->OutputNode;
    });
    if (!FunctionOutput || !FunctionOutput->Pins.ContainsByPredicate([Function](const FVoxelLandGraphPinRecord& Pin)
        { return Pin.Id == Function->OutputPin && Pin.bOutput && Pin.Type == Function->OutputType; }))
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot refresh function call: selected output is missing or incompatible."));
        return false;
    }
    return true;
}
void UVoxelLandEditorNode::RefreshFunctionInputs(const UVoxelLandGraphFunction& Function,
    const TArray<UEdGraphPin*>& PreviousPins, TArray<UEdGraphPin*>& Ordered, TMap<FGuid, FGuid>& NewInterfaceIds)
{
    for (const FVoxelLandGraphParameter& Parameter : Function.Parameters)
    {
        UEdGraphPin* Pin = nullptr;
        for (UEdGraphPin* Candidate : PreviousPins)
        {
            if (Candidate && Candidate->Direction == EGPD_Input && FunctionPinIds.FindRef(Candidate->PinId) == Parameter.Id)
            {
                Pin = Candidate;
                break;
            }
        }
        const FName Category = Parameter.Type == EVoxelLandGraphValueType::Float ? FloatPin :
            Parameter.Type == EVoxelLandGraphValueType::Int32 ? IntPin : BoolPin;
        if (!Pin)
        {
            Pin = CreatePin(EGPD_Input, Category, FName(*Parameter.Name));
        }
        const bool bTypeChanged = Pin->PinType.PinCategory != Category;
        if (bTypeChanged)
        {
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked && Linked->GetOwningNode()) { Linked->GetOwningNode()->Modify(); Linked->Modify(); }
            }
            Pin->Modify();
            Pin->BreakAllPinLinks();
            Pin->PinType.PinCategory = Category;
        }
        if (bTypeChanged || !PreviousPins.Contains(Pin))
        {
            Pin->DefaultValue = Parameter.Type == EVoxelLandGraphValueType::Float ? FVoxelLandEditorFloat::Format(Parameter.FloatDefault) :
                Parameter.Type == EVoxelLandGraphValueType::Int32 ? LexToString(Parameter.IntDefault) :
                Parameter.BoolDefault ? TEXT("true") : TEXT("false");
            Pin->AutogeneratedDefaultValue = Pin->DefaultValue;
        }
        Pin->Modify();
        Pin->PinName = FName(*Parameter.Name);
        NewInterfaceIds.Add(Pin->PinId, Parameter.Id);
        Ordered.Add(Pin);
    }
}
void UVoxelLandEditorNode::RefreshFunctionOutput(const UVoxelLandGraphFunction& Function,
    const TArray<UEdGraphPin*>& PreviousPins, TArray<UEdGraphPin*>& Ordered)
{
    UEdGraphPin* Output = nullptr;
    for (UEdGraphPin* Candidate : PreviousPins)
    {
        if (Candidate && Candidate->Direction == EGPD_Output) { Output = Candidate; break; }
    }
    const FName OutputCategory = Function.OutputType == EVoxelLandGraphValueType::Float ? FloatPin :
        Function.OutputType == EVoxelLandGraphValueType::Int32 ? IntPin : BoolPin;
    if (!Output) { Output = CreatePin(EGPD_Output, OutputCategory, TEXT("Result")); }
    if (Output->PinType.PinCategory != OutputCategory)
    {
        for (UEdGraphPin* Linked : Output->LinkedTo)
        {
            if (Linked && Linked->GetOwningNode()) { Linked->GetOwningNode()->Modify(); Linked->Modify(); }
        }
        Output->Modify();
        Output->BreakAllPinLinks();
        Output->PinType.PinCategory = OutputCategory;
    }
    Ordered.Add(Output);
}
void UVoxelLandEditorNode::RemoveOldFunctionPins(const TArray<UEdGraphPin*>& PreviousPins, const TArray<UEdGraphPin*>& Ordered)
{
    for (UEdGraphPin* OldPin : PreviousPins)
    {
        if (OldPin && !Ordered.Contains(OldPin))
        {
            for (UEdGraphPin* Linked : OldPin->LinkedTo)
            {
                if (Linked && Linked->GetOwningNode()) { Linked->GetOwningNode()->Modify(); Linked->Modify(); }
            }
            OldPin->Modify();
            OldPin->BreakAllPinLinks();
            RemovePin(OldPin);
        }
    }
}
void UVoxelLandEditorNode::UpdateFunctionPinTemplates()
{
    ExternalPins.Reset();
    for (const UEdGraphPin* Pin : Pins)
    {
        FVoxelLandGraphPinRecord Spec;
        Spec.InterfaceId = FunctionPinIds.FindRef(Pin->PinId);
        Spec.Name = Pin->PinName.ToString();
        Spec.bOutput = Pin->Direction == EGPD_Output;
        Spec.Type = Pin->PinType.PinCategory == IntPin ? EVoxelLandGraphValueType::Int32 :
            Pin->PinType.PinCategory == BoolPin ? EVoxelLandGraphValueType::Bool : EVoxelLandGraphValueType::Float;
        if (Spec.Type == EVoxelLandGraphValueType::Float) { LexTryParseString(Spec.FloatDefault, *Pin->DefaultValue); }
        else if (Spec.Type == EVoxelLandGraphValueType::Int32) { LexTryParseString(Spec.IntDefault, *Pin->DefaultValue); }
        else { Spec.BoolDefault = Pin->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase); }
        ExternalPins.Add(MoveTemp(Spec));
    }
}
void UVoxelLandEditorNode::RefreshFunctionInterface()
{
    if (Kind != int32(VoxelLandGraphFunctionCallId().D) || !GetGraph()) { return; }
    const UVoxelLandGraphFunction* Function = Cast<UVoxelLandGraphFunction>(FunctionPath.TryLoad());
    if (!ValidateFunctionInterface(Function)) { return; }
    const FScopedTransaction Transaction(LOCTEXT("RefreshFunction", "Refresh function interface"));
    Modify();
    GetGraph()->Modify();
    const TArray<UEdGraphPin*> PreviousPins = Pins;
    TArray<UEdGraphPin*> Ordered;
    TMap<FGuid, FGuid> NewInterfaceIds;
    RefreshFunctionInputs(*Function, PreviousPins, Ordered, NewInterfaceIds);
    RefreshFunctionOutput(*Function, PreviousPins, Ordered);
    RemoveOldFunctionPins(PreviousPins, Ordered);
    Pins = MoveTemp(Ordered);
    FunctionPinIds = MoveTemp(NewInterfaceIds);
    UpdateFunctionPinTemplates();
    ExternalTitle = FText::FromString(Function->GetName());
    GetGraph()->NotifyGraphChanged();
}
void UVoxelLandEditorNode::AddVariadicInput()
{
    if (VariadicInputs >= 32)
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("AddInput", "Add graph input"));
    Modify(); GetGraph()->Modify();
    ++VariadicInputs;
    UEdGraphPin* Pin = CreatePin(EGPD_Input, FloatPin, *FString::Printf(TEXT("Input %d"), VariadicInputs));
    Pin->DefaultValue = Kind == int32(EVoxelLandBuiltinNode::Multiply) ? TEXT("1") : TEXT("0");
    Pin->AutogeneratedDefaultValue = Pin->DefaultValue;
    Pins.Remove(Pin);
    Pins.Insert(Pin, Pins.Num() - 1);
    GetGraph()->NotifyGraphChanged();
}
void UVoxelLandEditorNode::RenumberVariadicInputs()
{
    for (int32 Index = 0; Index < VariadicInputs; ++Index)
    {
        UEdGraphPin* Pin = Pins.IsValidIndex(Index) ? Pins[Index] : nullptr;
        if (Pin && Pin->Direction == EGPD_Input)
        {
            Pin->Modify();
            Pin->PinName = FName(*FString::Printf(TEXT("Input %d"), Index + 1));
        }
    }
}
void UVoxelLandEditorNode::RemoveVariadicInput(FGuid PinId)
{
    const int32 Index = Pins.IndexOfByPredicate([PinId](const UEdGraphPin* Pin)
    {
        return Pin && Pin->PinId == PinId && Pin->Direction == EGPD_Input;
    });
    if (VariadicInputs <= 2 || Index < 0 || Index >= VariadicInputs)
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("RemoveInput", "Remove graph input"));
    Modify(); GetGraph()->Modify();
    UEdGraphPin* Pin = Pins[Index];
    Pin->Modify();
    Pin->BreakAllPinLinks();
    RemovePin(Pin);
    --VariadicInputs;
    RenumberVariadicInputs();
    GetGraph()->NotifyGraphChanged();
}
void UVoxelLandEditorNode::MoveVariadicInput(FGuid PinId, int32 Direction)
{
    const int32 Index = Pins.IndexOfByPredicate([PinId](const UEdGraphPin* Pin)
    {
        return Pin && Pin->PinId == PinId && Pin->Direction == EGPD_Input;
    });
    const int32 Destination = Index + Direction;
    if ((Direction != -1 && Direction != 1) || Index < 0 || Index >= VariadicInputs ||
        Destination < 0 || Destination >= VariadicInputs || !Pins.IsValidIndex(Destination))
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("MoveInput", "Reorder graph input"));
    Modify(); GetGraph()->Modify();
    Pins[Index]->Modify();
    Pins[Destination]->Modify();
    Pins.Swap(Index, Destination);
    RenumberVariadicInputs();
    GetGraph()->NotifyGraphChanged();
}
void UVoxelLandEditorNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
    TWeakObjectPtr<UVoxelLandEditorNode> WeakNode(const_cast<UVoxelLandEditorNode*>(this));
    if (Kind == int32(VoxelLandGraphFunctionCallId().D))
    {
        FToolMenuSection& Section = Menu->AddSection(TEXT("VoxelLandFunction"), LOCTEXT("FunctionSection", "Function"));
        Section.AddMenuEntry(TEXT("RefreshFunction"), LOCTEXT("RefreshFunctionLabel", "Refresh function interface"),
            LOCTEXT("RefreshFunctionTip", "Preserve matching pin IDs; removed or type-changed pins lose incompatible links."), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([WeakNode]() { if (WeakNode.IsValid()) { WeakNode->RefreshFunctionInterface(); } })));
    }
    if (Kind == int32(EVoxelLandBuiltinNode::Add) || Kind == int32(EVoxelLandBuiltinNode::Multiply))
    {
        FToolMenuSection& Section = Menu->AddSection(TEXT("VoxelLandInputs"), LOCTEXT("Inputs", "Inputs"));
        Section.AddMenuEntry(TEXT("AddInput"), LOCTEXT("AddInputLabel", "Add Input"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakNode]() { if (WeakNode.IsValid()) { WeakNode->AddVariadicInput(); } })));
        const UEdGraphPin* SelectedPin = Context ? Context->Pin : nullptr;
        if (SelectedPin && SelectedPin->GetOwningNode() == this && SelectedPin->Direction == EGPD_Input)
        {
            const FGuid SelectedId = SelectedPin->PinId;
            Section.AddMenuEntry(TEXT("RemoveInput"), LOCTEXT("RemoveInputLabel", "Remove This Input"),
                LOCTEXT("RemoveInputTip", "Removes this input and its connection. At least two inputs must remain."), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([WeakNode, SelectedId]() { if (WeakNode.IsValid()) { WeakNode->RemoveVariadicInput(SelectedId); } })));
            Section.AddMenuEntry(TEXT("MoveInputUp"), LOCTEXT("MoveInputUp", "Move Input Up"),
                LOCTEXT("MoveInputTip", "Changes evaluation order while preserving pin IDs and connections."), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([WeakNode, SelectedId]() { if (WeakNode.IsValid()) { WeakNode->MoveVariadicInput(SelectedId, -1); } })));
            Section.AddMenuEntry(TEXT("MoveInputDown"), LOCTEXT("MoveInputDown", "Move Input Down"),
                LOCTEXT("MoveInputDownTip", "Changes evaluation order while preserving pin IDs and connections."), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([WeakNode, SelectedId]() { if (WeakNode.IsValid()) { WeakNode->MoveVariadicInput(SelectedId, 1); } })));
        }
    }
    EVoxelLandGraphValueType Type = EVoxelLandGraphValueType::Float;
    if (Kind == int32(EVoxelLandBuiltinNode::IntParameter))
    {
        Type = EVoxelLandGraphValueType::Int32;
    }
    else if (Kind == int32(EVoxelLandBuiltinNode::BoolParameter))
    {
        Type = EVoxelLandGraphValueType::Bool;
    }
    else if (Kind != int32(EVoxelLandBuiltinNode::FloatParameter))
    {
        return;
    }
    UVoxelLandTerrainGraph* Asset = GetGraph() ? Cast<UVoxelLandTerrainGraph>(GetGraph()->GetOuter()) : nullptr;
    if (!Asset)
    {
        return;
    }
    FToolMenuSection& Section = Menu->AddSection(TEXT("VoxelLandParameters"), LOCTEXT("Parameters", "Select parameter"));
    for (const FVoxelLandGraphParameter& Parameter : Asset->Parameters)
    {
        if (Parameter.Type != Type || !Parameter.Id.IsValid())
        {
            continue;
        }
        const FGuid Id = Parameter.Id;
        Section.AddMenuEntry(FName(*Id.ToString()), FText::FromString(Parameter.Name), Parameter.Tooltip, FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([WeakNode, Id]()
            {
                if (UVoxelLandEditorNode* Node = WeakNode.Get())
                {
                    const FScopedTransaction Transaction(LOCTEXT("SetParameter", "Select graph parameter"));
                    Node->Modify();
                    Node->GetGraph()->Modify();
                    Node->ParameterId = Id;
                    Node->GetGraph()->NotifyGraphChanged();
                }
            })));
    }
}
#undef LOCTEXT_NAMESPACE
