#include "Graph/VoxelLandEditorNode.h"
#include "Graph/VoxelLandBuiltinPresentationCatalog.h"
#include "Graph/VoxelLandEditorFloat.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/VoxelLandBuiltinCatalog.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandM4ControlGraph.h"
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

EVoxelLandBuiltinNode VariadicContractKind(int32 Kind)
{
    return Kind == int32(EVoxelLandBuiltinNode::Multiply) ? EVoxelLandBuiltinNode::Multiply : EVoxelLandBuiltinNode::Add;
}
}

FText UVoxelLandEditorNode::DisplayName(int32 InKind)
{
	return FVoxelLandBuiltinPresentationCatalog::Title(static_cast<EVoxelLandBuiltinNode>(InKind));
}

FText UVoxelLandEditorNode::GetNodeTitle(ENodeTitleType::Type) const
{
	if (Kind == int32(EVoxelLandBuiltinNode::DensityOutput) && GetGraph())
	{
		if (const UVoxelLandM4ControlGraph* Named = Cast<UVoxelLandM4ControlGraph>(GetGraph()->GetOuter()))
		{
			if (Named->ContinentalOutputId == NodeGuid)
			{
				return LOCTEXT("M4Continental", "Continental Output");
			}
			if (Named->MountainOutputId == NodeGuid)
			{
				return LOCTEXT("M4Mountain", "Mountain Output");
			}
			if (Named->PeaksOutputId == NodeGuid)
			{
				return LOCTEXT("M4Peaks", "Peaks Output");
			}
			return LOCTEXT("M4Unassigned", "Unassigned Control Output");
		}
	}
	if (Kind == 0 || Kind == int32(VoxelLandGraphFunctionCallId().D))
	{
		return ExternalTitle.IsEmpty()
			       ? FText::FromString(FString::Printf(TEXT("External operator %s v%d"), *ExternalOperatorId.ToString(),
				       OperatorVersion))
			       : ExternalTitle;
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
			       ? Presentation.NodeColor
			       : FLinearColor(0.25f, 0.25f, 0.25f);
	}
	return FVoxelLandBuiltinPresentationCatalog::TitleColor(static_cast<EVoxelLandBuiltinNode>(Kind));
}

FText UVoxelLandEditorNode::GetTooltipText() const
{
	if (Kind == int32(VoxelLandGraphFunctionCallId().D))
	{
		return LOCTEXT("FunctionCallTooltip",
			"Calls a reusable terrain graph function. Refresh its interface after changing the function inputs or output type.");
	}
	if (Kind == 0)
	{
		FVoxelLandEditorOperatorPresentation Presentation;
		if (FVoxelLandEditorOperatorRegistry::FindPresentation(ExternalOperatorId, OperatorVersion, Presentation))
		{
			return Presentation.Tooltip;
		}
		return LOCTEXT("MissingProvider",
			"The editor presentation or runtime provider for this operator is unavailable.");
	}
	return FVoxelLandBuiltinPresentationCatalog::Description(static_cast<EVoxelLandBuiltinNode>(Kind));
}

FText UVoxelLandEditorNode::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	if (!Pin || Pin->Direction != EGPD_Input)
	{
		return Super::GetPinDisplayName(Pin);
	}
	FText Label;
	return FVoxelLandBuiltinPresentationCatalog::TryPinLabel(static_cast<EVoxelLandBuiltinNode>(Kind), Pin->PinName, Label)
		? Label : Super::GetPinDisplayName(Pin);
}

