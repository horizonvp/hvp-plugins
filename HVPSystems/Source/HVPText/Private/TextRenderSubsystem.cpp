#include "TextRenderSubsystem.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "LetterFontAsset.h"
#include "Materials/MaterialInterface.h"
#include "TextRenderSettings.h"
#include "TextRenderTechnique.h"

DEFINE_LOG_CATEGORY_STATIC(LogHVPText, Log, All);

/** One pool's view of the letters: their fonts, and their frames in the pool root's space. */
class FTextPoolLetters final : public ITextLetterSource
{
public:
	FTextPoolLetters(const UTextRenderSubsystem& InText, int32 InPool) : Text(InText), Pool(InPool) {}

	virtual int32 Num() const override { return Text.Letters.Num(); }
	virtual TCHAR GetChar(int32 Letter) const override { return Text.Letters[Letter].GetChar(); }

	virtual const ULetterFontAsset* GetFont(int32 Letter) const override
	{
		const FTextGroupState* Group = Text.GroupOf(Letter);
		return Group ? Text.ResolveFont(Group->Style) : nullptr;
	}

	virtual FTransform GetDrawFrame(int32 Letter) const override
	{
		const FTextGroupState* Group = Text.GroupOf(Letter);
		const FTextLetter& Data = Text.Letters[Letter];
		if (!Group || !Data.bVisible)
		{
			return FTransform(FQuat::Identity, Data.Transform.GetLocation(), FVector::ZeroVector);
		}
		// The glyph at 1 em = 1 cm, scaled to the group's em, placed by the letter frame, then (in the
		// shared pool, whose root is the world origin) carried by the group's frame. A group's own pool
		// hangs under its frame already.
		const FTransform Em(FQuat::Identity, FVector::ZeroVector, FVector(Text.GetEmCm(Group->Style)));
		const FTransform InPool = Em * Data.Transform;
		return Pool == 0 && Group->bHasFrame ? InPool * Text.FrameTransform(*Group) : InPool;
	}

	virtual const TArray<float>& GetCustomData(int32 Letter) const override { return Text.Letters[Letter].CustomData; }

private:
	const UTextRenderSubsystem& Text;
	int32 Pool;
};

// --- Pools -----------------------------------------------------------------------------------

UTextRenderTechnique* UTextRenderPool::Get(ETextRenderTechnique Which) const
{
	switch (Which)
	{
	case ETextRenderTechnique::MSDF:      return MSDF;
	case ETextRenderTechnique::GlyphMesh: return GlyphMesh;
	default:                              return nullptr;
	}
}

void UTextRenderPool::Clear()
{
	MSDF->Clear();
	GlyphMesh->Clear();
}

ATextRenderPoolActor::ATextRenderPoolActor()
{
	// Late in the frame: after gameplay, and after HandGrab (which ticks after TG_PostPhysics) has
	// moved anything held, so every change made this frame is drawn this frame.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ATextRenderPoolActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (UTextRenderSubsystem* Text = Subsystem.Get())
	{
		Text->FlushChanges();
	}
}

UTextRenderPool* UTextRenderSubsystem::MakePool(USceneComponent* Root)
{
	UTextRenderPool* Pool = NewObject<UTextRenderPool>(this);
	Pool->Root = Root;
	Pool->MSDF = NewObject<UTextRenderTechnique_MSDF>(Pool);
	Pool->MSDF->QuadMesh = QuadMesh;
	Pool->MSDF->TranslucentSortPriority = TranslucentSortPriority;
	Pool->GlyphMesh = NewObject<UTextRenderTechnique_GlyphMesh>(Pool);
	Pool->GlyphMesh->Material = GlyphMeshMaterial;
	return Pool;
}

int32 UTextRenderSubsystem::AllocatePool(USceneComponent* Root)
{
	UTextRenderPool* Pool = MakePool(Root);
	if (!FreePools.IsEmpty())
	{
		const int32 Index = FreePools.Pop();
		Pools[Index] = Pool;
		return Index;
	}
	return Pools.Add(Pool);
}

void UTextRenderSubsystem::ReleasePool(int32 Pool)
{
	if (Pool > 0 && Pools.IsValidIndex(Pool) && Pools[Pool])
	{
		Pools[Pool]->Clear();
		Pools[Pool] = nullptr;
		FreePools.Add(Pool);
	}
}

void UTextRenderSubsystem::EnsurePoolActor()
{
	if (IsValid(PoolActor))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Name = MakeUniqueObjectName(World->PersistentLevel, ATextRenderPoolActor::StaticClass(), TEXT("HVPTextPool"));
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PoolActor = World->SpawnActor<ATextRenderPoolActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!PoolActor)
	{
		return;
	}
	PoolActor->Subsystem = this;
	// A new root: whatever the shared pool drew under an old actor is gone with it.
	Pools[0]->Clear();
	Pools[0]->Root = PoolActor->GetRootComponent();
}

void UTextRenderSubsystem::UpdateGroupPool(FTextGroupState& Group)
{
	USceneComponent* Frame = Group.Frame.Get();
	const bool bWantOwn = Group.bOwnInstances && Frame;
	const bool bHasOwn = Group.Pool != 0;
	if (bHasOwn && (!bWantOwn || Pools[Group.Pool]->Root.Get() != Frame))
	{
		ReleasePool(Group.Pool);
		Group.Pool = 0;
		MarkGroupGlyphs(Group);
	}
	if (bWantOwn && Group.Pool == 0)
	{
		Group.Pool = AllocatePool(Frame);
		MarkGroupGlyphs(Group);
	}
}

