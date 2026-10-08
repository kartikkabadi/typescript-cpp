// PORTED FROM Go fourslash tests (batch C, fsgen)
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


static std::pair<std::shared_ptr<fourslash::FourslashTest>, std::function<void()>> newContentMapperFourslash(gostd::testing::T* t, std::string content, std::string mapper, const std::vector<std::string>& extensions) {
	t->Helper();
	auto quotedExtensions = std::vector<std::string>(int(extensions.size()));
	{
		int i = 0;
		for (auto&& extension : extensions) {
			quotedExtensions[i] = gostd::sprintf("%q", {extension});
			i++;
		}
	}
	content = (((((std::string(R"TS(// @Filename: /tsconfig.json
{
	"compilerOptions": {
		"target": "es2020",
		"module": "esnext",
		"moduleResolution": "bundler",
		"strict": true
	},
	"contentMappers": [
		{ "package": "mapper", "extensions": [)TS") + gostr::join(quotedExtensions, ", ")) + std::string(R"TS(] }
	]
}

// @Filename: /node_modules/mapper/package.json
)TS")) + testutil::contentmappertest::PackageJSON(mapper)) + std::string(R"TS(

)TS")) + content);
	return fourslash::NewFourslashWithOptions(t, content, tsu::ptr(fourslash::FourslashOptions{.ContentMapperSpawner = std::shared_ptr<contentmapper::Spawner>(testutil::contentmappertest::NewSpawner()), .RunExternalCode = true}));
}

// contentMapperAutoImports_test.go

// contentMapperAutoImports_test.go
static void TestContentMapperAutoImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<script lang="ts">
export const profileTitle = "Profile";
</script>

// @Filename: /main.ts
profileTi/**/
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"profileTitle"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperAutoImports, TestContentMapperAutoImports);

// contentMapperAutoImports_test.go
static void TestContentMapperAnonymousDefaultAutoImportName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /Component.vue
<script lang="ts">
export default {};
</script>

// @Filename: /main.ts
Comp/**/
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"Component"}, .Excludes = std::vector<std::string>{"ComponentVue"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "Component", .Source = "./Component.vue", .Description = R"TS(Add import from "./Component.vue")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import Component from "./Component.vue";

Comp
)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperAnonymousDefaultAutoImportName, TestContentMapperAnonymousDefaultAutoImportName);

// contentMapperAutoImports_test.go
static void TestContentMapperAutoImportsIntoMappedFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /dep.ts
export const existing = 1;
export const helper = 2;

// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<script lang="ts">
import { existing } from "./dep";
export const profileTitle = help/**/;
</script>
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"helper"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperAutoImportsIntoMappedFile, TestContentMapperAutoImportsIntoMappedFile);

// contentMapperAutoImports_test.go
static void TestContentMapperAutoImportsAfterSynthesizedHeader(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /dep.ts
export const helper = 1;

// @Filename: /app.box
const value = help/**/;
)TS", std::string(testutil::contentmappertest::TransformingMapper), {".box"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"helper"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "helper", .Source = "./dep", .Description = R"TS(Add import from "./dep")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import { helper } from "./dep";

const value = help;
)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperAutoImportsAfterSynthesizedHeader, TestContentMapperAutoImportsAfterSynthesizedHeader);

// contentMapperAutoImports_test.go
static void TestContentMapperSupplementalAutoImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /dep.ts
export const helper = 1;

// @Filename: /app.astro
const value = help/**/;
)TS", std::string(testutil::contentmappertest::SupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "helper", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./dep"})})})}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalAutoImports, TestContentMapperSupplementalAutoImports);

// contentMapperAutoImports_test.go
static void TestContentMapperSupplementalFilesAreNotAutoImportTargets(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /lib.astro
export const supplementalOnly = 1;

// @Filename: /main.ts
supplementalOn/**/
)TS", std::string(testutil::contentmappertest::SupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = std::vector<std::string>{"supplementalOnly"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalFilesAreNotAutoImportTargets, TestContentMapperSupplementalFilesAreNotAutoImportTargets);

// contentMapperAutoImports_test.go
static void TestContentMapperNodeModulesAutoImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /package.json
{ "dependencies": { "profile-package": "1.0.0" } }

// @Filename: /node_modules/profile-package/package.json
{ "name": "profile-package", "version": "1.0.0" }

// @Filename: /node_modules/profile-package/ProfileCard.vue
<component name="ProfileCard">
<script lang="ts">
export const profileTitle = "Profile";
</script>

// @Filename: /node_modules/profile-package/HiddenCard.vue
<component name="HiddenCard">
<script lang="ts">
export const hiddenTitle = "Hidden";
</script>

// @Filename: /load.ts
import "profile-package/ProfileCard.vue";

