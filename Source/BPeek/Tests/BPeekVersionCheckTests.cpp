#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "../BPeekVersionCheck.h"

/* Unit tests for FBPeekVersionCheck::CheckCompat — the pure part of
 * the version gate. LoadManifest / RunStartupCheck exercise
 * IPluginManager and need a live plugin set, so they're validated at
 * integration time rather than here.
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject
 *       -ExecCmds="Automation RunTests BPeek.Version.; Quit"
 *       -unattended -nosplash -nop4
 */

namespace
{
    FBPeekSemver V(const TCHAR* S)
    {
        return FBPeekSemver::Parse(FString(S)).GetValue();
    }

    FBPeekExtensionManifest MakeManifest(
        const FString& MinC, const FString& MaxC,
        const FString& Target = FString(),
        const FString& MinT = FString(), const FString& MaxT = FString())
    {
        FBPeekExtensionManifest M;
        M.bPresent = true;
        M.PluginName = TEXT("TestExt");
        M.CoreVersionMin = MinC;
        M.CoreVersionMax = MaxC;
        M.TargetPlugin = Target;
        M.TargetVersionMin = MinT;
        M.TargetVersionMax = MaxT;
        return M;
    }
}

// ---- No manifest → always compatible ----------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekVersionNoManifestTest,
    "BPeek.Version.NoManifest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekVersionNoManifestTest::RunTest(const FString&)
{
    FBPeekExtensionManifest Empty;   // bPresent=false
    const auto Core = V(TEXT("1.0.0"));
    const auto R = FBPeekVersionCheck::CheckCompat(Empty, Core, nullptr);
    TestTrue(TEXT("no manifest → compatible"), R.bCompatible);
    return true;
}

// ---- Core range: happy path -------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekVersionCoreRangeTest,
    "BPeek.Version.CoreRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekVersionCoreRangeTest::RunTest(const FString&)
{
    const auto M = MakeManifest(TEXT("1.0.0"), TEXT("2.0.0"));

    TestTrue (TEXT("1.0.0 in [1.0.0, 2.0.0)"), FBPeekVersionCheck::CheckCompat(M, V(TEXT("1.0.0")), nullptr).bCompatible);
    TestTrue (TEXT("1.5.0 in [1.0.0, 2.0.0)"), FBPeekVersionCheck::CheckCompat(M, V(TEXT("1.5.0")), nullptr).bCompatible);
    TestFalse(TEXT("0.9.9 too old"),           FBPeekVersionCheck::CheckCompat(M, V(TEXT("0.9.9")), nullptr).bCompatible);
    TestFalse(TEXT("2.0.0 boundary (exclusive)"), FBPeekVersionCheck::CheckCompat(M, V(TEXT("2.0.0")), nullptr).bCompatible);
    TestFalse(TEXT("2.1.0 too new"),           FBPeekVersionCheck::CheckCompat(M, V(TEXT("2.1.0")), nullptr).bCompatible);
    return true;
}

// ---- Core range: empty bounds = open range ----------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekVersionCoreOpenRangeTest,
    "BPeek.Version.CoreOpenRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekVersionCoreOpenRangeTest::RunTest(const FString&)
{
    // Only Min set — any version ≥ 1.0.0 passes.
    const auto MinOnly = MakeManifest(TEXT("1.0.0"), TEXT(""));
    TestTrue (TEXT("min only, high version"), FBPeekVersionCheck::CheckCompat(MinOnly, V(TEXT("99.0.0")), nullptr).bCompatible);
    TestFalse(TEXT("min only, low fails"),    FBPeekVersionCheck::CheckCompat(MinOnly, V(TEXT("0.1.0")), nullptr).bCompatible);

    // Only Max set — any version < 2.0.0 passes.
    const auto MaxOnly = MakeManifest(TEXT(""), TEXT("2.0.0"));
    TestTrue (TEXT("max only, low version"),  FBPeekVersionCheck::CheckCompat(MaxOnly, V(TEXT("0.1.0")), nullptr).bCompatible);
    TestFalse(TEXT("max only, high fails"),   FBPeekVersionCheck::CheckCompat(MaxOnly, V(TEXT("2.0.0")), nullptr).bCompatible);

    // Both empty — anything goes (weird but valid).
    const auto Unbounded = MakeManifest(TEXT(""), TEXT(""));
    TestTrue(TEXT("unbounded compat"), FBPeekVersionCheck::CheckCompat(Unbounded, V(TEXT("0.0.1")), nullptr).bCompatible);
    return true;
}

// ---- Target plugin: missing / present / out of range -----------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekVersionTargetPluginTest,
    "BPeek.Version.TargetPlugin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekVersionTargetPluginTest::RunTest(const FString&)
{
    const auto M = MakeManifest(TEXT("1.0.0"), TEXT("2.0.0"),
                                TEXT("Flow"), TEXT("3.0.0"), TEXT("4.0.0"));
    const auto Core = V(TEXT("1.5.0"));

    // Target not installed — flagged.
    TestFalse(TEXT("target missing"),
        FBPeekVersionCheck::CheckCompat(M, Core, nullptr).bCompatible);

    // Target in range.
    const auto FlowOk = V(TEXT("3.5.0"));
    TestTrue(TEXT("target in range"),
        FBPeekVersionCheck::CheckCompat(M, Core, &FlowOk).bCompatible);

    // Target too old.
    const auto FlowOld = V(TEXT("2.9.0"));
    TestFalse(TEXT("target too old"),
        FBPeekVersionCheck::CheckCompat(M, Core, &FlowOld).bCompatible);

    // Target too new.
    const auto FlowNew = V(TEXT("4.0.0"));
    TestFalse(TEXT("target too new (max exclusive)"),
        FBPeekVersionCheck::CheckCompat(M, Core, &FlowNew).bCompatible);
    return true;
}

// ---- Malformed bounds rejected loudly ---------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekVersionMalformedTest,
    "BPeek.Version.Malformed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekVersionMalformedTest::RunTest(const FString&)
{
    // Garbage in CoreVersionMin → rejected.
    auto M = MakeManifest(TEXT("one-point-oh"), TEXT(""));
    const auto Core = V(TEXT("1.0.0"));
    const auto R = FBPeekVersionCheck::CheckCompat(M, Core, nullptr);
    TestFalse(TEXT("garbage min rejected"), R.bCompatible);
    TestTrue (TEXT("reason mentions Min"), R.Reason.Contains(TEXT("Min")));

    // Garbage in TargetVersionMax → rejected after core passes.
    auto M2 = MakeManifest(TEXT("1.0.0"), TEXT("2.0.0"),
                           TEXT("Flow"), TEXT("3.0.0"), TEXT("xyz"));
    const auto TargetVer = V(TEXT("3.0.0"));
    const auto R2 = FBPeekVersionCheck::CheckCompat(M2, Core, &TargetVer);
    TestFalse(TEXT("garbage target-max rejected"), R2.bCompatible);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
