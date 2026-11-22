#include "Graph/VoxelLandEditorSchema.h"
#include "Data/Assets/VoxelLandM4ControlGraph.h"
#include "../VoxelLandM4EditorBake.h"
#include "Graph/VoxelLandEditorFloat.h"
#include "Operators/VoxelLandOperator.h"
#include "Misc/DefaultValueHelper.h"
#include "ScopedTransaction.h"
#include "Graph/VoxelLandEditorNode.h"
#include "Graph/VoxelLandBuiltinPresentationCatalog.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/VoxelLandBuiltinCatalog.h"
#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Framework/Commands/GenericCommands.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "VoxelLandEditorSchema"

namespace
{
bool ConfigureFunctionTemplate(UVoxelLandEditorNode& Template, const UVoxelLandGraphFunction& Function)
{
	if (Function.SourceVersion != 1 || Function.Parameters.Num() > 63 || !Function.OutputNode.IsValid() ||
	    !Function.OutputPin.IsValid() || (Function.OutputType != EVoxelLandGraphValueType::Float &&
	                                      Function.OutputType != EVoxelLandGraphValueType::Int32 && Function.OutputType
	                                      != EVoxelLandGraphValueType::Bool))
	{
		return false;
	}
	const FVoxelLandGraphNodeRecord* OutputNode = Function.Nodes.FindByPredicate(
		[&Function](const FVoxelLandGraphNodeRecord& Node) {
			return Node.Id == Function.OutputNode;
		});
	if (!OutputNode || !OutputNode->Pins.ContainsByPredicate([&Function](const FVoxelLandGraphPinRecord& Pin) {
		return Pin.Id == Function.OutputPin && Pin.bOutput && Pin.Type == Function.OutputType;
	}))
	{
		return false;
	}
	TArray<FVoxelLandGraphPinRecord> Specs;
	TSet<FGuid> Ids;
	TSet<FString> Names;
	for (const FVoxelLandGraphParameter& Parameter : Function.Parameters)
	{
		const FString Name = Parameter.Name.ToLower();
		if (!Parameter.Id.IsValid() || Name.IsEmpty() || Ids.Contains(Parameter.Id) || Names.Contains(Name) ||
		    (Parameter.Type != EVoxelLandGraphValueType::Float && Parameter.Type != EVoxelLandGraphValueType::Int32 &&
		     Parameter.Type != EVoxelLandGraphValueType::Bool) || !FMath::IsFinite(Parameter.FloatDefault))
		{
			return false;
		}
		Ids.Add(Parameter.Id);
		Names.Add(Name);
		FVoxelLandGraphPinRecord Input;
		Input.InterfaceId = Parameter.Id;
		Input.Name = Parameter.Name;
		Input.Type = Parameter.Type;
		Input.FloatDefault = Parameter.FloatDefault;
		Input.IntDefault = Parameter.IntDefault;
		Input.BoolDefault = Parameter.BoolDefault;
		Specs.Add(MoveTemp(Input));
	}
	FVoxelLandGraphPinRecord Output;
	Output.Name = TEXT("Result");
	Output.Type = Function.OutputType;
	Output.bOutput = true;
	Specs.Add(MoveTemp(Output));
	Template.Kind = int32(VoxelLandGraphFunctionCallId().D);
	Template.FunctionPath = FSoftObjectPath(&Function);
	Template.ExternalTitle = FText::FromString(Function.GetName());
	Template.ExternalPins = MoveTemp(Specs);
	return true;
}

class FVoxelLandFunctionAction : public FEdGraphSchemaAction_NewNode
{
public:
	FVoxelLandFunctionAction(const FText& Category, const FText& Name, const FText& Tooltip)
		: FEdGraphSchemaAction_NewNode(Category, Name, Tooltip, 0) {}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin,
		const FVector2f& Location, bool bSelectNewNode = true) override
	{
		if (!Prepare(ParentGraph)) { return nullptr; }
		return FEdGraphSchemaAction_NewNode::PerformAction(ParentGraph, FromPin, Location, bSelectNewNode);
	}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins,
		const FVector2f& Location, bool bSelectNewNode = true) override
	{
		if (!Prepare(ParentGraph)) { return nullptr; }
		const FScopedTransaction Transaction(LOCTEXT("CreateFunctionCall", "Create function call"));
		UEdGraphNode* Result = FEdGraphSchemaAction_NewNode::PerformAction(
			ParentGraph, FromPins.IsEmpty() ? nullptr : FromPins[0], Location, bSelectNewNode);
		if (!Result) { return nullptr; }
		for (int32 Index = 1; Index < FromPins.Num(); ++Index)
		{
			Result->AutowireNewNode(FromPins[Index]);
		}
		return Result;
	}

