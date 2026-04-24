#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../BPeekTextUnwrap.h"

/* Tests FBPeekTextUnwrap::Unwrap — LOCTEXT / NSLOCTEXT macro unroll that
 * strips the namespace+key metadata and keeps only the display value.
 * Critical for widget/DT default-value rendering where UE exports these
 * macros verbatim. */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekTextUnwrapNSLocTextTest,
    "BPeek.TextUnwrap.NSLOCTEXT",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekTextUnwrapNSLocTextTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("basic NSLOCTEXT unwraps to value only"),
        FBPeekTextUnwrap::Unwrap(
            TEXT("NSLOCTEXT(\"UI\", \"Hello\", \"Hello world\")")),
        FString(TEXT("\"Hello world\"")));

    TestEqual(TEXT("NSLOCTEXT inside longer string"),
        FBPeekTextUnwrap::Unwrap(
            TEXT("before NSLOCTEXT(\"ns\", \"k\", \"v\") after")),
        FString(TEXT("before \"v\" after")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekTextUnwrapLocTextTest,
    "BPeek.TextUnwrap.LOCTEXT",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekTextUnwrapLocTextTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("basic LOCTEXT (2 args) unwraps"),
        FBPeekTextUnwrap::Unwrap(
            TEXT("LOCTEXT(\"Key\", \"Value\")")),
        FString(TEXT("\"Value\"")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekTextUnwrapNoOpTest,
    "BPeek.TextUnwrap.NoOp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekTextUnwrapNoOpTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("empty input"),
        FBPeekTextUnwrap::Unwrap(TEXT("")),
        FString(TEXT("")));

    TestEqual(TEXT("plain text passes through"),
        FBPeekTextUnwrap::Unwrap(TEXT("hello world")),
        FString(TEXT("hello world")));

    TestEqual(TEXT("malformed LOCTEXT (missing closing paren) passes through"),
        FBPeekTextUnwrap::Unwrap(TEXT("LOCTEXT(\"k\", \"v\"")),
        FString(TEXT("LOCTEXT(\"k\", \"v\"")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekTextUnwrapUnicodeTest,
    "BPeek.TextUnwrap.Unicode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekTextUnwrapUnicodeTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("cyrillic value preserved"),
        FBPeekTextUnwrap::Unwrap(
            TEXT("NSLOCTEXT(\"ns\", \"k\", \"Привет, мир!\")")),
        FString(TEXT("\"Привет, мир!\"")));

    return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
