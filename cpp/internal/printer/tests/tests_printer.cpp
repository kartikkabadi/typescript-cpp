// Port of tsc/internal/printer/printer_test.go.
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/visitor.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/testutil/emittestutil/emittestutil.h"
#include "internal/testutil/parsetestutil/parsetestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/transformers/tstransforms/tstransforms.h"
#include "internal/transformers/transformers.h"

using tsc::gostd::testing::T;
using namespace tsc;

namespace parsetestutil = tsc::testutil::parsetestutil;
namespace emittestutil = tsc::testutil::emittestutil;

namespace {

// stringsTrimSuffix — strings.TrimSuffix.
std::string stringsTrimSuffix(std::string s, std::string_view suffix) {
	if (s.size() >= suffix.size() &&
	    s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
		s.resize(s.size() - suffix.size());
	}
	return s;
}

// isBinaryOperator — printer_test.go:1297 (file-local helper).
bool isBinaryOperator(Kind token) {
	switch (token) {
	case Kind::CommaToken:
	case Kind::LessThanToken:
	case Kind::GreaterThanToken:
	case Kind::LessThanEqualsToken:
	case Kind::GreaterThanEqualsToken:
	case Kind::EqualsEqualsToken:
	case Kind::EqualsEqualsEqualsToken:
	case Kind::ExclamationEqualsToken:
	case Kind::ExclamationEqualsEqualsToken:
	case Kind::PlusToken:
	case Kind::MinusToken:
	case Kind::AsteriskToken:
	case Kind::AsteriskAsteriskToken:
	case Kind::SlashToken:
	case Kind::PercentToken:
	case Kind::LessThanLessThanToken:
	case Kind::GreaterThanGreaterThanToken:
	case Kind::GreaterThanGreaterThanGreaterThanToken:
	case Kind::AmpersandToken:
	case Kind::BarToken:
	case Kind::CaretToken:
	case Kind::AmpersandAmpersandToken:
	case Kind::BarBarToken:
	case Kind::QuestionQuestionToken:
	case Kind::EqualsToken:
	case Kind::PlusEqualsToken:
	case Kind::MinusEqualsToken:
	case Kind::AsteriskEqualsToken:
	case Kind::AsteriskAsteriskEqualsToken:
	case Kind::SlashEqualsToken:
	case Kind::PercentEqualsToken:
	case Kind::LessThanLessThanEqualsToken:
	case Kind::GreaterThanGreaterThanEqualsToken:
	case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
	case Kind::AmpersandEqualsToken:
	case Kind::BarEqualsToken:
	case Kind::BarBarEqualsToken:
	case Kind::AmpersandAmpersandEqualsToken:
	case Kind::QuestionQuestionEqualsToken:
	case Kind::CaretEqualsToken:
	case Kind::InKeyword:
	case Kind::InstanceOfKeyword:
		return true;
	}
	return false;
}

// makeSide — printer_test.go:1346.
Node* makeSide(const std::string& label, Kind kind, tsc::NodeFactory* f) {
	if (kind == Kind::Identifier || kind == Kind::Unknown) {
		return f->newIdentifier(label);
	}
	if (kind == Kind::ArrowFunction) {
		return f->newArrowFunction(nullptr, /*modifiers*/
		                           nullptr, /*typeParameters*/
		                           f->newNodeList({}),
		                           nullptr, /*returnType*/
		                           nullptr, /*fullSignature*/
		                           f->newToken(Kind::EqualsGreaterThanToken),
		                           f->newBlock(f->newNodeList({}),
		                                       false /*multiLine*/));
	}
	if (isBinaryOperator(kind)) {
		return f->newBinaryExpression(
		    nullptr, /*modifiers*/
		    f->newIdentifier(label + "l"),
		    nullptr, /*typeNode*/
		    f->newToken(kind),
		    f->newIdentifier(label + "r"));
	}
	TSC_UNREACHABLE("unsupported kind");
}

void TestEmit(T* t) {
	t->Parallel();
	struct Row {
		const char* title;
		const char* input;
		const char* output;
		bool jsx;
	};
	static const Row data[] = {
		{"StringLiteral#1", R"TS(;"test")TS", ";\n\"test\";", false},
		{"StringLiteral#2", R"TS(;'test')TS", ";\n'test';", false},
		{"NumericLiteral#1", R"TS(0)TS", R"TS(0;)TS", false},
		{"NumericLiteral#2", R"TS(10_000)TS", R"TS(10000;)TS", false},
		{"BigIntLiteral#1", R"TS(0n)TS", R"TS(0n;)TS", false},
		{"BigIntLiteral#2", R"TS(10_000n)TS", R"TS(10000n;)TS", false}, // TODO: Preserve numeric literal separators after Strada migration
		{"BooleanLiteral#1", R"TS(true)TS", R"TS(true;)TS", false},
		{"BooleanLiteral#2", R"TS(false)TS", R"TS(false;)TS", false},
		{"NoSubstitutionTemplateLiteral", "``", "``;", false},
		{"NoSubstitutionTemplateLiteral#2", "`\n`", "`\n`;", false},
		{"RegularExpressionLiteral#1", R"TS(/a/)TS", R"TS(/a/;)TS", false},
		{"RegularExpressionLiteral#2", R"TS(/a/g)TS", R"TS(/a/g;)TS", false},
		{"NullLiteral", R"TS(null)TS", R"TS(null;)TS", false},
		{"ThisExpression", R"TS(this)TS", R"TS(this;)TS", false},
		{"SuperExpression", R"TS(super())TS", R"TS(super();)TS", false},
		{"ImportExpression", R"TS(import())TS", R"TS(import();)TS", false},
		{"PropertyAccess#1", R"TS(a.b)TS", R"TS(a.b;)TS", false},
		{"PropertyAccess#2", R"TS(a.#b)TS", R"TS(a.#b;)TS", false},
		{"PropertyAccess#3", R"TS(a?.b)TS", R"TS(a?.b;)TS", false},
		{"PropertyAccess#4", R"TS(a?.b.c)TS", R"TS(a?.b.c;)TS", false},
		{"PropertyAccess#5", R"TS(1..b)TS", R"TS(1..b;)TS", false},
		{"PropertyAccess#6", R"TS(1.0.b)TS", R"TS(1.0.b;)TS", false},
		{"PropertyAccess#7", R"TS(0x1.b)TS", R"TS(0x1.b;)TS", false},
		{"PropertyAccess#8", R"TS(0b1.b)TS", R"TS(0b1.b;)TS", false},
		{"PropertyAccess#9", R"TS(0o1.b)TS", R"TS(0o1.b;)TS", false},
		{"PropertyAccess#10", R"TS(10e1.b)TS", R"TS(10e1.b;)TS", false},
		{"PropertyAccess#11", R"TS(10E1.b)TS", R"TS(10E1.b;)TS", false},
		{"PropertyAccess#12", R"TS(a.b?.c)TS", R"TS(a.b?.c;)TS", false},
		{"PropertyAccess#13", "a\n.b", "a\n    .b;", false},
		{"PropertyAccess#14", "a.\nb", "a.\n    b;", false},
		{"ElementAccess#1", R"TS(a[b])TS", R"TS(a[b];)TS", false},
		{"ElementAccess#2", R"TS(a?.[b])TS", R"TS(a?.[b];)TS", false},
		{"ElementAccess#3", R"TS(a?.[b].c)TS", R"TS(a?.[b].c;)TS", false},
		{"CallExpression#1", R"TS(a())TS", R"TS(a();)TS", false},
		{"CallExpression#2", R"TS(a<T>())TS", R"TS(a<T>();)TS", false},
		{"CallExpression#3", R"TS(a(b))TS", R"TS(a(b);)TS", false},
		{"CallExpression#4", R"TS(a<T>(b))TS", R"TS(a<T>(b);)TS", false},
		{"CallExpression#5", R"TS(a(b).c)TS", R"TS(a(b).c;)TS", false},
		{"CallExpression#6", R"TS(a<T>(b).c)TS", R"TS(a<T>(b).c;)TS", false},
		{"CallExpression#7", R"TS(a?.(b))TS", R"TS(a?.(b);)TS", false},
		{"CallExpression#8", R"TS(a?.<T>(b))TS", R"TS(a?.<T>(b);)TS", false},
		{"CallExpression#9", R"TS(a?.(b).c)TS", R"TS(a?.(b).c;)TS", false},
		{"CallExpression#10", R"TS(a?.<T>(b).c)TS", R"TS(a?.<T>(b).c;)TS", false},
		{"CallExpression#11", R"TS(a<T, U>())TS", R"TS(a<T, U>();)TS", false},
		// {title: "CallExpression#12", input: `a<T,>()`, output: `a<T,>();`}, // TODO: preserve trailing comma after Strada migration
		{"CallExpression#13", R"TS(a?.b())TS", R"TS(a?.b();)TS", false},
		{"NewExpression#1", R"TS(new a)TS", R"TS(new a;)TS", false},
		{"NewExpression#2", R"TS(new a.b)TS", R"TS(new a.b;)TS", false},
		{"NewExpression#3", R"TS(new a())TS", R"TS(new a();)TS", false},
		{"NewExpression#4", R"TS(new a.b())TS", R"TS(new a.b();)TS", false},
		{"NewExpression#5", R"TS(new a<T>())TS", R"TS(new a<T>();)TS", false},
		{"NewExpression#6", R"TS(new a.b<T>())TS", R"TS(new a.b<T>();)TS", false},
		{"NewExpression#7", R"TS(new a(b))TS", R"TS(new a(b);)TS", false},
		{"NewExpression#8", R"TS(new a.b(c))TS", R"TS(new a.b(c);)TS", false},
		{"NewExpression#9", R"TS(new a<T>(b))TS", R"TS(new a<T>(b);)TS", false},
		{"NewExpression#10", R"TS(new a.b<T>(c))TS", R"TS(new a.b<T>(c);)TS", false},
		{"NewExpression#11", R"TS(new a(b).c)TS", R"TS(new a(b).c;)TS", false},
		{"NewExpression#12", R"TS(new a<T>(b).c)TS", R"TS(new a<T>(b).c;)TS", false},
		{"TaggedTemplateExpression#1", "tag``", "tag ``;", false},
		{"TaggedTemplateExpression#2", "tag<T>``", "tag<T> ``;", false},
		{"TypeAssertionExpression#1", R"TS(<T>a)TS", R"TS(<T>a;)TS", false},
		{"FunctionExpression#1", R"TS((function(){}))TS", R"TS((function () { });)TS", false},
		{"FunctionExpression#2", R"TS((function f(){}))TS", R"TS((function f() { });)TS", false},
		{"FunctionExpression#3", R"TS((function*f(){}))TS", R"TS((function* f() { });)TS", false},
		{"FunctionExpression#4", R"TS((async function f(){}))TS", R"TS((async function f() { });)TS", false},
		{"FunctionExpression#5", R"TS((async function*f(){}))TS", R"TS((async function* f() { });)TS", false},
		{"FunctionExpression#6", R"TS((function<T>(){}))TS", R"TS((function <T>() { });)TS", false},
		{"FunctionExpression#7", R"TS((function(a){}))TS", R"TS((function (a) { });)TS", false},
		{"FunctionExpression#8", R"TS((function():T{}))TS", R"TS((function (): T { });)TS", false},
		{"ArrowFunction#1", R"TS(a=>{})TS", R"TS(a => { };)TS", false},
		{"ArrowFunction#2", R"TS(()=>{})TS", R"TS(() => { };)TS", false},
		{"ArrowFunction#3", R"TS((a)=>{})TS", R"TS((a) => { };)TS", false},
		{"ArrowFunction#4", R"TS(<T>(a)=>{})TS", R"TS(<T>(a) => { };)TS", false},
		{"ArrowFunction#5", R"TS(async a=>{})TS", R"TS(async (a) => { };)TS", false},
		{"ArrowFunction#6", R"TS(async()=>{})TS", R"TS(async () => { };)TS", false},
		{"ArrowFunction#7", R"TS(async<T>()=>{})TS", R"TS(async <T>() => { };)TS", false},
		{"ArrowFunction#8", R"TS(():T=>{})TS", R"TS((): T => { };)TS", false},
		{"ArrowFunction#9", R"TS(()=>a)TS", R"TS(() => a;)TS", false},
		{"DeleteExpression", R"TS(delete a)TS", R"TS(delete a;)TS", false},
		{"TypeOfExpression", R"TS(typeof a)TS", R"TS(typeof a;)TS", false},
		{"VoidExpression", R"TS(void a)TS", R"TS(void a;)TS", false},
		{"AwaitExpression", R"TS(await a)TS", R"TS(await a;)TS", false},
		{"PrefixUnaryExpression#1", R"TS(+a)TS", R"TS(+a;)TS", false},
		{"PrefixUnaryExpression#2", R"TS(++a)TS", R"TS(++a;)TS", false},
		{"PrefixUnaryExpression#3", R"TS(+ +a)TS", R"TS(+ +a;)TS", false},
		{"PrefixUnaryExpression#4", R"TS(+ ++a)TS", R"TS(+ ++a;)TS", false},
		{"PrefixUnaryExpression#5", R"TS(-a)TS", R"TS(-a;)TS", false},
		{"PrefixUnaryExpression#6", R"TS(--a)TS", R"TS(--a;)TS", false},
		{"PrefixUnaryExpression#7", R"TS(- -a)TS", R"TS(- -a;)TS", false},
		{"PrefixUnaryExpression#8", R"TS(- --a)TS", R"TS(- --a;)TS", false},
		{"PrefixUnaryExpression#9", R"TS(+-a)TS", R"TS(+-a;)TS", false},
		{"PrefixUnaryExpression#10", R"TS(+--a)TS", R"TS(+--a;)TS", false},
		{"PrefixUnaryExpression#11", R"TS(-+a)TS", R"TS(-+a;)TS", false},
		{"PrefixUnaryExpression#12", R"TS(-++a)TS", R"TS(-++a;)TS", false},
		{"PrefixUnaryExpression#13", R"TS(~a)TS", R"TS(~a;)TS", false},
		{"PrefixUnaryExpression#14", R"TS(!a)TS", R"TS(!a;)TS", false},
		{"PostfixUnaryExpression#1", R"TS(a++)TS", R"TS(a++;)TS", false},
		{"PostfixUnaryExpression#2", R"TS(a--)TS", R"TS(a--;)TS", false},
		{"BinaryExpression#1", R"TS(a,b)TS", R"TS(a, b;)TS", false},
		{"BinaryExpression#2", R"TS(a+b)TS", R"TS(a + b;)TS", false},
		{"BinaryExpression#3", R"TS(a**b)TS", R"TS(a ** b;)TS", false},
		{"BinaryExpression#4", R"TS(a instanceof b)TS", R"TS(a instanceof b;)TS", false},
		{"BinaryExpression#5", R"TS(a in b)TS", R"TS(a in b;)TS", false},
		{"BinaryExpression#6", "a\n&& b", "a\n    && b;", false},
		{"BinaryExpression#7", "a &&\nb", "a &&\n    b;", false},
		{"ConditionalExpression#1", R"TS(a?b:c)TS", R"TS(a ? b : c;)TS", false},
		{"ConditionalExpression#2", "a\n?b:c", "a\n    ? b : c;", false},
		{"ConditionalExpression#3", "a?\nb:c", "a ?\n    b : c;", false},
		{"ConditionalExpression#4", "a?b\n:c", "a ? b\n    : c;", false},
		{"ConditionalExpression#5", "a?b:\nc", "a ? b :\n    c;", false},
		{"TemplateExpression#1", "`a${b}c`", "`a${b}c`;", false},
		{"TemplateExpression#2", "`a${b}c${d}e`", "`a${b}c${d}e`;", false},
		{"YieldExpression#1", R"TS((function*() { yield }))TS", R"TS((function* () { yield; });)TS", false},
		{"YieldExpression#2", R"TS((function*() { yield a }))TS", R"TS((function* () { yield a; });)TS", false},
		{"YieldExpression#3", R"TS((function*() { yield*a }))TS", R"TS((function* () { yield* a; });)TS", false},
		{"SpreadElement", R"TS([...a])TS", R"TS([...a];)TS", false},
		{"ClassExpression#1", R"TS((class {}))TS", "(class {\n});", false},
		{"ClassExpression#2", R"TS((class a {}))TS", "(class a {\n});", false},
		{"ClassExpression#3", R"TS((class<T>{}))TS", "(class<T> {\n});", false},
		{"ClassExpression#4", R"TS((class a<T>{}))TS", "(class a<T> {\n});", false},
		{"ClassExpression#5", R"TS((class extends b {}))TS", "(class extends b {\n});", false},
		{"ClassExpression#6", R"TS((class a extends b {}))TS", "(class a extends b {\n});", false},
		{"ClassExpression#7", R"TS((class implements b {}))TS", "(class implements b {\n});", false},
		{"ClassExpression#8", R"TS((class a implements b {}))TS", "(class a implements b {\n});", false},
		{"ClassExpression#9", R"TS((class implements b, c {}))TS", "(class implements b, c {\n});", false},
		{"ClassExpression#10", R"TS((class a implements b, c {}))TS", "(class a implements b, c {\n});", false},
		{"ClassExpression#11", R"TS((class extends b implements c, d {}))TS", "(class extends b implements c, d {\n});", false},
		{"ClassExpression#12", R"TS((class a extends b implements c, d {}))TS", "(class a extends b implements c, d {\n});", false},
		{"ClassExpression#13", R"TS((@a class {}))TS", "(\n@a\nclass {\n});", false},
		{"OmittedExpression", R"TS([,])TS", R"TS([,];)TS", false},
		{"ExpressionWithTypeArguments", R"TS(a<T>)TS", R"TS(a<T>;)TS", false},
		{"AsExpression", R"TS(a as T)TS", R"TS(a as T;)TS", false},
		{"SatisfiesExpression", R"TS(a satisfies T)TS", R"TS(a satisfies T;)TS", false},
		{"NonNullExpression", R"TS(a!)TS", R"TS(a!;)TS", false},
		{"MetaProperty#1", R"TS(new.target)TS", R"TS(new.target;)TS", false},
		{"MetaProperty#2", R"TS(import.meta)TS", R"TS(import.meta;)TS", false},
		{"ArrayLiteralExpression#1", R"TS([])TS", R"TS([];)TS", false},
		{"ArrayLiteralExpression#2", R"TS([a])TS", R"TS([a];)TS", false},
		{"ArrayLiteralExpression#3", R"TS([a,])TS", R"TS([a,];)TS", false},
		{"ArrayLiteralExpression#4", R"TS([,a])TS", R"TS([, a];)TS", false},
		{"ArrayLiteralExpression#5", R"TS([...a])TS", R"TS([...a];)TS", false},
		{"ArrayLiteralExpression#6", R"TS(const array = [/* comment */];)TS", R"TS(const array = [ /* comment */];)TS", false},
		{"ObjectLiteralExpression#1", R"TS(({}))TS", R"TS(({});)TS", false},
		{"ObjectLiteralExpression#2", R"TS(({a,}))TS", R"TS(({ a, });)TS", false},
		{"ShorthandPropertyAssignment", R"TS(({a}))TS", R"TS(({ a });)TS", false},
		{"PropertyAssignment", R"TS(({a:b}))TS", R"TS(({ a: b });)TS", false},
		{"SpreadAssignment", R"TS(({...a}))TS", R"TS(({ ...a });)TS", false},
		{"Block", R"TS({})TS", R"TS({ })TS", false},
		{"VariableStatement#1", R"TS(var a)TS", R"TS(var a;)TS", false},
		{"VariableStatement#2", R"TS(let a)TS", R"TS(let a;)TS", false},
		{"VariableStatement#3", R"TS(const a = b)TS", R"TS(const a = b;)TS", false},
		{"VariableStatement#4", R"TS(using a = b)TS", R"TS(using a = b;)TS", false},
		{"VariableStatement#5", R"TS(await using a = b)TS", R"TS(await using a = b;)TS", false},
		{"EmptyStatement", R"TS(;)TS", R"TS(;)TS", false},
		{"IfStatement#1", R"TS(if(a);)TS", "if (a)\n    ;", false},
		{"IfStatement#2", R"TS(if(a);else;)TS", "if (a)\n    ;\nelse\n    ;", false},
		{"IfStatement#3", R"TS(if(a);else{})TS", "if (a)\n    ;\nelse { }", false},
		{"IfStatement#4", R"TS(if(a);else if(b);)TS", "if (a)\n    ;\nelse if (b)\n    ;", false},
		{"IfStatement#5", R"TS(if(a);else if(b) {})TS", "if (a)\n    ;\nelse if (b) { }", false},
		{"IfStatement#6", R"TS(if(a) {})TS", "if (a) { }", false},
		{"IfStatement#7", R"TS(if(a) {} else;)TS", "if (a) { }\nelse\n    ;", false},
		{"IfStatement#8", R"TS(if(a) {} else {})TS", "if (a) { }\nelse { }", false},
		{"IfStatement#9", R"TS(if(a) {} else if(b);)TS", "if (a) { }\nelse if (b)\n    ;", false},
		{"IfStatement#10", R"TS(if(a) {} else if(b){})TS", "if (a) { }\nelse if (b) { }", false},
		{"DoStatement#1", R"TS(do;while(a);)TS", "do\n    ;\nwhile (a);", false},
		{"DoStatement#2", R"TS(do {} while(a);)TS", "do { } while (a);", false},
		{"WhileStatement#1", R"TS(while(a);)TS", "while (a)\n    ;", false},
		{"WhileStatement#2", R"TS(while(a) {})TS", "while (a) { }", false},
		{"ForStatement#1", R"TS(for(;;);)TS", "for (;;)\n    ;", false},
		{"ForStatement#2", R"TS(for(a;;);)TS", "for (a;;)\n    ;", false},
		{"ForStatement#3", R"TS(for(var a;;);)TS", "for (var a;;)\n    ;", false},
		{"ForStatement#4", R"TS(for(;a;);)TS", "for (; a;)\n    ;", false},
		{"ForStatement#5", R"TS(for(;;a);)TS", "for (;; a)\n    ;", false},
		{"ForStatement#6", R"TS(for(;;){})TS", "for (;;) { }", false},
		{"ForInStatement#1", R"TS(for(a in b);)TS", "for (a in b)\n    ;", false},
		{"ForInStatement#2", R"TS(for(var a in b);)TS", "for (var a in b)\n    ;", false},
		{"ForInStatement#3", R"TS(for(a in b){})TS", "for (a in b) { }", false},
		{"ForOfStatement#1", R"TS(for(a of b);)TS", "for (a of b)\n    ;", false},
		{"ForOfStatement#2", R"TS(for(var a of b);)TS", "for (var a of b)\n    ;", false},
		{"ForOfStatement#3", R"TS(for(a of b){})TS", "for (a of b) { }", false},
		{"ForOfStatement#4", R"TS(for await(a of b);)TS", "for await (a of b)\n    ;", false},
		{"ForOfStatement#5", R"TS(for await(var a of b);)TS", "for await (var a of b)\n    ;", false},
		{"ForOfStatement#6", R"TS(for await(a of b){})TS", "for await (a of b) { }", false},
		{"ContinueStatement#1", R"TS(continue)TS", "continue;", false},
		{"ContinueStatement#2", R"TS(continue a)TS", "continue a;", false},
		{"BreakStatement#1", R"TS(break)TS", "break;", false},
		{"BreakStatement#2", R"TS(break a)TS", "break a;", false},
		{"ReturnStatement#1", R"TS(return)TS", "return;", false},
		{"ReturnStatement#2", R"TS(return a)TS", "return a;", false},
		{"WithStatement#1", R"TS(with(a);)TS", "with (a)\n    ;", false},
		{"WithStatement#2", R"TS(with(a){})TS", "with (a) { }", false},
		{"SwitchStatement", R"TS(switch (a) {})TS", "switch (a) {\n}", false},
		{"CaseClause#1", R"TS(switch (a) {case b:})TS", "switch (a) {\n    case b:\n}", false},
		{"CaseClause#2", R"TS(switch (a) {case b:;})TS", "switch (a) {\n    case b: ;\n}", false},
		{"DefaultClause#1", R"TS(switch (a) {default:})TS", "switch (a) {\n    default:\n}", false},
		{"DefaultClause#2", R"TS(switch (a) {default:;})TS", "switch (a) {\n    default: ;\n}", false},
		{"LabeledStatement", R"TS(a:;)TS", "a: ;", false},
		{"ThrowStatement", R"TS(throw a)TS", "throw a;", false},
		{"TryStatement#1", R"TS(try {} catch {})TS", "try { }\ncatch { }", false},
		{"TryStatement#2", R"TS(try {} finally {})TS", "try { }\nfinally { }", false},
		{"TryStatement#3", R"TS(try {} catch {} finally {})TS", "try { }\ncatch { }\nfinally { }", false},
		{"DebuggerStatement", R"TS(debugger)TS", "debugger;", false},
		{"FunctionDeclaration#1", R"TS(export default function(){})TS", R"TS(export default function () { })TS", false},
		{"FunctionDeclaration#2", R"TS(function f(){})TS", R"TS(function f() { })TS", false},
		{"FunctionDeclaration#3", R"TS(function*f(){})TS", R"TS(function* f() { })TS", false},
		{"FunctionDeclaration#4", R"TS(async function f(){})TS", R"TS(async function f() { })TS", false},
		{"FunctionDeclaration#5", R"TS(async function*f(){})TS", R"TS(async function* f() { })TS", false},
		{"FunctionDeclaration#6", R"TS(function f<T>(){})TS", R"TS(function f<T>() { })TS", false},
		{"FunctionDeclaration#7", R"TS(function f(a){})TS", R"TS(function f(a) { })TS", false},
		{"FunctionDeclaration#8", R"TS(function f():T{})TS", R"TS(function f(): T { })TS", false},
		{"FunctionDeclaration#9", R"TS(function f();)TS", R"TS(function f();)TS", false},
		{"ClassDeclaration#1", R"TS(class a {})TS", "class a {\n}", false},
		{"ClassDeclaration#2", R"TS(class a<T>{})TS", "class a<T> {\n}", false},
		{"ClassDeclaration#3", R"TS(class a extends b {})TS", "class a extends b {\n}", false},
		{"ClassDeclaration#4", R"TS(class a implements b {})TS", "class a implements b {\n}", false},
		{"ClassDeclaration#5", R"TS(class a implements b, c {})TS", "class a implements b, c {\n}", false},
		{"ClassDeclaration#6", R"TS(class a extends b implements c, d {})TS", "class a extends b implements c, d {\n}", false},
		{"ClassDeclaration#7", R"TS(export default class {})TS", "export default class {\n}", false},
		{"ClassDeclaration#8", R"TS(export default class<T>{})TS", "export default class<T> {\n}", false},
		{"ClassDeclaration#9", R"TS(export default class extends b {})TS", "export default class extends b {\n}", false},
		{"ClassDeclaration#10", R"TS(export default class implements b {})TS", "export default class implements b {\n}", false},
		{"ClassDeclaration#11", R"TS(export default class implements b, c {})TS", "export default class implements b, c {\n}", false},
		{"ClassDeclaration#12", R"TS(export default class extends b implements c, d {})TS", "export default class extends b implements c, d {\n}", false},
		{"ClassDeclaration#13", R"TS(@a class b {})TS", "@a\nclass b {\n}", false},
		{"ClassDeclaration#14", R"TS(@a export class b {})TS", "@a\nexport class b {\n}", false},
		{"ClassDeclaration#15", R"TS(export @a class b {})TS", "export \n@a\nclass b {\n}", false},
		{"InterfaceDeclaration#1", R"TS(interface a {})TS", "interface a {\n}", false},
		{"InterfaceDeclaration#2", R"TS(interface a<T>{})TS", "interface a<T> {\n}", false},
		{"InterfaceDeclaration#3", R"TS(interface a extends b {})TS", "interface a extends b {\n}", false},
		{"InterfaceDeclaration#4", R"TS(interface a extends b, c {})TS", "interface a extends b, c {\n}", false},
		{"TypeAliasDeclaration#1", R"TS(type a = b)TS", "type a = b;", false},
		{"TypeAliasDeclaration#2", R"TS(type a<T> = b)TS", "type a<T> = b;", false},
		{"EnumDeclaration#1", R"TS(enum a{})TS", "enum a {\n}", false},
		{"EnumDeclaration#2", R"TS(enum a{b})TS", "enum a {\n    b\n}", false},
		{"EnumDeclaration#3", R"TS(enum a{b=c})TS", "enum a {\n    b = c\n}", false},
		{"ModuleDeclaration#1", R"TS(module a{})TS", "module a { }", false},
		{"ModuleDeclaration#2", R"TS(module a.b{})TS", "module a.b { }", false},
		{"ModuleDeclaration#3", R"TS(module "a";)TS", "module \"a\";", false},
		{"ModuleDeclaration#4", R"TS(module "a"{})TS", "module \"a\" { }", false},
		{"ModuleDeclaration#5", R"TS(namespace a{})TS", "namespace a { }", false},
		{"ModuleDeclaration#6", R"TS(namespace a.b{})TS", "namespace a.b { }", false},
		{"ModuleDeclaration#7", R"TS(global;)TS", "global;", false},
		{"ModuleDeclaration#8", R"TS(global{})TS", "global { }", false},
		{"ImportEqualsDeclaration#1", R"TS(import a = b)TS", "import a = b;", false},
		{"ImportEqualsDeclaration#2", R"TS(import a = b.c)TS", "import a = b.c;", false},
		{"ImportEqualsDeclaration#3", R"TS(import a = require("b"))TS", "import a = require(\"b\");", false},
		{"ImportEqualsDeclaration#4", R"TS(export import a = b)TS", "export import a = b;", false},
		{"ImportEqualsDeclaration#5", R"TS(export import a = require("b"))TS", "export import a = require(\"b\");", false},
		{"ImportEqualsDeclaration#6", R"TS(import type a = b)TS", "import type a = b;", false},
		{"ImportEqualsDeclaration#7", R"TS(import type a = b.c)TS", "import type a = b.c;", false},
		{"ImportEqualsDeclaration#8", R"TS(import type a = require("b"))TS", "import type a = require(\"b\");", false},
		{"ImportDeclaration#1", R"TS(import "a")TS", "import \"a\";", false},
		{"ImportDeclaration#2", R"TS(import a from "b")TS", "import a from \"b\";", false},
		{"ImportDeclaration#3", R"TS(import type a from "b")TS", "import type a from \"b\";", false},
		{"ImportDeclaration#4", R"TS(import * as a from "b")TS", "import * as a from \"b\";", false},
		{"ImportDeclaration#5", R"TS(import type * as a from "b")TS", "import type * as a from \"b\";", false},
		{"ImportDeclaration#6", R"TS(import {} from "b")TS", "import {} from \"b\";", false},
		{"ImportDeclaration#7", R"TS(import type {} from "b")TS", "import type {} from \"b\";", false},
		{"ImportDeclaration#8", R"TS(import { a } from "b")TS", "import { a } from \"b\";", false},
		{"ImportDeclaration#9", R"TS(import type { a } from "b")TS", "import type { a } from \"b\";", false},
		{"ImportDeclaration#8", R"TS(import { a as b } from "c")TS", "import { a as b } from \"c\";", false},
		{"ImportDeclaration#9", R"TS(import type { a as b } from "c")TS", "import type { a as b } from \"c\";", false},
		{"ImportDeclaration#10", R"TS(import { "a" as b } from "c")TS", "import { \"a\" as b } from \"c\";", false},
		{"ImportDeclaration#11", R"TS(import type { "a" as b } from "c")TS", "import type { \"a\" as b } from \"c\";", false},
		{"ImportDeclaration#12", R"TS(import a, {} from "b")TS", "import a, {} from \"b\";", false},
		{"ImportDeclaration#13", R"TS(import a, * as b from "c")TS", "import a, * as b from \"c\";", false},
		{"ImportDeclaration#14", R"TS(import {} from "a" with {})TS", "import {} from \"a\" with {};", false},
		{"ImportDeclaration#15", R"TS(import {} from "a" with { b: "c" })TS", "import {} from \"a\" with { b: \"c\" };", false},
		{"ImportDeclaration#16", R"TS(import {} from "a" with { "b": "c" })TS", "import {} from \"a\" with { \"b\": \"c\" };", false},
		{"ExportAssignment#1", R"TS(export = a)TS", "export = a;", false},
		{"ExportAssignment#2", R"TS(export default a)TS", "export default a;", false},
		{"NamespaceExportDeclaration", R"TS(export as namespace a)TS", "export as namespace a;", false},
		{"ExportDeclaration#1", R"TS(export * from "a")TS", "export * from \"a\";", false},
		{"ExportDeclaration#2", R"TS(export type * from "a")TS", "export type * from \"a\";", false},
		{"ExportDeclaration#3", R"TS(export * as a from "b")TS", "export * as a from \"b\";", false},
		{"ExportDeclaration#4", R"TS(export type * as a from "b")TS", "export type * as a from \"b\";", false},
		{"ExportDeclaration#5", R"TS(export { } from "a")TS", "export {} from \"a\";", false},
		{"ExportDeclaration#6", R"TS(export type { } from "a")TS", "export type {} from \"a\";", false},
		{"ExportDeclaration#7", R"TS(export { a } from "b")TS", "export { a } from \"b\";", false},
		{"ExportDeclaration#8", R"TS(export { type a } from "b")TS", "export { type a } from \"b\";", false},
		{"ExportDeclaration#9", R"TS(export type { a } from "b")TS", "export type { a } from \"b\";", false},
		{"ExportDeclaration#10", R"TS(export { a as b } from "c")TS", "export { a as b } from \"c\";", false},
		{"ExportDeclaration#11", R"TS(export { type a as b } from "c")TS", "export { type a as b } from \"c\";", false},
		{"ExportDeclaration#12", R"TS(export type { a as b } from "c")TS", "export type { a as b } from \"c\";", false},
		{"ExportDeclaration#13", R"TS(export { a as "b" } from "c")TS", "export { a as \"b\" } from \"c\";", false},
		{"ExportDeclaration#14", R"TS(export { type a as "b" } from "c")TS", "export { type a as \"b\" } from \"c\";", false},
		{"ExportDeclaration#15", R"TS(export type { a as "b" } from "c")TS", "export type { a as \"b\" } from \"c\";", false},
		{"ExportDeclaration#16", R"TS(export { "a" } from "b")TS", "export { \"a\" } from \"b\";", false},
		{"ExportDeclaration#17", R"TS(export { type "a" } from "b")TS", "export { type \"a\" } from \"b\";", false},
		{"ExportDeclaration#18", R"TS(export type { "a" } from "b")TS", "export type { \"a\" } from \"b\";", false},
		{"ExportDeclaration#19", R"TS(export { "a" as b } from "c")TS", "export { \"a\" as b } from \"c\";", false},
		{"ExportDeclaration#20", R"TS(export { type "a" as b } from "c")TS", "export { type \"a\" as b } from \"c\";", false},
		{"ExportDeclaration#21", R"TS(export type { "a" as b } from "c")TS", "export type { \"a\" as b } from \"c\";", false},
		{"ExportDeclaration#22", R"TS(export { "a" as "b" } from "c")TS", "export { \"a\" as \"b\" } from \"c\";", false},
		{"ExportDeclaration#23", R"TS(export { type "a" as "b" } from "c")TS", "export { type \"a\" as \"b\" } from \"c\";", false},
		{"ExportDeclaration#24", R"TS(export type { "a" as "b" } from "c")TS", "export type { \"a\" as \"b\" } from \"c\";", false},
		{"ExportDeclaration#25", R"TS(export { })TS", "export {};", false},
		{"ExportDeclaration#26", R"TS(export type { })TS", "export type {};", false},
		{"ExportDeclaration#27", R"TS(export { a })TS", "export { a };", false},
		{"ExportDeclaration#28", R"TS(export { type a })TS", "export { type a };", false},
		{"ExportDeclaration#29", R"TS(export type { a })TS", "export type { a };", false},
		{"ExportDeclaration#30", R"TS(export { a as b })TS", "export { a as b };", false},
		{"ExportDeclaration#31", R"TS(export { type a as b })TS", "export { type a as b };", false},
		{"ExportDeclaration#32", R"TS(export type { a as b })TS", "export type { a as b };", false},
		{"ExportDeclaration#33", R"TS(export { a as "b" })TS", "export { a as \"b\" };", false},
		{"ExportDeclaration#34", R"TS(export { type a as "b" })TS", "export { type a as \"b\" };", false},
		{"ExportDeclaration#35", R"TS(export type { a as "b" })TS", "export type { a as \"b\" };", false},
		{"ExportDeclaration#36", R"TS(export {} from "a" with {})TS", "export {} from \"a\" with {};", false},
		{"ExportDeclaration#37", R"TS(export {} from "a" with { b: "c" })TS", "export {} from \"a\" with { b: \"c\" };", false},
		{"ExportDeclaration#38", R"TS(export {} from "a" with { "b": "c" })TS", "export {} from \"a\" with { \"b\": \"c\" };", false},
		{"KeywordTypeNode#1", R"TS(type T = any)TS", R"TS(type T = any;)TS", false},
		{"KeywordTypeNode#2", R"TS(type T = unknown)TS", R"TS(type T = unknown;)TS", false},
		{"KeywordTypeNode#3", R"TS(type T = never)TS", R"TS(type T = never;)TS", false},
		{"KeywordTypeNode#4", R"TS(type T = void)TS", R"TS(type T = void;)TS", false},
		{"KeywordTypeNode#5", R"TS(type T = undefined)TS", R"TS(type T = undefined;)TS", false},
		{"KeywordTypeNode#6", R"TS(type T = null)TS", R"TS(type T = null;)TS", false},
		{"KeywordTypeNode#7", R"TS(type T = object)TS", R"TS(type T = object;)TS", false},
		{"KeywordTypeNode#8", R"TS(type T = string)TS", R"TS(type T = string;)TS", false},
		{"KeywordTypeNode#9", R"TS(type T = symbol)TS", R"TS(type T = symbol;)TS", false},
		{"KeywordTypeNode#10", R"TS(type T = number)TS", R"TS(type T = number;)TS", false},
		{"KeywordTypeNode#11", R"TS(type T = bigint)TS", R"TS(type T = bigint;)TS", false},
		{"KeywordTypeNode#12", R"TS(type T = boolean)TS", R"TS(type T = boolean;)TS", false},
		{"KeywordTypeNode#13", R"TS(type T = intrinsic)TS", R"TS(type T = intrinsic;)TS", false},
		{"TypePredicateNode#1", R"TS(function f(): asserts a)TS", R"TS(function f(): asserts a;)TS", false},
		{"TypePredicateNode#2", R"TS(function f(): asserts a is b)TS", R"TS(function f(): asserts a is b;)TS", false},
		{"TypePredicateNode#3", R"TS(function f(): asserts this)TS", R"TS(function f(): asserts this;)TS", false},
		{"TypePredicateNode#4", R"TS(function f(): asserts this is b)TS", R"TS(function f(): asserts this is b;)TS", false},
		{"TypeReferenceNode#1", R"TS(type T = a)TS", R"TS(type T = a;)TS", false},
		{"TypeReferenceNode#2", R"TS(type T = a.b)TS", R"TS(type T = a.b;)TS", false},
		{"TypeReferenceNode#3", R"TS(type T = a<U>)TS", R"TS(type T = a<U>;)TS", false},
		{"TypeReferenceNode#4", R"TS(type T = a.b<U>)TS", R"TS(type T = a.b<U>;)TS", false},
		{"FunctionTypeNode#1", R"TS(type T = () => a)TS", R"TS(type T = () => a;)TS", false},
		{"FunctionTypeNode#2", R"TS(type T = <T>() => a)TS", R"TS(type T = <T>() => a;)TS", false},
		{"FunctionTypeNode#3", R"TS(type T = (a) => b)TS", R"TS(type T = (a) => b;)TS", false},
		{"ConstructorTypeNode#1", R"TS(type T = new () => a)TS", R"TS(type T = new () => a;)TS", false},
		{"ConstructorTypeNode#2", R"TS(type T = new <T>() => a)TS", R"TS(type T = new <T>() => a;)TS", false},
		{"ConstructorTypeNode#3", R"TS(type T = new (a) => b)TS", R"TS(type T = new (a) => b;)TS", false},
		{"ConstructorTypeNode#4", R"TS(type T = abstract new () => a)TS", R"TS(type T = abstract new () => a;)TS", false},
		{"TypeQueryNode#1", R"TS(type T = typeof a)TS", R"TS(type T = typeof a;)TS", false},
		{"TypeQueryNode#2", R"TS(type T = typeof a.b)TS", R"TS(type T = typeof a.b;)TS", false},
		{"TypeQueryNode#3", R"TS(type T = typeof a<U>)TS", R"TS(type T = typeof a<U>;)TS", false},
		{"TypeLiteralNode#1", R"TS(type T = {})TS", R"TS(type T = {};)TS", false},
		{"TypeLiteralNode#2", R"TS(type T = {a})TS", "type T = {\n    a;\n};", false},
		{"ArrayTypeNode", R"TS(type T = a[])TS", "type T = a[];", false},
		{"TupleTypeNode#1", R"TS(type T = [])TS", "type T = [\n];", false},
		{"TupleTypeNode#2", R"TS(type T = [a])TS", "type T = [\n    a\n];", false},
		{"TupleTypeNode#3", R"TS(type T = [a,])TS", "type T = [\n    a\n];", false},
		{"RestTypeNode", R"TS(type T = [...a])TS", "type T = [\n    ...a\n];", false},
		{"OptionalTypeNode", R"TS(type T = [a?])TS", "type T = [\n    a?\n];", false},
		{"NamedTupleMember#1", R"TS(type T = [a: b])TS", "type T = [\n    a: b\n];", false},
		{"NamedTupleMember#2", R"TS(type T = [a?: b])TS", "type T = [\n    a?: b\n];", false},
		{"NamedTupleMember#3", R"TS(type T = [...a: b])TS", "type T = [\n    ...a: b\n];", false},
		{"UnionTypeNode#1", R"TS(type T = a | b)TS", "type T = a | b;", false},
		{"UnionTypeNode#2", R"TS(type T = a | b | c)TS", "type T = a | b | c;", false},
		{"UnionTypeNode#3", R"TS(type T = | a | b)TS", "type T = a | b;", false},
		{"IntersectionTypeNode#1", R"TS(type T = a & b)TS", "type T = a & b;", false},
		{"IntersectionTypeNode#2", R"TS(type T = a & b & c)TS", "type T = a & b & c;", false},
		{"IntersectionTypeNode#3", R"TS(type T = & a & b)TS", "type T = a & b;", false},
		{"ConditionalTypeNode", R"TS(type T = a extends b ? c : d)TS", "type T = a extends b ? c : d;", false},
		{"InferTypeNode#1", R"TS(type T = a extends infer b ? c : d)TS", "type T = a extends infer b ? c : d;", false},
		{"InferTypeNode#2", R"TS(type T = a extends infer b extends c ? d : e)TS", "type T = a extends infer b extends c ? d : e;", false},
		{"ParenthesizedTypeNode", R"TS(type T = (U))TS", "type T = (U);", false},
		{"ThisTypeNode", R"TS(type T = this)TS", "type T = this;", false},
		{"TypeOperatorNode#1", R"TS(type T = keyof U)TS", "type T = keyof U;", false},
		{"TypeOperatorNode#2", R"TS(type T = readonly U[])TS", "type T = readonly U[];", false},
		{"TypeOperatorNode#3", R"TS(type T = unique symbol)TS", "type T = unique symbol;", false},
		{"IndexedAccessTypeNode", R"TS(type T = a[b])TS", "type T = a[b];", false},
		{"MappedTypeNode#1", R"TS(type T = { [a in b]: c })TS", "type T = {\n    [a in b]: c;\n};", false},
		{"MappedTypeNode#2", R"TS(type T = { [a in b as c]: d })TS", "type T = {\n    [a in b as c]: d;\n};", false},
		{"MappedTypeNode#3", R"TS(type T = { readonly [a in b]: c })TS", "type T = {\n    readonly [a in b]: c;\n};", false},
		{"MappedTypeNode#4", R"TS(type T = { +readonly [a in b]: c })TS", "type T = {\n    +readonly [a in b]: c;\n};", false},
		{"MappedTypeNode#5", R"TS(type T = { -readonly [a in b]: c })TS", "type T = {\n    -readonly [a in b]: c;\n};", false},
		{"MappedTypeNode#6", R"TS(type T = { [a in b]?: c })TS", "type T = {\n    [a in b]?: c;\n};", false},
		{"MappedTypeNode#7", R"TS(type T = { [a in b]+?: c })TS", "type T = {\n    [a in b]+?: c;\n};", false},
		{"MappedTypeNode#8", R"TS(type T = { [a in b]-?: c })TS", "type T = {\n    [a in b]-?: c;\n};", false},
		{"MappedTypeNode#9", R"TS(type T = { [a in b]: c; d })TS", "type T = {\n    [a in b]: c;\n    d;\n};", false},
		{"LiteralTypeNode#1", R"TS(type T = null)TS", "type T = null;", false},
		{"LiteralTypeNode#2", R"TS(type T = true)TS", "type T = true;", false},
		{"LiteralTypeNode#3", R"TS(type T = false)TS", "type T = false;", false},
		{"LiteralTypeNode#4", R"TS(type T = "")TS", "type T = \"\";", false},
		{"LiteralTypeNode#5", "type T = ''", "type T = '';", false},
		{"LiteralTypeNode#6", "type T = ``", "type T = ``;", false},
		{"LiteralTypeNode#7", R"TS(type T = 0)TS", "type T = 0;", false},
		{"LiteralTypeNode#8", R"TS(type T = 0n)TS", "type T = 0n;", false},
		{"LiteralTypeNode#9", R"TS(type T = -0)TS", "type T = -0;", false},
		{"LiteralTypeNode#10", R"TS(type T = -0n)TS", "type T = -0n;", false},
		{"TemplateTypeNode#1", "type T = `a${b}c`", "type T = `a${b}c`;", false},
		{"TemplateTypeNode#2", "type T = `a${b}c${d}e`", "type T = `a${b}c${d}e`;", false},
		{"ImportTypeNode#1", R"TS(type T = import(a))TS", "type T = import(a);", false},
		{"ImportTypeNode#2", R"TS(type T = import(a).b)TS", "type T = import(a).b;", false},
		{"ImportTypeNode#3", R"TS(type T = import(a).b<U>)TS", "type T = import(a).b<U>;", false},
		{"ImportTypeNode#4", R"TS(type T = typeof import(a))TS", "type T = typeof import(a);", false},
		{"ImportTypeNode#5", R"TS(type T = typeof import(a).b)TS", "type T = typeof import(a).b;", false},
		{"ImportTypeNode#6", R"TS(type T = import(a, { with: { } }))TS", "type T = import(a, { with: {} });", false},
		{"ImportTypeNode#6", R"TS(type T = import(a, { with: { b: "c" } }))TS", "type T = import(a, { with: { b: \"c\" } });", false},
		{"ImportTypeNode#7", R"TS(type T = import(a, { with: { "b": "c" } }))TS", "type T = import(a, { with: { \"b\": \"c\" } });", false},
		{"PropertySignature#1", "interface I {a}", "interface I {\n    a;\n}", false},
		{"PropertySignature#2", "interface I {readonly a}", "interface I {\n    readonly a;\n}", false},
		{"PropertySignature#3", "interface I {\"a\"}", "interface I {\n    \"a\";\n}", false},
		{"PropertySignature#4", "interface I {'a'}", "interface I {\n    'a';\n}", false},
		{"PropertySignature#5", "interface I {0}", "interface I {\n    0;\n}", false},
		{"PropertySignature#6", "interface I {0n}", "interface I {\n    0n;\n}", false},
		{"PropertySignature#7", "interface I {[a]}", "interface I {\n    [a];\n}", false},
		{"PropertySignature#8", "interface I {a?}", "interface I {\n    a?;\n}", false},
		{"PropertySignature#9", "interface I {a: b}", "interface I {\n    a: b;\n}", false},
		{"MethodSignature#1", "interface I {a()}", "interface I {\n    a();\n}", false},
		{"MethodSignature#2", "interface I {\"a\"()}", "interface I {\n    \"a\"();\n}", false},
		{"MethodSignature#3", "interface I {'a'()}", "interface I {\n    'a'();\n}", false},
		{"MethodSignature#4", "interface I {0()}", "interface I {\n    0();\n}", false},
		{"MethodSignature#5", "interface I {0n()}", "interface I {\n    0n();\n}", false},
		{"MethodSignature#6", "interface I {[a]()}", "interface I {\n    [a]();\n}", false},
		{"MethodSignature#7", "interface I {a?()}", "interface I {\n    a?();\n}", false},
		{"MethodSignature#8", "interface I {a<T>()}", "interface I {\n    a<T>();\n}", false},
		{"MethodSignature#9", "interface I {a(): b}", "interface I {\n    a(): b;\n}", false},
		{"MethodSignature#10", "interface I {a(b): c}", "interface I {\n    a(b): c;\n}", false},
		{"CallSignature#1", "interface I {()}", "interface I {\n    ();\n}", false},
		{"CallSignature#2", "interface I {():a}", "interface I {\n    (): a;\n}", false},
		{"CallSignature#3", "interface I {(p)}", "interface I {\n    (p);\n}", false},
		{"CallSignature#4", "interface I {<T>()}", "interface I {\n    <T>();\n}", false},
		{"ConstructSignature#1", "interface I {new ()}", "interface I {\n    new ();\n}", false},
		{"ConstructSignature#2", "interface I {new ():a}", "interface I {\n    new (): a;\n}", false},
		{"ConstructSignature#3", "interface I {new (p)}", "interface I {\n    new (p);\n}", false},
		{"ConstructSignature#4", "interface I {new <T>()}", "interface I {\n    new <T>();\n}", false},
		{"IndexSignatureDeclaration#1", "interface I {[a]}", "interface I {\n    [a];\n}", false},
		{"IndexSignatureDeclaration#2", "interface I {[a: b]}", "interface I {\n    [a: b];\n}", false},
		{"IndexSignatureDeclaration#3", "interface I {[a: b]: c}", "interface I {\n    [a: b]: c;\n}", false},
		{"PropertyDeclaration#1", "class C {a}", "class C {\n    a;\n}", false},
		{"PropertyDeclaration#2", "class C {readonly a}", "class C {\n    readonly a;\n}", false},
		{"PropertyDeclaration#3", "class C {static a}", "class C {\n    static a;\n}", false},
		{"PropertyDeclaration#4", "class C {accessor a}", "class C {\n    accessor a;\n}", false},
		{"PropertyDeclaration#5", "class C {\"a\"}", "class C {\n    \"a\";\n}", false},
		{"PropertyDeclaration#6", "class C {'a'}", "class C {\n    'a';\n}", false},
		{"PropertyDeclaration#7", "class C {0}", "class C {\n    0;\n}", false},
		{"PropertyDeclaration#8", "class C {0n}", "class C {\n    0n;\n}", false},
		{"PropertyDeclaration#9", "class C {[a]}", "class C {\n    [a];\n}", false},
		{"PropertyDeclaration#10", "class C {#a}", "class C {\n    #a;\n}", false},
		{"PropertyDeclaration#11", "class C {a?}", "class C {\n    a?;\n}", false},
		{"PropertyDeclaration#12", "class C {a!}", "class C {\n    a!;\n}", false},
		{"PropertyDeclaration#13", "class C {a: b}", "class C {\n    a: b;\n}", false},
		{"PropertyDeclaration#14", "class C {a = b}", "class C {\n    a = b;\n}", false},
		{"PropertyDeclaration#15", "class C {@a b}", "class C {\n    @a\n    b;\n}", false},
		{"MethodDeclaration#1", "class C {a()}", "class C {\n    a();\n}", false},
		{"MethodDeclaration#2", "class C {\"a\"()}", "class C {\n    \"a\"();\n}", false},
		{"MethodDeclaration#3", "class C {'a'()}", "class C {\n    'a'();\n}", false},
		{"MethodDeclaration#4", "class C {0()}", "class C {\n    0();\n}", false},
		{"MethodDeclaration#5", "class C {0n()}", "class C {\n    0n();\n}", false},
		{"MethodDeclaration#6", "class C {[a]()}", "class C {\n    [a]();\n}", false},
		{"MethodDeclaration#7", "class C {#a()}", "class C {\n    #a();\n}", false},
		{"MethodDeclaration#8", "class C {a?()}", "class C {\n    a?();\n}", false},
		{"MethodDeclaration#9", "class C {a<T>()}", "class C {\n    a<T>();\n}", false},
		{"MethodDeclaration#10", "class C {a(): b}", "class C {\n    a(): b;\n}", false},
		{"MethodDeclaration#11", "class C {a(b): c}", "class C {\n    a(b): c;\n}", false},
		{"MethodDeclaration#12", "class C {a() {} }", "class C {\n    a() { }\n}", false},
		{"MethodDeclaration#13", "class C {@a b() {} }", "class C {\n    @a\n    b() { }\n}", false},
		{"MethodDeclaration#14", "class C {static a() {} }", "class C {\n    static a() { }\n}", false},
		{"MethodDeclaration#15", "class C {async a() {} }", "class C {\n    async a() { }\n}", false},
		{"GetAccessorDeclaration#1", "class C {get a()}", "class C {\n    get a();\n}", false},
		{"GetAccessorDeclaration#2", "class C {get \"a\"()}", "class C {\n    get \"a\"();\n}", false},
		{"GetAccessorDeclaration#3", "class C {get 'a'()}", "class C {\n    get 'a'();\n}", false},
		{"GetAccessorDeclaration#4", "class C {get 0()}", "class C {\n    get 0();\n}", false},
		{"GetAccessorDeclaration#5", "class C {get 0n()}", "class C {\n    get 0n();\n}", false},
		{"GetAccessorDeclaration#6", "class C {get [a]()}", "class C {\n    get [a]();\n}", false},
		{"GetAccessorDeclaration#7", "class C {get #a()}", "class C {\n    get #a();\n}", false},
		{"GetAccessorDeclaration#8", "class C {get a(): b}", "class C {\n    get a(): b;\n}", false},
		{"GetAccessorDeclaration#9", "class C {get a(b): c}", "class C {\n    get a(b): c;\n}", false},
		{"GetAccessorDeclaration#10", "class C {get a() {} }", "class C {\n    get a() { }\n}", false},
		{"GetAccessorDeclaration#11", "class C {@a get b() {} }", "class C {\n    @a\n    get b() { }\n}", false},
		{"GetAccessorDeclaration#12", "class C {static get a() {} }", "class C {\n    static get a() { }\n}", false},
		{"SetAccessorDeclaration#1", "class C {set a()}", "class C {\n    set a();\n}", false},
		{"SetAccessorDeclaration#2", "class C {set \"a\"()}", "class C {\n    set \"a\"();\n}", false},
		{"SetAccessorDeclaration#3", "class C {set 'a'()}", "class C {\n    set 'a'();\n}", false},
		{"SetAccessorDeclaration#4", "class C {set 0()}", "class C {\n    set 0();\n}", false},
		{"SetAccessorDeclaration#5", "class C {set 0n()}", "class C {\n    set 0n();\n}", false},
		{"SetAccessorDeclaration#6", "class C {set [a]()}", "class C {\n    set [a]();\n}", false},
		{"SetAccessorDeclaration#7", "class C {set #a()}", "class C {\n    set #a();\n}", false},
		{"SetAccessorDeclaration#8", "class C {set a(): b}", "class C {\n    set a(): b;\n}", false},
		{"SetAccessorDeclaration#9", "class C {set a(b): c}", "class C {\n    set a(b): c;\n}", false},
		{"SetAccessorDeclaration#10", "class C {set a() {} }", "class C {\n    set a() { }\n}", false},
		{"SetAccessorDeclaration#11", "class C {@a set b() {} }", "class C {\n    @a\n    set b() { }\n}", false},
		{"SetAccessorDeclaration#12", "class C {static set a() {} }", "class C {\n    static set a() { }\n}", false},
		{"ConstructorDeclaration#1", "class C {constructor()}", "class C {\n    constructor();\n}", false},
		{"ConstructorDeclaration#2", "class C {constructor(): b}", "class C {\n    constructor(): b;\n}", false},
		{"ConstructorDeclaration#3", "class C {constructor(b): c}", "class C {\n    constructor(b): c;\n}", false},
		{"ConstructorDeclaration#4", "class C {constructor() {} }", "class C {\n    constructor() { }\n}", false},
		{"ConstructorDeclaration#5", "class C {@a constructor() {} }", "class C {\n    constructor() { }\n}", false},
		{"ConstructorDeclaration#6", "class C {private constructor() {} }", "class C {\n    private constructor() { }\n}", false},
		{"ClassStaticBlockDeclaration", "class C {static { }}", "class C {\n    static { }\n}", false},
		{"SemicolonClassElement#1", "class C {;}", "class C {\n    ;\n}", false},
		{"ParameterDeclaration#1", "function f(a)", "function f(a);", false},
		{"ParameterDeclaration#2", "function f(a: b)", "function f(a: b);", false},
		{"ParameterDeclaration#3", "function f(a = b)", "function f(a = b);", false},
		{"ParameterDeclaration#4", "function f(a?)", "function f(a?);", false},
		{"ParameterDeclaration#5", "function f(...a)", "function f(...a);", false},
		{"ParameterDeclaration#6", "function f(this)", "function f(this);", false},
		// {title: "ParameterDeclaration#7", input: "function f(a)", output: "function f(a);"}, // TODO: preserve trailing comma after Strada migration
		{"ObjectBindingPattern#1", "function f({})", "function f({});", false},
		{"ObjectBindingPattern#2", "function f({a})", "function f({ a });", false},
		{"ObjectBindingPattern#3", "function f({a = b})", "function f({ a = b });", false},
		{"ObjectBindingPattern#4", "function f({a: b})", "function f({ a: b });", false},
		{"ObjectBindingPattern#5", "function f({a: b = c})", "function f({ a: b = c });", false},
		{"ObjectBindingPattern#6", "function f({\"a\": b})", "function f({ \"a\": b });", false},
		{"ObjectBindingPattern#7", "function f({'a': b})", "function f({ 'a': b });", false},
		{"ObjectBindingPattern#8", "function f({0: b})", "function f({ 0: b });", false},
		{"ObjectBindingPattern#9", "function f({[a]: b})", "function f({ [a]: b });", false},
		{"ObjectBindingPattern#10", "function f({...a})", "function f({ ...a });", false},
		{"ObjectBindingPattern#11", "function f({a: {}})", "function f({ a: {} });", false},
		{"ObjectBindingPattern#12", "function f({a: []})", "function f({ a: [] });", false},
		{"ArrayBindingPattern#1", "function f([])", "function f([]);", false},
		{"ArrayBindingPattern#2", "function f([,])", "function f([,]);", false},
		{"ArrayBindingPattern#3", "function f([a])", "function f([a]);", false},
		{"ArrayBindingPattern#4", "function f([a, b])", "function f([a, b]);", false},
		{"ArrayBindingPattern#5", "function f([a, , b])", "function f([a, , b]);", false},
		{"ArrayBindingPattern#6", "function f([a = b])", "function f([a = b]);", false},
		{"ArrayBindingPattern#7", "function f([...a])", "function f([...a]);", false},
		{"ArrayBindingPattern#8", "function f([{}])", "function f([{}]);", false},
		{"ArrayBindingPattern#9", "function f([[]])", "function f([[]]);", false},
		{"TypeParameterDeclaration#1", "function f<T>();", "function f<T>();", false},
		{"TypeParameterDeclaration#2", "function f<in T>();", "function f<in T>();", false},
		{"TypeParameterDeclaration#3", "function f<T extends U>();", "function f<T extends U>();", false},
		{"TypeParameterDeclaration#4", "function f<T = U>();", "function f<T = U>();", false},
		{"TypeParameterDeclaration#5", "function f<T extends U = V>();", "function f<T extends U = V>();", false},
		{"TypeParameterDeclaration#6", "function f<T, U>();", "function f<T, U>();", false},
		// {title: "TypeParameterDeclaration#7", input: "function f<T,>();", output: "function f<T,>();"}, // TODO: preserve trailing comma after Strada migration
		{"JsxElement1", "<a></a>", "<a></a>;", true},
		{"JsxElement2", "<this></this>", "<this></this>;", true},
		{"JsxElement3", "<a:b></a:b>", "<a:b></a:b>;", true},
		{"JsxElement4", "<a.b></a.b>", "<a.b></a.b>;", true},
		{"JsxElement5", "<a<b>></a>", "<a<b>></a>;", true},
		{"JsxElement6", "<a b></a>", "<a b></a>;", true},
		{"JsxElement7", "<a>b</a>", "<a>b</a>;", true},
		{"JsxElement8", "<a>{b}</a>", "<a>{b}</a>;", true},
		{"JsxElement9", "<a><b></b></a>", "<a><b></b></a>;", true},
		{"JsxElement10", "<a><b /></a>", "<a><b /></a>;", true},
		{"JsxElement11", "<a><></></a>", "<a><></></a>;", true},
		{"JsxElement12", "<a>\n    {/* missing */}\n    {\n        // foo\n    }\n</a>", "<a>\n    {/* missing */}\n    {\n    // foo\n    }\n</a>;", true},
		{"JsxSelfClosingElement1", "<a />", "<a />;", true},
		{"JsxSelfClosingElement2", "<this />", "<this />;", true},
		{"JsxSelfClosingElement3", "<a:b />", "<a:b />;", true},
		{"JsxSelfClosingElement4", "<a.b />", "<a.b />;", true},
		{"JsxSelfClosingElement5", "<a<b> />", "<a<b> />;", true},
		{"JsxSelfClosingElement6", "<a b/>", "<a b/>;", true},
		{"JsxFragment1", "<></>", "<></>;", true},
		{"JsxFragment2", "<>b</>", "<>b</>;", true},
		{"JsxFragment3", "<>{b}</>", "<>{b}</>;", true},
		{"JsxFragment4", "<><b></b></>", "<><b></b></>;", true},
		{"JsxFragment5", "<><b /></>", "<><b /></>;", true},
		{"JsxFragment6", "<><></></>", "<><></></>;", true},
		{"JsxAttribute1", "<a b/>", "<a b/>;", true},
		{"JsxAttribute2", "<a b:c/>", "<a b:c/>;", true},
		{"JsxAttribute3", "<a b=\"c\"/>", "<a b=\"c\"/>;", true},
		{"JsxAttribute4", "<a b='c'/>", "<a b='c'/>;", true},
		{"JsxAttribute5", "<a b={c}/>", "<a b={c}/>;", true},
		{"JsxAttribute6", "<a b=<c></c>/>", "<a b=<c></c>/>;", true},
		{"JsxAttribute7", "<a b=<c />/>", "<a b=<c />/>;", true},
		{"JsxAttribute8", "<a b=<></>/>", "<a b=<></>/>;", true},
		{"JsxSpreadAttribute", "<a {...b}/>", "<a {...b}/>;", true},};

	for (auto& rec : data) {
		t->Run(rec.title, [rec](T* t) {
			t->Parallel();
			auto* file = parsetestutil::ParseTypeScript(rec.input, rec.jsx);
			parsetestutil::CheckDiagnostics(t, file);
			emittestutil::CheckEmit(t, nullptr, file, rec.output);
		});
	}
}
REGISTER_UNIT_TEST("printer.TestEmit", TestEmit);

void TestParenthesizeDecorator(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newClassDeclaration(
				f.newModifierList(
					std::vector<Node*>{
						f.newDecorator(
							f.newBinaryExpression(
								nullptr, /*modifiers*/
								f.newIdentifier("a"),
								nullptr, /*typeNode*/
								f.newToken(Kind::PlusToken),
								f.newIdentifier("b")
							)
						),
					}
				),
				f.newIdentifier("C"),
				nullptr,
				nullptr,
				f.newNodeList(std::vector<Node*>{})
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "@(a + b)\nclass C {\n}");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeDecorator", TestParenthesizeDecorator);

void TestParenthesizeComputedPropertyName(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newClassDeclaration(
				nullptr, /*modifiers*/
				f.newIdentifier("C"),
				nullptr, /*typeParameters*/
				nullptr, /*heritageClauses*/
				f.newNodeList(std::vector<Node*>{
					f.newPropertyDeclaration(
						nullptr, /*modifiers*/
						f.newComputedPropertyName(
							// will be parenthesized on emit:
							f.newBinaryExpression(
								nullptr, /*modifiers*/
								f.newIdentifier("a"),
								nullptr, /*typeNode*/
								f.newToken(Kind::CommaToken),
								f.newIdentifier("b")
							)
						),
						nullptr, /*postfixToken*/
						nullptr, /*typeNode*/
						nullptr /*initializer*/
					),
				})
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "class C {\n    [(a, b)];\n}");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeComputedPropertyName", TestParenthesizeComputedPropertyName);

void TestParenthesizeArrayLiteral(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newArrayLiteralExpression(
					f.newNodeList(
						std::vector<Node*>{
							// will be parenthesized on emit:
							f.newBinaryExpression(
								nullptr, /*modifiers*/
								f.newIdentifier("a"),
								nullptr, /*typeNode*/
								f.newToken(Kind::CommaToken),
								f.newIdentifier("b")
							),
						}
					),
					false /*multiLine*/
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "[(a, b)];");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeArrayLiteral", TestParenthesizeArrayLiteral);

void TestParenthesizePropertyAccess1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newPropertyAccessExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					nullptr, /*questionDotToken*/
					f.newIdentifier("c"),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b).c;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizePropertyAccess1", TestParenthesizePropertyAccess1);

void TestParenthesizePropertyAccess2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newPropertyAccessExpression(
					// will be parenthesized on emit:
					f.newPropertyAccessExpression(
						f.newIdentifier("a"),
						f.newToken(Kind::QuestionDotToken),
						f.newIdentifier("b"),
						NodeFlagsOptionalChain
					),
					nullptr, /*questionDotToken*/
					f.newIdentifier("c"),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a?.b).c;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizePropertyAccess2", TestParenthesizePropertyAccess2);

void TestParenthesizePropertyAccess3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newPropertyAccessExpression(
					// will be parenthesized on emit:
					f.newNewExpression(
						f.newIdentifier("a"),
						nullptr, /*typeArguments*/
						nullptr /*arguments*/
					),
					nullptr, /*questionDotToken*/
					f.newIdentifier("b"),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(new a).b;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizePropertyAccess3", TestParenthesizePropertyAccess3);

void TestParenthesizeElementAccess1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newElementAccessExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					nullptr, /*questionDotToken*/
					f.newIdentifier("c"),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b)[c];");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeElementAccess1", TestParenthesizeElementAccess1);

void TestParenthesizeElementAccess2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newElementAccessExpression(
					// will be parenthesized on emit:
					f.newPropertyAccessExpression(
						f.newIdentifier("a"),
						f.newToken(Kind::QuestionDotToken),
						f.newIdentifier("b"),
						NodeFlagsOptionalChain
					),
					nullptr, /*questionDotToken*/
					f.newIdentifier("c"),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a?.b)[c];");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeElementAccess2", TestParenthesizeElementAccess2);

void TestParenthesizeElementAccess3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newElementAccessExpression(
					// will be parenthesized on emit:
					f.newNewExpression(
						f.newIdentifier("a"),
						nullptr, /*typeArguments*/
						nullptr /*arguments*/
					),
					nullptr, /*questionDotToken*/
					f.newIdentifier("b"),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(new a)[b];");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeElementAccess3", TestParenthesizeElementAccess3);

void TestParenthesizeCall1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newCallExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					nullptr, /*questionDotToken*/
					nullptr, /*typeArguments*/
					f.newNodeList(std::vector<Node*>{}),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b)();");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeCall1", TestParenthesizeCall1);

