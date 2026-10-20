#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "TextRenderTypes.h"
#include "TextRenderSubsystem.generated.h"

class ULetterFontAsset;
class UTextRenderSubsystem;
class UTextRenderTechnique;
class UTextRenderTechnique_GlyphMesh;
class UTextRenderTechnique_MSDF;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTextRenderEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTextLetterEvent, int32, Letter);

/** One letter group as the subsystem keeps it. */
USTRUCT()
struct FTextGroupState
{
	GENERATED_BODY()

	/** Letter slots, in group order. */
	UPROPERTY()
	TArray<int32> Letters;

	UPROPERTY()
	FTextGroupStyle Style;

	/** What the letters' frames are relative to; the world when unset. */
	UPROPERTY()
	TWeakObjectPtr<USceneComponent> Frame;

	bool bHasFrame = false;
	/** Drawn by instances of its own, attached to Frame, instead of the shared ones. */
	bool bOwnInstances = false;
	/** Index into UTextRenderSubsystem::Pools: 0 is the shared pool. */
	int32 Pool = 0;
	/** Frame's transform at the last flush, to notice it move (shared pool only). */
	FTransform FrameAtFlush;
};

/** A set of instanced components under one root: the shared pool, or a group's own. */
UCLASS(Transient)
class HVPTEXT_API UTextRenderPool : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UTextRenderTechnique_MSDF> MSDF;

	UPROPERTY()
	TObjectPtr<UTextRenderTechnique_GlyphMesh> GlyphMesh;

	UTextRenderTechnique* Get(ETextRenderTechnique Which) const;
	void Clear();
};

/** The actor the shared pool's components hang under, at the world origin. Spawned by the
    subsystem; its tick is the subsystem's flush. */
UCLASS(Transient, NotPlaceable, NotBlueprintable)
class HVPTEXT_API ATextRenderPoolActor : public AActor
{
	GENERATED_BODY()

public:
	ATextRenderPoolActor();
	virtual void Tick(float DeltaSeconds) override;

	TWeakObjectPtr<UTextRenderSubsystem> Subsystem;
};

/**
 * Every letter in the world, as data, drawn in one batch per frame. Setters only record a change;
 * the subsystem applies everything that changed late in the frame (TG_PostUpdateWork, after HandGrab
 * has moved anything held), one instance update per changed glyph, however many letters moved.
 *
 * Letters live in groups: a name of the caller's choosing, the group's own letter indices 0..N-1,
 * and a style (font, technique, cap height). Systems that share the subsystem only need a unique
 * group name. Every letter of every group that shares a font and technique is drawn by the same
 * instanced component per glyph, wherever it is, so there is never more than one component for a
 * glyph. Each letter also has a world-wide index ("Letter" below), stable while it lives: removing
 * it frees the slot for reuse, and OnLetterRemoved tells anyone holding the index to let go.
 *
 * A group's letter frames are relative to its frame: the world, or a scene component
 * (SetGroupFrame). When that component moves, the group's letters are re-sent with the next batch.
 * A group whose frame moves every frame can instead have instances of its own, attached to the
 * component (bOwnInstances), so moving it costs nothing at all: the price is a component per glyph
 * for that group alone. UTextGroupComponent wraps one group this way.
 *
 * The subsystem only draws. Moving, grabbing (UTextGrabBridge) and highlighting are the callers'.
 * Game worlds only.
 */
