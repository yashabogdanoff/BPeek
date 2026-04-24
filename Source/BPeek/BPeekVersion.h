#pragma once

//
// Single source of truth for BPeek core plugin semver. Extension
// plugins read these (compile-time constant) to declare their
// "BPeek.CoreVersionMin/Max" ranges in their uplugin, and the
// runtime version-check compares against this value.
//
// Bump policy:
//   - MAJOR (X.0.0)   — breaking change to IBPeekExtension or to the
//                       BPeek uplugin-field schema. Also bumps the
//                       BPEEK_EXTENSION_API_VERSION macro in
//                       BPeekExtensionAPI.h.
//   - MINOR (0.X.0)   — new optional extension API methods with default
//                       implementations, new public helpers, new asset
//                       types handled by core.
//   - PATCH (0.0.X)   — bugfixes, no API surface changes.
//
// Target cadence: at most one MAJOR bump per year.
//

#define BPEEK_PLUGIN_VERSION_MAJOR  1
#define BPEEK_PLUGIN_VERSION_MINOR  0
#define BPEEK_PLUGIN_VERSION_PATCH  0

#define BPEEK_STRINGIFY_INNER(x) #x
#define BPEEK_STRINGIFY(x) BPEEK_STRINGIFY_INNER(x)

#define BPEEK_PLUGIN_VERSION_NAME \
    BPEEK_STRINGIFY(BPEEK_PLUGIN_VERSION_MAJOR) "." \
    BPEEK_STRINGIFY(BPEEK_PLUGIN_VERSION_MINOR) "." \
    BPEEK_STRINGIFY(BPEEK_PLUGIN_VERSION_PATCH)
