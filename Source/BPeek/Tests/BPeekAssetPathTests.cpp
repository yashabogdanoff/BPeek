#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../BPeekAssetPathHelpers.h"

/* Tests in this file cover FBPeekAssetPath — pure FString utilities with no
 * UObject dependencies. Run via:
 *   UnrealEditor-Cmd.exe <Project>.uproject
 *       -ExecCmds="Automation RunTests BPeek.AssetPath.; Quit"
 *       -unattended -nosplash -nop4
 */

// ---- Normalize --------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetPathNormalizeTest,
    "BPeek.AssetPath.Normalize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetPathNormalizeTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("strip .Name suffix"),
        FBPeekAssetPath::Normalize(TEXT("/Game/X/Y.Y")),
        FString(TEXT("/Game/X/Y")));

    TestEqual(TEXT("strip _C then .Name"),
        FBPeekAssetPath::Normalize(TEXT("/Game/X/Y.Y_C")),
        FString(TEXT("/Game/X/Y")));

    TestEqual(TEXT("strip surrounding quotes"),
        FBPeekAssetPath::Normalize(TEXT("'/Game/X/Y.Y'")),
        FString(TEXT("/Game/X/Y")));

    TestEqual(TEXT("no dot — return as-is trimmed"),
        FBPeekAssetPath::Normalize(TEXT("  /Game/X/Y  ")),
        FString(TEXT("/Game/X/Y")));

    TestEqual(TEXT("cyrillic path passes intact"),
        FBPeekAssetPath::Normalize(TEXT("/Game/Оборудование/BP_Тест.BP_Тест")),
        FString(TEXT("/Game/Оборудование/BP_Тест")));

    TestEqual(TEXT("empty input returns empty"),
        FBPeekAssetPath::Normalize(TEXT("")),
        FString(TEXT("")));
    return true;
}

// ---- ShortName --------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetPathShortNameTest,
    "BPeek.AssetPath.ShortName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetPathShortNameTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("basic path"),
        FBPeekAssetPath::ShortName(TEXT("/Game/X/Y/BP_Foo.BP_Foo")),
        FString(TEXT("BP_Foo")));

    TestEqual(TEXT("_C class suffix stripped"),
        FBPeekAssetPath::ShortName(TEXT("/Game/X/BP_Bar.BP_Bar_C")),
        FString(TEXT("BP_Bar")));

    TestEqual(TEXT("no directory"),
        FBPeekAssetPath::ShortName(TEXT("BP_Lone.BP_Lone")),
        FString(TEXT("BP_Lone")));

    TestEqual(TEXT("cyrillic name"),
        FBPeekAssetPath::ShortName(TEXT("/Game/X/BP_Тест.BP_Тест")),
        FString(TEXT("BP_Тест")));

    return true;
}

// ---- ToMdSubpath ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetPathToMdSubpathTest,
    "BPeek.AssetPath.ToMdSubpath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetPathToMdSubpathTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("strip leading slash, append .md"),
        FBPeekAssetPath::ToMdSubpath(TEXT("/Game/X/Y.Y")),
        FString(TEXT("Game/X/Y.md")));

    TestEqual(TEXT("module mount point preserved"),
        FBPeekAssetPath::ToMdSubpath(TEXT("/Module_10_GPM/Content/BP.BP")),
        FString(TEXT("Module_10_GPM/Content/BP.md")));

    TestEqual(TEXT("backslashes normalized to forward"),
        FBPeekAssetPath::ToMdSubpath(TEXT("/Game/A\\B.B")),
        FString(TEXT("Game/A/B.md")));

    return true;
}

// ---- RelativeMdPath ---------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetPathRelativeMdPathTest,
    "BPeek.AssetPath.RelativeMdPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetPathRelativeMdPathTest::RunTest(const FString& /*Parameters*/)
{
    TestEqual(TEXT("same directory"),
        FBPeekAssetPath::RelativeMdPath(
            TEXT("/Game/A/B.B"),
            TEXT("/Game/A/C.C")),
        FString(TEXT("C.md")));

    TestEqual(TEXT("sibling directory"),
        FBPeekAssetPath::RelativeMdPath(
            TEXT("/Game/A/B.B"),
            TEXT("/Game/D/E.E")),
        FString(TEXT("../D/E.md")));

    TestEqual(TEXT("deeper target"),
        FBPeekAssetPath::RelativeMdPath(
            TEXT("/Game/A/B.B"),
            TEXT("/Game/A/D/E.E")),
        FString(TEXT("D/E.md")));

    TestEqual(TEXT("cross mount points"),
        FBPeekAssetPath::RelativeMdPath(
            TEXT("/Game/A/B.B"),
            TEXT("/Module_10_GPM/Content/C.C")),
        FString(TEXT("../../Module_10_GPM/Content/C.md")));

    return true;
}

// ---- OrdinalIgnoreCaseCompare -----------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekAssetPathOrdinalIgnoreCaseCompareTest,
    "BPeek.AssetPath.OrdinalIgnoreCaseCompare",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekAssetPathOrdinalIgnoreCaseCompareTest::RunTest(const FString& /*Parameters*/)
{
    TestTrue(TEXT("equal strings"),
        FBPeekAssetPath::OrdinalIgnoreCaseCompare(TEXT("ABC"), TEXT("abc")) == 0);

    TestTrue(TEXT("case-insensitive equal"),
        FBPeekAssetPath::OrdinalIgnoreCaseCompare(TEXT("BP_Foo"), TEXT("bp_foo")) == 0);

    TestTrue(TEXT("'A' < 'B'"),
        FBPeekAssetPath::OrdinalIgnoreCaseCompare(TEXT("A"), TEXT("B")) < 0);

    // Upper-fold-first ordering is load-bearing: 'LS_' < 'L_' because
    // 'S' (0x53) < '_' (0x5F) — matches coreclr Ordinal* folding.
    TestTrue(TEXT("LS_X comes before L_X ('S' < '_')"),
        FBPeekAssetPath::OrdinalIgnoreCaseCompare(TEXT("LS_X"), TEXT("L_X")) < 0);
    TestTrue(TEXT("and the reverse also holds (positive for A > B)"),
        FBPeekAssetPath::OrdinalIgnoreCaseCompare(TEXT("L_X"), TEXT("LS_X")) > 0);

    TestTrue(TEXT("shorter prefix < longer"),
        FBPeekAssetPath::OrdinalIgnoreCaseCompare(TEXT("Foo"), TEXT("Foobar")) < 0);

    return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