// @Filename: /main.ts
profileTi/**/
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"profileTitle"}, .Excludes = std::vector<std::string>{"hiddenTitle"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->BaselineAutoImportsCompletions(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperNodeModulesAutoImports, TestContentMapperNodeModulesAutoImports);

// contentMapperAutoImports_test.go
static void TestContentMapperAutoImportAtHoistedImportBoundary(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /aaa.ts
export const helper = 1;

// @Filename: /dep.ts
export const existing = 2;

// @Filename: /App.svelte
<script lang="ts">
  import { existing } from "./dep";
  const value = help/**/;
</script>
)TS", std::string(testutil::contentmappertest::HoistingMapper), {".svelte"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "helper", .SortText = std::string(std::string(ls::SortTextAutoImportSuggestions)), .AdditionalTextEdits = fourslash::AnyTextEdits, .Data = std::make_shared<lsproto::CompletionItemData>(lsproto::CompletionItemData{.AutoImport = std::make_shared<lsproto::AutoImportFix>(lsproto::AutoImportFix{.ModuleSpecifier = "./aaa"})})})}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "helper", .Source = "./aaa", .Description = R"TS(Add import from "./aaa")TS", .NewFileContent = std::make_shared<std::string>(R"TS(<script lang="ts">
  import { helper } from "./aaa";
  import { existing } from "./dep";
  const value = help;
</script>
)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperAutoImportAtHoistedImportBoundary, TestContentMapperAutoImportAtHoistedImportBoundary);

// contentMapperCodeActions_test.go

// contentMapperCodeActions_test.go
static void TestContentMapperDiagnosticCodeDoesNotSelectTypeScriptFix(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.box
export function f/*query*/oo() { return 1; }
)TS", std::string(testutil::contentmappertest::DiagnosticCodeCollisionMapper), {".box"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "query");
		f->VerifyCodeFixNotAvailable(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDiagnosticCodeDoesNotSelectTypeScriptFix, TestContentMapperDiagnosticCodeDoesNotSelectTypeScriptFix);

// contentMapperCompletions_test.go

// contentMapperCompletions_test.go
static void TestContentMapperCompletions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /settings.ts
export const settings = { color: "blue", size: 2 };

// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template>
  <h1>{{ ti/*atom*/tle }}</h1>
  <div class="card/*markup*/">Profile</div>
</template>
<script lang="ts">
import { settings } from "./settings";
export const title = "Profile";
export const card = { title, settings };
settings.co/*outgoing*/lor;
card.ti/*exact*/tle;
</script>

// @Filename: /main.ts
import { card } from "./ProfileCard.vue";
card.se/*incoming*/ttings;
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto propertyCompletions = [&](const std::vector<fourslash::CompletionsExpectedItem>& items) {
	return std::make_shared<fourslash::CompletionsExpectedList>(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = items})});
	};
		f->VerifyCompletions(t, "exact", (propertyCompletions({"title"})).get());
		f->VerifyCompletions(t, "outgoing", (propertyCompletions({"color"})).get());
		f->VerifyCompletions(t, "incoming", (propertyCompletions({"settings"})).get());
		f->VerifyCompletions(t, "atom", nullptr);
		f->VerifyCompletions(t, "markup", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperCompletions, TestContentMapperCompletions);

// contentMapperCompletions_test.go
static void TestContentMapperCompletionAtMappedBoundary(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /settings.ts
export const settings = { color: "blue", size: 2 };

// @Filename: /ProfileCard.vue
<script lang="ts">
import { settings } from "./settings";
settings./*boundary*/</script>
<template>{{ settings }}</template>
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "boundary", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"color", "size"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperCompletionAtMappedBoundary, TestContentMapperCompletionAtMappedBoundary);

// contentMapperCompletions_test.go
static void TestContentMapperCompletionEditRange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.box
export const foo = 1;
[|foo|]/*completion*/
)TS", std::string(testutil::contentmappertest::TransformingMapper), {".box"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "completion", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = std::make_shared<fourslash::EditRange>(fourslash::EditRange{.Insert = f->Ranges()[0], .Replace = f->Ranges()[0]})}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"foo"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperCompletionEditRange, TestContentMapperCompletionEditRange);

