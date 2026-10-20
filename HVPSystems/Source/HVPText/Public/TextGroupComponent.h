#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "TextRenderTypes.h"
#include "TextGroupComponent.generated.h"

class UTextRenderSubsystem;

/**
 * One letter group of the text subsystem, placed in the world: the group's style, its letters'
 * frame (this component) and, optionally, its text. The group is made when play begins and removed
 * when the component goes. Everything else about it is the subsystem's: get the group's letters
 * with GetGroupName.
 *
 * The letters follow the component. By default they are drawn with every other group's, so moving
 * the component re-sends its letters with the next batch: fine for text that moves now and then.
 * Text that moves every frame (on a prop, in a hand) can turn on bOwnInstances: the letters are
 * then drawn by instances attached here, and moving costs nothing, at the price of components of
 * its own instead of the shared ones.
 */
UCLASS(ClassGroup = (HVP), meta = (BlueprintSpawnableComponent))
class HVPTEXT_API UTextGroupComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** The group's name in the subsystem. Empty: made from the owner and component names. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text Group")
	FName GroupName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStyle, Category = "Text Group")
	FTextGroupStyle Style;

	/** Spelled when play begins, anchored at the component, facing its +X. Empty: no letters until
	    something adds them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetText, Category = "Text Group", meta = (MultiLine = "true"))
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Group|Layout")
	float TextScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Group|Layout")
	ETextAnchor Anchor = ETextAnchor::Center;

	/** Extra space after every character, em. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Group|Layout")
	float Tracking = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Group|Layout")
	float LineSpacing = 1.f;

	/** Draw this group with instances of its own, attached to this component, so moving it is free.
	    Off: drawn with every other group's letters, and re-sent when it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOwnInstances, Category = "Text Group")
	bool bOwnInstances = false;

	/** The name the group goes by in the subsystem. */
	UFUNCTION(BlueprintPure, Category = "Text Group")
	FName GetGroupName() const;

	/** Spells Text again with the layout above, keeping the letters' custom data where they stay. */
	UFUNCTION(BlueprintSetter)
	void SetText(const FString& NewText);

	UFUNCTION(BlueprintSetter)
	void SetStyle(const FTextGroupStyle& NewStyle);

	UFUNCTION(BlueprintSetter)
	void SetOwnInstances(bool bNewOwnInstances);

	/** Every letter of the group at once. */
	UFUNCTION(BlueprintCallable, Category = "Text Group")
	void SetCustomDataVector(int32 DataIndex, FVector Value);

	// Letter transforms relative to this component, by index in the group. The subsystem's group
	// functions take and give world transforms.

	UFUNCTION(BlueprintCallable, Category = "Text Group|Letters")
	void SetLetterRelativeTransform(int32 Index, const FTransform& Transform);

	/** Sets Transforms.Num() letters starting at FirstIndex, in one call. */
	UFUNCTION(BlueprintCallable, Category = "Text Group|Letters")
	void SetLetterRelativeTransforms(const TArray<FTransform>& Transforms, int32 FirstIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Text Group|Letters")
	FTransform GetLetterRelativeTransform(int32 Index) const;

	/** Every letter's, in group order. */
	UFUNCTION(BlueprintPure, Category = "Text Group|Letters")
	TArray<FTransform> GetLetterRelativeTransforms() const;

	UFUNCTION(BlueprintPure, Category = "Text Group")
	UTextRenderSubsystem* GetTextSubsystem() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Register();

	/** The name in use, fixed when the group is made. */
	FName ActiveName;
};
