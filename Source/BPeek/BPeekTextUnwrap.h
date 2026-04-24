#pragma once
#include "CoreMinimal.h"

class FBPeekTextUnwrap
{
public:
    /** Разворачивает все NSLOCTEXT/LOCTEXT внутри строки до стабильного состояния. */
    static FString Unwrap(const FString& In)
    {
        if (In.IsEmpty()) return In;
        FString Cur = In;
        for (int32 Pass = 0; Pass < 8; ++Pass)
        {
            FString Next = UnwrapOnce(Cur);
            if (Next.Equals(Cur)) return Next;
            Cur = MoveTemp(Next);
        }
        return Cur;
    }

private:
    /** Один проход: заменяет каждое occurrence NSLOCTEXT(...)/LOCTEXT(...) на "value". */
    static FString UnwrapOnce(const FString& In)
    {
        FString Out;
        Out.Reserve(In.Len());
        const TCHAR* P = *In;
        const TCHAR* End = P + In.Len();
        while (P < End)
        {
            if (TryMatch(P, End, TEXT("NSLOCTEXT("), 10))
            {
                const TCHAR* After = P + 10;
                // NSLOCTEXT has three quoted args: ns, key, value. Skip first two, capture third.
                if (SkipQuoted(After, End) && SkipComma(After, End) &&
                    SkipQuoted(After, End) && SkipComma(After, End))
                {
                    FString Val;
                    if (CaptureQuoted(After, End, Val) && SkipCloseParen(After, End))
                    {
                        Out.AppendChar(TEXT('"')); Out.Append(Val); Out.AppendChar(TEXT('"'));
                        P = After;
                        continue;
                    }
                }
            }
            else if (TryMatch(P, End, TEXT("LOCTEXT("), 8))
            {
                const TCHAR* After = P + 8;
                if (SkipQuoted(After, End) && SkipComma(After, End))
                {
                    FString Val;
                    if (CaptureQuoted(After, End, Val) && SkipCloseParen(After, End))
                    {
                        Out.AppendChar(TEXT('"')); Out.Append(Val); Out.AppendChar(TEXT('"'));
                        P = After;
                        continue;
                    }
                }
            }
            Out.AppendChar(*P);
            ++P;
        }
        return Out;
    }

    static bool TryMatch(const TCHAR* P, const TCHAR* End, const TCHAR* Token, int32 TokenLen)
    {
        if (End - P < TokenLen) return false;
        for (int32 i = 0; i < TokenLen; ++i) if (P[i] != Token[i]) return false;
        return true;
    }

    /** Skips optional whitespace then one double-quoted string with \\-escapes. */
    static bool SkipQuoted(const TCHAR*& P, const TCHAR* End)
    {
        while (P < End && FChar::IsWhitespace(*P)) ++P;
        if (P >= End || *P != TEXT('"')) return false;
        ++P;
        while (P < End)
        {
            if (*P == TEXT('\\')) { if (P + 1 >= End) return false; P += 2; continue; }
            if (*P == TEXT('"')) { ++P; return true; }
            ++P;
        }
        return false;
    }

    /** Same as SkipQuoted but stores the unescaped body into <Out>. */
    static bool CaptureQuoted(const TCHAR*& P, const TCHAR* End, FString& Out)
    {
        while (P < End && FChar::IsWhitespace(*P)) ++P;
        if (P >= End || *P != TEXT('"')) return false;
        ++P;
        while (P < End)
        {
            if (*P == TEXT('\\'))
            {
                if (P + 1 >= End) return false;
                // Keep the escape verbatim — C# version does the same, snapshot parity.
                Out.AppendChar(*P); ++P; Out.AppendChar(*P); ++P; continue;
            }
            if (*P == TEXT('"')) { ++P; return true; }
            Out.AppendChar(*P); ++P;
        }
        return false;
    }

    static bool SkipComma(const TCHAR*& P, const TCHAR* End)
    {
        while (P < End && FChar::IsWhitespace(*P)) ++P;
        if (P >= End || *P != TEXT(',')) return false;
        ++P;
        return true;
    }

    static bool SkipCloseParen(const TCHAR*& P, const TCHAR* End)
    {
        while (P < End && FChar::IsWhitespace(*P)) ++P;
        if (P >= End || *P != TEXT(')')) return false;
        ++P;
        return true;
    }
};