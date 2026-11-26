#include "VoxelLandMaterialClassifier.h"
#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Data/Meshing/VoxelLandMaterialBlend.h"
#include "Data/Meshing/VoxelLandMeshVertex.h"
#include "Graph/Compilation/VoxelLandMaterialGraphPoint.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"

bool FVoxelLandMaterialClassifier::IsValid(const FVoxelLandMaterialAppearance& Appearance)
{
	if (Appearance.SlotCount == 0 || Appearance.SlotCount > FVoxelLandMaterialAppearance::MaxSlots)
	{
		return false;
	}

	for (const FVoxelLandResolvedMaterialRule& Rule : Appearance.Rules)
	{
		if (Rule.Slot >= Appearance.SlotCount || !FMath::IsFinite(Rule.Threshold) || !FMath::IsFinite(Rule.BlendWidth)
			|| Rule.BlendWidth < 0.0
			|| (Rule.Input != EVoxelLandMaterialRuleInput::ElevationCm
				&& Rule.Input != EVoxelLandMaterialRuleInput::SlopeDegrees))
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandMaterialClassifier::HasSlopeRules(const FVoxelLandMaterialAppearance& Appearance)
{
	if (Appearance.Program)
	{
		return Appearance.Program->UsesSlope();
	}
	for (const FVoxelLandResolvedMaterialRule& Rule : Appearance.Rules)
	{
		if (Rule.Input == EVoxelLandMaterialRuleInput::SlopeDegrees)
		{
			return true;
		}
	}
	return false;
}

FVoxelLandMaterialCandidates FVoxelLandMaterialClassifier::Sample(const FVoxelLandMeshVertex& Vertex,
	const FVoxelLandMaterialAppearance& Appearance, const FVector3d& Origin, bool bHasSlopeRules)
{
	double Weights[FVoxelLandMaterialAppearance::MaxSlots] = {};
	const FVector3d Position = Origin + FVector3d(Vertex.Position);
	const double Elevation = Position.Z;
	const double Slope = bHasSlopeRules ? GetSlope(Vertex.Normal) : 0.0;
	if (Appearance.Program)
	{
		const FVoxelLandMaterialGraphPoint Point{Position, Slope, Appearance.Seed, Appearance.Signals.Get()};
		if (!Appearance.Program->Evaluate(Point, MakeArrayView(Weights, Appearance.SlotCount)))
		{
			return {};
		}
		return Select(Weights, Appearance.SlotCount);
	}
	double Remaining = 1.0;

	// Later rules take their share first. The unclaimed weight belongs to the fallback.
	for (int32 Index = Appearance.Rules.Num() - 1; Index >= 0 && Remaining > 0.0; --Index)
	{
		const FVoxelLandResolvedMaterialRule& Rule = Appearance.Rules[Index];
		const double Value = Rule.Input == EVoxelLandMaterialRuleInput::SlopeDegrees ? Slope : Elevation;
		const double Contribution = Remaining * GetActivation(Rule, Value);
		Weights[Rule.Slot] += Contribution;
		Remaining -= Contribution;
	}
	Weights[FVoxelLandMaterialAppearance::FallbackSlot] += Remaining;
	return Select(Weights, Appearance.SlotCount);
}

double FVoxelLandMaterialClassifier::GetSlope(const FVector3f& Normal)
{
	const FVector3d UnitNormal = FVector3d(Normal).GetSafeNormal();
	if (UnitNormal.IsNearlyZero() || UnitNormal.ContainsNaN())
	{
		return 0.0;
	}
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(UnitNormal.Z, -1.0, 1.0)));
}

double FVoxelLandMaterialClassifier::GetActivation(const FVoxelLandResolvedMaterialRule& Rule, double Value)
{
	if (!FMath::IsFinite(Value))
	{
		return 0.0;
	}
	if (Rule.BlendWidth == 0.0)
	{
		return Value >= Rule.Threshold ? 1.0 : 0.0;
	}
	const double T = FMath::Clamp((Value - Rule.Threshold) / Rule.BlendWidth + 0.5, 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}

FVoxelLandMaterialCandidates FVoxelLandMaterialClassifier::Select(const double* Weights, uint8 SlotCount)
{
	FVoxelLandMaterialCandidates Result;
	uint8 First = 0;
	uint8 Second = 0;
	double FirstWeight = 0.0;
	double SecondWeight = 0.0;
	for (uint8 Slot = 0; Slot < SlotCount; ++Slot)
	{
		if (Weights[Slot] > FirstWeight)
		{
			Second = First;
			SecondWeight = FirstWeight;
			First = Slot;
			FirstWeight = Weights[Slot];
		}
		else if (Weights[Slot] > SecondWeight)
		{
			Second = Slot;
			SecondWeight = Weights[Slot];
		}
	}
	Result.Slots[0] = First;
	Result.Weights[0] = FVoxelLandMaterialBlend::WeightSum;
	Result.Count = 1;
	if (SecondWeight > 0.0)
	{
		const double Scaled = FirstWeight / (FirstWeight + SecondWeight) * FVoxelLandMaterialBlend::WeightSum;
		const int32 Whole = FMath::FloorToInt(Scaled);
		const double Remainder = Scaled - Whole;
		const bool bIsRoundedUp = Remainder > 0.5 || (Remainder == 0.5 && First < Second);
		Result.Weights[0] = static_cast<uint8>(Whole + bIsRoundedUp);
		Result.Weights[1] = FVoxelLandMaterialBlend::WeightSum - Result.Weights[0];
		Result.Slots[1] = Second;
		Result.Count += Result.Weights[1] > 0;
	}
	return Result;
}
