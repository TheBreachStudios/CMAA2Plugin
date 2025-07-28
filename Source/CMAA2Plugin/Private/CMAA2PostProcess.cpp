// Copyright 2025 Maksym Paziuk and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "CMAA2PostProcess.h"
#include "CMAA2Utils.h"
#include "SceneRendering.h"
#if CMAA2_UE_VERSION_NEWER_THAN(5, 1)
	#include "DataDrivenShaderPlatformInfo.h"
#endif


// Longest line search distance; must be even number; for high perf low quality start from ~32 - the bigger the number, 
// the nicer the gradients but more costly. Max supported is 128!
#ifndef CMAA2_MAX_LINE_LENGTH
#define CMAA2_MAX_LINE_LENGTH 86
#endif

// Utils for UE 4.27
#if CMAA2_UE_VERSION_OLDER_THAN(5, 0)
#define SRGB TexCreate_SRGB

bool IsFloatFormat(EPixelFormat Format)
{
	switch (Format)
	{
	case PF_A32B32G32R32F:
	case PF_FloatR11G11B10:
	case PF_FloatRGB:
	case PF_FloatRGBA:
	case PF_G16R16F_FILTER:
	case PF_G16R16F:
	case PF_G32R32F:
	case PF_R16F_FILTER:
	case PF_R16F:
	case PF_R16G16B16A16_SNORM:
	case PF_R16G16B16A16_UNORM:
	case PF_R32_FLOAT:
	case PF_R5G6B5_UNORM:
	case PF_R8G8B8A8_SNORM:
		return true;
	default:
		return false;
	}
}

struct FUintVector2 {
	uint32 X;
	uint32 Y;
};
#endif

namespace CMAA2
{
	int32 GEnable = 1;
	static FAutoConsoleVariableRef CVarEnableCMAA2(
		TEXT("r.CMAA2.Enable"),
		GEnable,
		TEXT("Enable CMAA2 post-process anti-aliasing.\n")
		TEXT("0: Disabled (default)\n")
		TEXT("1: Enabled\n")
		TEXT("For this to work, set r.AntiAliasingMethod=0 to disable the default TAA/FXAA."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarCMAA2Quality(
		TEXT("r.CMAA2.Quality"),
		2, // Default to HIGH
		TEXT("Sets the quality preset for CMAA2. 0: LOW, 1: MEDIUM, 2: HIGH, 3: ULTRA."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarCMAA2ExtraSharpness(
		TEXT("r.CMAA2.ExtraSharpness"),
		0,
		TEXT("Set to 1 to preserve more text and shape clarity at the expense of less AA."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarCMAA2Debug(
		TEXT("r.CMAA2.Debug"),
		0,
		TEXT("Set to 1 to enable debug visualization of detected edges."),
		ECVF_RenderThreadSafe);
}


// Base shader class to handle shared permutations
class FCMAA2Shader : public FGlobalShader
{
public:
	class FQualityDim : SHADER_PERMUTATION_INT("CMAA2_STATIC_QUALITY_PRESET", 4);
	class FSharpnessDim : SHADER_PERMUTATION_BOOL("CMAA2_EXTRA_SHARPNESS");
	class FUAVStoreTypedDim : SHADER_PERMUTATION_BOOL("CMAA2_UAV_STORE_TYPED");
	class FUAVStoreTypedUnormFloatDim : SHADER_PERMUTATION_BOOL("CMAA2_UAV_STORE_TYPED_UNORM_FLOAT");
	class FUAVStoreConvertToSRGBDim : SHADER_PERMUTATION_BOOL("CMAA2_UAV_STORE_CONVERT_TO_SRGB");
	class FUAVStoreUntypedFormatDim : SHADER_PERMUTATION_INT("CMAA2_UAV_STORE_UNTYPED_FORMAT", 3); // 0=none, 1=R8G8B8A8, 2=R10G10B10A2
	class FHDRDim : SHADER_PERMUTATION_BOOL("CMAA2_SUPPORT_HDR_COLOR_RANGE");
	class FLumaPathDim : SHADER_PERMUTATION_INT("CMAA2_EDGE_DETECTION_LUMA_PATH", 2); // 0, 1

	using FPermutationDomain = TShaderPermutationDomain<
		FQualityDim,
		FSharpnessDim,
		FUAVStoreTypedDim,
		FUAVStoreTypedUnormFloatDim,
		FUAVStoreConvertToSRGBDim,
		FUAVStoreUntypedFormatDim,
		FHDRDim,
		FLumaPathDim>;

	FCMAA2Shader() = default;
	FCMAA2Shader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FUAVStoreTypedDim>())
		{
			if (PermutationVector.Get<FUAVStoreUntypedFormatDim>() != 0)
			{
				return false; // Untyped format is irrelevant for typed stores
			}
		}
		else // Not typed
		{
			if (PermutationVector.Get<FUAVStoreTypedUnormFloatDim>())
			{
				return false; // Typed unorm/float is irrelevant for untyped stores
			}
			if (PermutationVector.Get<FUAVStoreUntypedFormatDim>() == 0)
			{
				return false; // Must select an untyped format
			}
		}
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("CMAA_MSAA_SAMPLE_COUNT"), 1);
		OutEnvironment.SetDefine(TEXT("CMAA2_MAX_LINE_LENGTH"), CMAA2_MAX_LINE_LENGTH);
	}
};

// Shader for the first pass: Edge Detection
class FCMAA2EdgesColor2x2CS : public FCMAA2Shader
{
	DECLARE_GLOBAL_SHADER(FCMAA2EdgesColor2x2CS);
	SHADER_USE_PARAMETER_STRUCT(FCMAA2EdgesColor2x2CS, FCMAA2Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, g_inoutColorReadonly)
		SHADER_PARAMETER_SAMPLER(SamplerState, g_gather_point_clamp_Sampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, g_workingEdges)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, g_workingShapeCandidates)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, g_workingDeferredBlendItemListHeads)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_workingControlBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCMAA2EdgesColor2x2CS, "/CMAA2Plugin/CMAA2.usf", "EdgesColor2x2CS", SF_Compute);

// Shader for the second pass: Process Shape Candidates
class FCMAA2ProcessCandidatesCS : public FCMAA2Shader
{
	DECLARE_GLOBAL_SHADER(FCMAA2ProcessCandidatesCS);
	SHADER_USE_PARAMETER_STRUCT(FCMAA2ProcessCandidatesCS, FCMAA2Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, g_inoutColorReadonly)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, g_workingEdges)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer<uint>, g_workingShapeCandidates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_workingControlBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, g_workingDeferredBlendItemListHeads)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, g_workingDeferredBlendItemList)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, g_workingDeferredBlendLocationList)
#if CMAA2_UE_VERSION_NEWER_THAN(4,27)
		RDG_BUFFER_ACCESS(IndirectDispatchArgsBuffer, ERHIAccess::IndirectArgs)
#else
		RDG_BUFFER_ACCESS(IndirectDispatchArgsBuffer, ERHIAccess::EReadable)
#endif
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCMAA2ProcessCandidatesCS, "/CMAA2Plugin/CMAA2.usf", "ProcessCandidatesCS", SF_Compute);

// Shader for the third pass: Deferred Color Application
class FCMAA2DeferredColorApply2x2CS : public FCMAA2Shader
{
	DECLARE_GLOBAL_SHADER(FCMAA2DeferredColorApply2x2CS);
	SHADER_USE_PARAMETER_STRUCT(FCMAA2DeferredColorApply2x2CS, FCMAA2Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, g_workingDeferredBlendLocationList)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, g_workingDeferredBlendItemListHeads)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, g_workingDeferredBlendItemList)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_workingControlBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, g_inoutColorWriteonly)
#if CMAA2_UE_VERSION_NEWER_THAN(4,27)
		RDG_BUFFER_ACCESS(IndirectDispatchArgsBuffer, ERHIAccess::IndirectArgs)
#else
		RDG_BUFFER_ACCESS(IndirectDispatchArgsBuffer, ERHIAccess::EReadable)
