// In-process runner for debugging a single registered unit test under a
// debugger. Runs the test on a 64MB-stack thread like unittestrunner (Go
// goroutine stacks grow; deep parses need more than 8MB).
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <pthread.h>
#endif

namespace {
struct Ctx {
	const tsc::testutil::unittests::UnitTestCase* tc;
	tsc::gostd::testing::T* t;
};

void* runTestImpl(void* arg) {
	auto* c = static_cast<Ctx*>(arg);
	c->t->Run(c->tc->name, c->tc->fn);
	return nullptr;
}

#ifdef _WIN32
DWORD WINAPI runTestImplW32(LPVOID arg) {
	runTestImpl(arg);
	return 0;
}
#endif
}  // namespace

int main(int argc, char** argv) {
	if (argc < 2) { fprintf(stderr, "usage: dbg <testname>\n"); return 2; }
	for (auto& tc : tsc::testutil::unittests::unitTestRegistry()) {
		if (std::strstr(tc.name, argv[1])) {
			tsc::gostd::testing::T t;
			Ctx ctx{&tc, &t};
#ifdef _WIN32
			HANDLE h = CreateThread(nullptr, SIZE_T{64} << 20,
			                        runTestImplW32, &ctx, 0, nullptr);
			if (h == nullptr) { runTestImpl(&ctx); }
			else { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
#else
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, size_t{64} << 20);
			pthread_t th;
			int rc = pthread_create(&th, &attr, runTestImpl, &ctx);
			pthread_attr_destroy(&attr);
			if (rc != 0) { runTestImpl(&ctx); }
			else { pthread_join(th, nullptr); }
#endif
			printf("%s failed=%d skipped=%d\n", tc.name, (int)t.Failed(), (int)t.Skipped());
			return t.Failed() ? 1 : 0;
		}
	}
	fprintf(stderr, "no match\n");
	return 2;
}
