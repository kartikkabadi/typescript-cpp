// Port of tsc/internal/vfs/vfsmatch/vfsmatch_test.go.
// Internal test: exercises vfsmatch internals (matchFiles, globPattern,
// SpecMatcher, nextPathPartParts, getBasePaths) — same cases as Go.
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/vfs/vfsmatch/vfsmatch.h"
#include "internal/vfs/vfstest/vfstest.h"

using tsc::gostd::testing::T;
using tsc::vfs::vfsmatch::UnlimitedDepth;
namespace vfs = tsc::vfs;
namespace vfstest = tsc::vfs::vfstest;
namespace vfsmatch = tsc::vfs::vfsmatch;
using vfsmatch::Usage;
using vfsmatch::matchFiles;
using vfsmatch::NewSpecMatcher;
namespace assert = tsc::gotest::assert;

namespace {

// slicesContains — slices.Contains.
inline bool slicesContains(const std::vector<std::string>& v,
                           const std::string& s) {
	return std::find(v.begin(), v.end(), s) != v.end();
}

// strContains — local test helper `contains` (substring check).
inline bool strContains(const std::string& s, const std::string& sub) {
	return s.find(sub) != std::string::npos;
}

// strHasSuffix — local test helper `hasSuffix`.
inline bool strHasSuffix(const std::string& s, const std::string& suf) {
	return s.size() >= suf.size() &&
	       s.compare(s.size() - suf.size(), std::string::npos, suf) == 0;
}

// Host constructors — vfstest.FromMap maps, same file lists as Go.
std::shared_ptr<vfs::FS> caseInsensitiveHost() {
	return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
	    {"/dev/a.ts", ""}, {"/dev/a.d.ts", ""}, {"/dev/a.js", ""},
	    {"/dev/b.ts", ""}, {"/dev/b.js", ""}, {"/dev/c.d.ts", ""},
	    {"/dev/z/a.ts", ""}, {"/dev/z/abz.ts", ""}, {"/dev/z/aba.ts", ""},
	    {"/dev/z/b.ts", ""}, {"/dev/z/bbz.ts", ""}, {"/dev/z/bba.ts", ""},
	    {"/dev/x/a.ts", ""}, {"/dev/x/aa.ts", ""}, {"/dev/x/b.ts", ""},
	    {"/dev/x/y/a.ts", ""}, {"/dev/x/y/b.ts", ""},
	    {"/dev/js/a.js", ""}, {"/dev/js/b.js", ""},
	    {"/dev/js/d.min.js", ""}, {"/dev/js/ab.min.js", ""},
	    {"/ext/ext.ts", ""}, {"/ext/b/a..b.ts", ""},
	}, false);
}

std::shared_ptr<vfs::FS> caseSensitiveHost() {
	return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
	    {"/dev/a.ts", ""}, {"/dev/a.d.ts", ""}, {"/dev/a.js", ""},
	    {"/dev/b.ts", ""}, {"/dev/b.js", ""}, {"/dev/A.ts", ""},
	    {"/dev/B.ts", ""}, {"/dev/c.d.ts", ""},
	    {"/dev/z/a.ts", ""}, {"/dev/z/abz.ts", ""}, {"/dev/z/aba.ts", ""},
	    {"/dev/z/b.ts", ""}, {"/dev/z/bbz.ts", ""}, {"/dev/z/bba.ts", ""},
	    {"/dev/x/a.ts", ""}, {"/dev/x/b.ts", ""},
	    {"/dev/x/y/a.ts", ""}, {"/dev/x/y/b.ts", ""},
	    {"/dev/q/a/c/b/d.ts", ""},
	    {"/dev/js/a.js", ""}, {"/dev/js/b.js", ""}, {"/dev/js/d.MIN.js", ""},
	}, true);
}

std::shared_ptr<vfs::FS> commonFoldersHost() {
	return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
	    {"/dev/a.ts", ""}, {"/dev/a.d.ts", ""}, {"/dev/a.js", ""},
	    {"/dev/b.ts", ""}, {"/dev/x/a.ts", ""},
	    {"/dev/node_modules/a.ts", ""}, {"/dev/bower_components/a.ts", ""},
	    {"/dev/jspm_packages/a.ts", ""},
	}, false);
}

std::shared_ptr<vfs::FS> dottedFoldersHost() {
	return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
	    {"/dev/x/d.ts", ""}, {"/dev/x/y/d.ts", ""}, {"/dev/x/y/.e.ts", ""},
	    {"/dev/x/.y/a.ts", ""}, {"/dev/.z/.b.ts", ""}, {"/dev/.z/c.ts", ""},
	    {"/dev/w/.u/e.ts", ""}, {"/dev/g.min.js/.g/g.ts", ""},
	}, false);
}

std::shared_ptr<vfs::FS> mixedExtensionHost() {
	return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
	    {"/dev/a.ts", ""}, {"/dev/a.d.ts", ""}, {"/dev/a.js", ""},
	    {"/dev/b.tsx", ""}, {"/dev/b.d.ts", ""}, {"/dev/b.jsx", ""},
	    {"/dev/c.tsx", ""}, {"/dev/c.js", ""}, {"/dev/d.js", ""},
	    {"/dev/e.jsx", ""}, {"/dev/f.other", ""},
	}, false);
}

std::shared_ptr<vfs::FS> sameNamedDeclarationsHost() {
	return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
	    {"/dev/a.tsx", ""}, {"/dev/a.d.ts", ""}, {"/dev/b.tsx", ""},
	    {"/dev/b.ts", ""}, {"/dev/c.tsx", ""}, {"/dev/m.ts", ""},
	    {"/dev/m.d.ts", ""}, {"/dev/n.tsx", ""}, {"/dev/n.ts", ""},
	    {"/dev/n.d.ts", ""}, {"/dev/o.ts", ""}, {"/dev/x.d.ts", ""},
	}, false);
}

// readDirTestCase — vfsmatch_test.go.
struct readDirTestCase {
	std::string name;
	std::function<std::shared_ptr<vfs::FS>()> host;
	std::string currentDir;
	std::string path;
	std::vector<std::string_view> extensions;
	std::vector<std::string> excludes;
	std::vector<std::string> includes;
	int depth;
	std::function<void(T*, const std::vector<std::string>&)> expect;
};

// runReadDirectoryCase — vfsmatch_test.go.
void runReadDirectoryCase(T* t, const readDirTestCase& tc) {
	std::string currentDir = tc.currentDir.empty() ? "/" : tc.currentDir;
	std::string path = tc.path.empty() ? "/dev" : tc.path;
	int depth = tc.depth == 0 ? UnlimitedDepth : tc.depth;
	auto host = tc.host();
	auto got = matchFiles(path, tc.extensions, tc.excludes, tc.includes,
	                      host->UseCaseSensitiveFileNames(), currentDir,
	                      depth, host.get());
	tc.expect(t, got);
}

