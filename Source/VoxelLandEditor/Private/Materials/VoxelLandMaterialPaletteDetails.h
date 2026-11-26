#pragma once

#include "IDetailCustomization.h"

class FReply;
class UVoxelLandMaterialPalette;

class FVoxelLandMaterialPaletteDetails final : public IDetailCustomization
{
public:
	[[nodiscard]] static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	[[nodiscard]] static FReply Bake(TWeakObjectPtr<UVoxelLandMaterialPalette> Palette);
	[[nodiscard]] static FText GetStatus(TWeakObjectPtr<UVoxelLandMaterialPalette> Palette);
};