#endif
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCMAA2DeferredColorApply2x2CS, "/CMAA2Plugin/CMAA2.usf", "DeferredColorApply2x2CS", SF_Compute);

// Shader for computing indirect dispatch arguments
class FCMAA2ComputeDispatchArgsCS : public FCMAA2Shader
{
	DECLARE_GLOBAL_SHADER(FCMAA2ComputeDispatchArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FCMAA2ComputeDispatchArgsCS, FCMAA2Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_workingControlBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, g_workingExecuteIndirectBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, g_workingShapeCandidates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, g_workingDeferredBlendLocationList)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCMAA2ComputeDispatchArgsCS, "/CMAA2Plugin/CMAA2.usf", "ComputeDispatchArgsCS", SF_Compute);

// Shader for debug visualization
class FCMAA2DebugDrawEdgesCS : public FCMAA2Shader
{
	DECLARE_GLOBAL_SHADER(FCMAA2DebugDrawEdgesCS);
	SHADER_USE_PARAMETER_STRUCT(FCMAA2DebugDrawEdgesCS, FCMAA2Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, g_workingEdges)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, g_inoutColorWriteonly)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCMAA2DebugDrawEdgesCS, "/CMAA2Plugin/CMAA2.usf", "DebugDrawEdgesCS", SF_Compute);


