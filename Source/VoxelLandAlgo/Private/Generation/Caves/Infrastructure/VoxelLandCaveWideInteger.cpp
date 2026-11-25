#include "Generation/Caves/Infrastructure/VoxelLandCaveWideInteger.h"

FVoxelLandM5WideInteger FVoxelLandM5WideInteger::FromInt64(int64 Value)
{
	FVoxelLandM5WideInteger Result;
	const uint64 Magnitude = Value < 0 ? static_cast<uint64>(-(Value + 1)) + 1u : static_cast<uint64>(Value);
	Result.Limbs[0] = static_cast<uint32>(Magnitude);
	Result.Limbs[1] = static_cast<uint32>(Magnitude >> 32);
	Result.bIsNegative = Value < 0;
	return Result;
}

bool FVoxelLandM5WideInteger::TryFromMagnitude(
	TConstArrayView<uint32> LittleEndianLimbs, bool bNegative, FVoxelLandM5WideInteger& Out)
{
	if (LittleEndianLimbs.Num() > LimbCount)
	{
		return false;
	}
	FVoxelLandM5WideInteger Result;
	for (int32 Index = 0; Index < LittleEndianLimbs.Num(); ++Index)
	{
		Result.Limbs[Index] = LittleEndianLimbs[Index];
	}
	Result.bIsNegative = bNegative;
	Result.Normalize();
	Out = Result;
	return true;
}

int32 FVoxelLandM5WideInteger::CompareMagnitude(const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B)
{
	for (int32 Index = LimbCount - 1; Index >= 0; --Index)
	{
		if (A.Limbs[Index] != B.Limbs[Index])
		{
			return A.Limbs[Index] < B.Limbs[Index] ? -1 : 1;
		}
	}
	return 0;
}

int32 FVoxelLandM5WideInteger::Compare(const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B)
{
	if (A.bIsNegative != B.bIsNegative)
	{
		return A.bIsNegative ? -1 : 1;
	}
	const int32 MagnitudeOrder = CompareMagnitude(A, B);
	return A.bIsNegative ? -MagnitudeOrder : MagnitudeOrder;
}

bool FVoxelLandM5WideInteger::AddMagnitude(
	const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out)
{
	uint64 Carry = 0;
	for (int32 Index = 0; Index < LimbCount; ++Index)
	{
		const uint64 Sum = static_cast<uint64>(A.Limbs[Index]) + B.Limbs[Index] + Carry;
		Out.Limbs[Index] = static_cast<uint32>(Sum);
		Carry = Sum >> 32;
	}
	return Carry == 0;
}

void FVoxelLandM5WideInteger::SubtractMagnitude(
	const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out)
{
	uint64 Borrow = 0;
	for (int32 Index = 0; Index < LimbCount; ++Index)
	{
		const uint64 Subtrahend = static_cast<uint64>(B.Limbs[Index]) + Borrow;
		const uint64 Minuend = A.Limbs[Index];
		Out.Limbs[Index] = static_cast<uint32>(Minuend - Subtrahend);
		Borrow = Minuend < Subtrahend ? 1u : 0u;
	}
}

bool FVoxelLandM5WideInteger::TryAdd(
	const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out)
{
	FVoxelLandM5WideInteger Result;
	if (A.bIsNegative == B.bIsNegative)
	{
		if (!AddMagnitude(A, B, Result))
		{
			return false;
		}
		Result.bIsNegative = A.bIsNegative;
	}
	else if (CompareMagnitude(A, B) >= 0)
	{
		SubtractMagnitude(A, B, Result);
		Result.bIsNegative = A.bIsNegative;
	}
	else
	{
		SubtractMagnitude(B, A, Result);
		Result.bIsNegative = B.bIsNegative;
	}
	Result.Normalize();
	Out = Result;
	return true;
}

bool FVoxelLandM5WideInteger::TrySubtract(
	const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out)
{
	FVoxelLandM5WideInteger Negated = B;
	if (!Negated.IsZero())
	{
		Negated.bIsNegative = !Negated.bIsNegative;
	}
	return TryAdd(A, Negated, Out);
}

bool FVoxelLandM5WideInteger::MultiplyRow(
	const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, int32 Row, FVoxelLandM5WideInteger& Out)
{
	uint64 Carry = 0;
	for (int32 Column = 0; Column < LimbCount - Row; ++Column)
	{
		const int32 Index = Row + Column;
		const uint64 Product = static_cast<uint64>(A.Limbs[Row]) * B.Limbs[Column];
		const uint64 Sum = Product + Out.Limbs[Index] + Carry;
		Out.Limbs[Index] = static_cast<uint32>(Sum);
		Carry = Sum >> 32;
	}
	if (Carry != 0)
	{
		return false;
	}
	for (int32 Column = LimbCount - Row; Column < LimbCount; ++Column)
	{
		if (A.Limbs[Row] != 0 && B.Limbs[Column] != 0)
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM5WideInteger::TryMultiply(
	const FVoxelLandM5WideInteger& A, const FVoxelLandM5WideInteger& B, FVoxelLandM5WideInteger& Out)
{
	FVoxelLandM5WideInteger Result;
	for (int32 Row = 0; Row < LimbCount; ++Row)
	{
		if (!MultiplyRow(A, B, Row, Result))
		{
			return false;
		}
	}
	Result.bIsNegative = A.bIsNegative != B.bIsNegative;
	Result.Normalize();
	Out = Result;
	return true;
}

bool FVoxelLandM5WideInteger::TryToInt64(int64& Out) const
{
	for (int32 Index = 2; Index < LimbCount; ++Index)
	{
		if (Limbs[Index] != 0)
		{
			return false;
		}
	}
	const uint64 Magnitude = static_cast<uint64>(Limbs[0]) | (static_cast<uint64>(Limbs[1]) << 32);
	const uint64 NegativeLimit = uint64(1) << 63;
	if ((!bIsNegative && Magnitude >= NegativeLimit) || (bIsNegative && Magnitude > NegativeLimit))
	{
		return false;
	}
	if (bIsNegative && Magnitude == NegativeLimit)
	{
		Out = MIN_int64;
		return true;
	}
	const int64 SignedMagnitude = static_cast<int64>(Magnitude);
	Out = bIsNegative ? -SignedMagnitude : SignedMagnitude;
	return true;
}

bool FVoxelLandM5WideInteger::IsZero() const
{
	for (int32 Index = 0; Index < LimbCount; ++Index)
	{
		if (Limbs[Index] != 0)
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM5WideInteger::IsNegative() const
{
	return bIsNegative;
}

uint32 FVoxelLandM5WideInteger::GetLimb(int32 Index) const
{
	check(Index >= 0 && Index < LimbCount);
	return Limbs[Index];
}

void FVoxelLandM5WideInteger::Normalize()
{
	if (IsZero())
	{
		bIsNegative = false;
	}
}
