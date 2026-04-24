#pragma once

// Polyfill header for engine APIs that differ between supported UE
// versions.
//
// Convention:
//   - Wrap divergent calls in inline helpers named Bpeek_* living in
//     the BPeekCompat namespace.
//   - Re-export relocated headers as conditional includes below so
//     callers can #include "BPeekCompat.h" once and stay version-agnostic.
//   - Gate branches with the BPEEK_UE_5_X_OR_LATER macros that come
//     from BPeekBuild.Build.cs (inherited by every BPeek module).

#include "CoreMinimal.h"

// UUserDefinedStruct moved from Engine/ to StructUtils/ in UE 5.5.
// Engine/UserDefinedStruct.h still exists as a shim in 5.5+, but its
// body is #if-guarded on UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5 —
// projects that opt out of deprecated include order (Lyra 5.7 does)
// get an empty header and the type becomes undefined. Always go
// through the real path on 5.5+.
#if BPEEK_UE_5_5_OR_LATER
    #include "StructUtils/UserDefinedStruct.h"
#else
    #include "Engine/UserDefinedStruct.h"
#endif
