// Ported fourslash tests -- batch B (callhierarchy). One static void TestX(gostd::testing::T*)
// per Go `func TestX` in tsc/internal/fourslash/tests/*_test.go; each test
// self-registers in the fourslashrunner registry. `defer done()` is modeled
// by TSC_DEFER_FN(done) and `defer testutil.RecoverAndFail` by
// tsc::testutil::withRecoverAndFail (a try/catch: a propagating C++
// exception is the recovered panic; testGoexit passes through).
#include "internal/fourslash/fourslash.h"
#include "internal/fourslash/goutil.h"
#include "internal/fourslash/test_parser.h"
#include "internal/fourslash/tests/registry.h"
#include "internal/fourslash/tests/util/util.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/ls/ls.h"
#include "internal/testutil/testutil.h"

namespace {
using namespace tsc;
namespace tsu = tsc::fourslash::tests::util;

// callHierarchyAccessor_test.go
static void TestCallHierarchyAccessor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
    new C().bar;
}

class C {
    get /**/bar() {
        return baz();
    }
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyAccessor, TestCallHierarchyAccessor);

// callHierarchyAnonymousClassNoCrash1_test.go
static void TestCallHierarchyAnonymousClassNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /main.ts
class {
    con/*1*/structor() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyAnonymousClassNoCrash1, TestCallHierarchyAnonymousClassNoCrash1);

// callHierarchyAnonymousClassNoCrash2_test.go
static void TestCallHierarchyAnonymousClassNoCrash2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /main.ts
(class {
    con/*1*/structor() {}
}))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyAnonymousClassNoCrash2, TestCallHierarchyAnonymousClassNoCrash2);

// callHierarchyAnonymousClassNoCrash3_test.go
static void TestCallHierarchyAnonymousClassNoCrash3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /main.ts
import Bar from "./other";

function foo() {
    new /*1*/Bar();
}
// @Filename: /other.ts
export default class {
    constructor() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyAnonymousClassNoCrash3, TestCallHierarchyAnonymousClassNoCrash3);

// callHierarchyAnonymousFunctionNoCrash1_test.go
static void TestCallHierarchyAnonymousFunctionNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /main.ts
func/*1*/tion() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyAnonymousFunctionNoCrash1, TestCallHierarchyAnonymousFunctionNoCrash1);

// callHierarchyAnonymousFunctionNoCrash2_test.go
static void TestCallHierarchyAnonymousFunctionNoCrash2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /main.ts
(func/*1*/tion() {}))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyAnonymousFunctionNoCrash2, TestCallHierarchyAnonymousFunctionNoCrash2);

// callHierarchyAnonymousFunctionNoCrash3_test.go
static void TestCallHierarchyAnonymousFunctionNoCrash3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /main.ts
import bar from "./other";

function foo() {
    /*1*/bar();
}
// @Filename: /other.ts
export default function() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyAnonymousFunctionNoCrash3, TestCallHierarchyAnonymousFunctionNoCrash3);

// callHierarchyCallExpressionByConstNamedFunctionExpression_test.go
static void TestCallHierarchyCallExpressionByConstNamedFunctionExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
    bar();
}

const bar = function () {
    baz();
}

function baz() {
}

/**/bar())TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyCallExpressionByConstNamedFunctionExpression, TestCallHierarchyCallExpressionByConstNamedFunctionExpression);

// callHierarchyClassPropertyArrowFunction_test.go
static void TestCallHierarchyClassPropertyArrowFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {
    caller = () => {
        this.callee();
    }

    /**/callee = () => {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyClassPropertyArrowFunction, TestCallHierarchyClassPropertyArrowFunction);

// callHierarchyClassStaticBlock2_test.go
static void TestCallHierarchyClassStaticBlock2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {
    /**/static {
        function foo() {
            bar();
        }

        function bar() {
            baz();
            quxx();
            baz();
        }

        foo();
    }
}

function baz() {
}

function quxx() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyClassStaticBlock2, TestCallHierarchyClassStaticBlock2);

// callHierarchyClassStaticBlock_test.go
static void TestCallHierarchyClassStaticBlock(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {
    static {
        function foo() {
            bar();
        }

        function /**/bar() {
            baz();
            quxx();
            baz();
        }

        foo();
    }
}

function baz() {
}

function quxx() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyClassStaticBlock, TestCallHierarchyClassStaticBlock);

// callHierarchyClass_test.go
static void TestCallHierarchyClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
    bar();
}

function /**/bar() {
    new Baz();
}

class Baz {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyClass, TestCallHierarchyClass);

// callHierarchyConstNamedArrowFunction_test.go
static void TestCallHierarchyConstNamedArrowFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
    bar();
}

const /**/bar = () => {
    baz();
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyConstNamedArrowFunction, TestCallHierarchyConstNamedArrowFunction);

// callHierarchyConstNamedClassExpression_test.go
static void TestCallHierarchyConstNamedClassExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
    new Bar();
}

const /**/Bar = class {
    constructor() {
        baz();
    }
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyConstNamedClassExpression, TestCallHierarchyConstNamedClassExpression);