void TestReadDirectory(T* t) {
	t->Parallel();
	static const readDirTestCase cases[] = {

	{"defaults include common package folders", commonFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/b.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/node_modules/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/bower_components/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/jspm_packages/a.ts"));}},
	{"literal includes without exclusions", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"a.ts", "b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/a.ts", "/dev/b.ts"});}},
	{"literal includes with non ts extensions excluded", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"a.js", "b.js"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{0});}},
	{"literal includes missing files excluded", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"z.ts", "x.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{0});}},
	{"literal includes with literal excludes", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"b.ts"},
	 std::vector<std::string>{"a.ts", "b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/a.ts"});}},
	{"literal includes with wildcard excludes", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"*.ts", "z/??z.ts", "*/b.ts"},
	 std::vector<std::string>{"a.ts", "b.ts", "z/a.ts", "z/abz.ts", "z/aba.ts", "x/b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/z/a.ts", "/dev/z/aba.ts"});}},
	{"literal includes with recursive excludes", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**/b.ts"},
	 std::vector<std::string>{"a.ts", "b.ts", "x/a.ts", "x/b.ts", "x/y/a.ts", "x/y/b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/a.ts", "/dev/x/a.ts", "/dev/x/y/a.ts"});}},
	{"case sensitive exclude is respected", caseSensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**/b.ts"},
	 std::vector<std::string>{"B.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/B.ts"});}},
	{"explicit includes keep common package folders", commonFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"a.ts", "b.ts", "node_modules/a.ts", "bower_components/a.ts", "jspm_packages/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/b.ts"));
				assert::Assert(t, slicesContains(got, "/dev/node_modules/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/bower_components/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/jspm_packages/a.ts"));}},
	{"wildcard include sorted order", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"z/*.ts", "x/*.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {std::vector<std::string> expected{
					"/dev/z/a.ts", "/dev/z/aba.ts", "/dev/z/abz.ts", "/dev/z/b.ts", "/dev/z/bba.ts", "/dev/z/bbz.ts",
					"/dev/x/a.ts", "/dev/x/aa.ts", "/dev/x/b.ts",
				};
				assert::Equal(t, got, expected);}},
	{"wildcard include same named declarations excluded", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/b.ts"));
				assert::Assert(t, slicesContains(got, "/dev/a.d.ts"));
				assert::Assert(t, slicesContains(got, "/dev/c.d.ts"));}},
	{"wildcard star matches only ts files", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					assert::Assert(t, strContains(f, ".ts") || strContains(f, ".tsx") || strContains(f, ".d.ts"), "unexpected file: " + f);
				}
				assert::Assert(t, !slicesContains(got, "/dev/a.js"));
				assert::Assert(t, !slicesContains(got, "/dev/b.js"));}},
	{"wildcard question mark single character", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"x/?.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/x/a.ts", "/dev/x/b.ts"});}},
	{"wildcard recursive directory", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/z/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/y/a.ts"));}},
	{"double asterisk matches zero-or-more directories", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"x/**/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{2});
				assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/y/a.ts"));}},
	{"wildcard multiple recursive directories", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"x/y/**/a.ts", "x/**/a.ts", "z/**/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, !got.empty());}},
	{"wildcard case sensitive matching", caseSensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/A.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/A.ts"});}},
	{"wildcard missing files excluded", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*/z.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{0});}},
	{"exclude folders with wildcards", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"z", "x"},
	 std::vector<std::string>{"**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					assert::Assert(t, !strContains(f, "/z/") && !strContains(f, "/x/"), "should not contain z or x: " + f);
				}
				assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/b.ts"));}},
	{"include paths outside project absolute", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*", "/ext/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/ext/ext.ts"));}},
	{"include paths outside project relative", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**"},
	 std::vector<std::string>{"*", "../ext/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/ext/ext.ts"));}},
	{"include files containing double dots", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**"},
	 std::vector<std::string>{"/ext/b/a..b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/ext/b/a..b.ts"));}},
	{"exclude files containing double dots", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"/ext/b/a..b.ts"},
	 std::vector<std::string>{"/ext/**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/ext/ext.ts"));
				assert::Assert(t, !slicesContains(got, "/ext/b/a..b.ts"));}},
	{"common package folders implicitly excluded", commonFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/node_modules/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/bower_components/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/jspm_packages/a.ts"));}},
	{"common package folders explicit recursive include", commonFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/a.ts", "**/node_modules/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/node_modules/a.ts"));}},
	{"common package folders wildcard include", commonFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/node_modules/a.ts"));}},
	{"common package folders explicit wildcard include", commonFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*/a.ts", "node_modules/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/node_modules/a.ts"));}},
	{"dotted folders not implicitly included", dottedFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"x/**/*", "w/*/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/d.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/y/d.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/x/.y/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/x/y/.e.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/w/.u/e.ts"));}},
	{"dotted folders explicitly included", dottedFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"x/.y/a.ts", "/dev/.z/.b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/.y/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/.z/.b.ts"));}},
	{"dotted folders recursive wildcard matches directories", dottedFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/.*/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/.y/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/.z/c.ts"));
				assert::Assert(t, slicesContains(got, "/dev/w/.u/e.ts"));}},
	{"trailing recursive include returns empty", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{0});}},
	{"trailing recursive exclude removes everything", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**"},
	 std::vector<std::string>{"**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{0});}},
	{"multiple recursive directory patterns in includes", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/x/**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/y/a.ts"));}},
	{"multiple recursive directory patterns in excludes", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**/x/**"},
	 std::vector<std::string>{"**/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/z/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/x/y/a.ts"));}},
	{"implicit globbification expands directory", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"z"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/z/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/z/aba.ts"));
				assert::Assert(t, slicesContains(got, "/dev/z/b.ts"));}},
	{"exclude patterns starting with starstar", caseSensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**/x"},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					assert::Assert(t, !strContains(f, "/x/"), "should not contain /x/: " + f);
				}}},
	{"include patterns starting with starstar", caseSensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/x", "**/a/**/b"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/q/a/c/b/d.ts"));}},
	{"depth limit one", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 1,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					auto suffix = f.substr(5);
					assert::Assert(t, !strContains(suffix, "/"), "depth 1 should not include nested files: " + f);
				}}},
	{"depth limit two", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 2,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/z/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/x/y/a.ts"));}},
	{"mixed extensions only ts", mixedExtensionHost, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					assert::Assert(t, strHasSuffix(f, ".ts"), "should only have .ts files: " + f);
				}}},
	{"mixed extensions ts and tsx", mixedExtensionHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx"},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					assert::Assert(t, strHasSuffix(f, ".ts") || strHasSuffix(f, ".tsx"), "should only have .ts or .tsx files: " + f);
				}}},
	{"mixed extensions js and jsx", mixedExtensionHost, "/", "/dev",
	 std::vector<std::string_view>{".js", ".jsx"},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					assert::Assert(t, strHasSuffix(f, ".js") || strHasSuffix(f, ".jsx"), "should only have .js or .jsx files: " + f);
				}}},
	{"min js files excluded by wildcard", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".js"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"js/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/js/a.js"));
				assert::Assert(t, slicesContains(got, "/dev/js/b.js"));
				assert::Assert(t, !slicesContains(got, "/dev/js/d.min.js"));
				assert::Assert(t, !slicesContains(got, "/dev/js/ab.min.js"));}},
	{"min js exclusion is case-sensitive on case-sensitive FS", caseSensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".js"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"js/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/js/a.js"));
				assert::Assert(t, slicesContains(got, "/dev/js/b.js"));
				// Legacy behavior: only lowercase ".min.js" is excluded by default when matching is case-sensitive.
				assert::Assert(t, slicesContains(got, "/dev/js/d.MIN.js"));}},
	{"min js files explicitly included", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".js"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"js/*.min.js"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/js/d.min.js"));
				assert::Assert(t, slicesContains(got, "/dev/js/ab.min.js"));}},
	{"min js files included when pattern mentions .min.", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".js"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"js/*.min.*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{2});
				assert::Assert(t, slicesContains(got, "/dev/js/d.min.js"));
				assert::Assert(t, slicesContains(got, "/dev/js/ab.min.js"));}},
	{"exclude literal node_modules folder", commonFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"node_modules"},
	 std::vector<std::string>{"**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/node_modules/a.ts"));}},
	{"same named declarations include ts", sameNamedDeclarationsHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, !got.empty());}},
	{"same named declarations include tsx", sameNamedDeclarationsHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*.tsx"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					assert::Assert(t, strHasSuffix(f, ".tsx"), "should only have .tsx files: " + f);
				}}},
	{"empty includes returns all matching files", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, !got.empty());
				assert::Assert(t, slicesContains(got, "/dev/a.ts"));}},
	{"nil extensions returns all files", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/a.js"));}},
	{"empty extensions slice returns all files", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, !got.empty(), "expected files to be returned");}},

	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			runReadDirectoryCase(t, tc);
			return;
		});
	}
}
void TestIsImplicitGlob(T* t) {
	t->Parallel();
	struct row {
		const char* name;
		const char* input;
		bool expected;
	};
	static const row tests[] = {
	    {"simple", "foo", true},           {"folder", "src", true},
	    {"with extension", "foo.ts", false}, {"trailing dot", "foo.", false},
	    {"star", "*", false},              {"question", "?", false},
	    {"star suffix", "foo*", false},    {"question suffix", "foo?", false},
	    {"dot name", "foo.bar", false},    {"empty", "", true},
	};
	for (auto& tt : tests) {
		auto rec = tt;
		t->Run(rec.name, [rec](T* t) {
			t->Parallel();
			bool result = vfsmatch::isImplicitGlob(rec.input);
			assert::Equal(t, result, rec.expected);
		});
	}
}

