#include "BPeekVersionCheck.h"
#include "BPeekLog.h"
#include "BPeekVersion.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

// ---------------------------------------------------------------------
// Manifest loader
// ---------------------------------------------------------------------

FBPeekExtensionManifest FBPeekVersionCheck::LoadManifest(const IPlugin& Plugin)
{
    FBPeekExtensionManifest Out;
    Out.PluginName = Plugin.GetName();

    // .uplugin files are JSON — load raw, parse, look for our custom
    // "BPeek" object. UE's FPluginDescriptor strips unknown fields, so
    // we can't get this through the normal descriptor path.
    const FString DescriptorPath = Plugin.GetDescriptorFileName();
    if (DescriptorPath.IsEmpty() || !FPaths::FileExists(DescriptorPath))
    {
        return Out;
    }

    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *DescriptorPath))
    {
        return Out;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return Out;
    }

    const TSharedPtr<FJsonObject>* BPeekObj = nullptr;
    if (!Root->TryGetObjectField(TEXT("BPeek"), BPeekObj) || !BPeekObj || !(*BPeekObj).IsValid())
    {
        // Not a BPeek extension — no "BPeek" field. Return with bPresent=false.
        return Out;
    }

    const TSharedPtr<FJsonObject>& B = *BPeekObj;
    Out.bPresent = true;
    B->TryGetStringField(TEXT("CoreVersionMin"),   Out.CoreVersionMin);
    B->TryGetStringField(TEXT("CoreVersionMax"),   Out.CoreVersionMax);
    B->TryGetStringField(TEXT("TargetPlugin"),     Out.TargetPlugin);
    B->TryGetStringField(TEXT("TargetVersionMin"), Out.TargetVersionMin);
    B->TryGetStringField(TEXT("TargetVersionMax"), Out.TargetVersionMax);
    return Out;
}

// ---------------------------------------------------------------------
// Compat checker
// ---------------------------------------------------------------------

namespace
{
    /** Parse a bound as optional semver. Empty string = "no bound" =
     *  unset optional. Invalid string = unset optional but with a
     *  flag so we can distinguish "parse failed" from "not set". */
    struct FBound
    {
        TOptional<FBPeekSemver> Value;
        bool bParseFailed = false;
        bool bSet = false;
    };

    FBound ParseBound(const FString& Raw)
    {
        FBound B;
        if (Raw.IsEmpty()) return B;
        B.bSet = true;
        B.Value = FBPeekSemver::Parse(Raw);
        B.bParseFailed = !B.Value.IsSet();
        return B;
    }
}

FBPeekCompatResult FBPeekVersionCheck::CheckCompat(
    const FBPeekExtensionManifest& Manifest,
    const FBPeekSemver& InstalledCoreVersion,
    const FBPeekSemver* InstalledTargetVersion)
{
    FBPeekCompatResult Result;

    if (!Manifest.bPresent)
    {
        // No manifest = not a BPeek extension. Don't gate it.
        return Result;
    }

    // --- Core range check ------------------------------------------
    const FBound MinCore = ParseBound(Manifest.CoreVersionMin);
    const FBound MaxCore = ParseBound(Manifest.CoreVersionMax);

    if (MinCore.bParseFailed || MaxCore.bParseFailed)
    {
        Result.bCompatible = false;
        Result.Reason = FString::Printf(
            TEXT("malformed CoreVersion%s in %s uplugin"),
            MinCore.bParseFailed ? TEXT("Min") : TEXT("Max"),
            *Manifest.PluginName);
        return Result;
    }

    if (MinCore.Value.IsSet() && InstalledCoreVersion < *MinCore.Value)
    {
        Result.bCompatible = false;
        Result.Reason = FString::Printf(
            TEXT("%s requires BPeek >= %s, installed %s"),
            *Manifest.PluginName,
            *MinCore.Value->ToString(),
            *InstalledCoreVersion.ToString());
        return Result;
    }
    // Max is exclusive (semver-range convention: <2.0.0 means "any 1.x").
    if (MaxCore.Value.IsSet() && !(InstalledCoreVersion < *MaxCore.Value))
    {
        Result.bCompatible = false;
        Result.Reason = FString::Printf(
            TEXT("%s requires BPeek < %s, installed %s"),
            *Manifest.PluginName,
            *MaxCore.Value->ToString(),
            *InstalledCoreVersion.ToString());
        return Result;
    }

    // --- Target-plugin range check ---------------------------------
    if (Manifest.TargetPlugin.IsEmpty())
    {
        return Result;  // no target declared — nothing to check
    }
    if (!InstalledTargetVersion)
    {
        Result.bCompatible = false;
        Result.Reason = FString::Printf(
            TEXT("%s requires target plugin '%s', not installed/not enabled"),
            *Manifest.PluginName, *Manifest.TargetPlugin);
        return Result;
    }

    const FBound MinTarget = ParseBound(Manifest.TargetVersionMin);
    const FBound MaxTarget = ParseBound(Manifest.TargetVersionMax);
    if (MinTarget.bParseFailed || MaxTarget.bParseFailed)
    {
        Result.bCompatible = false;
        Result.Reason = FString::Printf(
            TEXT("malformed TargetVersion%s in %s uplugin"),
            MinTarget.bParseFailed ? TEXT("Min") : TEXT("Max"),
            *Manifest.PluginName);
        return Result;
    }
    if (MinTarget.Value.IsSet() && *InstalledTargetVersion < *MinTarget.Value)
    {
        Result.bCompatible = false;
        Result.Reason = FString::Printf(
            TEXT("%s requires %s >= %s, installed %s"),
            *Manifest.PluginName, *Manifest.TargetPlugin,
            *MinTarget.Value->ToString(),
            *InstalledTargetVersion->ToString());
        return Result;
    }
    if (MaxTarget.Value.IsSet() && !(*InstalledTargetVersion < *MaxTarget.Value))
    {
        Result.bCompatible = false;
        Result.Reason = FString::Printf(
            TEXT("%s requires %s < %s, installed %s"),
            *Manifest.PluginName, *Manifest.TargetPlugin,
            *MaxTarget.Value->ToString(),
            *InstalledTargetVersion->ToString());
        return Result;
    }

    return Result;
}

