#pragma once

#include "CoreMinimal.h"
#include "K2Node_CodeAnimWebBase.h"

#include "K2Node_WebState.generated.h"

/**
 * Set Web State: head a Web for a state, picked from - or wired in as - the Web's own States enum.
 * Compiles to UCodeAnimationWeb::SetState.
 */
UCLASS()
class HVPCODEANIMWEBUNCOOKED_API UK2Node_SetWebState : public UK2Node_CodeAnimWebBase
{
	GENERATED_BODY()

public:
	static const FName StatePinName;
	static const FName InstantPinName;
	static const FName SuccessPinName;

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetKeywords() const override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

protected:
	virtual void OnWebClassChanged() override;

	/** The UCodeAnimationWeb function this compiles to: (NewState, bInstant) -> bool. */
	virtual FName GetTargetFunctionName() const;

private:
	UPROPERTY()
	TObjectPtr<UEnum> StateEnum;
};

/**
 * Pause Web In State: head for a state, refuse any other until released, and freeze on arrival.
 * Compiles to UCodeAnimationWeb::PauseInState. Set Paused (false) releases it.
 */
UCLASS()
class HVPCODEANIMWEBUNCOOKED_API UK2Node_PauseWebInState : public UK2Node_SetWebState
{
	GENERATED_BODY()

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetKeywords() const override;

protected:
	virtual FName GetTargetFunctionName() const override;
};

/** Get Web State: the state a Web is headed for (or sat in), as the Web's own States enum. */
UCLASS()
class HVPCODEANIMWEBUNCOOKED_API UK2Node_GetWebState : public UK2Node_CodeAnimWebBase
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetKeywords() const override;
	virtual bool IsNodePure() const override { return true; }
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

protected:
	virtual void OnWebClassChanged() override;

private:
	UPROPERTY()
	TObjectPtr<UEnum> StateEnum;
};

namespace CodeAnimWebNodes
{
	/** The States enum of a Web class, or null. */
	HVPCODEANIMWEBUNCOOKED_API UEnum* GetStateEnum(const UClass* WebClass);
}
