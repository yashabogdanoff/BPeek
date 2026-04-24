#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <initializer_list>
#include "Misc/AutomationTest.h"
#include "../BPeekAssetLinks.h"

/* Tests cover FBPeekAssetLinks::Linkify and ExtractPaths — ICU-regex-based
 * UE-path recognition, with Unicode support (cyrillic / accented names) and
 * terminator lookahead to avoid matching stray slashes inside words. */

static TSet<FString> MakeKnownSet(std::initializer_list<const TCHAR*> Paths)
{
    TSet<FString> S;
    for (const TCHAR* P : Paths) S.Add(FString(P));
    return S;
}

// ---- Linkify ----------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetLinksLinkifyBasicTest,
    "BPeek.AssetLinks.Linkify.Basic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetLinksLinkifyBasicTest::RunTest(const FString& /*Parameters*/)
{
    const TSet<FString> Known = MakeKnownSet({ TEXT("/Game/X/B") });

    TestEqual(TEXT("standalone known path becomes MD link"),
        FBPeekAssetLinks::Linkify(
            TEXT("ref: /Game/X/B"),
            TEXT("/Game/A.A"),
            Known),
        FString(TEXT("ref: [B](X/B.md)")));

    TestEqual(TEXT("unknown path passes through"),
        FBPeekAssetLinks::Linkify(
            TEXT("ref: /Game/X/Unknown"),
            TEXT("/Game/A.A"),
            Known),
        FString(TEXT("ref: /Game/X/Unknown")));

    TestEqual(TEXT("empty input returns empty"),
        FBPeekAssetLinks::Linkify(
            TEXT(""),
            TEXT("/Game/A.A"),
            Known),
        FString(TEXT("")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetLinksLinkifyUnicodeTest,
    "BPeek.AssetLinks.Linkify.Unicode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetLinksLinkifyUnicodeTest::RunTest(const FString& /*Parameters*/)
{
    // Regression guard: C# version's [A-Za-z0-9_\-] regex rejected cyrillic
    // asset names, truncated paths. We fixed that with \p{L}\p{N} in the C++
    // port. This test locks the behaviour in.
    const TSet<FString> Known = MakeKnownSet({ TEXT("/Game/Оборудование/BP_Тест") });

    TestEqual(TEXT("cyrillic path linkified correctly"),
        FBPeekAssetLinks::Linkify(
            TEXT("see /Game/Оборудование/BP_Тест end"),
            TEXT("/Game/A.A"),
            Known),
        FString(TEXT("see [BP_Тест](Оборудование/BP_Тест.md) end")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetLinksLinkifyTerminatorTest,
    "BPeek.AssetLinks.Linkify.Terminator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetLinksLinkifyTerminatorTest::RunTest(const FString& /*Parameters*/)
{
    // Terminator lookahead: match only if followed by whitespace/comma/quote/
    // closing bracket — otherwise the match is a partial hit inside a longer
    // token and must be ignored.
    const TSet<FString> Known = MakeKnownSet({ TEXT("/Game/X/B") });

    TestEqual(TEXT("end of string terminates match"),
        FBPeekAssetLinks::Linkify(
            TEXT("/Game/X/B"),
            TEXT("/Game/A.A"),
            Known),
        FString(TEXT("[B](X/B.md)")));

    TestEqual(TEXT("trailing slash + word blocks match"),
        FBPeekAssetLinks::Linkify(
            TEXT("/Game/X/Blongerword"),
            TEXT("/Game/A.A"),
            Known),
        FString(TEXT("/Game/X/Blongerword")));

    TestEqual(TEXT("quote terminator works"),
        FBPeekAssetLinks::Linkify(
            TEXT("path=\"/Game/X/B\""),
            TEXT("/Game/A.A"),
            Known),
        FString(TEXT("path=\"[B](X/B.md)\"")));

    return true;
}

// ---- ExtractPaths -----------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetLinksExtractPathsTest,
    "BPeek.AssetLinks.ExtractPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetLinksExtractPathsTest::RunTest(const FString& /*Parameters*/)
{
    TArray<FString> Out = FBPeekAssetLinks::ExtractPaths(
        TEXT("one /Game/A/B.B and another /Module_10_GPM/Content/C.C end"));

    TestEqual(TEXT("found two paths"), Out.Num(), 2);
    if (Out.Num() == 2)
    {
        TestEqual(TEXT("first is normalized"), Out[0], FString(TEXT("/Game/A/B")));
        TestEqual(TEXT("second preserves module mount"),
            Out[1], FString(TEXT("/Module_10_GPM/Content/C")));
    }

    TArray<FString> Empty = FBPeekAssetLinks::ExtractPaths(TEXT("no paths here"));
    TestEqual(TEXT("no paths → empty array"), Empty.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetLinksExtractPathsUnicodeTest,
    "BPeek.AssetLinks.ExtractPaths.Unicode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetLinksExtractPathsUnicodeTest::RunTest(const FString& /*Parameters*/)
{
    TArray<FString> Out = FBPeekAssetLinks::ExtractPaths(
        TEXT("pre /Game/Папка/BP_Name.BP_Name post"));

    TestEqual(TEXT("cyrillic folder & name extracted"), Out.Num(), 1);
    if (Out.Num() == 1)
        TestEqual(TEXT("normalized correctly"),
            Out[0], FString(TEXT("/Game/Папка/BP_Name")));

    return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
