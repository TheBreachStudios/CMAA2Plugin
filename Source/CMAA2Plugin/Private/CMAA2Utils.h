#pragma once

#include "Runtime/Launch/Resources/Version.h"

// Basically a modified version of the Unreal Engine version macros utils but without patch version support that introduced confusion and just made code inconsistent

// Helper for UE_VERSION_NEWER_THAN and UE_VERSION_OLDER_THAN
#define CMAA2_UE_GREATER_SORT(Value, ValueToBeGreaterThan, TieBreaker) \
	(((Value) > (ValueToBeGreaterThan)) || (((Value) == (ValueToBeGreaterThan)) && (TieBreaker)))

// Version comparison macro that is defined to true if the UE version is newer or equal than MajorVer.MinorVer.PatchVer 
// and false otherwise
// (a typical use is for backward compatible code)
#define CMAA2_UE_VERSION_NEWER_THAN_OR_EQUAL(MajorVersion, MinorVersion)\
	CMAA2_UE_GREATER_SORT(ENGINE_MAJOR_VERSION, MajorVersion, CMAA2_UE_GREATER_SORT(ENGINE_MINOR_VERSION, MinorVersion, true))

// Version comparison macro that is defined to true if the UE version is newer than MajorVer.MinorVer.PatchVer and false otherwise
// (a typical use is to alert integrators to revisit this code when upgrading to a new engine version)
#define CMAA2_UE_VERSION_NEWER_THAN(MajorVersion, MinorVersion) \
	CMAA2_UE_GREATER_SORT(ENGINE_MAJOR_VERSION, MajorVersion, CMAA2_UE_GREATER_SORT(ENGINE_MINOR_VERSION, MinorVersion, false))

// Version comparison macro that is defined to true if the UE version is older than MajorVer.MinorVer.PatchVer and false otherwise
// (use when making code that needs to be compatible with older engine versions)
#define CMAA2_UE_VERSION_OLDER_THAN(MajorVersion, MinorVersion) \
	CMAA2_UE_GREATER_SORT(MajorVersion, ENGINE_MAJOR_VERSION, CMAA2_UE_GREATER_SORT(MinorVersion, ENGINE_MINOR_VERSION, false))