TArray<bool> UTextRenderSubsystem::GetMine(int32 Pool, ETextRenderTechnique Which) const
{
	TArray<bool> Mine;
	Mine.SetNumUninitialized(Letters.Num());
	for (int32 Letter = 0; Letter < Letters.Num(); ++Letter)
	{
		const FTextGroupState* Group = GroupOf(Letter);
		Mine[Letter] = Group && Group->Pool == Pool && Group->Style.Technique == Which;
	}
	return Mine;
}

// --- Lifetime --------------------------------------------------------------------------------

bool UTextRenderSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTextRenderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UTextRenderSettings* Settings = GetDefault<UTextRenderSettings>();
	DefaultFont = Cast<ULetterFontAsset>(Settings->DefaultFont.TryLoad());
	QuadMesh = Cast<UStaticMesh>(Settings->QuadMesh.TryLoad());
	GlyphMeshMaterial = Cast<UMaterialInterface>(Settings->GlyphMeshMaterial.TryLoad());
	NumCustomDataFloats = FMath::Clamp(Settings->NumCustomDataFloats, 0, 64);
	DefaultCustomData = Settings->DefaultCustomData;
	TranslucentSortPriority = Settings->TranslucentSortPriority;
	Pools.Reset();
	Pools.Add(MakePool(nullptr));
	bRebuildPending = true;
}

void UTextRenderSubsystem::Deinitialize()
{
	for (UTextRenderPool* Pool : Pools)
	{
		if (Pool)
		{
			Pool->Clear();
		}
	}
	// The pool actor is transient and goes with the world, which is what deinitializes us.
	PoolActor = nullptr;
	Super::Deinitialize();
}

// --- Groups ----------------------------------------------------------------------------------

FTextGroupStyle UTextRenderSubsystem::DefaultStyle() const
{
	const UTextRenderSettings* Settings = GetDefault<UTextRenderSettings>();
	FTextGroupStyle Style;
	Style.Technique = Settings->DefaultTechnique;
	Style.CapHeightCm = FMath::Max(Settings->DefaultCapHeightCm, 0.1f);
	return Style;  // Font left empty: the Default Font
}

FTextGroupState& UTextRenderSubsystem::FindOrAddGroup(FName Group)
{
	if (FTextGroupState* Found = Groups.Find(Group))
	{
		return *Found;
	}
	FTextGroupState& Added = Groups.Add(Group);
	Added.Style = DefaultStyle();
	return Added;
}

const ULetterFontAsset* UTextRenderSubsystem::ResolveFont(const FTextGroupStyle& Style) const
{
	return Style.Font ? Style.Font.Get() : DefaultFont.Get();
}

float UTextRenderSubsystem::GetEmCm(const FTextGroupStyle& Style) const
{
	const ULetterFontAsset* Font = ResolveFont(Style);
	const float Cap = FMath::Max(Style.CapHeightCm, 0.1f);
	return Font && Font->CapHeight > 0.f ? Cap / Font->CapHeight : Cap / 0.7f;
}

const FTextGroupState* UTextRenderSubsystem::GroupOf(int32 Letter) const
{
	return SlotGroup.IsValidIndex(Letter) && !SlotGroup[Letter].IsNone() ? Groups.Find(SlotGroup[Letter]) : nullptr;
}

FTransform UTextRenderSubsystem::FrameTransform(const FTextGroupState& Group) const
{
	const USceneComponent* Frame = Group.bHasFrame ? Group.Frame.Get() : nullptr;
	return Frame ? Frame->GetComponentTransform() : FTransform::Identity;
}

void UTextRenderSubsystem::MarkGroupGlyphs(const FTextGroupState& Group)
{
	DirtyGlyphs.Append(Group.Letters);
}

void UTextRenderSubsystem::MarkGroupTransforms(const FTextGroupState& Group)
{
	for (int32 Slot : Group.Letters)
	{
		MarkTransformDirty(Slot);
	}
}

void UTextRenderSubsystem::SetGroupStyle(FName Group, const FTextGroupStyle& Style)
{
	if (Group.IsNone())
	{
		return;
	}
	FTextGroupState& State = FindOrAddGroup(Group);
	const FTextGroupStyle Old = State.Style;
	State.Style = Style;
	State.Style.CapHeightCm = FMath::Max(Style.CapHeightCm, 0.1f);
	if (ResolveFont(Old) != ResolveFont(State.Style) || Old.Technique != State.Style.Technique)
	{
		MarkGroupGlyphs(State);
	}
	if (GetEmCm(Old) != GetEmCm(State.Style))
	{
		MarkGroupTransforms(State);
	}
}

FTextGroupStyle UTextRenderSubsystem::GetGroupStyle(FName Group) const
{
	const FTextGroupState* Found = Groups.Find(Group);
	return Found ? Found->Style : DefaultStyle();
}

void UTextRenderSubsystem::SetGroupFont(FName Group, ULetterFontAsset* Font)
{
	FTextGroupStyle Style = GetGroupStyle(Group);
	Style.Font = Font;
	SetGroupStyle(Group, Style);
}

void UTextRenderSubsystem::SetGroupTechnique(FName Group, ETextRenderTechnique Technique)
{
	FTextGroupStyle Style = GetGroupStyle(Group);
	Style.Technique = Technique;
	SetGroupStyle(Group, Style);
}

void UTextRenderSubsystem::SetGroupCapHeight(FName Group, float CapHeightCm)
{
	FTextGroupStyle Style = GetGroupStyle(Group);
	Style.CapHeightCm = CapHeightCm;
	SetGroupStyle(Group, Style);
}

ULetterFontAsset* UTextRenderSubsystem::GetGroupFont(FName Group) const
{
	return const_cast<ULetterFontAsset*>(ResolveFont(GetGroupStyle(Group)));
}

