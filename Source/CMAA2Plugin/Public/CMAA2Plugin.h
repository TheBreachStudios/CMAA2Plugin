// Copyright 2025 Maksym Paziuk and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "CMAA2Utils.h"


class FCMAA2PluginModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void InitCMAA2ViewExtension();

private:
#if CMAA2_UE_VERSION_OLDER_THAN(5, 0)
	TSharedPtr<class FSceneViewExtensionBase, ESPMode::ThreadSafe> CMAA2ViewExtension;
#else
	TSharedPtr<class FSceneViewExtensionBase> CMAA2ViewExtension;
#endif

	FDelegateHandle OnPostEngineInitDelegateHandle;
};