void TestParenthesizeCall2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newCallExpression(
					// will be parenthesized on emit:
					f.newPropertyAccessExpression(
						f.newIdentifier("a"),
						f.newToken(Kind::QuestionDotToken),
						f.newIdentifier("b"),
						NodeFlagsOptionalChain
					),
					nullptr, /*questionDotToken*/
					nullptr, /*typeArguments*/
					f.newNodeList(std::vector<Node*>{}),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a?.b)();");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeCall2", TestParenthesizeCall2);

void TestParenthesizeCall3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newCallExpression(
					// will be parenthesized on emit:
					f.newNewExpression(
						f.newIdentifier("C"),
						nullptr, /*typeArguments*/
						nullptr /*arguments*/
					),
					nullptr, /*questionDotToken*/
					nullptr, /*typeArguments*/
					f.newNodeList(std::vector<Node*>{}),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(new C)();");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeCall3", TestParenthesizeCall3);

void TestParenthesizeCall4(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newCallExpression(
					f.newIdentifier("a"),
					nullptr, /*questionDotToken*/
					nullptr, /*typeArguments*/
					f.newNodeList(std::vector<Node*>{
						f.newBinaryExpression(
							nullptr, /*modifiers*/
							f.newIdentifier("b"),
							nullptr, /*typeNode*/
							f.newToken(Kind::CommaToken),
							f.newIdentifier("c")
						),
					}),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "a((b, c));");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeCall4", TestParenthesizeCall4);

void TestParenthesizeNew1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newNewExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					nullptr, /*typeArguments*/
					f.newNodeList(std::vector<Node*>{})
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "new (a, b)();");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeNew1", TestParenthesizeNew1);

