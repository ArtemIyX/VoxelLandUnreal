#include "Graph/VoxelLandEditorSchema.h"
#include "Graph/VoxelLandEditorFloat.h"
#include "Operators/VoxelLandOperator.h"
#include "Misc/DefaultValueHelper.h"
#include "ScopedTransaction.h"
#include "Graph/VoxelLandEditorNode.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Framework/Commands/GenericCommands.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "VoxelLandEditorSchema"
namespace
{
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
    Section.AddMenuEntry(FGenericCommands::Get().Copy);
    Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
    Section.AddMenuEntry(FGenericCommands::Get().Delete);
    if (Context->Pin && !Context->Pin->LinkedTo.IsEmpty())
    {
        TWeakObjectPtr<UEdGraphNode> Node = const_cast<UEdGraphNode*>(Context->Node.Get());
        const FGuid PinId = Context->Pin->PinId;
        Section.AddMenuEntry(TEXT("BreakPinLinks"), LOCTEXT("BreakLinks", "Break pin links"), FText(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([Node, PinId]()
            {
                if (UEdGraphNode* Owner = Node.Get())
                {
                    UEdGraphPin* const* Pin = Owner->Pins.FindByPredicate([PinId](const UEdGraphPin* Candidate)
                    {
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
        LOCTEXT("Organization", "Organization"), LOCTEXT("Comment", "Comment"), LOCTEXT("CommentTip", "Add an editor-only comment."), 0);
    Comment->NodeTemplate = NewObject<UEdGraphNode_Comment>(ContextMenuBuilder.OwnerOfTemporaries);
    ContextMenuBuilder.AddAction(Comment);
    for (int32 Kind = int32(EVoxelLandBuiltinNode::DensityOutput); Kind <= int32(EVoxelLandBuiltinNode::SmoothDensity); ++Kind)
    {
        FText Category = LOCTEXT("Math", "Math and control");
        if (Kind <= int32(EVoxelLandBuiltinNode::FloatParameter) || (Kind >= int32(EVoxelLandBuiltinNode::IntConstant) && Kind <= int32(EVoxelLandBuiltinNode::BoolParameter)))
        {
            Category = LOCTEXT("Inputs", "Inputs and output");
        }
        else if (Kind == int32(EVoxelLandBuiltinNode::SmoothDensity))
        {
            Category = LOCTEXT("Filters", "Density filters");
        }
        else if (Kind >= int32(EVoxelLandBuiltinNode::RerouteFloat))
        {
            Category = LOCTEXT("Reroutes", "Organization");
        }
        else if (Kind >= int32(EVoxelLandBuiltinNode::Perlin2))
        {
            Category = LOCTEXT("Noise", "Noise");
        }
        else if (Kind >= int32(EVoxelLandBuiltinNode::Plane))
        {
            Category = LOCTEXT("Shapes", "Shapes");
        }
        TSharedPtr<FEdGraphSchemaAction_NewNode> Action = MakeShared<FEdGraphSchemaAction_NewNode>(Category, UVoxelLandEditorNode::DisplayName(Kind), FText(), 0);
        UVoxelLandEditorNode* Template = NewObject<UVoxelLandEditorNode>(ContextMenuBuilder.OwnerOfTemporaries);
        Template->Kind = Kind;
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
const FPinConnectionResponse UVoxelLandEditorSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
    if (!A || !B || A->GetOwningNode() == B->GetOwningNode() || A->Direction == B->Direction)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("Direction", "Connect an output to an input on another node."));
    }
    if (A->PinType.PinCategory != B->PinType.PinCategory)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("Type", "Pin types must match; use an explicit conversion."));
    }
    const UEdGraphPin* Output = A->Direction == EGPD_Output ? A : B;
    const UEdGraphPin* Input = A->Direction == EGPD_Input ? A : B;
    const UVoxelLandEditorNode* FromNode = Cast<UVoxelLandEditorNode>(Output->GetOwningNode());
    const UVoxelLandEditorNode* ToNode = Cast<UVoxelLandEditorNode>(Input->GetOwningNode());
    bool bFromFilter = FromNode && FromNode->Kind == int32(EVoxelLandBuiltinNode::SmoothDensity);
    if (FromNode && FromNode->Kind == 0)
    {
        const auto Provider = FVoxelLandOperatorRegistry::Resolve(FromNode->ExternalOperatorId, FromNode->OperatorVersion);
        bFromFilter = Provider && Provider->Stage() == EVoxelLandOperatorStage::OneCellFilter;
    }
    if (bFromFilter &&
        (!ToNode || ToNode->Kind != int32(EVoxelLandBuiltinNode::DensityOutput) ||
         (!Output->LinkedTo.IsEmpty() && !Output->LinkedTo.Contains(Input))))
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("SmoothOutput", "A filter must feed only Density Output."));
    }
    if (ToNode && ToNode->Kind == int32(EVoxelLandBuiltinNode::SmoothDensity) && Input->PinName != FName(TEXT("Density")))
    {
        const int32 Expected = Input->PinName == FName(TEXT("Enabled")) ? int32(EVoxelLandBuiltinNode::BoolParameter) :
            int32(EVoxelLandBuiltinNode::FloatParameter);
        if (!FromNode || FromNode->Kind != Expected)
        {
            return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("SmoothUniform", "Filter controls accept direct parameter references or unconnected defaults."));
        }
    }
    if (ToNode && ToNode->Kind >= int32(EVoxelLandBuiltinNode::Perlin2) && ToNode->Kind <= int32(EVoxelLandBuiltinNode::Simplex3))
    {
        const bool bUniform = Input->PinName == TEXT("Frequency") || Input->PinName == TEXT("Octaves") ||
            Input->PinName == TEXT("Gain") || Input->PinName == TEXT("Lacunarity");
        if (bUniform)
        {
            const int32 Expected = Input->PinType.PinCategory == TEXT("int") ? int32(EVoxelLandBuiltinNode::IntParameter) :
                int32(EVoxelLandBuiltinNode::FloatParameter);
            if (!FromNode || FromNode->Kind != Expected)
            {
                return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("NoiseUniform", "Noise controls accept direct parameter references or unconnected defaults."));
            }
        }
    }
    if (Reaches(Input->GetOwningNode(), Output->GetOwningNode()))
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("Cycle", "Connections cannot form cycles."));
    }
    if (!Input->LinkedTo.IsEmpty())
    {
        return FPinConnectionResponse(Input == A ? CONNECT_RESPONSE_BREAK_OTHERS_A : CONNECT_RESPONSE_BREAK_OTHERS_B, LOCTEXT("Replace", "Replace existing input connection."));
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

void UVoxelLandEditorSchema::TrySetDefaultValue(UEdGraphPin& Pin, const FString& NewDefaultValue, bool bMarkAsModified) const
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
        if (!NewDefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) && !NewDefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
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