void CMAA2::AddCMAA2Pass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef Output)
{
	const FIntPoint RenderExtent = ((FViewInfo&)(View)).ViewRect.Size();

	int32 Quality = FMath::Clamp(CVarCMAA2Quality.GetValueOnRenderThread(), 0, 3);
	RDG_EVENT_SCOPE(GraphBuilder, "CMAA2 %dx%d Quality: %d", RenderExtent.X, RenderExtent.Y, Quality);

	FCMAA2Shader::FPermutationDomain PermutationVector;
	PermutationVector.Set<FCMAA2Shader::FQualityDim>(Quality);
	PermutationVector.Set<FCMAA2Shader::FSharpnessDim>(CVarCMAA2ExtraSharpness.GetValueOnRenderThread() != 0);
	PermutationVector.Set<FCMAA2Shader::FHDRDim>(IsFloatFormat(Output->Desc.Format));
	PermutationVector.Set<FCMAA2Shader::FLumaPathDim>(1);

#if CMAA2_UE_VERSION_NEWER_THAN(4,27)
	// Determine UAV storage strategy
	const FPixelFormatInfo& FormatInfo = GPixelFormats[Output->Desc.Format];
	EPixelFormat NonSRGBFormat = Output->Desc.Format;
	const FPixelFormatInfo& NonSRGBFormatInfo = GPixelFormats[NonSRGBFormat];
	bool bIsSRGB = uint64(Output->Desc.Flags & ETextureCreateFlags::SRGB) != 0;
	EPixelFormat UAVFormat = Output->Desc.Format; // Use the format of our working texture

	if (int(NonSRGBFormatInfo.Capabilities & EPixelFormatCapabilities::TypedUAVStore) != 0)
	{
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedDim>(true);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedUnormFloatDim>(!IsFloatFormat(UAVFormat));
		PermutationVector.Set<FCMAA2Shader::FUAVStoreConvertToSRGBDim>(false);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreUntypedFormatDim>(0);
	}
	else if (bIsSRGB && int(NonSRGBFormatInfo.Capabilities & EPixelFormatCapabilities::TypedUAVStore))
	{
		UAVFormat = NonSRGBFormat;
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedDim>(true);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedUnormFloatDim>(!IsFloatFormat(UAVFormat));
		PermutationVector.Set<FCMAA2Shader::FUAVStoreConvertToSRGBDim>(true);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreUntypedFormatDim>(0);
	}
	else
	{
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedDim>(false);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedUnormFloatDim>(false);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreConvertToSRGBDim>(bIsSRGB);
		UAVFormat = PF_R32_UINT;

		if (NonSRGBFormat == PF_B8G8R8A8 || NonSRGBFormat == PF_R8G8B8A8)
		{
			PermutationVector.Set<FCMAA2Shader::FUAVStoreUntypedFormatDim>(1);
		}
		else if (NonSRGBFormat == PF_A2B10G10R10)
		{
			PermutationVector.Set<FCMAA2Shader::FUAVStoreUntypedFormatDim>(2);
		}
		else
		{
			return;
		}
	}
#else
	// Manual format info retrieval
	EPixelFormat Format = Output->Desc.Format;
	bool bIsSRGB = EnumHasAnyFlags(Output->Desc.Flags, TexCreate_SRGB);

	// Start with default assumption
	EPixelFormat UAVFormat = Format;

	// Check for known formats that support typed UAVs in UE 4.27
	const bool bTypedUAVSupported =
		Format == PF_R32_FLOAT ||
		Format == PF_R16F ||
		Format == PF_R32_UINT ||
		Format == PF_FloatRGBA ||
		Format == PF_B8G8R8A8 ||
		Format == PF_R8G8B8A8 ||
		Format == PF_A2B10G10R10;

	// Initialize permutation vector
	PermutationVector.Set<FCMAA2Shader::FUAVStoreConvertToSRGBDim>(false);
	PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedDim>(false);
	PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedUnormFloatDim>(false);
	PermutationVector.Set<FCMAA2Shader::FUAVStoreUntypedFormatDim>(0);

	if (bTypedUAVSupported)
	{
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedDim>(true);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedUnormFloatDim>(!IsFloatFormat(Format));
	}
	else if (bIsSRGB &&
		(Format == PF_B8G8R8A8 || Format == PF_R8G8B8A8) &&
		// Assume non-SRGB variant of these formats support UAVs
		true)
	{
		// Use non-sRGB variant
		UAVFormat = Format; // Same format, but omit SRGB for UAV
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedDim>(true);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedUnormFloatDim>(true);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreConvertToSRGBDim>(true);
	}
	else
	{
		// Fall back to untyped UAV
		UAVFormat = PF_R32_UINT;

		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedDim>(false);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreTypedUnormFloatDim>(false);
		PermutationVector.Set<FCMAA2Shader::FUAVStoreConvertToSRGBDim>(bIsSRGB);

		if (Format == PF_B8G8R8A8 || Format == PF_R8G8B8A8)
		{
			PermutationVector.Set<FCMAA2Shader::FUAVStoreUntypedFormatDim>(1);
		}
		else if (Format == PF_A2B10G10R10)
		{
			PermutationVector.Set<FCMAA2Shader::FUAVStoreUntypedFormatDim>(2);
		}
		else
		{
			// Unsupported fallback
			return;
		}
	}
#endif

	const int32 EdgesResX = (RenderExtent.X + 1) / 2;
	FRDGTextureDesc EdgesDesc = FRDGTextureDesc::Create2D(FIntPoint(EdgesResX, RenderExtent.Y), PF_R8_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef WorkingEdges = GraphBuilder.CreateTexture(EdgesDesc, TEXT("CMAA2.WorkingEdges"));

	FRDGTextureDesc ListHeadsDesc = FRDGTextureDesc::Create2D(FIntPoint((RenderExtent.X + 1) / 2, (RenderExtent.Y + 1) / 2), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef WorkingDeferredBlendItemListHeads = GraphBuilder.CreateTexture(ListHeadsDesc, TEXT("CMAA2.WorkingDeferredBlendItemListHeads"));

	const int32 RequiredCandidatePixels = RenderExtent.X * RenderExtent.Y / 4;
	FRDGBufferRef WorkingShapeCandidates = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), RequiredCandidatePixels), TEXT("CMAA2.WorkingShapeCandidates"));

	const int32 RequiredDeferredColorApplyBuffer = RenderExtent.X * RenderExtent.Y / 2;
	FRDGBufferRef WorkingDeferredBlendItemList = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FUintVector2), RequiredDeferredColorApplyBuffer), TEXT("CMAA2.WorkingDeferredBlendItemList"));

	const int32 RequiredListHeadsPixels = (RenderExtent.X * RenderExtent.Y + 3) / 6;
	FRDGBufferRef WorkingDeferredBlendLocationList = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), RequiredListHeadsPixels), TEXT("CMAA2.WorkingDeferredBlendLocationList"));

	FRDGBufferRef WorkingControlBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(16 * sizeof(uint32)), TEXT("CMAA2.WorkingControlBuffer"));
