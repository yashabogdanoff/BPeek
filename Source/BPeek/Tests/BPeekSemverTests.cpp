#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../BPeekSemver.h"

/* Unit tests for FBPeekSemver.
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject
 *       -ExecCmds="Automation RunTests BPeek.Semver.; Quit"
 *       -unattended -nosplash -nop4
 */

// ---- Parse — happy path -----------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekSemverParseOkTest,
    "BPeek.Semver.Parse.Ok",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekSemverParseOkTest::RunTest(const FString&)
{
    auto V = FBPeekSemver::Parse(TEXT("1.2.3"));
    TestTrue(TEXT("parsed"), V.IsSet());
    TestEqual(TEXT("major"), V->Major, 1);
    TestEqual(TEXT("minor"), V->Minor, 2);
    TestEqual(TEXT("patch"), V->Patch, 3);

    auto W = FBPeekSemver::Parse(TEXT("10.0.0"));
    TestTrue(TEXT("10.0.0 parsed"), W.IsSet());
    TestEqual(TEXT("10.0.0 major"), W->Major, 10);

    // Shorter forms — missing components default to 0
    auto X = FBPeekSemver::Parse(TEXT("2"));
    TestTrue(TEXT("'2' parsed"), X.IsSet());
    TestEqual(TEXT("'2' major"), X->Major, 2);
    TestEqual(TEXT("'2' minor defaults to 0"), X->Minor, 0);
    TestEqual(TEXT("'2' patch defaults to 0"), X->Patch, 0);

    auto Y = FBPeekSemver::Parse(TEXT("1.5"));
    TestTrue(TEXT("'1.5' parsed"), Y.IsSet());
    TestEqual(TEXT("'1.5' minor"), Y->Minor, 5);

    // Whitespace is trimmed
    auto Z = FBPeekSemver::Parse(TEXT("  3.1.4  "));
    TestTrue(TEXT("whitespace trimmed"), Z.IsSet());
    TestEqual(TEXT("3.1.4 patch"), Z->Patch, 4);
    return true;
}

// ---- Parse — reject bad input -----------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekSemverParseBadTest,
    "BPeek.Semver.Parse.Bad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekSemverParseBadTest::RunTest(const FString&)
{
    TestFalse(TEXT("empty"),        FBPeekSemver::Parse(TEXT("")).IsSet());
    TestFalse(TEXT("whitespace"),   FBPeekSemver::Parse(TEXT("   ")).IsSet());
    TestFalse(TEXT("prerelease"),   FBPeekSemver::Parse(TEXT("1.2.3-beta")).IsSet());
    TestFalse(TEXT("build meta"),   FBPeekSemver::Parse(TEXT("1.2.3+sha")).IsSet());
    TestFalse(TEXT("letters"),      FBPeekSemver::Parse(TEXT("a.b.c")).IsSet());
    TestFalse(TEXT("too many dots"),FBPeekSemver::Parse(TEXT("1.2.3.4")).IsSet());
    TestFalse(TEXT("empty mid"),    FBPeekSemver::Parse(TEXT("1..3")).IsSet());
    TestFalse(TEXT("leading dot"),  FBPeekSemver::Parse(TEXT(".1.2")).IsSet());
    return true;
}

// ---- Comparison -------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekSemverCompareTest,
    "BPeek.Semver.Compare",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekSemverCompareTest::RunTest(const FString&)
{
    auto V100 = FBPeekSemver::Parse(TEXT("1.0.0")).GetValue();
    auto V101 = FBPeekSemver::Parse(TEXT("1.0.1")).GetValue();
    auto V110 = FBPeekSemver::Parse(TEXT("1.1.0")).GetValue();
    auto V200 = FBPeekSemver::Parse(TEXT("2.0.0")).GetValue();
    auto V100b = FBPeekSemver::Parse(TEXT("1.0.0")).GetValue();

    TestTrue (TEXT("1.0.0 == 1.0.0"),  V100 == V100b);
    TestTrue (TEXT("1.0.0 < 1.0.1"),  V100 <  V101);
    TestTrue (TEXT("1.0.1 < 1.1.0"),  V101 <  V110);
    TestTrue (TEXT("1.1.0 < 2.0.0"),  V110 <  V200);
    TestTrue (TEXT("2.0.0 > 1.1.0"),  V200 >  V110);
    TestTrue (TEXT("1.0.0 <= 1.0.0"), V100 <= V100b);
    TestTrue (TEXT("1.0.0 >= 1.0.0"), V100 >= V100b);
    TestFalse(TEXT("not 1.0.0 != 1.0.0"), V100 != V100b);
    return true;
}

// ---- ToString round-trip ----------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekSemverToStringTest,
    "BPeek.Semver.ToString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekSemverToStringTest::RunTest(const FString&)
{
    TestEqual(TEXT("1.2.3"), FBPeekSemver::Parse(TEXT("1.2.3")).GetValue().ToString(), FString(TEXT("1.2.3")));
    TestEqual(TEXT("zero padded"), FBPeekSemver::Parse(TEXT("0")).GetValue().ToString(), FString(TEXT("0.0.0")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
