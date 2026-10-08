// Port of tsc/internal/vfs/vfsmock/wrapper_test.go. Go walks the wrapped
// FSMock's exported fields via reflection and asserts none are nil; the
// C++ port asserts every *Func member the mock exposes.
#include <memory>
#include <string>
#include <unordered_map>

#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/vfs/vfsmock/vfsmock.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

void TestWrap(T* t) {
	t->Parallel();

	auto base = tsc::vfs::vfstest::FromMap(
	    std::unordered_map<std::string,
	                       tsc::vfs::vfstest::MapFileInput>{},
	    true);
	auto wrapper = tsc::vfs::vfsmock::Wrap(base.get());

	assert::Assert(t, wrapper->AppendFileFunc != nullptr,
	               "field AppendFileFunc should not be zero; update Wrap");
	assert::Assert(t, wrapper->ChtimesFunc != nullptr,
	               "field ChtimesFunc should not be zero; update Wrap");
	assert::Assert(t, wrapper->DirectoryExistsFunc != nullptr,
	               "field DirectoryExistsFunc should not be zero; update "
	               "Wrap");
	assert::Assert(t, wrapper->FileExistsFunc != nullptr,
	               "field FileExistsFunc should not be zero; update Wrap");
	assert::Assert(t, wrapper->GetAccessibleEntriesFunc != nullptr,
	               "field GetAccessibleEntriesFunc should not be zero; "
	               "update Wrap");
	assert::Assert(t, wrapper->ReadFileFunc != nullptr,
	               "field ReadFileFunc should not be zero; update Wrap");
	assert::Assert(t, wrapper->RealpathFunc != nullptr,
	               "field RealpathFunc should not be zero; update Wrap");
	assert::Assert(t, wrapper->RemoveFunc != nullptr,
	               "field RemoveFunc should not be zero; update Wrap");
	assert::Assert(t, wrapper->StatFunc != nullptr,
	               "field StatFunc should not be zero; update Wrap");
	assert::Assert(t, wrapper->UseCaseSensitiveFileNamesFunc != nullptr,
	               "field UseCaseSensitiveFileNamesFunc should not be "
	               "zero; update Wrap");
	assert::Assert(t, wrapper->WriteFileFunc != nullptr,
	               "field WriteFileFunc should not be zero; update Wrap");
}

} // namespace

REGISTER_UNIT_TEST("vfsmock.TestWrap", TestWrap);
