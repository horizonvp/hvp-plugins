#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Containers/Ticker.h"
#include "EdGraphUtilities.h"
#include "K2Node_SetNamedPrimitiveData.h"
#include "K2Node_SetNamedPrimitiveDataMulti.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Logging/MessageLog.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "PrimitiveDataLegend.h"
#include "PrimitiveDataLegendBinding.h"
#include "PropertyEditorModule.h"
#include "SetNamedPrimitiveDataMultiDetails.h"
#include "SGraphPinPrimitiveDataParameter.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "HVPPrimitiveDataEditor"

/**
 * Wiring. Three jobs, all reactions to something the user did elsewhere:
 *
 *   - a legend was edited: re-lay out its bound materials, refresh the nodes that use it;
 *   - a bound material was saved: check it still matches its legend, and say so if not;
 *   - the right-click entries on legends and on materials.
 *
 * The first two are DEFERRED to the next tick. A legend edit arrives from inside PostEditChange (and
 * from inside undo), and a save arrives from inside SavePackage, where loading assets or recompiling
 * materials is at best rude and at worst illegal. One tick later nothing is mid-flight.
 */
class FHVPPrimitiveDataEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FMessageLogModule& MessageLog = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		FMessageLogInitializationOptions Options;
		Options.bShowFilters = true;
		Options.bAllowClear = true;
		MessageLog.RegisterLogListing(FPrimitiveDataLegendBinding::LogName, LOCTEXT("LogLabel", "Primitive Data Legend"), Options);

		PinFactory = MakeShared<FPrimitiveDataParameterPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);

		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomClassLayout(UK2Node_SetNamedPrimitiveDataMulti::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FSetNamedPrimitiveDataMultiDetails::MakeInstance));

		LegendChangedHandle = UPrimitiveDataLegend::OnChanged.AddRaw(this, &FHVPPrimitiveDataEditorModule::OnLegendChanged);
		PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FHVPPrimitiveDataEditorModule::OnObjectPreSave);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FHVPPrimitiveDataEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		UPrimitiveDataLegend::OnChanged.Remove(LegendChangedHandle);
		FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);

		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}

		if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
		{
			PropertyEditor->UnregisterCustomClassLayout(UK2Node_SetNamedPrimitiveDataMulti::StaticClass()->GetFName());
		}

		if (PinFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
			PinFactory.Reset();
		}

		if (FMessageLogModule* MessageLog = FModuleManager::GetModulePtr<FMessageLogModule>("MessageLog"))
		{
			MessageLog->UnregisterLogListing(FPrimitiveDataLegendBinding::LogName);
		}
	}

