#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "IDetailCustomization.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "WebGraph/CodeAnimWebGraph.h"

class FBlueprintEditor;
class FUICommandList;
class SGraphEditor;
class UBlueprint;

/**
 * The Web Graph tab. Selecting in it shows the selection in the Blueprint editor's own Details panel -
 * a state's values, a transition, or with nothing selected the Web's own settings - through a
 * UCodeAnimWebGraphSelection whose Details are the Web's real class-default properties.
 */
class SCodeAnimWebGraphEditor : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SCodeAnimWebGraphEditor) {}
	SLATE_END_ARGS()

	/** Editor may be null (a test): the graph then works without pushing anything to a Details panel. */
	void Construct(const FArguments& InArgs, UBlueprint* InBlueprint, TSharedPtr<FBlueprintEditor> InEditor);
	virtual ~SCodeAnimWebGraphEditor() override;

	//~ FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override { RequestRebuild(true); }
	virtual void PostRedo(bool bSuccess) override { RequestRebuild(true); }

private:
	void RequestRebuild(bool bRefreshDetails);
	EActiveTimerReturnType DoRebuild(double InCurrentTime, float InDeltaTime);

	void OnSelectionChanged(const TSet<UObject*>& Selection);
	void OnNodeDoubleClicked(class UEdGraphNode* Node);
	void DeleteSelected();
	bool CanDeleteSelected() const;
	void RestoreSelection();

	/** Shows the selection in the Blueprint editor's Details panel. */
	void ShowSelection(bool bForceRefresh);
	bool IsShowingSelection() const;

	void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);
	void OnBlueprintCompiled(UBlueprint* Compiled);

	EVisibility GetEmptyHintVisibility() const;

	TWeakObjectPtr<UBlueprint> Blueprint;
	TWeakPtr<FBlueprintEditor> Editor;
	TStrongObjectPtr<UCodeAnimWebEdGraph> Graph;
	TStrongObjectPtr<UCodeAnimWebGraphSelection> Selection;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedPtr<FUICommandList> Commands;

	bool bRebuildPending = false;
	bool bRefreshDetailsPending = false;
	bool bRestoringSelection = false;
	FDelegateHandle PropertyChangedHandle;
	FDelegateHandle CompiledHandle;
	FDelegateHandle DataChangedHandle;
};

/** The Details of a UCodeAnimWebGraphSelection: the selected state's or transition's own row, from the Web. */
class FCodeAnimWebGraphSelectionDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FCodeAnimWebGraphSelectionDetails>(); }
	virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;
};
