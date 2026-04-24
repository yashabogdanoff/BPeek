# Adds `{ "Name": "BPeek", "Enabled": true }` to a UE .uproject's Plugins
# array, preserving existing indentation (tabs or spaces — whatever UE uses).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File patch-uproject.ps1 "C:\path\to\Project.uproject"
#
# Idempotent: if BPeek entry already exists, no-op. Safe to run repeatedly.
# Preserves original byte-for-byte outside the inserted fragment (no
# JSON round-trip that would renormalize indentation / line endings).

param(
    [Parameter(Mandatory=$true)][string]$UProjectPath,
    [string[]]$PluginNames = @('BPeek')
)

# When invoked via `powershell -File script.ps1 -PluginNames a,b,c`, the CLI
# parser passes "a,b,c" as a single string, not an array. Split on comma so
# callers can pass a simple cmd-style list.
$PluginNames = $PluginNames | ForEach-Object { $_.Split(',') } | Where-Object { $_ }

if (-not (Test-Path $UProjectPath)) {
    Write-Host "[bpeek]     ERROR: .uproject not found: $UProjectPath"
    exit 1
}

# Read as UTF-8 no-BOM to preserve bytes exactly.
$enc = New-Object System.Text.UTF8Encoding $false
$content = $enc.GetString([System.IO.File]::ReadAllBytes($UProjectPath))

# Filter out plugins already present so re-runs are idempotent. After the
# 4→1 merge there is only one plugin ("BPeek") to patch in; extension
# functionality (EnhancedInput/GAS/Flow rendering) lives as modules inside
# BPeek.uplugin and is gated by BPEEK_WITH_* build-time defines. The
# -PluginNames parameter is kept so callers can still pass additional
# names if they need to enable other plugins in the same patch.
$toAdd = @()
foreach ($name in $PluginNames) {
    $escaped = [regex]::Escape($name)
    if ($content -match "`"Name`"\s*:\s*`"$escaped`"") {
        Write-Host "[bpeek]     already patched: $name entry exists"
    } else {
        $toAdd += $name
    }
}
if ($toAdd.Count -eq 0) {
    Write-Host '[bpeek]     no plugins to add'
    exit 0
}

# Find the Plugins array opening `[`
$m = [regex]::Match($content, '"Plugins"\s*:\s*\[')
if (-not $m.Success) {
    Write-Host '[bpeek]     no "Plugins" array in .uproject — add manually:'
    Write-Host '[bpeek]       "Plugins": [ { "Name": "BPeek", "Enabled": true } ]'
    exit 1
}
$openEnd = $m.Index + $m.Length

# Find matching closing `]` (scan forward balancing brackets).
$depth = 1
$closeIdx = -1
for ($i = $openEnd; $i -lt $content.Length; $i++) {
    $ch = $content[$i]
    if ($ch -eq '[') { $depth++ }
    elseif ($ch -eq ']') {
        $depth--
        if ($depth -eq 0) { $closeIdx = $i; break }
    }
}
if ($closeIdx -lt 0) {
    Write-Host '[bpeek]     ERROR: could not find closing ] of Plugins array'
    exit 1
}

# Detect indentation of the closing `]` — that's the array-level indent.
# Entry indent = closeIndent + one more level (tab or 4 spaces, based on what's used).
$nlBefore = $content.LastIndexOf("`n", $closeIdx)
$closeIndent = if ($nlBefore -ge 0) { $content.Substring($nlBefore + 1, $closeIdx - $nlBefore - 1) } else { '' }

# Figure out whether the file uses tabs or spaces by looking at closeIndent.
# If contains tab → use tabs. Otherwise fall back to 4 spaces per level
# (typical UE project default). Entry indent = close indent + one level.
if ($closeIndent.Contains("`t")) {
    $levelUp = "`t"
} else {
    $levelUp = '    '  # 4 spaces — matches UE default when no tab detected
}
$entryIndent = $closeIndent + $levelUp
$fieldIndent = $entryIndent + $levelUp

# Detect line ending style (CRLF vs LF) from the file.
$nl = if ($content.Contains("`r`n")) { "`r`n" } else { "`n" }

# Detect whether array is empty.
$between = $content.Substring($openEnd, $closeIdx - $openEnd).Trim()
$lastClose = $content.LastIndexOf('}', $closeIdx)

# Build the entries block — one {..} per plugin, joined by `,`.
$entries = @()
foreach ($name in $toAdd) {
    $entries += ($entryIndent + '{' + $nl +
                 $fieldIndent + '"Name": "' + $name + '",' + $nl +
                 $fieldIndent + '"Enabled": true' + $nl +
                 $entryIndent + '}')
}
$entriesJoined = [string]::Join(',' + $nl, $entries)

if ($between.Length -eq 0) {
    # Empty array — insert as only entries.
    $content = $content.Substring(0, $closeIdx) + $nl + $entriesJoined + $nl + $closeIndent + $content.Substring($closeIdx)
}
elseif ($lastClose -gt $openEnd) {
    # Non-empty — append after last entry's `}`.
    $content = $content.Substring(0, $lastClose + 1) + ',' + $nl + $entriesJoined + $content.Substring($lastClose + 1)
}
else {
    Write-Host '[bpeek]     ERROR: could not locate last entry in Plugins array'
    exit 1
}

# Write back preserving UTF-8 no-BOM.
[System.IO.File]::WriteAllText($UProjectPath, $content, $enc)
Write-Host ('[bpeek]     patched: added ' + ($toAdd -join ', ') + ' to Plugins array (indentation preserved)')
exit 0
