// Port of tsc/internal/lsp/lsproto/lsp_json_test.go (internal package test).
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/lsp/lsproto/lsproto_generated.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace gostd = tsc::gostd;
namespace json = tsc::json;
namespace lsproto = tsc::lsp::lsproto;
using tsc::gostd::testing::T;

// json.Unmarshal error strings — helpers matching assert.NilError /
// assert.ErrorContains over std::string.
static void nilErrorStr(T* t, const std::string& err) {
	assert::Assert(t, err.empty(), {"expected nil error, got: " + err});
}
static void errContainsStr(T* t, const std::string& err,
                         const std::string& sub) {
	assert::Assert(t, !err.empty() && err.find(sub) != std::string::npos,
	               {"expected error containing " + sub + ", got: " + err});
}
static void errNonNilStr(T* t, const std::string& err) {
	assert::Assert(t, !err.empty(), {"expected non-nil error"});
}

template <class Ty>
static void checkRejectsNull(T* t, const std::string& input,
                             const std::string& errText) {
	Ty target;
	errContainsStr(t, json::unmarshal(input, &target), errText);
}
template <class Ty>
static void checkAcceptsNull(T* t, const std::string& input) {
	Ty target;
	nilErrorStr(t, json::unmarshal(input, &target));
}
template <class Ty>
static void checkRejects(T* t, const std::string& input) {
	Ty target;
	errNonNilStr(t, json::unmarshal(input, &target));
}
// Round-trip marshal -> unmarshal -> marshal; the wire output must be
// identical (equivalent to Go's reflect.DeepEqual on the fully-marshaled
// struct fields).
template <class Ty>
static void roundTrip(T* t, const Ty& v) {
	auto [data, err] = json::marshal(v);
	nilErrorStr(t, err);
	Ty got{};
	nilErrorStr(t, json::unmarshal(data, &got));
	auto [again, err2] = json::marshal(got);
	nilErrorStr(t, err2);
	assert::Equal(t, again, data);
}

void TestUnmarshalRejectsNullForOptionalNonNullableFields(T* t) {
	t->Parallel();
	struct Case {
		std::string name;
		std::string input;
		int type = 0;
		std::string errText;
	};
	// type: 1=InlayHint 2=FoldingRange 3=CompletionItem 4=Hover
	//       5=WorkDoneProgressOptions 6=CallHierarchyIncomingCallsParams
	//       7=CallHierarchyIncomingCall 8=InitializeParams
	//       9=InitializeResult 10=SemanticTokens 11=TextDocumentEdit
	struct FullCase {
		std::string name;
		std::string input;
		std::string errText;
		int type;
	};
	std::vector<FullCase> tests{
	    {"InlayHint kind null",
	     R"JSON({"position": {"line": 0, "character": 0}, "label": "foo", "kind": null})JSON",
	     R"JSON(null value is not allowed for field "kind")JSON", 1},
	    {"InlayHint textEdits null",
	     R"JSON({"position": {"line": 0, "character": 0}, "label": "foo", "textEdits": null})JSON",
	     R"JSON(null value is not allowed for field "textEdits")JSON", 1},
	    {"InlayHint paddingLeft null",
	     R"JSON({"position": {"line": 0, "character": 0}, "label": "foo", "paddingLeft": null})JSON",
	     R"JSON(null value is not allowed for field "paddingLeft")JSON", 1},
	    {"FoldingRange kind null",
	     R"JSON({"startLine": 0, "endLine": 10, "kind": null})JSON",
	     R"JSON(null value is not allowed for field "kind")JSON", 2},
	    {"FoldingRange startCharacter null",
	     R"JSON({"startLine": 0, "endLine": 10, "startCharacter": null})JSON",
	     R"JSON(null value is not allowed for field "startCharacter")JSON", 2},
	    {"CompletionItem insertTextFormat null",
	     R"JSON({"label": "test", "insertTextFormat": null})JSON",
	     R"JSON(null value is not allowed for field "insertTextFormat")JSON", 3},
	    {"Hover range null",
	     R"JSON({"contents": {"kind": "plaintext", "value": "hi"}, "range": null})JSON",
	     R"JSON(null value is not allowed for field "range")JSON", 4},
	    {"WorkDoneProgressOptions workDoneProgress null",
	     R"JSON({"workDoneProgress": null})JSON",
	     R"JSON(null value is not allowed for field "workDoneProgress")JSON", 5},
	    {"CallHierarchyIncomingCallsParams item null", R"JSON({"item": null})JSON",
	     R"JSON(null value is not allowed for field "item")JSON", 6},
	    {"CallHierarchyIncomingCall from null",
	     R"JSON({"from": null, "fromRanges": []})JSON",
	     R"JSON(null value is not allowed for field "from")JSON", 7},
	    {"InitializeParams capabilities null",
	     R"JSON({"processId": null, "rootUri": null, "capabilities": null})JSON",
	     R"JSON(null value is not allowed for field "capabilities")JSON", 8},
	    {"InitializeResult capabilities null", R"JSON({"capabilities": null})JSON",
	     R"JSON(null value is not allowed for field "capabilities")JSON", 9},
	    {"SemanticTokens data null (required slice)", R"JSON({"data": null})JSON",
	     R"JSON(null value is not allowed for field "data")JSON", 10},
	    {"TextDocumentEdit edits null (required slice)",
	     R"JSON({"textDocument": {"uri": "file:///a.ts", "version": 1}, "edits": null})JSON",
	     R"JSON(null value is not allowed for field "edits")JSON", 11},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&](T* t) {
			t->Parallel();
			switch (tt.type) {
			case 1:
				checkRejectsNull<lsproto::InlayHint>(t, tt.input, tt.errText);
				break;
			case 2:
				checkRejectsNull<lsproto::FoldingRange>(t, tt.input,
				                                      tt.errText);
				break;
			case 3:
				checkRejectsNull<lsproto::CompletionItem>(t, tt.input,
				                                        tt.errText);
				break;
			case 4:
				checkRejectsNull<lsproto::Hover>(t, tt.input, tt.errText);
				break;
			case 5:
				checkRejectsNull<lsproto::WorkDoneProgressOptions>(
				    t, tt.input, tt.errText);
				break;
			case 6:
				checkRejectsNull<lsproto::CallHierarchyIncomingCallsParams>(
				    t, tt.input, tt.errText);
				break;
			case 7:
				checkRejectsNull<lsproto::CallHierarchyIncomingCall>(
				    t, tt.input, tt.errText);
				break;
			case 8:
				checkRejectsNull<lsproto::InitializeParams>(t, tt.input,
				                                          tt.errText);
				break;
			case 9:
				checkRejectsNull<lsproto::InitializeResult>(t, tt.input,
				                                          tt.errText);
				break;
			case 10:
				checkRejectsNull<lsproto::SemanticTokens>(t, tt.input,
				                                        tt.errText);
				break;
			case 11:
				checkRejectsNull<lsproto::TextDocumentEdit>(t, tt.input,
				                                          tt.errText);
				break;
			}
		});
	}
}
REGISTER_UNIT_TEST(
    "lsproto.TestUnmarshalRejectsNullForOptionalNonNullableFields",
    TestUnmarshalRejectsNullForOptionalNonNullableFields);

