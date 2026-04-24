#include "BPeekSemver.h"

TOptional<FBPeekSemver> FBPeekSemver::Parse(const FString& In)
{
    FString S = In;
    S.TrimStartAndEndInline();
    if (S.IsEmpty()) return {};

    // Split on '.'. We accept 1-3 components; missing ones default to 0.
    TArray<FString> Parts;
    S.ParseIntoArray(Parts, TEXT("."), /*CullEmpty=*/false);
    if (Parts.Num() < 1 || Parts.Num() > 3) return {};

    FBPeekSemver Out;
    int32* Targets[3] = { &Out.Major, &Out.Minor, &Out.Patch };

    for (int32 i = 0; i < Parts.Num(); ++i)
    {
        const FString& P = Parts[i];
        if (P.IsEmpty()) return {};           // "1..3" — reject
        // Only digits. Matches numeric semver — we deliberately don't
        // support pre-release/build suffixes.
        for (TCHAR C : P)
        {
            if (!FChar::IsDigit(C)) return {};
        }
        int64 Value = FCString::Strtoi64(*P, nullptr, 10);
        if (Value < 0 || Value > MAX_int32) return {};
        *Targets[i] = static_cast<int32>(Value);
    }
    return Out;
}
