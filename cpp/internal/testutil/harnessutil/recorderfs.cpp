// recorderfs.go — OutputRecorderFS: a vfs.FS wrapper that records every file
// written through it as a TestFile (the emitted-output store for baselines).
// The struct itself is declared in recorderfs.h so harnessutil.cpp can do the
// Go `fs.(*OutputRecorderFS)` type assertion.
#include "internal/testutil/harnessutil/recorderfs.h"

namespace tsc::testutil::harnessutil {

// NewOutputRecorderFS — recorderfs.go:18.
std::shared_ptr<vfs::FS>
NewOutputRecorderFS(const std::shared_ptr<vfs::FS>& fs) {
	return std::make_shared<OutputRecorderFS>(fs);
}

}  // namespace tsc::testutil::harnessutil
