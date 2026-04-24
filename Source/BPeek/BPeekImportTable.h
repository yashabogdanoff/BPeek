#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekAssetLinks.h"
#include "BPeekCppSource.h"
#include "Engine/Blueprint.h"
#include "UObject/LinkerLoad.h"
#include "UObject/ObjectResource.h"
#include "UObject/Package.h"

/**
 * Emits the set of metadata sections driven by FLinkerLoad::ImportMap:
 *
 *   ## Dependencies
 *     **Project assets (N)** — bullet list with MD links.
 *     **Engine / plugin modules (M)** — single `·`-joined line.
 *
 *   ## Components & object references
 *     Table of Kind | Object(s) covering every non-reflection import.
 *
 *   ## Reflection
 *     ### Node kinds / Functions called / Structs used / Enums used
 *     subsections, always expanded — no <details> collapse; data
 *     volume is modest and the info is load-bearing for AI agents
 *     reading the markdown raw.
 */
class FBPeekImportTable
{
public:
    static void Write(FBPeekMarkdownWriter& W, UBlueprint* BP,
                      const TSet<FString>& KnownNormalized,
                      bool bVerboseMode = false)
    {
#if WITH_EDITOR
        if (!BP) return;
        UPackage* Pkg = BP->GetOutermost();
        if (!Pkg) return;
        FLinkerLoad* Linker = FLinkerLoad::FindExistingLinkerForPackage(Pkg);
        if (!Linker) return;
        const FString AssetPath = BP->GetPathName();

        // Group imports by ClassName to avoid N passes over the ImportMap.
        TMap<FName, TArray<FString>> Groups;
        for (const FObjectImport& Imp : Linker->ImportMap)
        {
            const FName ClassName = Imp.ClassName;
            const FString ObjectName = Imp.ObjectName.ToString();
            Groups.FindOrAdd(ClassName).AddUnique(ObjectName);
        }

        TArray<FString> Packages = GetSortedOrEmpty(Groups, FName(TEXT("Package")));
        WriteDependenciesSection(W, Packages, AssetPath, KnownNormalized, bVerboseMode);
        WriteComponentsTableSection(W, Groups, bVerboseMode);
        WriteReflectionSection(W, Groups, Linker, AssetPath, bVerboseMode);
#endif
    }

private:
    static TArray<FString> GetSortedOrEmpty(
        TMap<FName, TArray<FString>>& Groups, const FName& Key)
    {
        TArray<FString> Out;
        if (TArray<FString>* Found = Groups.Find(Key)) Out = *Found;
        Out.Sort();
        return Out;
    }

    /** Dependencies section — project assets as linked bullets, engine
     *  modules as a single `·`-joined compact line. No section emitted
     *  when both buckets are empty. */
    static void WriteDependenciesSection(FBPeekMarkdownWriter& W,
                                         const TArray<FString>& Packages,
                                         const FString& AssetPath,
                                         const TSet<FString>& KnownNormalized,
                                         bool bVerboseMode = false)
    {
        if (Packages.Num() == 0) return;

        TArray<FString> ProjectAssets;
        TArray<FString> EngineModules;
        for (const FString& P : Packages)
        {
            const FString Linked = FBPeekAssetLinks::Linkify(P, AssetPath, KnownNormalized);
            if (Linked != P)
                ProjectAssets.Add(Linked);   // already linkified
            else
                EngineModules.Add(P);        // plain /Script/X path
        }

        if (ProjectAssets.Num() == 0 && EngineModules.Num() == 0) return;

        W.WriteHeading(2, TEXT("Dependencies"));
        W.WriteLine();

        if (ProjectAssets.Num() > 0)
        {
            if (bVerboseMode)
            {
                W.WriteLine(FString::Printf(TEXT("**Project assets (%d)**"), ProjectAssets.Num()));
                W.WriteLine();
                for (const FString& L : ProjectAssets)
                    W.WriteLine(FString::Printf(TEXT("- %s"), *L));
                W.WriteLine();
            }
            else
            {
                W.WriteLine(FString::Printf(TEXT("**Project assets (%d):** %s"),
                    ProjectAssets.Num(), *FString::Join(ProjectAssets, TEXT(", "))));
                W.WriteLine();
            }
        }
        if (EngineModules.Num() > 0)
        {
            W.WriteLine(FString::Printf(TEXT("**Engine / plugin modules (%d)**"), EngineModules.Num()));
            W.WriteLine();
            // Strip `/Script/` prefix for readability: /Script/Engine → Engine.
            TArray<FString> Short;
            for (const FString& M : EngineModules)
            {
                FString S = M;
                S.RemoveFromStart(TEXT("/Script/"));
                Short.Add(FString::Printf(TEXT("`%s`"), *S));
            }
            W.WriteLine(FString::Join(Short, TEXT(" · ")));
            W.WriteLine();
        }
    }