void TestUnmarshalAcceptsNullForNullableFields(T* t) {
	t->Parallel();
	struct Case {
		std::string name;
		std::string input;
		int type;
	};
	// type: 1=InitializeParams 2=InitializationOptions
	std::vector<Case> tests{
	    {"InitializeParams rootUri null",
	     R"JSON({"processId": null, "rootUri": null, "capabilities": {}})JSON", 1},
	    {"InitializeParams workspaceFolders null",
	     R"JSON({"processId": null, "rootUri": null, "capabilities": {}, "workspaceFolders": null})JSON",
	     1},
	    {"InitializeParams processId null",
	     R"JSON({"processId": null, "rootUri": null, "capabilities": {}})JSON", 1},
	    {"InitializationOptions userPreferences null",
	     R"JSON({"userPreferences": null})JSON", 2},
	    {"InitializeParams initializationOptions null",
	     R"JSON({"processId": null, "rootUri": null, "capabilities": {}, "initializationOptions": null})JSON",
	     1},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&](T* t) {
			t->Parallel();
			if (tt.type == 1) {
				checkAcceptsNull<lsproto::InitializeParams>(t, tt.input);
			} else {
				checkAcceptsNull<lsproto::InitializationOptions>(t,
				                                               tt.input);
			}
		});
	}
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalAcceptsNullForNullableFields",
                   TestUnmarshalAcceptsNullForNullableFields);

void TestUnmarshalAcceptsOmittedOptionalFields(T* t) {
	t->Parallel();

	t->Run("InlayHint with only required fields", [](T* t) {
		t->Parallel();
		lsproto::InlayHint hint;
		nilErrorStr(
		    t, json::unmarshal(
		           R"JSON({"position": {"line": 1, "character": 5}, "label": "test"})JSON",
		           &hint));
		assert::Assert(t, !hint.Kind);
		assert::Assert(t, !hint.TextEdits);
		assert::Assert(t, !hint.Tooltip);
		assert::Assert(t, !hint.PaddingLeft);
		assert::Assert(t, !hint.PaddingRight);
		assert::Assert(t, !hint.Data);
		assert::Equal(t, hint.Position.Line, (uint32_t)1);
		assert::Equal(t, hint.Position.Character, (uint32_t)5);
	});

	t->Run("FoldingRange with only required fields", [](T* t) {
		t->Parallel();
		lsproto::FoldingRange fr;
		nilErrorStr(
		    t, json::unmarshal(R"JSON({"startLine": 5, "endLine": 10})JSON", &fr));
		assert::Assert(t, fr.Kind == nullptr);
		assert::Assert(t, !fr.StartCharacter);
		assert::Assert(t, !fr.EndCharacter);
		assert::Assert(t, !fr.CollapsedText);
		assert::Equal(t, fr.StartLine, (uint32_t)5);
		assert::Equal(t, fr.EndLine, (uint32_t)10);
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalAcceptsOmittedOptionalFields",
                   TestUnmarshalAcceptsOmittedOptionalFields);

void TestUnmarshalRejectsIncompleteObjects(T* t) {
	t->Parallel();
	struct Case {
		std::string name;
		std::string input;
		int type;
		std::string errText;
	};
	// type: 1=InlayHint 2=Location
	std::vector<Case> tests{
	    {"InlayHint missing position", R"JSON({"label": "test"})JSON", 1,
	     "missing required properties: position"},
	    {"InlayHint missing label",
	     R"JSON({"position": {"line": 0, "character": 0}})JSON", 1,
	     "missing required properties: label"},
	    {"Location missing uri",
	     R"JSON({"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 0}}})JSON",
	     2, "missing required properties: uri"},
	    {"Location empty object", R"JSON({})JSON", 2,
	     "missing required properties: uri, range"},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&](T* t) {
			t->Parallel();
			if (tt.type == 1) {
				checkRejectsNull<lsproto::InlayHint>(t, tt.input,
				                                   tt.errText);
			} else {
				checkRejectsNull<lsproto::Location>(t, tt.input,
				                                  tt.errText);
			}
		});
	}
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalRejectsIncompleteObjects",
                   TestUnmarshalRejectsIncompleteObjects);

