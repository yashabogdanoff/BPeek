#pragma once
#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

/**
 * Thin sink for incremental markdown construction. Each Write* call appends
 * a line (or a line fragment) to an internal buffer. SaveTo() writes the
 * buffer to disk as UTF-8 without BOM, creating parent directories as
 * needed. Intentionally dependency-free so per-asset writers can be
 * header-only and tested in isolation.
 */
class FBPeekMarkdownWriter
{
public:
    FBPeekMarkdownWriter() = default;

    // Use CRLF to match C# StreamWriter default on Windows — byte-for-byte
    // parity is what the snapshot diff against the C# renderer needs.
    void WriteLine() { Buffer.Append(TEXT("\r\n")); }
    void WriteLine(const FString& Line) { Buffer.Append(Line); Buffer.Append(TEXT("\r\n")); }
    void Write(const FString& Fragment) { Buffer.Append(Fragment); }

    /** "# Heading" / "## Heading" — Level clamps to [1,6]. */
    void WriteHeading(int32 Level, const FString& Text)
    {
        Level = FMath::Clamp(Level, 1, 6);
        FString Prefix;
        for (int32 i = 0; i < Level; ++i) Prefix.AppendChar(TEXT('#'));
        WriteLine(FString::Printf(TEXT("%s %s"), *Prefix, *Text));
    }

    /** "- text" */
    void WriteBullet(const FString& Text) { WriteLine(FString::Printf(TEXT("- %s"), *Text)); }

    /** "- **Key**: Value" — the most common meta-field row. */
    void WriteMetaRow(const FString& Key, const FString& Value)
    {
        WriteLine(FString::Printf(TEXT("- **%s**: %s"), *Key, *Value));
    }

    /** "- **Key**: `Value`" — same but wrapping value in inline code. */
    void WriteMetaRowCode(const FString& Key, const FString& Value)
    {
        WriteLine(FString::Printf(TEXT("- **%s**: `%s`"), *Key, *Value));
    }

    /** Raw access for per-asset writers that need to emit nested structures. */
    FString& Raw() { return Buffer; }
    const FString& Raw() const { return Buffer; }

    /** Create parent dirs if needed and write buffer as UTF-8 (no BOM). */
    bool SaveTo(const FString& AbsolutePath) const
    {
        const FString Dir = FPaths::GetPath(AbsolutePath);
        if (!Dir.IsEmpty()) IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
        return FFileHelper::SaveStringToFile(
            Buffer, *AbsolutePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

private:
    FString Buffer;
};