void TestParenthesizeNew2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newNewExpression(
					// will be parenthesized on emit:
					f.newCallExpression(
						f.newIdentifier("C"),
						nullptr, /*questionDotToken*/
						nullptr, /*typeArguments*/
						f.newNodeList(std::vector<Node*>{}),
						NodeFlagsNone
					),
					nullptr, /*typeArguments*/
					nullptr /*arguments*/
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "new (C());");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeNew2", TestParenthesizeNew2);

void TestParenthesizeNew3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newNewExpression(
					f.newIdentifier("C"),
					nullptr, /*typeArguments*/
					f.newNodeList(std::vector<Node*>{
						f.newBinaryExpression(
							nullptr, /*modifiers*/
							f.newIdentifier("a"),
							nullptr, /*typeNode*/
							f.newToken(Kind::CommaToken),
							f.newIdentifier("b")
						),
					})
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "new C((a, b));");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeNew3", TestParenthesizeNew3);

void TestParenthesizeTaggedTemplate1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newTaggedTemplateExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					nullptr, /*questionDotToken*/
					nullptr, /*typeArguments*/
					f.newNoSubstitutionTemplateLiteral("", TokenFlagsNone),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b) ``;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeTaggedTemplate1", TestParenthesizeTaggedTemplate1);

void TestParenthesizeTaggedTemplate2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newTaggedTemplateExpression(
					// will be parenthesized on emit:
					f.newPropertyAccessExpression(
						f.newIdentifier("a"),
						f.newToken(Kind::QuestionDotToken),
						f.newIdentifier("b"),
						NodeFlagsOptionalChain
					),
					nullptr, /*questionDotToken*/
					nullptr, /*typeArguments*/
					f.newNoSubstitutionTemplateLiteral("", TokenFlagsNone),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a?.b) ``;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeTaggedTemplate2", TestParenthesizeTaggedTemplate2);