void TestMarshalUnmarshalRoundTrip(T* t) {
	t->Parallel();

	t->Run("InlayHint with kind", [](T* t) {
		t->Parallel();
		lsproto::InlayHint v;
		v.Position = lsproto::Position{1, 5};
		v.Label.String = std::make_shared<std::string>("param");
		v.Kind = std::make_shared<lsproto::InlayHintKind>(
		    lsproto::InlayHintKindParameter);
		roundTrip(t, v);
	});

	t->Run("InlayHint minimal", [](T* t) {
		t->Parallel();
		lsproto::InlayHint v;
		v.Position = lsproto::Position{0, 0};
		v.Label.String = std::make_shared<std::string>("x");
		roundTrip(t, v);
	});

	t->Run("FoldingRange with all fields", [](T* t) {
		t->Parallel();
		lsproto::FoldingRange v;
		v.StartLine = 1;
		v.StartCharacter = 0;
		v.EndLine = 10;
		v.EndCharacter = 5;
		v.Kind = std::make_shared<lsproto::FoldingRangeKind>(
		    lsproto::FoldingRangeKindRegion);
		v.CollapsedText = "...";
		roundTrip(t, v);
	});

	t->Run("Location", [](T* t) {
		t->Parallel();
		lsproto::Location v;
		v.Uri = "file:///test.ts";
		v.Range.Start = lsproto::Position{1, 2};
		v.Range.End = lsproto::Position{3, 4};
		roundTrip(t, v);
	});

	t->Run("InitializeParams with null processId", [](T* t) {
		t->Parallel();
		lsproto::InitializeParams v;
		v.ProcessId = lsproto::IntegerOrNull{};
		v.RootUri.DocumentUri =
		    std::make_shared<lsproto::DocumentUri>("file:///workspace");
		v.Capabilities = std::make_shared<lsproto::ClientCapabilities>();
		roundTrip(t, v);
	});
}
REGISTER_UNIT_TEST("lsproto.TestMarshalUnmarshalRoundTrip",
                   TestMarshalUnmarshalRoundTrip);

void TestUnmarshalUnionTypes(T* t) {
	t->Parallel();

	t->Run("IntegerOrString with integer", [](T* t) {
		t->Parallel();
		lsproto::IntegerOrString v;
		nilErrorStr(t, json::unmarshal(R"JSON(42)JSON", &v));
		assert::Assert(t, !!v.Integer);
		assert::Equal(t, *v.Integer, (int32_t)42);
		assert::Assert(t, !v.String);
	});

	t->Run("IntegerOrString with string", [](T* t) {
		t->Parallel();
		lsproto::IntegerOrString v;
		nilErrorStr(t, json::unmarshal(R"JSON("hello")JSON", &v));
		assert::Assert(t, !!v.String);
		assert::Equal(t, *v.String, std::string("hello"));
		assert::Assert(t, !v.Integer);
	});

	t->Run("IntegerOrNull with integer", [](T* t) {
		t->Parallel();
		lsproto::IntegerOrNull v;
		nilErrorStr(t, json::unmarshal(R"JSON(42)JSON", &v));
		assert::Assert(t, !!v.Integer);
		assert::Equal(t, *v.Integer, (int32_t)42);
	});

	t->Run("IntegerOrNull with null", [](T* t) {
		t->Parallel();
		lsproto::IntegerOrNull v;
		nilErrorStr(t, json::unmarshal(R"JSON(null)JSON", &v));
		assert::Assert(t, !v.Integer);
	});

	t->Run("DocumentUriOrNull with string", [](T* t) {
		t->Parallel();
		lsproto::DocumentUriOrNull v;
		nilErrorStr(t, json::unmarshal(R"JSON("file:///test.ts")JSON", &v));
		assert::Assert(t, !!v.DocumentUri);
		assert::Equal(t, *v.DocumentUri,
		              lsproto::DocumentUri("file:///test.ts"));
	});

	t->Run("DocumentUriOrNull with null", [](T* t) {
		t->Parallel();
		lsproto::DocumentUriOrNull v;
		nilErrorStr(t, json::unmarshal(R"JSON(null)JSON", &v));
		assert::Assert(t, !v.DocumentUri);
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalUnionTypes",
                   TestUnmarshalUnionTypes);

void TestMarshalUnionTypes(T* t) {
	t->Parallel();

	t->Run("IntegerOrNull with value", [](T* t) {
		t->Parallel();
		lsproto::IntegerOrNull v{
		    std::make_shared<int32_t>(42)};
		auto [data, err] = json::marshal(v);
		nilErrorStr(t, err);
		assert::Equal(t, data, std::string("42"));
	});

	t->Run("IntegerOrNull with null", [](T* t) {
		t->Parallel();
		lsproto::IntegerOrNull v{};
		auto [data, err] = json::marshal(v);
		nilErrorStr(t, err);
		assert::Equal(t, data, std::string("null"));
	});

	t->Run("IntegerOrString with integer", [](T* t) {
		t->Parallel();
		lsproto::IntegerOrString v;
		v.Integer = std::make_shared<int32_t>(7);
		auto [data, err] = json::marshal(v);
		nilErrorStr(t, err);
		assert::Equal(t, data, std::string("7"));
	});

	t->Run("IntegerOrString with string", [](T* t) {
		t->Parallel();
		lsproto::IntegerOrString v;
		v.String = std::make_shared<std::string>("tok");
		auto [data, err] = json::marshal(v);
		nilErrorStr(t, err);
		assert::Equal(t, data, std::string("\"tok\""));
	});
}
REGISTER_UNIT_TEST("lsproto.TestMarshalUnionTypes", TestMarshalUnionTypes);

void TestUnmarshalIgnoresUnknownFields(T* t) {
	t->Parallel();

	t->Run("Location with extra fields", [](T* t) {
		t->Parallel();
		lsproto::Location loc;
		nilErrorStr(t, json::unmarshal(R"JSON({
			"uri": "file:///test.ts",
			"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 5}},
			"someUnknownField": 42,
			"anotherUnknown": {"nested": true}
		})JSON",
		                               &loc));
		assert::Equal(t, loc.Uri,
		              lsproto::DocumentUri("file:///test.ts"));
	});

	t->Run("InlayHint with extra fields", [](T* t) {
		t->Parallel();
		lsproto::InlayHint hint;
		nilErrorStr(t, json::unmarshal(R"JSON({
			"position": {"line": 0, "character": 0},
			"label": "x",
			"futureField": [1, 2, 3]
		})JSON",
		                               &hint));
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalIgnoresUnknownFields",
                   TestUnmarshalIgnoresUnknownFields);

void TestUnmarshalRejectsWrongTypes(T* t) {
	t->Parallel();
	struct Case {
		std::string name;
		std::string input;
		int type;
	};
	// type: 1=Location 2=FoldingRange
	std::vector<Case> tests{
	    {"Location receives array", R"JSON([])JSON", 1},
	    {"Location receives string", R"JSON("not an object")JSON", 1},
	    {"Location receives number", R"JSON(42)JSON", 1},
	    {"Location receives null", R"JSON(null)JSON", 1},
	    {"FoldingRange receives boolean", R"JSON(true)JSON", 2},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&](T* t) {
			t->Parallel();
			if (tt.type == 1) {
				checkRejects<lsproto::Location>(t, tt.input);
			} else {
				checkRejects<lsproto::FoldingRange>(t, tt.input);
			}
		});
	}
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalRejectsWrongTypes",
                   TestUnmarshalRejectsWrongTypes);

