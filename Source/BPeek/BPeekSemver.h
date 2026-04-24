#pragma once

#include "CoreMinimal.h"

//
// Lightweight semver parser/comparator for the runtime compat gate.
// Covers MAJOR.MINOR.PATCH only — no pre-release tags, no build
// metadata. "1.2.3" parses, "1.2.3-beta" does not.
//
// We reject anything weirder than digits-and-dots so extensions that
// try to be clever with version strings fail loudly at Startup rather
// than silently passing the compat check.
//

struct BPEEK_API FBPeekSemver
{
    int32 Major = 0;
    int32 Minor = 0;
    int32 Patch = 0;

    /** Try to parse "X.Y.Z" (or shorter: "X", "X.Y") into a semver.
     *  Leading/trailing whitespace is trimmed. Missing components
     *  default to zero ("1.2" → 1.2.0). Returns unset on bad input. */
    static TOptional<FBPeekSemver> Parse(const FString& In);

    /** -1 / 0 / +1 ordering. */
    int32 CompareTo(const FBPeekSemver& Other) const
    {
        if (Major != Other.Major) return Major < Other.Major ? -1 : 1;
        if (Minor != Other.Minor) return Minor < Other.Minor ? -1 : 1;
        if (Patch != Other.Patch) return Patch < Other.Patch ? -1 : 1;
        return 0;
    }

    bool operator==(const FBPeekSemver& O) const { return CompareTo(O) == 0; }
    bool operator!=(const FBPeekSemver& O) const { return CompareTo(O) != 0; }
    bool operator< (const FBPeekSemver& O) const { return CompareTo(O) <  0; }
    bool operator<=(const FBPeekSemver& O) const { return CompareTo(O) <= 0; }
    bool operator> (const FBPeekSemver& O) const { return CompareTo(O) >  0; }
    bool operator>=(const FBPeekSemver& O) const { return CompareTo(O) >= 0; }

    FString ToString() const
    {
        return FString::Printf(TEXT("%d.%d.%d"), Major, Minor, Patch);
    }
};