void UTextRenderSubsystem::SetGroupFrame(FName Group, USceneComponent* Frame, bool bOwnInstances)
{
	if (Group.IsNone())
	{
		return;
	}
	FTextGroupState& State = FindOrAddGroup(Group);
	State.Frame = Frame;
	State.bHasFrame = Frame != nullptr;
	State.bOwnInstances = Frame && bOwnInstances;
	State.FrameAtFlush = Frame ? Frame->GetComponentTransform() : FTransform::Identity;
	UpdateGroupPool(State);
	MarkGroupTransforms(State);
}

USceneComponent* UTextRenderSubsystem::GetGroupFrame(FName Group) const
{
	const FTextGroupState* Found = Groups.Find(Group);
	return Found ? Found->Frame.Get() : nullptr;
}

FTransform UTextRenderSubsystem::GetGroupFrameTransform(FName Group) const
{
	const FTextGroupState* Found = Groups.Find(Group);
	return Found ? FrameTransform(*Found) : FTransform::Identity;
}

void UTextRenderSubsystem::TrackFrames()
{
	TArray<FName> Lost;
	for (TPair<FName, FTextGroupState>& Pair : Groups)
	{
		FTextGroupState& Group = Pair.Value;
		if (!Group.bHasFrame)
		{
			continue;
		}
		const USceneComponent* Frame = Group.Frame.Get();
		if (!Frame)
		{
			Lost.Add(Pair.Key);
			continue;
		}
		// The shared pool draws in world space: a moved frame means re-sending its letters. A group's
		// own pool hangs under the frame and moves with it for free.
		if (Group.Pool == 0)
		{
			const FTransform Now = Frame->GetComponentTransform();
			if (!Now.Equals(Group.FrameAtFlush, 1.e-4))
			{
				Group.FrameAtFlush = Now;
				MarkGroupTransforms(Group);
			}
		}
	}
	for (FName Group : Lost)
	{
		UE_LOG(LogHVPText, Log, TEXT("Group %s: its frame was destroyed, so the group was removed"), *Group.ToString());
		RemoveGroup(Group);
	}
}

// --- Group letters ---------------------------------------------------------------------------

int32 UTextRenderSubsystem::GroupSlot(FName Group, int32 Index) const
{
	const FTextGroupState* Found = Groups.Find(Group);
	return Found && Found->Letters.IsValidIndex(Index) ? Found->Letters[Index] : INDEX_NONE;
}

int32 UTextRenderSubsystem::AddGroupLetter(FName Group, const FTextLetter& Letter)
{
	if (Group.IsNone())
	{
		UE_LOG(LogHVPText, Warning, TEXT("AddGroupLetter needs a group name"));
		return INDEX_NONE;
	}
	FindOrAddGroup(Group);
	const int32 Slot = AllocateSlot(Letter, Group);
	return Groups[Group].Letters.Add(Slot);
}

int32 UTextRenderSubsystem::AddGroupLetters(FName Group, const TArray<FTextLetter>& NewLetters)
{
	const int32 First = GetGroupLetterCount(Group);
	for (const FTextLetter& Letter : NewLetters)
	{
		AddGroupLetter(Group, Letter);
	}
	return First;
}

void UTextRenderSubsystem::SetGroupLetters(FName Group, const TArray<FTextLetter>& NewLetters)
{
	if (Group.IsNone())
	{
		UE_LOG(LogHVPText, Warning, TEXT("SetGroupLetters needs a group name"));
		return;
	}
	TArray<int32> Slots = FindOrAddGroup(Group).Letters;
	// Reuse the group's slots in order, free what is left over, add what is missing.
	const int32 Keep = FMath::Min(Slots.Num(), NewLetters.Num());
	for (int32 Index = 0; Index < Keep; ++Index)
	{
		WriteSlot(Slots[Index], NewLetters[Index]);
	}
	const TArray<int32> Dropped(Slots.GetData() + Keep, Slots.Num() - Keep);
	Slots.SetNum(Keep);
	Groups[Group].Letters = Slots;
	for (int32 Slot : Dropped)
	{
		FreeSlot(Slot);  // after the group is updated, so OnLetterRemoved listeners see it current
	}
	for (int32 Index = Keep; Index < NewLetters.Num(); ++Index)
	{
		AddGroupLetter(Group, NewLetters[Index]);
	}
}

void UTextRenderSubsystem::RemoveGroupLetter(FName Group, int32 Index)
{
	const int32 Slot = GroupSlot(Group, Index);
	if (Slot != INDEX_NONE)
	{
		Groups[Group].Letters.RemoveAt(Index);
		FreeSlot(Slot);
	}
}

void UTextRenderSubsystem::RemoveGroup(FName Group)
{
	FTextGroupState Removed;
	if (Groups.RemoveAndCopyValue(Group, Removed))
	{
		ReleasePool(Removed.Pool);
		for (int32 Slot : Removed.Letters)
		{
			FreeSlot(Slot);
		}
	}
}

TArray<FName> UTextRenderSubsystem::GetGroupNames() const
{
	TArray<FName> Names;
	Groups.GetKeys(Names);
	return Names;
}

int32 UTextRenderSubsystem::GetGroupLetterCount(FName Group) const
{
	const FTextGroupState* Found = Groups.Find(Group);
	return Found ? Found->Letters.Num() : 0;
}

FTextLetter UTextRenderSubsystem::GetGroupLetter(FName Group, int32 Index) const
{
	return GetLetter(GroupSlot(Group, Index));
}