private:
	bool Prepare(UEdGraph* ParentGraph)
	{
		if (!IsInGameThread()) { return false; }
		UVoxelLandEditorNode* Template = Cast<UVoxelLandEditorNode>(NodeTemplate);
		FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const bool bExists = Template && Registry.Get().GetAssetByObjectPath(Template->FunctionPath).IsValid();
		UVoxelLandGraphFunction* Function = bExists
			                                    ? Cast<UVoxelLandGraphFunction>(Template->FunctionPath.TryLoad())
			                                    : nullptr;
		if (!ParentGraph || !Template || !Function || ParentGraph->GetOuter() == Function ||
		    !ConfigureFunctionTemplate(*Template, *Function))
		{
			FNotificationInfo Info(LOCTEXT("StaleFunctionAction",
				"Function is missing, invalid, recursive, or its interface changed. Refresh the palette and repair the function."));
			Info.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
			return false;
		}
		return true;
	}
};

bool Reaches(const UEdGraphNode* Start, const UEdGraphNode* Target)
{
	TArray<const UEdGraphNode*> Pending;
	TSet<const UEdGraphNode*> Visited;
	Pending.Add(Start);
	while (!Pending.IsEmpty())
	{
		const UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
		if (Node == Target)
		{
			return true;
		}
		if (Visited.Contains(Node))
		{
			continue;
		}
		Visited.Add(Node);
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked && Linked->GetOwningNode())
					{
						Pending.Add(Linked->GetOwningNode());
					}
				}
			}
		}
	}
	return false;
}
}

void UVoxelLandEditorSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (!Context || !Context->Node)
	{
		return;
	}
	FToolMenuSection& Section = Menu->AddSection(TEXT("VoxelLandEdit"), LOCTEXT("Edit", "Edit"));
	Section.AddMenuEntry(FGenericCommands::Get().Cut);
	Section.AddMenuEntry(FGenericCommands::Get().Copy);
	Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
	Section.AddMenuEntry(FGenericCommands::Get().Delete);
	const UVoxelLandEditorNode* OutputNode = Cast<UVoxelLandEditorNode>(Context->Node.Get());
	UVoxelLandM4ControlGraph* Named = OutputNode && OutputNode->GetGraph()
										? Cast<UVoxelLandM4ControlGraph>(OutputNode->GetGraph()->GetOuter())
										: nullptr;
	if (Named && OutputNode->Kind == int32(EVoxelLandBuiltinNode::M4HeightOutput))
	{
		TWeakObjectPtr<UVoxelLandM4ControlGraph> WeakGraph = Named;
		TWeakObjectPtr<UEdGraph> WeakEditorGraph = OutputNode->GetGraph();
		const FGuid NodeId = OutputNode->NodeGuid;
		Section.AddMenuEntry(TEXT("M4AssignHeight"), LOCTEXT("AssignHeight", "Use as Height Output"), FText(),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakGraph, WeakEditorGraph, NodeId] {
				UVoxelLandM4ControlGraph* Graph = WeakGraph.Get();
				UEdGraph* EditorGraph = WeakEditorGraph.Get();
				if (!Graph || !EditorGraph)
				{
					return;
				}
				const FScopedTransaction Transaction(LOCTEXT("AssignM4Height", "Assign M4 height output"));
				Graph->Modify();
				Graph->HeightOutputId = NodeId;
				Graph->MarkPackageDirty();
				EditorGraph->NotifyGraphChanged();
				Graph->PostEditChange();
			})));
	}
	if (Named && OutputNode->Kind == int32(EVoxelLandBuiltinNode::DensityOutput))
	{
		TWeakObjectPtr<UVoxelLandM4ControlGraph> WeakGraph = Named;
		TWeakObjectPtr<UEdGraph> WeakEditorGraph = OutputNode->GetGraph();
		const FGuid NodeId = OutputNode->NodeGuid;
		auto Assign = [WeakGraph, WeakEditorGraph, NodeId](int32 Slot) {
			UVoxelLandM4ControlGraph* Graph = WeakGraph.Get();
			UEdGraph* EditorGraph = WeakEditorGraph.Get();
			if (!Graph || !EditorGraph)
			{
				return;
			}
			const FScopedTransaction Transaction(LOCTEXT("AssignM4Output", "Assign M4 control output"));
			Graph->Modify();
			Graph->HeightOutputId.Invalidate();
			// Assigning a node to another channel clears its old role.
			if (Graph->ContinentalOutputId == NodeId)
			{
				Graph->ContinentalOutputId.Invalidate();
			}
			if (Graph->MountainOutputId == NodeId)
			{
				Graph->MountainOutputId.Invalidate();
			}
			if (Graph->PeaksOutputId == NodeId)
			{
				Graph->PeaksOutputId.Invalidate();
			}
			if (Slot == 0)
			{
				Graph->ContinentalOutputId = NodeId;
			}
			else if (Slot == 1)
			{
				Graph->MountainOutputId = NodeId;
			}
			else
			{
				Graph->PeaksOutputId = NodeId;
			}
			Graph->MarkPackageDirty();
			EditorGraph->NotifyGraphChanged();
			FString Error;
			VoxelLandM4Editor::CompileNamedGraph(*Graph, Error);
			Graph->PostEditChange();
		};
		Section.AddMenuEntry(TEXT("M4AssignContinental"), LOCTEXT("AssignContinental", "Use as Continental Output"),
			FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Assign] { Assign(0); })));
		Section.AddMenuEntry(TEXT("M4AssignMountain"), LOCTEXT("AssignMountain", "Use as Mountain Output"), FText(),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Assign] { Assign(1); })));
		Section.AddMenuEntry(TEXT("M4AssignPeaks"), LOCTEXT("AssignPeaks", "Use as Peaks Output"), FText(),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Assign] { Assign(2); })));
	}
	if (Context->Pin && !Context->Pin->LinkedTo.IsEmpty())
	{
		TWeakObjectPtr<UEdGraphNode> Node = const_cast<UEdGraphNode*>(Context->Node.Get());
		const FGuid PinId = Context->Pin->PinId;
		Section.AddMenuEntry(TEXT("BreakPinLinks"), LOCTEXT("BreakLinks", "Break pin links"), FText(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Node, PinId]() {
				if (UEdGraphNode* Owner = Node.Get())
				{
					UEdGraphPin* const* Pin = Owner->Pins.FindByPredicate([PinId](const UEdGraphPin* Candidate) {
						return Candidate && Candidate->PinId == PinId;
					});
					if (Pin && *Pin)
					{
						const FScopedTransaction Transaction(LOCTEXT("BreakPinTransaction", "Break graph links"));
						Owner->Modify();
						Owner->GetGraph()->Modify();
						Owner->GetGraph()->GetSchema()->BreakPinLinks(**Pin, true);
					}
				}
			})));
	}
}

void UVoxelLandEditorSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	TSharedPtr<FEdGraphSchemaAction_NewNode> Comment = MakeShared<FEdGraphSchemaAction_NewNode>(
		LOCTEXT("Organization", "Organization"), LOCTEXT("Comment", "Comment"),
		LOCTEXT("CommentTip", "Add an editor-only comment."), 0);
	Comment->NodeTemplate = NewObject<UEdGraphNode_Comment>(ContextMenuBuilder.OwnerOfTemporaries);
	ContextMenuBuilder.AddAction(Comment);
	TArray<EVoxelLandBuiltinNode> Builtins;
	FString CatalogError;
	if (FVoxelLandBuiltinPresentationCatalog::Validate(CatalogError))
	{
		FVoxelLandBuiltinCatalog::GetKinds(Builtins);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelLand builtin catalog unavailable: %s"), *CatalogError);
	}
	for (EVoxelLandBuiltinNode Builtin : Builtins)
	{
		const int32 Kind = int32(Builtin);
		if (Builtin == EVoxelLandBuiltinNode::M4HeightOutput
			&& (!ContextMenuBuilder.CurrentGraph
				|| !ContextMenuBuilder.CurrentGraph->GetOuter()->IsA<UVoxelLandM4ControlGraph>()))
		{
			continue;
		}
		if (ContextMenuBuilder.CurrentGraph && ContextMenuBuilder.CurrentGraph->GetOuter()->IsA<
			    UVoxelLandGraphFunction>() &&
		    (Kind == int32(EVoxelLandBuiltinNode::DensityOutput) || Kind ==
		     int32(EVoxelLandBuiltinNode::SmoothDensity)))
		{
			continue;
		}
		const FText Category = FVoxelLandBuiltinPresentationCatalog::Category(Builtin);
		UVoxelLandEditorNode* Template = NewObject<UVoxelLandEditorNode>(ContextMenuBuilder.OwnerOfTemporaries);
		Template->Kind = Kind;
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action = MakeShared<FEdGraphSchemaAction_NewNode>(
			Category, UVoxelLandEditorNode::DisplayName(Kind), Template->GetTooltipText(), 0);
		Action->NodeTemplate = Template;
		ContextMenuBuilder.AddAction(Action);
	}
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> FunctionAssets;
	Registry.Get().GetAssetsByClass(UVoxelLandGraphFunction::StaticClass()->GetClassPathName(), FunctionAssets);
	for (const FAssetData& FunctionAsset : FunctionAssets)
	{
		UVoxelLandGraphFunction* Function = Cast<UVoxelLandGraphFunction>(FunctionAsset.GetAsset());
		if (!Function || (ContextMenuBuilder.CurrentGraph && ContextMenuBuilder.CurrentGraph->GetOuter() == Function))
		{
			continue;
		}
		UVoxelLandEditorNode* Template = NewObject<UVoxelLandEditorNode>(ContextMenuBuilder.OwnerOfTemporaries);
		if (!ConfigureFunctionTemplate(*Template, *Function))
		{
			continue;
		}
		TSharedPtr<FVoxelLandFunctionAction> Action = MakeShared<FVoxelLandFunctionAction>(
			LOCTEXT("Functions", "Graph functions"), Template->ExternalTitle,
			LOCTEXT("FunctionActionTip", "Call this reusable terrain graph function."));
		Action->NodeTemplate = Template;
		ContextMenuBuilder.AddAction(Action);
	}
	TArray<FVoxelLandEditorOperatorPresentation> Presentations;
	FVoxelLandEditorOperatorRegistry::GetPresentations(Presentations);
	for (const FVoxelLandEditorOperatorPresentation& Presentation : Presentations)
	{
		TSharedPtr<FEdGraphSchemaAction_NewNode> Action = MakeShared<FEdGraphSchemaAction_NewNode>(
			Presentation.Category, Presentation.DisplayName, Presentation.Tooltip, 0);
		UVoxelLandEditorNode* Template = NewObject<UVoxelLandEditorNode>(ContextMenuBuilder.OwnerOfTemporaries);
		Template->Kind = 0;
		Template->ExternalOperatorId = Presentation.OperatorId;
		Template->OperatorVersion = Presentation.Version;
		Template->Configuration = Presentation.DefaultConfiguration;
		Template->ExternalTitle = Presentation.DisplayName;
		Template->ExternalPins = Presentation.Inputs;
		FVoxelLandGraphPinRecord Output;
		Output.Name = Presentation.OutputName;
		Output.Type = Presentation.OutputType;
		Output.bOutput = true;
		Template->ExternalPins.Add(MoveTemp(Output));
		Action->NodeTemplate = Template;
		ContextMenuBuilder.AddAction(Action);
	}
}

