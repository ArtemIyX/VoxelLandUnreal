#pragma once

#include "CoreMinimal.h"

// Fixed-width sign/magnitude arithmetic. Failure leaves the output unchanged.
// Operations are bounded: addition/comparison inspect 16 limbs; multiplication
// uses at most 256 limb products. This type does not establish geometry proof.
class VOXELLANDALGO_API FVoxelLandM5WideInteger final
{
public:
	static constexpr int32 LimbCount = 16;

	[[nodiscard]] static FVoxelLandM5WideInteger FromInt64(int64 Value);
	[[nodiscard]] static bool TryFromMagnitude(
		TConstArrayView<uint32> LittleEndianLimbs, bool bNegative, FVoxelLandM5WideInteger& Out);
	[[nodiscard]] static bool TryAdd(
		const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out);
	[[nodiscard]] static bool TrySubtract(
		const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out);
	[[nodiscard]] static bool TryMultiply(
		const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out);
	[[nodiscard]] static int32 Compare(const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B);
	[[nodiscard]] bool TryToInt64(int64& Out) const;
	[[nodiscard]] bool IsZero() const;
	[[nodiscard]] bool IsNegative() const;
	[[nodiscard]] uint32 GetLimb(int32 Index) const;

private:
	[[nodiscard]] static int32 CompareMagnitude(const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B);
	[[nodiscard]] static bool AddMagnitude(
		const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out);
	static void SubtractMagnitude(
		const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out);
	[[nodiscard]] static bool MultiplyRow(
		const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, int32 Row, FVoxelLandM5WideInteger& Out);
	void Normalize();

	uint32 Limbs[LimbCount] = {};
	bool bIsNegative = false;
};
