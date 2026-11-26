#include "VoxelLandMaterialPaletteDetails.h"

#include "Materials/VoxelLandMaterialPaletteBaker.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "VoxelLandMaterialPaletteDetails"

TSharedRef<IDetailCustomization> FVoxelLandMaterialPaletteDetails::MakeInstance()
{
	return MakeShared<FVoxelLandMaterialPaletteDetails>();
}

void FVoxelLandMaterialPaletteDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}
	const TWeakObjectPtr<UVoxelLandMaterialPalette> Palette = Cast<UVoxelLandMaterialPalette>(Objects[0].Get());
	if (!Palette.IsValid())
	{
		return;
	}
	DetailBuilder.EditCategory(TEXT("Bake"))
		.AddCustomRow(LOCTEXT("BakePalette", "Bake Palette"))
		.NameContent()[SNew(STextBlock)
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateStatic(&GetStatus, Palette)))
				.AutoWrapText(true)]
		.ValueContent()[SNew(SButton)
				.Text(LOCTEXT("BakePalette", "Bake Palette"))
				.ToolTipText(LOCTEXT("BakeTooltip",
					"Build and validate texture arrays and the terrain material from the ordered surfaces. Save the palette to persist the bake."))
				.OnClicked(FOnClicked::CreateStatic(&Bake, Palette))];
}

FReply FVoxelLandMaterialPaletteDetails::Bake(const TWeakObjectPtr<UVoxelLandMaterialPalette> WeakPalette)
{
	UVoxelLandMaterialPalette* Palette = WeakPalette.Get();
	if (Palette)
	{
		FScopedTransaction Transaction(LOCTEXT("BakePalette", "Bake Palette"));
		FText Error;
		if (!FVoxelLandMaterialPaletteBaker::Bake(*Palette, Error))
		{
			Transaction.Cancel();
			FMessageDialog::Open(EAppMsgType::Ok, Error);
		}
	}
	return FReply::Handled();
}

FText FVoxelLandMaterialPaletteDetails::GetStatus(const TWeakObjectPtr<UVoxelLandMaterialPalette> WeakPalette)
{
	const UVoxelLandMaterialPalette* Palette = WeakPalette.Get();
	if (!Palette || Palette->BakeVersion == 0)
	{
		return LOCTEXT("Unbaked", "Palette needs a bake");
	}
	if (Palette->IsBakeStale())
	{
		return LOCTEXT("Stale", "Source changes need a new bake");
	}
	return LOCTEXT("Current", "Bake is current");
}

#undef LOCTEXT_NAMESPACE
