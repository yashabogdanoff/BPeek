#pragma once
#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

/**
 * Per-project MD5 hash ledger used by the `-only-changed` incremental
 * regen mode. Stored on disk as `_bpeek_hashes.json` inside the output
 * directory:
 *
 *   {
 *     "/Game/A/BP_Foo.BP_Foo": "d41d8cd98f00b204e9800998ecf8427e",
 *     "/Game/B/DT_Bar.DT_Bar": "...hex...",
 *     ...
 *   }
 *
 * Usage:
 *   1. FBPeekHashStore Store;
 *   2. Store.Load(OutputDir);                    // loads prior hashes
 *   3. for each asset:
 *        bool bChanged = Store.VisitAsset(...);  // updates current hash
 *        if (bChanged) regen MD
 *   4. Store.GetDeletedAssets();                 // prior − current = deletes
 *   5. Store.Save(OutputDir);                    // writes new ledger
 *
 * Hash choice: MD5 over raw .uasset bytes. 30 MB hashes in ~50 ms, which
 * is negligible vs the 50 ms–3 s it saves per un-changed BP. Not used
 * cryptographically — we just need a stable fingerprint robust to
 * file-system touches (checkout, rename round-trips) that mtime isn't.
 */
class FBPeekHashStore
{
public:
    /** Load `_bpeek_hashes.json` from Dir. Missing file = empty prior map. */
    void Load(const FString& Dir)
    {
        PriorHashes.Empty();
        const FString Path = Dir / TEXT("_bpeek_hashes.json");
        FString Content;
        if (!FFileHelper::LoadFileToString(Content, *Path)) return;

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

        for (const auto& KV : Root->Values)
        {
            FString V;
            if (KV.Value.IsValid() && KV.Value->TryGetString(V))
                PriorHashes.Add(KV.Key, V);
        }
    }

    /** Serialise the current-run hash map to disk. Call after every
     *  asset was visited so the next incremental run has a baseline. */
    bool Save(const FString& Dir) const
    {
        TArray<FString> Keys;
        CurrentHashes.GetKeys(Keys);
        Keys.Sort();

        FString Json = TEXT("{\n");
        for (int32 i = 0; i < Keys.Num(); ++i)
        {
            Json += FString::Printf(TEXT("  \"%s\": \"%s\""),
                *Keys[i], *CurrentHashes[Keys[i]]);
            if (i + 1 < Keys.Num()) Json += TEXT(",");
            Json += TEXT("\n");
        }
        Json += TEXT("}\n");

        const FString Path = Dir / TEXT("_bpeek_hashes.json");
        return FFileHelper::SaveStringToFile(
            Json, *Path,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    /**
     * Record a visit to an asset. Computes MD5 of its .uasset on disk,
     * stores under CurrentHashes[AssetPath], compares against PriorHashes.
     * Returns true if the asset changed (or is new/unreadable).
     *
     * When the .uasset can't be located or hashed (rare — transient
     * assets, external packages), we return true so the caller regens
     * the MD anyway. Better safe than stale.
     */
    bool VisitAsset(UObject* Asset)
    {
        if (!Asset) return true;
        const FString AssetPath = Asset->GetPathName();

        const UPackage* Pkg = Asset->GetPackage();
        if (!Pkg)
        {
            CurrentHashes.Add(AssetPath, TEXT("?"));
            return true;
        }

        // Try .uasset first, then .umap — ULevel / UWorld ship as .umap
        // so a single extension lookup would miss them and leave hash=?,
        // forcing perpetual regen every run.
        FString FilePath;
        const bool bFound =
            FPackageName::TryConvertLongPackageNameToFilename(
                Pkg->GetName(), FilePath,
                FPackageName::GetAssetPackageExtension())
                && FPaths::FileExists(FilePath);
        if (!bFound)
        {
            FPackageName::TryConvertLongPackageNameToFilename(
                Pkg->GetName(), FilePath,
                FPackageName::GetMapPackageExtension());
        }
        if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
        {
            CurrentHashes.Add(AssetPath, TEXT("?"));
            return true;
        }

        // HashFile streams through the file directly — no need to load
        // the whole uasset into memory. Returns invalid FMD5Hash on
        // missing file, which we treat as "changed" so the caller regens.
        const FMD5Hash H = FMD5Hash::HashFile(*FilePath);
        if (!H.IsValid())
        {
            CurrentHashes.Add(AssetPath, TEXT("?"));
            return true;
        }
        const FString HashStr = LexToString(H);
        CurrentHashes.Add(AssetPath, HashStr);

        const FString* Prior = PriorHashes.Find(AssetPath);
        if (!Prior) return true;                 // new asset since last run
        return *Prior != HashStr;                // hash differs
    }

    /** Assets present in the prior ledger but not revisited this run —
     *  candidates for orphan MD removal. */
    TArray<FString> GetDeletedAssets() const
    {
        TArray<FString> Out;
        for (const auto& KV : PriorHashes)
            if (!CurrentHashes.Contains(KV.Key))
                Out.Add(KV.Key);
        return Out;
    }

    int32 NumCurrent() const { return CurrentHashes.Num(); }
    int32 NumPrior()   const { return PriorHashes.Num();   }

private:
    TMap<FString, FString> PriorHashes;    // loaded from disk
    TMap<FString, FString> CurrentHashes;  // accumulated this run
};
