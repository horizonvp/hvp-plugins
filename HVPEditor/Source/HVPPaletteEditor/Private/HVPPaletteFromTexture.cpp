#include "HVPPaletteFromTexture.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialParameterCollectionFactoryNew.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "ImageCore.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HVPPaletteFromTexture"

DEFINE_LOG_CATEGORY_STATIC(LogHVPPaletteFromTexture, Log, All);

namespace HVPPaletteFromTex
{
	/** 8x8, to agree with the swatch texture the generator writes. */
	static constexpr int32 DefaultGrid = 8;

	/** Per axis. The real ceiling is the collection's, checked separately against the total. */
	static constexpr int32 MaxAxis = 64;

	/**
	 * r.MPC.MaxVectorStorage, 1280 by default. Checked here rather than left to the engine, which
	 * warns at load time on the collection instead of at the point the mistake was made.
	 */
	static constexpr int32 MaxVectorParameters = 1280;

	/**
	 * Alpha at or below this counts as an empty cell. Not exactly zero: a palette that has been
	 * through a lossy encoder, or authored with a soft eraser, carries a few thousandths rather
	 * than a clean nothing.
	 */
	static constexpr float EmptyAlpha = 0.004f;

	/** "MyPalette_T" -> "MyPalette". Mirrors BaseNameOf in the generator, for the texture suffix. */
	static FString BaseNameOf(const UTexture2D& Texture)
	{
		FString Name = Texture.GetName();
		Name.RemoveFromEnd(TEXT("_T"));
		Name.RemoveFromStart(TEXT("T_"));
		return Name;
	}
}

bool FHVPPaletteFromTexture::PromptForGrid(int32& InOutX, int32& InOutY)
{
	using namespace HVPPaletteFromTex;

	TSharedPtr<SWindow> Window;
	bool bAccepted = false;
	int32 GridX = FMath::Clamp(InOutX, 1, MaxAxis);
	int32 GridY = FMath::Clamp(InOutY, 1, MaxAxis);

	auto MakeRow = [](const FText& Label, int32* Value)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(90.f)
				[
					SNew(SSpinBox<int32>)
					.MinValue(1).MaxValue(MaxAxis)
					.MinSliderValue(1).MaxSliderValue(16)
					.Value_Lambda([Value] { return *Value; })
					.OnValueChanged_Lambda([Value](int32 NewValue) { *Value = NewValue; })
				]
			];
	};

	Window = SNew(SWindow)
		.Title(LOCTEXT("GridTitle", "Palette Grid"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	Window->SetContent(
		SNew(SBox).Padding(16.f).MinDesiredWidth(320.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("GridHelp",
					"How many swatches across and down. Each cell is sampled at its centre, so grid "
					"lines and soft edges are ignored. Trailing empty cells are skipped."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[
				MakeRow(LOCTEXT("GridX", "Columns (X)"), &GridX)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				MakeRow(LOCTEXT("GridY", "Rows (Y)"), &GridY)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f).HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel).SlotPadding(FMargin(4.f, 0.f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("GridOk", "Build Collection"))
					.OnClicked_Lambda([&bAccepted, &Window]
					{
						bAccepted = true;
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("GridCancel", "Cancel"))
					.OnClicked_Lambda([&Window]
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]);

	// Modal, so the locals the spin boxes write through outlive the call.
	FSlateApplication::Get().AddModalWindow(
		Window.ToSharedRef(), FSlateApplication::Get().GetActiveTopLevelWindow());

	if (bAccepted)
	{
		InOutX = GridX;
		InOutY = GridY;
	}
	return bAccepted;
}

void FHVPPaletteFromTexture::RegisterMenus()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.Texture2D"));
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
	Section.AddMenuEntry(
		TEXT("HVPPaletteFromTexture"),
		LOCTEXT("FromTextureLabel", "Create Palette Collection from Texture"),
		LOCTEXT("FromTextureTooltip",
			"Read this texture as a grid of swatches and write them into a Material Parameter "
			"Collection beside it, creating it if it does not exist. Existing parameters keep their "
			"names and identities - only their colours are rewritten - so materials already using "
			"the collection keep working."),
		FSlateIcon(),
		FToolUIActionChoice(FToolMenuExecuteAction::CreateLambda(
			[](const FToolMenuContext& Context)
			{
				const UContentBrowserAssetContextMenuContext* CBContext =
					Context.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!CBContext)
				{
					return;
				}

				TArray<UTexture2D*> Textures = CBContext->LoadSelectedObjects<UTexture2D>();
				if (Textures.Num() == 0)
				{
					return;
				}

				// One prompt for the whole selection: a grid is a property of how the palettes were
				// authored, and anyone selecting several at once is saying they share a layout.
				int32 GridX = HVPPaletteFromTex::DefaultGrid;
				int32 GridY = HVPPaletteFromTex::DefaultGrid;
				if (!PromptForGrid(GridX, GridY))
				{
					return;
				}

				int32 Built = 0;
				for (UTexture2D* Texture : Textures)
				{
					if (BuildFromTexture(Texture, GridX, GridY))
					{
						++Built;
					}
				}

				if (Built == 0)
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						LOCTEXT("NothingBuilt",
							"No collection was written. See the Output Log for which texture was "
							"rejected and why."));
				}
			})));
}