void TestUnmarshalUnionTypeWrongKind(T* t) {
	t->Parallel();

	t->Run("IntegerOrString rejects boolean", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::IntegerOrString>(t, R"JSON(true)JSON");
	});

	t->Run("IntegerOrString rejects null", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::IntegerOrString>(t, R"JSON(null)JSON");
	});

	t->Run("IntegerOrString rejects object", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::IntegerOrString>(t, R"JSON({})JSON");
	});

	t->Run("IntegerOrString rejects array", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::IntegerOrString>(t, R"JSON([])JSON");
	});

	t->Run("StringOrInlayHintLabelParts rejects number", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::StringOrInlayHintLabelParts>(t, R"JSON(42)JSON");
	});

	t->Run("StringOrInlayHintLabelParts rejects boolean", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::StringOrInlayHintLabelParts>(t, R"JSON(true)JSON");
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalUnionTypeWrongKind",
                   TestUnmarshalUnionTypeWrongKind);

void TestUnmarshalBooleanUnionTypes(T* t) {
	t->Parallel();

	t->Run("BooleanOrHoverOptions with true", [](T* t) {
		t->Parallel();
		lsproto::BooleanOrHoverOptions v;
		nilErrorStr(t, json::unmarshal(R"JSON(true)JSON", &v));
		assert::Assert(t, !!v.Boolean);
		assert::Equal(t, *v.Boolean, true);
		assert::Assert(t, !v.HoverOptions);
	});

	t->Run("BooleanOrHoverOptions with false", [](T* t) {
		t->Parallel();
		lsproto::BooleanOrHoverOptions v;
		nilErrorStr(t, json::unmarshal(R"JSON(false)JSON", &v));
		assert::Assert(t, !!v.Boolean);
		assert::Equal(t, *v.Boolean, false);
		assert::Assert(t, !v.HoverOptions);
	});

	t->Run("BooleanOrHoverOptions with object", [](T* t) {
		t->Parallel();
		lsproto::BooleanOrHoverOptions v;
		nilErrorStr(t, json::unmarshal(R"JSON({})JSON", &v));
		assert::Assert(t, !v.Boolean);
		assert::Assert(t, !!v.HoverOptions);
	});

	t->Run("BooleanOrHoverOptions rejects string", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::BooleanOrHoverOptions>(t, R"JSON("nope")JSON");
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalBooleanUnionTypes",
                   TestUnmarshalBooleanUnionTypes);

// optionalDiscriminatorArm — test-local struct with an optional (omitzero)
// discriminator arm, like the Go test's local type.
struct optionalDiscriminatorArm {
	std::shared_ptr<lsproto::StringLiteralBegin> Kind; // R"JSON(kind,omitzero)JSON"
	std::string Title;                                 // R"JSON(title)JSON" required
	std::vector<lsproto::StructFieldBinding> fieldBindings() {
		return {
		    {"kind", &Kind,
		     &lsproto::fieldDecode<
		         std::shared_ptr<lsproto::StringLiteralBegin>>,
		     -1, false},
		    {"title", &Title, &lsproto::fieldDecode<std::string>, 0,
		     false},
		};
	}
};

void TestUnmarshalDiscriminatorUnion(T* t) {
	t->Parallel();

	t->Run("WorkDoneProgressBegin", [](T* t) {
		t->Parallel();
		lsproto::WorkDoneProgressBeginOrReportOrEnd v;
		nilErrorStr(t, json::unmarshal(
		                   R"JSON({"kind": "begin", "title": "Indexing"})JSON", &v));
		assert::Assert(t, !!v.Begin);
		assert::Assert(t, !v.Report);
		assert::Assert(t, !v.End);
		assert::Equal(t, v.Begin->Title, std::string("Indexing"));
	});

	t->Run("WorkDoneProgressReport", [](T* t) {
		t->Parallel();
		lsproto::WorkDoneProgressBeginOrReportOrEnd v;
		nilErrorStr(t, json::unmarshal(
		                   R"JSON({"kind": "report", "message": "50%"})JSON", &v));
		assert::Assert(t, !v.Begin);
		assert::Assert(t, !!v.Report);
		assert::Assert(t, !v.End);
		assert::Assert(t, v.Report->Message.has_value());
		assert::Equal(t, *v.Report->Message, std::string("50%"));
	});

	t->Run("WorkDoneProgressEnd", [](T* t) {
		t->Parallel();
		lsproto::WorkDoneProgressBeginOrReportOrEnd v;
		nilErrorStr(t, json::unmarshal(R"JSON({"kind": "end"})JSON", &v));
		assert::Assert(t, !v.Begin);
		assert::Assert(t, !v.Report);
		assert::Assert(t, !!v.End);
	});

	t->Run("discriminator after variant fields", [](T* t) {
		t->Parallel();
		lsproto::WorkDoneProgressBeginOrReportOrEnd v;
		nilErrorStr(t, json::unmarshal(
		                   R"JSON({"title": "Indexing", "percentage": 25, "kind": "begin"})JSON",
		                   &v));
		assert::Assert(t, !!v.Begin);
		assert::Equal(t, v.Begin->Title, std::string("Indexing"));
		assert::Assert(t, v.Begin->Percentage.has_value());
		assert::Equal(t, *v.Begin->Percentage, (uint32_t)25);
	});

	t->Run("optional discriminator is preserved", [](T* t) {
		t->Parallel();
		json::Decoder dec(R"JSON({"kind": "begin", "title": "Indexing"})JSON");
		auto [state, err] = lsproto::scanDiscriminatedStruct(
		    dec, "optionalDiscriminatorArm", "kind");
		nilErrorStr(t, err);
		std::shared_ptr<optionalDiscriminatorArm> v;
		nilErrorStr(t, lsproto::unmarshalDiscriminatedArm(state, &v));
		assert::Assert(t, !!v);
		assert::Assert(t, v->Kind != nullptr);
		assert::Equal(t, v->Title, std::string("Indexing"));
	});

	t->Run("invalid discriminator", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::WorkDoneProgressBeginOrReportOrEnd>(
		    t, R"JSON({"kind": "invalid"})JSON");
	});

	t->Run("non-string discriminator", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::WorkDoneProgressBeginOrReportOrEnd>(
		    t, R"JSON({"kind": null})JSON");
	});

	t->Run("missing discriminator", [](T* t) {
		t->Parallel();
		lsproto::WorkDoneProgressBeginOrReportOrEnd v;
		errContainsStr(
		    t, json::unmarshal(R"JSON({"message": "missing kind"})JSON", &v),
		    R"JSON(missing discriminator "kind")JSON");
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalDiscriminatorUnion",
                   TestUnmarshalDiscriminatorUnion);

void TestUnmarshalPresenceDiscriminatorUnion(T* t) {
	t->Parallel();

	t->Run("TextEdit via range field", [](T* t) {
		t->Parallel();
		lsproto::TextEditOrInsertReplaceEdit v;
		nilErrorStr(t, json::unmarshal(R"JSON({
			"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 1}},
			"newText": "x"
		})JSON",
		                               &v));
		assert::Assert(t, !!v.TextEdit);
		assert::Assert(t, !v.InsertReplaceEdit);
		assert::Equal(t, v.TextEdit->NewText, std::string("x"));
	});

	t->Run("InsertReplaceEdit via insert field", [](T* t) {
		t->Parallel();
		lsproto::TextEditOrInsertReplaceEdit v;
		nilErrorStr(t, json::unmarshal(R"JSON({
			"insert": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 1}},
			"replace": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 2}},
			"newText": "y"
		})JSON",
		                               &v));
		assert::Assert(t, !v.TextEdit);
		assert::Assert(t, !!v.InsertReplaceEdit);
		assert::Equal(t, v.InsertReplaceEdit->NewText, std::string("y"));
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalPresenceDiscriminatorUnion",
                   TestUnmarshalPresenceDiscriminatorUnion);

