// Port of tsc/internal/tsoptions/enummaps.go — OrderedMap[string,any] enum
// tables. LibMap/Libs/LibFilesSet/targetToLibMap/getLibFileName/
// getDefaultLibFileName already live inline in tsoptions.h; this TU adds the
// OrderedMap[string,any]-shaped views (values stored as int64 like Go's
// reflect view of the int32 enum kinds) plus the watch-* enum maps.
#include "internal/tsoptions/tsoptions.h"

namespace tsc::tsoptions {

namespace {

JsonObject enumMapFromEntries(
    std::initializer_list<std::pair<std::string_view, CompilerOptionsValue>>
        entries) {
	JsonObject m;
	for (auto& e : entries) {
		m.Set(std::string(e.first), e.second);
	}
	return m;
}

}  // namespace

// LibMap as *collections.OrderedMap[string, any] — enummaps.go:11. Built from
// the header's libMapEntries so both views stay in lockstep.
const JsonObject& libEnumMap() {
	static const JsonObject m = [] {
		JsonObject m;
		for (const auto& e : libMapEntries) {
			m.Set(std::string(e.first), std::string(e.second));
		}
		return m;
	}();
	return m;
}

const JsonObject& moduleResolutionOptionMap() {
	static const JsonObject m = enumMapFromEntries({
		{"node16", ModuleResolutionKind::Node16},
		{"nodenext", ModuleResolutionKind::NodeNext},
		{"bundler", ModuleResolutionKind::Bundler},
		{"classic", ModuleResolutionKind::Classic},
		{"node", ModuleResolutionKind::Node10},
		{"node10", ModuleResolutionKind::Node10},
	});
	return m;
}

const JsonObject& targetOptionMap() {
	static const JsonObject m = enumMapFromEntries({
		{"es5", ScriptTarget::ES5},
		{"es6", ScriptTarget::ES2015},
		{"es2015", ScriptTarget::ES2015},
		{"es2016", ScriptTarget::ES2016},
		{"es2017", ScriptTarget::ES2017},
		{"es2018", ScriptTarget::ES2018},
		{"es2019", ScriptTarget::ES2019},
		{"es2020", ScriptTarget::ES2020},
		{"es2021", ScriptTarget::ES2021},
		{"es2022", ScriptTarget::ES2022},
		{"es2023", ScriptTarget::ES2023},
		{"es2024", ScriptTarget::ES2024},
		{"es2025", ScriptTarget::ES2025},
		{"es2026", ScriptTarget::ES2026},
		{"esnext", ScriptTarget::ESNext},
	});
	return m;
}

const JsonObject& moduleOptionMap() {
	static const JsonObject m = enumMapFromEntries({
		{"commonjs", ModuleKind::CommonJS},
		{"amd", ModuleKind::AMD},
		{"system", ModuleKind::System},
		{"umd", ModuleKind::UMD},
		{"es6", ModuleKind::ES2015},
		{"es2015", ModuleKind::ES2015},
		{"es2020", ModuleKind::ES2020},
		{"es2022", ModuleKind::ES2022},
		{"esnext", ModuleKind::ESNext},
		{"node16", ModuleKind::Node16},
		{"node18", ModuleKind::Node18},
		{"node20", ModuleKind::Node20},
		{"nodenext", ModuleKind::NodeNext},
		{"preserve", ModuleKind::Preserve},
	});
	return m;
}

const JsonObject& moduleDetectionOptionMap() {
	static const JsonObject m = enumMapFromEntries({
		{"auto", ModuleDetectionKind::Auto},
		{"legacy", ModuleDetectionKind::Legacy},
		{"force", ModuleDetectionKind::Force},
	});
	return m;
}

const JsonObject& jsxOptionMap() {
	static const JsonObject m = enumMapFromEntries({
		{"preserve", JsxEmit::Preserve},
		{"react-native", JsxEmit::ReactNative},
		{"react-jsx", JsxEmit::ReactJSX},
		{"react-jsxdev", JsxEmit::ReactJSXDev},
		{"react", JsxEmit::React},
	});
	return m;
}

const JsonObject& newLineOptionMap() {
	static const JsonObject m = enumMapFromEntries({
		{"crlf", NewLineKind::CarriageReturnLineFeed},
		{"lf", NewLineKind::LineFeed},
	});
	return m;
}

const JsonObject& watchFileEnumMap() {
	static const JsonObject m = enumMapFromEntries({
		{"fixedpollinginterval", WatchFileKind::FixedPollingInterval},
		{"prioritypollinginterval", WatchFileKind::PriorityPollingInterval},
		{"dynamicprioritypolling", WatchFileKind::DynamicPriorityPolling},
		{"fixedchunksizepolling", WatchFileKind::FixedChunkSizePolling},
		{"usefsevents", WatchFileKind::UseFsEvents},
		{"usefseventsonparentdirectory",
		 WatchFileKind::UseFsEventsOnParentDirectory},
	});
	return m;
}

const JsonObject& watchDirectoryEnumMap() {
	static const JsonObject m = enumMapFromEntries({
		{"usefsevents", WatchDirectoryKind::UseFsEvents},
		{"fixedpollinginterval", WatchDirectoryKind::FixedPollingInterval},
		{"dynamicprioritypolling", WatchDirectoryKind::DynamicPriorityPolling},
		{"fixedchunksizepolling", WatchDirectoryKind::FixedChunkSizePolling},
	});
	return m;
}

const JsonObject& fallbackEnumMap() {
	static const JsonObject m = enumMapFromEntries({
		{"fixedinterval", PollingKind::FixedInterval},
		{"priorityinterval", PollingKind::PriorityInterval},
		{"dynamicpriority", PollingKind::DynamicPriority},
		{"fixedchunksize", PollingKind::FixedChunkSize},
	});
	return m;
}

}  // namespace tsc::tsoptions
