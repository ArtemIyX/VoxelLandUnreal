#include "World/VoxelLandM4OverrideValidation.h"
#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Data/Structs/VoxelLandZoneProfile.h"

bool VoxelLandM4OverrideValidation::Validate(const FVoxelLandM4ControlSnapshot& Channel, FString& Error)
{
	if (Channel.Source.Parameters.Num() > 128 || Channel.Overrides.Num() > 128)
	{
		Error = TEXT("M4 override declarations exceed supported limits");
		return false;
	}
	TMap<FGuid, const FVoxelLandGraphParameter*> Declarations;
	TSet<FString> Names;
	for (const FVoxelLandGraphParameter& P : Channel.Source.Parameters)
	{
		bool bValidName = !P.Name.IsEmpty();
		for (int32 Index = 0; Index < P.Name.Len(); ++Index)
		{
			const TCHAR C = P.Name[Index];
			if (!((C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || C == TEXT('_')
					|| (Index > 0 && C >= TEXT('0') && C <= TEXT('9'))))
			{
				bValidName = false;
				break;
			}
		}
		const FString LowerName = P.Name.ToLower();
		if (!bValidName || P.Name.Len() > 128 || Names.Contains(LowerName) || !P.Id.IsValid()
			|| Declarations.Contains(P.Id)
			|| (P.Type != EVoxelLandGraphValueType::Float && P.Type != EVoxelLandGraphValueType::Int32
				&& P.Type != EVoxelLandGraphValueType::Bool)
			|| (P.Type == EVoxelLandGraphValueType::Float
				&& (!FMath::IsFinite(P.FloatDefault)
					|| (P.bHasRange
						&& (!FMath::IsFinite(P.FloatMin) || !FMath::IsFinite(P.FloatMax) || P.FloatMin > P.FloatMax
							|| P.FloatDefault < P.FloatMin || P.FloatDefault > P.FloatMax))))
			|| (P.Type == EVoxelLandGraphValueType::Int32 && P.bHasRange
				&& (P.IntMin > P.IntMax || P.IntDefault < P.IntMin || P.IntDefault > P.IntMax))
			|| (P.Type == EVoxelLandGraphValueType::Bool && P.bHasRange))
		{
			Error = TEXT("M4 graph contains invalid or duplicate override declarations");
			return false;
		}
		Declarations.Add(P.Id, &P);
		Names.Add(LowerName);
	}
	TSet<FGuid> Seen;
	for (const FVoxelLandParameterOverride& O : Channel.Overrides)
	{
		const FVoxelLandGraphParameter* const* Found = Declarations.Find(O.ParameterId);
		if (!Found || Seen.Contains(O.ParameterId) || O.Type != (*Found)->Type
			|| (O.Type == EVoxelLandGraphValueType::Float
				&& (!FMath::IsFinite(O.FloatValue)
					|| ((*Found)->bHasRange
						&& (O.FloatValue < (*Found)->FloatMin || O.FloatValue > (*Found)->FloatMax))))
			|| (O.Type == EVoxelLandGraphValueType::Int32 && (*Found)->bHasRange
				&& (O.IntValue < (*Found)->IntMin || O.IntValue > (*Found)->IntMax)))
		{
			Error = TEXT("M4 override has an unknown or duplicate ID, incompatible type, or invalid value");
			return false;
		}
		Seen.Add(O.ParameterId);
	}
	Error.Reset();
	return true;
}

void VoxelLandM4OverrideValidation::BuildProfile(
	const FVoxelLandM4ControlSnapshot& Channel, int32 VoxelSizeCm, FVoxelLandZoneProfile& Profile)
{
	Profile = FVoxelLandZoneProfile();
	Profile.VoxelSizeCm = float(VoxelSizeCm);
	for (const FVoxelLandParameterOverride& Override : Channel.Overrides)
	{
		const FVoxelLandSerializedParameter* Parameter =
			Channel.Payload.Parameters.FindByPredicate([&Override](const FVoxelLandSerializedParameter& Candidate) {
				return Candidate.Id == Override.ParameterId;
			});
		if (!Parameter)
		{
			continue;
		}
		switch (Override.Type)
		{
			case EVoxelLandGraphValueType::Float:
				Profile.FloatOverrides.Add(Parameter->Name, Override.FloatValue);
				break;
			case EVoxelLandGraphValueType::Int32:
				Profile.IntOverrides.Add(Parameter->Name, Override.IntValue);
				break;
			case EVoxelLandGraphValueType::Bool:
				Profile.BoolOverrides.Add(Parameter->Name, Override.BoolValue);
				break;
			default:
				break;
		}
	}
}