void TestReadDirectoryEdgeCases(T* t) {
	t->Parallel();
	static const readDirTestCase cases[] = {

	{"rooted include path", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"/dev/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));}},
	{"include with extension in path", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));}},
	{"special regex characters in path", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/dev/file+test.ts", ""},
			{"/dev/file[0].ts", ""},
			{"/dev/file(1).ts", ""},
			{"/dev/file$money.ts", ""},
			{"/dev/file^start.ts", ""},
			{"/dev/file|pipe.ts", ""},
			{"/dev/file#hash.ts", ""}},
			false); }, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"file+test.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/file+test.ts"));}},
	{"include pattern starting with question mark", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"?.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/b.ts"));}},
	{"include pattern starting with star", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/b.ts"));}},
	{"case insensitive file matching", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/dev/File.ts", ""},
			{"/dev/FILE.ts", ""}},
			true); }, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, got.size() == 2);}},
	{"nested subdirectory base path", caseSensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"q/a/c/b/d.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/q/a/c/b/d.ts"));}},
	{"current directory differs from path", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"z/*.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, !got.empty());}},

	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			runReadDirectoryCase(t, tc);
			return;
		});
	}
}

void TestReadDirectoryEmptyIncludes(T* t) {
	t->Parallel();
	static const readDirTestCase cases[] = {

	{"empty includes slice behavior", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/root/a.ts", ""}},
			true); }, "/", "/root",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {if (got.empty()) { return; }
				assert::Assert(t, slicesContains(got, "/root/a.ts"));}},

	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			runReadDirectoryCase(t, tc);
			return;
		});
	}
}

// TestReadDirectorySymlinkCycle — cyclic symlinks must not loop forever;
// vfs detects the cycle via Realpath and skips the cyclic directory.
void TestReadDirectorySymlinkCycle(T* t) {
	t->Parallel();
	static const readDirTestCase cases[] = {

	{"detects and skips symlink cycles", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/root/file.ts", ""},
			{"/root/a/file.ts", ""},
			{"/root/a/b", vfstest::Symlink("/root/a")}},
			true); }, "/", "/root",
	 std::vector<std::string_view>{".ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {std::vector<std::string> expected{"/root/file.ts", "/root/a/file.ts"};
				assert::Equal(t, got, expected);}},

	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			runReadDirectoryCase(t, tc);
			return;
		});
	}
}

