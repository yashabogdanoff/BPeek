#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "BPeekInputMappingsParser.h"

/* Tests FBPeekInputMappings::TryParse — UE ini-style mapping blob parser.
 * Typical input shape:
 *   ((Key="LeftShift", Action="/Script/EnhancedInput.InputAction'/Game/Input/IA_Run.IA_Run'",
 *     Modifiers=("/Script/EnhancedInput.InputModifierNegate'/Game/Input/Neg.Neg'")),
 *    (Key="W", Action="/Script/EnhancedInput.InputAction'/Game/Input/IA_Move.IA_Move'"))
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekInputMappingsTryParseEmptyTest,
    "BPeek.InputMappings.TryParse.Empty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekInputMappingsTryParseEmptyTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("empty string"),
        FBPeekInputMappings::TryParse(TEXT("")).Num(), 0);
    TestEqual(TEXT("whitespace only"),
        FBPeekInputMappings::TryParse(TEXT("   ")).Num(), 0);
    TestEqual(TEXT("missing outer parens"),
        FBPeekInputMappings::TryParse(TEXT("Key=\"W\",Action=\"A\"")).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekInputMappingsTryParseSingleTest,
    "BPeek.InputMappings.TryParse.Single",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekInputMappingsTryParseSingleTest::RunTest(const FString& /*Parameters*/)
{
    const FString Raw =
        TEXT("((Key=\"W\",Action=\"/Script/EnhancedInput.InputAction'/Game/Input/IA_Move.IA_Move'\"))");
    TArray<FBPeekMappingEntry> Out = FBPeekInputMappings::TryParse(Raw);
    TestEqual(TEXT("one entry"), Out.Num(), 1);
    if (Out.Num() == 1)
    {
        TestEqual(TEXT("Key extracted"),   Out[0].Key,    FString(TEXT("W")));
        TestEqual(TEXT("Action prettified"), Out[0].Action, FString(TEXT("IA_Move")));
        TestEqual(TEXT("No modifiers"),    Out[0].Modifiers.Num(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekInputMappingsTryParseMultipleTest,
    "BPeek.InputMappings.TryParse.Multiple",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekInputMappingsTryParseMultipleTest::RunTest(const FString& /*Parameters*/)
{
    const FString Raw =
        TEXT("(")
        TEXT("(Key=\"LeftShift\",Action=\"/Script/EnhancedInput.InputAction'/Game/IA_Run.IA_Run'\"),")
        TEXT("(Key=\"W\",Action=\"/Script/EnhancedInput.InputAction'/Game/IA_Move.IA_Move'\")")
        TEXT(")");
    TArray<FBPeekMappingEntry> Out = FBPeekInputMappings::TryParse(Raw);
    TestEqual(TEXT("two entries"), Out.Num(), 2);
    if (Out.Num() == 2)
    {
        TestEqual(TEXT("first Key"),    Out[0].Key,    FString(TEXT("LeftShift")));
        TestEqual(TEXT("first Action"), Out[0].Action, FString(TEXT("IA_Run")));
        TestEqual(TEXT("second Key"),   Out[1].Key,    FString(TEXT("W")));
        TestEqual(TEXT("second Action"),Out[1].Action, FString(TEXT("IA_Move")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekInputMappingsTryParseModifiersTest,
    "BPeek.InputMappings.TryParse.Modifiers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekInputMappingsTryParseModifiersTest::RunTest(const FString& /*Parameters*/)
{
    // Modifiers list: each entry is a full /Script/EnhancedInput.InputModifier<Name>
    // path. Parser strips the prefix/path and `_N` instance suffix.
    const FString Raw =
        TEXT("((Key=\"W\",Action=\"/Script/EnhancedInput.InputAction'/Game/IA.IA'\",")
        TEXT("Modifiers=(\"/Script/EnhancedInput.InputModifierNegate\","
                        "\"/Script/EnhancedInput.InputModifierScalar_3\")))");
    TArray<FBPeekMappingEntry> Out = FBPeekInputMappings::TryParse(Raw);
    TestEqual(TEXT("one entry"), Out.Num(), 1);
    if (Out.Num() == 1)
    {
        TestEqual(TEXT("two modifiers"), Out[0].Modifiers.Num(), 2);
        if (Out[0].Modifiers.Num() == 2)
        {
            TestEqual(TEXT("Negate"), Out[0].Modifiers[0], FString(TEXT("Negate")));
            TestEqual(TEXT("Scalar (N-suffix stripped)"),
                      Out[0].Modifiers[1], FString(TEXT("Scalar")));
        }
    }
    return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
