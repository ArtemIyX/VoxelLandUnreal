#include "VoxelLandGraphPinFactory.h"
#include "../Authoring/VoxelLandFloatTypeInterface.h"
#include "../Authoring/VoxelLandEditorFloat.h"
#include "VoxelLandEditorNode.h"
#include "VoxelLandEditorSchema.h"
#include "SGraphPin.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"

class SVoxelLandTypedGraphPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SVoxelLandTypedGraphPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, UEdGraphPin* Pin) { SGraphPin::Construct(SGraphPin::FArguments(), Pin); }

public:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		if (GraphPinObj->PinType.PinCategory == TEXT("bool"))
		{
			return SNew(SCheckBox)
				.Visibility(this, &SVoxelLandTypedGraphPin::GetDefaultValueVisibility)
				.IsEnabled(this, &SVoxelLandTypedGraphPin::GetDefaultValueIsEditable)
				.IsChecked_Lambda([this]() {
					return GraphPinObj->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase)
							 ? ECheckBoxState::Checked
							 : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
					SetValue(State == ECheckBoxState::Checked ? TEXT("true") : TEXT("false"));
				});
		}
		if (GraphPinObj->PinType.PinCategory == TEXT("int"))
		{
			return SNew(SNumericEntryBox<int32>)
				.Visibility(this, &SVoxelLandTypedGraphPin::GetDefaultValueVisibility)
				.IsEnabled(this, &SVoxelLandTypedGraphPin::GetDefaultValueIsEditable)
				.Value_Lambda([this]() -> TOptional<int32> {
					int32 Value = 0;
					return LexTryParseString(Value, *GraphPinObj->DefaultValue) ? TOptional<int32>(Value)
																				: TOptional<int32>();
				})
				.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { SetValue(LexToString(Value)); });
		}
		return SNew(SNumericEntryBox<float>)
			.TypeInterface(MakeShared<FVoxelLandFloatTypeInterface>())
			.Visibility(this, &SVoxelLandTypedGraphPin::GetDefaultValueVisibility)
			.IsEnabled(this, &SVoxelLandTypedGraphPin::GetDefaultValueIsEditable)
			.Value_Lambda([this]() -> TOptional<float> {
				float Value = 0.0f;
				return LexTryParseString(Value, *GraphPinObj->DefaultValue) && FMath::IsFinite(Value)
						 ? TOptional<float>(Value)
						 : TOptional<float>();
			})
			.OnValueCommitted_Lambda(
				[this](float Value, ETextCommit::Type) { SetValue(FVoxelLandEditorFloat::Format(Value)); });
	}

private:
	// ReSharper disable once CppMemberFunctionMayBeConst
	void SetValue(const FString& Value)
	{
		if (GraphPinObj && GraphPinObj->GetSchema())
		{
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Value);
		}
	}
};

TSharedPtr<SGraphPin> FVoxelLandGraphPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (!Pin || Pin->Direction != EGPD_Input || !Cast<UVoxelLandEditorNode>(Pin->GetOwningNode())
		|| !Cast<UVoxelLandEditorSchema>(Pin->GetSchema()))
	{
		return nullptr;
	}
	const FName Category = Pin->PinType.PinCategory;
	if (Category != TEXT("float") && Category != TEXT("int") && Category != TEXT("bool"))
	{
		return nullptr;
	}
	return SNew(SVoxelLandTypedGraphPin, Pin);
}