#if CMAA2_UE_VERSION_NEWER_THAN(5, 1)
	FRDGBufferDesc IndirectArgsDesc = FRDGBufferDesc::CreateIndirectDesc(4, 128);
#else
	FRDGBufferDesc IndirectArgsDesc = FRDGBufferDesc::CreateIndirectDesc(128);
#endif
	FRDGBufferRef WorkingExecuteIndirectBuffer = GraphBuilder.CreateBuffer(IndirectArgsDesc, TEXT("CMAA2.WorkingExecuteIndirectBuffer"));

	// Initial clear passes
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(WorkingControlBuffer), 0);

	// PASS 1: Edge Detection. This pass populates the shape candidates buffer and increments the counter in the control buffer.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FCMAA2EdgesColor2x2CS::FParameters>();
#if CMAA2_UE_VERSION_NEWER_THAN(5, 0)
		PassParameters->g_inoutColorReadonly = GraphBuilder.CreateSRV(Output);
#else
		PassParameters->g_inoutColorReadonly = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Output));
#endif
		PassParameters->g_gather_point_clamp_Sampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->g_workingEdges = GraphBuilder.CreateUAV(WorkingEdges);
		PassParameters->g_workingShapeCandidates = GraphBuilder.CreateUAV(WorkingShapeCandidates);
		PassParameters->g_workingDeferredBlendItemListHeads = GraphBuilder.CreateUAV(WorkingDeferredBlendItemListHeads);
		PassParameters->g_workingControlBuffer = GraphBuilder.CreateUAV(WorkingControlBuffer);

		TShaderMapRef<FCMAA2EdgesColor2x2CS> ComputeShader(((FViewInfo&)(View)).ShaderMap, PermutationVector);
		const int32 csOutputKernelSizeX = 14;
		const int32 csOutputKernelSizeY = 14;
		FIntVector GroupCount = FIntVector(FMath::DivideAndRoundUp(RenderExtent.X, csOutputKernelSizeX * 2), FMath::DivideAndRoundUp(RenderExtent.Y, csOutputKernelSizeY * 2), 1);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CMAA2 EdgesColor2x2"), ComputeShader, PassParameters, GroupCount);
	}

	// PASS 2: Compute Dispatch Arguments for ProcessCandidates. This reads the counter filled by the previous pass.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FCMAA2ComputeDispatchArgsCS::FParameters>();
		PassParameters->g_workingControlBuffer = GraphBuilder.CreateUAV(WorkingControlBuffer);
		PassParameters->g_workingExecuteIndirectBuffer = GraphBuilder.CreateUAV(WorkingExecuteIndirectBuffer);
		PassParameters->g_workingShapeCandidates = GraphBuilder.CreateUAV(WorkingShapeCandidates);
		PassParameters->g_workingDeferredBlendLocationList = GraphBuilder.CreateUAV(WorkingDeferredBlendLocationList);
		TShaderMapRef<FCMAA2ComputeDispatchArgsCS> ComputeShader(((FViewInfo&)(View)).ShaderMap, PermutationVector);
		// Dispatch(2,1,1) triggers the groupID.x == 1 path in the shader to process shape candidates count.
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CMAA2 ComputeDispatchArgs (Process)"), ComputeShader, PassParameters, FIntVector(2, 1, 1));
	}

	// PASS 3: Process Shape Candidates (Indirect). This is launched with the correct arguments computed in the previous step.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FCMAA2ProcessCandidatesCS::FParameters>();
#if CMAA2_UE_VERSION_NEWER_THAN(5, 0)
		PassParameters->g_inoutColorReadonly = GraphBuilder.CreateSRV(Output);
#else
		PassParameters->g_inoutColorReadonly = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Output));
