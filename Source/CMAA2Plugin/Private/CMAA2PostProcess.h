// Copyright 2025 Maksym Paziuk and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PostProcess/PostProcessMaterial.h"

// Forward Declarations
class FSceneView;

namespace CMAA2
{
	// Console variables to control CMAA2
	extern int32 GEnable;
	extern int32 GPlacement;

	// The main entry point for the CMAA2 render graph setup
	void AddCMAA2Pass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef Output);
}