void TestUnmarshalStringOrArrayUnion(T* t) {
	t->Parallel();

	t->Run("StringOrInlayHintLabelParts with string", [](T* t) {
		t->Parallel();
		lsproto::StringOrInlayHintLabelParts v;
		nilErrorStr(t, json::unmarshal(R"JSON("hello")JSON", &v));
		assert::Assert(t, !!v.String);
		assert::Equal(t, *v.String, std::string("hello"));
		assert::Assert(t, !v.InlayHintLabelParts);
	});

	t->Run("StringOrInlayHintLabelParts with array", [](T* t) {
		t->Parallel();
		lsproto::StringOrInlayHintLabelParts v;
		nilErrorStr(
		    t, json::unmarshal(
		           R"JSON([{"value": "param"}, {"value": ": "}, {"value": "string"}])JSON",
		           &v));
		assert::Assert(t, !v.String);
		assert::Assert(t, v.InlayHintLabelParts != nullptr &&
		                      v.InlayHintLabelParts->has_value());
		assert::Equal(t, v.InlayHintLabelParts->value().size(), 3u);
		assert::Equal(t, v.InlayHintLabelParts->value()[0]->Value,
		              std::string("param"));
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalStringOrArrayUnion",
                   TestUnmarshalStringOrArrayUnion);

void TestUnmarshalDocumentEditUnion(T* t) {
	t->Parallel();

	t->Run("TextDocumentEdit without kind", [](T* t) {
		t->Parallel();
		lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile v;
		nilErrorStr(t, json::unmarshal(R"JSON({
			"textDocument": {"uri": "file:///a.ts", "version": 1},
			"edits": [{"range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 0}}, "newText": "x"}]
		})JSON",
		                               &v));
		assert::Assert(t, !!v.TextDocumentEdit);
		assert::Assert(t, !v.CreateFile);
		assert::Assert(t, !v.RenameFile);
		assert::Assert(t, !v.DeleteFile);
	});

	t->Run("TextDocumentEdit with non-string kind", [](T* t) {
		t->Parallel();
		lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile v;
		nilErrorStr(t, json::unmarshal(R"JSON({
			"kind": null,
			"textDocument": {"uri": "file:///a.ts", "version": 1},
			"edits": []
		})JSON",
		                               &v));
		assert::Assert(t, !!v.TextDocumentEdit);
		assert::Assert(t, !v.CreateFile);
		assert::Assert(t, !v.RenameFile);
		assert::Assert(t, !v.DeleteFile);
	});

	t->Run("CreateFile with kind create", [](T* t) {
		t->Parallel();
		lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile v;
		nilErrorStr(t, json::unmarshal(
		                   R"JSON({"kind": "create", "uri": "file:///new.ts"})JSON",
		                   &v));
		assert::Assert(t, !v.TextDocumentEdit);
		assert::Assert(t, !!v.CreateFile);
		assert::Equal(t, v.CreateFile->Uri,
		              lsproto::DocumentUri("file:///new.ts"));
	});

	t->Run("CreateFile with kind after fields", [](T* t) {
		t->Parallel();
		lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile v;
		nilErrorStr(t, json::unmarshal(
		                   R"JSON({"uri": "file:///new.ts", "kind": "create"})JSON",
		                   &v));
		assert::Assert(t, !!v.CreateFile);
		assert::Equal(t, v.CreateFile->Uri,
		              lsproto::DocumentUri("file:///new.ts"));
	});

	t->Run("RenameFile with kind rename", [](T* t) {
		t->Parallel();
		lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile v;
		nilErrorStr(t, json::unmarshal(
		                   R"JSON({"kind": "rename", "oldUri": "file:///old.ts", "newUri": "file:///new.ts"})JSON",
		                   &v));
		assert::Assert(t, !!v.RenameFile);
		assert::Equal(t, v.RenameFile->OldUri,
		              lsproto::DocumentUri("file:///old.ts"));
	});

	t->Run("DeleteFile with kind delete", [](T* t) {
		t->Parallel();
		lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile v;
		nilErrorStr(t, json::unmarshal(
		                   R"JSON({"kind": "delete", "uri": "file:///gone.ts"})JSON",
		                   &v));
		assert::Assert(t, !!v.DeleteFile);
		assert::Equal(t, v.DeleteFile->Uri,
		              lsproto::DocumentUri("file:///gone.ts"));
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalDocumentEditUnion",
                   TestUnmarshalDocumentEditUnion);

void TestUnmarshalFieldOrdering(T* t) {
	t->Parallel();

	t->Run("Location with reversed field order", [](T* t) {
		t->Parallel();
		lsproto::Location loc;
		nilErrorStr(t, json::unmarshal(R"JSON({
			"range": {"start": {"line": 1, "character": 2}, "end": {"line": 3, "character": 4}},
			"uri": "file:///test.ts"
		})JSON",
		                               &loc));
		assert::Equal(t, loc.Uri,
		              lsproto::DocumentUri("file:///test.ts"));
		assert::Equal(t, loc.Range.Start.Line, (uint32_t)1);
	});

	t->Run("InlayHint with kind before label", [](T* t) {
		t->Parallel();
		lsproto::InlayHint hint;
		nilErrorStr(t, json::unmarshal(R"JSON({
			"kind": 1,
			"label": "x",
			"position": {"line": 0, "character": 0}
		})JSON",
		                               &hint));
		assert::Assert(t, !!hint.Kind);
		assert::Equal(t, *hint.Kind, lsproto::InlayHintKindType);
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalFieldOrdering",
                   TestUnmarshalFieldOrdering);

