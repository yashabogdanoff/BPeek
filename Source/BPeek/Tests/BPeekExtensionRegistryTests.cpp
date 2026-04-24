#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Features/IModularFeatures.h"
#include "../BPeekExtensionAPI.h"
#include "../BPeekExtensionRegistry.h"

/* Tests for IBPeekExtension / FBPeekExtensionRegistry. Dummy in-process
 * extensions verify registration, priority sort, CanHandle dispatch,
 * and API-version compat gate.
 *
 *   UnrealEditor-Cmd.exe <Project>.uproject
 *       -ExecCmds="Automation RunTests BPeek.Extension.; Quit"
 *       -unattended -nosplash -nop4
 */

namespace
{
    /** Minimal extension for tests. All virtuals have trivial defaults;
     *  tests override Priority / CanHandle / GetAPIVersion as needed. */
    class FDummyExt : public IBPeekExtension
    {
    public:
        FName Id = TEXT("test.dummy");
        int32 Priority = 100;
        int32 APIVersion = BPEEK_EXTENSION_API_VERSION;
        TFunction<bool(UObject*)> CanHandleFn;
        TArray<UClass*> Handled;

        FName GetId() const override { return Id; }
        FString GetVersionName() const override { return TEXT("0.0.1"); }
        int32 GetAPIVersion() const override { return APIVersion; }
        int32 GetPriority() const override { return Priority; }
        bool CanHandle(UObject* Asset) const override
        {
            return CanHandleFn ? CanHandleFn(Asset) : false;
        }
        TArray<UClass*> GetHandledClasses() const override { return Handled; }
        void Write(FBPeekMarkdownWriter&, UObject*, const FBPeekScanContext&) override {}
    };

    /** RAII registration so tests don't leak extensions into other tests. */
    struct FScopedRegister
    {
        IBPeekExtension* Ext;
        explicit FScopedRegister(IBPeekExtension* E) : Ext(E)
        {
            IModularFeatures::Get().RegisterModularFeature(
                IBPeekExtension::GetModularFeatureName(), Ext);
        }
        ~FScopedRegister()
        {
            IModularFeatures::Get().UnregisterModularFeature(
                IBPeekExtension::GetModularFeatureName(), Ext);
        }
    };
}

// ---- Registration & GetAll --------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekExtensionRegisterTest,
    "BPeek.Extension.Register",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekExtensionRegisterTest::RunTest(const FString&)
{
    const int32 Before = FBPeekExtensionRegistry::GetAll().Num();

    FDummyExt A, B;
    A.Id = TEXT("a"); B.Id = TEXT("b");
    {
        FScopedRegister RA(&A);
        FScopedRegister RB(&B);

        TArray<IBPeekExtension*> All = FBPeekExtensionRegistry::GetAll();
        TestEqual(TEXT("two extensions visible"), All.Num(), Before + 2);
        TestTrue(TEXT("A present"), All.Contains(&A));
        TestTrue(TEXT("B present"), All.Contains(&B));
    }

    // Scope expired — both unregistered.
    TestEqual(TEXT("after scope, back to baseline"),
        FBPeekExtensionRegistry::GetAll().Num(), Before);
    return true;
}

// ---- Priority ordering ------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekExtensionPriorityTest,
    "BPeek.Extension.Priority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekExtensionPriorityTest::RunTest(const FString&)
{
    FDummyExt Low, Mid, High;
    Low.Id = TEXT("low");  Low.Priority = 10;
    Mid.Id = TEXT("mid");  Mid.Priority = 100;
    High.Id = TEXT("high"); High.Priority = 200;

    // Register in reversed order — sort must not rely on insertion order.
    FScopedRegister RLow(&Low);
    FScopedRegister RMid(&Mid);
    FScopedRegister RHigh(&High);

    TArray<IBPeekExtension*> All = FBPeekExtensionRegistry::GetAll();
    int32 HighIdx = All.IndexOfByKey(&High);
    int32 MidIdx  = All.IndexOfByKey(&Mid);
    int32 LowIdx  = All.IndexOfByKey(&Low);

    TestTrue(TEXT("high found"), HighIdx != INDEX_NONE);
    TestTrue(TEXT("mid found"),  MidIdx  != INDEX_NONE);
    TestTrue(TEXT("low found"),  LowIdx  != INDEX_NONE);
    TestTrue(TEXT("high before mid"), HighIdx < MidIdx);
    TestTrue(TEXT("mid before low"),  MidIdx  < LowIdx);
    return true;
}

// ---- FindFor dispatch -------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekExtensionFindForTest,
    "BPeek.Extension.FindFor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekExtensionFindForTest::RunTest(const FString&)
{
    FDummyExt Low, High;
    Low.Id = TEXT("low");   Low.Priority = 50;
    High.Id = TEXT("high"); High.Priority = 150;

    // Both claim the same asset; highest priority wins.
    Low.CanHandleFn  = [](UObject*) { return true; };
    High.CanHandleFn = [](UObject*) { return true; };

    FScopedRegister RLow(&Low);
    FScopedRegister RHigh(&High);

    UObject* Dummy = GetTransientPackage();   // any non-null UObject
    IBPeekExtension* Chosen = FBPeekExtensionRegistry::FindFor(Dummy);
    TestEqual(TEXT("high-priority extension wins"), Chosen, static_cast<IBPeekExtension*>(&High));

    // If neither claims — FindFor returns nullptr.
    Low.CanHandleFn  = [](UObject*) { return false; };
    High.CanHandleFn = [](UObject*) { return false; };
    TestNull(TEXT("no claimers → nullptr"), FBPeekExtensionRegistry::FindFor(Dummy));
    return true;
}

// ---- API-version compat gate ------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekExtensionAPICompatTest,
    "BPeek.Extension.APICompat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekExtensionAPICompatTest::RunTest(const FString&)
{
    FDummyExt OldOk, Newer, Current;
    OldOk.Id    = TEXT("old");     OldOk.APIVersion    = BPEEK_EXTENSION_API_VERSION;
    Current.Id  = TEXT("current"); Current.APIVersion  = BPEEK_EXTENSION_API_VERSION;
    Newer.Id    = TEXT("newer");   Newer.APIVersion    = BPEEK_EXTENSION_API_VERSION + 1;

    TestTrue (TEXT("same API compatible"), FBPeekExtensionRegistry::IsAPICompatible(&Current));
    TestTrue (TEXT("equal API compatible"), FBPeekExtensionRegistry::IsAPICompatible(&OldOk));
    TestFalse(TEXT("newer API refused"),   FBPeekExtensionRegistry::IsAPICompatible(&Newer));

    // Ensure FindFor also skips the incompatible one.
    Newer.CanHandleFn = [](UObject*) { return true; };
    FScopedRegister RNewer(&Newer);
    TestNull(TEXT("FindFor skips incompatible"),
        FBPeekExtensionRegistry::FindFor(GetTransientPackage()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