// callHierarchyConstNamedFunctionExpression_test.go
static void TestCallHierarchyConstNamedFunctionExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
    bar();
}

const /**/bar = function () {
    baz();
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyConstNamedFunctionExpression, TestCallHierarchyConstNamedFunctionExpression);

// callHierarchyContainerNameServer_test.go
static void TestCallHierarchyContainerNameServer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
function /**/f() {}

class A {
  static sameName() {
    f();
  }
}

class B {
  sameName() {
    A.sameName();
  }
}

const Obj = {
  get sameName() {
    return new B().sameName;
  }
};

namespace Foo {
  function sameName() {
    return Obj.sameName;
  }

  export class C {
    constructor() {
      sameName();
    }
  }
}

namespace Foo.Bar {
  const sameName = () => new Foo.C();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyContainerNameServer, TestCallHierarchyContainerNameServer);

// callHierarchyContainerName_test.go
static void TestCallHierarchyContainerName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function /**/f() {}

class A {
  static sameName() {
    f();
  }
}

class B {
  sameName() {
    A.sameName();
  }
}

const Obj = {
  get sameName() {
    return new B().sameName;
  }
};

namespace Foo {
  function sameName() {
    return Obj.sameName;
  }

  export class C {
    constructor() {
      sameName();
    }
  }
}

namespace Foo.Bar {
  const sameName = () => new Foo.C();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyContainerName, TestCallHierarchyContainerName);

// callHierarchyCrossFile_test.go
static void TestCallHierarchyCrossFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: /a.ts
export function /**/createModelReference() {}
// @filename: /b.ts
import { createModelReference } from "./a";
function openElementsAtEditor() {
  createModelReference();
}
// @filename: /c.ts
import { createModelReference } from "./a";
function registerDefaultLanguageCommand() {
  createModelReference();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyCrossFile, TestCallHierarchyCrossFile);

// callHierarchyDecorator_test.go
static void TestCallHierarchyDecorator(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @experimentalDecorators: true
@bar
class Foo {
}

function /**/bar() {
    baz();
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyDecorator, TestCallHierarchyDecorator);

// callHierarchyExportDefaultClass_test.go
static void TestCallHierarchyExportDefaultClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: main.ts
import Bar from "./other";

function foo() {
    new Bar();
}
// @filename: other.ts
export /**/default class {
    constructor() {
        baz();
    }
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyExportDefaultClass, TestCallHierarchyExportDefaultClass);

// callHierarchyExportDefaultFunction_test.go
static void TestCallHierarchyExportDefaultFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: main.ts
import bar from "./other";

function foo() {
    bar();
}
// @filename: other.ts
export /**/default function () {
    baz();
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyExportDefaultFunction, TestCallHierarchyExportDefaultFunction);

// callHierarchyExportEqualsFunction_test.go
static void TestCallHierarchyExportEqualsFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: main.ts
import bar = require("./other");

function foo() {
    bar();
}
// @filename: other.ts
export = /**/function () {
    baz();
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyExportEqualsFunction, TestCallHierarchyExportEqualsFunction);

// callHierarchyFile_test.go
static void TestCallHierarchyFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(foo();
function /**/foo() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyFile, TestCallHierarchyFile);

// callHierarchyFunctionAmbiguity1_test.go
static void TestCallHierarchyFunctionAmbiguity1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: a.d.ts
declare function foo(x?: number): void;
// @filename: b.d.ts
declare function foo(x?: string): void;
declare function foo(x?: boolean): void;
// @filename: main.ts
function bar() {
    /**/foo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyFunctionAmbiguity1, TestCallHierarchyFunctionAmbiguity1);

// callHierarchyFunctionAmbiguity2_test.go
static void TestCallHierarchyFunctionAmbiguity2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: a.d.ts
declare function /**/foo(x?: number): void;
// @filename: b.d.ts
declare function foo(x?: string): void;
declare function foo(x?: boolean): void;
// @filename: main.ts
function bar() {
    foo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyFunctionAmbiguity2, TestCallHierarchyFunctionAmbiguity2);

// callHierarchyFunctionAmbiguity3_test.go
static void TestCallHierarchyFunctionAmbiguity3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: a.d.ts
declare function foo(x?: number): void;
// @filename: b.d.ts
declare function /**/foo(x?: string): void;
declare function foo(x?: boolean): void;
// @filename: main.ts
function bar() {
    foo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyFunctionAmbiguity3, TestCallHierarchyFunctionAmbiguity3);

// callHierarchyFunctionAmbiguity4_test.go
static void TestCallHierarchyFunctionAmbiguity4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: a.d.ts
declare function foo(x?: number): void;
// @filename: b.d.ts
declare function foo(x?: string): void;
declare function /**/foo(x?: boolean): void;
// @filename: main.ts
function bar() {
    foo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyFunctionAmbiguity4, TestCallHierarchyFunctionAmbiguity4);

// callHierarchyFunctionAmbiguity5_test.go
static void TestCallHierarchyFunctionAmbiguity5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: a.d.ts
declare function foo(x?: number): void;
// @filename: b.d.ts
declare function foo(x?: string): void;
declare function foo(x?: boolean): void;
// @filename: main.ts
function /**/bar() {
    foo();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyFunctionAmbiguity5, TestCallHierarchyFunctionAmbiguity5);

// callHierarchyFunction_test.go
static void TestCallHierarchyFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
    bar();
}