void TestParenthesizeTypeAssertion1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newTypeAssertion(
					f.newTypeReferenceNode(
						f.newIdentifier("T"),
						nullptr /*typeArguments*/
					),
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::PlusToken),
						f.newIdentifier("b")
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "<T>(a + b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeTypeAssertion1", TestParenthesizeTypeAssertion1);

void TestParenthesizeArrowFunction1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newArrowFunction(
					nullptr, /*modifiers*/
					nullptr, /*typeParameters*/
					f.newNodeList(std::vector<Node*>{}),
					nullptr, /*returnType*/
					nullptr, /*fullSignature*/
					f.newToken(Kind::EqualsGreaterThanToken),
					// will be parenthesized on emit:
					f.newObjectLiteralExpression(
						f.newNodeList(std::vector<Node*>{}),
						false /*multiLine*/
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "() => ({});");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeArrowFunction1", TestParenthesizeArrowFunction1);

void TestParenthesizeArrowFunction2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newArrowFunction(
					nullptr, /*modifiers*/
					nullptr, /*typeParameters*/
					f.newNodeList(std::vector<Node*>{}),
					nullptr, /*returnType*/
					nullptr, /*fullSignature*/
					f.newToken(Kind::EqualsGreaterThanToken),
					// will be parenthesized on emit:
					f.newPropertyAccessExpression(
						f.newObjectLiteralExpression(
							f.newNodeList(std::vector<Node*>{}),
							false /*multiLine*/
						),
						nullptr, /*questionDotToken*/
						f.newIdentifier("a"),
						NodeFlagsNone
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "() => ({}.a);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeArrowFunction2", TestParenthesizeArrowFunction2);

void TestParenthesizeDelete(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newDeleteExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::PlusToken),
						f.newIdentifier("b")
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "delete (a + b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeDelete", TestParenthesizeDelete);

void TestParenthesizeVoid(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newVoidExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::PlusToken),
						f.newIdentifier("b")
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "void (a + b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeVoid", TestParenthesizeVoid);