    /** Table of Kind | Object(s) for every non-reflection, non-package
     *  import. "Reflection" keys (Class/Function/ScriptStruct/Enum/
     *  Package) go elsewhere; anything else is an object reference. */
    static void WriteComponentsTableSection(FBPeekMarkdownWriter& W,
                                            TMap<FName, TArray<FString>>& Groups,
                                            bool bVerboseMode = false)
    {
        static const TSet<FName> Skip = {
            FName(TEXT("Class")), FName(TEXT("Function")),
            FName(TEXT("ScriptStruct")), FName(TEXT("Enum")),
            FName(TEXT("Package"))
        };
        TArray<FName> Keys;
        Groups.GetKeys(Keys);
        Keys.Sort([](const FName& A, const FName& B){ return A.Compare(B) < 0; });

        // Collect non-empty rows first so we can skip the header if none.
        struct FRow { FString Kind; FString Objects; };
        TArray<FRow> Rows;
        for (const FName& K : Keys)
        {
            if (Skip.Contains(K)) continue;
            TArray<FString>* Names = Groups.Find(K);
            if (!Names || Names->Num() == 0) continue;
            Names->Sort();
            TArray<FString> Coded;
            for (const FString& N : *Names)
                Coded.Add(FString::Printf(TEXT("`%s`"), *N));
            FRow R;
            R.Kind = FString::Printf(TEXT("`%s`"), *K.ToString());
            R.Objects = FString::Join(Coded, TEXT(", "));
            Rows.Add(MoveTemp(R));
        }
        if (Rows.Num() == 0) return;

        if (bVerboseMode)
        {
            W.WriteHeading(2, TEXT("Components & object references"));
            W.WriteLine();
            W.WriteLine(TEXT("| Kind | Object |"));
            W.WriteLine(TEXT("|---|---|"));
            for (const FRow& R : Rows)
                W.WriteLine(FString::Printf(TEXT("| %s | %s |"), *R.Kind, *R.Objects));
            W.WriteLine();
            return;
        }

        // Default (AI-optimised) — one line per kind with " · " separator
        // at the top level, ", " inside each per-kind object list.
        TArray<FString> Parts;
        Parts.Reserve(Rows.Num());
        for (const FRow& R : Rows)
            Parts.Add(FString::Printf(TEXT("%s=%s"), *R.Kind, *R.Objects));
        W.WriteLine(FString::Printf(TEXT("**Components & object references (%d):** %s"),
            Rows.Num(), *FString::Join(Parts, TEXT(" · "))));
        W.WriteLine();
    }

    /** Reflection section — all metadata-style lists collapsed into
     *  <details> disclosures. Hidden by default in GitHub/VSCode but
     *  fully visible to AI agents reading raw markdown. */
    static void WriteReflectionSection(FBPeekMarkdownWriter& W,
                                       TMap<FName, TArray<FString>>& Groups,
                                       FLinkerLoad* Linker,
                                       const FString& AssetPath,
                                       bool bVerboseMode = false)
    {
        // K2 node types — Class imports starting with "K2Node_".
        TArray<FString> K2Nodes;
        if (const TArray<FString>* Classes = Groups.Find(FName(TEXT("Class"))))
        {
            for (const FString& N : *Classes)
                if (N.StartsWith(TEXT("K2Node_"))) K2Nodes.AddUnique(N);
        }
        K2Nodes.Sort();

        TArray<FString> Structs = GetSortedOrEmpty(Groups, FName(TEXT("ScriptStruct")));
        TArray<FString> Enums   = GetSortedOrEmpty(Groups, FName(TEXT("Enum")));

        // Functions get type-aware resolution (owner UClass → source link)
        // instead of raw name emit; collect entries for the <details>.
        TArray<FString> Functions = CollectFunctionRows(Linker, AssetPath);

        const bool bAny = K2Nodes.Num() > 0 || Functions.Num() > 0 ||
                          Structs.Num()  > 0 || Enums.Num()     > 0;
        if (!bAny) return;

        W.WriteHeading(2, TEXT("Reflection"));
        W.WriteLine();

        EmitCompact(W,   TEXT("Node kinds"),       K2Nodes);
        EmitFunctions(W, TEXT("Functions called"), Functions, bVerboseMode);
        EmitCompact(W,   TEXT("Structs used"),     Structs);
        EmitCompact(W,   TEXT("Enums used"),       Enums);
    }