// TestReadDirectoryMatchesTypeScriptBaselines — same outputs as the
// promoted TypeScript baselines.
void TestReadDirectoryMatchesTypeScriptBaselines(T* t) {
	t->Parallel();
	static const readDirTestCase cases[] = {

	{"sorted in include order then alphabetical", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/dev/z/a.ts", ""},
			{"/dev/z/aba.ts", ""},
			{"/dev/z/abz.ts", ""},
			{"/dev/z/b.ts", ""},
			{"/dev/z/bba.ts", ""},
			{"/dev/z/bbz.ts", ""},
			{"/dev/x/a.ts", ""},
			{"/dev/x/aa.ts", ""},
			{"/dev/x/b.ts", ""}},
			false); }, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"z/*.ts", "x/*.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {std::vector<std::string> expected{
					"/dev/z/a.ts", "/dev/z/aba.ts", "/dev/z/abz.ts", "/dev/z/b.ts", "/dev/z/bba.ts", "/dev/z/bbz.ts",
					"/dev/x/a.ts", "/dev/x/aa.ts", "/dev/x/b.ts",
				};
				assert::Equal(t, got, expected);}},
	{"recursive wildcards match dotted directories", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/dev/x/d.ts", ""},
			{"/dev/x/y/d.ts", ""},
			{"/dev/x/y/.e.ts", ""},
			{"/dev/x/.y/a.ts", ""},
			{"/dev/.z/.b.ts", ""},
			{"/dev/.z/c.ts", ""},
			{"/dev/w/.u/e.ts", ""},
			{"/dev/g.min.js/.g/g.ts", ""}},
			false); }, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/.*/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {std::vector<std::string> expected{"/dev/.z/c.ts", "/dev/g.min.js/.g/g.ts", "/dev/w/.u/e.ts", "/dev/x/.y/a.ts"};
				assert::Equal(t, got.size(), expected.size());
				for (auto& want : expected) {
					assert::Assert(t, slicesContains(got, want));
				}}},
	{"common package folders implicitly excluded with wildcard", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/dev/a.ts", ""},
			{"/dev/a.d.ts", ""},
			{"/dev/a.js", ""},
			{"/dev/b.ts", ""},
			{"/dev/x/a.ts", ""},
			{"/dev/node_modules/a.ts", ""},
			{"/dev/bower_components/a.ts", ""},
			{"/dev/jspm_packages/a.ts", ""}},
			false); }, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/a.ts", "/dev/x/a.ts"});}},
	{"js wildcard excludes min js files", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/dev/js/a.js", ""},
			{"/dev/js/b.js", ""},
			{"/dev/js/d.min.js", ""},
			{"/dev/js/ab.min.js", ""}},
			false); }, "/", "/dev",
	 std::vector<std::string_view>{".js"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"js/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/js/a.js", "/dev/js/b.js"});}},
	{"explicit min js pattern includes min files", []() -> std::shared_ptr<vfs::FS> { return vfstest::FromMap(std::unordered_map<std::string, vfstest::MapFileInput>{
			{"/dev/js/a.js", ""},
			{"/dev/js/b.js", ""},
			{"/dev/js/d.min.js", ""},
			{"/dev/js/ab.min.js", ""}},
			false); }, "/", "/dev",
	 std::vector<std::string_view>{".js"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"js/*.min.js"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {std::vector<std::string> expected{"/dev/js/ab.min.js", "/dev/js/d.min.js"};
				assert::Equal(t, got.size(), expected.size());
				for (auto& want : expected) {
					assert::Assert(t, slicesContains(got, want));
				}}},
	{"literal excludes baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"b.ts"},
	 std::vector<std::string>{"a.ts", "b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/a.ts"});}},
	{"wildcard excludes baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"*.ts", "z/??z.ts", "*/b.ts"},
	 std::vector<std::string>{"a.ts", "b.ts", "z/a.ts", "z/abz.ts", "z/aba.ts", "x/b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/z/a.ts", "/dev/z/aba.ts"});}},
	{"recursive excludes baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**/b.ts"},
	 std::vector<std::string>{"a.ts", "b.ts", "x/a.ts", "x/b.ts", "x/y/a.ts", "x/y/b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/a.ts", "/dev/x/a.ts", "/dev/x/y/a.ts"});}},
	{"question mark baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"x/?.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/x/a.ts", "/dev/x/b.ts"});}},
	{"recursive directory pattern baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/a.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/a.ts", "/dev/x/a.ts", "/dev/x/y/a.ts", "/dev/z/a.ts"});}},
	{"case sensitive baseline", caseSensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/A.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/A.ts"});}},
	{"exclude folders baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"z", "x"},
	 std::vector<std::string>{"**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {for (auto& f : got) {
					assert::Assert(t, !strContains(f, "/z/") && !strContains(f, "/x/"), "should not contain z or x: " + f);
				}
				assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/b.ts"));}},
	{"implicit glob expansion baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"z"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got, std::vector<std::string>{"/dev/z/a.ts", "/dev/z/aba.ts", "/dev/z/abz.ts", "/dev/z/b.ts", "/dev/z/bba.ts", "/dev/z/bbz.ts"});}},
	{"trailing recursive directory baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{0});}},
	{"exclude trailing recursive directory baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**"},
	 std::vector<std::string>{"**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Equal(t, got.size(), size_t{0});}},
	{"multiple recursive directory patterns baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/x/**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/aa.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/b.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/y/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/y/b.ts"));}},
	{"include dirs with starstar prefix baseline", caseSensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"**/x", "**/a/**/b"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/a.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/b.ts"));
				assert::Assert(t, slicesContains(got, "/dev/q/a/c/b/d.ts"));}},
	{"dotted folders not implicitly included baseline", dottedFoldersHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"x/**/*", "w/*/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/x/d.ts"));
				assert::Assert(t, slicesContains(got, "/dev/x/y/d.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/x/.y/a.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/x/y/.e.ts"));
				assert::Assert(t, !slicesContains(got, "/dev/w/.u/e.ts"));}},
	{"include paths outside project baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{},
	 std::vector<std::string>{"*", "/ext/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/dev/a.ts"));
				assert::Assert(t, slicesContains(got, "/ext/ext.ts"));}},
	{"include files with double dots baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"**"},
	 std::vector<std::string>{"/ext/b/a..b.ts"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/ext/b/a..b.ts"));}},
	{"exclude files with double dots baseline", caseInsensitiveHost, "/", "/dev",
	 std::vector<std::string_view>{".ts", ".tsx", ".d.ts"},
	 std::vector<std::string>{"/ext/b/a..b.ts"},
	 std::vector<std::string>{"/ext/**/*"},
	 0,
	 [] (T* t, const std::vector<std::string>& got) {assert::Assert(t, slicesContains(got, "/ext/ext.ts"));
				assert::Assert(t, !slicesContains(got, "/ext/b/a..b.ts"));}},
	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			runReadDirectoryCase(t, tc);
			return;
		});
	}
}


// vfsmatch_test.go — dynamic-root read-directory and glob case sensitivity.
void TestReadDirectoryWithExtendedDynamicRoot(T* t) {
	t->Parallel();
	std::string packageDirectory =
	    "^/~ts-uri~/custom/ts-nul-authority/node_modules/Pkg";
	auto host = vfstest::FromMap(
	    std::unordered_map<std::string, vfstest::MapFileInput>{
	        {packageDirectory + "/value.d.ts", ""},
	        {packageDirectory + "/node_modules/dep/index.d.ts", ""}},
	    true);
	auto got = matchFiles(packageDirectory, {".d.ts"}, {}, {"**/*"},
	                      host->UseCaseSensitiveFileNames(),
	                      packageDirectory, UnlimitedDepth, host.get());
	assert::DeepEqual(t, got,
	                  std::vector<std::string>{
	                      packageDirectory + "/value.d.ts"});
}

void TestDynamicAbsoluteGlobUsesCaseSensitivePattern(T* t) {
	t->Parallel();
	std::string root = "^/~ts-uri~/custom/ts-nul-authority";
	auto matcher = NewSpecMatcher({root + "/Foo/**/*.ts"}, "/dev",
	                              Usage::Files, false);
	assert::Assert(t, matcher != nullptr);
	assert::Assert(t, matcher->MatchString(root + "/Foo/a.ts"));
	assert::Assert(t, !matcher->MatchString(root + "/foo/a.ts"));
}

