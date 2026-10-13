#include "TextRenderTechnique.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "LetterFontAsset.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/** Where a free instance waits: nowhere, at no size. */
	const FTransform HiddenInstance(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
}

// --- Base: one instanced component per distinct glyph ----------------------------------------

void UTextRenderTechnique::SizeLetterArrays(int32 Num)
{
	const int32 Old = LetterComponent.Num();
	if (Num > Old)
	{
		LetterComponent.SetNum(Num);
		LetterInstance.SetNum(Num);
		LetterMissing.SetNum(Num);
		LetterFont.SetNum(Num);
		for (int32 Letter = Old; Letter < Num; ++Letter)
		{
			LetterComponent[Letter] = INDEX_NONE;
			LetterInstance[Letter] = INDEX_NONE;
			LetterMissing[Letter] = false;
		}
	}
}

int32 UTextRenderTechnique::GetGlyphComponent(const ULetterFontAsset* Font, TCHAR Char)
{
	if (!Font)
	{
		return INDEX_NONE;
	}
	const TPair<TObjectKey<ULetterFontAsset>, TCHAR> Key(Font, Char);
	if (const int32* Found = GlyphComponent.Find(Key))
	{
		return *Found;
	}
	const FString* Note = FontNotes.Find(Font);
	if (!Note)
	{
		Note = &FontNotes.Add(Font, CheckFont(*Font));
	}
	FGlyphDraw Glyph;
	USceneComponent* RootComponent = Root.Get();
	if (!Note->IsEmpty() || !RootComponent || !DescribeGlyph(*Font, Char, Glyph))
	{
		return GlyphComponent.Add(Key, INDEX_NONE);
	}
	AActor* Owner = RootComponent->GetOwner();
	UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(Owner,
		MakeUniqueObjectName(Owner, UInstancedStaticMeshComponent::StaticClass(), TEXT("GlyphInstances")), RF_Transient);
	Instances->SetupAttachment(RootComponent);
	Instances->SetStaticMesh(Glyph.Mesh);
	Instances->SetMaterial(0, Glyph.Material);
	Instances->SetNumCustomDataFloats(NumCustomDataFloats);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);
	if (Glyph.bHasPrimitiveData)
	{
		Instances->SetCustomPrimitiveDataVector4(0, FVector4(Glyph.PrimitiveData));
	}
	ConfigureComponent(Instances);
	Instances->RegisterComponent();
	const int32 Comp = Components.Add(Instances);
	ComponentInLetter.Add(Glyph.InLetter);
	InstanceOwner.AddDefaulted();
	FreeInstances.AddDefaulted();
	Scratch.AddDefaulted();
	return GlyphComponent.Add(Key, Comp);
}

void UTextRenderTechnique::AssignLetter(int32 Letter, const ULetterFontAsset* Font, TCHAR Char, bool bMine)
{
	const int32 Want = bMine ? GetGlyphComponent(Font, Char) : INDEX_NONE;
	LetterMissing[Letter] = bMine && Want == INDEX_NONE;
	LetterFont[Letter] = Font;
	const int32 Have = LetterComponent[Letter];
	if (Want == Have)
	{
		return;
	}
	if (Have != INDEX_NONE)
	{
		const int32 Released = LetterInstance[Letter];
		InstanceOwner[Have][Released] = INDEX_NONE;
		FreeInstances[Have].Add(Released);
		ReleasedIn.Add(Have);
	}
	LetterComponent[Letter] = Want;
	LetterInstance[Letter] = INDEX_NONE;
	if (Want == INDEX_NONE)
	{
		return;
	}
	int32 Instance;
	if (!FreeInstances[Want].IsEmpty())
	{
		Instance = FreeInstances[Want].Pop(EAllowShrinking::No);
		InstanceOwner[Want][Instance] = Letter;
	}
	else
	{
		Instance = Components[Want]->AddInstance(HiddenInstance, /*bWorldSpace*/ false);
		check(Instance == InstanceOwner[Want].Num());
		InstanceOwner[Want].Add(Letter);
	}
	LetterInstance[Letter] = Instance;
}