#endif

		PassParameters->g_workingEdges = GraphBuilder.CreateUAV(WorkingEdges);
		PassParameters->g_workingShapeCandidates = GraphBuilder.CreateUAV(WorkingShapeCandidates);
		PassParameters->g_workingControlBuffer = GraphBuilder.CreateUAV(WorkingControlBuffer);
		PassParameters->g_workingDeferredBlendItemListHeads = GraphBuilder.CreateUAV(WorkingDeferredBlendItemListHeads);
		PassParameters->g_workingDeferredBlendItemList = GraphBuilder.CreateUAV(WorkingDeferredBlendItemList);
		PassParameters->g_workingDeferredBlendLocationList = GraphBuilder.CreateUAV(WorkingDeferredBlendLocationList);
		PassParameters->IndirectDispatchArgsBuffer = WorkingExecuteIndirectBuffer;
		TShaderMapRef<FCMAA2ProcessCandidatesCS> ComputeShader(((FViewInfo&)(View)).ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CMAA2 ProcessCandidates"), ComputeShader, PassParameters, WorkingExecuteIndirectBuffer, 0);
	}

	// PASS 4: Compute Dispatch Arguments for DeferredColorApply. This reads the blend location counter filled by ProcessCandidates.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FCMAA2ComputeDispatchArgsCS::FParameters>();
		PassParameters->g_workingControlBuffer = GraphBuilder.CreateUAV(WorkingControlBuffer);
		PassParameters->g_workingExecuteIndirectBuffer = GraphBuilder.CreateUAV(WorkingExecuteIndirectBuffer);
		PassParameters->g_workingShapeCandidates = GraphBuilder.CreateUAV(WorkingShapeCandidates);
		PassParameters->g_workingDeferredBlendLocationList = GraphBuilder.CreateUAV(WorkingDeferredBlendLocationList);
		TShaderMapRef<FCMAA2ComputeDispatchArgsCS> ComputeShader(((FViewInfo&)(View)).ShaderMap, PermutationVector);
		// Dispatch(1,2,1) triggers the groupID.y == 1 path in the shader to process blend location list count.
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CMAA2 ComputeDispatchArgs (Apply)"), ComputeShader, PassParameters, FIntVector(1, 2, 1));
	}

	// PASS 5: Deferred Color Apply (Indirect). This applies the final blended colors to the output texture.
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FCMAA2DeferredColorApply2x2CS::FParameters>();
		PassParameters->g_workingDeferredBlendLocationList = GraphBuilder.CreateUAV(WorkingDeferredBlendLocationList);
		PassParameters->g_workingDeferredBlendItemListHeads = GraphBuilder.CreateUAV(WorkingDeferredBlendItemListHeads);
		PassParameters->g_workingDeferredBlendItemList = GraphBuilder.CreateUAV(WorkingDeferredBlendItemList);
		PassParameters->g_workingControlBuffer = GraphBuilder.CreateUAV(WorkingControlBuffer);
		PassParameters->g_inoutColorWriteonly = GraphBuilder.CreateUAV(Output);
		PassParameters->IndirectDispatchArgsBuffer = WorkingExecuteIndirectBuffer;
		TShaderMapRef<FCMAA2DeferredColorApply2x2CS> ComputeShader(((FViewInfo&)(View)).ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CMAA2 DeferredColorApply"), ComputeShader, PassParameters, WorkingExecuteIndirectBuffer, 0);
	}

	// PASS 6: Debug (Optional)
	if (CVarCMAA2Debug.GetValueOnRenderThread() != 0)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FCMAA2DebugDrawEdgesCS::FParameters>();
		PassParameters->g_workingEdges = GraphBuilder.CreateUAV(WorkingEdges);
		PassParameters->g_inoutColorWriteonly = GraphBuilder.CreateUAV(Output);
		TShaderMapRef<FCMAA2DebugDrawEdgesCS> ComputeShader(((FViewInfo&)(View)).ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CMAA2 DebugDrawEdges"), ComputeShader, PassParameters, FComputeShaderUtils::GetGroupCount(RenderExtent, FIntPoint(16, 16)));
	}
}