void TestSpecMatcher(T* t) {
	t->Parallel();
	struct row {
		const char* name;
		std::vector<std::string> specs;
		const char* basePath;
		Usage usage;
		bool useCaseSensitiveFileNames;
		std::vector<std::string> matchingPaths;
		std::vector<std::string> nonMatchingPaths;
	};
	static const row cases[] = {
	    {"simple wildcard", {"*.ts"}, "/project", Usage::Files, true,
	     {"/project/a.ts", "/project/b.ts", "/project/foo.ts"},
	     {"/project/a.js", "/project/sub/a.ts"}},
	    {"recursive wildcard", {"**/*.ts"}, "/project", Usage::Files, true,
	     {"/project/a.ts", "/project/sub/a.ts", "/project/sub/deep/a.ts"},
	     {"/project/a.js"}},
	    {"exclude pattern", {"node_modules"}, "/project", Usage::Exclude, true,
	     {"/project/node_modules/foo"},
	     {"/project/node_modules", "/project/src"}},
	    {"case insensitive", {"*.ts"}, "/project", Usage::Files, false,
	     {"/project/A.TS", "/project/B.Ts"}, {"/project/a.js"}},
	    {"multiple specs", {"*.ts", "*.tsx"}, "/project", Usage::Files, true,
	     {"/project/a.ts", "/project/b.tsx"}, {"/project/a.js"}},
	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			auto matcher = NewSpecMatcher(tc.specs, tc.basePath, tc.usage,
			                              tc.useCaseSensitiveFileNames);
			if (matcher == nullptr) {
				t->Fatal({"matcher should not be nil"});
			}
			for (auto& path : tc.matchingPaths) {
				assert::Assert(t, matcher->MatchString(path),
				               "should match: " + path);
			}
			for (auto& path : tc.nonMatchingPaths) {
				assert::Assert(t, !matcher->MatchString(path),
				               "should not match: " + path);
			}
			return;
		});
	}
}

void TestSpecMatcher_MatchString(T* t) {
	t->Parallel();
	struct row {
		const char* name;
		std::vector<std::string> specs;
		const char* basePath;
		Usage usage;
		bool useCaseSensitiveFileNames;
		std::vector<std::string> paths;
		std::vector<bool> expected;
	};
	static const row cases[] = {
	    {"simple wildcard files", {"*.ts"}, "/project", Usage::Files, true,
	     {"/project/a.ts", "/project/sub/a.ts", "/project/a.js"},
	     {true, false, false}},
	    {"recursive wildcard files", {"**/*.ts"}, "/project", Usage::Files, true,
	     {"/project/a.ts", "/project/sub/a.ts", "/project/a.js"},
	     {true, true, false}},
	    {"exclude pattern matches prefix", {"node_modules"}, "/project",
	     Usage::Exclude, true,
	     {"/project/node_modules", "/project/node_modules/foo",
	      "/project/src"},
	     {false, true, false}},
	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			assert::Equal(t, tc.paths.size(), tc.expected.size());
			auto m = NewSpecMatcher(tc.specs, tc.basePath, tc.usage,
			                        tc.useCaseSensitiveFileNames);
			assert::Assert(t, m != nullptr);
			for (size_t i = 0; i < tc.paths.size(); i++) {
				assert::Equal(t, m->MatchString(tc.paths[i]), tc.expected[i],
				              "path: " + tc.paths[i]);
			}
			return;
		});
	}
}

void TestSingleSpecMatcher_MatchString(T* t) {
	t->Parallel();
	struct row {
		const char* name;
		const char* spec;
		const char* basePath;
		Usage usage;
		bool useCaseSensitiveFileNames;
		std::vector<std::string> paths;
		std::vector<bool> expected;
	};
	static const row cases[] = {
	    {"single spec wildcard", "*.ts", "/project", Usage::Files, true,
	     {"/project/a.ts", "/project/sub/a.ts", "/project/a.js"},
	     {true, false, false}},
	    {"single spec trailing starstar exclude allowed", "**", "/project",
	     Usage::Exclude, true, {"/project/a.ts", "/project/sub/a.ts"},
	     {true, true}},
	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			assert::Equal(t, tc.paths.size(), tc.expected.size());
			auto m = NewSpecMatcher({std::string(tc.spec)}, tc.basePath, tc.usage,
			                        tc.useCaseSensitiveFileNames);
			assert::Assert(t, m != nullptr);
			for (size_t i = 0; i < tc.paths.size(); i++) {
				assert::Equal(t, m->MatchString(tc.paths[i]), tc.expected[i],
				              "path: " + tc.paths[i]);
			}
			return;
		});
	}
}

void TestSpecMatchers_MatchIndex(T* t) {
	t->Parallel();
	struct row {
		const char* name;
		std::vector<std::string> specs;
		const char* basePath;
		Usage usage;
		bool useCaseSensitiveFileNames;
		std::vector<std::string> paths;
		std::vector<int> expected;
	};
	static const row cases[] = {
	    {"index lookup prefers first match", {"*.ts", "*.tsx"}, "/project",
	     Usage::Files, true,
	     {"/project/a.ts", "/project/a.tsx", "/project/a.js"}, {0, 1, -1}},
	    {"exclude index lookup", {"node_modules", "bower_components"},
	     "/project", Usage::Exclude, true,
	     {"/project/node_modules", "/project/node_modules/foo",
	      "/project/bower_components", "/project/bower_components/bar",
	      "/project/src"},
	     {-1, 0, -1, 1, -1}},
	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			assert::Equal(t, tc.paths.size(), tc.expected.size());
			auto m = NewSpecMatcher(tc.specs, tc.basePath, tc.usage,
			                        tc.useCaseSensitiveFileNames);
			assert::Assert(t, m != nullptr);
			for (size_t i = 0; i < tc.paths.size(); i++) {
				assert::Equal(t, m->MatchIndex(tc.paths[i]), tc.expected[i],
				              "path: " + tc.paths[i]);
			}
			return;
		});
	}
}

void TestSingleSpecMatcher(T* t) {
	t->Parallel();
	struct row {
		const char* name;
		const char* spec;
		const char* basePath;
		Usage usage;
		bool useCaseSensitiveFileNames;
		bool expectNil;
		std::vector<std::string> matchingPaths;
		std::vector<std::string> nonMatchingPaths;
	};
	static const row cases[] = {
	    {"simple spec", "*.ts", "/project", Usage::Files, true, false,
	     {"/project/a.ts"}, {"/project/a.js"}},
	    {"trailing ** non-exclude returns nil", "**", "/project", Usage::Files,
	     true, true, {}, {}},
	    {"trailing ** exclude works", "**", "/project", Usage::Exclude, true,
	     false, {"/project/anything", "/project/deep/path"}, {}},
	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			auto matcher =
			    NewSpecMatcher({std::string(tc.spec)}, tc.basePath, tc.usage,
			                   tc.useCaseSensitiveFileNames);
			if (tc.expectNil) {
				assert::Assert(t, matcher == nullptr, "should be nil");
				return;
			}
			if (matcher == nullptr) {
				t->Fatal({"matcher should not be nil"});
			}
			for (auto& path : tc.matchingPaths) {
				assert::Assert(t, matcher->MatchString(path),
				               "should match: " + path);
			}
			for (auto& path : tc.nonMatchingPaths) {
				assert::Assert(t, !matcher->MatchString(path),
				               "should not match: " + path);
			}
			return;
		});
	}
}

