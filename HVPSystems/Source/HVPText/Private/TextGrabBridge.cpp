#include "TextGrabBridge.h"

#include "Engine/World.h"
#include "TextRenderSubsystem.h"

UTextGrabBridge::UTextGrabBridge()
{
	PrimaryComponentTick.bCanEverTick = false;

	GrabParams.GrabRadius = 3.f;
	GrabParams.bPreserveGrabOffset = true;
	GrabParams.bRigidFollow = true;
	GrabParams.bFollowRotation = true;
}

UHandGrabSubsystem* UTextGrabBridge::GetGrab() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UHandGrabSubsystem>() : nullptr;
}

UTextRenderSubsystem* UTextGrabBridge::GetText() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UTextRenderSubsystem>() : nullptr;
}

void UTextGrabBridge::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UHandGrabSubsystem* Grab = GetGrab())
	{
		Grab->UnregisterItemSource(this);
	}
	Bind(false);
	Super::EndPlay(EndPlayReason);
}

// --- Submission ------------------------------------------------------------------------------

void UTextGrabBridge::Bind(bool bBind)
{
	UTextRenderSubsystem* Text = GetText();
	if (bBound == bBind || !Text)
	{
		bBound = bBind && Text;
		return;
	}
	if (bBind)
	{
		Text->OnLettersRebuilt.AddDynamic(this, &UTextGrabBridge::HandleLettersRebuilt);
		Text->OnLettersReset.AddDynamic(this, &UTextGrabBridge::HandleLettersReset);
		Text->OnLetterRemoved.AddDynamic(this, &UTextGrabBridge::HandleLetterRemoved);
	}
	else
	{
		Text->OnLettersRebuilt.RemoveDynamic(this, &UTextGrabBridge::HandleLettersRebuilt);
		Text->OnLettersReset.RemoveDynamic(this, &UTextGrabBridge::HandleLettersReset);
		Text->OnLetterRemoved.RemoveDynamic(this, &UTextGrabBridge::HandleLetterRemoved);
	}
	bBound = bBind;
}

void UTextGrabBridge::SubmitGrabbables(const TArray<int32>& Letters)
{
	UHandGrabSubsystem* Grab = GetGrab();
	const UTextRenderSubsystem* Text = GetText();
	if (!Grab || !Text)
	{
		ClearGrabbables();
		return;
	}

	// Item indices are about to mean different letters. Keep holding letters that stay
	// submitted; HandGrab only knows items by index, so the rest are let go.
	TArray<int32> NewGrabbables;
	for (int32 Letter : Letters)
	{
		if (Text->IsValidLetter(Letter))
		{
			NewGrabbables.AddUnique(Letter);
		}
	}
	if (NewGrabbables != Grabbables)
	{
		Grab->ReleaseItems(this);
	}

	Bind(true);
	Grabbables = MoveTemp(NewGrabbables);
	RefreshBoxes();
	Grab->RegisterItemSource(this, GrabParams);  // updates the params when already registered
	Grab->bAllowMultiGrab |= bAllowMultiGrab;
}

void UTextGrabBridge::SubmitAllLetters()
{
	TArray<int32> All;
	if (const UTextRenderSubsystem* Text = GetText())
	{
		for (int32 Letter = 0; Letter < Text->GetLetterCount(); ++Letter)
		{
			All.Add(Letter);  // removed slots are filtered by SubmitGrabbables
		}
	}
	SubmitGrabbables(All);
}

void UTextGrabBridge::SubmitGroup(FName Group)
{
	SubmitGroups({ Group });
}

void UTextGrabBridge::SubmitGroups(const TArray<FName>& Groups)
{
	TArray<int32> Letters;
	if (const UTextRenderSubsystem* Text = GetText())
	{
		for (FName Group : Groups)
		{
			Letters.Append(Text->GetGroupLetterIndices(Group));
		}
	}
	SubmitGrabbables(Letters);
}

