#pragma once
#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "UObject/LinkerLoad.h"
#include "UObject/ObjectResource.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "BPeekAssetPathHelpers.h"

/**
 * Resolves UClass → markdown fragment pointing to its C++ header file.
 * Used by per-asset writers to emit "→ [Source](...)" links next to
 * parent-class / interface / functions-called lines.
 *
 * Returns one of:
 *   - `[Source](../../Source/X/Y.h)` — in-project class, header resolved.
 *   - `[engine]`                     — class owned by engine or plugin
 *                                      outside project (header not linkable
 *                                      because path is local UE install).
 *   - `[precompiled]`                — project-owned module but source missing
 *                                      (precompiled DLL distribution).
 *   - empty                          — null input.
 *
 * Relative path anchor is the MD file location: at write-time MD lives at
 * `<project>/bpeek/<asset-subpath>.md`, so depth = 1 (bpeek/) + slash-count
 * in the mount sub-path. Link remains informative even after the MD tree is
 * moved out of the project — the string tells agents exactly where in the
 * project source the header is, even if the relative `../` prefix no longer
 * resolves.
 */
class FBPeekCppSource
{
public:
    /** Number of directory levels between the project root and the MD
     *  output tree. Saved/BPeek/ → 2, bpeek/ → 1, custom → computed.
     *  Commandlet sets this once before any MD is written. Default 2
     *  matches the current Saved/BPeek/ output default. */
    inline static int32 OutputDirDepth = 2;

    /** Resolve class → `→ [Source](...)` or fallback label. AssetPath is
     *  the BP's mount path (`/Game/X/BP.BP`), used to compute `../` depth. */
    static FString ResolveClassLink(const UClass* Cls, const FString& AssetPath)
    {
        if (!Cls) return FString();

        // Blueprint-generated classes (B_Hero_Default_C, GA_Foo_C, …)
        // have no C++ header by design — their source is the BP uasset
        // itself. Point at the corresponding MD file instead of
        // falling through to `[engine]` (which was the bug on Lyra).
        if (const UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Cls))
        {
            if (UBlueprint* BP = Cast<UBlueprint>(BPGC->ClassGeneratedBy))
            {
                const FString Rel = FBPeekAssetPath::RelativeMdPath(
                    AssetPath, BP->GetPathName());
                return FString::Printf(TEXT("[%s](%s)"), *BP->GetName(), *Rel);
            }
            // BPGC with no ClassGeneratedBy — very unusual, fall through.
        }

        FString HeaderAbs;
        if (!FSourceCodeNavigation::FindClassHeaderPath(Cls, HeaderAbs) ||
            HeaderAbs.IsEmpty())
        {
            // Classes without a discoverable header are typically native
            // engine types whose source isn't visible via SourceCodeAccess
            // (stripped install, or cross-platform layout).
            return TEXT("[engine]");
        }

        const FString HeaderFull  = FPaths::ConvertRelativePathToFull(HeaderAbs);
        const FString ProjectDir  = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        const FString EngineDir   = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());

        // Engine-installed headers — not useful to link (local UE install).
        if (HeaderFull.StartsWith(EngineDir))
            return TEXT("[engine]");

        // Out-of-project headers (rare — third-party plugin w/ global install)
        // treated as engine-like for the reader.
        if (!HeaderFull.StartsWith(ProjectDir))
            return TEXT("[engine]");

        // Strip project-root prefix → path relative to project root.
        FString RelFromProject = HeaderFull.RightChop(ProjectDir.Len());
        if (RelFromProject.StartsWith(TEXT("/")) ||
            RelFromProject.StartsWith(TEXT("\\")))
        {
            RelFromProject = RelFromProject.RightChop(1);
        }
        RelFromProject.ReplaceInline(TEXT("\\"), TEXT("/"));

        // MD depth under project: <OutputDir>/<asset-subpath>.md
        //  → up-count = OutputDirDepth + slash-count in subpath.
        const FString MdSub = FBPeekAssetPath::ToMdSubpath(AssetPath);
        int32 SlashCount = 0;
        for (TCHAR C : MdSub) if (C == TEXT('/')) ++SlashCount;
        const int32 Depth = SlashCount + OutputDirDepth;

        FString Prefix;
        Prefix.Reserve(Depth * 3);
        for (int32 i = 0; i < Depth; ++i) Prefix += TEXT("../");

        return FString::Printf(TEXT("[Source](%s%s)"), *Prefix, *RelFromProject);
    }

    /** Walk import-table OuterIndex chain to build a full UE path string
     *  like "/Script/ProjectModule.CharacterBase". Returns empty if any
     *  hop is non-import or unresolvable. */
    static FString ResolveImportPath(FLinkerLoad* Linker, FPackageIndex Idx)
    {
        if (Idx.IsNull() || !Idx.IsImport() || !Linker) return FString();
        const FObjectImport& Imp = Linker->Imp(Idx);
        const FString Name = Imp.ObjectName.ToString();
        const FString Parent = ResolveImportPath(Linker, Imp.OuterIndex);
        if (Parent.IsEmpty())
            return Name;   // Package-level name (already starts with `/`).
        return Parent + TEXT(".") + Name;
    }

    /** Look up the UClass that owns a given Function import entry. Returns
     *  null if the owner isn't a loaded UClass (e.g., struct-scoped fn). */
    static UClass* ResolveFunctionOwnerClass(FLinkerLoad* Linker,
                                             const FObjectImport& FnImp)
    {
        if (!Linker || !FnImp.OuterIndex.IsImport()) return nullptr;
        const FString OwnerPath = ResolveImportPath(Linker, FnImp.OuterIndex);
        if (OwnerPath.IsEmpty()) return nullptr;
        return FindObject<UClass>(nullptr, *OwnerPath);
    }
};
