#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Containers/Ticker.h"
#include "EdGraphUtilities.h"
#include "K2Node_SetIndexedPrimitiveData.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Logging/MessageLog.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "PrimitiveDataIndex.h"
#include "PrimitiveDataIndexBinding.h"
#include "SGraphPinPrimitiveDataParameter.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "HVPPrimitiveDataEditor"

/**
 * Wiring. Three jobs, all reactions to something the user did elsewhere:
 *
 *   - an index was edited: re-lay out its bound materials, refresh the nodes that use it;
 *   - a bound material was saved: check it still matches its index, and say so if not;
 *   - the right-click entries on indexes and on materials.
 *
 * The first two are DEFERRED to the next tick. An index edit arrives from inside PostEditChange (and
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
		MessageLog.RegisterLogListing(FPrimitiveDataIndexBinding::LogName, LOCTEXT("LogLabel", "Primitive Data Index"), Options);

		PinFactory = MakeShared<FPrimitiveDataParameterPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);

		IndexChangedHandle = UPrimitiveDataIndex::OnChanged.AddRaw(this, &FHVPPrimitiveDataEditorModule::OnIndexChanged);
		PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FHVPPrimitiveDataEditorModule::OnObjectPreSave);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FHVPPrimitiveDataEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		UPrimitiveDataIndex::OnChanged.Remove(IndexChangedHandle);
		FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);

		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}

		if (PinFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
			PinFactory.Reset();
		}

		if (FMessageLogModule* MessageLog = FModuleManager::GetModulePtr<FMessageLogModule>("MessageLog"))
		{
			MessageLog->UnregisterLogListing(FPrimitiveDataIndexBinding::LogName);
		}
	}

private:
	// -----------------------------------------------------------------------
	// Deferred work
	// -----------------------------------------------------------------------

	struct FPendingIndex
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

	void OnIndexChanged(UPrimitiveDataIndex* Index, const TMap<FName, FName>& Renames, bool bLayoutChanged)
	{
		if (!Index)
		{
			return;
		}

		FPendingIndex& Pending = PendingIndexes.FindOrAdd(Index);
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

		TMap<TWeakObjectPtr<UPrimitiveDataIndex>, FPendingIndex> Indexes = MoveTemp(PendingIndexes);
		PendingIndexes.Reset();
		TSet<FSoftObjectPath> Saved = MoveTemp(PendingSaveChecks);
		PendingSaveChecks.Reset();

		for (const TPair<TWeakObjectPtr<UPrimitiveDataIndex>, FPendingIndex>& Pair : Indexes)
		{
			if (UPrimitiveDataIndex* Index = Pair.Key.Get())
			{
				FPrimitiveDataIndexBinding::SyncAll(*Index, /*bApply*/ true, Pair.Value.Renames);
				if (Pair.Value.bLayoutChanged)
				{
					RefreshNodes(Index);
				}
			}
		}

		CheckSaved(Saved);

		return false;	// one-shot; Schedule() adds a fresh ticker for the next batch
	}

	/**
	 * Reconstruct every loaded node that uses this index and mark its Blueprint for recompiling.
	 *
	 * Reconstruction picks up renames and type changes on the pins. The recompile is what picks up a
	 * moved SLOT, which no pin shows: it lives only in the compiled code, as a literal. Unloaded
	 * Blueprints need nothing - they compile against the current index when they load.
	 */
	static void RefreshNodes(UPrimitiveDataIndex* Index)
	{
		TSet<UBlueprint*> Touched;
		for (TObjectIterator<UK2Node_SetIndexedPrimitiveData> It; It; ++It)
		{
			UK2Node_SetIndexedPrimitiveData* Node = *It;
			if (!IsValid(Node) || Node->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Node->GetIndex() != Index)
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
			Touched.Add(Blueprint);
		}
		for (UBlueprint* Blueprint : Touched)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	/**
	 * The "enforcing" half: a bound material that was just saved gets checked against its index. It is
	 * reported, never changed - it has just been saved, and rewriting it would dirty it again.
	 */
	static void CheckSaved(const TSet<FSoftObjectPath>& Saved)
	{
		if (Saved.Num() == 0)
		{
			return;
		}

		FMessageLog Log(FPrimitiveDataIndexBinding::LogName);
		bool bProblems = false;
		for (const FSoftObjectPath& Path : Saved)
		{
			UObject* Material = Path.ResolveObject();
			if (!Material)
			{
				continue;
			}
			for (UPrimitiveDataIndex* Index : FPrimitiveDataIndexBinding::FindIndexesBinding(Material))
			{
				const FPrimitiveDataBindingResult Result = FPrimitiveDataIndexBinding::Sync(*Index, Material, /*bApply*/ false);
				if (!Result.IsClean())
				{
					FPrimitiveDataIndexBinding::ReportTo(Log, *Index, Material, Result);
					bProblems = true;
				}
			}
		}
		if (bProblems)
		{
			Log.Notify(LOCTEXT("SavedDrift", "A saved material no longer matches its Primitive Data Index"),
				EMessageSeverity::Warning, /*bForce*/ true);
		}
	}

	// -----------------------------------------------------------------------
	// Menus
	// -----------------------------------------------------------------------

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		RegisterIndexMenu();
		RegisterBindMenu(UMaterial::StaticClass());
		// Material layers and blends derive from UMaterialFunction; their menus inherit this one.
		RegisterBindMenu(UMaterialFunction::StaticClass());
	}

	static void RegisterIndexMenu()
	{
		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UPrimitiveDataIndex::StaticClass());
		if (!Menu)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
		Section.AddMenuEntry(
			TEXT("HVPPrimitiveDataSync"),
			LOCTEXT("SyncLabel", "Sync Bound Materials"),
			LOCTEXT("SyncTooltip",
				"Lay out every bound material by this index: parameters named like an entry are switched to "
				"custom primitive data and given its slot. Happens automatically when the index is edited; "
				"this is for when a material changed instead."),
			FSlateIcon(),
			FToolUIActionChoice(FToolMenuExecuteAction::CreateStatic(&RunOnSelectedIndexes, true)));

		Section.AddMenuEntry(
			TEXT("HVPPrimitiveDataCheck"),
			LOCTEXT("CheckLabel", "Check Bound Materials"),
			LOCTEXT("CheckTooltip", "Report how every bound material differs from this index, without changing anything."),
			FSlateIcon(),
			FToolUIActionChoice(FToolMenuExecuteAction::CreateStatic(&RunOnSelectedIndexes, false)));
	}

	static void RunOnSelectedIndexes(const FToolMenuContext& Context, bool bApply)
	{
		const UContentBrowserAssetContextMenuContext* Browser = Context.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Browser)
		{
			return;
		}
		for (UPrimitiveDataIndex* Index : Browser->LoadSelectedObjects<UPrimitiveDataIndex>())
		{
			FPrimitiveDataIndexBinding::SyncAll(*Index, bApply);
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
			LOCTEXT("BindLabel", "Bind to Primitive Data Index"),
			LOCTEXT("BindTooltip",
				"Let an index lay this material's custom primitive data out for it. A material belongs to one "
				"index at a time; choosing the ticked one unbinds it."),
			FNewToolMenuDelegate::CreateStatic(&FillBindMenu));
	}

	/**
	 * Built when the submenu opens, so it always lists the indexes that exist now. Only the indexes are
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

		TArray<FAssetData> Indexes;
		FAssetRegistryModule::GetRegistry().GetAssetsByClass(UPrimitiveDataIndex::StaticClass()->GetClassPathName(), Indexes);
		Indexes.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });

		FToolMenuSection& Section = SubMenu->AddSection(TEXT("Indexes"), LOCTEXT("IndexesHeading", "Primitive Data Indexes"));

		if (Indexes.Num() == 0)
		{
			Section.AddMenuEntry(
				TEXT("NoIndexes"),
				LOCTEXT("NoIndexesLabel", "No indexes yet - Add > Materials > Primitive Data Index"),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
			return;
		}

		for (const FAssetData& IndexAsset : Indexes)
		{
			const UPrimitiveDataIndex* Index = Cast<UPrimitiveDataIndex>(IndexAsset.GetAsset());
			if (!Index)
			{
				continue;
			}

			// Ticked when every selected material is already bound here; choosing it then unbinds.
			bool bAllBound = Selected.Num() > 0;
			for (const FSoftObjectPath& Path : Selected)
			{
				if (!Index->BoundMaterials.ContainsByPredicate(
					[&Path](const TSoftObjectPtr<UObject>& Bound) { return Bound.ToSoftObjectPath() == Path; }))
				{
					bAllBound = false;
					break;
				}
			}

			const FSoftObjectPath IndexPath = IndexAsset.GetSoftObjectPath();
			Section.AddMenuEntry(
				IndexAsset.AssetName,
				FText::FromName(IndexAsset.AssetName),
				FText::Format(LOCTEXT("IndexEntryTooltip", "{0} parameter(s), {1} of {2} floats used."),
					FText::AsNumber(Index->Parameters.Num()), FText::AsNumber(Index->GetUsedFloats()),
					FText::AsNumber(UPrimitiveDataIndex::GetCapacity())),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([IndexPath, Selected, bAllBound]()
					{
						UPrimitiveDataIndex* Target = Cast<UPrimitiveDataIndex>(IndexPath.TryLoad());
						if (!Target)
						{
							return;
						}
						const FScopedTransaction Transaction(bAllBound
							? LOCTEXT("UnbindTransaction", "Unbind from Primitive Data Index")
							: LOCTEXT("BindTransaction", "Bind to Primitive Data Index"));
						for (const FSoftObjectPath& Path : Selected)
						{
							if (UObject* Material = Path.TryLoad())
							{
								if (bAllBound)
								{
									FPrimitiveDataIndexBinding::Unbind(*Target, Material);
								}
								else
								{
									FPrimitiveDataIndexBinding::Bind(*Target, Material);
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
	FDelegateHandle IndexChangedHandle;
	FDelegateHandle PreSaveHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	TMap<TWeakObjectPtr<UPrimitiveDataIndex>, FPendingIndex> PendingIndexes;
	TSet<FSoftObjectPath> PendingSaveChecks;
};

IMPLEMENT_MODULE(FHVPPrimitiveDataEditorModule, HVPPrimitiveDataEditor);

#undef LOCTEXT_NAMESPACE