TArray<int32> UTextGrabBridge::GetGrabbables() const
{
	return Grabbables.FilterByPredicate([](int32 Letter) { return Letter != INDEX_NONE; });
}

void UTextGrabBridge::ClearGrabbables()
{
	if (UHandGrabSubsystem* Grab = GetGrab())
	{
		Grab->ReleaseItems(this);
		Grab->UnregisterItemSource(this);
	}
	Grabbables.Reset();
	Boxes.Reset();
	Radii.Reset();
	Bind(false);
}

void UTextGrabBridge::RemoveGrabbable(int32 Letter)
{
	UHandGrabSubsystem* Grab = GetGrab();
	for (int32 Item = 0; Item < Grabbables.Num(); ++Item)
	{
		if (Grabbables[Item] == Letter)
		{
			// Retired like a removed letter, so no other item is renumbered.
			if (Grab)
			{
				Grab->ForceReleaseItem(this, Item);
			}
			Grabbables[Item] = INDEX_NONE;
		}
	}
}

void UTextGrabBridge::HandleLettersRebuilt()
{
	// A font, size or character changed: the boxes did too.
	RefreshBoxes();
}

void UTextGrabBridge::HandleLettersReset()
{
	// Every letter index now means something else: nothing submitted is valid any more.
	ClearGrabbables();
}

void UTextGrabBridge::HandleLetterRemoved(int32 Letter)
{
	// Its slot may come back as another system's letter: it must not stay grabbable through us.
	// The item is retired, not removed, so no other item is renumbered; a hand holding it lets go
	// on HandGrab's next tick, when the pose comes back unavailable.
	for (int32& Submitted : Grabbables)
	{
		if (Submitted == Letter)
		{
			Submitted = INDEX_NONE;
		}
	}
}

void UTextGrabBridge::RefreshBoxes()
{
	const UTextRenderSubsystem* Text = GetText();
	Boxes.Reset(Grabbables.Num());
	Radii.Reset(Grabbables.Num());
	for (int32 Letter : Grabbables)
	{
		const FBox Box = Text ? Text->GetLetterBounds(Letter) : FBox(ForceInit);
		Boxes.Add(Box);
		Radii.Add(Box.IsValid ? Box.GetCenter().Size() + Box.GetExtent().Size() : 0.f);
	}
}

bool UTextGrabBridge::IsLetterHeld(int32 Letter) const
{
	const UHandGrabSubsystem* Grab = GetGrab();
	const int32 Item = Grabbables.IndexOfByKey(Letter);
	return Grab && Item != INDEX_NONE && Grab->IsItemHeld(this, Item);
}

// --- IHandGrabItemSource ---------------------------------------------------------------------

bool UTextGrabBridge::IsItemUsable(int32 Item) const
{
	const UTextRenderSubsystem* Text = GetText();
	return Text && Grabbables.IsValidIndex(Item) && Text->IsValidLetter(Grabbables[Item]);
}

float UTextGrabBridge::ItemBoxDistance(int32 Item, const FTransform& LetterWorld, const FVector& Point, FVector* OutClosest) const
{
	const FBox& Box = Boxes[Item];
	const float Scale = LetterWorld.GetScale3D().X;  // letters scale uniformly
	const FVector InLetter = LetterWorld.InverseTransformPosition(Point);  // unscaled letter units
	const FVector Clamped(
		FMath::Clamp(InLetter.X, Box.Min.X, Box.Max.X),
		FMath::Clamp(InLetter.Y, Box.Min.Y, Box.Max.Y),
		FMath::Clamp(InLetter.Z, Box.Min.Z, Box.Max.Z));
	if (OutClosest)
	{
		*OutClosest = LetterWorld.TransformPosition(Clamped);
	}
	return FVector::Dist(InLetter, Clamped) * Scale;
}

