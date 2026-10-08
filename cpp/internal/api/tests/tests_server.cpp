// Port of tsc/internal/api/server_test.go (package api).
#include <string>

#include "internal/api/server.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;

} // namespace

// TestServerRunError — server_test.go:11.
void TestServerRunError(T* t) {
	t->Parallel();

	t->Run("EOF", [](T* t) {
		t->Parallel();
		assert::NilError(
		    t, serverRunError(gostd::contextBackground(), nullptr));
	});

	t->Run("context cancellation", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] =
		    gostd::contextWithCancel(gostd::contextBackground());
		cancel();
		assert::NilError(t, serverRunError(ctx, gostd::errCanceled));
	});

	t->Run("unrelated cancellation", [](T* t) {
		t->Parallel();
		auto err = gostd::errCanceled;
		assert::ErrorIs(t, serverRunError(gostd::contextBackground(), err),
		                err);
	});

	t->Run("context cancellation supersedes server error", [](T* t) {
		t->Parallel();
		auto [ctx, cancel] =
		    gostd::contextWithCancel(gostd::contextBackground());
		cancel();
		assert::NilError(
		    t, serverRunError(ctx, gostd::newError("server failed")));
	});
}
REGISTER_UNIT_TEST("api.TestServerRunError", TestServerRunError);

} // namespace tsc::api
