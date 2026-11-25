#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Terrain/Controls/VoxelLandControlSnapshot.h"
#include "Misc/AutomationTest.h"
#include "World/Terrain/Controls/VoxelLandOverrideValidation.h"
#include <limits>
namespace
{
constexpr EAutomationTestFlags M4OverrideValidationFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4OverrideValidationTest, "VoxelLand.M4.Overrides.Validation", M4OverrideValidationFlags)
bool FVoxelLandM4OverrideValidationTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4ControlSnapshot Channel;
	FVoxelLandGraphParameter Float;
	Float.Id = FGuid(1, 2, 3, 4);
	Float.Name = TEXT("Height");
	Float.bHasRange = true;
	Float.FloatMin = -1.f;
	Float.FloatMax = 1.f;
	Channel.Source.Parameters.Add(Float);
	FVoxelLandParameterOverride Override;
	Override.ParameterId = Float.Id;
	Override.FloatValue = 0.75f;
	Channel.Overrides.Add(Override);
	FString Error;
	TestTrue(TEXT("valid override of unused declaration is accepted"),
		VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Overrides.Add(Override);
	TestFalse(TEXT("duplicate override rejected"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Overrides.Pop();
	Channel.Overrides[0].ParameterId = FGuid(9, 9, 9, 9);
	TestFalse(TEXT("unknown declaration rejected"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Overrides[0].ParameterId = Float.Id;
	Channel.Overrides[0].Type = EVoxelLandGraphValueType::Int32;
	TestFalse(TEXT("type mismatch rejected"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Overrides[0].Type = EVoxelLandGraphValueType::Float;
	Channel.Overrides[0].FloatValue = 2.f;
	TestFalse(TEXT("out-of-range float rejected"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Overrides[0].FloatValue = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("nonfinite float rejected"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Overrides[0].FloatValue = 1.f;
	TestTrue(TEXT("inclusive float endpoint accepted"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	FVoxelLandGraphParameter Integer;
	Integer.Id = FGuid(5, 6, 7, 8);
	Integer.Name = TEXT("Scale");
	Integer.Type = EVoxelLandGraphValueType::Int32;
	Integer.bHasRange = true;
	Integer.IntMin = -2;
	Integer.IntMax = 2;
	Channel.Source.Parameters.Add(Integer);
	FVoxelLandParameterOverride IntOverride;
	IntOverride.ParameterId = Integer.Id;
	IntOverride.Type = EVoxelLandGraphValueType::Int32;
	IntOverride.IntValue = -2;
	Channel.Overrides.Add(IntOverride);
	TestTrue(TEXT("inclusive integer endpoint accepted"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Overrides.Last().IntValue = -3;
	TestFalse(TEXT("out-of-range integer rejected"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Overrides.Pop();
	Channel.Source.Parameters.Add(Float);
	TestFalse(TEXT("duplicate source IDs rejected"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Source.Parameters.Pop();
	Channel.Source.Parameters[1].Name = TEXT("HEIGHT");
	TestFalse(TEXT("case-insensitive duplicate parameter names rejected"),
		VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Source.Parameters[1].Name = TEXT("Scale");
	Channel.Source.Parameters[1].Type = EVoxelLandGraphValueType::Bool;
	TestFalse(TEXT("boolean range declaration rejected"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	Channel.Source.Parameters[1].bHasRange = false;
	TestTrue(TEXT("unused boolean without range accepted"), VoxelLandM4OverrideValidation::Validate(Channel, Error));
	return true;
}
#endif