void UVoxelLandEditorNode::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	if (Kind == int32(VoxelLandGraphFunctionCallId().D))
	{
		const UVoxelLandGraphFunction* Function = Cast<UVoxelLandGraphFunction>(FunctionPath.ResolveObject());
		if (Pin.Direction == EGPD_Input && Function)
		{
			const FGuid InterfaceId = FunctionPinIds.FindRef(Pin.PinId);
			const FVoxelLandGraphParameter* Parameter = Function->Parameters.FindByPredicate(
				[InterfaceId](const FVoxelLandGraphParameter& Item) {
					return Item.Id == InterfaceId;
				});
			if (Parameter && !Parameter->Tooltip.IsEmpty())
			{
				HoverTextOut = Parameter->Tooltip.ToString();
				return;
			}
		}
		HoverTextOut = Pin.Direction == EGPD_Output
			               ? TEXT("The function's typed result.")
			               : TEXT("Typed function input; an unconnected pin uses its displayed default.");
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
	HoverTextOut = FVoxelLandBuiltinPresentationCatalog::PinTooltip(static_cast<EVoxelLandBuiltinNode>(Kind),
		Pin.PinName, Pin.PinType.PinCategory, Pin.Direction == EGPD_Output).ToString();
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
			const FName Category = Spec.Type == EVoxelLandGraphValueType::Float
				                       ? FloatPin
				                       : Spec.Type == EVoxelLandGraphValueType::Int32
				                       ? IntPin
				                       : BoolPin;
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
	    (Function->OutputType != EVoxelLandGraphValueType::Float && Function->OutputType !=
	     EVoxelLandGraphValueType::Int32 &&
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
	const FVoxelLandGraphNodeRecord* FunctionOutput = Function->Nodes.FindByPredicate(
		[Function](const FVoxelLandGraphNodeRecord& Node) {
			return Node.Id == Function->OutputNode;
		});
	if (!FunctionOutput || !FunctionOutput->Pins.ContainsByPredicate([Function](const FVoxelLandGraphPinRecord& Pin) {
		return Pin.Id == Function->OutputPin && Pin.bOutput && Pin.Type == Function->OutputType;
	}))
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
			if (Candidate && Candidate->Direction == EGPD_Input && FunctionPinIds.FindRef(Candidate->PinId) == Parameter
			    .Id)
			{
				Pin = Candidate;
				break;
			}
		}
		const FName Category = Parameter.Type == EVoxelLandGraphValueType::Float
			                       ? FloatPin
			                       : Parameter.Type == EVoxelLandGraphValueType::Int32
			                       ? IntPin
			                       : BoolPin;
		if (!Pin)
		{
			Pin = CreatePin(EGPD_Input, Category, FName(*Parameter.Name));
		}
		const bool bTypeChanged = Pin->PinType.PinCategory != Category;
		if (bTypeChanged)
		{
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode())
				{
					Linked->GetOwningNode()->Modify();
					Linked->Modify();
				}
			}
			Pin->Modify();
			Pin->BreakAllPinLinks();
			Pin->PinType.PinCategory = Category;
		}
		if (bTypeChanged || !PreviousPins.Contains(Pin))
		{
			Pin->DefaultValue = Parameter.Type == EVoxelLandGraphValueType::Float
				                    ? FVoxelLandEditorFloat::Format(Parameter.FloatDefault)
				                    : Parameter.Type == EVoxelLandGraphValueType::Int32
				                    ? LexToString(Parameter.IntDefault)
				                    : Parameter.BoolDefault
				                    ? TEXT("true")
				                    : TEXT("false");
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
		if (Candidate && Candidate->Direction == EGPD_Output)
		{
			Output = Candidate;
			break;
		}
	}
	const FName OutputCategory = Function.OutputType == EVoxelLandGraphValueType::Float
		                             ? FloatPin
		                             : Function.OutputType == EVoxelLandGraphValueType::Int32
		                             ? IntPin
		                             : BoolPin;
	if (!Output) { Output = CreatePin(EGPD_Output, OutputCategory, TEXT("Result")); }
	if (Output->PinType.PinCategory != OutputCategory)
	{
		for (UEdGraphPin* Linked : Output->LinkedTo)
		{
			if (Linked && Linked->GetOwningNode())
			{
				Linked->GetOwningNode()->Modify();
				Linked->Modify();
			}
		}
		Output->Modify();
		Output->BreakAllPinLinks();
		Output->PinType.PinCategory = OutputCategory;
	}
	Ordered.Add(Output);
}

