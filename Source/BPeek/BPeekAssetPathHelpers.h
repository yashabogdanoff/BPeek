#pragma once
#include "CoreMinimal.h"

class FBPeekAssetPath
{
public:
    /**
     * .NET-style OrdinalIgnoreCase compare: ASCII letters folded to UPPER
     * case, everything else compared as raw bytes. Direction matters —
     * upper-fold places letters (A-Z = 0x41–0x5A) *before* `_` (0x5F),
     * so `LS_X` < `L_X` (byte 'S' 0x53 < '_' 0x5F). Lower-fold would
     * invert that since 'a'..'z' (0x61+) sorts after '_'. Mirrors
     * coreclr's `Ordinal.CompareStringIgnoreCase` exactly.
     */
    static int32 OrdinalIgnoreCaseCompare(const FString& A, const FString& B)
    {
        const int32 LenA = A.Len();
        const int32 LenB = B.Len();
        const int32 MinLen = FMath::Min(LenA, LenB);
        for (int32 i = 0; i < MinLen; ++i)
        {
            TCHAR Ca = A[i];
            TCHAR Cb = B[i];
            if (Ca >= TEXT('a') && Ca <= TEXT('z')) Ca = Ca - TEXT('a') + TEXT('A');
            if (Cb >= TEXT('a') && Cb <= TEXT('z')) Cb = Cb - TEXT('a') + TEXT('A');
            if (Ca != Cb) return (int32)Ca - (int32)Cb;
        }
        return LenA - LenB;
    }

    /** Aggressive compact form for AI-consumption output.
     *    "/Game/UI/Menu/WBP_Main.WBP_Main"    → "UI/Menu/WBP_Main"
     *    "/ShooterCore/Bot/BT/BT_Enemy.BT_Enemy_C" → "Bot/BT/BT_Enemy"
     *    "/Script/Engine.Blueprint"           → unchanged (UE type refs
     *                                            are already terse and
     *                                            agent-recognisable).
     *  Paired with a one-liner at the top of `_index.md` noting the
     *  convention. Applied in the default (AI-optimised) output; verbose
     *  mode emits unabbreviated paths. */
    static FString Compact(const FString& Raw)
    {
        FString S = Normalize(Raw);
        // Preserve engine / plugin type refs — those are canonical
        // namespaces the AI agent already knows (`/Script/Engine.X`,
        // `/Script/Flow.Y`).
        if (S.StartsWith(TEXT("/Script/"))) return S;
        // Drop the `/Game/` mount point — it's implicit for every
        // project-owned asset.
        if (S.StartsWith(TEXT("/Game/"))) return S.Mid(6);
        // Any other rooted path just loses its leading slash so
        // `/Module_07_X/Y` → `Module_07_X/Y`.
        if (S.StartsWith(TEXT("/"))) return S.Mid(1);
        return S;
    }

    /** "/Game/X/Y.Y" → "/Game/X/Y"; "/Game/X/Y.Y_C" → "/Game/X/Y". */
    static FString Normalize(const FString& Raw)
    {
        FString S = Raw;
        S.TrimStartAndEndInline();
        S.RemoveFromStart(TEXT("'"));
        S.RemoveFromEnd(TEXT("'"));
        if (S.EndsWith(TEXT("_C"))) S = S.LeftChop(2);
        int32 Dot;
        if (S.FindLastChar(TEXT('.'), Dot) && Dot > 0) S = S.Left(Dot);
        return S;
    }

    /** Last path segment without ".Name" or "_C" suffix. */
    static FString ShortName(const FString& AssetPath)
    {
        FString S = AssetPath;
        int32 Slash;
        if (S.FindLastChar(TEXT('/'), Slash)) S = S.Mid(Slash + 1);
        int32 Dot;
        if (S.FindChar(TEXT('.'), Dot)) S = S.Left(Dot);
        if (S.EndsWith(TEXT("_C"))) S = S.LeftChop(2);
        return S;
    }

    /** "/Game/A/B.B" → "Game/A/B.md". Only strips the leading '/' so the
     * layout matches the C# renderer (which keeps the mount point as a
     * top-level directory: bpeek/Game/…, bpeek/Module_10_GPM/…). */
    static FString ToMdSubpath(const FString& AssetPath)
    {
        FString Rel = Normalize(AssetPath);
        if (Rel.StartsWith(TEXT("/"))) Rel = Rel.RightChop(1);
        Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
        return Rel + TEXT(".md");
    }

    /**
     * Relative MD link from <From> asset path to <To> asset path, both as
     * UE mount paths (/Game/X/Y, may or may not have .Name suffix). Returns
     * something like "../../B/Y.md".
     */
    static FString RelativeMdPath(const FString& FromAssetPath, const FString& ToAssetPath)
    {
        const FString FromRel = ToMdSubpath(FromAssetPath);
        const FString ToRel = ToMdSubpath(ToAssetPath);

        TArray<FString> FromParts, ToParts;
        FromRel.ParseIntoArray(FromParts, TEXT("/"), true);
        ToRel.ParseIntoArray(ToParts, TEXT("/"), true);
        // Drop basename off the From side — we want a dir-relative result.
        if (FromParts.Num() > 0) FromParts.Pop();

        int32 Common = 0;
        const int32 MaxCommon = FMath::Min(FromParts.Num(), ToParts.Num() - 1);
        while (Common < MaxCommon && FromParts[Common].Equals(ToParts[Common])) ++Common;

        FString Result;
        for (int32 i = Common; i < FromParts.Num(); ++i) Result += TEXT("../");
        for (int32 i = Common; i < ToParts.Num(); ++i)
        {
            Result += ToParts[i];
            if (i + 1 < ToParts.Num()) Result += TEXT("/");
        }
        return Result;
    }
};