void TestSpecMatchers(T* t) {
	t->Parallel();
	struct row {
		const char* name;
		std::vector<std::string> specs;
		const char* basePath;
		Usage usage;
		bool useCaseSensitiveFileNames;
		bool expectNil;
		std::vector<std::pair<std::string, int>> pathToIndex;
	};
	static const row cases[] = {
	    {"multiple specs return correct index", {"*.ts", "*.tsx", "*.js"},
	     "/project", Usage::Files, true, false,
	     {{"/project/a.ts", 0},
	      {"/project/b.tsx", 1},
	      {"/project/c.js", 2},
	      {"/project/d.css", -1}}},
	    {"empty specs returns nil", {}, "/project", Usage::Files, true, true,
	     {}},
	};
	for (auto& tc : cases) {
		t->Run(tc.name, [tc](T* t) {
			t->Parallel();
			auto matchers = NewSpecMatcher(tc.specs, tc.basePath, tc.usage,
			                               tc.useCaseSensitiveFileNames);
			if (tc.expectNil) {
				assert::Assert(t, matchers == nullptr, "should be nil");
				return;
			}
			if (matchers == nullptr) {
				t->Fatal({"matchers should not be nil"});
			}
			for (auto& [path, expectedIndex] : tc.pathToIndex) {
				int gotIndex = matchers->MatchIndex(path);
				assert::Equal(t, gotIndex, expectedIndex, "path: " + path);
			}
			return;
		});
	}
}

// TestGlobPatternInternals — internal glob pattern matching logic edge
// cases.
void TestGlobPatternInternals(T* t) {
	t->Parallel();

	t->Run("nextPathPart handles consecutive slashes", [](T* t) {
		t->Parallel();
		std::string path = "/dev//foo///bar";

		auto [part, offset, ok] = vfsmatch::nextPathPartParts(path, "", 0);
		assert::Assert(t, ok);
		assert::Equal(t, part, "");
		assert::Equal(t, offset, 1);

		std::tie(part, offset, ok) = vfsmatch::nextPathPartParts(path, "", 1);
		assert::Assert(t, ok);
		assert::Equal(t, part, "dev");

		std::tie(part, offset, ok) = vfsmatch::nextPathPartParts(path, "", offset);
		assert::Assert(t, ok);
		assert::Equal(t, part, "foo");

		std::tie(part, std::ignore, ok) =
		    vfsmatch::nextPathPartParts(path, "", offset);
		assert::Assert(t, ok);
		assert::Equal(t, part, "bar");
		return;
	});

	t->Run("nextPathPart handles path ending with slashes", [](T* t) {
		t->Parallel();
		std::string path = "/dev/";

		auto [part, offset, ok] = vfsmatch::nextPathPartParts(path, "", 0);
		assert::Assert(t, ok);
		std::tie(part, offset, ok) = vfsmatch::nextPathPartParts(path, "", offset);
		assert::Assert(t, ok);
		std::tie(part, std::ignore, ok) =
		    vfsmatch::nextPathPartParts(path, "", offset);
		assert::Assert(t, !ok);
		return;
	});

	t->Run("nextPathPartParts handles empty prefix", [](T* t) {
		t->Parallel();
		std::string path = "/dev//foo";

		auto [part, offset, ok] = vfsmatch::nextPathPartParts("", path, 0);
		assert::Assert(t, ok);
		assert::Equal(t, part, "");
		assert::Equal(t, offset, 1);

		std::tie(part, offset, ok) = vfsmatch::nextPathPartParts("", path, offset);
		assert::Assert(t, ok);
		assert::Equal(t, part, "dev");

		std::tie(part, std::ignore, ok) =
		    vfsmatch::nextPathPartParts("", path, offset);
		assert::Assert(t, ok);
		assert::Equal(t, part, "foo");
		return;
	});

	t->Run("nextPathPartParts returns not ok when only slashes remain",
	       [](T* t) {
		t->Parallel();
		std::string prefix = "/dev/";
		std::string suffix = "foo";

		auto [part, offset, ok] = vfsmatch::nextPathPartParts(prefix, suffix, 0);
		assert::Assert(t, ok);

		std::tie(part, offset, ok) =
		    vfsmatch::nextPathPartParts(prefix, suffix, offset);
		assert::Assert(t, ok);
		assert::Equal(t, part, "dev");

		std::tie(part, offset, ok) =
		    vfsmatch::nextPathPartParts(prefix, suffix, offset);
		assert::Assert(t, ok);
		assert::Equal(t, part, "foo");
		assert::Equal(t, offset, static_cast<int>(prefix.size() + suffix.size()));

		std::tie(part, std::ignore, ok) =
		    vfsmatch::nextPathPartParts(prefix, suffix, offset);
		assert::Assert(t, !ok);
		return;
	});

	t->Run("nextPathPartParts parses from suffix region", [](T* t) {
		t->Parallel();
		std::string prefix = "/";
		std::string suffix = "a";

		auto [part, offset, ok] = vfsmatch::nextPathPartParts(prefix, suffix, 0);
		assert::Assert(t, ok);
		assert::Equal(t, part, "");
		assert::Equal(t, offset, 1);

		std::tie(part, std::ignore, ok) =
		    vfsmatch::nextPathPartParts(prefix, suffix, offset);
		assert::Assert(t, ok);
		assert::Equal(t, part, "a");
		return;
	});

	t->Run("question mark segment at end of string", [](T* t) {
		t->Parallel();
		auto [p, ok] =
		    vfsmatch::compileGlobPattern("a?", "/", Usage::Files, true);
		assert::Assert(t, ok);

		assert::Assert(t, p.matches("/ab"));
		assert::Assert(t, !p.matches("/a"));
		return;
	});

	t->Run("star segment with complex pattern", [](T* t) {
		t->Parallel();
		auto [p, ok] =
		    vfsmatch::compileGlobPattern("a*b*c", "/", Usage::Files, true);
		assert::Assert(t, ok);

		assert::Assert(t, p.matches("/abc"));
		assert::Assert(t, p.matches("/aXbYc"));
		assert::Assert(t, p.matches("/aXXXbYYYc"));
		assert::Assert(t, !p.matches("/aXbY"));
		return;
	});

	t->Run("ensureTrailingSlash with existing slash", [](T* t) {
		t->Parallel();
		std::string result = vfsmatch::ensureTrailingSlash("/dev/");
		assert::Equal(t, result, "/dev/");

		result = vfsmatch::ensureTrailingSlash("/");
		assert::Equal(t, result, "/");
		return;
	});

	t->Run("ensureTrailingSlash with empty string", [](T* t) {
		t->Parallel();
		std::string result = vfsmatch::ensureTrailingSlash("");
		assert::Equal(t, result, "");
		return;
	});

	t->Run("literal component with package folder in include", [](T* t) {
		t->Parallel();
		auto host = vfstest::FromMap(
		    std::unordered_map<std::string, vfstest::MapFileInput>{
		        {"/dev/node_modules/pkg/index.ts", ""}},
		    false);

		auto got = matchFiles("/dev", {".ts"}, {},
		                      {"node_modules/pkg/index.ts"}, false, "/",
		                      UnlimitedDepth, host.get());
		assert::Assert(t,
		               slicesContains(got, "/dev/node_modules/pkg/index.ts"));
		return;
	});
}

