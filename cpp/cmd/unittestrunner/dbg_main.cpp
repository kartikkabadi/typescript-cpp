// In-process runner for debugging a single registered unit test under gdb.
// Runs the test on a 64MB-stack thread like unittestrunner (Go goroutine
// stacks grow; deep parses need more than 8MB).
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"
#include <cstdio>
#include <cstring>
#include <pthread.h>

int main(int argc, char** argv) {
	if (argc < 2) { fprintf(stderr, "usage: dbg <testname>\n"); return 2; }
	for (auto& tc : tsc::testutil::unittests::unitTestRegistry()) {
		if (std::strstr(tc.name, argv[1])) {
			struct Ctx { const tsc::testutil::unittests::UnitTestCase* tc; tsc::gostd::testing::T* t; };
			tsc::gostd::testing::T t;
			Ctx ctx{&tc, &t};
			auto run = [](void* p) -> void* {
				auto* c = static_cast<Ctx*>(p);
				c->t->Run(c->tc->name, c->tc->fn);
				return nullptr;
			};
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setstacksize(&attr, size_t{64} << 20);
			pthread_t th;
			int rc = pthread_create(&th, &attr, run, &ctx);
			pthread_attr_destroy(&attr);
			if (rc != 0) { run(&ctx); } else { pthread_join(th, nullptr); }
			printf("%s failed=%d skipped=%d\n", tc.name, (int)t.Failed(), (int)t.Skipped());
			return t.Failed() ? 1 : 0;
		}
	}
	fprintf(stderr, "no match\n");
	return 2;
}