UMaterialParameterCollection* FHVPPaletteFromTexture::BuildFromTexture(
	UTexture2D* Texture, int32 GridX, int32 GridY)
{
	using namespace HVPPaletteFromTex;

	if (!Texture || GridX < 1 || GridY < 1)
	{
		return nullptr;
	}

	if (GridX * GridY > MaxVectorParameters)
	{
		UE_LOG(LogHVPPaletteFromTexture, Error,
			TEXT("%dx%d is %d cells; a collection holds at most %d vector parameters."),
			GridX, GridY, GridX * GridY, MaxVectorParameters);
		return nullptr;
	}

	// The SOURCE, not the platform data: the cooked texture is compressed, possibly downscaled, and
	// on some platforms not addressable from the CPU at all. The source is what the artist saved.
	FImage Source;
	if (!Texture->Source.IsValid() || !Texture->Source.GetMipImage(Source, 0))
	{
		UE_LOG(LogHVPPaletteFromTexture, Error,
			TEXT("'%s' has no readable source image."), *Texture->GetName());
		return nullptr;
	}

	FImageView View = Source;
	// FTextureSource carries no gamma of its own - the flag lives on the texture - so an 8-bit
	// source would otherwise be decoded with whatever default its format implies. Float formats
	// ignore this, which is why it is only applied where the format actually needs it.
	if (ERawImageFormat::GetFormatNeedsGammaSpace(View.Format))
	{
		View.GammaSpace = Texture->SRGB ? EGammaSpace::sRGB : EGammaSpace::Linear;
	}

	// Centre of each cell. Sampling the centre rather than averaging the cell is what makes this
	// work on a palette image that has grid lines, borders or antialiased swatch edges - all of
	// which an average would mix into the colour.
	const double CellW = static_cast<double>(View.SizeX) / GridX;
	const double CellH = static_cast<double>(View.SizeY) / GridY;

	TArray<FLinearColor> Colours;
	Colours.Reserve(GridX * GridY);
	for (int32 Row = 0; Row < GridY; ++Row)
	{
		for (int32 Col = 0; Col < GridX; ++Col)
		{
			const int32 X = FMath::Clamp(FMath::FloorToInt32((Col + 0.5) * CellW), 0, View.SizeX - 1);
			const int32 Y = FMath::Clamp(FMath::FloorToInt32((Row + 0.5) * CellH), 0, View.SizeY - 1);
			Colours.Add(View.GetOnePixelLinear(X, Y));
		}
	}

	// Trailing empty cells only. A transparent cell in the MIDDLE is kept, because dropping it would
	// shift every index after it - and an index is exactly what the swatch texture and any material
	// reading it address by.
	int32 Count = Colours.Num();
	while (Count > 0 && Colours[Count - 1].A <= EmptyAlpha)
	{
		--Count;
	}
	if (Count == 0)
	{
		UE_LOG(LogHVPPaletteFromTexture, Warning,
			TEXT("Every cell of '%s' at %dx%d is transparent - nothing to write."),
			*Texture->GetName(), GridX, GridY);
		return nullptr;
	}
	Colours.SetNum(Count);

	const FString AssetName = BaseNameOf(*Texture) + TEXT("_MPC");
	const FString Folder = FPackageName::GetLongPackagePath(Texture->GetOutermost()->GetName());
	const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Folder, *AssetName, *AssetName);

	UMaterialParameterCollection* Collection =
		LoadObject<UMaterialParameterCollection>(nullptr, *ObjectPath);
	const bool bExisted = (Collection != nullptr);
	if (!bExisted)
	{
		IAssetTools& AssetTools =
			FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UMaterialParameterCollectionFactoryNew* Factory =
			NewObject<UMaterialParameterCollectionFactoryNew>();
		Collection = Cast<UMaterialParameterCollection>(AssetTools.CreateAsset(
			AssetName, Folder, UMaterialParameterCollection::StaticClass(), Factory));
	}
	if (!Collection)
	{
		UE_LOG(LogHVPPaletteFromTexture, Error, TEXT("Could not create or load '%s'."), *ObjectPath);
		return nullptr;
	}

	const int32 PreviousCount = Collection->VectorParameters.Num();

	Collection->Modify();
	Collection->PreEditChange(nullptr);

	// Existing entries keep BOTH their name and their Id, and take only a new colour. The Id is what
	// every material binding resolves through, so regenerating it would blank each reference; the
	// name is the user's, and re-reading a texture is not a reason to rename their parameters.
	for (int32 Index = 0; Index < Colours.Num(); ++Index)
	{
		if (Collection->VectorParameters.IsValidIndex(Index))
		{
			Collection->VectorParameters[Index].DefaultValue = Colours[Index];
			continue;
		}

		FCollectionVectorParameter Added;	// its constructor mints the Id
		Added.ParameterName = FName(*FString::FromInt(Index + 1));
		Added.DefaultValue = Colours[Index];
		Collection->VectorParameters.Add(Added);
	}

	// Fewer swatches than before means the tail no longer exists in the palette. Removed rather than
	// left stale, but said out loud: anything still referencing those parameters resolves to nothing.
	if (Collection->VectorParameters.Num() > Colours.Num())
	{
		const int32 Removed = Collection->VectorParameters.Num() - Colours.Num();
		UE_LOG(LogHVPPaletteFromTexture, Warning,
			TEXT("'%s' had %d parameters, the texture yields %d. Removing the last %d - any material "
				 "still bound to them will lose its value."),
			*AssetName, PreviousCount, Colours.Num(), Removed);
		Collection->VectorParameters.SetNum(Colours.Num());
	}

	// Rebuilds the collection's uniform buffer and pushes the new defaults to live materials.
	Collection->PostEditChange();
	Collection->MarkPackageDirty();

	if (!bExisted)
	{
		FAssetRegistryModule::AssetCreated(Collection);
	}
	TArray<UPackage*> Packages;
	Packages.Add(Collection->GetOutermost());
	UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty*/ false);

	UE_LOG(LogHVPPaletteFromTexture, Log,
		TEXT("%s '%s' from '%s' at %dx%d: %d colour(s), %d trailing empty cell(s) skipped."),
		bExisted ? TEXT("Updated") : TEXT("Created"), *AssetName, *Texture->GetName(),
		GridX, GridY, Colours.Num(), (GridX * GridY) - Colours.Num());

	return Collection;
}

#undef LOCTEXT_NAMESPACE
