#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "FlowAsset.h"
#include "Nodes/FlowNode.h"
#include "Nodes/FlowPin.h"

class FBPeekFlowWriter
{
public:
    static void Write(FBPeekMarkdownWriter& W, UFlowAsset* FA,
                      const TMap<FString, TArray<FString>>& Refs = TMap<FString, TArray<FString>>(),
                      const TSet<FString>& KnownNormalized = TSet<FString>(),
                      bool bVerboseMode = false)   // Node blocks already dense (properties + connection list); compact reuses verbose layout and only shortens the Asset path row.
    {
        if (!FA) return;
        const FString AssetPath = FA->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);

        // Collect nodes + edges in a single pass so we know the total before
        // writing the header (parity with C# which knows Count() upfront).
        struct FNodeInfo
        {
            FString Id; FString ClassName; FString DisplayName;
            TArray<TPair<FString, FString>> Properties;  // sorted alphabetically
            TArray<TTuple<FName, FGuid, FName>> Edges;   // (fromPin, toGuid, toPin)
        };
        TArray<FNodeInfo> Nodes;
        int32 EdgeCount = 0;

        const TMap<FGuid, UFlowNode*>& Map = FA->GetNodes();
        for (const TPair<FGuid, UFlowNode*>& KV : Map)
        {
            UFlowNode* FN = KV.Value;
            if (!FN) continue;
            FNodeInfo NI;
            NI.Id = KV.Key.ToString(EGuidFormats::DigitsWithHyphens);
            NI.ClassName = FN->GetClass()->GetName();
#if WITH_EDITOR
            NI.DisplayName = FN->GetNodeTitle().ToString();
#else
            NI.DisplayName = NI.ClassName;
#endif
            if (NI.DisplayName.IsEmpty()) NI.DisplayName = NI.ClassName;

            for (TFieldIterator<FProperty> PIt(FN->GetClass()); PIt; ++PIt)
            {
                FProperty* P = *PIt;
                if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
                if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
                if (P->GetOwnerClass() == UFlowNode::StaticClass()) continue;
                const FString PropName = P->GetName();
                if (PropName.StartsWith(TEXT("bOverride_")))
                {
                    if (FBoolProperty* BP = CastField<FBoolProperty>(P);
                        BP && !BP->GetPropertyValue_InContainer(FN)) continue;
                }
                FString Val;
                if (FObjectProperty* OP = CastField<FObjectProperty>(P))
                {
                    UObject* V = OP->GetObjectPropertyValue_InContainer(FN);
                    if (V) Val = V->GetPathName();
                }
                else if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P))
                {
                    const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(FN);
                    if (!V.IsNull()) Val = V.ToString();
                }
                else if (FTextProperty* TP = CastField<FTextProperty>(P))
                {
                    const FText V = TP->GetPropertyValue_InContainer(FN);
                    if (!V.IsEmpty()) Val = V.ToString();
                }
                else
                {
                    P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(FN), nullptr, nullptr, PPF_None);
                    if (Val == TEXT("None") || Val == TEXT("\"\"") || Val == TEXT("()"))
                        Val = FString();
                }
                if (!Val.IsEmpty()) NI.Properties.Add({ PropName, Val });
            }
            // Ordinal case-sensitive sort (see DataAssetWriter comment).
            NI.Properties.Sort([](const TPair<FString,FString>& A, const TPair<FString,FString>& B){
                return A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0;
            });

            for (const FName& OutName : FN->GetOutputNames())
            {
                const FConnectedPin Conn = FN->GetConnection(OutName);
                if (!Conn.NodeGuid.IsValid()) continue;
                NI.Edges.Add(MakeTuple(OutName, Conn.NodeGuid, Conn.PinName));
                ++EdgeCount;
            }
            Nodes.Add(MoveTemp(NI));
        }

        // Header
        W.WriteHeading(1, FString::Printf(TEXT("%s (Flow Asset)"), *FA->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        W.WriteMetaRow(TEXT("Nodes"), FString::FromInt(Nodes.Num()));
        W.WriteMetaRow(TEXT("Connections"), FString::FromInt(EdgeCount));
        W.WriteLine();

        if (Nodes.Num() == 0)
        {
            W.WriteLine(TEXT("_No node data available (regenerate with `bpeek setup` to include flow metadata)._"));
            FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
            return;
        }

        // Mermaid graph — assign n0/n1/… IDs for diagram readability.
        TMap<FString, FString> MermaidIds;
        for (int32 i = 0; i < Nodes.Num(); ++i)
        {
            const FString MId = FString::Printf(TEXT("n%d"), i);
            MermaidIds.Add(Nodes[i].Id, MId);
        }

        W.WriteHeading(2, TEXT("Graph"));
        W.WriteLine();
        W.WriteLine(TEXT("```mermaid"));
        W.WriteLine(TEXT("graph LR"));
        for (const FNodeInfo& N : Nodes)
        {
            const FString* MId = MermaidIds.Find(N.Id);
            if (!MId) continue;
            W.WriteLine(FString::Printf(TEXT("    %s[\"%s\"]"), **MId, *EscapeMermaid(N.DisplayName)));
        }
        for (const FNodeInfo& N : Nodes)
        {
            const FString* From = MermaidIds.Find(N.Id);
            if (!From) continue;
            for (const auto& E : N.Edges)
            {
                const FString ToGuidStr = E.Get<1>().ToString(EGuidFormats::DigitsWithHyphens);
                const FString* To = MermaidIds.Find(ToGuidStr);
                if (!To) continue;
                const FString FromPin = E.Get<0>().ToString();
                FString PinLabel;
                if (!FromPin.IsEmpty() && FromPin != TEXT("Out"))
                    PinLabel = FString::Printf(TEXT("|%s|"), *EscapeMermaid(FromPin));
                W.WriteLine(FString::Printf(TEXT("    %s -->%s %s"), **From, *PinLabel, **To));
            }
        }
        W.WriteLine(TEXT("```"));
        W.WriteLine();

        // Per-node sections
        W.WriteHeading(2, TEXT("Nodes"));
        W.WriteLine();
        for (const FNodeInfo& N : Nodes)
        {
            W.WriteHeading(3, N.DisplayName);
            W.WriteLine(FString::Printf(TEXT("- Class: `%s`"), *N.ClassName));
            W.WriteLine(FString::Printf(TEXT("- Id: `%s`"), *N.Id));
            for (const TPair<FString, FString>& P : N.Properties)
            {
                FString V = FBPeekTextUnwrap::Unwrap(P.Value);
                V = FBPeekAssetLinks::Linkify(V, AssetPath, KnownNormalized);
                W.WriteLine(FString::Printf(TEXT("- %s: %s"), *P.Key, *V));
            }
            W.WriteLine();
        }

        FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
    }

private:
    static FString EscapeMermaid(const FString& S)
    {
        FString Out = S;
        Out.ReplaceInline(TEXT("\""), TEXT("&quot;"));
        Out.ReplaceInline(TEXT("\n"), TEXT(" "));
        return Out;
    }
};