    /** Resolve every Function import → sorted TArray of pre-formatted MD
     *  rows including the cpp-source suffix. Dedup by function name. */
    static TArray<FString> CollectFunctionRows(FLinkerLoad* Linker,
                                               const FString& AssetPath)
    {
        struct FFn { FString Name; FString Link; };
        TArray<FFn> Fns;
        TSet<FString> Seen;
        const FName FunctionClass(TEXT("Function"));
        for (const FObjectImport& Imp : Linker->ImportMap)
        {
            if (Imp.ClassName != FunctionClass) continue;
            const FString Name = Imp.ObjectName.ToString();
            bool bAlready = false;
            Seen.Add(Name, &bAlready);
            if (bAlready) continue;
            UClass* Owner = FBPeekCppSource::ResolveFunctionOwnerClass(Linker, Imp);
            FFn F;
            F.Name = Name;
            F.Link = Owner ? FBPeekCppSource::ResolveClassLink(Owner, AssetPath) : FString();
            Fns.Add(MoveTemp(F));
        }
        Fns.Sort([](const FFn& A, const FFn& B){ return A.Name < B.Name; });

        TArray<FString> Rows;
        Rows.Reserve(Fns.Num());
        for (const FFn& F : Fns)
        {
            if (F.Link.IsEmpty())
                Rows.Add(FString::Printf(TEXT("`%s`"), *F.Name));
            else
                Rows.Add(FString::Printf(TEXT("`%s` → %s"), *F.Name, *F.Link));
        }
        return Rows;
    }

    /** Emit a `### Title (N)` subsection with a single `·`-joined line
     *  of backtick-wrapped names. Always visible (no <details> collapse). */
    static void EmitCompact(FBPeekMarkdownWriter& W, const FString& Title,
                            const TArray<FString>& Items)
    {
        if (Items.Num() == 0) return;
        W.WriteHeading(3, FString::Printf(TEXT("%s (%d)"), *Title, Items.Num()));
        W.WriteLine();
        TArray<FString> Coded;
        Coded.Reserve(Items.Num());
        for (const FString& It : Items)
            Coded.Add(FString::Printf(TEXT("`%s`"), *It));
        W.WriteLine(FString::Join(Coded, TEXT(" · ")));
        W.WriteLine();
    }

    /** Emit a `### Title (N)` subsection for Functions called — items
     *  come pre-formatted (`name → [Source]` or bare `name`). List form
     *  rather than compact because source links can be long. */
    static void EmitFunctions(FBPeekMarkdownWriter& W, const FString& Title,
                              const TArray<FString>& Rows, bool bVerboseMode = false)
    {
        if (Rows.Num() == 0) return;
        if (bVerboseMode)
        {
            W.WriteHeading(3, FString::Printf(TEXT("%s (%d)"), *Title, Rows.Num()));
            W.WriteLine();
            for (const FString& R : Rows)
                W.WriteLine(FString::Printf(TEXT("- %s"), *R));
            W.WriteLine();
            return;
        }
        // Default (AI-optimised) — one line, source links inline.
        W.WriteLine(FString::Printf(TEXT("**%s (%d):** %s"),
            *Title, Rows.Num(), *FString::Join(Rows, TEXT(", "))));
        W.WriteLine();
    }

};