UCLASS()
class HVPTEXT_API UTextRenderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Letters were drawn anew or changed glyph: a rebuild, a character, a new letter, or a group's
	    font or technique. Indices still mean the same letters unless OnLettersReset fired first. */
	UPROPERTY(BlueprintAssignable, Category = "Text")
	FTextRenderEvent OnLettersRebuilt;

	/** Everything was removed (ClearAll): every letter index and group held elsewhere is meaningless. */
	UPROPERTY(BlueprintAssignable, Category = "Text")
	FTextRenderEvent OnLettersReset;

	/** A letter was removed. Its slot may be reused by the next addition, for any group. */
	UPROPERTY(BlueprintAssignable, Category = "Text")
	FTextLetterEvent OnLetterRemoved;

	// --- Groups: style and frame ----------------------------------------------------------------
	// A group exists from its first letter, style or frame until RemoveGroup. A group made by its
	// first letter takes the project's default style (Project Settings > Plugins > HVP Text).

	/** Font, technique and cap height together. A new font or technique re-homes the group's letters;
	    a new cap height only resizes them. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupStyle(FName Group, const FTextGroupStyle& Style);

	/** The group's style; the default style for a group that does not exist. */
	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	FTextGroupStyle GetGroupStyle(FName Group) const;

	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupFont(FName Group, ULetterFontAsset* Font);

	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupTechnique(FName Group, ETextRenderTechnique Technique);

	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupCapHeight(FName Group, float CapHeightCm);

	/** The font the group draws with: its own, else the Default Font. */
	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	ULetterFontAsset* GetGroupFont(FName Group) const;

	/**
	 * Makes the group's letter frames relative to Frame (None: the world). Existing frames keep their
	 * numbers, so the letters move with the change. While Frame moves, the group follows it.
	 * @param bOwnInstances  Draw the group with instances of its own, attached to Frame: moving it is
	 *                       then free, at the cost of the shared batching. Ignored without a Frame.
	 * A group whose frame is destroyed is removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupFrame(FName Group, USceneComponent* Frame, bool bOwnInstances = false);

	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	USceneComponent* GetGroupFrame(FName Group) const;

	/** The group frame's world transform; identity for a group in the world. */
	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	FTransform GetGroupFrameTransform(FName Group) const;

	// --- Groups: letters --------------------------------------------------------------------------
	// Indices below are the group's. Adding appends; removing shifts the group's later letters down
	// by one, like an array, and touches no other group.

	/** Adds a letter to the end of Group and returns its index in the group. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	int32 AddGroupLetter(FName Group, const FTextLetter& Letter);

	/** Adds letters to the end of Group and returns the group index of the first one. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	int32 AddGroupLetters(FName Group, const TArray<FTextLetter>& NewLetters);

	/** Makes Group exactly these letters, reusing its slots in order: re-spelling a word moves no
	    other letter and re-homes only letters whose character changes. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupLetters(FName Group, const TArray<FTextLetter>& NewLetters);

	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void RemoveGroupLetter(FName Group, int32 Index);

	/** Removes the group: its letters, style and frame. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void RemoveGroup(FName Group);

	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	bool HasGroup(FName Group) const { return Groups.Contains(Group); }

	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	TArray<FName> GetGroupNames() const;

	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	int32 GetGroupLetterCount(FName Group) const;

	/** A copy of one group letter; a default letter when the index is invalid. */
	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	FTextLetter GetGroupLetter(FName Group, int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	TArray<FTextLetter> GetGroupLetters(FName Group) const;

	/** The world-wide letter index of each group letter, in group order. */
	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	TArray<int32> GetGroupLetterIndices(FName Group) const;

	/** The world-wide letter index of one group letter; -1 when invalid. */
	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	int32 GetGroupLetterIndex(FName Group, int32 Index) const;

	/** World space: converted through the group's frame as it is now. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupLetterWorldTransform(FName Group, int32 Index, const FTransform& Transform);

	/** Sets the world transforms of Transforms.Num() group letters starting at FirstIndex. The
	    cheapest way to move many letters from Blueprint: one call, not one per letter. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupLetterWorldTransforms(FName Group, const TArray<FTransform>& Transforms, int32 FirstIndex = 0);

	/** World space, through the group's frame as it is now. */
	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	FTransform GetGroupLetterWorldTransform(FName Group, int32 Index) const;

	/** Every group letter's world transform, in group order. */
	UFUNCTION(BlueprintPure, Category = "Text|Groups")
	TArray<FTransform> GetGroupLetterWorldTransforms(FName Group) const;

	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupLetterVisible(FName Group, int32 Index, bool bNewVisible);

	/** Shows or hides every letter of the group. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupVisible(FName Group, bool bNewVisible);

	UFUNCTION(BlueprintCallable, Category = "Text|Groups")
	void SetGroupLetterCharacter(FName Group, int32 Index, const FString& Character);

	// One group letter's custom data, and every letter of a group at once. DataIndex is the first
	// float, as a material's PerInstanceCustomData node numbers it; a vector takes it and the next two.

	UFUNCTION(BlueprintCallable, Category = "Text|Groups|Custom Data")
	void SetGroupLetterCustomDataScalar(FName Group, int32 Index, int32 DataIndex, float Value);

	UFUNCTION(BlueprintCallable, Category = "Text|Groups|Custom Data")
	void SetGroupLetterCustomDataVector(FName Group, int32 Index, int32 DataIndex, FVector Value);

	/** The floats of Values whose bit is set in Mask (bit i is Values[i]) go to DataIndex + i; the
	    others keep their values. */
	UFUNCTION(BlueprintCallable, Category = "Text|Groups|Custom Data", meta = (AdvancedDisplay = "Mask"))
	void SetGroupLetterCustomDataMasked(FName Group, int32 Index, int32 DataIndex, const TArray<float>& Values, int64 Mask = -1);

	UFUNCTION(BlueprintCallable, Category = "Text|Groups|Custom Data")
	void SetGroupCustomDataScalar(FName Group, int32 DataIndex, float Value);

	UFUNCTION(BlueprintCallable, Category = "Text|Groups|Custom Data")
	void SetGroupCustomDataVector(FName Group, int32 DataIndex, FVector Value);

	// --- Text layout ------------------------------------------------------------------------------
	// Lines read along a letter frame's -Y (the reader's right, facing the letter's +X), with +Z up.
	// Spacing comes from the group's font: each glyph's advance plus the pair's kerning. Letters are
	// centred on their ink, so every distance here is between letter origins.

	/**
	 * Spells Text as the letters of Group, replacing whatever the group held (its slots are reused).
	 * One letter per visible character; spaces and line breaks (\n) only move the pen, so the group's
	 * indices follow the visible characters in reading order. Returns how many letters that is.
	 *
	 * @param Location  Group-frame point the Anchor goes to.
	 * @param Scale     Letter scale: capitals are the group's cap height * Scale tall.
	 * @param Rotation  Which way the text faces in the group frame. Zero: facing its +X, reading
	 *                  toward its -Y.
	 * @param Tracking  Extra space after every character, em (0.05 is a light letter-spacing).
	 * @param LineSpacing  Multiplies the font's line height.
	 */
	UFUNCTION(BlueprintCallable, Category = "Text|Layout", meta = (AdvancedDisplay = "Tracking,LineSpacing"))
	int32 SetGroupText(FName Group, const FString& Text, FVector Location, float Scale = 1.f,
		ETextAnchor Anchor = ETextAnchor::TopLeft, FRotator Rotation = FRotator::ZeroRotator,
		float Tracking = 0.f, float LineSpacing = 1.f);

	/** The size SetGroupText would give Text in Group's style, cm: X its widest line's ink, Y from the
	    first line's capital height to the last line's baseline. */
	UFUNCTION(BlueprintPure, Category = "Text|Layout", meta = (AdvancedDisplay = "Tracking,LineSpacing"))
	FVector2D MeasureText(FName Group, const FString& Text, float Scale = 1.f, float Tracking = 0.f, float LineSpacing = 1.f) const;

	/**
	 * How far Right's centre sits from Left's when Left is followed by Right on a line, cm, in Group's
	 * style at letters of Scale. Kerning included. For setting letters by hand: Right goes at Left's
	 * position plus this along the reading direction.
	 */
	UFUNCTION(BlueprintPure, Category = "Text|Layout", meta = (AdvancedDisplay = "Tracking"))
	float GetLetterSpacing(FName Group, const FString& Left, const FString& Right, float Scale = 1.f, float Tracking = 0.f) const;

	// --- Letters by world-wide index --------------------------------------------------------------

	/** The number of slots, removed ones included: letter indices run below this. */
	UFUNCTION(BlueprintPure, Category = "Text|Letters")
	int32 GetLetterCount() const { return Letters.Num(); }

	/** True for a live letter; false for a removed slot or an index out of range. */
	UFUNCTION(BlueprintPure, Category = "Text|Letters")
	bool IsValidLetter(int32 Letter) const { return Letters.IsValidIndex(Letter) && !SlotGroup[Letter].IsNone(); }

	/** The group a letter belongs to and its index there. False for an invalid letter. */
	UFUNCTION(BlueprintPure, Category = "Text|Letters")
	bool GetLetterGroup(int32 Letter, FName& OutGroup, int32& OutIndexInGroup) const;

	/** A copy of one letter; a default letter when the index is invalid. */
	UFUNCTION(BlueprintPure, Category = "Text|Letters")
	FTextLetter GetLetter(int32 Letter) const;

	/** Removes one letter from its group. Fires OnLetterRemoved. */
	UFUNCTION(BlueprintCallable, Category = "Text|Letters")
	void RemoveLetter(int32 Letter);

	UFUNCTION(BlueprintCallable, Category = "Text|Letters")
	void SetLetterCharacter(int32 Letter, const FString& Character);

	/** Group-frame relative. */
	UFUNCTION(BlueprintCallable, Category = "Text|Letters")
	void SetLetterTransform(int32 Letter, const FTransform& Transform);

	UFUNCTION(BlueprintCallable, Category = "Text|Letters")
	void SetLetterWorldTransform(int32 Letter, const FTransform& Transform);

	/** Group-frame relative location; rotation and scale are kept. */
	UFUNCTION(BlueprintCallable, Category = "Text|Letters")
	void SetLetterLocation(int32 Letter, const FVector& Location);

	UFUNCTION(BlueprintPure, Category = "Text|Letters")
	FTransform GetLetterTransform(int32 Letter) const;

	UFUNCTION(BlueprintPure, Category = "Text|Letters")
	FTransform GetLetterWorldTransform(int32 Letter) const;

	UFUNCTION(BlueprintCallable, Category = "Text|Letters")
	void SetLetterVisible(int32 Letter, bool bNewVisible);

	/** The letter's box in its own frame at scale 1, cm: the glyph mesh's bounds where the font has
	    one, else the MSDF quad, else a generic capital. */
	UFUNCTION(BlueprintPure, Category = "Text|Letters")
	FBox GetLetterBounds(int32 Letter) const;

	UFUNCTION(BlueprintCallable, Category = "Text|Letters|Custom Data")
	void SetLetterCustomDataScalar(int32 Letter, int32 DataIndex, float Value);

	UFUNCTION(BlueprintCallable, Category = "Text|Letters|Custom Data")
	void SetLetterCustomDataVector(int32 Letter, int32 DataIndex, FVector Value);

	/** Writes Values into the letter's floats from DataIndex on. */
	UFUNCTION(BlueprintCallable, Category = "Text|Letters|Custom Data")
	void SetLetterCustomData(int32 Letter, int32 DataIndex, const TArray<float>& Values);

	/** The floats of Values whose bit is set in Mask (bit i is Values[i]) go to DataIndex + i; the
	    others keep their values. */
	UFUNCTION(BlueprintCallable, Category = "Text|Letters|Custom Data", meta = (AdvancedDisplay = "Mask"))
	void SetLetterCustomDataMasked(int32 Letter, int32 DataIndex, const TArray<float>& Values, int64 Mask = -1);

	UFUNCTION(BlueprintPure, Category = "Text|Letters|Custom Data")
	float GetLetterCustomDataScalar(int32 Letter, int32 DataIndex) const;

	UFUNCTION(BlueprintPure, Category = "Text|Letters|Custom Data")
	FVector GetLetterCustomDataVector(int32 Letter, int32 DataIndex) const;

	/** Every custom data float of a letter; empty when the index is invalid. */
	UFUNCTION(BlueprintPure, Category = "Text|Letters|Custom Data")
	TArray<float> GetLetterCustomData(int32 Letter) const;

	// --- Everything -------------------------------------------------------------------------------

	/** Per instance custom data floats every letter carries. Starts as the project setting. */
	UFUNCTION(BlueprintPure, Category = "Text|Custom Data")
	int32 GetNumCustomDataFloats() const { return NumCustomDataFloats; }

	/** Resizes every letter's custom data (padding from the defaults) and rebuilds. */
	UFUNCTION(BlueprintCallable, Category = "Text|Custom Data")
	void SetNumCustomDataFloats(int32 NewNumFloats);

	/** Removes every letter and group. Fires OnLettersReset. */
	UFUNCTION(BlueprintCallable, Category = "Text")
	void ClearAll();

	/** Applies every pending change now instead of late in the frame. */
	UFUNCTION(BlueprintCallable, Category = "Text")
	void FlushChanges();

	/** Destroys and redraws every letter on the next flush. */
	UFUNCTION(BlueprintCallable, Category = "Text")
	void Rebuild();

	/** Instanced components drawing letters, every pool together. */
	UFUNCTION(BlueprintPure, Category = "Text")
	int32 GetRenderComponentCount() const;

	/** Set when some letters cannot be drawn as asked (a font without meshes...); empty otherwise. */
	UFUNCTION(BlueprintPure, Category = "Text")
	FString GetRenderNote() const;

	// --- UWorldSubsystem ---
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	friend class FTextPoolLetters;

	FTextGroupState& FindOrAddGroup(FName Group);
	FTextGroupStyle DefaultStyle() const;
	const ULetterFontAsset* ResolveFont(const FTextGroupStyle& Style) const;
	/** Centimetres per em at letter scale 1, in this style. */
	float GetEmCm(const FTextGroupStyle& Style) const;
	const FTextGroupState* GroupOf(int32 Letter) const;
	FTransform FrameTransform(const FTextGroupState& Group) const;
	/** Re-homes every letter of the group with the next flush (font, technique or pool changed). */
	void MarkGroupGlyphs(const FTextGroupState& Group);
	void MarkGroupTransforms(const FTextGroupState& Group);

	/** Spawns the shared pool's actor if needed. */
	void EnsurePoolActor();
	/** The pool a group draws into, made or released as its frame says. */
	void UpdateGroupPool(FTextGroupState& Group);
	int32 AllocatePool(USceneComponent* Root);
	void ReleasePool(int32 Pool);
	UTextRenderPool* MakePool(USceneComponent* Root);
	/** Per letter: whether Which, in pool Pool, draws it. */
	TArray<bool> GetMine(int32 Pool, ETextRenderTechnique Which) const;
	/** Frames gone or moved, before a flush. */
	void TrackFrames();

	/** Makes a letter's CustomData exactly NumCustomDataFloats long, padding from the defaults. */
	void NormalizeCustomData(FTextLetter& Letter) const;
	/** Writes Num floats into a live letter's custom data from DataIndex; floats past the end are dropped. */
	void WriteCustomData(int32 Letter, int32 DataIndex, const float* Values, int32 Num);
	/** Puts a letter in a free slot (preferring one that drew the same character) or a new one. */
	int32 AllocateSlot(const FTextLetter& Letter, FName Group);
	/** Writes a letter into a live slot, re-homing only if the character changed. */
	void WriteSlot(int32 Slot, const FTextLetter& Letter);
	/** Hides and frees a slot. Does not touch the group's list; the caller does. */
	void FreeSlot(int32 Slot);
	/** The slot of a group letter; INDEX_NONE when invalid. */
	int32 GroupSlot(FName Group, int32 Index) const;

	/** One visible character of a laid-out text, em: X along the line, Y up, from the block's anchor. */
	struct FLaidOutLetter
	{
		TCHAR Char;
		FVector2D Position;
	};
	/** Lays Text out in em, anchored, and returns the block's size in em. */
	static FVector2D LayoutText(const ULetterFontAsset* Font, const FString& Text, ETextAnchor Anchor, float Tracking,
		float LineSpacing, TArray<FLaidOutLetter>* OutLetters);

	void MarkTransformDirty(int32 Letter);
	void MarkCustomDataDirty(int32 Letter);

	/** Every letter slot. A free slot keeps its character (so reusing it for the same one is free),
	    is hidden and belongs to no group. */
	UPROPERTY()
	TArray<FTextLetter> Letters;

	/** Per slot: its group, NAME_None while free. */
	TArray<FName> SlotGroup;
	TArray<int32> FreeSlots;

	UPROPERTY()
	TMap<FName, FTextGroupState> Groups;

	/** 0 is the shared pool, under PoolActor; the rest are groups' own (null once released). */
	UPROPERTY()
	TArray<TObjectPtr<UTextRenderPool>> Pools;
	TArray<int32> FreePools;

	UPROPERTY()
	TObjectPtr<ATextRenderPoolActor> PoolActor;

	/** Loaded from the settings at start. */
	UPROPERTY()
	TObjectPtr<ULetterFontAsset> DefaultFont;

	UPROPERTY()
	TObjectPtr<UStaticMesh> QuadMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlyphMeshMaterial;

	int32 NumCustomDataFloats = 3;
	TArray<float> DefaultCustomData;
	int32 TranslucentSortPriority = 10;

	/** Letters whose glyph, font, technique or pool changed, or that are new, since the last flush:
	    re-homed one by one instead of rebuilding everything. */
	TSet<int32> DirtyGlyphs;
	bool bRebuildPending = false;
	bool bTransformsDirty = false;
	bool bCustomDataDirty = false;
	/** Per letter: changed since the last flush. */
	TBitArray<> TransformDirty;
	TBitArray<> CustomDataDirty;
	bool bWarnedCustomDataRange = false;
	bool bFlushing = false;
};
