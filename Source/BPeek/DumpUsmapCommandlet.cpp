#include "DumpUsmapCommandlet.h"
#include "BPeekLog.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"

enum class EUsmapPropType : uint8
{
    ByteProperty, BoolProperty, IntProperty, FloatProperty, ObjectProperty,
    NameProperty, DelegateProperty, DoubleProperty, ArrayProperty, StructProperty,
    StrProperty, TextProperty, InterfaceProperty, MulticastDelegateProperty,
    WeakObjectProperty, LazyObjectProperty, AssetObjectProperty, SoftObjectProperty,
    UInt64Property, UInt32Property, UInt16Property, Int64Property, Int16Property,
    Int8Property, MapProperty, SetProperty, EnumProperty, FieldPathProperty,
};

static EUsmapPropType MapPropType(const FProperty* P)
{
    if (P->IsA<FByteProperty>()) { return CastField<FByteProperty>(P)->Enum ? EUsmapPropType::EnumProperty : EUsmapPropType::ByteProperty; }
    if (P->IsA<FEnumProperty>()) return EUsmapPropType::EnumProperty;
    if (P->IsA<FBoolProperty>()) return EUsmapPropType::BoolProperty;
    if (P->IsA<FIntProperty>()) return EUsmapPropType::IntProperty;
    if (P->IsA<FFloatProperty>()) return EUsmapPropType::FloatProperty;
    if (P->IsA<FObjectProperty>() || P->IsA<FClassProperty>()) return EUsmapPropType::ObjectProperty;
    if (P->IsA<FNameProperty>()) return EUsmapPropType::NameProperty;
    if (P->IsA<FDelegateProperty>()) return EUsmapPropType::DelegateProperty;
    if (P->IsA<FDoubleProperty>()) return EUsmapPropType::DoubleProperty;
    if (P->IsA<FArrayProperty>()) return EUsmapPropType::ArrayProperty;
    if (P->IsA<FStructProperty>()) return EUsmapPropType::StructProperty;
    if (P->IsA<FStrProperty>()) return EUsmapPropType::StrProperty;
    if (P->IsA<FTextProperty>()) return EUsmapPropType::TextProperty;
    if (P->IsA<FInterfaceProperty>()) return EUsmapPropType::InterfaceProperty;
    if (P->IsA<FMulticastDelegateProperty>()) return EUsmapPropType::MulticastDelegateProperty;
    if (P->IsA<FWeakObjectProperty>()) return EUsmapPropType::WeakObjectProperty;
    if (P->IsA<FLazyObjectProperty>()) return EUsmapPropType::LazyObjectProperty;
    if (P->IsA<FSoftObjectProperty>() || P->IsA<FSoftClassProperty>()) return EUsmapPropType::SoftObjectProperty;
    if (P->IsA<FUInt64Property>()) return EUsmapPropType::UInt64Property;
    if (P->IsA<FUInt32Property>()) return EUsmapPropType::UInt32Property;
    if (P->IsA<FUInt16Property>()) return EUsmapPropType::UInt16Property;
    if (P->IsA<FInt64Property>()) return EUsmapPropType::Int64Property;
    if (P->IsA<FInt16Property>()) return EUsmapPropType::Int16Property;
    if (P->IsA<FInt8Property>()) return EUsmapPropType::Int8Property;
    if (P->IsA<FMapProperty>()) return EUsmapPropType::MapProperty;
    if (P->IsA<FSetProperty>()) return EUsmapPropType::SetProperty;
    if (P->IsA<FFieldPathProperty>()) return EUsmapPropType::FieldPathProperty;
    return EUsmapPropType::ByteProperty;
}

struct FNameTable {
    TArray<FString> Names; TMap<FString, int32> Map;
    int32 Add(const FString& S) { if (int32* I = Map.Find(S)) return *I; int32 I = Names.Num(); Names.Add(S); Map.Add(S, I); return I; }
};

static void WriteName(TArray<uint8>& B, FNameTable& T, const FString& S) { int32 I = T.Add(S); B.Append(reinterpret_cast<uint8*>(&I), 4); }
static void WriteU8(TArray<uint8>& B, uint8 V) { B.Add(V); }
static void WriteU16(TArray<uint8>& B, uint16 V) { B.Append(reinterpret_cast<uint8*>(&V), 2); }
static void WriteU32(TArray<uint8>& B, uint32 V) { B.Append(reinterpret_cast<uint8*>(&V), 4); }
static void WriteI32(TArray<uint8>& B, int32 V) { B.Append(reinterpret_cast<uint8*>(&V), 4); }