TArray<FTextLetter> UTextRenderSubsystem::GetGroupLetters(FName Group) const
{
	TArray<FTextLetter> Result;
	if (const FTextGroupState* Found = Groups.Find(Group))
	{
		Result.Reserve(Found->Letters.Num());
		for (int32 Slot : Found->Letters)
		{
			Result.Add(Letters[Slot]);
		}
	}
	return Result;
}

TArray<int32> UTextRenderSubsystem::GetGroupLetterIndices(FName Group) const
{
	const FTextGroupState* Found = Groups.Find(Group);
	return Found ? Found->Letters : TArray<int32>();
}

int32 UTextRenderSubsystem::GetGroupLetterIndex(FName Group, int32 Index) const
{
	return GroupSlot(Group, Index);
}

void UTextRenderSubsystem::SetGroupLetterTransform(FName Group, int32 Index, const FTransform& Transform)
{
	SetLetterTransform(GroupSlot(Group, Index), Transform);
}

void UTextRenderSubsystem::SetGroupLetterTransforms(FName Group, const TArray<FTransform>& Transforms, int32 FirstIndex)
{
	const FTextGroupState* Found = Groups.Find(Group);
	if (!Found)
	{
		return;
	}
	const int32 First = FMath::Max(FirstIndex, 0);
	const int32 Num = FMath::Min(Transforms.Num(), Found->Letters.Num() - First);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const int32 Slot = Found->Letters[First + Index];
		Letters[Slot].Transform = Transforms[Index];
		MarkTransformDirty(Slot);
	}
}

FTransform UTextRenderSubsystem::GetGroupLetterTransform(FName Group, int32 Index) const
{
	return GetLetterTransform(GroupSlot(Group, Index));
}

TArray<FTransform> UTextRenderSubsystem::GetGroupLetterTransforms(FName Group) const
{
	TArray<FTransform> Result;
	if (const FTextGroupState* Found = Groups.Find(Group))
	{
		Result.Reserve(Found->Letters.Num());
		for (int32 Slot : Found->Letters)
		{
			Result.Add(Letters[Slot].Transform);
		}
	}
	return Result;
}

void UTextRenderSubsystem::SetGroupLetterVisible(FName Group, int32 Index, bool bNewVisible)
{
	SetLetterVisible(GroupSlot(Group, Index), bNewVisible);
}

void UTextRenderSubsystem::SetGroupVisible(FName Group, bool bNewVisible)
{
	for (int32 Slot : GetGroupLetterIndices(Group))
	{
		SetLetterVisible(Slot, bNewVisible);
	}
}

void UTextRenderSubsystem::SetGroupLetterCharacter(FName Group, int32 Index, const FString& Character)
{
	SetLetterCharacter(GroupSlot(Group, Index), Character);
}

void UTextRenderSubsystem::SetGroupLetterCustomDataScalar(FName Group, int32 Index, int32 DataIndex, float Value)
{
	SetLetterCustomDataScalar(GroupSlot(Group, Index), DataIndex, Value);
}

void UTextRenderSubsystem::SetGroupLetterCustomDataVector(FName Group, int32 Index, int32 DataIndex, FVector Value)
{
	SetLetterCustomDataVector(GroupSlot(Group, Index), DataIndex, Value);
}

void UTextRenderSubsystem::SetGroupLetterCustomDataMasked(FName Group, int32 Index, int32 DataIndex, const TArray<float>& Values, int64 Mask)
{
	SetLetterCustomDataMasked(GroupSlot(Group, Index), DataIndex, Values, Mask);
}

void UTextRenderSubsystem::SetGroupCustomDataScalar(FName Group, int32 DataIndex, float Value)
{
	for (int32 Slot : GetGroupLetterIndices(Group))
	{
		SetLetterCustomDataScalar(Slot, DataIndex, Value);
	}
}

void UTextRenderSubsystem::SetGroupCustomDataVector(FName Group, int32 DataIndex, FVector Value)
{
	for (int32 Slot : GetGroupLetterIndices(Group))
	{
		SetLetterCustomDataVector(Slot, DataIndex, Value);
	}
}

// --- Slots -----------------------------------------------------------------------------------

int32 UTextRenderSubsystem::AllocateSlot(const FTextLetter& Letter, FName Group)
{
	// A freed slot that drew the same character is reused without re-homing (when the font and
	// technique match too); any other slot, reused or new, is re-homed.
	const TCHAR Char = Letter.GetChar();
	int32 FreeIndex = FreeSlots.IndexOfByPredicate([this, Char](int32 Slot) { return Letters[Slot].GetChar() == Char; });
	if (FreeIndex == INDEX_NONE && !FreeSlots.IsEmpty())
	{
		FreeIndex = FreeSlots.Num() - 1;
	}

	int32 Slot = INDEX_NONE;
	if (FreeIndex != INDEX_NONE)
	{
		Slot = FreeSlots[FreeIndex];
		FreeSlots.RemoveAtSwap(FreeIndex);
		SlotGroup[Slot] = Group;
		WriteSlot(Slot, Letter);
	}
	else
	{
		Slot = Letters.Add(Letter);
		NormalizeCustomData(Letters[Slot]);
		SlotGroup.Add(Group);
		MarkTransformDirty(Slot);
		MarkCustomDataDirty(Slot);
	}
	DirtyGlyphs.Add(Slot);  // a new letter, or one whose group (so font, technique, pool) may differ
	return Slot;
}

void UTextRenderSubsystem::WriteSlot(int32 Slot, const FTextLetter& Letter)
{
	if (Letters[Slot].GetChar() != Letter.GetChar())
	{
		DirtyGlyphs.Add(Slot);
	}
	Letters[Slot] = Letter;
	NormalizeCustomData(Letters[Slot]);
	MarkTransformDirty(Slot);
	MarkCustomDataDirty(Slot);
}

