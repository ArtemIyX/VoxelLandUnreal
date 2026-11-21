#include "Data/Assets/VoxelLandWorldSettings.h"

#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandGraphFunction.h"

namespace
{
bool IsIdentifier(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return false;
	}
	for (int32 Index = 0; Index < Name.Len(); ++Index)
	{
		const TCHAR C = Name[Index];
		const bool bLetter = (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z'));
		const bool bDigit = C >= TEXT('0') && C <= TEXT('9');
		if (!bLetter && C != TEXT('_') && !(Index > 0 && bDigit))
		{
			return false;
		}
	}
	return true;
}

bool ValidateParameter(const FVoxelLandGraphParameter& Parameter, FString& OutError)
{
	if (!Parameter.Id.IsValid() || !IsIdentifier(Parameter.Name))
	{
		OutError = TEXT("Each parameter needs a valid GUID and ASCII identifier.");
		return false;
	}
	switch (Parameter.Type)
	{
	case EVoxelLandGraphValueType::Float:
		if (!FMath::IsFinite(Parameter.FloatDefault) || (Parameter.bHasRange &&
			(!FMath::IsFinite(Parameter.FloatMin) || !FMath::IsFinite(Parameter.FloatMax) ||
			 Parameter.FloatMin > Parameter.FloatMax || Parameter.FloatDefault < Parameter.FloatMin ||
			 Parameter.FloatDefault > Parameter.FloatMax)))
		{
			OutError = FString::Printf(TEXT("Invalid float default or range for parameter %s."), *Parameter.Name);
			return false;
		}
		break;
	case EVoxelLandGraphValueType::Int32:
		if (Parameter.bHasRange && (Parameter.IntMin > Parameter.IntMax ||
			Parameter.IntDefault < Parameter.IntMin || Parameter.IntDefault > Parameter.IntMax))
		{
			OutError = FString::Printf(TEXT("Invalid integer default or range for parameter %s."), *Parameter.Name);
			return false;
		}
		break;
	case EVoxelLandGraphValueType::Bool:
		if (Parameter.bHasRange)
		{
			OutError = FString::Printf(TEXT("Boolean parameter %s cannot have a range."), *Parameter.Name);
			return false;
		}
		break;
	default:
		OutError = TEXT("Unknown parameter type.");
		return false;
	}
	return true;
}

bool ValidateOverride(const FVoxelLandParameterOverride& Override, const FVoxelLandGraphParameter& Parameter,
	FString& OutError)
{
	if (Override.Type != Parameter.Type)
	{
		OutError = FString::Printf(TEXT("Override type does not match parameter %s."), *Parameter.Name);
		return false;
	}
	if (Override.Type == EVoxelLandGraphValueType::Float && (!FMath::IsFinite(Override.FloatValue) ||
		(Parameter.bHasRange && (Override.FloatValue < Parameter.FloatMin || Override.FloatValue > Parameter.FloatMax))))
	{
		OutError = FString::Printf(TEXT("Float override for %s is outside its valid range."), *Parameter.Name);
		return false;
	}
	if (Override.Type == EVoxelLandGraphValueType::Int32 && Parameter.bHasRange &&
		(Override.IntValue < Parameter.IntMin || Override.IntValue > Parameter.IntMax))
	{
		OutError = FString::Printf(TEXT("Integer override for %s is outside its valid range."), *Parameter.Name);
		return false;
	}
	return true;
}
}

bool UVoxelLandWorldSettings::ValidateSettings(FString& OutError) const
{
	OutError.Reset();
	if (FormatVersion != 1 || !Graph || Graph->IsA<UVoxelLandGraphFunction>() || Graph->SourceVersion != 1)
	{
		OutError = TEXT("Missing graph or unsupported settings/graph version.");
		return false;
	}
	if (!FMath::IsFinite(VoxelSizeCm) || VoxelSizeCm <= 0.0f || HorizontalRadius < 1 || HorizontalRadius > 8 ||
		VerticalRadius < 1 || VerticalRadius > 4)
	{
		OutError = TEXT("Invalid voxel size or chunk bounds.");
		return false;
	}
	TMap<FGuid, const FVoxelLandGraphParameter*> ParametersById;
	TSet<FString> Names;
	for (const FVoxelLandGraphParameter& Parameter : Graph->Parameters)
	{
		if (!ValidateParameter(Parameter, OutError))
		{
			return false;
		}
		const FString NormalizedName = Parameter.Name.ToLower();
		if (ParametersById.Contains(Parameter.Id) || Names.Contains(NormalizedName))
		{
			OutError = FString::Printf(TEXT("Duplicate parameter ID or name: %s."), *Parameter.Name);
			return false;
		}
		ParametersById.Add(Parameter.Id, &Parameter);
		Names.Add(NormalizedName);
	}
	TSet<FGuid> SeenOverrides;
	for (const FVoxelLandParameterOverride& Override : Overrides)
	{
		const FVoxelLandGraphParameter* const* Parameter = ParametersById.Find(Override.ParameterId);
		if (!Parameter || SeenOverrides.Contains(Override.ParameterId))
		{
			OutError = TEXT("An override has a missing or duplicate parameter ID.");
			return false;
		}
		if (!ValidateOverride(Override, **Parameter, OutError))
		{
			return false;
		}
		SeenOverrides.Add(Override.ParameterId);
	}
	return true;
}
