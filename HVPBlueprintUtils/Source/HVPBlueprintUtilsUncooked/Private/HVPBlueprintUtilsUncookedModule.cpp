#include "Modules/ModuleManager.h"

// Nothing to start up: the module exists to give its K2Nodes an UncookedOnly package, which is what
// the blueprint compiler checks for. Node registration happens through GetMenuActions on the nodes
// themselves, which the blueprint action database discovers by class.
IMPLEMENT_MODULE(FDefaultModuleImpl, HVPBlueprintUtilsUncooked);