void UTextRenderTechnique::Build(USceneComponent* InRoot, int32 InNumCustomDataFloats, const ITextLetterSource& Letters, TConstArrayView<bool> Mine)
{
	Clear();
	Root = InRoot;
	NumCustomDataFloats = InNumCustomDataFloats;
	SizeLetterArrays(Letters.Num());

	// Every letter's component first, then each component's instances in one call: the batched
	// version of AssignLetter, for the many letters of a full build.
	TArray<TArray<int32>> Owners;
	for (int32 Letter = 0; Letter < Letters.Num(); ++Letter)
	{
		const ULetterFontAsset* Font = Letters.GetFont(Letter);
		const int32 Comp = Mine[Letter] ? GetGlyphComponent(Font, Letters.GetChar(Letter)) : INDEX_NONE;
		LetterMissing[Letter] = Mine[Letter] && Comp == INDEX_NONE;
		LetterFont[Letter] = Font;
		if (Comp == INDEX_NONE)
		{
			continue;
		}
		Owners.SetNum(Components.Num());
		LetterComponent[Letter] = Comp;
		LetterInstance[Letter] = Owners[Comp].Add(Letter);
	}
	Owners.SetNum(Components.Num());
	for (int32 Comp = 0; Comp < Components.Num(); ++Comp)
	{
		TArray<FTransform> Hidden;
		Hidden.Init(HiddenInstance, Owners[Comp].Num());
		Components[Comp]->AddInstances(Hidden, /*bShouldReturnIndices*/ false);
		InstanceOwner[Comp] = MoveTemp(Owners[Comp]);
	}
	ApplyTransforms(Letters);
	ApplyCustomData(Letters);
}

void UTextRenderTechnique::UpdateGlyphs(const ITextLetterSource& Letters, TConstArrayView<bool> Mine, TConstArrayView<int32> Changed)
{
	if (!Root.IsValid())
	{
		return;  // never built: the subsystem's next build draws everything
	}
	SizeLetterArrays(Letters.Num());
	for (int32 Letter : Changed)
	{
		if (Letter >= 0 && Letter < Letters.Num())
		{
			AssignLetter(Letter, Letters.GetFont(Letter), Letters.GetChar(Letter), Mine[Letter]);
		}
	}
}

void UTextRenderTechnique::ApplyTransforms(const ITextLetterSource& Letters, const TBitArray<>* Dirty)
{
	// One batched update per distinct glyph, not per letter, and only for glyphs that changed.
	// Free instances stay at zero size.
	for (int32 Comp = 0; Comp < Components.Num(); ++Comp)
	{
		const TArray<int32>& Owners = InstanceOwner[Comp];
		bool bChanged = !Dirty || ReleasedIn.Contains(Comp);
		for (int32 Instance = 0; Instance < Owners.Num() && !bChanged; ++Instance)
		{
			bChanged = Owners[Instance] != INDEX_NONE && Dirty->IsValidIndex(Owners[Instance]) && (*Dirty)[Owners[Instance]];
		}
		if (!bChanged || !IsValid(Components[Comp]))
		{
			continue;
		}
		TArray<FTransform>& Transforms = Scratch[Comp];
		Transforms.SetNumUninitialized(Owners.Num());
		for (int32 Instance = 0; Instance < Owners.Num(); ++Instance)
		{
			const int32 Letter = Owners[Instance];
			Transforms[Instance] = Letter != INDEX_NONE ? ComponentInLetter[Comp] * Letters.GetDrawFrame(Letter) : HiddenInstance;
		}
		Components[Comp]->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
	}
	ReleasedIn.Reset();
}

void UTextRenderTechnique::ApplyCustomData(const ITextLetterSource& Letters, const TBitArray<>* Dirty)
{
	if (NumCustomDataFloats <= 0)
	{
		return;
	}
	for (int32 Comp = 0; Comp < Components.Num(); ++Comp)
	{
		if (!IsValid(Components[Comp]))
		{
			continue;
		}
		const TArray<int32>& Owners = InstanceOwner[Comp];
		bool bTouched = false;
		for (int32 Instance = 0; Instance < Owners.Num(); ++Instance)
		{
			const int32 Letter = Owners[Instance];
			if (Letter != INDEX_NONE && (!Dirty || (Dirty->IsValidIndex(Letter) && (*Dirty)[Letter])))
			{
				Components[Comp]->SetCustomData(Instance, Letters.GetCustomData(Letter), /*bMarkRenderStateDirty*/ false);
				bTouched = true;
			}
		}
		if (bTouched)
		{
			Components[Comp]->MarkRenderStateDirty();
		}
	}
}