void UTextRenderSubsystem::FreeSlot(int32 Slot)
{
	if (!IsValidLetter(Slot))
	{
		return;
	}
	// Keeps its instance, hidden, and its character, so reusing it for the same glyph is free.
	SlotGroup[Slot] = NAME_None;
	Letters[Slot].bVisible = false;
	FreeSlots.Add(Slot);
	MarkTransformDirty(Slot);
	OnLetterRemoved.Broadcast(Slot);
}

void UTextRenderSubsystem::MarkTransformDirty(int32 Letter)
{
	if (Letter >= TransformDirty.Num())
	{
		TransformDirty.SetNum(Letters.Num(), false);
	}
	TransformDirty[Letter] = true;
	bTransformsDirty = true;
}

void UTextRenderSubsystem::MarkCustomDataDirty(int32 Letter)
{
	if (Letter >= CustomDataDirty.Num())
	{
		CustomDataDirty.SetNum(Letters.Num(), false);
	}
	CustomDataDirty[Letter] = true;
	bCustomDataDirty = true;
}

// --- Letters ---------------------------------------------------------------------------------

bool UTextRenderSubsystem::GetLetterGroup(int32 Letter, FName& OutGroup, int32& OutIndexInGroup) const
{
	OutGroup = NAME_None;
	OutIndexInGroup = INDEX_NONE;
	const FTextGroupState* Group = IsValidLetter(Letter) ? GroupOf(Letter) : nullptr;
	if (!Group)
	{
		return false;
	}
	OutIndexInGroup = Group->Letters.IndexOfByKey(Letter);
	OutGroup = OutIndexInGroup != INDEX_NONE ? SlotGroup[Letter] : NAME_None;
	return OutIndexInGroup != INDEX_NONE;
}

FTextLetter UTextRenderSubsystem::GetLetter(int32 Letter) const
{
	return IsValidLetter(Letter) ? Letters[Letter] : FTextLetter();
}

void UTextRenderSubsystem::RemoveLetter(int32 Letter)
{
	if (!IsValidLetter(Letter))
	{
		return;
	}
	if (FTextGroupState* Group = Groups.Find(SlotGroup[Letter]))
	{
		Group->Letters.Remove(Letter);
	}
	FreeSlot(Letter);
}

void UTextRenderSubsystem::SetLetterCharacter(int32 Letter, const FString& Character)
{
	if (IsValidLetter(Letter) && Letters[Letter].Character != Character)
	{
		Letters[Letter].Character = Character;
		DirtyGlyphs.Add(Letter);
	}
}

void UTextRenderSubsystem::SetLetterTransform(int32 Letter, const FTransform& Transform)
{
	if (IsValidLetter(Letter))
	{
		Letters[Letter].Transform = Transform;
		MarkTransformDirty(Letter);
	}
}

void UTextRenderSubsystem::SetLetterWorldTransform(int32 Letter, const FTransform& Transform)
{
	if (const FTextGroupState* Group = IsValidLetter(Letter) ? GroupOf(Letter) : nullptr)
	{
		SetLetterTransform(Letter, Transform.GetRelativeTransform(FrameTransform(*Group)));
	}
}

void UTextRenderSubsystem::SetLetterLocation(int32 Letter, const FVector& Location)
{
	if (IsValidLetter(Letter))
	{
		Letters[Letter].Transform.SetLocation(Location);
		MarkTransformDirty(Letter);
	}
}

FTransform UTextRenderSubsystem::GetLetterTransform(int32 Letter) const
{
	return IsValidLetter(Letter) ? Letters[Letter].Transform : FTransform::Identity;
}

FTransform UTextRenderSubsystem::GetLetterWorldTransform(int32 Letter) const
{
	const FTextGroupState* Group = IsValidLetter(Letter) ? GroupOf(Letter) : nullptr;
	return Group ? Letters[Letter].Transform * FrameTransform(*Group) : FTransform::Identity;
}

void UTextRenderSubsystem::SetLetterVisible(int32 Letter, bool bNewVisible)
{
	if (IsValidLetter(Letter) && Letters[Letter].bVisible != bNewVisible)
	{
		Letters[Letter].bVisible = bNewVisible;
		MarkTransformDirty(Letter);  // hidden letters are drawn at zero scale
	}
}

FBox UTextRenderSubsystem::GetLetterBounds(int32 Letter) const
{
	const FTextGroupState* Group = IsValidLetter(Letter) ? GroupOf(Letter) : nullptr;
	const FTextGroupStyle Style = Group ? Group->Style : DefaultStyle();
	const ULetterFontAsset* Font = ResolveFont(Style);
	const TCHAR Char = Group ? Letters[Letter].GetChar() : TEXT('A');
	const float EmCm = GetEmCm(Style);
	const float CapHeightCm = Style.CapHeightCm;
	const float HalfDepth = FMath::Max(0.06f * EmCm, 0.5f);  // the extruded glyphs' depth

	FBox Box(FVector(-HalfDepth, -0.35f * CapHeightCm, -0.5f * CapHeightCm),
		FVector(HalfDepth, 0.35f * CapHeightCm, 0.5f * CapHeightCm));
	if (const UStaticMesh* Mesh = Font ? Font->FindGlyphMesh(Char) : nullptr)
	{
		// Glyph meshes share the letter frame at 1 em = 100 units.
		const FBox MeshBox = Mesh->GetBoundingBox();
		Box = FBox(MeshBox.Min * (EmCm / 100.f), MeshBox.Max * (EmCm / 100.f));
	}
	else if (const FLetterGlyph* Glyph = Font ? Font->FindGlyph(Char) : nullptr)
	{
		// A little generous: the quad carries the distance-field padding.
		const FVector4f& Q = Glyph->Quad;  // left, top, right, bottom; em, y down, baseline origin
		const float HalfWidth = (Q.Z - Q.X) * 0.5f * EmCm;
		const float HalfHeight = (Q.W - Q.Y) * 0.5f * EmCm;
		const float CentreZ = -((Q.Y + Q.W) * 0.5f + Font->CapHeight * 0.5f) * EmCm;
		Box = FBox(FVector(-HalfDepth, -HalfWidth, CentreZ - HalfHeight), FVector(HalfDepth, HalfWidth, CentreZ + HalfHeight));
	}
	Box.Min.X = FMath::Min(Box.Min.X, -HalfDepth);
	Box.Max.X = FMath::Max(Box.Max.X, HalfDepth);
	return Box;
}