const FPinConnectionResponse UVoxelLandEditorSchema::CanCreateConnection(const UEdGraphPin* A,
	const UEdGraphPin* B) const
{
	if (!A || !B || A->GetOwningNode() == B->GetOwningNode() || A->Direction == B->Direction)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("Direction", "Connect an output to an input on another node."));
	}
	if (A->PinType.PinCategory != B->PinType.PinCategory)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("Type", "Pin types must match; use an explicit conversion."));
	}
	const UEdGraphPin* Output = A->Direction == EGPD_Output ? A : B;
	const UEdGraphPin* Input = A->Direction == EGPD_Input ? A : B;
	const UVoxelLandEditorNode* FromNode = Cast<UVoxelLandEditorNode>(Output->GetOwningNode());
	const UVoxelLandEditorNode* ToNode = Cast<UVoxelLandEditorNode>(Input->GetOwningNode());
	bool bFromFilter = FromNode && FromNode->Kind == int32(EVoxelLandBuiltinNode::SmoothDensity);
	if (FromNode && FromNode->Kind == 0)
	{
		const auto Provider = FVoxelLandOperatorRegistry::Resolve(FromNode->ExternalOperatorId,
			FromNode->OperatorVersion);
		bFromFilter = Provider && Provider->Stage() == EVoxelLandOperatorStage::OneCellFilter;
	}
	if (bFromFilter &&
	    (!ToNode || ToNode->Kind != int32(EVoxelLandBuiltinNode::DensityOutput) ||
	     (!Output->LinkedTo.IsEmpty() && !Output->LinkedTo.Contains(Input))))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW,
			LOCTEXT("SmoothOutput", "A filter must feed only Density Output."));
	}
	FVoxelLandBuiltinNodeSpec SmoothSpec;
	const bool bSmooth = ToNode && ToNode->Kind == int32(EVoxelLandBuiltinNode::SmoothDensity);
	const bool bHasSmoothSpec = bSmooth && FVoxelLandBuiltinCatalog::Find(EVoxelLandBuiltinNode::SmoothDensity, SmoothSpec);
	const FVoxelLandBuiltinPinSpec* SmoothPin = bHasSmoothSpec ? SmoothSpec.Pins.FindByPredicate(
		[Input](const FVoxelLandBuiltinPinSpec& PinSpec) { return !PinSpec.bOutput && FName(*PinSpec.Name) == Input->PinName; }) : nullptr;
	const bool bSmoothUniform = SmoothPin ? SmoothPin->bUniform : Input->PinName != FName(TEXT("Density"));
	if (bSmooth && bSmoothUniform)
	{
		const int32 Expected = Input->PinName == FName(TEXT("Enabled"))
			                       ? int32(EVoxelLandBuiltinNode::BoolParameter)
			                       : int32(EVoxelLandBuiltinNode::FloatParameter);
		if (!FromNode || FromNode->Kind != Expected)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("SmoothUniform",
				"Filter controls accept direct parameter references or unconnected defaults."));
		}
	}
	FVoxelLandBuiltinNodeSpec NoiseSpec;
	if (ToNode && FVoxelLandBuiltinCatalog::Find(EVoxelLandBuiltinNode(ToNode->Kind), NoiseSpec) && NoiseSpec.bNoise)
	{
		const bool bUniform = NoiseSpec.Pins.ContainsByPredicate([Input](const FVoxelLandBuiltinPinSpec& PinSpec)
		{
			return PinSpec.bUniform && FName(*PinSpec.Name) == Input->PinName;
		});
		if (bUniform)
		{
			const int32 Expected = Input->PinType.PinCategory == TEXT("int")
				                       ? int32(EVoxelLandBuiltinNode::IntParameter)
				                       : int32(EVoxelLandBuiltinNode::FloatParameter);
			if (!FromNode || FromNode->Kind != Expected)
			{
				return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("NoiseUniform",
					"Noise controls accept direct parameter references or unconnected defaults."));
			}
		}
	}
	if (Reaches(Input->GetOwningNode(), Output->GetOwningNode()))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("Cycle", "Connections cannot form cycles."));
	}
	if (!Input->LinkedTo.IsEmpty())
	{
		return FPinConnectionResponse(Input == A ? CONNECT_RESPONSE_BREAK_OTHERS_A : CONNECT_RESPONSE_BREAK_OTHERS_B,
			LOCTEXT("Replace", "Replace existing input connection."));
	}
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText());
}

