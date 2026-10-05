// spanmap.h — dep-stub for tsc/internal/spanmap, owned by the spanmap slice.
// Only the member emitter.cpp calls is declared; the real SpanMap struct and
// the rest of the package land with the spanmap port.
#pragma once

#include <utility>

#include "internal/core/text.h"

namespace tsc::spanmap {

struct SpanMap {
	// spanmap.go:330 VirtualToOriginalPositionExact — dep stub, owned by the
	// spanmap slice.
	std::pair<TextPos, bool> VirtualToOriginalPositionExact(
	    TextPos /*pos*/) {
		TSC_UNREACHABLE(
		    "spanmap::SpanMap::VirtualToOriginalPositionExact — spanmap slice");
	}
};

} // namespace tsc::spanmap