void TestUnmarshalEmptyObject(T* t) {
	t->Parallel();

	t->Run("WorkDoneProgressOptions empty", [](T* t) {
		t->Parallel();
		lsproto::WorkDoneProgressOptions v;
		nilErrorStr(t, json::unmarshal(R"JSON({})JSON", &v));
		assert::Assert(t, !v.WorkDoneProgress);
	});

	t->Run("InitializationOptions empty", [](T* t) {
		t->Parallel();
		lsproto::InitializationOptions v;
		nilErrorStr(t, json::unmarshal(R"JSON({})JSON", &v));
	});

	t->Run("ClientCapabilities empty", [](T* t) {
		t->Parallel();
		lsproto::ClientCapabilities v;
		nilErrorStr(t, json::unmarshal(R"JSON({})JSON", &v));
	});

	t->Run("ServerCapabilities empty", [](T* t) {
		t->Parallel();
		lsproto::ServerCapabilities v;
		nilErrorStr(t, json::unmarshal(R"JSON({})JSON", &v));
	});
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalEmptyObject",
                   TestUnmarshalEmptyObject);

void TestMarshalOmitsZeroOptionalFields(T* t) {
	t->Parallel();

	t->Run("InlayHint omits nil fields", [](T* t) {
		t->Parallel();
		lsproto::InlayHint hint;
		hint.Position = lsproto::Position{0, 0};
		hint.Label.String = std::make_shared<std::string>("x");
		auto [data, err] = json::marshal(hint);
		nilErrorStr(t, err);
		auto& s = data;
		assert::Assert(t, s.find("kind") == std::string::npos,
		               {"should not contain 'kind', got: " + s});
		assert::Assert(t, s.find("textEdits") == std::string::npos,
		               {"should not contain 'textEdits', got: " + s});
		assert::Assert(t, s.find("paddingLeft") == std::string::npos,
		               {"should not contain 'paddingLeft', got: " + s});
		assert::Assert(t, s.find("position") != std::string::npos,
		               {"should contain 'position', got: " + s});
		assert::Assert(t, s.find("label") != std::string::npos,
		               {"should contain 'label', got: " + s});
	});

	t->Run("FoldingRange omits nil optional fields", [](T* t) {
		t->Parallel();
		lsproto::FoldingRange fr;
		fr.StartLine = 1;
		fr.EndLine = 10;
		auto [data, err] = json::marshal(fr);
		nilErrorStr(t, err);
		auto& s = data;
		assert::Assert(t, s.find("kind") == std::string::npos,
		               {"should not contain 'kind', got: " + s});
		assert::Assert(t, s.find("startCharacter") == std::string::npos,
		               {"should not contain 'startCharacter', got: " + s});
		assert::Assert(t, s.find("startLine") != std::string::npos,
		               {"should contain 'startLine', got: " + s});
		assert::Assert(t, s.find("endLine") != std::string::npos,
		               {"should contain 'endLine', got: " + s});
	});
}
REGISTER_UNIT_TEST("lsproto.TestMarshalOmitsZeroOptionalFields",
                   TestMarshalOmitsZeroOptionalFields);