FString UTextRenderTechnique::GetNote() const
{
	// Missing letters, by cause: a font this technique cannot draw at all, else glyphs it lacks.
	TArray<FString> Notes;
	int32 MissingGlyphs = 0;
	TSet<TObjectKey<ULetterFontAsset>> Noted;
	for (int32 Letter = 0; Letter < LetterMissing.Num(); ++Letter)
	{
		if (!LetterMissing[Letter])
		{
			continue;
		}
		const FString* FontNote = FontNotes.Find(LetterFont[Letter]);
		if (FontNote && !FontNote->IsEmpty())
		{
			if (!Noted.Contains(LetterFont[Letter]))
			{
				Noted.Add(LetterFont[Letter]);
				const ULetterFontAsset* Font = LetterFont[Letter].ResolveObjectPtr();
				Notes.Add(FString::Printf(TEXT("%s: %s"), Font ? *Font->DisplayName : TEXT("no font"), **FontNote));
			}
		}
		else if (LetterFont[Letter] == TObjectKey<ULetterFontAsset>())
		{
			if (!Noted.Contains(LetterFont[Letter]))
			{
				Noted.Add(LetterFont[Letter]);
				Notes.Add(TEXT("letters without a font (set one, or the Default Font)"));
			}
		}
		else
		{
			++MissingGlyphs;
		}
	}
	if (MissingGlyphs > 0)
	{
		Notes.Add(FString::Printf(TEXT("%d letters have no glyph in their font"), MissingGlyphs));
	}
	return FString::Join(Notes, TEXT("; "));
}

void UTextRenderTechnique::Clear()
{
	for (UInstancedStaticMeshComponent* Comp : Components)
	{
		if (IsValid(Comp))
		{
			Comp->DestroyComponent();
		}
	}
	Components.Reset();
	Root.Reset();
	FontNotes.Reset();
	GlyphComponent.Reset();
	ComponentInLetter.Reset();
	InstanceOwner.Reset();
	FreeInstances.Reset();
	ReleasedIn.Reset();
	LetterComponent.Reset();
	LetterInstance.Reset();
	LetterMissing.Reset();
	LetterFont.Reset();
	Scratch.Reset();
}

// --- MSDF ------------------------------------------------------------------------------------

FString UTextRenderTechnique_MSDF::CheckFont(const ULetterFontAsset& Font) const
{
	if (!QuadMesh)
	{
		return TEXT("no Quad Mesh (Project Settings > HVP Text)");
	}
	return Font.MSDFMaterial && !Font.Glyphs.IsEmpty() ? FString() : TEXT("no MSDF atlas in this font asset");
}

bool UTextRenderTechnique_MSDF::DescribeGlyph(const ULetterFontAsset& Font, TCHAR Char, FGlyphDraw& Out) const
{
	const FLetterGlyph* Glyph = Font.FindGlyph(Char);
	// Glyphs known only from their mesh (for layout) have no rect in the atlas.
	if (!Glyph || Glyph->UVRect.Z <= Glyph->UVRect.X)
	{
		return false;
	}
	// Glyph quads come in em, y down, origin on the baseline, padded by the distance range. Centre
	// each on the capital's middle so it lines up with the glyph meshes. 1 em = 1 cm here: the
	// letter's draw frame carries its group's em size.
	const FVector4f& Q = Glyph->Quad;  // left, top, right, bottom
	const float Width = Q.Z - Q.X;
	const float Height = Q.W - Q.Y;
	const float Up = -((Q.Y + Q.W) * 0.5f + Font.CapHeight * 0.5f);
	// The plane's normal is +Z: pitching it -90 turns the normal to +X (toward the reader), its X
	// axis to -Z (height) and leaves its Y axis as the width.
	Out.Mesh = QuadMesh;
	Out.Material = Font.MSDFMaterial;
	Out.InLetter = FTransform(FRotator(-90.f, 0.f, 0.f), FVector(0.f, 0.f, Up), FVector(Height / 100.f, Width / 100.f, 1.f));
	Out.PrimitiveData = Glyph->UVRect;
	Out.bHasPrimitiveData = true;
	return true;
}

void UTextRenderTechnique_MSDF::ConfigureComponent(UInstancedStaticMeshComponent* Component) const
{
	// Translucent primitives sort by distance to their bounds' centre. A glyph's component spans
	// every letter of that glyph, so the centre can be metres away from any of them; the letters
	// sorted before the other translucency (passthrough among it) and were drawn over.
	Component->SetTranslucentSortPriority(TranslucentSortPriority);
}

// --- Glyph meshes ----------------------------------------------------------------------------

FString UTextRenderTechnique_GlyphMesh::CheckFont(const ULetterFontAsset& Font) const
{
	if (!Material)
	{
		return TEXT("no Glyph Mesh Material (Project Settings > HVP Text)");
	}
	return Font.GlyphMeshes.IsEmpty() ? TEXT("no glyph meshes in this font asset (build_glyph_meshes.py)") : FString();
}

bool UTextRenderTechnique_GlyphMesh::DescribeGlyph(const ULetterFontAsset& Font, TCHAR Char, FGlyphDraw& Out) const
{
	UStaticMesh* Mesh = Font.FindGlyphMesh(Char);
	if (!Mesh)
	{
		return false;
	}
	// Meshes are 1 em = 100 units, in the letter frame: only the size changes.
	Out.Mesh = Mesh;
	Out.Material = Material;
	Out.InLetter = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.01f));
	return true;
}
