// race.h — port of tsc/internal/testutil/race. Go ships a build-tag pair:
//   norace.go: //go:build !race → const Enabled = false
//   race.go:   //go:build race  → const Enabled = true
// C++ has no race-detector build tag, so this port always carries the
// non-race implementation (Enabled = false), matching a standard `go test`
// run.
#pragma once

namespace tsc::testutil::race {

// Enabled reports if the race detector is enabled (norace.go:5).
inline constexpr bool Enabled = false;

}  // namespace tsc::testutil::race
