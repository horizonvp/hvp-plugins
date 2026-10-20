#include "HVPTextFontMaker.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "Dialogs/DlgPickPath.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "IPythonScriptPlugin.h"
#include "Interfaces/IPluginManager.h"
#include "LetterFontAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/AppStyle.h"
#include "TextRenderSettings.h"
#include "ToolMenus.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HVPTextFontMaker"

namespace
{
	const FName MenuOwner(TEXT("HVPTextFontMaker"));

	/** What the dialog asks for. Folders are content paths (/Game/...). */
	struct FMakeFontRequest
	{
		FString Id;
		FString FontFolder;
		FString MaterialsFolder;
		bool bMSDF = true;
		bool bMeshes = true;
	};

	uint32 ReadBigEndian(const TArray<uint8>& Data, int64 At, int32 Bytes)
	{
		uint32 Value = 0;
		for (int32 Byte = 0; Byte < Bytes; ++Byte)
		{
			Value = Value << 8 | (At + Byte < Data.Num() ? Data[At + Byte] : 0);
		}
		return Value;
	}

	/** The OS/2 table's usWeightClass (400 regular, 500 medium, 700 bold), or 400 without one. TTF and
	    OTF share the sfnt table directory; a collection (ttcf) is not read. */
	int32 ReadWeightClass(const TArray<uint8>& Data)
	{
		const int32 NumTables = ReadBigEndian(Data, 4, 2);
		for (int32 Table = 0; Table < NumTables; ++Table)
		{
			const int64 Record = 12 + 16 * Table;
			if (Record + 16 > Data.Num())
			{
				break;
			}
			if (FMemory::Memcmp(&Data[Record], "OS/2", 4) == 0)
			{
				const int32 Weight = ReadBigEndian(Data, ReadBigEndian(Data, Record + 8, 4) + 4, 2);
				return Weight > 0 ? Weight : 400;
			}
		}
		return 400;
	}

	/** The family part of a Font Face's name, letters and digits only: Gotham_Medium_FF -> Gotham. */
	FString FamilyName(FString Name)
	{
		Name.RemoveFromEnd(TEXT("_FF"));
		Name.RemoveFromStart(TEXT("FF_"));
		int32 Underscore;
		if (Name.FindChar(TEXT('_'), Underscore) && Underscore > 0)
		{
			Name.LeftInline(Underscore);
		}
		FString Family;
		for (const TCHAR C : Name)
		{
			if (FChar::IsAlnum(C))
			{
				Family.AppendChar(C);
			}
		}
		return Family.IsEmpty() ? TEXT("Font") : Family;
	}

	/** Font ids name files and assets: letters, digits, - and _. */
	bool IsValidId(const FString& Id)
	{
		if (Id.IsEmpty())
		{
			return false;
		}
		for (const TCHAR C : Id)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	bool IsValidFolder(const FString& Folder)
	{
		return Folder.StartsWith(TEXT("/")) && FPackageName::IsValidLongPackageName(Folder / TEXT("X"));
	}

	/** Shared by every font: where the project's Glyph Mesh Material already is, else the importer's default. */
	FString DefaultMaterialsFolder()
	{
		const FSoftObjectPath& Existing = GetDefault<UTextRenderSettings>()->GlyphMeshMaterial;
		return Existing.IsNull() ? FString(TEXT("/Game/HVPText/Materials")) : FPackageName::GetLongPackagePath(Existing.GetLongPackageName());
	}

	class SMakeFontDialog : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMakeFontDialog) {}
			SLATE_ARGUMENT(FMakeFontRequest, Request)
			/** The id's default parent folder: the font folder follows the id until it is edited. */
			SLATE_ARGUMENT(FString, FamilyFolder)
			SLATE_ARGUMENT(TSharedPtr<SWindow>, Window)
		SLATE_END_ARGS()

		FMakeFontRequest Request;
		bool bConfirmed = false;

