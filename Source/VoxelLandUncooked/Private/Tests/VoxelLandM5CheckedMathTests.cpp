#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5CheckedMathFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
using FM5BinaryCheckedOperation = bool (*)(int64, int64, int64&);

struct FM5ArithmeticCase
{
	int64 A;
	int64 B;
	bool bSuccess;
	int64 Expected;
};

// All values here are small enough that ordinary signed arithmetic is an
// independent exact reference, without relying on the production guards.
int64 M5SmallFloorReference(int64 Value, int64 Divisor)
{
	int64 Quotient = 0;
	while (Value < 0)
	{
		Value += Divisor;
		--Quotient;
	}
	while (Value >= Divisor)
	{
		Value -= Divisor;
		++Quotient;
	}
	return Quotient;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CheckedMathBoundaryTest, "VoxelLand.M5.Numeric.CheckedMath.Boundaries", M5CheckedMathFlags)
bool FVoxelLandM5CheckedMathBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::CheckedMath;
	constexpr int64 Sentinel = 173;
	const auto CheckCases = [this](const TCHAR* Name, FM5BinaryCheckedOperation Operation,
								const FM5ArithmeticCase* Cases, int32 Count) {
		for (int32 Index = 0; Index < Count; ++Index)
		{
			int64 Result = Sentinel;
			const bool bSuccess = Operation(Cases[Index].A, Cases[Index].B, Result);
			const FString Label = FString::Printf(TEXT("%s case %d"), Name, Index);
			TestEqual(*FString::Printf(TEXT("%s status"), *Label), bSuccess, Cases[Index].bSuccess);
			TestEqual(*FString::Printf(TEXT("%s output"), *Label), Result,
				Cases[Index].bSuccess ? Cases[Index].Expected : Sentinel);
		}
	};
	const FM5ArithmeticCase Add[] = {{MAX_int64, 0, true, MAX_int64}, {MAX_int64, 1, false, 0},
		{MAX_int64 - 1, 1, true, MAX_int64}, {MIN_int64, 0, true, MIN_int64}, {MIN_int64, -1, false, 0},
		{MIN_int64 + 1, -1, true, MIN_int64}, {MIN_int64, MAX_int64, true, -1}, {MAX_int64, MIN_int64, true, -1}};
	const FM5ArithmeticCase Subtract[] = {{MIN_int64, 1, false, 0}, {MIN_int64 + 1, 1, true, MIN_int64},
		{MAX_int64, -1, false, 0}, {MAX_int64 - 1, -1, true, MAX_int64}, {MIN_int64, MIN_int64, true, 0},
		{0, MIN_int64, false, 0}, {-1, MIN_int64, true, MAX_int64}, {MAX_int64, MAX_int64, true, 0}};
	const FM5ArithmeticCase Multiply[] = {{MIN_int64, -1, false, 0}, {-1, MIN_int64, false, 0},
		{MIN_int64, 1, true, MIN_int64}, {MAX_int64, 1, true, MAX_int64}, {MIN_int64, 0, true, 0},
		{0, MIN_int64, true, 0}, {MAX_int64, 2, false, 0}, {MIN_int64, 2, false, 0},
		{MAX_int64 / 2, 2, true, MAX_int64 - 1}, {MIN_int64 / 2, 2, true, MIN_int64},
		{3037000499ll, 3037000499ll, true, 9223372030926249001ll}, {3037000500ll, 3037000500ll, false, 0},
		{-3037000500ll, 3037000500ll, false, 0}};
	const FM5ArithmeticCase NonNegative[] = {{0, 0, true, 0}, {0, MAX_int64, true, 0}, {MAX_int64, 1, true, MAX_int64},
		{MAX_int64 / 2, 2, true, MAX_int64 - 1}, {MAX_int64, 2, false, 0}, {-1, 0, false, 0}, {0, -1, false, 0},
		{-1, -1, false, 0}, {MIN_int64, 1, false, 0}};
	const FM5ArithmeticCase Divide[] = {{-33, 32, true, -2}, {-32, 32, true, -1}, {-1, 32, true, -1}, {0, 32, true, 0},
		{31, 32, true, 0}, {32, 32, true, 1}, {33, 32, true, 1}, {MIN_int64, 1, true, MIN_int64},
		{MIN_int64, 2, true, MIN_int64 / 2}, {MIN_int64, MAX_int64, true, -2}, {MAX_int64, MAX_int64, true, 1},
		{1, 0, false, 0}, {MIN_int64, -1, false, 0}, {1, MIN_int64, false, 0}};
	CheckCases(TEXT("add"), TryAdd, Add, UE_ARRAY_COUNT(Add));
	CheckCases(TEXT("subtract"), TrySubtract, Subtract, UE_ARRAY_COUNT(Subtract));
	CheckCases(TEXT("multiply"), TryMultiply, Multiply, UE_ARRAY_COUNT(Multiply));
	CheckCases(TEXT("nonnegative multiply"), TryMultiplyNonNegative, NonNegative, UE_ARRAY_COUNT(NonNegative));
	CheckCases(TEXT("floor divide"), TryFloorDivide, Divide, UE_ARRAY_COUNT(Divide));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CheckedMathSmallReferenceTest, "VoxelLand.M5.Numeric.CheckedMath.SmallReference", M5CheckedMathFlags)
bool FVoxelLandM5CheckedMathSmallReferenceTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::CheckedMath;
	for (int64 A = -64; A <= 64; ++A)
	{
		for (int64 B = -64; B <= 64; ++B)
		{
			int64 Result = 0;
			const auto Check = [this, A, B, &Result](const TCHAR* Name, bool bOk, int64 Expected) {
				if (!bOk || Result != Expected)
				{
					AddError(FString::Printf(TEXT("%s(%lld, %lld): success=%d result=%lld expected=%lld"), Name, A, B,
						bOk, Result, Expected));
					return false;
				}
				return true;
			};
			if (!Check(TEXT("add"), TryAdd(A, B, Result), A + B)
				|| !Check(TEXT("subtract"), TrySubtract(A, B, Result), A - B)
				|| !Check(TEXT("multiply"), TryMultiply(A, B, Result), A * B))
			{
				return false;
			}
			if (A >= 0 && B >= 0 && !Check(TEXT("nonnegative multiply"), TryMultiplyNonNegative(A, B, Result), A * B))
			{
				return false;
			}
			if (B > 0 && !Check(TEXT("floor divide"), TryFloorDivide(A, B, Result), M5SmallFloorReference(A, B)))
			{
				return false;
			}
		}
	}
	return true;
}
#endif