private:
	// -----------------------------------------------------------------------
	// Deferred work
	// -----------------------------------------------------------------------

	struct FPendingLegend
	{
		TMap<FName, FName> Renames;
		bool bLayoutChanged = false;
	};

	void Schedule()
	{
		if (!TickerHandle.IsValid())
		{
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateRaw(this, &FHVPPrimitiveDataEditorModule::RunDeferred));
		}
	}

	void OnLegendChanged(UPrimitiveDataLegend* Legend, const TMap<FName, FName>& Renames, bool bLayoutChanged)
	{
		if (!Legend)
		{
			return;
		}

		FPendingLegend& Pending = PendingLegends.FindOrAdd(Legend);
		Pending.bLayoutChanged |= bLayoutChanged;

		// Two renames of one entry inside a tick (A -> B, then B -> C) have to reach the material as
		// A -> C: it only ever had A.
		for (const TPair<FName, FName>& Rename : Renames)
		{
			bool bChained = false;
			for (TPair<FName, FName>& Existing : Pending.Renames)
			{
				if (Existing.Value == Rename.Key)
				{
					Existing.Value = Rename.Value;
					bChained = true;
				}
			}
			if (!bChained)
			{
				Pending.Renames.Add(Rename.Key, Rename.Value);
			}
		}

		Schedule();
	}

	void OnObjectPreSave(UObject* Object, FObjectPreSaveContext Context)
	{
		if (!Object || Context.IsProceduralSave() || IsRunningCommandlet())
		{
			return;
		}
		if (!Object->IsA<UMaterial>() && !Object->IsA<UMaterialFunction>())
		{
			return;
		}
		// The material editor's working copy lives in the transient package; only the real asset counts.
		if (Object->HasAnyFlags(RF_Transient) || Object->GetPackage()->HasAnyFlags(RF_Transient))
		{
			return;
		}

		PendingSaveChecks.Add(FSoftObjectPath(Object));
		Schedule();
	}

	bool RunDeferred(float)
	{
		TickerHandle.Reset();

		TMap<TWeakObjectPtr<UPrimitiveDataLegend>, FPendingLegend> Legends = MoveTemp(PendingLegends);
		PendingLegends.Reset();
		TSet<FSoftObjectPath> Saved = MoveTemp(PendingSaveChecks);
		PendingSaveChecks.Reset();

		for (const TPair<TWeakObjectPtr<UPrimitiveDataLegend>, FPendingLegend>& Pair : Legends)
		{
			if (UPrimitiveDataLegend* Legend = Pair.Key.Get())
			{
				FPrimitiveDataLegendBinding::SyncAll(*Legend, /*bApply*/ true, Pair.Value.Renames);
				if (Pair.Value.bLayoutChanged)
				{
					RefreshNodes(Legend);
				}
			}
		}

		CheckSaved(Saved);

		return false;	// one-shot; Schedule() adds a fresh ticker for the next batch
	}

	/**
	 * Reconstruct every loaded node that uses this legend and mark its Blueprint for recompiling.
	 *
	 * Reconstruction picks up renames and type changes on the pins. The recompile is what picks up a
	 * moved SLOT, which no pin shows: it lives only in the compiled code, as a literal. Unloaded
	 * Blueprints need nothing - they compile against the current legend when they load.
	 */
	static void RefreshNodes(UPrimitiveDataLegend* Legend)
	{
		TSet<UBlueprint*> Touched;
		RefreshNodesOf<UK2Node_SetNamedPrimitiveData>(Legend, Touched);
		RefreshNodesOf<UK2Node_SetNamedPrimitiveDataMulti>(Legend, Touched, [](UK2Node_SetNamedPrimitiveDataMulti* Node)
		{
			// Its Details panel lists the legend's parameters; an edit may have added, removed or renamed some.
			Node->OnParametersOffered.Broadcast();
		});
		for (UBlueprint* Blueprint : Touched)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	template <typename TNode>
	static void RefreshNodesOf(UPrimitiveDataLegend* Legend, TSet<UBlueprint*>& Touched, TFunction<void(TNode*)> After = nullptr)
	{
		for (TObjectIterator<TNode> It; It; ++It)
		{
			TNode* Node = *It;
			if (!IsValid(Node) || Node->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Node->GetLegend() != Legend)
			{
				continue;
			}
			// No Blueprint means a compiler intermediate or an orphaned graph; nothing to refresh.
			UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
			if (!Blueprint)
			{
				continue;
			}
			Node->ReconstructNode();
			if (After)
			{
				After(Node);
			}
			Touched.Add(Blueprint);
		}
	}

	/**
	 * The "enforcing" half: a bound material that was just saved gets checked against its legend. It is
	 * reported, never changed - it has just been saved, and rewriting it would dirty it again.
	 */
	static void CheckSaved(const TSet<FSoftObjectPath>& Saved)
	{
		if (Saved.Num() == 0)
		{
			return;
		}

		FMessageLog Log(FPrimitiveDataLegendBinding::LogName);
		bool bProblems = false;
		for (const FSoftObjectPath& Path : Saved)
		{
			UObject* Material = Path.ResolveObject();
			if (!Material)
			{
				continue;
			}
			for (UPrimitiveDataLegend* Legend : FPrimitiveDataLegendBinding::FindLegendsBinding(Material))
			{
				const FPrimitiveDataBindingResult Result = FPrimitiveDataLegendBinding::Sync(*Legend, Material, /*bApply*/ false);
				if (!Result.IsClean())
				{
					FPrimitiveDataLegendBinding::ReportTo(Log, *Legend, Material, Result);
					bProblems = true;
				}
			}
		}
		if (bProblems)
		{
			Log.Notify(LOCTEXT("SavedDrift", "A saved material no longer matches its Primitive Data Legend"),
				EMessageSeverity::Warning, /*bForce*/ true);
		}
	}

	// -----------------------------------------------------------------------
	// Menus
	// -----------------------------------------------------------------------

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		RegisterLegendMenu();
		RegisterBindMenu(UMaterial::StaticClass());
		// Material layers and blends derive from UMaterialFunction; their menus inherit this one.
		RegisterBindMenu(UMaterialFunction::StaticClass());
	}

	static void RegisterLegendMenu()
	{
		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UPrimitiveDataLegend::StaticClass());
		if (!Menu)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
		Section.AddMenuEntry(
			TEXT("HVPPrimitiveDataSync"),
			LOCTEXT("SyncLabel", "Sync Bound Materials"),
			LOCTEXT("SyncTooltip",
				"Lay out every bound material by this legend: parameters named like an entry are switched to "
				"custom primitive data and given its slot. Happens automatically when the legend is edited; "
				"this is for when a material changed instead."),
			FSlateIcon(),
			FToolUIActionChoice(FToolMenuExecuteAction::CreateStatic(&RunOnSelectedLegends, true)));

		Section.AddMenuEntry(
			TEXT("HVPPrimitiveDataCheck"),
			LOCTEXT("CheckLabel", "Check Bound Materials"),
			LOCTEXT("CheckTooltip", "Report how every bound material differs from this legend, without changing anything."),
			FSlateIcon(),
			FToolUIActionChoice(FToolMenuExecuteAction::CreateStatic(&RunOnSelectedLegends, false)));
	}

	static void RunOnSelectedLegends(const FToolMenuContext& Context, bool bApply)
	{
		const UContentBrowserAssetContextMenuContext* Browser = Context.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Browser)
		{
			return;
		}
		for (UPrimitiveDataLegend* Legend : Browser->LoadSelectedObjects<UPrimitiveDataLegend>())
		{
			FPrimitiveDataLegendBinding::SyncAll(*Legend, bApply);
		}
	}

	static void RegisterBindMenu(UClass* MaterialClass)
	{
		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(MaterialClass);
		if (!Menu)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
		Section.AddSubMenu(
			TEXT("HVPPrimitiveDataBind"),
			LOCTEXT("BindLabel", "Bind to Primitive Data Legend"),
			LOCTEXT("BindTooltip",
				"Let a legend lay this material's custom primitive data out for it. A material belongs to one "
				"legend at a time; choosing the ticked one unbinds it."),
			FNewToolMenuDelegate::CreateStatic(&FillBindMenu));
	}

	/**
	 * Built when the submenu opens, so it always lists the legends that exist now. Only the legends are
	 * loaded to read their binding lists - small assets. The selected materials are not loaded until
	 * one is actually bound.
	 */
	static void FillBindMenu(UToolMenu* SubMenu)
	{
		const UContentBrowserAssetContextMenuContext* Browser = SubMenu->FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Browser)
		{
			return;
		}

		TArray<FSoftObjectPath> Selected;
		for (const FAssetData& Asset : Browser->SelectedAssets)
		{
			if (Asset.IsInstanceOf(UMaterial::StaticClass()) || Asset.IsInstanceOf(UMaterialFunction::StaticClass()))
			{
				Selected.Add(Asset.GetSoftObjectPath());
			}
		}

		TArray<FAssetData> Legends;
		FAssetRegistryModule::GetRegistry().GetAssetsByClass(UPrimitiveDataLegend::StaticClass()->GetClassPathName(), Legends);
		Legends.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });

		FToolMenuSection& Section = SubMenu->AddSection(TEXT("Legends"), LOCTEXT("LegendsHeading", "Primitive Data Legends"));

		if (Legends.Num() == 0)
		{
			Section.AddMenuEntry(
				TEXT("NoLegends"),
				LOCTEXT("NoLegendsLabel", "No legends yet - Add > Materials > Primitive Data Legend"),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
			return;
		}

		for (const FAssetData& LegendAsset : Legends)
		{
			const UPrimitiveDataLegend* Legend = Cast<UPrimitiveDataLegend>(LegendAsset.GetAsset());
			if (!Legend)
			{
				continue;
			}

			// Ticked when every selected material is already bound here; choosing it then unbinds.
			bool bAllBound = Selected.Num() > 0;
			for (const FSoftObjectPath& Path : Selected)
			{
				if (!Legend->BoundMaterials.ContainsByPredicate(
					[&Path](const TSoftObjectPtr<UObject>& Bound) { return Bound.ToSoftObjectPath() == Path; }))
				{
					bAllBound = false;
					break;
				}
			}

			const FSoftObjectPath LegendPath = LegendAsset.GetSoftObjectPath();
			Section.AddMenuEntry(
				LegendAsset.AssetName,
				FText::FromName(LegendAsset.AssetName),
				FText::Format(LOCTEXT("LegendEntryTooltip", "{0} parameter(s), {1} of {2} floats used."),
					FText::AsNumber(Legend->Parameters.Num()), FText::AsNumber(Legend->GetUsedFloats()),
					FText::AsNumber(UPrimitiveDataLegend::GetCapacity())),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([LegendPath, Selected, bAllBound]()
					{
						UPrimitiveDataLegend* Target = Cast<UPrimitiveDataLegend>(LegendPath.TryLoad());
						if (!Target)
						{
							return;
						}
						const FScopedTransaction Transaction(bAllBound
							? LOCTEXT("UnbindTransaction", "Unbind from Primitive Data Legend")
							: LOCTEXT("BindTransaction", "Bind to Primitive Data Legend"));
						for (const FSoftObjectPath& Path : Selected)
						{
							if (UObject* Material = Path.TryLoad())
							{
								if (bAllBound)
								{
									FPrimitiveDataLegendBinding::Unbind(*Target, Material);
								}
								else
								{
									FPrimitiveDataLegendBinding::Bind(*Target, Material);
								}
							}
						}
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([bAllBound]() { return bAllBound; })),
				EUserInterfaceActionType::ToggleButton);
		}
	}

	TSharedPtr<FPrimitiveDataParameterPinFactory> PinFactory;
	FDelegateHandle LegendChangedHandle;
	FDelegateHandle PreSaveHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	TMap<TWeakObjectPtr<UPrimitiveDataLegend>, FPendingLegend> PendingLegends;
	TSet<FSoftObjectPath> PendingSaveChecks;
};

IMPLEMENT_MODULE(FHVPPrimitiveDataEditorModule, HVPPrimitiveDataEditor);

#undef LOCTEXT_NAMESPACE