void TestLiteralTypes(T* t) {
	t->Parallel();

	t->Run("StringLiteralCreate marshal", [](T* t) {
		t->Parallel();
		lsproto::StringLiteralCreate v;
		auto [data, err] = json::marshal(v);
		nilErrorStr(t, err);
		assert::Equal(t, data, std::string("\"create\""));
	});

	t->Run("StringLiteralCreate unmarshal", [](T* t) {
		t->Parallel();
		lsproto::StringLiteralCreate v;
		nilErrorStr(t, json::unmarshal(R"JSON("create")JSON", &v));
	});

	t->Run("StringLiteralCreate rejects wrong value", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::StringLiteralCreate>(t, R"JSON("delete")JSON");
	});

	t->Run("StringLiteralCreate rejects wrong type", [](T* t) {
		t->Parallel();
		checkRejects<lsproto::StringLiteralCreate>(t, R"JSON(42)JSON");
	});
}
REGISTER_UNIT_TEST("lsproto.TestLiteralTypes", TestLiteralTypes);

void TestEnumStringValues(T* t) {
	t->Parallel();

	t->Run("InlayHintKind values", [](T* t) {
		t->Parallel();
		assert::Equal(
		    t, lsproto::String(lsproto::InlayHintKindType),
		    std::string("Type"));
		assert::Equal(
		    t, lsproto::String(lsproto::InlayHintKindParameter),
		    std::string("Parameter"));
	});

	t->Run("SymbolKind values", [](T* t) {
		t->Parallel();
		assert::Equal(t, lsproto::String(lsproto::SymbolKindFile),
		              std::string("File"));
		assert::Equal(t, lsproto::String(lsproto::SymbolKindFunction),
		              std::string("Function"));
		assert::Equal(t, lsproto::String(lsproto::SymbolKindVariable),
		              std::string("Variable"));
	});

	t->Run("unknown enum value", [](T* t) {
		t->Parallel();
		auto v = lsproto::InlayHintKind(999);
		auto s = lsproto::String(v);
		assert::Assert(t, s.find("999") != std::string::npos,
		               {"should contain the numeric value, got: " + s});
	});
}
REGISTER_UNIT_TEST("lsproto.TestEnumStringValues", TestEnumStringValues);

// TestRoundTrip locks the generated codecs: every value must survive
// marshal -> unmarshal unchanged. Covers required fields,
// nullable/non-nullable optionals, enums, slices, nested objects, unions.
void TestRoundTrip(T* t) {
	t->Parallel();

	t->Run("Range", [](T* t) {
		t->Parallel();
		lsproto::Range v{lsproto::Position{1, 2},
		                 lsproto::Position{3, 4}};
		roundTrip(t, v);
	});
	t->Run("TextEdit", [](T* t) {
		t->Parallel();
		lsproto::TextEdit v;
		v.Range = lsproto::Range{lsproto::Position{1, 2},
		                         lsproto::Position{3, 4}};
		v.NewText = "hello";
		roundTrip(t, v);
	});
	t->Run("MarkupContent", [](T* t) {
		t->Parallel();
		lsproto::MarkupContent v{lsproto::MarkupKindMarkdown, "**x**"};
		roundTrip(t, v);
	});
	t->Run("DidChangeConfigurationParams object", [](T* t) {
		t->Parallel();
		lsproto::DidChangeConfigurationParams v;
		v.Settings = lsproto::LSPAny(std::map<std::string, lsproto::LSPAny>{
		    {"js/ts",
		     lsproto::LSPAny(std::map<std::string, lsproto::LSPAny>{
		         {"x", lsproto::LSPAny(1.0)}})}});
		roundTrip(t, v);
	});
	t->Run("DidChangeConfigurationParams null", [](T* t) {
		t->Parallel();
		lsproto::DidChangeConfigurationParams v;
		roundTrip(t, v);
	});
	t->Run("CompletionItem", [](T* t) {
		t->Parallel();
		lsproto::CompletionItem v;
		v.Label = "pageXOffset";
		v.Kind = 
		    std::make_shared<lsproto::CompletionItemKind>(
		        lsproto::CompletionItemKindField);
		v.SortText = std::string("15");
		v.InsertTextFormat =
		    std::make_shared<lsproto::InsertTextFormat>(
		        lsproto::InsertTextFormatPlainText);
		roundTrip(t, v);
	});
	// StringOrTuple union (string arm and tuple arm).
	t->Run("ParameterInformation string label", [](T* t) {
		t->Parallel();
		lsproto::ParameterInformation v;
		v.Label.String = std::make_shared<std::string>("p: number");
		roundTrip(t, v);
	});
	t->Run("ParameterInformation tuple label", [](T* t) {
		t->Parallel();
		lsproto::ParameterInformation v;
		v.Label.Tuple =
		    std::make_shared<std::array<uint32_t, 2>>(
		        std::array<uint32_t, 2>{0, 4});
		roundTrip(t, v);
	});
}
REGISTER_UNIT_TEST("lsproto.TestRoundTrip", TestRoundTrip);