static void WritePropData(TArray<uint8>& B, FNameTable& T, const FProperty* P, EUsmapPropType Type);
static void WritePropData(TArray<uint8>& B, FNameTable& T, const FProperty* P, EUsmapPropType Type)
{
    if (Type == EUsmapPropType::EnumProperty && P->IsA<FByteProperty>()) {
        WriteU8(B, (uint8)EUsmapPropType::EnumProperty);
        WriteU8(B, (uint8)EUsmapPropType::ByteProperty);
        WriteName(B, T, CastField<FByteProperty>(P)->Enum ? CastField<FByteProperty>(P)->Enum->GetName() : TEXT("None"));
        return;
    }
    WriteU8(B, (uint8)Type);
    switch (Type) {
    case EUsmapPropType::EnumProperty: {
        auto* EP = CastField<FEnumProperty>(P);
        if (EP && EP->GetUnderlyingProperty()) WritePropData(B, T, EP->GetUnderlyingProperty(), MapPropType(EP->GetUnderlyingProperty()));
        else WriteU8(B, (uint8)EUsmapPropType::ByteProperty);
        WriteName(B, T, EP && EP->GetEnum() ? EP->GetEnum()->GetName() : TEXT("None"));
        break; }
    case EUsmapPropType::StructProperty: {
        auto* SP = CastField<FStructProperty>(P);
        WriteName(B, T, SP && SP->Struct ? SP->Struct->GetName() : TEXT("None"));
        break; }
    case EUsmapPropType::ArrayProperty: {
        auto* AP = CastField<FArrayProperty>(P);
        if (AP && AP->Inner) WritePropData(B, T, AP->Inner, MapPropType(AP->Inner));
        else WriteU8(B, (uint8)EUsmapPropType::ByteProperty);
        break; }
    case EUsmapPropType::SetProperty: {
        auto* SP = CastField<FSetProperty>(P);
        if (SP && SP->ElementProp) WritePropData(B, T, SP->ElementProp, MapPropType(SP->ElementProp));
        else WriteU8(B, (uint8)EUsmapPropType::ByteProperty);
        break; }
    case EUsmapPropType::MapProperty: {
        auto* MP = CastField<FMapProperty>(P);
        if (MP && MP->KeyProp) WritePropData(B, T, MP->KeyProp, MapPropType(MP->KeyProp)); else WriteU8(B, (uint8)EUsmapPropType::ByteProperty);
        if (MP && MP->ValueProp) WritePropData(B, T, MP->ValueProp, MapPropType(MP->ValueProp)); else WriteU8(B, (uint8)EUsmapPropType::ByteProperty);
        break; }
    default: break;
    }
}

// Write a usmap V0 file from the given structs and enums.
static bool WriteUsmapFile(const TArray<UStruct*>& Structs, const TArray<UEnum*>& Enums, const FString& OutputPath)
{
    FNameTable NT; TArray<uint8> Body;

    WriteU32(Body, (uint32)Enums.Num());
    for (UEnum* E : Enums) {
        WriteName(Body, NT, E->GetName());
        int32 C = E->NumEnums();
        if (C > 0 && E->GetNameStringByIndex(C-1).EndsWith(TEXT("_MAX"))) C--;
        WriteU8(Body, (uint8)FMath::Clamp(C,0,255));
        for (int32 i = 0; i < FMath::Clamp(C,0,255); i++) {
            FString N = E->GetNameStringByIndex(i); int32 CI;
            if (N.FindChar(TEXT(':'), CI)) N = N.Mid(CI+2);
            WriteName(Body, NT, N);
        }
    }

    WriteU32(Body, (uint32)Structs.Num());
    for (UStruct* S : Structs) {
        WriteName(Body, NT, S->GetName());
        if (UStruct* Super = S->GetSuperStruct()) WriteName(Body, NT, Super->GetName());
        else WriteU32(Body, 0xFFFFFFFF);
        struct E { const FProperty* P; uint16 I; uint8 A; };
        TArray<E> Props; uint16 PC = 0;
        for (FField* F = S->ChildProperties; F; F = F->Next)
            if (const FProperty* P = CastField<FProperty>(F)) {
                E e; e.P=P; e.I=PC; e.A=(uint8)FMath::Clamp(P->ArrayDim,1,255);
                Props.Add(e); PC += e.A;
            }
        WriteU16(Body, PC); WriteU16(Body, (uint16)Props.Num());
        for (const E& e : Props) { WriteU16(Body, e.I); WriteU8(Body, e.A); WriteName(Body, NT, e.P->GetName()); WritePropData(Body, NT, e.P, MapPropType(e.P)); }
    }

    TArray<uint8> File;
    WriteU16(File, 0x30C4); WriteU8(File, 0); WriteU8(File, 0);
    WriteU32(File, Body.Num()); WriteU32(File, Body.Num());
    WriteI32(File, NT.Names.Num());
    for (const FString& N : NT.Names) {
        uint8 L = (uint8)FMath::Clamp(N.Len(),0,255); WriteU8(File, L);
        auto A = StringCast<ANSICHAR>(*N);
        for (int32 i = 0; i < L; i++) File.Add((uint8)A.Get()[i]);
    }
    File.Append(Body);

    if (FFileHelper::SaveArrayToFile(File, *OutputPath)) {
        UE_LOG(LogBPeek, Display, TEXT("[BPeek] Written: %d bytes (%d schemas) -> %s"), File.Num(), Structs.Num(), *OutputPath);
        return true;
    }
    UE_LOG(LogBPeek, Error, TEXT("[BPeek] Failed to write: %s"), *OutputPath);
    return false;
}