// contentMapperCompletions_test.go
static void TestContentMapperModulePathCompletions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto content = ((std::string(R"TS(// @Filename: /tsconfig.json
{
	"compilerOptions": {
		"target": "es2020",
		"module": "esnext",
		"moduleResolution": "bundler",
		"strict": true
	},
	"contentMappers": [
		{ "package": "mapper", "extensions": [".vue"] }
	],
	"files": ["main.ts", "Loaded.vue"]
}

// @Filename: /node_modules/mapper/package.json
)TS") + testutil::contentmappertest::PackageJSON(testutil::contentmappertest::ComponentMapper)) + std::string(R"TS(

// @Filename: /Loaded.vue
<component name="Loaded">
<script lang="ts">
export const loaded = true;
</script>

// @Filename: /Unloaded.vue
// @noOpen: true
<component name="Unloaded">
<script lang="ts">
export const unloaded = true;
</script>

// @Filename: /main.ts
import { loaded } from "./Loaded.vue";
import {} from "./[|/*loadedPath*/Lo|]";
import {} from "./[|/*unloadedPath*/Un|]";
)TS"));
		auto __fsp = fourslash::NewFourslashWithOptions(t, content, tsu::ptr(fourslash::FourslashOptions{.ContentMapperSpawner = std::shared_ptr<contentmapper::Spawner>(testutil::contentmappertest::NewSpawner()), .RunExternalCode = true})); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto pathCompletion = [&](std::string label, int rangeIndex) {
	return std::make_shared<fourslash::CompletionsExpectedList>(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = label, .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindFile), .Detail = std::string(label), .TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{.TextEdit = std::make_shared<lsproto::TextEdit>(lsproto::TextEdit{.Range = f->Ranges()[rangeIndex]->LSRange, .NewText = label})})})}})});
	};
		f->VerifyCompletions(t, "loadedPath", (pathCompletion("Loaded.vue", 0)).get());
		f->VerifyCompletions(t, "unloadedPath", (pathCompletion("Unloaded.vue", 1)).get());
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperModulePathCompletions, TestContentMapperModulePathCompletions);

// contentMapperDeclarationMapNavigation_test.go

// contentMapperDeclarationMapNavigation_test.go
static void TestContentMapperDeclarationMapNavigation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /node_modules/component/package.json
{
    "name": "component",
    "version": "1.0.0",
    "types": "component.d.vue.ts"
}

// @Filename: /node_modules/component/component.vue
export interface ComponentProps { emoji: "😀"; label: string; }
export declare const /*source*/component: ComponentProps;

// @Filename: /node_modules/component/component.d.vue.ts
export interface ComponentProps {
    emoji: "😀";
    label: string;
}
export declare const component: ComponentProps;
//# sourceMappingURL=component.d.vue.ts.map

// @Filename: /node_modules/component/component.d.vue.ts.map
{"version":3,"file":"component.d.vue.ts","sourceRoot":"","sources":["component.vue"],"names":[],"mappings":"AAAA,MAAM,WAAW,cAAc;IAAG,KAAK,EAAE,IAAI,CAAC;IAAC,KAAK,EAAE,MAAM,CAAC;CAAE;AAC/D,MAAM,CAAC,OAAO,CAAC,MAAM,SAAS,EAAE,cAAc,CAAC"}

// @Filename: /main.ts
import { component } from "component";
/*use*/component.label;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToDefinition(t, true, {"use"});
		f->VerifyBaselineFindAllReferences(t, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDeclarationMapNavigation, TestContentMapperDeclarationMapNavigation);

// contentMapperDefinition_test.go

// contentMapperDefinition_test.go
static void TestContentMapperDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /format.ts
export function [|format|](value: string): string { return value.toUpperCase(); }

// @Filename: /ProfileCard.vue
<component name="[|ProfileCard|]">
<template>
  <h1>{{ ti/*template*/tle }}</h1>
  <p class="card/*markup*/">Profile</p>
</template>
<script lang="ts">
import { format } from "./format";
export const [|title|] = "Profile";
export const heading = for/*outgoing*/mat(title);
export const localTitle = ti/*within*/tle;
</script>

// @Filename: /main.ts
import DefaultCard, { ProfileCard, title } from "./ProfileCard.vue";
export const pageTitle = ti/*incoming*/tle;
export const component = Profile/*atomTarget*/Card;
export const fallback = Default/*fallback*/Card;
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"within", "template", "incoming", "outgoing", "atomTarget", "fallback", "markup"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDefinition, TestContentMapperDefinition);

// contentMapperDiagnostics_test.go

// contentMapperDiagnostics_test.go
static void TestContentMapperDiagnostics(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template>
  <h1>{{ [|missingTitle|] }}</h1>
  <p>{{ takesNumber([|title + suffix|]) }}</p>
</template>
<script lang="ts">
export const [|bad|]: number = "wrong";
const title = "Profile";
const suffix = "!";
function takesNumber(value: number) { return value; }
</script>
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDiagnostics, TestContentMapperDiagnostics);

// contentMapperDiagnostics_test.go
static void TestContentMapperSynthesizedDiagnostics(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /ProfileCard.vue
<template><h1>Profile</h1></template>
)TS", std::string(testutil::contentmappertest::SynthesizingMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSynthesizedDiagnostics, TestContentMapperSynthesizedDiagnostics);