// --- Custom data -----------------------------------------------------------------------------

void UTextRenderSubsystem::NormalizeCustomData(FTextLetter& Letter) const
{
	const int32 Own = Letter.CustomData.Num();
	Letter.CustomData.SetNum(NumCustomDataFloats);
	for (int32 Float = Own; Float < NumCustomDataFloats; ++Float)
	{
		Letter.CustomData[Float] = DefaultCustomData.IsValidIndex(Float) ? DefaultCustomData[Float] : 0.f;
	}
}

void UTextRenderSubsystem::WriteCustomData(int32 Letter, int32 DataIndex, const float* Values, int32 Num)
{
	if (!IsValidLetter(Letter) || Num <= 0)
	{
		return;
	}
	TArray<float>& Data = Letters[Letter].CustomData;
	const int32 First = FMath::Max(DataIndex, 0);
	const int32 Last = FMath::Min(DataIndex + Num, Data.Num());
	for (int32 Float = First; Float < Last; ++Float)
	{
		Data[Float] = Values[Float - DataIndex];
	}
	if ((DataIndex < 0 || DataIndex + Num > Data.Num()) && !bWarnedCustomDataRange)
	{
		bWarnedCustomDataRange = true;
		UE_LOG(LogHVPText, Warning, TEXT("Custom data floats %d-%d written, but letters have %d (NumCustomDataFloats). The rest were dropped."),
			DataIndex, DataIndex + Num - 1, Data.Num());
	}
	MarkCustomDataDirty(Letter);
}

void UTextRenderSubsystem::SetLetterCustomDataScalar(int32 Letter, int32 DataIndex, float Value)
{
	WriteCustomData(Letter, DataIndex, &Value, 1);
}

void UTextRenderSubsystem::SetLetterCustomDataVector(int32 Letter, int32 DataIndex, FVector Value)
{
	const float Floats[3] = { static_cast<float>(Value.X), static_cast<float>(Value.Y), static_cast<float>(Value.Z) };
	WriteCustomData(Letter, DataIndex, Floats, 3);
}

void UTextRenderSubsystem::SetLetterCustomData(int32 Letter, int32 DataIndex, const TArray<float>& Values)
{
	WriteCustomData(Letter, DataIndex, Values.GetData(), Values.Num());
}

void UTextRenderSubsystem::SetLetterCustomDataMasked(int32 Letter, int32 DataIndex, const TArray<float>& Values, int64 Mask)
{
	for (int32 Float = 0; Float < FMath::Min(Values.Num(), 64); ++Float)
	{
		if (static_cast<uint64>(Mask) & (uint64(1) << Float))
		{
			WriteCustomData(Letter, DataIndex + Float, &Values[Float], 1);
		}
	}
}

float UTextRenderSubsystem::GetLetterCustomDataScalar(int32 Letter, int32 DataIndex) const
{
	return IsValidLetter(Letter) && Letters[Letter].CustomData.IsValidIndex(DataIndex) ? Letters[Letter].CustomData[DataIndex] : 0.f;
}

FVector UTextRenderSubsystem::GetLetterCustomDataVector(int32 Letter, int32 DataIndex) const
{
	return FVector(GetLetterCustomDataScalar(Letter, DataIndex), GetLetterCustomDataScalar(Letter, DataIndex + 1),
		GetLetterCustomDataScalar(Letter, DataIndex + 2));
}

TArray<float> UTextRenderSubsystem::GetLetterCustomData(int32 Letter) const
{
	return IsValidLetter(Letter) ? Letters[Letter].CustomData : TArray<float>();
}

void UTextRenderSubsystem::SetNumCustomDataFloats(int32 NewNumFloats)
{
	NumCustomDataFloats = FMath::Clamp(NewNumFloats, 0, 64);
	for (FTextLetter& Letter : Letters)
	{
		NormalizeCustomData(Letter);
	}
	bWarnedCustomDataRange = false;
	bRebuildPending = true;
}

// --- Layout ----------------------------------------------------------------------------------