void UVoxelLandEditorNode::RemoveOldFunctionPins(const TArray<UEdGraphPin*>& PreviousPins,
	const TArray<UEdGraphPin*>& Ordered)
{
	for (UEdGraphPin* OldPin : PreviousPins)
	{
		if (OldPin && !Ordered.Contains(OldPin))
		{
			for (UEdGraphPin* Linked : OldPin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode())
				{
					Linked->GetOwningNode()->Modify();
					Linked->Modify();
				}
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
		Spec.Type = Pin->PinType.PinCategory == IntPin
			            ? EVoxelLandGraphValueType::Int32
			            : Pin->PinType.PinCategory == BoolPin
			            ? EVoxelLandGraphValueType::Bool
			            : EVoxelLandGraphValueType::Float;
		if (Spec.Type == EVoxelLandGraphValueType::Float) { LexTryParseString(Spec.FloatDefault, *Pin->DefaultValue); }
		else if (Spec.Type == EVoxelLandGraphValueType::Int32)
		{
			LexTryParseString(Spec.IntDefault, *Pin->DefaultValue);
		}
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
	FVoxelLandBuiltinNodeSpec Spec;
	if (!FVoxelLandBuiltinCatalog::Find(VariadicContractKind(Kind), Spec) || VariadicInputs >= Spec.MaxVariadicInputs)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddInput", "Add graph input"));
	Modify();
	GetGraph()->Modify();
	++VariadicInputs;
	FVoxelLandBuiltinNodeSpec UpdatedSpec;
	const bool bHasPinSpec = FVoxelLandBuiltinCatalog::Find(VariadicContractKind(Kind), UpdatedSpec, VariadicInputs) &&
		UpdatedSpec.Pins.IsValidIndex(VariadicInputs - 1);
	const FString PinName = bHasPinSpec ? UpdatedSpec.Pins[VariadicInputs - 1].Name :
		FString::Printf(TEXT("Input %d"), VariadicInputs);
	UEdGraphPin* Pin = CreatePin(EGPD_Input, FloatPin, *PinName);
	Pin->DefaultValue = bHasPinSpec ? UpdatedSpec.Pins[VariadicInputs - 1].DefaultLiteral :
		(Kind == int32(EVoxelLandBuiltinNode::Multiply) ? TEXT("1") : TEXT("0"));
	Pin->AutogeneratedDefaultValue = Pin->DefaultValue;
	Pins.Remove(Pin);
	Pins.Insert(Pin, Pins.Num() - 1);
	GetGraph()->NotifyGraphChanged();
}

void UVoxelLandEditorNode::RenumberVariadicInputs()
{
	FVoxelLandBuiltinNodeSpec Spec;
	const bool bHasSpec = FVoxelLandBuiltinCatalog::Find(VariadicContractKind(Kind), Spec, VariadicInputs);
	for (int32 Index = 0; Index < VariadicInputs; ++Index)
	{
		UEdGraphPin* Pin = Pins.IsValidIndex(Index) ? Pins[Index] : nullptr;
		if (Pin && Pin->Direction == EGPD_Input)
		{
			Pin->Modify();
			const FString PinName = bHasSpec && Spec.Pins.IsValidIndex(Index) ? Spec.Pins[Index].Name :
				FString::Printf(TEXT("Input %d"), Index + 1);
			Pin->PinName = FName(*PinName);
		}
	}
}

void UVoxelLandEditorNode::RemoveVariadicInput(FGuid PinId)
{
	const int32 Index = Pins.IndexOfByPredicate([PinId](const UEdGraphPin* Pin) {
		return Pin && Pin->PinId == PinId && Pin->Direction == EGPD_Input;
	});
	FVoxelLandBuiltinNodeSpec Spec;
	if (!FVoxelLandBuiltinCatalog::Find(VariadicContractKind(Kind), Spec) ||
		VariadicInputs <= Spec.MinVariadicInputs || Index < 0 || Index >= VariadicInputs)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("RemoveInput", "Remove graph input"));
	Modify();
	GetGraph()->Modify();
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
	const int32 Index = Pins.IndexOfByPredicate([PinId](const UEdGraphPin* Pin) {
		return Pin && Pin->PinId == PinId && Pin->Direction == EGPD_Input;
	});
	const int32 Destination = Index + Direction;
	if ((Direction != -1 && Direction != 1) || Index < 0 || Index >= VariadicInputs ||
	    Destination < 0 || Destination >= VariadicInputs || !Pins.IsValidIndex(Destination))
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("MoveInput", "Reorder graph input"));
	Modify();
	GetGraph()->Modify();
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
			LOCTEXT("RefreshFunctionTip",
				"Preserve matching pin IDs; removed or type-changed pins lose incompatible links."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakNode]() {
				if (WeakNode.IsValid()) { WeakNode->RefreshFunctionInterface(); }
			})));
	}
	if (Kind == int32(EVoxelLandBuiltinNode::Add) || Kind == int32(EVoxelLandBuiltinNode::Multiply))
	{
		FToolMenuSection& Section = Menu->AddSection(TEXT("VoxelLandInputs"), LOCTEXT("Inputs", "Inputs"));
		Section.AddMenuEntry(TEXT("AddInput"), LOCTEXT("AddInputLabel", "Add Input"), FText(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakNode]() {
				if (WeakNode.IsValid()) { WeakNode->AddVariadicInput(); }
			})));
		const UEdGraphPin* SelectedPin = Context ? Context->Pin : nullptr;
		if (SelectedPin && SelectedPin->GetOwningNode() == this && SelectedPin->Direction == EGPD_Input)
		{
			const FGuid SelectedId = SelectedPin->PinId;
			Section.AddMenuEntry(TEXT("RemoveInput"), LOCTEXT("RemoveInputLabel", "Remove This Input"),
				LOCTEXT("RemoveInputTip", "Removes this input and its connection. At least two inputs must remain."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([WeakNode, SelectedId]() {
					if (WeakNode.IsValid()) { WeakNode->RemoveVariadicInput(SelectedId); }
				})));
			Section.AddMenuEntry(TEXT("MoveInputUp"), LOCTEXT("MoveInputUp", "Move Input Up"),
				LOCTEXT("MoveInputTip", "Changes evaluation order while preserving pin IDs and connections."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([WeakNode, SelectedId]() {
					if (WeakNode.IsValid()) { WeakNode->MoveVariadicInput(SelectedId, -1); }
				})));
			Section.AddMenuEntry(TEXT("MoveInputDown"), LOCTEXT("MoveInputDown", "Move Input Down"),
				LOCTEXT("MoveInputDownTip", "Changes evaluation order while preserving pin IDs and connections."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([WeakNode, SelectedId]() {
					if (WeakNode.IsValid()) { WeakNode->MoveVariadicInput(SelectedId, 1); }
				})));
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
	FToolMenuSection& Section = Menu->
		AddSection(TEXT("VoxelLandParameters"), LOCTEXT("Parameters", "Select parameter"));
	for (const FVoxelLandGraphParameter& Parameter : Asset->Parameters)
	{
		if (Parameter.Type != Type || !Parameter.Id.IsValid())
		{
			continue;
		}
		const FGuid Id = Parameter.Id;
		Section.AddMenuEntry(FName(*Id.ToString()), FText::FromString(Parameter.Name), Parameter.Tooltip, FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakNode, Id]() {
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