// contentMapperDiagnostics_test.go
static void TestContentMapperTransformFailureDiagnostics(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.vue
[||]<template>hi</template>
)TS", std::string(testutil::contentmappertest::FailingMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/app.vue");
		f->VerifyNonSuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Range = f->Ranges()[0]->LSRange, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(18069))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>(R"TS(The content mapper 'mapper' failed to transform this file.
  The content mapper process failed while handling the transform request.)TS")}})});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperTransformFailureDiagnostics, TestContentMapperTransformFailureDiagnostics);

// contentMapperDocumentHighlights_test.go

// contentMapperDocumentHighlights_test.go
static void TestContentMapperDocumentHighlights(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template>
  <h1>{{ [|ti/*template*/tle|] }}</h1>
  <p class="card/*markup*/">Profile</p>
</template>
<script lang="ts">
export const [|ti/*script*/tle|] = "Profile";
export const heading = [|title|].toUpperCase();
</script>
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"script", "template", "markup"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDocumentHighlights, TestContentMapperDocumentHighlights);

// contentMapperDocumentSymbols_test.go

// contentMapperDocumentSymbols_test.go
static void TestContentMapperSynthesizedDocumentSymbols(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.vue
component source with no direct TypeScript span/**/
)TS", std::string(testutil::contentmappertest::SynthesizingMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSynthesizedDocumentSymbols, TestContentMapperSynthesizedDocumentSymbols);

// contentMapperDocumentSymbols_test.go
static void TestContentMapperSupplementalDocumentSymbols(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.astro
export function supplementalSymbol() {}
)TS", std::string(testutil::contentmappertest::SupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/app.astro");
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalDocumentSymbols, TestContentMapperSupplementalDocumentSymbols);

// contentMapperDuplicateMappings_test.go

