#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetNamingConfig.h"
#include "AssetSuffixResolver.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSuffixBlueprintTypesTest, "HVP.Naming.Suffixes.BlueprintTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The plain-UBlueprint suffixes are told apart by BlueprintType, and a new one needs entries in two
 * places to work at all: the resolver (which type gets it) and the config's suffix table (so a name
 * already carrying it is not suffixed again). This covers both halves for _BML, _PDL and _IDL, and keeps
 * the neighbouring _BFL and _BP honest.
 */
bool FAssetSuffixBlueprintTypesTest::RunTest(const FString& Parameters)
{
	FAssetSuffixResolver Resolver;
	Resolver.Initialise();

	auto MakeBlueprint = [](const TCHAR* Name, UClass* Parent, EBlueprintType Type)
	{
		UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HVPNamingTest/%s"), Name));
		return TStrongObjectPtr<UBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			Parent, Package, Name, Type, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	};

	const TStrongObjectPtr<UBlueprint> Macros = MakeBlueprint(TEXT("NamingProbeMacros"), AActor::StaticClass(), BPTYPE_MacroLibrary);
	const TStrongObjectPtr<UBlueprint> Functions = MakeBlueprint(TEXT("NamingProbeFunctions"), UBlueprintFunctionLibrary::StaticClass(), BPTYPE_FunctionLibrary);
	const TStrongObjectPtr<UBlueprint> Actor = MakeBlueprint(TEXT("NamingProbeActor"), AActor::StaticClass(), BPTYPE_Normal);

	TestEqual(TEXT("Macro library -> _BML"), Resolver.ResolveSuffix(Macros.Get()), FString(TEXT("_BML")));
	TestEqual(TEXT("Function library -> _BFL"), Resolver.ResolveSuffix(Functions.Get()), FString(TEXT("_BFL")));
	TestEqual(TEXT("Actor Blueprint -> _BP"), Resolver.ResolveSuffix(Actor.Get()), FString(TEXT("_BP")));

	const TStrongObjectPtr<UBlueprint> Component = MakeBlueprint(TEXT("NamingProbeComponent"), UActorComponent::StaticClass(), BPTYPE_Normal);
	TestEqual(TEXT("Actor component Blueprint -> _AC"), Resolver.ResolveSuffix(Component.Get()), FString(TEXT("_AC")));

	// _AW, like _PDL below, only applies where its plugin (HVPCodeAnimWeb) is enabled.
	if (UClass* WebClass = FindObject<UClass>(nullptr, TEXT("/Script/HVPCodeAnimWeb.CodeAnimationWeb")))
	{
		const TStrongObjectPtr<UBlueprint> Web = MakeBlueprint(TEXT("NamingProbeWeb"), WebClass, BPTYPE_Normal);
		TestEqual(TEXT("Animation Web -> _AW, not _AC"), Resolver.ResolveSuffix(Web.Get()), FString(TEXT("_AW")));
	}
	else
	{
		AddInfo(TEXT("HVPCodeAnimWeb is not enabled here; _AW resolution skipped."));
	}

	// _PDL resolves by class path, so it only applies where HVPPrimitiveData is enabled.
	if (UClass* LegendClass = FindObject<UClass>(nullptr, TEXT("/Script/HVPPrimitiveDataUncooked.PrimitiveDataLegend")))
	{
		const TStrongObjectPtr<UObject> Legend(NewObject<UObject>(GetTransientPackage(), LegendClass));
		TestEqual(TEXT("Primitive Data Legend -> _PDL"), Resolver.ResolveSuffix(Legend.Get()), FString(TEXT("_PDL")));
	}
	else
	{
		AddInfo(TEXT("HVPPrimitiveData is not enabled here; _PDL resolution skipped."));
	}
	if (UClass* LegendClass = FindObject<UClass>(nullptr, TEXT("/Script/HVPPrimitiveDataUncooked.InstanceDataLegend")))
	{
		const TStrongObjectPtr<UObject> Legend(NewObject<UObject>(GetTransientPackage(), LegendClass));
		TestEqual(TEXT("Instance Data Legend -> _IDL"), Resolver.ResolveSuffix(Legend.Get()), FString(TEXT("_IDL")));
	}
	else
	{
		AddInfo(TEXT("HVPPrimitiveData is not enabled here, or predates the Instance Data Legend; _IDL resolution skipped."));
	}

	// The other half: names already carrying the suffix are recognised, so nothing appends a second.
	const FAssetNamingConfig Config = FAssetNamingConfig::Load(TEXT("Tools/Conventions/conventions.json"));
	TestEqual(TEXT("'Utility_BML' already has _BML"), Config.FindTrailingSuffix(TEXT("Utility_BML")), FString(TEXT("_BML")));
	TestEqual(TEXT("'Rocks_PDL' already has _PDL"), Config.FindTrailingSuffix(TEXT("Rocks_PDL")), FString(TEXT("_PDL")));
	TestEqual(TEXT("'Grass_IDL' already has _IDL"), Config.FindTrailingSuffix(TEXT("Grass_IDL")), FString(TEXT("_IDL")));
	TestEqual(TEXT("'DeviceWeb_AW' already has _AW"), Config.FindTrailingSuffix(TEXT("DeviceWeb_AW")), FString(TEXT("_AW")));
	TestEqual(TEXT("'Utility_BML_BP' ends in _BP"), Config.FindTrailingSuffix(TEXT("Utility_BML_BP")), FString(TEXT("_BP")));

	return true;
}

#endif