// TestMatchSegmentsEdgeCases — edge cases in the matchSegments function.
void TestMatchSegmentsEdgeCases(T* t) {
	t->Parallel();

	t->Run("question mark before slash in string", [](T* t) {
		t->Parallel();
		// ? should not match "/"; tested within a single component.
		auto [p, ok] =
		    vfsmatch::compileGlobPattern("a?b", "/", Usage::Files, true);
		assert::Assert(t, ok);

		assert::Assert(t, p.matches("/aXb"));    // X matches ?
		assert::Assert(t, !p.matches("/ab"));    // nothing to match ?
		assert::Assert(t, !p.matches("/aXYb"));  // too many chars for ?
		return;
	});

	t->Run("star with no trailing content", [](T* t) {
		t->Parallel();
		auto [p, ok] =
		    vfsmatch::compileGlobPattern("a*", "/", Usage::Files, true);
		assert::Assert(t, ok);

		assert::Assert(t, p.matches("/a"));
		assert::Assert(t, p.matches("/abc"));
		assert::Assert(t, p.matches("/aXYZ"));
		return;
	});

	t->Run("multiple stars in pattern", [](T* t) {
		t->Parallel();
		auto [p, ok] =
		    vfsmatch::compileGlobPattern("*a*", "/", Usage::Files, true);
		assert::Assert(t, ok);

		assert::Assert(t, p.matches("/a"));
		assert::Assert(t, p.matches("/Xa"));
		assert::Assert(t, p.matches("/aX"));
		assert::Assert(t, p.matches("/XaY"));
		assert::Assert(t, !p.matches("/XYZ"));  // no 'a'
		return;
	});

	t->Run("multiple stars requiring backtracking", [](T* t) {
		t->Parallel();
		auto [p1, ok1] =
		    vfsmatch::compileGlobPattern("*a*a", "/", Usage::Files, true);
		assert::Assert(t, ok1);
		assert::Assert(t, p1.matches("/aa"));
		assert::Assert(t, p1.matches("/Xaa"));
		assert::Assert(t, p1.matches("/aXa"));
		assert::Assert(t, p1.matches("/XaYa"));
		assert::Assert(t, p1.matches("/aaaa"));
		assert::Assert(t, !p1.matches("/a"));
		assert::Assert(t, !p1.matches("/Xa"));
		assert::Assert(t, !p1.matches("/aX"));
		assert::Assert(t, !p1.matches("/XaYaZ"));

		auto [p2, ok2] =
		    vfsmatch::compileGlobPattern("*a*b*c", "/", Usage::Files, true);
		assert::Assert(t, ok2);
		assert::Assert(t, p2.matches("/abc"));
		assert::Assert(t, p2.matches("/XaYbZc"));
		assert::Assert(t, p2.matches("/aXbYc"));
		assert::Assert(t, p2.matches("/aaabbbccc"));
		assert::Assert(t, !p2.matches("/ab"));
		assert::Assert(t, !p2.matches("/ac"));
		assert::Assert(t, !p2.matches("/cba"));
		assert::Assert(t, !p2.matches("/abcX"));

		auto [p3, ok3] =
		    vfsmatch::compileGlobPattern("*a*a*a", "/", Usage::Files, true);
		assert::Assert(t, ok3);
		assert::Assert(t, p3.matches("/aaa"));
		assert::Assert(t, p3.matches("/aXaYa"));
		assert::Assert(t, p3.matches("/XaYaZa"));
		assert::Assert(t, !p3.matches("/aa"));
		assert::Assert(t, !p3.matches("/aaX"));

		auto [p4, ok4] =
		    vfsmatch::compileGlobPattern("a*b*a", "/", Usage::Files, true);
		assert::Assert(t, ok4);
		assert::Assert(t, p4.matches("/aba"));
		assert::Assert(t, p4.matches("/aXbYa"));
		assert::Assert(t, p4.matches("/abba"));
		assert::Assert(t, !p4.matches("/ab"));
		assert::Assert(t, !p4.matches("/aba "));
		assert::Assert(t, !p4.matches("/Xaba"));
		return;
	});

	t->Run("pathological pattern performance", [](T* t) {
		t->Parallel();
		auto [p, ok] = vfsmatch::compileGlobPattern("*a*a*a*a*b", "/",
		                                            Usage::Files, true);
		assert::Assert(t, ok);

		assert::Assert(t, !p.matches("/aaaaaaaaaaaaaaaa"));
		assert::Assert(t, !p.matches("/aaaaaaaaaaaaaaaaX"));
		assert::Assert(t, p.matches("/aaaab"));
		assert::Assert(t, p.matches("/XaYaZaWab"));
		return;
	});

	t->Run("literal segment not matching", [](T* t) {
		t->Parallel();
		auto [p, ok] = vfsmatch::compileGlobPattern("abcdefgh.ts", "/",
		                                            Usage::Files, true);
		assert::Assert(t, ok);

		assert::Assert(t, !p.matches("/abc.ts"));
		assert::Assert(t, p.matches("/abcdefgh.ts"));
		return;
	});

	t->Run("question mark matches multi-byte unicode rune", [](T* t) {
		t->Parallel();
		// ? matches one Unicode codepoint, not one byte.

		auto [p1, ok1] =
		    vfsmatch::compileGlobPattern("?.ts", "/", Usage::Files, true);
		assert::Assert(t, ok1);

		assert::Assert(t, p1.matches("/a.ts"));
		assert::Assert(t, p1.matches("/\u00e9.ts"));
		assert::Assert(t, p1.matches("/中.ts"));
		assert::Assert(t, p1.matches("/\U0001f389.ts"));
		assert::Assert(t, !p1.matches("/.ts"));
		assert::Assert(t, !p1.matches("/ab.ts"));

		auto [p2, ok2] =
		    vfsmatch::compileGlobPattern("??.ts", "/", Usage::Files, true);
		assert::Assert(t, ok2);

		assert::Assert(t, p2.matches("/ab.ts"));
		assert::Assert(t, p2.matches("/é中.ts"));
		assert::Assert(t, p2.matches("/\U0001f389\u00e9.ts"));
		assert::Assert(t, !p2.matches("/a.ts"));
		assert::Assert(t, !p2.matches("/abc.ts"));
		return;
	});

	t->Run("star matches multi-byte unicode runes correctly", [](T* t) {
		t->Parallel();
		auto [p, ok] = vfsmatch::compileGlobPattern("*\u00e9.ts", "/",
		                                            Usage::Files, true);
		assert::Assert(t, ok);

		assert::Assert(t, p.matches("/\u00e9.ts"));
		assert::Assert(t, p.matches("/caf\u00e9.ts"));
		assert::Assert(t, !p.matches("/cafe.ts"));

		auto [p2, ok2] = vfsmatch::compileGlobPattern("*\U0001f389*", "/",
		                                              Usage::Files, true);
		assert::Assert(t, ok2);

		assert::Assert(t, p2.matches("/\U0001f389"));
		assert::Assert(t, p2.matches("/a\U0001f389b"));
		assert::Assert(t, !p2.matches("/abc"));
		return;
	});
}