// contentMapperDuplicateMappings_test.go
static void TestContentMapperDuplicateMappings(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /value.dup
[|val/*query*/ue|]
)TS", std::string(testutil::contentmappertest::DuplicateMapper), {".dup"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "query", "const value: 1", "");
		f->VerifyBaselineGoToDefinition(t, false, {"query"});
		f->VerifyBaselineFindAllReferences(t, {"query"});
		f->VerifyRename(t, "query", "renamed", std::unordered_map<std::string, std::string>{{"/value.dup", R"TS(renamed
)TS"}});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDuplicateMappings, TestContentMapperDuplicateMappings);

// contentMapperDuplicateMappings_test.go
static void TestContentMapperDisabledFeatures(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /disabled.dup
val/*query*/ue
)TS", std::string(testutil::contentmappertest::DuplicateMapper), {".dup"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "query");
		f->VerifyNotQuickInfoExists(t);
		f->VerifyBaselineGoToDefinition(t, false, {"query"});
		f->VerifyBaselineFindAllReferences(t, {"query"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDisabledFeatures, TestContentMapperDisabledFeatures);

// contentMapperDuplicateMappings_test.go
static void TestContentMapperDisabledNavigationTargets(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /disabled.dup
value

// @Filename: /main.ts
import { value } from "./disabled.dup";
export const result = val/*query*/ue;
)TS", std::string(testutil::contentmappertest::DuplicateMapper), {".dup"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"query"});
		f->VerifyBaselineFindAllReferences(t, {"query"});
		f->VerifyBaselineVSFindAllReferences(t, {"query"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDisabledNavigationTargets, TestContentMapperDisabledNavigationTargets);

// contentMapperDuplicateMappings_test.go
static void TestContentMapperConflictingDuplicateRenameMappings(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /rename-conflict.dup
val/*query*/ue
)TS", std::string(testutil::contentmappertest::DuplicateMapper), {".dup"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "query");
		auto result = f->RenameAtCaret(t, "renamed");
		if ((result.WorkspaceEdit != nullptr)) {
			t->Fatalf("expected conflicting projections to abort rename, got %#v", {gostd::fmtArg::ptr((result.WorkspaceEdit).get())});
		}
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperConflictingDuplicateRenameMappings, TestContentMapperConflictingDuplicateRenameMappings);

// contentMapperDuplicateProjections_test.go

// contentMapperDuplicateProjections_test.go
static void TestContentMapperFileRenameAcrossDuplicateProjections(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /dep.ts
export const helper = 1;

// @Filename: /app.astro
import { helper } from "./dep";
helper;
)TS", std::string(testutil::contentmappertest::DuplicateProjectionMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWillRenameFilesEdits(t, "/dep.ts", "/renamed.ts", std::unordered_map<std::string, std::string>{{"/app.astro", R"TS(import { helper } from "./renamed";
helper;
)TS"}}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperFileRenameAcrossDuplicateProjections, TestContentMapperFileRenameAcrossDuplicateProjections);

// contentMapperEditSafety_test.go

// contentMapperEditSafety_test.go
static void TestContentMapperAutoImportAfterSynthesizedSupplementalPrefix(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /dep.ts
export const helper = 1;

// @Filename: /app.astro
const value = help/**/;
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"helper"}}), .UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.IncludeCompletionsForModuleExports = Tristate::True, .IncludeCompletionsForImportStatements = Tristate::True})}));
		f->VerifyApplyCodeActionFromCompletion(t, std::make_shared<std::string>(""), tsu::ptr(fourslash::ApplyCodeActionFromCompletionOptions{.Name = "helper", .Source = "./dep", .Description = R"TS(Add import from "./dep")TS", .NewFileContent = std::make_shared<std::string>(R"TS(import { helper } from "./dep";

const value = help;
)TS")}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperAutoImportAfterSynthesizedSupplementalPrefix, TestContentMapperAutoImportAfterSynthesizedSupplementalPrefix);

// contentMapperEditSafety_test.go
static void TestContentMapperDropsUnmappedFoldingRanges(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.fold
host markup
)TS", std::string(testutil::contentmappertest::UnmappedFoldingMapper), {".fold"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/app.fold");
		f->VerifyFoldingRangeLines(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDropsUnmappedFoldingRanges, TestContentMapperDropsUnmappedFoldingRanges);

// contentMapperEditSafety_test.go
static void TestContentMapperSupplementalFoldingRanges(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.astro
function outer() {
    const value = 1;
}
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/app.astro");
		f->VerifyFoldingRangeLines(t, std::vector<fourslash::FoldingRangeLineExpected>{fourslash::FoldingRangeLineExpected{.StartLine = 0, .EndLine = 2}});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalFoldingRanges, TestContentMapperSupplementalFoldingRanges);

// contentMapperEditSafety_test.go
static void TestContentMapperDisabledSupplementalFoldingRanges(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /folding-disabled.astro
function outer() {
    const value = 1;
}
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/folding-disabled.astro");
		f->VerifyFoldingRangeLines(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDisabledSupplementalFoldingRanges, TestContentMapperDisabledSupplementalFoldingRanges);

// contentMapperEditSafety_test.go
static void TestContentMapperDeduplicatesProjectedFoldingRanges(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /folding-duplicate.astro
function outer() {
    const value = 1;
}
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/folding-duplicate.astro");
		f->VerifyFoldingRangeLines(t, std::vector<fourslash::FoldingRangeLineExpected>{fourslash::FoldingRangeLineExpected{.StartLine = 0, .EndLine = 2}});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDeduplicatesProjectedFoldingRanges, TestContentMapperDeduplicatesProjectedFoldingRanges);

// contentMapperEditSafety_test.go
static void TestContentMapperSupplementalCodeLens(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /codelens-supplemental.astro
function outer() {}
outer();
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalCodeLens, TestContentMapperSupplementalCodeLens);

// contentMapperEditSafety_test.go
static void TestContentMapperDisabledSupplementalCodeLens(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /codelens-disabled.astro
function outer() {}
outer();
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDisabledSupplementalCodeLens, TestContentMapperDisabledSupplementalCodeLens);

// contentMapperEditSafety_test.go
static void TestContentMapperDeduplicatesProjectedCodeLens(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /codelens-duplicate.astro
function outer() {}
outer();
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperDeduplicatesProjectedCodeLens, TestContentMapperDeduplicatesProjectedCodeLens);

// contentMapperEditSafety_test.go
static void TestContentMapperSupplementalImplementationCodeLens(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /codelens-implementation.astro
interface Service { run(): void }
class Impl implements Service { run() {} }
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ImplementationsCodeLensEnabled = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalImplementationCodeLens, TestContentMapperSupplementalImplementationCodeLens);

// contentMapperEditSafety_test.go
static void TestContentMapperFormatsSupplementalVerbatimRange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /formatting.astro
function outer(){
const value={a:1};
}
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/formatting.astro");
		f->FormatDocument(t, "/formatting.astro");
		f->VerifyCurrentFileContent(t, R"TS(function outer() {
    const value = { a: 1 };
}
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperFormatsSupplementalVerbatimRange, TestContentMapperFormatsSupplementalVerbatimRange);

// contentMapperEditSafety_test.go
static void TestContentMapperSkipsFormattingDisabledVerbatimRange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function outer(){
const value={a:1};
}
)TS";
		auto __fsp = newContentMapperFourslash(t, (R"TS(// @Filename: /formatting-disabled.astro
)TS" + content), std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/formatting-disabled.astro");
		f->FormatDocument(t, "/formatting-disabled.astro");
		f->VerifyCurrentFileContent(t, content);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSkipsFormattingDisabledVerbatimRange, TestContentMapperSkipsFormattingDisabledVerbatimRange);