UDumpUsmapCommandlet::UDumpUsmapCommandlet() { IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true; }

int32 UDumpUsmapCommandlet::Main(const FString& Params)
{
    FString OutputPath;
    if (!FParse::Value(*Params, TEXT("-output="), OutputPath))
        OutputPath = FPaths::ProjectDir() / TEXT("Mappings.usmap");
    OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

    UE_LOG(LogBPeek, Display, TEXT("[BPeek] Collecting UClass/UScriptStruct/UFunction/UEnum from object iterator..."));
    TArray<UStruct*> Structs; TArray<UEnum*> Enums;
    int32 ClassCount = 0, ScriptStructCount = 0, FunctionCount = 0;
    for (TObjectIterator<UObject> It; It; ++It) {
        UObject* O = *It;
        if (O->GetClass() == UClass::StaticClass()) { Structs.Add(static_cast<UStruct*>(O)); ClassCount++; }
        else if (O->GetClass() == UScriptStruct::StaticClass()) { Structs.Add(static_cast<UStruct*>(O)); ScriptStructCount++; }
        else if (O->GetClass() == UFunction::StaticClass()) { Structs.Add(static_cast<UStruct*>(O)); FunctionCount++; }
        else if (O->GetClass() == UEnum::StaticClass()) Enums.Add(static_cast<UEnum*>(O));
    }
    UE_LOG(LogBPeek, Display, TEXT("[BPeek] Collected: %d UClass, %d UScriptStruct, %d UFunction, %d UEnum (total structs=%d)"),
        ClassCount, ScriptStructCount, FunctionCount, Enums.Num(), Structs.Num());

    // 1. Filtered usmap for cooked (CPF_EditorOnly excluded → safe)
    TArray<UStruct*> Filtered;
    int32 FilteredOut = 0;
    for (UStruct* S : Structs) {
        bool Skip = false;
        for (FField* F = S->ChildProperties; F; F = F->Next)
            if (const FProperty* P = CastField<FProperty>(F))
                if (P->HasAnyPropertyFlags(CPF_EditorOnly)) { Skip = true; break; }
        if (!Skip) Filtered.Add(S); else FilteredOut++;
    }
    UE_LOG(LogBPeek, Display, TEXT("[BPeek] CPF_EditorOnly filter: %d passed, %d filtered out"), Filtered.Num(), FilteredOut);
    WriteUsmapFile(Filtered, Enums, OutputPath);

    // 2. Full usmap for uncooked (ALL properties → NewVariables, WidgetTree, etc.)
    FString EditorPath = OutputPath.Replace(TEXT(".usmap"), TEXT(".editor.usmap"));
    WriteUsmapFile(Structs, Enums, EditorPath);

    UE_LOG(LogBPeek, Display, TEXT("[BPeek] Success: cooked=%d schemas, editor=%d schemas"), Filtered.Num(), Structs.Num());
    return 0;
}