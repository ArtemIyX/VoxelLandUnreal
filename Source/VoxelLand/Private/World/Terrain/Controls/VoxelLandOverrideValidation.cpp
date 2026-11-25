#include "World/Terrain/Controls/VoxelLandOverrideValidation.h"
#include "Data/Terrain/Controls/VoxelLandControlSnapshot.h"
#include "Data/World/Configuration/VoxelLandZoneProfile.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"

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

FVoxelLandM5Diagnostic VoxelLandM4OverrideValidation::ValidateMetered(const FVoxelLandM4ControlSnapshot& Channel,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	auto Failure = [](EVoxelLandM5ErrorCode Code, const TCHAR* Message) {
		FVoxelLandM5Diagnostic D;
		D.Code = Code;
		D.Stage = TEXT("M5.OverrideValidation");
		D.Message = Message;
		return D;
	};
	auto Debit = [&](int64 Units = 1) -> EVoxelLandM5ErrorCode {
		if (IsCancelled())
		{
			return EVoxelLandM5ErrorCode::Cancelled;
		}
		return Budget->TryChargeWork(Units) ? EVoxelLandM5ErrorCode::None : EVoxelLandM5ErrorCode::BudgetExceeded;
	};
	if (const auto Code = Debit(); Code != EVoxelLandM5ErrorCode::None)
	{
		return Failure(Code, TEXT("Override validation cancelled or exceeded its work budget"));
	}
	const auto& Parameters = Channel.Source.Parameters;
	const auto& Overrides = Channel.Overrides;
	if (Parameters.Num() > 128 || Overrides.Num() > 128)
	{
		return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("M4 override declarations exceed supported limits"));
	}
	for (int32 I = 0; I < Parameters.Num(); ++I)
	{
		const FVoxelLandGraphParameter& P = Parameters[I];
		if (P.Name.IsEmpty() || P.Name.Len() > 128 || !P.Id.IsValid()
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
			return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid M4 override declaration"));
		}
		for (int32 C = 0; C < P.Name.Len(); ++C)
		{
			if (const auto Code = Debit(); Code != EVoxelLandM5ErrorCode::None)
			{
				return Failure(Code, TEXT("Override name validation cancelled or exceeded its work budget"));
			}
			const TCHAR Ch = P.Name[C];
			if (!((Ch >= TEXT('A') && Ch <= TEXT('Z')) || (Ch >= TEXT('a') && Ch <= TEXT('z')) || Ch == TEXT('_')
					|| (C > 0 && Ch >= TEXT('0') && Ch <= TEXT('9'))))
			{
				return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid M4 override name"));
			}
		}
		for (int32 J = 0; J < I; ++J)
		{
			if (const auto Code = Debit(1 + P.Name.Len() + Parameters[J].Name.Len());
				Code != EVoxelLandM5ErrorCode::None)
			{
				return Failure(Code, TEXT("Override declaration scan cancelled or exceeded its work budget"));
			}
			if (P.Id == Parameters[J].Id || P.Name.Equals(Parameters[J].Name, ESearchCase::IgnoreCase))
			{
				return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate M4 override declaration"));
			}
		}
	}
	for (int32 I = 0; I < Overrides.Num(); ++I)
	{
		const FVoxelLandParameterOverride& O = Overrides[I];
		const FVoxelLandGraphParameter* Found = nullptr;
		for (const FVoxelLandGraphParameter& P : Parameters)
		{
			if (const auto Code = Debit(); Code != EVoxelLandM5ErrorCode::None)
			{
				return Failure(Code, TEXT("Override lookup cancelled or exceeded its work budget"));
			}
			if (P.Id == O.ParameterId)
			{
				Found = &P;
			}
		}
		for (int32 J = 0; J < I; ++J)
		{
			if (const auto Code = Debit(); Code != EVoxelLandM5ErrorCode::None)
			{
				return Failure(Code, TEXT("Override duplicate scan cancelled or exceeded its work budget"));
			}
			if (Overrides[J].ParameterId == O.ParameterId)
			{
				return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Duplicate M4 override"));
			}
		}
		if (!Found || O.Type != Found->Type
			|| (O.Type == EVoxelLandGraphValueType::Float
				&& (!FMath::IsFinite(O.FloatValue)
					|| (Found->bHasRange && (O.FloatValue < Found->FloatMin || O.FloatValue > Found->FloatMax))))
			|| (O.Type == EVoxelLandGraphValueType::Int32 && Found->bHasRange
				&& (O.IntValue < Found->IntMin || O.IntValue > Found->IntMax)))
		{
			return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid M4 override value"));
		}
	}
	if (const auto Code = Debit(); Code != EVoxelLandM5ErrorCode::None)
	{
		return Failure(Code, TEXT("Override validation cancelled or exceeded its work budget"));
	}
	return FVoxelLandM5Diagnostic();
}
