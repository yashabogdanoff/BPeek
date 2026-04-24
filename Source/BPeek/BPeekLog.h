#pragma once
#include "Logging/LogMacros.h"

/**
 * Single log category for BPeek. Replaces LogTemp across the plugin so
 * users can control verbosity without drowning in engine-wide log
 * traffic. Category is exported so the editor module can use it too.
 *
 * Level policy:
 *   Log         (default) — lifecycle, phase changes, final counts.
 *                           Typical user run shows ~20-40 lines.
 *   Verbose     — per-asset detail (one line per loaded/written asset).
 *                 ~2×N lines on an N-asset project.
 *   VeryVerbose — walker internals (per-node, per-pin). Development-only.
 *   Warning     — recoverable issues (bad filter rule, failed asset load).
 *   Error       — hard failures (MD save failed, JSON save failed).
 *
 * To bump verbosity at runtime:
 *   `LogCmds` command line flag: `-LogCmds="LogBPeek Verbose"`
 *   Or in DefaultEngine.ini:
 *     [Core.Log]
 *     LogBPeek=Verbose
 */
BPEEK_API DECLARE_LOG_CATEGORY_EXTERN(LogBPeek, Log, All);