// TestStrictnessMissingRequired confirms required fields are still enforced;
// default reflective decoding would silently accept these.
void TestStrictnessMissingRequired(T* t) {
	t->Parallel();
	struct Case {
		std::string name;
		std::string input;
		int type;
	};
	// type: 1=TextEdit 2=Range 3=Position
	std::vector<Case> tests{
	    {"TextEdit missing newText",
	     R"JSON({"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}}})JSON",
	     1},
	    {"Range missing end", R"JSON({"start":{"line":0,"character":0}})JSON", 2},
	    {"Position missing character", R"JSON({"line":0})JSON", 3},
	};
	for (auto& tt : tests) {
		t->Run(tt.name, [&](T* t) {
			t->Parallel();
			if (tt.type == 1) {
				checkRejectsNull<lsproto::TextEdit>(
				    t, tt.input, "missing required properties");
			} else if (tt.type == 2) {
				checkRejectsNull<lsproto::Range>(
				    t, tt.input, "missing required properties");
			} else {
				checkRejectsNull<lsproto::Position>(
				    t, tt.input, "missing required properties");
			}
		});
	}
}
REGISTER_UNIT_TEST("lsproto.TestStrictnessMissingRequired",
                   TestStrictnessMissingRequired);

// TestStrictnessNotObject confirms a non-object where an object is required
// is rejected rather than coerced.
void TestStrictnessNotObject(T* t) {
	t->Parallel();
	lsproto::TextEdit v;
	auto err = json::unmarshal(R"JSON("oops")JSON", &v);
	assert::Assert(t, !err.empty());
	assert::Assert(
	    t,
	    err.find("object") != std::string::npos ||
	        err.find("cannot unmarshal") != std::string::npos,
	    {"unexpected error text: " + err});
}
REGISTER_UNIT_TEST("lsproto.TestStrictnessNotObject",
                   TestStrictnessNotObject);

// TestUnmarshalParamsRequiresParams verifies that a NoParams method must be
// given no params while every other method must be given params, and that a
// mismatch (including a null value either way) is an InvalidParams error.
void TestUnmarshalParamsRequiresParams(T* t) {
	t->Parallel();

	// NoParams: only truly-absent/empty params are accepted; null and any
	// present value are rejected.
	struct Case {
		std::string name;
		lsproto::AnyValue params;
		bool wantErr;
	};
	std::vector<Case> noParamsTests{
	    {"absent", lsproto::AnyValue{}, false},
	    {"empty", lsproto::AnyValue::ofRaw(""), false},
	    {"null", lsproto::AnyValue::ofRaw("null"), true},
	    {"object", lsproto::AnyValue::ofRaw("{}"), true},
	};
	for (auto& tt : noParamsTests) {
		t->Run("NoParams/" + tt.name, [&](T* t) {
			t->Parallel();
			lsproto::RequestMessage req;
			req.Params = tt.params;
			auto [params, err] = req.UnmarshalParams<lsproto::NoParams>();
			if (tt.wantErr) {
				assert::ErrorIs(
				    t, err,
				    lsproto::errorCodeErr(
				        lsproto::ErrorCode::InvalidParams));
			} else {
				assert::NilError(t, err);
			}
		});
	}

	// Required-params method: only an object or array is accepted; absent,
	// empty, null, and other scalars are rejected.
	std::vector<Case> typedTests{
	    {"absent", lsproto::AnyValue{}, true},
	    {"empty", lsproto::AnyValue::ofRaw(""), true},
	    {"null", lsproto::AnyValue::ofRaw("null"), true},
	    {"number", lsproto::AnyValue::ofRaw("5"), true},
	    {"string", lsproto::AnyValue::ofRaw("\"x\""), true},
	    {"object",
	     lsproto::AnyValue::ofRaw(R"JSON({"settings":{"x":1}})JSON"), false},
	};
	for (auto& tt : typedTests) {
		t->Run("typed/" + tt.name, [&](T* t) {
			t->Parallel();
			lsproto::RequestMessage req;
			req.Params = tt.params;
			auto [got, err] =
			    req.UnmarshalParams<
			        std::shared_ptr<lsproto::DidChangeConfigurationParams>>();
			if (tt.wantErr) {
				assert::ErrorIs(
				    t, err,
				    lsproto::errorCodeErr(
				        lsproto::ErrorCode::InvalidParams));
			} else {
				assert::NilError(t, err);
				assert::Assert(
				    t, got != nullptr &&
				           got->Settings.kind ==
				               lsproto::LSPAny::K::Object);
			}
		});
	}
}
REGISTER_UNIT_TEST("lsproto.TestUnmarshalParamsRequiresParams",
                   TestUnmarshalParamsRequiresParams);

}  // namespace
