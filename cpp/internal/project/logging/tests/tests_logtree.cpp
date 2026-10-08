// Port of tsc/internal/project/logging/logtree_test.go.
#include "internal/gostd/testing.h"
#include "internal/project/logging/logging.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

// Verify LogTree implements the expected interface.
struct testLogger {
	virtual ~testLogger() = default;
	virtual void Log(const std::vector<tsc::gostd::fmtArg>& msg) = 0;
};

struct testLoggerAdapter final : testLogger {
	tsc::logging::LogTree* tree;
	explicit testLoggerAdapter(tsc::logging::LogTree* t) : tree(t) {}
	void Log(const std::vector<tsc::gostd::fmtArg>& msg) override {
		tree->Log(msg);
	}
};

void TestLogTreeImplementsLogger(tsc::gostd::testing::T* t) {
	t->Parallel();
	// Go: var _ testLogger = &LogTree{} — LogTree must satisfy the
	// Log(msg ...any) shape, i.e. it is a logging::Logger.
	tsc::logging::Logger* l =
	    static_cast<tsc::logging::LogTree*>(nullptr);
	(void)l;
	testLoggerAdapter adapter(tsc::logging::newLogTree("test"));
	adapter.Log({"hello"});
}

void TestLogTree(tsc::gostd::testing::T* t) { t->Parallel(); }

}  // namespace

REGISTER_UNIT_TEST("project/logging.TestLogTreeImplementsLogger",
                   TestLogTreeImplementsLogger);
REGISTER_UNIT_TEST("project/logging.TestLogTree", TestLogTree);
