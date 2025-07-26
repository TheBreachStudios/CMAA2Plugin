// Copyright 2025 Maksym Paziuk and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using System.IO;

namespace UnrealBuildTool.Rules
{
    public class CMAA2Plugin : ModuleRules
    {
        public CMAA2Plugin(ReadOnlyTargetRules Target) : base(Target)
        {
            PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",		
                }
            );

            var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

            PrivateIncludePaths.AddRange(
                new string[] {
				//required for FScreenPassVS, AddDrawScreenPass, and for scene view extensions related headers
				Path.Combine(EngineDir, "Source", "Runtime", "Renderer", "Private")
            });

            if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 3)
            {
                // For UE5.3 and later, we need to include the InternalIncludePaths for Renderer
                // to access certain internal headers.
                PrivateIncludePaths.AddRange(
                    new string[] {
                        Path.Combine(EngineDir, "Source", "Runtime", "Renderer", "Internal")
                    });
            }

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "CoreUObject",
                    "Engine",
                    "Projects",
				    "RenderCore",
                    "Renderer",
                    "RHI"
                }
            );
        }
    }
}