void TestParenthesizeTypeOf(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newTypeOfExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::PlusToken),
						f.newIdentifier("b")
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "typeof (a + b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeTypeOf", TestParenthesizeTypeOf);

void TestParenthesizeAwait(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newAwaitExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::PlusToken),
						f.newIdentifier("b")
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "await (a + b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeAwait", TestParenthesizeAwait);

void TestParenthesizeBinary(T* t) {
	t->Parallel();

	struct Row {
		Kind left;
		Kind op;
		Kind right;
		const char* output;
	};
	static const Row data[] = {
	    {Kind::Unknown, Kind::CommaToken, Kind::Unknown, "l, r"},
	    {Kind::PlusToken, Kind::CommaToken, Kind::Unknown, "ll + lr, r"},
	    {Kind::PlusToken, Kind::AsteriskToken, Kind::Unknown, "(ll + lr) * r"},
	    {Kind::Unknown, Kind::AsteriskToken, Kind::PlusToken, "l * (rl + rr)"},
	    {Kind::AsteriskToken, Kind::PlusToken, Kind::Unknown, "ll * lr + r"},
	    {Kind::Unknown, Kind::PlusToken, Kind::AsteriskToken, "l + rl * rr"},
	    {Kind::AsteriskToken, Kind::SlashToken, Kind::Unknown, "ll * lr / r"},
	    {Kind::AsteriskAsteriskToken, Kind::SlashToken, Kind::Unknown,
	     "ll ** lr / r"},
	    {Kind::AsteriskToken, Kind::AsteriskAsteriskToken, Kind::Unknown,
	     "(ll * lr) ** r"},
	    {Kind::AsteriskAsteriskToken, Kind::AsteriskAsteriskToken,
	     Kind::Unknown, "(ll ** lr) ** r"},
	    {Kind::Unknown, Kind::AsteriskToken, Kind::AsteriskToken, "l * rl * rr"},
	    {Kind::Unknown, Kind::BarToken, Kind::BarToken, "l | rl | rr"},
	    {Kind::Unknown, Kind::AmpersandToken, Kind::AmpersandToken,
	     "l & rl & rr"},
	    {Kind::Unknown, Kind::CaretToken, Kind::CaretToken, "l ^ rl ^ rr"},
	    {Kind::Unknown, Kind::AmpersandAmpersandToken, Kind::ArrowFunction,
	     "l && (() => { })"},
	};
	for (auto& rec : data) {
		t->Run(rec.output, [rec](T* t) {
			t->Parallel();

			tsc::NodeFactory f;
			auto* file = f.newSourceFile(
			    SourceFileParseOptions{.FileName = "/file.ts",
			                           .Path = "/file.ts"},
			    "",
			    f.newNodeList(std::vector<Node*>{
			        f.newExpressionStatement(
			            f.newBinaryExpression(nullptr, /*modifiers*/
			                                  makeSide("l", rec.left, &f),
			                                  nullptr, /*typeNode*/
			                                  f.newToken(rec.op),
			                                  makeSide("r", rec.right, &f)))}),
			    f.newToken(Kind::EndOfFile));

			parsetestutil::MarkSyntheticRecursive(file);
			emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(),
			                        std::string(rec.output) + ";");
		});
	}
}
REGISTER_UNIT_TEST("printer.TestParenthesizeBinary", TestParenthesizeBinary);