FVector2D UTextRenderSubsystem::LayoutText(const ULetterFontAsset* Font, const FString& Text, ETextAnchor Anchor, float Tracking,
	float LineSpacing, TArray<FLaidOutLetter>* OutLetters)
{
	if (OutLetters)
	{
		OutLetters->Reset();
	}
	if (!Font)
	{
		return FVector2D::ZeroVector;
	}
	TArray<FString> Lines;
	Text.Replace(TEXT("\r"), TEXT("")).ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty*/ false);
	if (Lines.IsEmpty())
	{
		Lines.AddDefaulted();
	}

	// Pen positions along each line, baselines going down by the line height. A letter's origin is
	// its ink centre on the line, and the capital's middle above the baseline.
	const float Cap = Font->CapHeight;
	const float LineStep = Font->LineHeight * LineSpacing;
	TArray<FLaidOutLetter> Placed;
	TArray<TPair<int32, FVector2D>> LineInk;  // first letter of the line, and its ink (left, right)
	float Widest = 0.f;
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString& Line = Lines[LineIndex];
		const int32 First = Placed.Num();
		FVector2D Ink(TNumericLimits<float>::Max(), TNumericLimits<float>::Lowest());
		float Pen = 0.f;
		for (int32 Index = 0; Index < Line.Len(); ++Index)
		{
			const TCHAR Char = Line[Index];
			float InkLeft, InkRight;
			if (!FChar::IsWhitespace(Char) && Font->GetInkExtent(Char, InkLeft, InkRight))
			{
				Placed.Add({ Char, FVector2D(Pen + (InkLeft + InkRight) * 0.5f, Cap * 0.5f - LineIndex * LineStep) });
				Ink.X = FMath::Min(Ink.X, Pen + InkLeft);
				Ink.Y = FMath::Max(Ink.Y, Pen + InkRight);
			}
			Pen += Font->GetAdvance(Char) + Tracking;
			if (Index + 1 < Line.Len())
			{
				Pen += Font->GetKerning(Char, Line[Index + 1]);
			}
		}
		if (Placed.Num() > First)
		{
			LineInk.Add({ First, Ink });
			Widest = FMath::Max(Widest, Ink.Y - Ink.X);
		}
	}

	// Anchor: each line's ink left-aligned or centred on 0; the block's top (first capital height)
	// or middle (down to the last baseline) on 0.
	const float Height = Cap + (Lines.Num() - 1) * LineStep;
	const float Down = Anchor == ETextAnchor::TopLeft ? Cap : Cap - Height * 0.5f;
	for (int32 LineIndex = 0; LineIndex < LineInk.Num(); ++LineIndex)
	{
		const int32 First = LineInk[LineIndex].Key;
		const int32 End = LineInk.IsValidIndex(LineIndex + 1) ? LineInk[LineIndex + 1].Key : Placed.Num();
		const FVector2D& Ink = LineInk[LineIndex].Value;
		const float Across = Anchor == ETextAnchor::TopLeft ? Ink.X : (Ink.X + Ink.Y) * 0.5f;
		for (int32 Index = First; Index < End; ++Index)
		{
			Placed[Index].Position -= FVector2D(Across, Down);
		}
	}
	if (OutLetters)
	{
		*OutLetters = MoveTemp(Placed);
	}
	return FVector2D(Widest, Height);
}

int32 UTextRenderSubsystem::SetGroupText(FName Group, const FString& Text, FVector Location, float Scale, ETextAnchor Anchor,
	FRotator Rotation, float Tracking, float LineSpacing)
{
	if (Group.IsNone())
	{
		UE_LOG(LogHVPText, Warning, TEXT("SetGroupText needs a group name"));
		return 0;
	}
	const FTextGroupStyle Style = FindOrAddGroup(Group).Style;
	const ULetterFontAsset* Font = ResolveFont(Style);
	if (!Font)
	{
		UE_LOG(LogHVPText, Warning, TEXT("SetGroupText: group %s has no font, and there is no Default Font, to lay out with"), *Group.ToString());
	}
	TArray<FLaidOutLetter> Laid;
	LayoutText(Font, Text, Anchor, Tracking, LineSpacing, &Laid);

	// Along the line is the letter frame's -Y, up its +Z.
	const float Unit = GetEmCm(Style) * Scale;
	const FQuat Facing = Rotation.Quaternion();
	TArray<FTextLetter> NewLetters;
	NewLetters.Reserve(Laid.Num());
	for (const FLaidOutLetter& Placed : Laid)
	{
		FTextLetter& Letter = NewLetters.AddDefaulted_GetRef();
		Letter.Character = FString::Chr(Placed.Char);
		const FVector Offset(0.f, -Placed.Position.X * Unit, Placed.Position.Y * Unit);
		Letter.Transform = FTransform(Facing, Location + Facing.RotateVector(Offset), FVector(Scale));
	}
	// Re-spelling keeps each kept letter's custom data, as a new text in the same colours.
	const TArray<int32> Slots = GetGroupLetterIndices(Group);
	for (int32 Index = 0; Index < FMath::Min(Slots.Num(), NewLetters.Num()); ++Index)
	{
		NewLetters[Index].CustomData = Letters[Slots[Index]].CustomData;
	}
	SetGroupLetters(Group, NewLetters);
	return NewLetters.Num();
}

FVector2D UTextRenderSubsystem::MeasureText(FName Group, const FString& Text, float Scale, float Tracking, float LineSpacing) const
{
	const FTextGroupStyle Style = GetGroupStyle(Group);
	return LayoutText(ResolveFont(Style), Text, ETextAnchor::TopLeft, Tracking, LineSpacing, nullptr) * (GetEmCm(Style) * Scale);
}

float UTextRenderSubsystem::GetLetterSpacing(FName Group, const FString& Left, const FString& Right, float Scale, float Tracking) const
{
	const FTextGroupStyle Style = GetGroupStyle(Group);
	const ULetterFontAsset* Font = ResolveFont(Style);
	if (!Font)
	{
		return 0.f;
	}
	const TCHAR A = Left.IsEmpty() ? TEXT(' ') : Left[0];
	const TCHAR B = Right.IsEmpty() ? TEXT(' ') : Right[0];
	// From A's centre back to its pen position, on by its advance and the pair's kerning to B's pen
	// position, then out to B's centre.
	const float Em = Font->GetAdvance(A) + Tracking + Font->GetKerning(A, B) - Font->GetInkCentre(A) + Font->GetInkCentre(B);
	return Em * GetEmCm(Style) * Scale;
}

