// Port of small enums from tsc/internal/core (languagevariant.go, scriptkind.go,
// compileroptions.go bits needed by the front end).
#pragma once

#include <cstdint>

namespace tsc {

enum class LanguageVariant : int32_t {
	Standard = 0,
	JSX = 1,
};

enum class ScriptKind : int32_t {
	Unknown = 0,
	JS = 1,
	JSX = 2,
	TS = 3,
	TSX = 4,
	JSON = 6,
	Deferred = 7,
};

enum class ScriptTarget : int32_t {
	None = 0,
	ES5 = 1,
	ES2015 = 2,
	ES2016 = 3,
	ES2017 = 4,
	ES2018 = 5,
	ES2019 = 6,
	ES2020 = 7,
	ES2021 = 8,
	ES2022 = 9,
	ES2023 = 10,
	ES2024 = 11,
	ES2025 = 12,
	ESNext = 99,
	LatestStandard = ES2025,
};

enum class Tristate : int32_t {
	False = 0,
	True = 1,
	Unknown = 2,
};

enum class ModuleKind : int32_t {
	None = 0,
	CommonJS = 1,
	AMD = 2,
	UMD = 3,
	System = 4,
	ES2015 = 5,
	ES2020 = 6,
	ES2022 = 7,
	ESNext = 8,
	Node16 = 9,
	Node18 = 10,
	NodeNext = 11,
	Preserve = 12,
	Bundler = 100,
};

enum class ModuleDetectionKind : int32_t {
	None = 0,
	Auto = 1,
	Legacy = 2,
	Force = 3,
};

enum class JsxEmit : int32_t {
	None = 0,
	Preserve = 1,
	React = 2,
	ReactNative = 3,
	ReactJSX = 4,
	ReactJSXDev = 5,
};

enum class ResolutionMode : int32_t {
	None = 0,
	ESM = 1,
	CommonJS = 2,
};

}  // namespace tsc