FLinearColor UVoxelLandEditorSchema::GetPinTypeColor(const FEdGraphPinType& Type) const
{
	if (Type.PinCategory == TEXT("bool"))
	{
		return FLinearColor(0.8f, 0.12f, 0.12f);
	}
	if (Type.PinCategory == TEXT("int"))
	{
		return FLinearColor(0.15f, 0.75f, 0.8f);
	}
	return FLinearColor(0.3f, 0.8f, 0.3f);
}
#undef LOCTEXT_NAMESPACE

void UVoxelLandEditorSchema::TrySetDefaultValue(UEdGraphPin& Pin, const FString& NewDefaultValue,
	bool bMarkAsModified) const
{
	if (Pin.Direction != EGPD_Input || !Pin.LinkedTo.IsEmpty() || Pin.bDefaultValueIsReadOnly)
	{
		return;
	}
	FString Value;
	if (Pin.PinType.PinCategory == TEXT("float"))
	{
		float Parsed = 0.0f;
		if (!FDefaultValueHelper::ParseFloat(NewDefaultValue, Parsed) || !FMath::IsFinite(Parsed))
		{
			return;
		}
		Value = FVoxelLandEditorFloat::Format(Parsed);
	}
	else if (Pin.PinType.PinCategory == TEXT("int"))
	{
		int32 Parsed = 0;
		if (!LexTryParseString(Parsed, *NewDefaultValue))
		{
			return;
		}
		Value = LexToString(Parsed);
	}
	else if (Pin.PinType.PinCategory == TEXT("bool"))
	{
		if (!NewDefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) && !NewDefaultValue.Equals(TEXT("false"),
			    ESearchCase::IgnoreCase))
		{
			return;
		}
		Value = NewDefaultValue.ToLower();
	}
	else
	{
		return;
	}
	if (Pin.DefaultValue == Value)
	{
		return;
	}
	UEdGraphNode* Node = Pin.GetOwningNode();
	if (!Node || !Node->GetGraph())
	{
		return;
	}
	const FScopedTransaction Transaction(NSLOCTEXT("VoxelLand", "EditPinDefault", "Edit terrain graph input"));
	Node->GetGraph()->Modify();
	Node->Modify();
	Pin.Modify();
	Super::TrySetDefaultValue(Pin, Value, bMarkAsModified);
}