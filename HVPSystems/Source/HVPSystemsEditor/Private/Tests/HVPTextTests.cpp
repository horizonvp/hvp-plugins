#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "LetterFontAsset.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "TextRenderSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HVPTextTest
{
	/** A game world of its own (the subsystem exists only in game worlds), torn down at scope end. */
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("HVPTextTest"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		UTextRenderSubsystem* Text() const { return World->GetSubsystem<UTextRenderSubsystem>(); }

		USceneComponent* SpawnFrame(const FVector& Location) const
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USceneComponent* Frame = NewObject<USceneComponent>(Actor, TEXT("Frame"));
			Actor->SetRootComponent(Frame);
			Frame->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Frame;
		}
	};

	/** A font in memory: the given capitals in an atlas, each quad 0.6 x 0.7 em from the baseline up to
	    the cap height, so a glyph's middle is its letter's origin. */
	ULetterFontAsset* MakeFont(const TCHAR* Chars)
	{
		ULetterFontAsset* Font = NewObject<ULetterFontAsset>(GetTransientPackage());
		Font->AddToRoot();
		Font->DisplayName = TEXT("Test");
		Font->CapHeight = 0.7f;
		Font->MSDFMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		for (const TCHAR* C = Chars; *C; ++C)
		{
			FLetterGlyph Glyph;
			Glyph.UVRect = FVector4f(0.f, 0.f, 0.5f, 0.5f);
			Glyph.Quad = FVector4f(0.f, -0.7f, 0.6f, 0.f);
			Glyph.Advance = 0.65f;
			Font->Glyphs.Add(*C, Glyph);
		}
		return Font;
	}

	FTextGroupStyle Style(ULetterFontAsset* Font, float CapHeightCm = 8.f, ETextRenderTechnique Technique = ETextRenderTechnique::MSDF)
	{
		FTextGroupStyle Result;
		Result.Font = Font;
		Result.Technique = Technique;
		Result.CapHeightCm = CapHeightCm;
		return Result;
	}

	/** Whether any visible instance of any letter component under Root sits at Location. */
	bool HasInstanceAt(const USceneComponent* Root, const FVector& Location)
	{
		TArray<USceneComponent*> Children;
		Root->GetChildrenComponents(false, Children);
		for (const USceneComponent* Child : Children)
		{
			const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Child);
			for (int32 Index = 0; Instances && Index < Instances->GetInstanceCount(); ++Index)
			{
				FTransform Instance;
				Instances->GetInstanceTransform(Index, Instance, /*bWorldSpace*/ true);
				if (!Instance.GetScale3D().IsNearlyZero() && Instance.GetLocation().Equals(Location, 0.01))
				{
					return true;
				}
			}
		}
		return false;
	}

	const USceneComponent* SharedPoolRoot(UWorld* World)
	{
		for (TActorIterator<ATextRenderPoolActor> It(World); It; ++It)
		{
			return It->GetRootComponent();
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHVPTextBatchingTest, "HVP.Text.Batching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHVPTextBatchingTest::RunTest(const FString& Parameters)
{
	using namespace HVPTextTest;
	FTestWorld Test;
	UTextRenderSubsystem* Text = Test.Text();
	if (!TestNotNull(TEXT("The game world has a text subsystem"), Text))
	{
		return false;
	}
	ULetterFontAsset* Font = MakeFont(TEXT("AB"));
	ULetterFontAsset* Other = MakeFont(TEXT("A"));

	// Two groups, one font, different sizes: one component per glyph, shared.
	Text->SetGroupStyle(TEXT("One"), Style(Font, 8.f));
	TestEqual(TEXT("Spaces only move the pen"), Text->SetGroupText(TEXT("One"), TEXT("AB A"), FVector::ZeroVector), 3);
	Text->SetGroupStyle(TEXT("Two"), Style(Font, 20.f));
	Text->SetGroupText(TEXT("Two"), TEXT("BA"), FVector(0.f, 100.f, 0.f));
	Text->FlushChanges();
	TestEqual(TEXT("Both groups share a component per glyph"), Text->GetRenderComponentCount(), 2);
	TestTrue(TEXT("No notes"), Text->GetRenderNote().IsEmpty());

	// Another font is another set of glyphs.
	Text->SetGroupFont(TEXT("Two"), Other);
	Text->FlushChanges();
	TestEqual(TEXT("Group Two's A moved to the other font; its B has no glyph there"), Text->GetRenderComponentCount(), 3);
	TestFalse(TEXT("The missing glyph is noted"), Text->GetRenderNote().IsEmpty());
	Text->SetGroupFont(TEXT("Two"), Font);

	// Removing frees slots; a new letter of the same character takes one back.
	const int32 Slots = Text->GetLetterCount();
	const int32 Removed = Text->GetGroupLetterIndex(TEXT("One"), 0);
	Text->RemoveGroup(TEXT("One"));
	TestFalse(TEXT("A removed group's letter is no longer valid"), Text->IsValidLetter(Removed));
	TestFalse(TEXT("The group is gone"), Text->HasGroup(TEXT("One")));
	Text->SetGroupStyle(TEXT("Three"), Style(Font));
	Text->SetGroupText(TEXT("Three"), TEXT("A"), FVector::ZeroVector);
	TestEqual(TEXT("A freed slot was reused"), Text->GetLetterCount(), Slots);
	Text->FlushChanges();
	TestEqual(TEXT("An emptied component stays until a rebuild"), Text->GetRenderComponentCount(), 3);
	Text->Rebuild();
	Text->FlushChanges();
	TestEqual(TEXT("A rebuild leaves one component per glyph in use"), Text->GetRenderComponentCount(), 2);

	Text->ClearAll();
	Text->FlushChanges();
	TestEqual(TEXT("ClearAll leaves no letters"), Text->GetLetterCount(), 0);
	TestEqual(TEXT("ClearAll leaves no components"), Text->GetRenderComponentCount(), 0);

	Font->RemoveFromRoot();
	Other->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHVPTextFramesTest, "HVP.Text.Frames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHVPTextFramesTest::RunTest(const FString& Parameters)
{
	using namespace HVPTextTest;
	FTestWorld Test;
	UTextRenderSubsystem* Text = Test.Text();
	if (!TestNotNull(TEXT("The game world has a text subsystem"), Text))
	{
		return false;
	}
	ULetterFontAsset* Font = MakeFont(TEXT("AB"));

	// A group in the world, and one following a frame in the shared pool.
	Text->SetGroupStyle(TEXT("World"), Style(Font));
	Text->SetGroupText(TEXT("World"), TEXT("A"), FVector(0.f, 0.f, 50.f), 1.f, ETextAnchor::Center);
	USceneComponent* Shared = Test.SpawnFrame(FVector(100.f, 0.f, 0.f));
	Text->SetGroupStyle(TEXT("Follow"), Style(Font));
	Text->SetGroupFrame(TEXT("Follow"), Shared);
	Text->SetGroupText(TEXT("Follow"), TEXT("B"), FVector(0.f, 0.f, 10.f), 1.f, ETextAnchor::Center);
	Text->FlushChanges();
	const USceneComponent* PoolRoot = SharedPoolRoot(Test.World);
	if (!TestNotNull(TEXT("The shared pool's actor was spawned"), PoolRoot))
	{
		return false;
	}
	const int32 FollowLetter = Text->GetGroupLetterIndex(TEXT("Follow"), 0);
	TestTrue(TEXT("The framed letter is drawn at the frame"), HasInstanceAt(PoolRoot, Text->GetLetterWorldTransform(FollowLetter).GetLocation()));
	TestTrue(TEXT("...which is 100 cm out"), Text->GetLetterWorldTransform(FollowLetter).GetLocation().Equals(FVector(100.f, 0.f, 10.f), 0.01));

	// Moving the frame re-sends the group with the next flush.
	Shared->GetOwner()->SetActorLocation(FVector(100.f, 0.f, 300.f));
	Text->FlushChanges();
	TestTrue(TEXT("The framed letter followed its frame"), HasInstanceAt(PoolRoot, FVector(100.f, 0.f, 310.f)));
	TestTrue(TEXT("The world letter stayed"), HasInstanceAt(PoolRoot, FVector(0.f, 0.f, 50.f)));

	// Own instances: attached to the frame, so they move with it untouched.
	USceneComponent* Own = Test.SpawnFrame(FVector(0.f, 500.f, 0.f));
	Text->SetGroupStyle(TEXT("Own"), Style(Font));
	Text->SetGroupFrame(TEXT("Own"), Own, /*bOwnInstances*/ true);
	Text->SetGroupText(TEXT("Own"), TEXT("A"), FVector::ZeroVector, 1.f, ETextAnchor::Center);
	Text->FlushChanges();
	TestEqual(TEXT("The own group added a component of its own"), Text->GetRenderComponentCount(), 3);
	Own->GetOwner()->SetActorLocation(FVector(0.f, 700.f, 0.f));
	TestTrue(TEXT("Its instance moved with the frame before any flush"), HasInstanceAt(Own, FVector(0.f, 700.f, 0.f)));

	// Back to the shared pool: the own component goes.
	Text->SetGroupFrame(TEXT("Own"), Own, /*bOwnInstances*/ false);
	Text->FlushChanges();
	TestEqual(TEXT("Released the own component"), Text->GetRenderComponentCount(), 2);
	TestTrue(TEXT("The letter is drawn in the shared pool, still at its frame"), HasInstanceAt(PoolRoot, FVector(0.f, 700.f, 0.f)));

	// A group whose frame is destroyed is removed.
	Shared->GetOwner()->Destroy();
	Text->FlushChanges();
	TestFalse(TEXT("The group of a destroyed frame was removed"), Text->HasGroup(TEXT("Follow")));
	TestFalse(TEXT("Its letter too"), Text->IsValidLetter(FollowLetter));

	// World transforms round-trip through a frame.
	const int32 OwnLetter = Text->GetGroupLetterIndex(TEXT("Own"), 0);
	Text->SetLetterWorldTransform(OwnLetter, FTransform(FVector(10.f, 20.f, 30.f)));
	TestTrue(TEXT("SetLetterWorldTransform lands where asked"), Text->GetLetterWorldTransform(OwnLetter).GetLocation().Equals(FVector(10.f, 20.f, 30.f), 0.01));

	Font->RemoveFromRoot();
	return true;
}

#endif
