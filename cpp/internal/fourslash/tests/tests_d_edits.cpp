// PORTED FROM Go fourslash tests (batch D, fsgen)
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
#include "internal/testutil/stringtestutil/stringtestutil.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/json/json.h"
#include "internal/modulespecifiers/types.h"

namespace {

using namespace tsc;
namespace tsu = tsc::fourslash::tests::util;

static std::vector<fourslash::MarkerOrRangeOrName> asMonVec(
	const std::vector<std::any> &v) {
	std::vector<fourslash::MarkerOrRangeOrName> out;
	out.reserve(v.size());
	for (auto &e : v) {
		if (auto *p = std::any_cast<std::shared_ptr<fourslash::Marker>>(&e))
			out.push_back(*p);
		else if (auto *p =
					 std::any_cast<std::shared_ptr<fourslash::RangeMarker>>(&e))
			out.push_back(*p);
		else if (auto *p = std::any_cast<std::string>(&e)) out.push_back(*p);
	}
	return out;
}

static std::string itoaSmall(int32_t n) {
	if (n == 0) {
		return "0";
	}
	std::vector<uint8_t> b;
	while (n > 0) {
		b = gostr::slicesConcat({std::vector<uint8_t>{uint8_t('0' + n % 10)}, b});
		n /= 10;
	}
	return std::string(b.begin(), b.end());
}


static void TestGetEditsForFileRename_duplicateUnresolvedImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto numFiles = 60;
		auto numImports = 60;
		gostr::Builder content;
		content.WriteString(R"TS(// @Filename: /tsconfig.json
{ "compilerOptions": { "allowJs": true } }
)TS");
		for (int i = 0; i < numFiles; i++) {
			auto fi = itoaSmall(i);
			content.WriteString("// @Filename: /src/file" + fi + R"TS(.ts
export const v)TS" + fi + " = " + fi + R"TS(;
)TS");
		}
		content.WriteString(R"TS(// @Filename: /pkg/ugly.ts
)TS");
		for (int __i = 0; __i < numImports; __i++) {
			content.WriteString(R"TS(import "@some/unresolved-module";
)TS");
		}
		content.WriteString(R"TS(import { v0 } from "../src/file0";
)TS");
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content.String()); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		gostr::Builder expected;
		for (int __i = 0; __i < numImports; __i++) {
			expected.WriteString(R"TS(import "@some/unresolved-module";
)TS");
		}
		expected.WriteString(R"TS(import { v0 } from "../src/file0-renamed";
)TS");
		f->VerifyWillRenameFilesEdits(t, "/src/file0.ts", "/src/file0-renamed.ts", std::unordered_map<std::string, std::string>{{"/pkg/ugly.ts", expected.String()}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_duplicateUnresolvedImports, TestGetEditsForFileRename_duplicateUnresolvedImports);


static void TestGetEditsForFileRename_cssImport4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /tsconfig.json
{ "compilerOptions": { "allowArbitraryExtensions": true } }
// @Filename: /app.css
.cookie-banner {
  display: none;
}
// @Filename: /app.d.css.ts
declare const css: {
  cookieBanner: string;
};
export default css;
// @Filename: /a.ts
import styles from ".//*rename*/app.css";)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->Workspace->FileOperations->WillRename = false;
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyRename(t, "rename", "app2.css", std::unordered_map<std::string, std::string>{{"/a.ts", R"TS(import styles from "./app2.css";)TS"}, {"/app2.d.css.ts", R"TS(declare const css: {
  cookieBanner: string;
};
export default css;)TS"}, {"/app2.css", R"TS(.cookie-banner {
  display: none;
})TS"}});
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRename_cssImport4, TestGetEditsForFileRename_cssImport4);


static void TestGetEditsForFileRenameLoadsUnopenedCompositeProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @stateBaseline: true
// @Filename: /tsconfig.json
{
  "files": [],
  "references": [
    { "path": "./lib" },
    { "path": "./app" }
  ]
}

// @Filename: /lib/tsconfig.json
{
  "compilerOptions": {
    "composite": true
  },
  "files": ["./helper.ts", "./other-helper.ts"]
}

// @Filename: /lib/helper.ts
export const /*helper*/helper = 0;

// @Filename: /lib/other-helper.ts
import { helper } from "./helper";
helper;

// @Filename: /app/tsconfig.json
{
  "compilerOptions": {
    "composite": true
  },
  "files": ["./main.ts"],
  "references": [
    { "path": "../lib" }
  ]
}

// @Filename: /app/main.ts
import { helper } from "../lib/helper";
helper;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "helper");
		auto result = f->WillRenameFiles(t, {std::make_shared<lsproto::FileRename>(lsproto::FileRename{.OldUri = lsconv::FileNameToDocumentURI(std::string("/lib/helper.ts")), .NewUri = lsconv::FileNameToDocumentURI(std::string("/lib/renamed-helper.ts"))})});
		if (result.WorkspaceEdit == nullptr || result.WorkspaceEdit->DocumentChanges == nullptr) {
			t->Fatal({"workspace/willRenameFiles returned no document changes"});
		}
		for (auto change : *(*result.WorkspaceEdit->DocumentChanges)) {
			if (change.TextDocumentEdit != nullptr && lsproto::documentUriFileName(change.TextDocumentEdit->TextDocument.Uri) == "/app/main.ts") {
				for (auto edit : *change.TextDocumentEdit->Edits) {
					if (edit.TextEdit != nullptr && edit.TextEdit->NewText == "../lib/renamed-helper") {
						return;
					}
				}
			}
		}
		t->Fatal({"workspace/willRenameFiles returned no import update for /app/main.ts"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetEditsForFileRenameLoadsUnopenedCompositeProject, TestGetEditsForFileRenameLoadsUnopenedCompositeProject);
} // namespace
