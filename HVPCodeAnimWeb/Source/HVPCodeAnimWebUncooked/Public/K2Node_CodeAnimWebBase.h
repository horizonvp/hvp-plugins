#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"

#include "K2Node_CodeAnimWebBase.generated.h"

class UCodeAnimationWeb;

/**
 * Which output a node reads, held by the variable's GUID as well as its name so a renamed output keeps
 * its nodes, with the pin type it was last resolved to so pins can be built before the class is loaded.
 */
USTRUCT()
struct HVPCODEANIMWEBUNCOOKED_API FCodeAnimOutputSelection
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FGuid Guid;

	UPROPERTY()
	FEdGraphPinType ValueType;

	UPROPERTY()
	bool bHasValue = false;
};

namespace CodeAnimWebNodes
{
	/** The Animation Outputs of a Web class, in variable order. */
	HVPCODEANIMWEBUNCOOKED_API void GetOutputs(const UClass* WebClass, TArray<const FProperty*>& OutOutputs);

	/** The selected output's property, following a rename by GUID (and updating the name). Null if gone. */
	HVPCODEANIMWEBUNCOOKED_API const FProperty* Resolve(const UClass* WebClass, FCodeAnimOutputSelection& Selection);

	/** Re-resolves and refreshes ValueType / bHasValue. True if either changed. */
	HVPCODEANIMWEBUNCOOKED_API bool RefreshType(const UClass* WebClass, FCodeAnimOutputSelection& Selection);

	/** For the Output dropdown: the Web class whose outputs a node offers, and whether it offers "any output". */
	HVPCODEANIMWEBUNCOOKED_API UClass* GetOutputSourceClass(const UEdGraphNode* Node, bool& bOutAllowsAny);

	/** The pin every output-picking node names its dropdown. */
	HVPCODEANIMWEBUNCOOKED_API extern const FName OutputPinName;
}

/**
 * A node acting on a Web wired into its Web pin. The node types itself from what is wired in - the
 * pins that carry the Web out, the outputs it offers, the States enum - so a Web Blueprint's own
 * variables, states and outputs appear on it without a cast.
 *
 * Left unwired, the Web pin means self, which only makes sense inside a Web's own Blueprint.
 */
UCLASS(Abstract)
class HVPCODEANIMWEBUNCOOKED_API UK2Node_CodeAnimWebBase : public UK2Node
{
	GENERATED_BODY()

public:
	/** The Web pin is a self pin, as on engine function nodes, shown as "Web". */
	static const FName WebPinName;

	/** The Web class wired in, or UCodeAnimationWeb itself when nothing more specific is known. */
	UClass* GetWebClass() const;

	//~ UEdGraphNode
	virtual void NotifyPinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PostReconstructNode() override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;

	//~ UK2Node
	virtual bool HasExternalDependencies(TArray<UStruct*>* OptionalOutput) const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void PreloadRequiredAssets() override;

protected:
	UEdGraphPin* CreateWebPin();

	/** Re-reads the class from the Web pin's link; retypes the Web pins and calls OnWebClassChanged if it moved. */
	void RefreshWebClass();

	/** Pins typed as the Web class, retyped with it. The Web input by default. */
	virtual void GetWebTypedPins(TArray<UEdGraphPin*>& OutPins) const;

	virtual void OnWebClassChanged() {}

	/** Error and false when the Web pin is unwired outside a Web's own Blueprint. Call before moving links. */
	bool CheckWebSource(FKismetCompilerContext& CompilerContext);

	/** Re-lays the node after a pin was added or removed outside reconstruction. */
	void NotifyChanged();

	UPROPERTY()
	TObjectPtr<UClass> WebClass;

private:
	UClass* ResolveWebClassFromPin() const;
};