// --- Drawing ---------------------------------------------------------------------------------

void UTextRenderSubsystem::ClearAll()
{
	for (int32 Pool = 1; Pool < Pools.Num(); ++Pool)
	{
		ReleasePool(Pool);
	}
	Groups.Reset();
	Letters.Reset();
	SlotGroup.Reset();
	FreeSlots.Reset();
	DirtyGlyphs.Reset();
	TransformDirty.Reset();
	CustomDataDirty.Reset();
	bRebuildPending = true;
	OnLettersReset.Broadcast();
}

void UTextRenderSubsystem::Rebuild()
{
	bRebuildPending = true;
}

void UTextRenderSubsystem::FlushChanges()
{
	if (bFlushing)
	{
		return;  // a listener of OnLettersRebuilt changed letters: they go with the next flush
	}
	TGuardValue<bool> Guard(bFlushing, true);
	EnsurePoolActor();
	if (!PoolActor)
	{
		return;
	}
	TrackFrames();

	if (bRebuildPending)
	{
		bRebuildPending = false;
		for (UTextRenderPool* Pool : Pools)
		{
			if (Pool)
			{
				Pool->Clear();
			}
		}
	}

	// Pools not drawn yet (all of them after a rebuild, a group's new own pool) are built whole:
	// everything they hold is current after that.
	TransformDirty.SetNum(Letters.Num(), false);
	CustomDataDirty.SetNum(Letters.Num(), false);
	bool bChanged = false;
	for (int32 Pool = 0; Pool < Pools.Num(); ++Pool)
	{
		UTextRenderPool* Set = Pools[Pool];
		USceneComponent* Root = Set ? Set->Root.Get() : nullptr;
		if (!Root)
		{
			continue;
		}
		const FTextPoolLetters Source(*this, Pool);
		for (ETextRenderTechnique Which : { ETextRenderTechnique::MSDF, ETextRenderTechnique::GlyphMesh })
		{
			UTextRenderTechnique* Tech = Set->Get(Which);
			if (!Tech->IsBuilt())
			{
				Tech->Build(Root, NumCustomDataFloats, Source, GetMine(Pool, Which));
				bChanged = true;
			}
		}
	}

	if (!DirtyGlyphs.IsEmpty())
	{
		// Letters that changed glyph, font, technique or pool, or are new: each technique of each pool
		// re-homes just those (a letter leaving a pool gives its instance back there).
		const TArray<int32> Changed = DirtyGlyphs.Array();
		DirtyGlyphs.Reset();
		UE_LOG(LogHVPText, Verbose, TEXT("%d letters changed glyph"), Changed.Num());
		for (int32 Pool = 0; Pool < Pools.Num(); ++Pool)
		{
			if (UTextRenderPool* Set = Pools[Pool])
			{
				const FTextPoolLetters Source(*this, Pool);
				for (ETextRenderTechnique Which : { ETextRenderTechnique::MSDF, ETextRenderTechnique::GlyphMesh })
				{
					Set->Get(Which)->UpdateGlyphs(Source, GetMine(Pool, Which), Changed);
				}
			}
		}
		for (int32 Letter : Changed)
		{
			if (Letters.IsValidIndex(Letter))
			{
				MarkTransformDirty(Letter);
				MarkCustomDataDirty(Letter);
			}
		}
		bChanged = true;
	}

	// Only the glyph components holding a changed letter are updated: labels following a hand do not
	// re-send a thousand still letters every frame.
	if (bTransformsDirty || bCustomDataDirty)
	{
		TransformDirty.SetNum(Letters.Num(), false);
		CustomDataDirty.SetNum(Letters.Num(), false);
		for (int32 Pool = 0; Pool < Pools.Num(); ++Pool)
		{
			UTextRenderPool* Set = Pools[Pool];
			if (!Set)
			{
				continue;
			}
			const FTextPoolLetters Source(*this, Pool);
			for (ETextRenderTechnique Which : { ETextRenderTechnique::MSDF, ETextRenderTechnique::GlyphMesh })
			{
				UTextRenderTechnique* Tech = Set->Get(Which);
				if (bTransformsDirty)
				{
					Tech->ApplyTransforms(Source, &TransformDirty);
				}
				if (bCustomDataDirty)
				{
					Tech->ApplyCustomData(Source, &CustomDataDirty);
				}
			}
		}
	}
	TransformDirty.Init(false, Letters.Num());
	CustomDataDirty.Init(false, Letters.Num());
	bTransformsDirty = false;
	bCustomDataDirty = false;

	if (bChanged)
	{
		OnLettersRebuilt.Broadcast();
	}
}

int32 UTextRenderSubsystem::GetRenderComponentCount() const
{
	int32 Count = 0;
	for (const UTextRenderPool* Pool : Pools)
	{
		if (Pool)
		{
			Count += Pool->MSDF->GetComponentCount() + Pool->GlyphMesh->GetComponentCount();
		}
	}
	return Count;
}

FString UTextRenderSubsystem::GetRenderNote() const
{
	TArray<FString> Notes;
	for (const UTextRenderPool* Pool : Pools)
	{
		if (Pool)
		{
			for (const UTextRenderTechnique* Tech : { static_cast<const UTextRenderTechnique*>(Pool->MSDF), static_cast<const UTextRenderTechnique*>(Pool->GlyphMesh) })
			{
				const FString Note = Tech->GetNote();
				if (!Note.IsEmpty())
				{
					Notes.AddUnique(Note);
				}
			}
		}
	}
	return FString::Join(Notes, TEXT("; "));
}
