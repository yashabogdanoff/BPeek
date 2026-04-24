#include "BPeekSettings.h"

UBPeekSettings::UBPeekSettings()
{
    // Include-style default: only dump project-owned mount points.
    // Everything else — /Engine, /Script, engine-distributed plugins
    // (HairStrands, ChaosNiagara, Niagara, …) — stays out by default
    // because it doesn't match. Much more robust than maintaining a
    // hand-curated exclude list of every engine-plugin mount point,
    // because each new UE version / user-enabled plugin adds more.
    //
    // /Game is the standard project content root.
    // /Module_*/** catches per-module GameFeature plugin layouts.
    // Add your own project plugin mount points here if needed — e.g.
    // /MyPlugin/** if you ship /MyPlugin under Plugins/MyPlugin/Content/.
    IncludePatterns = {
        TEXT("/Game/**"),
        TEXT("/Module_*/**"),
    };

    // Exclude applied AFTER include. Dumps everything that passed
    // Include unless one of these rules strips it again. Useful for
    // marketplace / vendor dirs living under /Game.
    ExcludePatterns = {
        TEXT("/Game/Megascans/**"),
        TEXT("/Game/Quixel/**"),
    };
}