// contentMapperEditSafety_test.go
static void TestContentMapperFormatsEachSupplementalVerbatimRange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /formatting-split.astro
function first(){return 1;}
function second(){return 2;}
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/formatting-split.astro");
		f->FormatDocument(t, "/formatting-split.astro");
		f->VerifyCurrentFileContent(t, R"TS(function first() { return 1; }
function second() { return 2; }
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperFormatsEachSupplementalVerbatimRange, TestContentMapperFormatsEachSupplementalVerbatimRange);

// contentMapperEditSafety_test.go
static void TestContentMapperFormatsOnlyFirstOverlappingProjection(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /formatting-overlap.astro
function first(){return 1;}
function second(){return 2;}
function third(){return 3;}
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/formatting-overlap.astro");
		f->FormatDocument(t, "/formatting-overlap.astro");
		f->VerifyCurrentFileContent(t, R"TS(function first() { return 1; }
function second() { return 2; }
    function third() { return 3; }
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperFormatsOnlyFirstOverlappingProjection, TestContentMapperFormatsOnlyFirstOverlappingProjection);

// contentMapperEditSafety_test.go
static void TestContentMapperFormatsSupplementalOriginalSelection(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /formatting-selection.astro
function first(){return 1;}
/*start*/function second(){return 2;}/*end*/
)TS", std::string(testutil::contentmappertest::PrefixedSupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/formatting-selection.astro");
		f->FormatSelection(t, "start", "end");
		f->VerifyCurrentFileContent(t, R"TS(function first(){return 1;}
function second() { return 2; }
)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperFormatsSupplementalOriginalSelection, TestContentMapperFormatsSupplementalOriginalSelection);

// contentMapperHover_test.go

// contentMapperHover_test.go
static void TestContentMapperHover(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /format.ts
export function format(value: string): string { return value.toUpperCase(); }

// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template>
  <h1>{{ ti/*templateTitle*/tle }}</h1>
  <p class="/*markup*/subtitle">Welcome</p>
</template>
<script lang="ts">
import { format } from "./format";
export const ti/*scriptTitle*/tle: string = "Profile";
export const heading = for/*outgoing*/mat(title);
</script>

// @Filename: /main.ts
import { title } from "./ProfileCard.vue";
export const pageTitle = ti/*incoming*/tle;
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "scriptTitle", "const title: string", "");
		f->VerifyQuickInfoAt(t, "templateTitle", "const title: string", "");
		f->VerifyQuickInfoAt(t, "outgoing", "(alias) function format(value: string): string", "");
		f->VerifyQuickInfoAt(t, "incoming", "(alias) const title: string", "");
		f->GoToMarker(t, "markup");
		f->VerifyNotQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperHover, TestContentMapperHover);

// contentMapperHover_test.go
static void TestContentMapperSupplementalHoverJSDocLink(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /globals.astro
const target = 1;
/** See {@link target}. */
const val/*hover*/ue = target;
)TS", std::string(testutil::contentmappertest::SupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "hover", "const value: 1", "See [target](file:///globals.astro#1,7-1,13).");
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalHoverJSDocLink, TestContentMapperSupplementalHoverJSDocLink);

// contentMapperHover_test.go
static void TestContentMapperHoverTriesLaterProjection(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /hover-fallback.dup
val/*hover*/ue
)TS", std::string(testutil::contentmappertest::DuplicateMapper), {".dup"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "hover", "const value: 1", "");
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperHoverTriesLaterProjection, TestContentMapperHoverTriesLaterProjection);

// contentMapperHover_test.go
static void TestContentMapperHoverConcatenatesProjections(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /hover-concat.dup
val/*hover*/ue
)TS", std::string(testutil::contentmappertest::DuplicateMapper), {".dup"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperHoverConcatenatesProjections, TestContentMapperHoverConcatenatesProjections);

// contentMapperImplementation_test.go

// contentMapperImplementation_test.go
static void TestContentMapperImplementation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /models.ts
export class ExternalShape { area = 2 }

// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template>
  <p>{{ local/*template*/Shape.area }}</p>
  <span class="shape/*markup*/">Shape</span>
</template>
<script lang="ts">
import * as models from "./models";
export interface Local/*within*/Shape { area: number }
export class [|LocalSquare|] implements LocalShape { area = 1 }
export class [|ExternalSquare|] extends models.ExternalShape {}
const localShape: LocalShape = new LocalSquare();
models.External/*outgoing*/Shape;
</script>