		void Construct(const FArguments& InArgs)
		{
			Request = InArgs._Request;
			FamilyFolder = InArgs._FamilyFolder;
			Window = InArgs._Window;

			auto Label = [](const FText& Text)
			{
				return SNew(STextBlock).Text(Text);
			};
			auto Browse = [this](FString* Folder, bool bIsFontFolder)
			{
				return SNew(SButton)
					.Text(LOCTEXT("Browse", "Browse..."))
					.OnClicked_Lambda([this, Folder, bIsFontFolder]()
					{
						TSharedRef<SDlgPickPath> Picker = SNew(SDlgPickPath)
							.Title(LOCTEXT("PickFolder", "Choose a folder"))
							.DefaultPath(FText::FromString(*Folder));
						if (Picker->ShowModal() == EAppReturnType::Ok)
						{
							*Folder = Picker->GetPath().ToString();
							bFontFolderEdited |= bIsFontFolder;
						}
						return FReply::Handled();
					});
			};
			auto Technique = [](bool* bValue, const FText& Text, const FText& Tip)
			{
				return SNew(SCheckBox)
					.IsChecked_Lambda([bValue]() { return *bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([bValue](ECheckBoxState State) { *bValue = State == ECheckBoxState::Checked; })
					.ToolTipText(Tip)
					[
						SNew(STextBlock).Text(Text)
					];
			};

			ChildSlot
			[
				SNew(SBox)
				.Padding(16.f)
				.MinDesiredWidth(560.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SGridPanel)
						.FillColumn(1, 1.f)
						+ SGridPanel::Slot(0, 0).Padding(0.f, 4.f, 12.f, 4.f).VAlign(VAlign_Center)
						[
							Label(LOCTEXT("Id", "Font id"))
						]
						+ SGridPanel::Slot(1, 0).Padding(0.f, 4.f).ColumnSpan(2)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([this]() { return FText::FromString(Request.Id); })
							.ToolTipText(LOCTEXT("IdTip", "Names the font's assets: <id>_DA, <id>_MSDF_T, <id>_U0041_SM. Family-weight by convention."))
							.OnTextChanged_Lambda([this](const FText& Text)
							{
								Request.Id = Text.ToString();
								if (!bFontFolderEdited)
								{
									Request.FontFolder = FamilyFolder / Request.Id;
								}
							})
						]
						+ SGridPanel::Slot(0, 1).Padding(0.f, 4.f, 12.f, 4.f).VAlign(VAlign_Center)
						[
							Label(LOCTEXT("Folder", "Folder"))
						]
						+ SGridPanel::Slot(1, 1).Padding(0.f, 4.f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([this]() { return FText::FromString(Request.FontFolder); })
							.ToolTipText(LOCTEXT("FolderTip", "Where the font asset, its atlas and its glyph meshes go."))
							.OnTextChanged_Lambda([this](const FText& Text)
							{
								Request.FontFolder = Text.ToString();
								bFontFolderEdited = true;
							})
						]
						+ SGridPanel::Slot(2, 1).Padding(6.f, 4.f, 0.f, 4.f)
						[
							Browse(&Request.FontFolder, true)
						]
						+ SGridPanel::Slot(0, 2).Padding(0.f, 4.f, 12.f, 4.f).VAlign(VAlign_Center)
						[
							Label(LOCTEXT("Materials", "Materials folder"))
						]
						+ SGridPanel::Slot(1, 2).Padding(0.f, 4.f)
						[
							SNew(SEditableTextBox)
							.Text_Lambda([this]() { return FText::FromString(Request.MaterialsFolder); })
							.ToolTipText(LOCTEXT("MaterialsTip", "M_LetterMSDF and M_LetterMesh, shared by every font. Made only if they are not there yet."))
							.OnTextChanged_Lambda([this](const FText& Text) { Request.MaterialsFolder = Text.ToString(); })
						]
						+ SGridPanel::Slot(2, 2).Padding(6.f, 4.f, 0.f, 4.f)
						[
							Browse(&Request.MaterialsFolder, false)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 24.f, 0.f)
						[
							Technique(&Request.bMSDF, LOCTEXT("MSDF", "As MSDF"),
								LOCTEXT("MSDFTip", "Flat distance-field quads, sharp at any size, with a glow: an atlas and its material instance."))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Technique(&Request.bMeshes, LOCTEXT("Meshes", "As meshes"),
								LOCTEXT("MeshesTip", "Extruded glyph meshes with 3 LODs: real thickness when a letter turns."))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return Problem(); })
						.ColorAndOpacity(FAppStyle::GetSlateColor("Colors.Warning"))
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 12.f, 0.f, 0.f)
					[
						SNew(SUniformGridPanel)
						.SlotPadding(FMargin(6.f, 0.f, 0.f, 0.f))
						+ SUniformGridPanel::Slot(0, 0)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("Make", "Make"))
							.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
							.IsEnabled_Lambda([this]() { return Problem().IsEmpty(); })
							.OnClicked_Lambda([this]() { return Close(true); })
						]
						+ SUniformGridPanel::Slot(1, 0)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.Text(LOCTEXT("Cancel", "Cancel"))
							.OnClicked_Lambda([this]() { return Close(false); })
						]
					]
				]
			];
		}

	private:
		FString FamilyFolder;
		TWeakPtr<SWindow> Window;
		bool bFontFolderEdited = false;

		FText Problem() const
		{
			if (!IsValidId(Request.Id))
			{
				return LOCTEXT("BadId", "The font id needs letters, digits, - or _ only.");
			}
			if (!IsValidFolder(Request.FontFolder) || !IsValidFolder(Request.MaterialsFolder))
			{
				return LOCTEXT("BadFolder", "Folders are content paths, like /Game/Fonts/Gotham.");
			}
			if (!Request.bMSDF && !Request.bMeshes)
			{
				return LOCTEXT("NoTechnique", "Tick MSDF, meshes or both.");
			}
			return FText::GetEmpty();
		}

		FReply Close(bool bMake)
		{
			bConfirmed = bMake;
			if (TSharedPtr<SWindow> Pinned = Window.Pin())
			{
				Pinned->RequestDestroyWindow();
			}
			return FReply::Handled();
		}
	};

	bool AskForRequest(const UFontFace& Face, int32 Weight, FMakeFontRequest& OutRequest)
	{
		const FString Family = FamilyName(Face.GetName());
		const FString FamilyFolder = FPackageName::GetLongPackagePath(Face.GetPackage()->GetName()) / Family;
		FMakeFontRequest Request;
		Request.Id = FString::Printf(TEXT("%s-%d"), *Family, Weight);
		Request.FontFolder = FamilyFolder / Request.Id;
		Request.MaterialsFolder = DefaultMaterialsFolder();

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(FText::Format(LOCTEXT("Title", "Make HVPText Font from {0}"), FText::FromString(Face.GetName())))
			.SizingRule(ESizingRule::Autosized)
			.SupportsMinimize(false)
			.SupportsMaximize(false);
		TSharedRef<SMakeFontDialog> Dialog = SNew(SMakeFontDialog)
			.Request(Request)
			.FamilyFolder(FamilyFolder)
			.Window(Window);
		Window->SetContent(Dialog);
		GEditor->EditorAddModalWindow(Window);
		OutRequest = Dialog->Request;
		return Dialog->bConfirmed;
	}

	void Fail(const FText& Why)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Why, LOCTEXT("FailedTitle", "Make HVPText Font"));
	}

	/** Point Project Settings > HVP Text at what was made, wherever it has nothing yet. */
	void FillEmptySettings(const FString& FontPath, const FString& GlyphMeshMaterialPath, bool bMSDF)
	{
		UTextRenderSettings* Settings = GetMutableDefault<UTextRenderSettings>();
		bool bChanged = false;
		auto Fill = [&bChanged](FSoftObjectPath& Setting, const FString& Path)
		{
			if (Setting.IsNull() && !Path.IsEmpty())
			{
				Setting = FSoftObjectPath(Path);
				bChanged = true;
			}
		};
		Fill(Settings->DefaultFont, FontPath);
		Fill(Settings->GlyphMeshMaterial, GlyphMeshMaterialPath);
		if (bMSDF)
		{
			Fill(Settings->QuadMesh, TEXT("/Engine/BasicShapes/Plane.Plane"));
		}
		if (bChanged)
		{
			Settings->TryUpdateDefaultConfigFile();
		}
	}

	void MakeFont(UFontFace& Face)
	{
		const TArray<uint8>& Data = Face.GetFontFaceData()->GetData();
		if (Data.Num() < 12)
		{
			Fail(FText::Format(LOCTEXT("NoData", "{0} holds no font data to build from."), FText::FromString(Face.GetName())));
			return;
		}
		FMakeFontRequest Request;
		if (!AskForRequest(Face, ReadWeightClass(Data), Request))
		{
			return;
		}
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (!Python || !Python->IsPythonAvailable())
		{
			Fail(LOCTEXT("NoPython", "Make HVPText Font runs the HVPText font tools, which need the Python Editor Script Plugin."));
			return;
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HVPSystems"));
		const FString Script = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Tools/HVPText/make_font.py"));

		// A fresh work folder per font: the font file in, the tools' output and the result out.
		const FString Work = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("HVPText") / Request.Id);
		IFileManager::Get().DeleteDirectory(*Work, false, true);
		if (!FFileHelper::SaveArrayToFile(Data, *(Work / TEXT("Fonts") / Request.Id + TEXT(".ttf"))))
		{
			Fail(FText::Format(LOCTEXT("NoWrite", "Could not write the font to {0}."), FText::FromString(Work)));
			return;
		}

		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Command.Command = FString::Printf(TEXT("\"%s\" \"--work=%s\" \"--id=%s\" \"--font-folder=%s\" \"--materials-folder=%s\"%s%s"),
			*Script, *Work, *Request.Id, *Request.FontFolder, *Request.MaterialsFolder,
			Request.bMSDF ? TEXT(" --msdf") : TEXT(""), Request.bMeshes ? TEXT(" --meshes") : TEXT(""));
		Python->ExecPythonCommandEx(Command);

		FString ResultText;
		TSharedPtr<FJsonObject> Result;
		if (!FFileHelper::LoadFileToString(ResultText, *(Work / TEXT("result.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ResultText), Result) || !Result.IsValid())
		{
			Fail(FText::Format(LOCTEXT("NoResult", "make_font.py did not finish: {0}"), FText::FromString(Command.CommandResult)));
			return;
		}
		if (!Result->GetBoolField(TEXT("ok")))
		{
			Fail(FText::FromString(Result->GetStringField(TEXT("error"))));
			return;
		}

		FString FontPath;
		const TArray<TSharedPtr<FJsonValue>>* Fonts;
		if (Result->TryGetArrayField(TEXT("fonts"), Fonts) && !Fonts->IsEmpty())
		{
			FontPath = (*Fonts)[0]->AsString();
		}
		FString GlyphMeshMaterialPath;
		Result->TryGetStringField(TEXT("glyphMeshMaterial"), GlyphMeshMaterialPath);
		FillEmptySettings(FontPath, GlyphMeshMaterialPath, Result->GetBoolField(TEXT("msdf")));

		FNotificationInfo Info(FText::Format(LOCTEXT("Made", "Made HVPText font {0}"), FText::FromString(Request.Id)));
		Info.ExpireDuration = 6.f;
		if (!FontPath.IsEmpty())
		{
			Info.Hyperlink = FSimpleDelegate::CreateLambda([FontPath]()
			{
				const FAssetData Asset(LoadObject<ULetterFontAsset>(nullptr, *FontPath));
				FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets({ Asset });
			});
			Info.HyperlinkText = LOCTEXT("Show", "Show in Content Browser");
		}
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	void ExtendFontFaceMenu()
	{
		FToolMenuOwnerScoped Owner(MenuOwner);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.FontFace");
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		Section.AddDynamicEntry("HVPTextMakeFont", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (!Context || Context->SelectedAssets.IsEmpty())
			{
				return;
			}
			const TArray<FAssetData> Assets = Context->SelectedAssets;
			InSection.AddMenuEntry("HVPTextMakeFont",
				LOCTEXT("MenuLabel", "Make HVPText Font..."),
				LOCTEXT("MenuTip", "Build an HVPText font asset from this font face: MSDF quads, glyph meshes or both."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Font"),
				FUIAction(FExecuteAction::CreateLambda([Assets]()
				{
					for (const FAssetData& Asset : Assets)
					{
						if (UFontFace* Face = Cast<UFontFace>(Asset.GetAsset()))
						{
							MakeFont(*Face);
						}
					}
				})));
		}));
	}
}

namespace HVPTextFontMaker
{
	void Register()
	{
		if (!IsRunningCommandlet())
		{
			UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&ExtendFontFaceMenu));
		}
	}

	void Unregister()
	{
		if (UToolMenus* Menus = UToolMenus::TryGet())
		{
			Menus->UnregisterOwnerByName(MenuOwner);
		}
	}
}

#undef LOCTEXT_NAMESPACE
