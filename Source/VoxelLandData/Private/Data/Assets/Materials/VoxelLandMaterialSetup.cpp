#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"

#if WITH_EDITOR
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "UObject/UnrealType.h"

void UVoxelLandMaterialSetup::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FVoxelLandMaterialRule, Surface))
	{
		const int32 RuleIndex =
			PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(UVoxelLandMaterialSetup, Rules));
		if (Rules.IsValidIndex(RuleIndex))
		{
			FVoxelLandMaterialRule& Rule = Rules[RuleIndex];
			Rule.SurfaceId = Rule.Surface ? Rule.Surface->SurfaceId : FGuid();
		}
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif
