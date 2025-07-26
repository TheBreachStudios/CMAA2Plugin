// Copyright 2025 Maksym Paziuk and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "CMAA2Plugin.h"
#include "CMAA2PostProcess.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "PostProcess/PostProcessMaterial.h"
#include "PostProcess/PostProcessing.h"
#include "SceneViewExtension.h"

IMPLEMENT_MODULE(FCMAA2PluginModule, CMAA2Plugin)

//#if CMAA2_UE_VERSION_OLDER_THAN(5, 6)
//using FPostProcessingPassDelegateArray = FAfterPassCallbackDelegateArray;
//#endif


class FCMAA2ViewExtension : public FSceneViewExtensionBase
{
public:
	FCMAA2ViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}

	// These are required overrides from the base ISceneViewExtension interface.
	// We can leave them empty as we only need to hook into the post-processing pass.
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

//#if CMAA2_UE_VERSION_OLDER_THAN(4,27)
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) {}
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) {}
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& InOutInputs)
	{
		const FViewInfo& ViewInfo = (FViewInfo&)(View);
		const FIntRect ViewRect = ViewInfo.ViewRect;

		if (CMAA2::GEnable && View.AntiAliasingMethod == AAM_None)
		{
			InOutInputs.Validate();
			const FIntRect PrimaryViewRect = static_cast<const FViewInfo&>(View).ViewRect;
			FScreenPassTexture SceneColor((*InOutInputs.SceneTextures)->SceneColorTexture, PrimaryViewRect);

			if (!SceneColor.IsValid())
			{
				return;
			}

			FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
			OutputDesc.Flags |= TexCreate_UAV;
			// ToDo: Figure out a way to work with Input/Output.Texture in place
			FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("CMAA2.FinalOutput"));

			if (View.AntiAliasingMethod == AAM_None)
			{
				CMAA2::AddCMAA2Pass(GraphBuilder, View, SceneColor.Texture);
			}
		}
	}
//#endif


//#if CMAA2_UE_VERSION_OLDER_THAN(5, 4)
//	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override
//#else
//	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override
//#endif
//	{
//		// ToDo: Figure out better place to integrate into pipeline, EPostProcessingPass::FXAA works, but it is not the best place for CMAA2.
//		if (Pass == EPostProcessingPass::FXAA && CMAA2::GEnable)
//		{
//			InOutPassCallbacks.Add(
//				FAfterPassCallbackDelegate::CreateLambda([this](FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& InOutInputs)
//					{
//						const FViewInfo& ViewInfo = (FViewInfo&)(View);
//						const FIntRect ViewRect = ViewInfo.ViewRect;
//
//#if CMAA2_UE_VERSION_OLDER_THAN(5, 4)
//						const FScreenPassTexture& SceneColor = InOutInputs.GetInput(EPostProcessMaterialInput::SceneColor);
//#else
//						const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, InOutInputs.GetInput(EPostProcessMaterialInput::SceneColor));
//#endif
//						FScreenPassRenderTarget Output = InOutInputs.OverrideOutput;
//
//						FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
//						OutputDesc.Flags |= TexCreate_UAV;
//						// ToDo: Figure out a way to work with Input/Output.Texture in place
//						FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("CMAA2.FinalOutput"));
//						if (!Output.IsValid())
//						{
//							Output.ViewRect = SceneColor.ViewRect;
//#if CMAA2_UE_VERSION_OLDER_THAN(5, 3)
//	#if CMAA2_UE_VERSION_NEWER_THAN(5, 0)
//							Output.LoadAction = ViewInfo.GetOverwriteLoadAction();
//	#else
//							Output.LoadAction = ERenderTargetLoadAction::ENoAction;
//	#endif
//#else
//							Output.LoadAction = View.GetOverwriteLoadAction();
//#endif
//							Output.Texture = OutputTexture;
//						}
//
//						if (View.AntiAliasingMethod == AAM_None)
//						{
//							AddCopyTexturePass(GraphBuilder, SceneColor.Texture, OutputTexture, ViewRect.Min, ViewRect.Min, ViewRect.Size());
//
//							CMAA2::AddCMAA2Pass(GraphBuilder, View, OutputTexture);
//
//							if (OutputTexture != Output.Texture)
//							{
//								AddCopyTexturePass(GraphBuilder, OutputTexture, Output.Texture, ViewRect.Min, ViewRect.Min, ViewRect.Size());
//							}
//						}
//						else
//						{
//							// We are forced to make a write, and in UE_VERSION_OLDER_THAN(5, 3, 0) SubscribeToPostProcessingPass does not provide a way to check that beforehand
//							AddCopyTexturePass(GraphBuilder, SceneColor.Texture, Output.Texture);
//						}
//
//						return MoveTemp(Output);
//					})
//			);
//		}
//	}
};


void FCMAA2PluginModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("CMAA2Plugin"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/CMAA2Plugin"), PluginShaderDir);
    
	IRendererModule* RendererModule = &FModuleManager::LoadModuleChecked<IRendererModule>(TEXT("Renderer"));
	OnPostEngineInitDelegateHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FCMAA2PluginModule::InitCMAA2ViewExtension);
}

void FCMAA2PluginModule::ShutdownModule()
{
	if (OnPostEngineInitDelegateHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(OnPostEngineInitDelegateHandle);
	}

	CMAA2ViewExtension.Reset();
}


void FCMAA2PluginModule::InitCMAA2ViewExtension()
{
	CMAA2ViewExtension = FSceneViewExtensions::NewExtension<FCMAA2ViewExtension>();
}