// ---------------------------------------------------------------------
// Startup driver
// ---------------------------------------------------------------------

void FBPeekVersionCheck::RunStartupCheck()
{
    const TOptional<FBPeekSemver> CoreVer = FBPeekSemver::Parse(TEXT(BPEEK_PLUGIN_VERSION_NAME));
    if (!CoreVer.IsSet())
    {
        UE_LOG(LogBPeek, Error,
            TEXT("[ext] BPEEK_PLUGIN_VERSION_NAME '%s' is unparseable — version check disabled"),
            TEXT(BPEEK_PLUGIN_VERSION_NAME));
        return;
    }

    int32 Compatible = 0;
    int32 Incompatible = 0;
    int32 NonExtensions = 0;

    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
    {
        // Skip core itself so we don't self-check.
        if (Plugin->GetName() == TEXT("BPeek")) continue;

        FBPeekExtensionManifest M = LoadManifest(Plugin.Get());
        if (!M.bPresent) { ++NonExtensions; continue; }

        TOptional<FBPeekSemver> TargetVer;
        FBPeekSemver* TargetVerPtr = nullptr;
        if (!M.TargetPlugin.IsEmpty())
        {
            TSharedPtr<IPlugin> Tp = IPluginManager::Get().FindPlugin(M.TargetPlugin);
            if (!Tp.IsValid())
            {
                UE_LOG(LogBPeek, Verbose,
                    TEXT("[ext] %s target plugin '%s' not found by PluginManager"),
                    *M.PluginName, *M.TargetPlugin);
            }
            else if (!Tp->IsEnabled())
            {
                UE_LOG(LogBPeek, Verbose,
                    TEXT("[ext] %s target plugin '%s' is installed but not enabled"),
                    *M.PluginName, *M.TargetPlugin);
            }
            else
            {
                const FString VerStr = Tp->GetDescriptor().VersionName;
                TargetVer = FBPeekSemver::Parse(VerStr);
                if (TargetVer.IsSet())
                {
                    TargetVerPtr = &TargetVer.GetValue();
                }
                else
                {
                    UE_LOG(LogBPeek, Verbose,
                        TEXT("[ext] %s target plugin '%s' version '%s' unparseable — treating as missing"),
                        *M.PluginName, *M.TargetPlugin, *VerStr);
                }
            }
        }

        const FBPeekCompatResult R = CheckCompat(M, *CoreVer, TargetVerPtr);
        if (R.bCompatible)
        {
            ++Compatible;
            UE_LOG(LogBPeek, Verbose,
                TEXT("[ext] %s compat OK (core %s)"),
                *M.PluginName, *CoreVer->ToString());
        }
        else
        {
            ++Incompatible;
            UE_LOG(LogBPeek, Error,
                TEXT("[ext] %s INCOMPATIBLE: %s"),
                *M.PluginName, *R.Reason);
        }
    }

    UE_LOG(LogBPeek, Log,
        TEXT("[ext] version check: core=%s, extensions compat=%d incompat=%d non-ext=%d"),
        *CoreVer->ToString(), Compatible, Incompatible, NonExtensions);
}