function /**/bar() {
    baz();
    quxx();
    baz();
}

function baz() {
}

function quxx() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyFunction, TestCallHierarchyFunction);

// callHierarchyInPropDeclarationOfExportedDefaultClass1_test.go
static void TestCallHierarchyInPropDeclarationOfExportedDefaultClass1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /main.ts
export default class {
  onSave = () => {
    const values = [];
    values./*m1*/push(1);
  };
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "m1");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyInPropDeclarationOfExportedDefaultClass1, TestCallHierarchyInPropDeclarationOfExportedDefaultClass1);

// callHierarchyIncomingCallsNoCrashArrayPush_test.go
static void TestCallHierarchyIncomingCallsNoCrashArrayPush(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function splitNames(name: string) {
  return (name || "").split(",").filter(Boolean);
}

async function trim(packageNames: string[]) {
  const nameOrPkgs = packageNames.filter(Boolean);
  const names = [];
  for (const nameOrPkg of nameOrPkgs) {
    try {
      names./*push*/push(nameOrPkg);
    } catch (error) {
    }
  }
  return names;
}
	)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "push");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyIncomingCallsNoCrashArrayPush, TestCallHierarchyIncomingCallsNoCrashArrayPush);

// callHierarchyIncomingCallsObjectLiteralMethodInExpressionComputedProperty_test.go
static void TestCallHierarchyIncomingCallsObjectLiteralMethodInExpressionComputedProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const obj = {
  [1 + 2]: {
    method() {
      return ""./*split*/split(",");
    }
  }
};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "split");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyIncomingCallsObjectLiteralMethodInExpressionComputedProperty, TestCallHierarchyIncomingCallsObjectLiteralMethodInExpressionComputedProperty);

// callHierarchyIncomingCallsObjectLiteralMethodInIdentifierComputedProperty_test.go
static void TestCallHierarchyIncomingCallsObjectLiteralMethodInIdentifierComputedProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const key = "x";
const obj = {
  [key]: {
    method() {
      return ""./*split*/split(",");
    }
  }
};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "split");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyIncomingCallsObjectLiteralMethodInIdentifierComputedProperty, TestCallHierarchyIncomingCallsObjectLiteralMethodInIdentifierComputedProperty);

// callHierarchyIncomingCallsObjectLiteralMethodInStringLiteralComputedProperty_test.go
static void TestCallHierarchyIncomingCallsObjectLiteralMethodInStringLiteralComputedProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const obj = {
  ["x"]: {
    method() {
      return ""./*split*/split(",");
    }
  }
};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "split");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyIncomingCallsObjectLiteralMethodInStringLiteralComputedProperty, TestCallHierarchyIncomingCallsObjectLiteralMethodInStringLiteralComputedProperty);

// callHierarchyInterfaceMethod_test.go
static void TestCallHierarchyInterfaceMethod(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface I {
    /**/foo(): void;
}

const obj: I = { foo() {} };

obj.foo();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyInterfaceMethod, TestCallHierarchyInterfaceMethod);

// callHierarchyJsxElement_test.go
static void TestCallHierarchyJsxElement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @jsx: preserve
// @filename: main.tsx
function foo() {
    return <Bar/>;
}

function /**/Bar() {
    baz();
}

function baz() {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyJsxElement, TestCallHierarchyJsxElement);

// callHierarchyTaggedTemplate_test.go
static void TestCallHierarchyTaggedTemplate(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
    bar)TS" +std::string("`") +std::string(R"TS(a${1}b)TS") +std::string("`") +std::string(R"TS(;
}

function /**/bar(array: TemplateStringsArray, ...args: any[]) {
    baz();
}

function baz() {
})TS");
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyTaggedTemplate, TestCallHierarchyTaggedTemplate);

// callHierarchyUnclosedTemplateExprNoCrash1_test.go
static void TestCallHierarchyUnclosedTemplateExprNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Regression test for a crash in prepareCallHierarchy caused by parser error;
		// recovery: when a template expression is truncated mid-call (e.g. `${format`;
		// without closing `)`), the parser misinterprets the `class` keyword in;
		// subsequent HTML template literals as a TypeScript class declaration.;
		// The resulting anonymous ClassDeclaration (no name, no `default` modifier);
		// previously caused a "Expected call hierarchy declaration to have a reference;
		// node" assertion failure.;
		const std::string content = "// @Filename: /main.ts\n" +std::string("function updateBadge() {\n") +std::string("    const header = `<div class=\"sub\">${format`;\n") +std::string("    const badge = `<div /*1*/class=\"badge\">`;\n") +std::string("}");
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyBaselineCallHierarchy(t);
	});
}
REGISTER_FOURSLASH_TEST(TestCallHierarchyUnclosedTemplateExprNoCrash1, TestCallHierarchyUnclosedTemplateExprNoCrash1);


}  // namespace