// @Filename: /main.ts
import { LocalShape } from "./ProfileCard.vue";
let shape: Local/*incoming*/Shape;
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToImplementation(t, {"within", "template", "incoming", "outgoing", "markup"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperImplementation, TestContentMapperImplementation);

// contentMapperRangeFeatures_test.go

// contentMapperRangeFeatures_test.go
static void TestContentMapperRangeFeaturesIncludeScriptInsideMarkup(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.vue
<template>before</template>
<script lang="ts">
const message = "world";
message/*selection*/;
</script>
<template>after</template>
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperRangeFeaturesIncludeScriptInsideMarkup, TestContentMapperRangeFeaturesIncludeScriptInsideMarkup);

// contentMapperReferences_test.go

// contentMapperReferences_test.go
static void TestContentMapperReferences(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /format.ts
export function [|format|](value: string): string { return value; }

// @Filename: /ProfileCard.vue
<component name="[|ProfileCard|]">
<template>
  <h1>{{ [|ti/*template*/tle|] }}</h1>
  <p class="card/*markup*/">Profile</p>
</template>
<script lang="ts">
import { format } from "./format";
export const [|ti/*script*/tle|] = "Profile";
export const heading = [|for/*outgoing*/mat|]([|title|]);
</script>

// @Filename: /main.ts
import DefaultCard, { ProfileCard, title } from "./ProfileCard.vue";
export const pageTitle = [|ti/*incoming*/tle|];
export const component = [|Profile/*atomResult*/Card|];
export const fallback = [|Default/*synthesized*/Card|];
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"script", "template", "incoming", "outgoing", "atomResult", "synthesized", "markup"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperReferences, TestContentMapperReferences);

// contentMapperRename_test.go

// contentMapperRename_test.go
static void TestContentMapperRename(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template><h1>{{ title }}</h1></template>
<script lang="ts">
export const ti/*rename*/tle = "Profile";
export const heading = title.toUpperCase();
</script>

// @Filename: /main.ts
import { title } from "./ProfileCard.vue";
export const pageTitle = title;
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRename(t, "rename", "newTitle", std::unordered_map<std::string, std::string>{{"/ProfileCard.vue", R"TS(<component name="ProfileCard">
<template><h1>{{ title }}</h1></template>
<script lang="ts">
export const newTitle = "Profile";
export const heading = newTitle.toUpperCase();
</script>
)TS"}, {"/main.ts", R"TS(import { newTitle } from "./ProfileCard.vue";
export const pageTitle = newTitle;
)TS"}});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperRename, TestContentMapperRename);

// contentMapperRename_test.go
static void TestContentMapperRenameRejectsAtomAndUnmappedOrigins(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template>
  <h1>{{ ti/*atom*/tle }}</h1>
  <p class="card/*markup*/">Profile</p>
</template>
<script lang="ts">
export const title = "Profile";
</script>
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "atom");
		f->VerifyRenameFailed(t, nullptr);
		f->GoToMarker(t, "markup");
		f->VerifyRenameFailed(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperRenameRejectsAtomAndUnmappedOrigins, TestContentMapperRenameRejectsAtomAndUnmappedOrigins);

// contentMapperRename_test.go
static void TestContentMapperRenameOutgoing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /format.ts
export function format(value: string): string { return value; }

// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template><h1>{{ utils.format(title) }}</h1></template>
<script lang="ts">
import * as utils from "./format";
export const title = "Profile";
export const heading = utils.for/*rename*/mat(title);
</script>
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyRename(t, "rename", "render", std::unordered_map<std::string, std::string>{{"/format.ts", R"TS(export function render(value: string): string { return value; }
)TS"}, {"/ProfileCard.vue", R"TS(<component name="ProfileCard">
<template><h1>{{ utils.format(title) }}</h1></template>
<script lang="ts">
import * as utils from "./format";
export const title = "Profile";
export const heading = utils.render(title);
</script>
)TS"}});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperRenameOutgoing, TestContentMapperRenameOutgoing);

// contentMapperSignatureHelp_test.go

// contentMapperSignatureHelp_test.go
static void TestContentMapperSignatureHelp(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /format.ts
export function format(value: string, uppercase?: boolean): string { return value; }

// @Filename: /ProfileCard.vue
<component name="ProfileCard">
<template>
  <h1>{{ format(ti/*templateCall*/tle) }}</h1>
  <button title="/*markup*/save">Save</button>
</template>
<script lang="ts">
import { format } from "./format";
export const title = "Profile";
format(title, /*scriptCall*/true);
export function greet(name: string, count: number): string { return name.repeat(count); }
</script>

// @Filename: /main.ts
import { greet } from "./ProfileCard.vue";
greet("hello", /*incomingCall*/2);
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "scriptCall");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "format(value: string, uppercase?: boolean): string", .ParameterName = "uppercase?", .ParameterSpan = "uppercase?: boolean"});
		f->GoToMarker(t, "templateCall");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "format(value: string, uppercase?: boolean): string", .ParameterName = "value", .ParameterSpan = "value: string"});
		f->GoToMarker(t, "incomingCall");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "greet(name: string, count: number): string", .ParameterName = "count", .ParameterSpan = "count: number"});
		f->GoToMarker(t, "markup");
		f->VerifyNoSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSignatureHelp, TestContentMapperSignatureHelp);

