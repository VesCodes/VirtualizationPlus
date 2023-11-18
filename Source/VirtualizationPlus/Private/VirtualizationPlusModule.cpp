#include <Modules/ModuleManager.h>

#include <HAL/IConsoleManager.h>
#include <Virtualization/VirtualizationSystem.h>

IMPLEMENT_MODULE(FDefaultModuleImpl, VirtualizationPlus)

FAutoConsoleCommandWithWorldAndArgs Cmd_VirtualizeAssets(
	TEXT("VirtualizationPlus.Virtualize"), TEXT("Virtualizes a list of assets"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World)
	{
		UE::Virtualization::IVirtualizationSystem& System = UE::Virtualization::IVirtualizationSystem::Get();
		System.TryVirtualizePackages(Args, UE::Virtualization::EVirtualizationOptions::Checkout);
	}));

FAutoConsoleCommandWithWorldAndArgs Cmd_RehydrateAssets(
	TEXT("VirtualizationPlus.Rehydrate"), TEXT("Rehydrates a list of assets"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World)
	{
		UE::Virtualization::IVirtualizationSystem& System = UE::Virtualization::IVirtualizationSystem::Get();
		System.TryRehydratePackages(Args, UE::Virtualization::ERehydrationOptions::Checkout);
	}));
