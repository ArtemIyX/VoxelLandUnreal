#pragma once
#include "Graph/VoxelLandEditorFloat.h"
#include "Misc/DefaultValueHelper.h"
#include "Widgets/Input/NumericTypeInterface.h"

class FVoxelLandFloatTypeInterface : public TDefaultNumericTypeInterface<float>
{
public:
	virtual FString ToString(const float& Value) const override
	{
		return FVoxelLandEditorFloat::Format(Value);
	}

	virtual TOptional<float> FromString(const FString& Text, const float&) override
	{
		float Value = 0.f;
		if (FDefaultValueHelper::ParseFloat(Text, Value) && FMath::IsFinite(Value))
		{
			return Value;
		}
		return TOptional<float>();
	}

	virtual bool IsCharacterValid(TCHAR Character) const override
	{
		return FChar::IsDigit(Character) || Character == TEXT('.') || Character == TEXT('-') || Character == TEXT('+')
		       || Character == TEXT('e') || Character == TEXT('E');
	}
};