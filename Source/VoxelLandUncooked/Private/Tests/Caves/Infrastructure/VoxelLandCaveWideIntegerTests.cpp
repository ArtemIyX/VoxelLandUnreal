#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Caves/Infrastructure/VoxelLandCaveWideInteger.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5WideIntegerFixture final
{
public:
	static bool EqualLimbs(const FVoxelLandM5WideInteger& Value, TConstArrayView<uint32> Expected)
	{
		if (Expected.Num() != FVoxelLandM5WideInteger::LimbCount)
		{
			return false;
		}
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			if (Value.GetLimb(Index) != Expected[Index])
			{
				return false;
			}
		}
		return true;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5WideIntegerTest, "VoxelLand.M5.Numeric.WideInteger",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5WideIntegerTest::RunTest(const FString& Parameters)
{
	// Independent Python bigint oracle: a=2**255+2**129+123456789;
	// b=2**200+2**65+987654321; limbs are (value >> (32*i)) & 0xffffffff.
	const uint32 ALimbs[] = {0x075bcd15u, 0u, 0u, 0u, 2u, 0u, 0u, 0x80000000u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	const uint32 BLimbs[] = {0x3ade68b1u, 0u, 2u, 0u, 0u, 0u, 0x100u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	const uint32 SumLimbs[] = {0x423a35c6u, 0u, 2u, 0u, 2u, 0u, 0x100u, 0x80000000u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	const uint32 DifferenceLimbs[] = {0xcc7d6464u, 0xffffffffu, 0xfffffffdu, 0xffffffffu, 1u, 0u, 0xffffff00u,
		0x7fffffffu, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	const uint32 ProductLimbs[] = {0xfbff5385u, 0x01b13114u, 0x0eb79a2au, 0u, 0x75bcd162u, 0u, 0x5bcd1504u, 0x80000007u,
		0x1d6f3458u, 0u, 0x201u, 0u, 0u, 0u, 0x80u, 0u};
	FVoxelLandM5WideInteger A;
	FVoxelLandM5WideInteger B;
	FVoxelLandM5WideInteger Out;
	TestTrue(TEXT("construct A"), FVoxelLandM5WideInteger::TryFromMagnitude(MakeArrayView(ALimbs), false, A));
	TestTrue(TEXT("construct B"), FVoxelLandM5WideInteger::TryFromMagnitude(MakeArrayView(BLimbs), false, B));
	TestTrue(TEXT("wide sum"), FVoxelLandM5WideInteger::TryAdd(A, B, Out));
	TestTrue(TEXT("sum bigint oracle"), FVoxelLandM5WideIntegerFixture::EqualLimbs(Out, MakeArrayView(SumLimbs)));
	TestTrue(TEXT("wide difference"), FVoxelLandM5WideInteger::TrySubtract(A, B, Out));
	TestTrue(TEXT("difference bigint oracle"),
		FVoxelLandM5WideIntegerFixture::EqualLimbs(Out, MakeArrayView(DifferenceLimbs)));
	TestTrue(TEXT("wide product"), FVoxelLandM5WideInteger::TryMultiply(A, B, Out));
	TestTrue(
		TEXT("product bigint oracle"), FVoxelLandM5WideIntegerFixture::EqualLimbs(Out, MakeArrayView(ProductLimbs)));
	TestTrue(TEXT("reverse difference"), FVoxelLandM5WideInteger::TrySubtract(B, A, Out));
	TestTrue(TEXT("negative result"), Out.IsNegative());
	TestTrue(
		TEXT("negative magnitude"), FVoxelLandM5WideIntegerFixture::EqualLimbs(Out, MakeArrayView(DifferenceLimbs)));
	TestTrue(TEXT("negative order"), FVoxelLandM5WideInteger::Compare(Out, A) < 0);
	FVoxelLandM5WideInteger NegativeB;
	TestTrue(TEXT("construct negative B"),
		FVoxelLandM5WideInteger::TryFromMagnitude(MakeArrayView(BLimbs), true, NegativeB));
	TestTrue(TEXT("signed multiplication"), FVoxelLandM5WideInteger::TryMultiply(A, NegativeB, Out));
	TestTrue(TEXT("signed product negative"), Out.IsNegative());
	TestTrue(TEXT("signed product bigint magnitude"),
		FVoxelLandM5WideIntegerFixture::EqualLimbs(Out, MakeArrayView(ProductLimbs)));
	TestTrue(TEXT("alias output"), FVoxelLandM5WideInteger::TryAdd(A, B, A));
	TestTrue(TEXT("alias preserved inputs"), FVoxelLandM5WideIntegerFixture::EqualLimbs(A, MakeArrayView(SumLimbs)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5WideIntegerBoundaryTest, "VoxelLand.M5.Numeric.WideInteger.Boundaries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5WideIntegerBoundaryTest::RunTest(const FString& Parameters)
{
	const FVoxelLandM5WideInteger Min = FVoxelLandM5WideInteger::FromInt64(MIN_int64);
	const FVoxelLandM5WideInteger Max = FVoxelLandM5WideInteger::FromInt64(MAX_int64);
	const FVoxelLandM5WideInteger One = FVoxelLandM5WideInteger::FromInt64(1);
	int64 Scalar = 7;
	TestTrue(TEXT("minimum round trip"), Min.TryToInt64(Scalar));
	TestEqual(TEXT("minimum exact"), Scalar, MIN_int64);
	TestTrue(TEXT("maximum round trip"), Max.TryToInt64(Scalar));
	TestEqual(TEXT("maximum exact"), Scalar, MAX_int64);
	FVoxelLandM5WideInteger Out;
	TestTrue(TEXT("positive int64 overflow represented"), FVoxelLandM5WideInteger::TryAdd(Max, One, Out));
	Scalar = 7;
	TestFalse(TEXT("positive int64 conversion rejected"), Out.TryToInt64(Scalar));
	TestEqual(TEXT("failed conversion preserves scalar"), Scalar, int64(7));
	TestTrue(TEXT("zero by subtraction"), FVoxelLandM5WideInteger::TrySubtract(Min, Min, Out));
	TestTrue(TEXT("zero normalized"), Out.IsZero() && !Out.IsNegative());
	uint32 MaximumLimbs[FVoxelLandM5WideInteger::LimbCount];
	for (int32 Index = 0; Index < FVoxelLandM5WideInteger::LimbCount; ++Index)
	{
		MaximumLimbs[Index] = MAX_uint32;
	}
	FVoxelLandM5WideInteger Maximum;
	TestTrue(TEXT("512-bit maximum"),
		FVoxelLandM5WideInteger::TryFromMagnitude(MakeArrayView(MaximumLimbs), false, Maximum));
	Out = One;
	TestFalse(TEXT("addition overflow"), FVoxelLandM5WideInteger::TryAdd(Maximum, One, Out));
	TestEqual(TEXT("addition failure preserves output"), FVoxelLandM5WideInteger::Compare(Out, One), 0);
	TestFalse(TEXT("multiplication overflow"),
		FVoxelLandM5WideInteger::TryMultiply(Maximum, FVoxelLandM5WideInteger::FromInt64(2), Out));
	TestEqual(TEXT("multiply failure preserves output"), FVoxelLandM5WideInteger::Compare(Out, One), 0);
	TestTrue(TEXT("maximum times one"), FVoxelLandM5WideInteger::TryMultiply(Maximum, One, Out));
	TestEqual(TEXT("maximum product exact"), FVoxelLandM5WideInteger::Compare(Out, Maximum), 0);
	TestTrue(TEXT("negative times zero"), FVoxelLandM5WideInteger::TryMultiply(Min, FVoxelLandM5WideInteger(), Out));
	TestTrue(TEXT("product zero normalized"), Out.IsZero() && !Out.IsNegative());
	uint32 Oversized[FVoxelLandM5WideInteger::LimbCount + 1] = {};
	Out = One;
	TestFalse(TEXT("oversized input rejected"),
		FVoxelLandM5WideInteger::TryFromMagnitude(MakeArrayView(Oversized), false, Out));
	TestEqual(TEXT("input failure preserves output"), FVoxelLandM5WideInteger::Compare(Out, One), 0);
	const uint32 ZeroLimbs[] = {0u, 0u};
	TestTrue(
		TEXT("negative zero input"), FVoxelLandM5WideInteger::TryFromMagnitude(MakeArrayView(ZeroLimbs), true, Out));
	TestTrue(TEXT("input zero normalized"), Out.IsZero() && !Out.IsNegative());
	return true;
}

#endif
