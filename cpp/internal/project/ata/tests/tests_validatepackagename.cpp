// Port of tsc/internal/project/ata/validatepackagename_test.go.
#include <string>

#include "internal/gostd/testing.h"
#include "internal/project/ata/ata.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

void TestValidatePackageName(tsc::gostd::testing::T* t) {
	namespace assert = tsc::gotest::assert;
	using tsc::ata::NameContainsNonURISafeCharacters;
	using tsc::ata::NameOk;
	using tsc::ata::NameStartsWithDot;
	using tsc::ata::NameStartsWithUnderscore;
	using tsc::ata::NameTooLong;
	using tsc::ata::ValidatePackageName;

	t->Parallel();
	t->Run("name cannot be too long", [](tsc::gostd::testing::T* t) {
		t->Parallel();
		std::string packageName = "a";
		for (int i = 0; i < 8; i++) {
			packageName += packageName;
		}
		auto r = ValidatePackageName(packageName);
		assert::Equal(t, r.result, NameTooLong);
	});
	t->Run("package name cannot start with dot",
	       [](tsc::gostd::testing::T* t) {
		       t->Parallel();
		       auto r = ValidatePackageName(".foo");
		       assert::Equal(t, r.result, NameStartsWithDot);
	       });
	t->Run("package name cannot start with underscore",
	       [](tsc::gostd::testing::T* t) {
		       t->Parallel();
		       auto r = ValidatePackageName("_foo");
		       assert::Equal(t, r.result, NameStartsWithUnderscore);
	       });
	t->Run("package non URI safe characters are not supported",
	       [](tsc::gostd::testing::T* t) {
		       t->Parallel();
		       auto r = ValidatePackageName("  scope  ");
		       assert::Equal(t, r.result, NameContainsNonURISafeCharacters);
		       r = ValidatePackageName("; say ‘Hello from TypeScript!’ #");
		       assert::Equal(t, r.result, NameContainsNonURISafeCharacters);
		       r = ValidatePackageName("a/b/c");
		       assert::Equal(t, r.result, NameContainsNonURISafeCharacters);
	       });
	t->Run("scoped package name is supported",
	       [](tsc::gostd::testing::T* t) {
		       t->Parallel();
		       auto r = ValidatePackageName("@scope/bar");
		       assert::Equal(t, r.result, NameOk);
	       });
	t->Run("scoped name in scoped package name cannot start with dot",
	       [](tsc::gostd::testing::T* t) {
		       t->Parallel();
		       auto r = ValidatePackageName("@.scope/bar");
		       assert::Equal(t, r.result, NameStartsWithDot);
		       assert::Equal(t, r.name, std::string(".scope"));
		       assert::Equal(t, r.isScopeName, true);
		       r = ValidatePackageName("@.scope/.bar");
		       assert::Equal(t, r.result, NameStartsWithDot);
		       assert::Equal(t, r.name, std::string(".scope"));
		       assert::Equal(t, r.isScopeName, true);
	       });
	t->Run("scoped name in scoped package name cannot start with dot",
	       [](tsc::gostd::testing::T* t) {
		       t->Parallel();
		       auto r = ValidatePackageName("@_scope/bar");
		       assert::Equal(t, r.result, NameStartsWithUnderscore);
		       assert::Equal(t, r.name, std::string("_scope"));
		       assert::Equal(t, r.isScopeName, true);
		       r = ValidatePackageName("@_scope/_bar");
		       assert::Equal(t, r.result, NameStartsWithUnderscore);
		       assert::Equal(t, r.name, std::string("_scope"));
		       assert::Equal(t, r.isScopeName, true);
	       });
	t->Run(
	    "scope name in scoped package name with non URI safe characters are "
	    "not supported",
	    [](tsc::gostd::testing::T* t) {
		    t->Parallel();
		    auto r = ValidatePackageName("@  scope  /bar");
		    assert::Equal(t, r.result, NameContainsNonURISafeCharacters);
		    assert::Equal(t, r.name, std::string("  scope  "));
		    assert::Equal(t, r.isScopeName, true);
		    r = ValidatePackageName(
		        "@; say ‘Hello from TypeScript!’ #/bar");
		    assert::Equal(t, r.result, NameContainsNonURISafeCharacters);
		    assert::Equal(t, r.name,
		                  std::string("; say ‘Hello from TypeScript!’ #"));
		    assert::Equal(t, r.isScopeName, true);
		    r = ValidatePackageName("@  scope  /  bar  ");
		    assert::Equal(t, r.result, NameContainsNonURISafeCharacters);
		    assert::Equal(t, r.name, std::string("  scope  "));
		    assert::Equal(t, r.isScopeName, true);
	    });
	t->Run("package name in scoped package name cannot start with dot",
	       [](tsc::gostd::testing::T* t) {
		       t->Parallel();
		       auto r = ValidatePackageName("@scope/.bar");
		       assert::Equal(t, r.result, NameStartsWithDot);
		       assert::Equal(t, r.name, std::string(".bar"));
		       assert::Equal(t, r.isScopeName, false);
	       });
	t->Run("package name in scoped package name cannot start with underscore",
	       [](tsc::gostd::testing::T* t) {
		       t->Parallel();
		       auto r = ValidatePackageName("@scope/_bar");
		       assert::Equal(t, r.result, NameStartsWithUnderscore);
		       assert::Equal(t, r.name, std::string("_bar"));
		       assert::Equal(t, r.isScopeName, false);
	       });
	t->Run(
	    "package name in scoped package name with non URI safe characters "
	    "are not supported",
	    [](tsc::gostd::testing::T* t) {
		    t->Parallel();
		    auto r = ValidatePackageName("@scope/  bar  ");
		    assert::Equal(t, r.result, NameContainsNonURISafeCharacters);
		    assert::Equal(t, r.name, std::string("  bar  "));
		    assert::Equal(t, r.isScopeName, false);
		    r = ValidatePackageName(
		        "@scope/; say ‘Hello from TypeScript!’ #");
		    assert::Equal(t, r.result, NameContainsNonURISafeCharacters);
		    assert::Equal(t, r.name,
		                  std::string("; say ‘Hello from TypeScript!’ #"));
		    assert::Equal(t, r.isScopeName, false);
	    });
}

}  // namespace

REGISTER_UNIT_TEST("project/ata.TestValidatePackageName",
                   TestValidatePackageName);
