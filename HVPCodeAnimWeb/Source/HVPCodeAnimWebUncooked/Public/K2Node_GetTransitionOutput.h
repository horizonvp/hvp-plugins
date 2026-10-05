#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_CodeAnimWebBase.h"

#include "K2Node_GetTransitionOutput.generated.h"

/**
 * Get From Output / Get To Output: inside a Web's transition or Custom Lerp graph, an output's value at
 * one end of the transition being played - "from" is wherever the Web was coming from (still live, if
 * that was itself moving), "to" is the target state's value. Typed as the output.
 *
 * Only offered in a Code Animation Web's own Blueprint. Outside a transition, both ends read the default.
 */
UCLASS()
class HVPCODEANIMWEBUNCOOKED_API UK2Node_GetTransitionOutput : public UK2Node
{
	GENERATED_BODY()

public:
	static const FName ValuePinName;

	/** The Web Blueprint this node sits in, as the class whose outputs it offers. */
	UClass* GetWebClass() const;

	//~ UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;

	//~ UK2Node
	virtual bool IsNodePure() const override { return true; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual FText GetKeywords() const override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void PostReconstructNode() override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	/** False: Get From Output. True: Get To Output. */
	UPROPERTY()
	bool bTo = false;

private:
	void CreateValuePin();
	void RefreshValuePin();

	UPROPERTY()
	FCodeAnimOutputSelection Output;
};