// contentMapperSignatureHelp_test.go
static void TestContentMapperSupplementalSignatureHelpJSDocLink(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /globals.astro
const target = 1;
/** See {@link target}. */
function use(value: number) { return value; }
use(/*call*/target);
)TS", std::string(testutil::contentmappertest::SupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "call");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "use(value: number): number", .DocComment = "See [target](file:///globals.astro#1,7-1,13).", .ParameterName = "value", .ParameterSpan = "value: number"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalSignatureHelpJSDocLink, TestContentMapperSupplementalSignatureHelpJSDocLink);

// contentMapperSignatureHelp_test.go
static void TestContentMapperSignatureHelpTriesLaterProjection(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /signature-fallback.dup
use(/*call*/)
)TS", std::string(testutil::contentmappertest::DuplicateMapper), {".dup"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "call");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "use(value: number): void", .ParameterName = "value", .ParameterSpan = "value: number"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSignatureHelpTriesLaterProjection, TestContentMapperSignatureHelpTriesLaterProjection);

// contentMapperSupplementalDefinition_test.go

// contentMapperSupplementalDefinition_test.go
static void TestContentMapperSupplementalDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /globals.astro
const [|supplementalValue|] = 1;
[|supplemental/*use*/Value|];
supplementalV/*completion*/;
)TS", std::string(testutil::contentmappertest::SupplementalMapper), {".astro"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"use"});
		f->VerifyQuickInfoAt(t, "use", "const supplementalValue: 1", "");
		f->VerifyBaselineFindAllReferences(t, {"use"});
		f->GoToMarker(t, "completion");
		auto completions = f->GetCompletions(t, nullptr);
		std::shared_ptr<lsproto::CompletionItem> completion{};
		{
			int _ = 0;
			for (auto&& item : (*completions->Items)) {
				if ((item->Label == "supplementalValue")) {
					completion = item;
					break;
				}
				_++;
			}
		}
		if ((completion == nullptr)) {
			t->Fatal({"supplementalValue completion not found"});
		}
		if ((((completion->Data->FileName != "/globals.astro") || (completion->Data->SupplementalFileIndex == std::nullopt)) || ((*completion->Data->SupplementalFileIndex) != 0))) {
			t->Fatalf("unexpected completion content mapper file data: %#v", {gostd::fmtArg::ptr((completion->Data).get())});
		}
		f->ResolveCompletionItem(t, completion);
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperSupplementalDefinition, TestContentMapperSupplementalDefinition);

// contentMapperTypeDefinition_test.go

// contentMapperTypeDefinition_test.go
static void TestContentMapperTypeDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /models.ts
export interface [|ExternalShape|] { area: number }

// @Filename: /ProfileCard.vue
<component name="[|ProfileCard|]">
<template>
  <p>{{ lo/*template*/calShape.area }}</p>
  <span class="shape/*markup*/">Shape</span>
</template>
<script lang="ts">
import { ExternalShape } from "./models";
export interface [|LocalShape|] { area: number }
export const localShape: LocalShape = { area: 1 };
export const externalShape: ExternalShape = { area: 2 };
local/*within*/Shape;
external/*outgoing*/Shape;
</script>

// @Filename: /main.ts
import { ProfileCard, localShape } from "./ProfileCard.vue";
local/*incoming*/Shape;
Profile/*atomTarget*/Card;
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToTypeDefinition(t, {"within", "template", "incoming", "outgoing", "atomTarget", "markup"});
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperTypeDefinition, TestContentMapperTypeDefinition);

// contentMapperInlayHints_test.go
// The mapper emits the script verbatim followed by a synthesized render
// function; the script and four template identifiers map to five disjoint
// virtual ranges. Each range must release its checker before processing the
// next range.
static void TestContentMapperInlayHintsReleaseEachRange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto __fsp = newContentMapperFourslash(t, R"TS(// @Filename: /app.vue
<script>
const value = () => 1;
</script>
{{value}}
{{value}}
{{value}}
{{value}}
)TS", std::string(testutil::contentmappertest::ComponentMapper), {".vue"}); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/app.vue");
		f->VerifyBaselineInlayHints(t, nullptr, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestContentMapperInlayHintsReleaseEachRange, TestContentMapperInlayHintsReleaseEachRange);

} // namespace