int32 UTextGrabBridge::FindGrabItem(const FVector& Probe, float MaxDistance,
	TFunctionRef<bool(int32)> IsAvailable, float& OutDistance) const
{
	OutDistance = MaxDistance;
	const UTextRenderSubsystem* Text = GetText();
	if (!Text)
	{
		return INDEX_NONE;
	}
	// A straight pass: a sphere reject, then the box test. At a thousand letters and a few probes
	// per hand that is cheap; a spatial grid can come later.
	float Best = MaxDistance;
	int32 BestItem = INDEX_NONE;
	for (int32 Item = 0; Item < Grabbables.Num(); ++Item)
	{
		const int32 Letter = Grabbables[Item];
		if (!Text->IsValidLetter(Letter) || !Text->GetLetter(Letter).bVisible)
		{
			continue;
		}
		const FTransform World = Text->GetLetterWorldTransform(Letter);
		const float Reach = Radii[Item] * World.GetScale3D().X + Best;
		if (FVector::DistSquared(Probe, World.GetLocation()) > Reach * Reach)
		{
			continue;
		}
		const float Dist = ItemBoxDistance(Item, World, Probe);
		if (Dist < Best && IsAvailable(Item))
		{
			Best = Dist;
			BestItem = Item;
		}
	}
	OutDistance = Best;
	return BestItem;
}

float UTextGrabBridge::GetGrabItemDistance(int32 Item, const FVector& Probe, FVector& OutClosest) const
{
	if (!IsItemUsable(Item))
	{
		OutClosest = Probe;
		return -1.f;
	}
	return ItemBoxDistance(Item, GetText()->GetLetterWorldTransform(Grabbables[Item]), Probe, &OutClosest);
}

bool UTextGrabBridge::GetGrabItemPose(int32 Item, FVector& OutLocation, FQuat& OutRotation) const
{
	if (!IsItemUsable(Item))
	{
		return false;
	}
	const FTransform World = GetText()->GetLetterWorldTransform(Grabbables[Item]);
	OutLocation = World.GetLocation();
	OutRotation = World.GetRotation();
	return true;
}

void UTextGrabBridge::SetGrabItemPose(int32 Item, const FVector& Location, const FQuat& Rotation)
{
	if (!IsItemUsable(Item))
	{
		return;
	}
	// Data only: the subsystem draws it in its own late-frame batch, which runs after this.
	UTextRenderSubsystem* Text = GetText();
	const int32 Letter = Grabbables[Item];
	const FVector Scale = Text->GetLetterWorldTransform(Letter).GetScale3D();
	Text->SetLetterWorldTransform(Letter, FTransform(Rotation, Location, Scale));
}

void UTextGrabBridge::BroadcastGrab(const FTextGrabEvent& Event, int32 Item, int32 HandIndex, const FVector& PinchLocation)
{
	if (IsItemUsable(Item))
	{
		FName Group;
		int32 IndexInGroup;
		GetText()->GetLetterGroup(Grabbables[Item], Group, IndexInGroup);
		const UHandGrabSubsystem* Grab = GetGrab();
		USkinnedMeshComponent* Hand = Grab ? Grab->GetHandMesh(HandIndex) : nullptr;
		Event.Broadcast(Grabbables[Item], Group, IndexInGroup, HandIndex, Hand, PinchLocation);
	}
}

void UTextGrabBridge::OnGrabItemGrabbed(int32 Item, int32 HandIndex, const FVector& PinchLocation)
{
	BroadcastGrab(OnLetterGrabbed, Item, HandIndex, PinchLocation);
}

void UTextGrabBridge::OnGrabItemReleased(int32 Item, int32 HandIndex, const FVector& PinchLocation)
{
	BroadcastGrab(OnLetterReleased, Item, HandIndex, PinchLocation);
}

void UTextGrabBridge::OnGrabItemHover(int32 Item, bool bHovered)
{
	if (IsItemUsable(Item))
	{
		FName Group;
		int32 IndexInGroup;
		GetText()->GetLetterGroup(Grabbables[Item], Group, IndexInGroup);
		(bHovered ? OnLetterHoverBegin : OnLetterHoverEnd).Broadcast(Grabbables[Item], Group, IndexInGroup);
	}
}