void TestParenthesizeConditional1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newConditionalExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					f.newToken(Kind::QuestionToken),
					f.newIdentifier("c"),
					f.newToken(Kind::ColonToken),
					f.newIdentifier("d")
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b) ? c : d;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditional1", TestParenthesizeConditional1);

void TestParenthesizeConditional2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newConditionalExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::EqualsToken),
						f.newIdentifier("b")
					),
					f.newToken(Kind::QuestionToken),
					f.newIdentifier("c"),
					f.newToken(Kind::ColonToken),
					f.newIdentifier("d")
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a = b) ? c : d;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditional2", TestParenthesizeConditional2);

void TestParenthesizeConditional3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newConditionalExpression(
					// will be parenthesized on emit:
					f.newArrowFunction(
						nullptr, /*modifiers*/
						nullptr, /*typeParameters*/
						f.newNodeList(std::vector<Node*>{}),
						nullptr, /*returnType*/
						nullptr, /*fullSignature*/
						f.newToken(Kind::EqualsGreaterThanToken),
						f.newBlock(
							f.newNodeList(std::vector<Node*>{}),
							false /*multiLine*/
						)
					),
					f.newToken(Kind::QuestionToken),
					f.newIdentifier("a"),
					f.newToken(Kind::ColonToken),
					f.newIdentifier("b")
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(() => { }) ? a : b;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditional3", TestParenthesizeConditional3);

void TestParenthesizeConditional4(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newConditionalExpression(
					// will be parenthesized on emit:
					f.newYieldExpression(nullptr, nullptr),
					f.newToken(Kind::QuestionToken),
					f.newIdentifier("a"),
					f.newToken(Kind::ColonToken),
					f.newIdentifier("b")
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(yield) ? a : b;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditional4", TestParenthesizeConditional4);

void TestParenthesizeConditional5(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newConditionalExpression(
					f.newIdentifier("a"),
					f.newToken(Kind::QuestionToken),
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("b"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("c")
					),
					f.newToken(Kind::ColonToken),
					f.newIdentifier("d")
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "a ? (b, c) : d;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditional5", TestParenthesizeConditional5);

void TestParenthesizeConditional6(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newConditionalExpression(
					f.newIdentifier("a"),
					f.newToken(Kind::QuestionToken),
					f.newIdentifier("b"),
					f.newToken(Kind::ColonToken),
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("c"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("d")
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "a ? b : (c, d);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditional6", TestParenthesizeConditional6);

void TestParenthesizeYield1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newYieldExpression(
					nullptr, /*asteriskToken*/
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "yield (a, b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeYield1", TestParenthesizeYield1);

void TestParenthesizeSpreadElement1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newArrayLiteralExpression(
					f.newNodeList(
						std::vector<Node*>{
							f.newSpreadElement(
								// will be parenthesized on emit:
								f.newBinaryExpression(
									nullptr, /*modifiers*/
									f.newIdentifier("a"),
									nullptr, /*typeNode*/
									f.newToken(Kind::CommaToken),
									f.newIdentifier("b")
								)
							),
						}
					),
					false /*multiLine*/
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "[...(a, b)];");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeSpreadElement1", TestParenthesizeSpreadElement1);

void TestParenthesizeSpreadElement2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newCallExpression(
					f.newIdentifier("a"),
					nullptr, /*questionDotToken*/
					nullptr, /*typeArguments*/
					f.newNodeList(
						std::vector<Node*>{
							f.newSpreadElement(
								// will be parenthesized on emit:
								f.newBinaryExpression(
									nullptr, /*modifiers*/
									f.newIdentifier("b"),
									nullptr, /*typeNode*/
									f.newToken(Kind::CommaToken),
									f.newIdentifier("c")
								)
							),
						}
					),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "a(...(b, c));");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeSpreadElement2", TestParenthesizeSpreadElement2);

void TestParenthesizeSpreadElement3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newNewExpression(
					f.newIdentifier("a"),
					nullptr, /*typeArguments*/
					f.newNodeList(
						std::vector<Node*>{
							f.newSpreadElement(
								// will be parenthesized on emit:
								f.newBinaryExpression(
									nullptr, /*modifiers*/
									f.newIdentifier("b"),
									nullptr, /*typeNode*/
									f.newToken(Kind::CommaToken),
									f.newIdentifier("c")
								)
							),
						}
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "new a(...(b, c));");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeSpreadElement3", TestParenthesizeSpreadElement3);

void TestParenthesizeExpressionWithTypeArguments(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newExpressionWithTypeArguments(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					f.newNodeList(
						std::vector<Node*>{
							f.newTypeReferenceNode(
								f.newIdentifier("c"),
								nullptr
							),
						}
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b)<c>;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeExpressionWithTypeArguments", TestParenthesizeExpressionWithTypeArguments);

void TestParenthesizeAsExpression(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newAsExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					f.newTypeReferenceNode(
						f.newIdentifier("c"),
						nullptr
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b) as c;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeAsExpression", TestParenthesizeAsExpression);

void TestParenthesizeSatisfiesExpression(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newSatisfiesExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					f.newTypeReferenceNode(
						f.newIdentifier("c"),
						nullptr
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b) satisfies c;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeSatisfiesExpression", TestParenthesizeSatisfiesExpression);

void TestParenthesizeNonNullExpression(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newNonNullExpression(
					// will be parenthesized on emit:
					f.newBinaryExpression(
						nullptr, /*modifiers*/
						f.newIdentifier("a"),
						nullptr, /*typeNode*/
						f.newToken(Kind::CommaToken),
						f.newIdentifier("b")
					),
					NodeFlagsNone
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(a, b)!;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeNonNullExpression", TestParenthesizeNonNullExpression);

void TestParenthesizeExpressionStatement1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newObjectLiteralExpression(
					f.newNodeList(
						std::vector<Node*>{}
					),
					false /*multiLine*/
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "({});");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeExpressionStatement1", TestParenthesizeExpressionStatement1);

void TestParenthesizeExpressionStatement2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newFunctionExpression(
					nullptr, /*modifiers*/
					nullptr, /*asteriskToken*/
					nullptr, /*name*/
					nullptr, /*typeParameters*/
					f.newNodeList(
						std::vector<Node*>{}
					),
					nullptr, /*returnType*/
					nullptr, /*fullSignature*/
					f.newBlock(
						f.newNodeList(std::vector<Node*>{}),
						false /*multiLine*/
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "(function () { });");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeExpressionStatement2", TestParenthesizeExpressionStatement2);

void TestParenthesizeExpressionStatement3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExpressionStatement(
				f.newClassExpression(
					nullptr, /*modifiers*/
					nullptr, /*name*/
					nullptr, /*typeParameters*/
					nullptr, /*heritageClauses*/
					f.newNodeList(
						std::vector<Node*>{}
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "class {\n};");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeExpressionStatement3", TestParenthesizeExpressionStatement3);

void TestParenthesizeExpressionDefault1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExportAssignment(
				nullptr,   /*modifiers*/
				false, /*isExportEquals*/
				nullptr,   /*typeNode*/
				// will be parenthesized on emit:
				f.newClassExpression(
					nullptr, /*modifiers*/
					nullptr, /*name*/
					nullptr, /*typeParameters*/
					nullptr, /*heritageClauses*/
					f.newNodeList(
						std::vector<Node*>{}
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "export default (class {\n});");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeExpressionDefault1", TestParenthesizeExpressionDefault1);

void TestParenthesizeExpressionDefault2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExportAssignment(
				nullptr,   /*modifiers*/
				false, /*isExportEquals*/
				nullptr,   /*typeNode*/
				// will be parenthesized on emit:
				f.newFunctionExpression(
					nullptr, /*modifiers*/
					nullptr, /*asteriskToken*/
					nullptr, /*name*/
					nullptr, /*typeParameters*/
					f.newNodeList(
						std::vector<Node*>{}
					),
					nullptr, /*returnType*/
					nullptr, /*fullSignature*/
					f.newBlock(
						f.newNodeList(
							std::vector<Node*>{}
						),
						false /*multiLine*/
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "export default (function () { });");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeExpressionDefault2", TestParenthesizeExpressionDefault2);

void TestParenthesizeExpressionDefault3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newExportAssignment(
				nullptr,   /*modifiers*/
				false, /*isExportEquals*/
				nullptr,   /*typeNode*/
				// will be parenthesized on emit:
				f.newBinaryExpression(
					nullptr, /*modifiers*/
					f.newIdentifier("a"),
					nullptr, /*typeNode*/
					f.newToken(Kind::CommaToken),
					f.newIdentifier("b")
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "export default (a, b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeExpressionDefault3", TestParenthesizeExpressionDefault3);

void TestParenthesizeArrayType(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newArrayTypeNode(
					// will be parenthesized on emit:
					f.newUnionTypeNode(
						f.newNodeList(
							std::vector<Node*>{
								f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
								f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
							}
						)
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = (a | b)[];");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeArrayType", TestParenthesizeArrayType);

void TestParenthesizeOptionalType(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newTupleTypeNode(
					f.newNodeList(
						std::vector<Node*>{
							f.newOptionalTypeNode(
								// will be parenthesized on emit:
								f.newUnionTypeNode(
									f.newNodeList(
										std::vector<Node*>{
											f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
											f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
										}
									)
								)
							),
						}
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = [\n    (a | b)?\n];");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeOptionalType", TestParenthesizeOptionalType);

void TestParenthesizeUnionType1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newUnionTypeNode(
					f.newNodeList(
						std::vector<Node*>{
							f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
							// will be parenthesized on emit:
							f.newFunctionTypeNode(
								nullptr, /*typeParameters*/
								f.newNodeList(
									std::vector<Node*>{}
								),
								f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/)
							),
						}
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = a | (() => b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeUnionType1", TestParenthesizeUnionType1);

void TestParenthesizeUnionType2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newUnionTypeNode(
					f.newNodeList(
						std::vector<Node*>{
							// will be parenthesized on emit:
							f.newInferTypeNode(
								f.newTypeParameterDeclaration(
									nullptr,
									f.newIdentifier("a"),
									f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
									nullptr, /*expression*/
									nullptr /*defaultType*/
								)
							),
							f.newTypeReferenceNode(f.newIdentifier("c"), nullptr /*typeArguments*/),
						}
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = (infer a extends b) | c;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeUnionType2", TestParenthesizeUnionType2);

void TestParenthesizeIntersectionType(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newIntersectionTypeNode(
					f.newNodeList(
						std::vector<Node*>{
							f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
							// will be parenthesized on emit:
							f.newUnionTypeNode(
								f.newNodeList(
									std::vector<Node*>{
										f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
										f.newTypeReferenceNode(f.newIdentifier("c"), nullptr /*typeArguments*/),
									}
								)
							),
						}
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = a & (b | c);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeIntersectionType", TestParenthesizeIntersectionType);

void TestParenthesizeReadonlyTypeOperator1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newTypeOperatorNode(
					Kind::ReadonlyKeyword,
					// will be parenthesized on emit:
					f.newUnionTypeNode(
						f.newNodeList(
							std::vector<Node*>{
								f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
								f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
							}
						)
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = readonly (a | b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeReadonlyTypeOperator1", TestParenthesizeReadonlyTypeOperator1);

void TestParenthesizeReadonlyTypeOperator2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newTypeOperatorNode(
					Kind::ReadonlyKeyword,
					// will be parenthesized on emit:
					f.newTypeOperatorNode(
						Kind::KeyOfKeyword,
						f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/)
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = readonly (keyof a);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeReadonlyTypeOperator2", TestParenthesizeReadonlyTypeOperator2);

void TestParenthesizeKeyofTypeOperator(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newTypeOperatorNode(
					Kind::KeyOfKeyword,
					// will be parenthesized on emit:
					f.newUnionTypeNode(
						f.newNodeList(
							std::vector<Node*>{
								f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
								f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
							}
						)
					)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = keyof (a | b);");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeKeyofTypeOperator", TestParenthesizeKeyofTypeOperator);

void TestParenthesizeIndexedAccessType(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newIndexedAccessTypeNode(
					// will be parenthesized on emit:
					f.newUnionTypeNode(
						f.newNodeList(
							std::vector<Node*>{
								f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
								f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
							}
						)
					),
					f.newTypeReferenceNode(f.newIdentifier("c"), nullptr /*typeArguments*/)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = (a | b)[c];");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeIndexedAccessType", TestParenthesizeIndexedAccessType);

void TestParenthesizeConditionalType1(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newConditionalTypeNode(
					// will be parenthesized on emit:
					f.newFunctionTypeNode(
						nullptr, /*typeParameters*/
						f.newNodeList(
							std::vector<Node*>{}
						),
						f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/)
					),
					f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
					f.newTypeReferenceNode(f.newIdentifier("c"), nullptr /*typeArguments*/),
					f.newTypeReferenceNode(f.newIdentifier("d"), nullptr /*typeArguments*/)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = (() => a) extends b ? c : d;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditionalType1", TestParenthesizeConditionalType1);

void TestParenthesizeConditionalType2(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newConditionalTypeNode(
					f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
					// will be parenthesized on emit:
					f.newConditionalTypeNode(
						f.newTypeReferenceNode(f.newIdentifier("b"), nullptr /*typeArguments*/),
						f.newTypeReferenceNode(f.newIdentifier("c"), nullptr /*typeArguments*/),
						f.newTypeReferenceNode(f.newIdentifier("d"), nullptr /*typeArguments*/),
						f.newTypeReferenceNode(f.newIdentifier("e"), nullptr /*typeArguments*/)
					),
					f.newTypeReferenceNode(f.newIdentifier("f"), nullptr /*typeArguments*/),
					f.newTypeReferenceNode(f.newIdentifier("g"), nullptr /*typeArguments*/)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = a extends (b extends c ? d : e) ? f : g;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditionalType2", TestParenthesizeConditionalType2);

void TestParenthesizeConditionalType3(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(
		std::vector<Node*>{
			f.newTypeAliasDeclaration(
				nullptr,                        /*modifiers*/
				f.newIdentifier("_"), /*name*/
				nullptr,                        /*typeParameters*/
				f.newConditionalTypeNode(
					f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
					f.newFunctionTypeNode(
						nullptr, /*typeParameters*/
						f.newNodeList(
							std::vector<Node*>{}
						),
						// will be parenthesized on emit:
						f.newInferTypeNode(
							f.newTypeParameterDeclaration(
								nullptr,
								f.newIdentifier("b"),
								f.newTypeReferenceNode(f.newIdentifier("c"), nullptr /*typeArguments*/),
								nullptr, /*expression*/
								nullptr /*defaultType*/
							)
						)
					),
					f.newTypeReferenceNode(f.newIdentifier("d"), nullptr /*typeArguments*/),
					f.newTypeReferenceNode(f.newIdentifier("e"), nullptr /*typeArguments*/)
				)
			),
		}
	), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = a extends () => (infer b extends c) ? d : e;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditionalType3", TestParenthesizeConditionalType3);

void TestParenthesizeConditionalType4(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* file = f.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", f.newNodeList(std::vector<Node*>{
		f.newTypeAliasDeclaration(
			nullptr,                        /*modifiers*/
			f.newIdentifier("_"), /*name*/
			nullptr,                        /*typeParameters*/
			f.newConditionalTypeNode(
				f.newTypeReferenceNode(f.newIdentifier("a"), nullptr /*typeArguments*/),
				f.newFunctionTypeNode(
					nullptr, /*typeParameters*/
					f.newNodeList(
						std::vector<Node*>{}
					),
					// will be parenthesized on emit:
					f.newUnionTypeNode(
						f.newNodeList(
							std::vector<Node*>{
								f.newInferTypeNode(
									f.newTypeParameterDeclaration(
										nullptr,
										f.newIdentifier("b"),
										f.newTypeReferenceNode(f.newIdentifier("c"), nullptr /*typeArguments*/),
										nullptr, /*expression*/
										nullptr /*defaultType*/
									)
								),
								f.newTypeReferenceNode(f.newIdentifier("d"), nullptr /*typeArguments*/),
							}
						)
					)
				),
				f.newTypeReferenceNode(f.newIdentifier("e"), nullptr /*typeArguments*/),
				f.newTypeReferenceNode(f.newIdentifier("f"), nullptr /*typeArguments*/)
			)
		),
	}), f.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(), "type _ = a extends () => (infer b extends c) | d ? e : f;");

}

REGISTER_UNIT_TEST("printer.TestParenthesizeConditionalType4", TestParenthesizeConditionalType4);

void TestNameGeneration(T* t) {

	t->Parallel();
	auto* ec = printer::NewEmitContext();
	auto* file = ec->factory.newSourceFile(SourceFileParseOptions{.FileName = "/file.ts", .Path = "/file.ts"}, "", ec->factory.newNodeList(std::vector<Node*>{
		ec->factory.newVariableStatement(nullptr, ec->factory.newVariableDeclarationList(
			ec->factory.newNodeList(std::vector<Node*>{
				ec->factory.newVariableDeclaration(ec->factory.newTempVariable(), nullptr, nullptr, nullptr),
			}),
			NodeFlagsNone
		)),
		ec->factory.newFunctionDeclaration(
			nullptr,
			nullptr,
			ec->factory.newIdentifier("f"),
			nullptr,
			ec->factory.newNodeList(std::vector<Node*>{}),
			nullptr,
			nullptr,
			ec->factory.newBlock(ec->factory.newNodeList(std::vector<Node*>{
				ec->factory.newVariableStatement(nullptr, ec->factory.newVariableDeclarationList(
					ec->factory.newNodeList(std::vector<Node*>{
						ec->factory.newVariableDeclaration(ec->factory.newTempVariable(), nullptr, nullptr, nullptr),
					}),
					NodeFlagsNone
				)),
			}), true)
		),
	}), ec->factory.newToken(Kind::EndOfFile));

	parsetestutil::MarkSyntheticRecursive(file);
	emittestutil::CheckEmit(t, ec, file->as<SourceFile>(), "var _a;\nfunction f() {\n    var _a;\n}");

}

REGISTER_UNIT_TEST("printer.TestNameGeneration", TestNameGeneration);

void TestNoTrailingCommaAfterTransform(T* t) {

	t->Parallel();

	auto* file = parsetestutil::ParseTypeScript("[a!]", false /*jsx*/);
	auto* emitContext = printer::NewEmitContext();

	tsc::NodeVisitor* visitor = nullptr;
	visitor = emitContext->newNodeVisitor([&visitor](Node* node) -> Node* {
		switch (node->kind) {
		case Kind::NonNullExpression:
			node = node->expression();
		default:
			node = node->visitEachChild(*visitor);
		}
		return node;
	});
	file = visitor->visitSourceFile(file);

	emittestutil::CheckEmit(t, emitContext, file->as<SourceFile>(), "[a];");

}

REGISTER_UNIT_TEST("printer.TestNoTrailingCommaAfterTransform", TestNoTrailingCommaAfterTransform);

void TestTrailingCommaAfterTransform(T* t) {

	t->Parallel();

	auto* file = parsetestutil::ParseTypeScript("[a!,]", false /*jsx*/);
	auto* emitContext = printer::NewEmitContext();

	tsc::NodeVisitor* visitor = nullptr;
	visitor = emitContext->newNodeVisitor([&visitor](Node* node) -> Node* {
		switch (node->kind) {
		case Kind::NonNullExpression:
			node = node->expression();
		default:
			node = node->visitEachChild(*visitor);
		}
		return node;
	});
	file = visitor->visitSourceFile(file);

	emittestutil::CheckEmit(t, emitContext, file->as<SourceFile>(), "[a,];");

}

REGISTER_UNIT_TEST("printer.TestTrailingCommaAfterTransform", TestTrailingCommaAfterTransform);

void TestPartiallyEmittedExpression(T* t) {

	t->Parallel();

	CompilerOptions compilerOptions{};

	auto* file = parsetestutil::ParseTypeScript(R"TS(return ((container.parent
    .left as PropertyAccessExpression)
    .expression as PropertyAccessExpression)
    .expression;)TS", false /*jsx*/);

	auto* emitContext = printer::NewEmitContext();
	transformers::TransformOptions opts{.Context = emitContext,
	                                    .CompilerOptions = &compilerOptions};
	file = transformers::tstransforms::NewTypeEraserTransformer(&opts)->transformSourceFile(file);
	emittestutil::CheckEmit(t, emitContext, file->as<SourceFile>(), R"TS(return container.parent
    .left
    .expression
    .expression;)TS");

}

REGISTER_UNIT_TEST("printer.TestPartiallyEmittedExpression", TestPartiallyEmittedExpression);

void TestParenthesizeBinaryExpressionMixingNullishCoalescing(T* t) {
	t->Parallel();

	struct Row {
		const char* title;
		Kind innerOp;
		Kind outerOp;
		const char* side;
		const char* output;
	};
	static const Row tests[] = {
	    // inner ?? on left side of || or &&
	    {"BarBarWithLeftQuestionQuestion", Kind::QuestionQuestionToken,
	     Kind::BarBarToken, "left", "(a ?? b) || c;"},
	    {"AmpersandAmpersandWithLeftQuestionQuestion",
	     Kind::QuestionQuestionToken, Kind::AmpersandAmpersandToken, "left",
	     "(a ?? b) && c;"},
	    // inner ?? on right side of || or &&
	    {"BarBarWithRightQuestionQuestion", Kind::QuestionQuestionToken,
	     Kind::BarBarToken, "right", "a || (b ?? c);"},
	    {"AmpersandAmpersandWithRightQuestionQuestion",
	     Kind::QuestionQuestionToken, Kind::AmpersandAmpersandToken, "right",
	     "a && (b ?? c);"},
	    // inner || or && on left side of ??
	    {"QuestionQuestionWithLeftBarBar", Kind::BarBarToken,
	     Kind::QuestionQuestionToken, "left", "(a || b) ?? c;"},
	    {"QuestionQuestionWithLeftAmpersandAmpersand",
	     Kind::AmpersandAmpersandToken, Kind::QuestionQuestionToken, "left",
	     "(a && b) ?? c;"},
	    // inner || or && on right side of ??
	    {"QuestionQuestionWithRightBarBar", Kind::BarBarToken,
	     Kind::QuestionQuestionToken, "right", "a ?? (b || c);"},
	    {"QuestionQuestionWithRightAmpersandAmpersand",
	     Kind::AmpersandAmpersandToken, Kind::QuestionQuestionToken, "right",
	     "a ?? (b && c);"},
	};

	for (auto& tt : tests) {
		t->Run(tt.title, [tt](T* t) {
			t->Parallel();
			tsc::NodeFactory f;
			auto* innerExpr = f.newBinaryExpression(
			    nullptr, /*modifiers*/
			    f.newIdentifier("a"),
			    nullptr, /*typeNode*/
			    f.newToken(tt.innerOp),
			    f.newIdentifier("b"));
			Node* outerExpr;
			if (std::string(tt.side) == "left") {
				outerExpr = f.newBinaryExpression(
				    nullptr,   /*modifiers*/
				    innerExpr, /*left: (a innerOp b)*/
				    nullptr,   /*typeNode*/
				    f.newToken(tt.outerOp),
				    f.newIdentifier("c"));
			} else {
				outerExpr = f.newBinaryExpression(
				    nullptr, /*modifiers*/
				    f.newIdentifier("a"),
				    nullptr, /*typeNode*/
				    f.newToken(tt.outerOp),
				    innerExpr /*right: (b innerOp c)*/);
				// adjust identifiers for right side
				innerExpr->as<BinaryExpression>()->Left =
				    f.newIdentifier("b");
				innerExpr->as<BinaryExpression>()->Right =
				    f.newIdentifier("c");
			}
			auto* file = f.newSourceFile(
			    SourceFileParseOptions{.FileName = "/file.ts",
			                           .Path = "/file.ts"},
			    "",
			    f.newNodeList(std::vector<Node*>{
			        f.newExpressionStatement(outerExpr)}),
			    f.newToken(Kind::EndOfFile));

			parsetestutil::MarkSyntheticRecursive(file);
			emittestutil::CheckEmit(t, nullptr, file->as<SourceFile>(),
			                        tt.output);
		});
	}
}

REGISTER_UNIT_TEST("printer.TestParenthesizeBinaryExpressionMixingNullishCoalescing", TestParenthesizeBinaryExpressionMixingNullishCoalescing);

void TestOmitTrailingSemicolon(T* t) {

	t->Parallel();

	tsc::NodeFactory f;
	auto* methodSignature = f.newMethodSignatureDeclaration(
		nullptr, /*modifiers*/
		f.newIdentifier("m"),
		nullptr, /*postfixToken*/
		nullptr, /*typeParameters*/
		f.newNodeList({}),
		f.newKeywordTypeNode(Kind::VoidKeyword)
	);
	auto* file = parsetestutil::ParseTypeScript("interface I {}", false);

	auto* defaultPrinter = printer::NewPrinter(printer::PrinterOptions{.NewLine = NewLineKind::LineFeed}, printer::PrintHandlers{}, nullptr);
	if (auto got = defaultPrinter->Emit(methodSignature, file); got != "m(): void;") {
		t->Fatalf("default Emit() = %q, want %q", {got, "m(): void;"});
	}

	auto* omitPrinter = printer::NewPrinter(printer::PrinterOptions{.NewLine = NewLineKind::LineFeed, .OmitTrailingSemicolon = true}, printer::PrintHandlers{}, nullptr);
	if (auto got = omitPrinter->Emit(methodSignature, file); got != "m(): void") {
		t->Fatalf("omit Emit() = %q, want %q", {got, "m(): void"});
	}

	auto* forFile = parsetestutil::ParseTypeScript("for (;;) {}", false);
	auto got = stringsTrimSuffix(omitPrinter->EmitSourceFile(forFile), "\n");
	if (got != "for (;;) { }") {
		t->Fatalf("omit EmitSourceFile(for) = %q, want %q", {got, "for (;;) { }"});
	}

}

REGISTER_UNIT_TEST("printer.TestOmitTrailingSemicolon", TestOmitTrailingSemicolon);

}  // namespace