// TestReadDirectoryConsecutiveSlashes — consecutive slashes normalize fine.
void TestReadDirectoryConsecutiveSlashes(T* t) {
	t->Parallel();

	auto host = vfstest::FromMap(
	    std::unordered_map<std::string, vfstest::MapFileInput>{
	        {"/dev/a.ts", ""}, {"/dev/x/b.ts", ""}},
	    false);

	auto got = matchFiles("/dev", {".ts"}, {}, {"**/*.ts"}, false, "/",
	                      UnlimitedDepth, host.get());
	assert::Assert(t, got.size() >= 2, "should find files");
	assert::Assert(t, slicesContains(got, "/dev/a.ts"));
	assert::Assert(t, slicesContains(got, "/dev/x/b.ts"));
}

// TestGlobPatternLiteralWithPackageFolders — literal components vs package
// folder skipping.
void TestGlobPatternLiteralWithPackageFolders(T* t) {
	t->Parallel();

	t->Run("wildcard skips package folders", [](T* t) {
		t->Parallel();
		auto host = vfstest::FromMap(
		    std::unordered_map<std::string, vfstest::MapFileInput>{
		        {"/dev/a.ts", ""}, {"/dev/node_modules/b.ts", ""}},
		    false);

		auto got = matchFiles("/dev", {".ts"}, {}, {"*/*.ts"}, false, "/",
		                      UnlimitedDepth, host.get());
		assert::Assert(t, !slicesContains(got, "/dev/node_modules/b.ts"),
		               "should skip node_modules with wildcard");
		return;
	});

	t->Run("explicit literal includes package folder", [](T* t) {
		t->Parallel();
		auto host = vfstest::FromMap(
		    std::unordered_map<std::string, vfstest::MapFileInput>{
		        {"/dev/node_modules/b.ts", ""}},
		    false);

		auto got = matchFiles("/dev", {".ts"}, {}, {"node_modules/b.ts"},
		                      false, "/", UnlimitedDepth, host.get());
		assert::Assert(t, slicesContains(got, "/dev/node_modules/b.ts"),
		               "should include explicit node_modules path");
		return;
	});
}

// TestGetBasePathsCaseSensitivity — getBasePaths must respect FS case
// sensitivity when deduping base paths.
void TestGetBasePathsCaseSensitivity(T* t) {
	t->Parallel();

	t->Run("case-sensitive does not dedup differently-cased paths", [](T* t) {
		t->Parallel();
		auto basePaths = vfsmatch::getBasePaths(
		    "/root", {"../Other/**/*.ts", "../other/**/*.ts"}, true);
		assert::Assert(t, slicesContains(basePaths, "/Other"),
		               "expected /Other in base paths");
		assert::Assert(t, slicesContains(basePaths, "/other"),
		               "expected /other in base paths");
		return;
	});

	t->Run("case-insensitive dedups differently-cased paths", [](T* t) {
		t->Parallel();
		auto basePaths = vfsmatch::getBasePaths(
		    "/root", {"../Other/**/*.ts", "../other/**/*.ts"}, false);
		int count = 0;
		for (auto& bp : basePaths) {
			if (bp == "/Other" || bp == "/other") {
				count++;
			}
		}
		assert::Assert(t, count <= 1,
		               "expected at most one of /Other or /other");
		return;
	});
}

}  // namespace

REGISTER_UNIT_TEST("vfsmatch.TestReadDirectory", TestReadDirectory);
REGISTER_UNIT_TEST("vfsmatch.TestIsImplicitGlob", TestIsImplicitGlob);
REGISTER_UNIT_TEST("vfsmatch.TestReadDirectoryEdgeCases",
                   TestReadDirectoryEdgeCases);
REGISTER_UNIT_TEST("vfsmatch.TestReadDirectoryEmptyIncludes",
                   TestReadDirectoryEmptyIncludes);
REGISTER_UNIT_TEST("vfsmatch.TestReadDirectorySymlinkCycle",
                   TestReadDirectorySymlinkCycle);
REGISTER_UNIT_TEST("vfsmatch.TestReadDirectoryMatchesTypeScriptBaselines",
                   TestReadDirectoryMatchesTypeScriptBaselines);
REGISTER_UNIT_TEST("vfsmatch.TestSpecMatcher", TestSpecMatcher);
REGISTER_UNIT_TEST("vfsmatch.TestReadDirectoryWithExtendedDynamicRoot",
                   TestReadDirectoryWithExtendedDynamicRoot);
REGISTER_UNIT_TEST("vfsmatch.TestDynamicAbsoluteGlobUsesCaseSensitivePattern",
                   TestDynamicAbsoluteGlobUsesCaseSensitivePattern);
REGISTER_UNIT_TEST("vfsmatch.TestSpecMatcher_MatchString",
                   TestSpecMatcher_MatchString);
REGISTER_UNIT_TEST("vfsmatch.TestSingleSpecMatcher_MatchString",
                   TestSingleSpecMatcher_MatchString);
REGISTER_UNIT_TEST("vfsmatch.TestSpecMatchers_MatchIndex",
                   TestSpecMatchers_MatchIndex);
REGISTER_UNIT_TEST("vfsmatch.TestSingleSpecMatcher", TestSingleSpecMatcher);
REGISTER_UNIT_TEST("vfsmatch.TestSpecMatchers", TestSpecMatchers);
REGISTER_UNIT_TEST("vfsmatch.TestGlobPatternInternals",
                   TestGlobPatternInternals);
REGISTER_UNIT_TEST("vfsmatch.TestMatchSegmentsEdgeCases",
                   TestMatchSegmentsEdgeCases);
REGISTER_UNIT_TEST("vfsmatch.TestReadDirectoryConsecutiveSlashes",
                   TestReadDirectoryConsecutiveSlashes);
REGISTER_UNIT_TEST("vfsmatch.TestGlobPatternLiteralWithPackageFolders",
                   TestGlobPatternLiteralWithPackageFolders);
REGISTER_UNIT_TEST("vfsmatch.TestGetBasePathsCaseSensitivity",
                   TestGetBasePathsCaseSensitivity);
