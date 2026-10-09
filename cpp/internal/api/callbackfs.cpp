// Callback filesystem — callbackfs.go.
#include "internal/api/callbackfs.h"

#include "internal/api/proto.h"
#include "internal/tspath/tspath.h"

#include <chrono>
#include <optional>
#include <stdexcept>

namespace tsc::api {
namespace {

bool stringEndsWith(std::string_view s, std::string_view suffix) {
	return s.size() >= suffix.size() &&
	       s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool containsString(const std::vector<std::string>& v, std::string_view s) {
	for (const auto& x : v) {
		if (x == s) return true;
	}
	return false;
}

// callbackResponse — callbackfs.go:127.
struct callbackResponse {
	std::string Kind;      // `json:"kind"`
	json::Value Value;     // `json:"value"`

	std::string unmarshalJSONFrom(json::Decoder& d) {
		return tsc::api::readFields(d, [this](std::string_view n, json::Decoder& f) -> std::string {
			if (tsc::api::fieldIs(n, "kind")) {
				return json::unmarshalDecode(f, &Kind);
			}
			if (tsc::api::fieldIs(n, "value")) {
				return json::unmarshalDecode(f, &Value);
			}
			return f.skipValue();
		});
	}
};

// decodeCallbackResponse — callbackfs.go:132.
callbackResponse decodeCallbackResponse(std::string_view name,
                                        const json::Value& result) {
	callbackResponse response;
	if (auto e = json::unmarshal(result, &response); !e.empty()) {
		throw std::runtime_error(e);
	}
	if (response.Kind.empty()) {
		throw std::runtime_error("filesystem callback response is missing a kind");
	}
	if (response.Kind == "error") {
		throw std::runtime_error("filesystem callback returned serverFS.error: " +
		                         std::string(name));
	}
	return response;
}

// invalidCallbackResponse — callbackfs.go:149.
[[noreturn]] void invalidCallbackResponse(std::string_view name,
                                          const callbackResponse& response) {
	throw std::runtime_error("invalid " + std::string(name) +
	                         " callback response kind: " + response.Kind);
}

// RFC3339Nano time.Parse — parses "YYYY-MM-DDTHH:MM:SS[.frac](Z|±HH:MM)" into
// a system_clock time_point; throws on any malformed input (Go panic parity).
vfs::TimePoint parseRFC3339Nano(const std::string& s) {
	auto bad = [&s]() -> vfs::TimePoint {
		throw std::runtime_error(
		    "parsing time \"" + s + "\" as RFC3339: cannot parse");
	};
	size_t i = 0;
	auto num = [&](int n) -> int {
		if (i + n > s.size()) bad();
		int v = 0;
		for (int k = 0; k < n; k++) {
			if (s[i] < '0' || s[i] > '9') bad();
			v = v * 10 + (s[i] - '0');
			i++;
		}
		return v;
	};
	auto lit = [&](char c) {
		if (i >= s.size() || s[i] != c) bad();
		i++;
	};
	int year = num(4); lit('-');
	int mon = num(2); lit('-');
	int day = num(2); lit('T');
	int hour = num(2); lit(':');
	int min = num(2); lit(':');
	int sec = num(2);
	int64_t nanos = 0;
	if (i < s.size() && s[i] == '.') {
		i++;
		int digits = 0;
		while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
			if (digits < 9) nanos = nanos * 10 + (s[i] - '0');
			i++;
			digits++;
		}
		while (digits < 9) { nanos *= 10; digits++; }
	}
	int offsetMinutes = 0;
	if (i < s.size() && (s[i] == 'Z' || s[i] == 'z')) {
		i++;
	} else if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
		int sign = s[i] == '+' ? 1 : -1;
		i++;
		int oh = num(2); lit(':'); int om = num(2);
		offsetMinutes = sign * (oh * 60 + om);
	} else {
		bad();
	}
	if (i != s.size()) bad();
	// days-from-civil (Howard Hinnant) → seconds since epoch, UTC.
	auto daysFromCivil = [](int y, unsigned m, unsigned d) -> int64_t {
		y -= m <= 2;
		const int era = (y >= 0 ? y : y - 399) / 400;
		const unsigned yoe = static_cast<unsigned>(y - era * 400);
		const unsigned doy =
		    (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return static_cast<int64_t>(era) * 146097 + doe - 719468;
	};
	int64_t epochSec = daysFromCivil(year, mon, day) * 86400 + hour * 3600 +
	                   min * 60 + sec - offsetMinutes * 60;
	return vfs::TimePoint(std::chrono::duration_cast<vfs::TimePoint::duration>(
	    std::chrono::seconds(epochSec) + std::chrono::nanoseconds(nanos)));
}

// callbackFileInfo — callbackfs.go:309.
struct callbackFileInfo : vfs::FileInfo {
	std::string name;
	int64_t size = 0;
	vfs::FileMode mode{};
	vfs::TimePoint modTime{};

	std::string Name() const override { return name; }
	int64_t Size() const override { return size; }
	vfs::FileMode Mode() const override { return mode; }
	vfs::TimePoint ModTime() const override { return modTime; }
	bool IsDir() const override { return mode.IsDir(); }
	std::any Sys() const override { return {}; }
};

} // namespace

// nodeFileModeToGoFileMode — callbackfs.go:407.
vfs::FileMode nodeFileModeToGoFileMode(uint32_t mode) {
	vfs::FileMode result{mode & 0777u};
	if (mode & 04000u) result = vfs::FileMode{result.v | vfs::FileMode::kSetuid};
	if (mode & 02000u) result = vfs::FileMode{result.v | vfs::FileMode::kSetgid};
	if (mode & 01000u) result = vfs::FileMode{result.v | vfs::FileMode::kSticky};
	switch (mode & 0170000u) {
	case 0010000u:
		result = vfs::FileMode{result.v | vfs::FileMode::kNamedPipe};
		break;
	case 0020000u:
		result = vfs::FileMode{result.v | vfs::FileMode::kDevice |
		                       vfs::FileMode::kCharDevice};
		break;
	case 0040000u:
		result = vfs::FileMode{result.v | vfs::FileMode::kDir};
		break;
	case 0060000u:
		result = vfs::FileMode{result.v | vfs::FileMode::kDevice};
		break;
	case 0100000u:
		// Regular file.
		break;
	case 0120000u:
		result = vfs::FileMode{result.v | vfs::FileMode::kSymlink};
		break;
	case 0140000u:
		result = vfs::FileMode{result.v | vfs::FileMode::kSocket};
		break;
	default:
		result = vfs::FileMode{result.v | vfs::FileMode::kIrregular};
	}
	return result;
}

// isCallbackName — callbackfs.go:55.
bool isCallbackName(std::string_view name) {
	return name == callbackReadFile || name == callbackFileExists ||
	       name == callbackDirectoryExists ||
	       name == callbackGetAccessibleEntries || name == callbackRealpath ||
	       name == callbackStat || name == callbackWriteFile ||
	       name == callbackRemoveFile;
}

// newCallbackFS — callbackfs.go:70.
callbackFS::callbackFS(std::shared_ptr<vfs::FS> b,
                       const std::vector<std::string>& callbacks,
                       std::optional<bool> caseSensitive_)
    : base(std::move(b)),
      realpathIdentity(containsString(callbacks, "realpath:identity")),
      fakeStat(containsString(callbacks, "stat:fakeStat")),
      writeFileNoop(containsString(callbacks, "writeFile:noop")),
      removeFileNoop(containsString(callbacks, "removeFile:noop")),
      caseSensitive(caseSensitive_) {
	enabledCallbacks.reserve(callbacks.size());
	for (const auto& cb : callbacks) {
		if (stringEndsWith(cb, ":error")) {
			std::string name = cb.substr(0, cb.size() - 6);
			if (!isCallbackName(name)) {
				throw std::runtime_error("unknown callback name: " + name);
			}
			errorCallbacks.insert(name);
			continue;
		}
		if (cb == "realpath:identity" || cb == "stat:fakeStat" ||
		    cb == "writeFile:noop" || cb == "removeFile:noop") {
			continue;
		}
		if (!isCallbackName(cb)) {
			throw std::runtime_error("unknown callback name: " + cb);
		}
		enabledCallbacks.insert(cb);
	}
}

std::shared_ptr<callbackFS> newCallbackFS(
    std::shared_ptr<vfs::FS> base, const std::vector<std::string>& callbacks,
    std::optional<bool> caseSensitive) {
	return std::make_shared<callbackFS>(std::move(base), callbacks,
	                                    caseSensitive);
}

// SetConnection — callbackfs.go:99.
void callbackFS::SetConnection(gostd::Context c, std::shared_ptr<ipc::Conn> cn) {
	ctx = std::move(c);
	conn = std::move(cn);
}

// isEnabled — callbackfs.go:114.
bool callbackFS::isEnabled(std::string_view name) const {
	return enabledCallbacks.contains(std::string(name));
}

// call — callbackfs.go:119.
std::pair<json::Value, gostd::Error> callbackFS::call(std::string_view name,
                                                    const json::Value& arg) {
	if (!conn) {
		return {json::Value{},
		        gostd::errorf("CallbackFS: %s called before connection set",
		                      {std::string(name)})};
	}

	auto [result, err] = conn->Call(ctx, name, arg);
	if (err) {
		return {json::Value{}, err};
	}
	return {result, nullptr};
}

// panicIfError — callbackfs.go:157.
void callbackFS::panicIfError(std::string_view name) const {
	if (errorCallbacks.contains(std::string(name))) {
		throw std::runtime_error(
		    "filesystem operation configured with serverFS.error: " +
		    std::string(name));
	}
}

// UseCaseSensitiveFileNames — callbackfs.go:163.
bool callbackFS::UseCaseSensitiveFileNames() {
	if (caseSensitive.has_value()) {
		return *caseSensitive;
	}
	return base->UseCaseSensitiveFileNames();
}

// ReadFile — callbackfs.go:170.
std::pair<std::string, bool> callbackFS::ReadFile(const std::string& path) {
	panicIfError(callbackReadFile);
	if (isEnabled(callbackReadFile)) {
		auto [result, err] = call(callbackReadFile, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		callbackResponse response = decodeCallbackResponse(callbackReadFile, result);
		if (response.Kind == "value") {
			std::string content;
			if (auto e = json::unmarshal(response.Value, &content); !e.empty()) {
				throw std::runtime_error(e);
			}
			return {content, true};
		}
		if (response.Kind == "missing") {
			return {"", false};
		}
		if (response.Kind == "useOS") {
			return base->ReadFile(path);
		}
		invalidCallbackResponse(callbackReadFile, response);
	}
	return base->ReadFile(path);
}

// FileExists — callbackfs.go:197.
bool callbackFS::FileExists(const std::string& path) {
	panicIfError(callbackFileExists);
	if (isEnabled(callbackFileExists)) {
		auto [result, err] = call(callbackFileExists, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		callbackResponse response = decodeCallbackResponse(callbackFileExists, result);
		if (response.Kind == "value") {
			bool exists = false;
			if (auto e = json::unmarshal(response.Value, &exists); !e.empty()) {
				throw std::runtime_error(e);
			}
			return exists;
		}
		if (response.Kind == "useOS") {
			return base->FileExists(path);
		}
		invalidCallbackResponse(callbackFileExists, response);
	}
	return base->FileExists(path);
}

// DirectoryExists — callbackfs.go:222.
bool callbackFS::DirectoryExists(const std::string& path) {
	panicIfError(callbackDirectoryExists);
	if (isEnabled(callbackDirectoryExists)) {
		auto [result, err] =
		    call(callbackDirectoryExists, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		callbackResponse response =
		    decodeCallbackResponse(callbackDirectoryExists, result);
		if (response.Kind == "value") {
			bool exists = false;
			if (auto e = json::unmarshal(response.Value, &exists); !e.empty()) {
				throw std::runtime_error(e);
			}
			return exists;
		}
		if (response.Kind == "useOS") {
			return base->DirectoryExists(path);
		}
		invalidCallbackResponse(callbackDirectoryExists, response);
	}
	return base->DirectoryExists(path);
}

// GetAccessibleEntries — callbackfs.go:247.
vfs::Entries callbackFS::GetAccessibleEntries(const std::string& path) {
	panicIfError(callbackGetAccessibleEntries);
	if (isEnabled(callbackGetAccessibleEntries)) {
		auto [result, err] = call(callbackGetAccessibleEntries,
		                          json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		callbackResponse response =
		    decodeCallbackResponse(callbackGetAccessibleEntries, result);
		if (response.Kind == "value") {
			struct rawEntries {
				std::vector<std::string> Files;       // `json:"files"`
				std::vector<std::string> Directories; // `json:"directories"`
				std::optional<std::vector<std::string>> Symlinks; // `json:"symlinks"`
				std::string unmarshalJSONFrom(json::Decoder& d) {
					return tsc::api::readFields(d, [this](std::string_view n, json::Decoder& f) -> std::string {
						if (tsc::api::fieldIs(n, "files")) return json::unmarshalDecode(f, &Files);
						if (tsc::api::fieldIs(n, "directories")) return json::unmarshalDecode(f, &Directories);
						if (tsc::api::fieldIs(n, "symlinks")) return json::unmarshalDecode(f, &Symlinks);
						return f.skipValue();
					});
				}
			};
			rawEntries raw;
			if (auto e = json::unmarshal(response.Value, &raw); !e.empty()) {
				throw std::runtime_error(e);
			}
			vfs::Entries entries{raw.Files, raw.Directories, std::nullopt};
			if (raw.Symlinks.has_value()) {
				entries.symlinks = std::unordered_set<std::string>(
				    raw.Symlinks->begin(), raw.Symlinks->end());
			}
			return entries;
		}
		if (response.Kind == "useOS") {
			return base->GetAccessibleEntries(path);
		}
		invalidCallbackResponse(callbackGetAccessibleEntries, response);
	}
	return base->GetAccessibleEntries(path);
}

// Realpath — callbackfs.go:284.
std::string callbackFS::Realpath(const std::string& path) {
	panicIfError(callbackRealpath);
	if (isEnabled(callbackRealpath)) {
		auto [result, err] = call(callbackRealpath, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		callbackResponse response = decodeCallbackResponse(callbackRealpath, result);
		if (response.Kind == "value") {
			std::string realpath;
			if (auto e = json::unmarshal(response.Value, &realpath); !e.empty()) {
				throw std::runtime_error(e);
			}
			return realpath;
		}
		if (response.Kind == "identity") {
			return path;
		}
		if (response.Kind == "useOS") {
			return base->Realpath(path);
		}
		invalidCallbackResponse(callbackRealpath, response);
	}
	if (realpathIdentity) {
		return path;
	}
	return base->Realpath(path);
}

// Stat — callbackfs.go:327.
std::shared_ptr<vfs::FileInfo> callbackFS::Stat(const std::string& path) {
	panicIfError(callbackStat);
	if (isEnabled(callbackStat)) {
		auto [result, err] = call(callbackStat, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		callbackResponse response = decodeCallbackResponse(callbackStat, result);
		if (response.Kind == "value") {
			struct stat_ {
				uint32_t Mode = 0;      // `json:"mode"`
				int64_t Size = 0;       // `json:"size"`
				std::string MTime;      // `json:"mtime"`
				std::string unmarshalJSONFrom(json::Decoder& d) {
					return tsc::api::readFields(d, [this](std::string_view n, json::Decoder& f) -> std::string {
						if (tsc::api::fieldIs(n, "mode")) return json::unmarshalDecode(f, &Mode);
						if (tsc::api::fieldIs(n, "size")) return json::unmarshalDecode(f, &Size);
						if (tsc::api::fieldIs(n, "mtime")) return json::unmarshalDecode(f, &MTime);
						return f.skipValue();
					});
				}
			};
			stat_ stat;
			if (auto e = json::unmarshal(response.Value, &stat); !e.empty()) {
				throw std::runtime_error(e);
			}
			auto info = std::make_shared<callbackFileInfo>();
			info->name = std::string(tspath::getBaseFileName(path));
			info->size = stat.Size;
			info->mode = nodeFileModeToGoFileMode(stat.Mode);
			info->modTime = parseRFC3339Nano(stat.MTime);
			return info;
		}
		if (response.Kind == "missing") {
			return nullptr;
		}
		if (response.Kind == "fakeStat") {
			return fakeStatForPath(path);
		}
		if (response.Kind == "useOS") {
			return base->Stat(path);
		}
		invalidCallbackResponse(callbackStat, response);
	}
	if (fakeStat) {
		return fakeStatForPath(path);
	}
	return base->Stat(path);
}

// fakeStatForPath — callbackfs.go:398.
std::shared_ptr<vfs::FileInfo> callbackFS::fakeStatForPath(
    const std::string& path) {
	if (DirectoryExists(path)) {
		auto info = std::make_shared<callbackFileInfo>();
		info->name = std::string(tspath::getBaseFileName(path));
		info->mode = vfs::FileMode{vfs::FileMode::kDir | 0555u};
		return info;
	}
	if (FileExists(path)) {
		auto info = std::make_shared<callbackFileInfo>();
		info->name = std::string(tspath::getBaseFileName(path));
		info->mode = vfs::FileMode{0444u};
		return info;
	}
	return nullptr;
}

// WriteFile — callbackfs.go:425.
vfs::Error callbackFS::WriteFile(const std::string& path,
                                 const std::string& data) {
	panicIfError(callbackWriteFile);
	if (isEnabled(callbackWriteFile)) {
		// {"path": path, "data": data}
		std::string payload = "{\"path\":" + json::marshalString(path) +
		                      ",\"data\":" + json::marshalString(data) + "}";
		auto [result, err] = call(callbackWriteFile, json::Value(payload));
		if (err) {
			return vfs::Error::newError(err->Error());
		}
		callbackResponse response =
		    decodeCallbackResponse(callbackWriteFile, result);
		if (response.Kind == "value" || response.Kind == "noop") {
			return {};
		}
		if (response.Kind == "useOS") {
			return base->WriteFile(path, data);
		}
		invalidCallbackResponse(callbackWriteFile, response);
	}
	if (writeFileNoop) {
		return {};
	}

	return base->WriteFile(path, data);
}

// AppendFile — callbackfs.go:450 - always delegates to base (no callback
// support).
vfs::Error callbackFS::AppendFile(const std::string& path,
                                  const std::string& data) {
	return base->AppendFile(path, data);
}

// Remove — callbackfs.go:455.
vfs::Error callbackFS::Remove(const std::string& path) {
	panicIfError(callbackRemoveFile);
	if (isEnabled(callbackRemoveFile)) {
		auto [result, err] =
		    call(callbackRemoveFile, json::Value(json::marshalString(path)));
		if (err) {
			return vfs::Error::newError(err->Error());
		}
		callbackResponse response =
		    decodeCallbackResponse(callbackRemoveFile, result);
		if (response.Kind == "value" || response.Kind == "noop") {
			return {};
		}
		if (response.Kind == "useOS") {
			return base->Remove(path);
		}
		invalidCallbackResponse(callbackRemoveFile, response);
	}
	if (removeFileNoop) {
		return {};
	}
	return base->Remove(path);
}

// Chtimes — callbackfs.go:470 - always delegates to base (no callback
// support).
vfs::Error callbackFS::Chtimes(const std::string& path, vfs::TimePoint aTime,
                               vfs::TimePoint mTime) {
	return base->Chtimes(path, aTime, mTime);
}

}  // namespace tsc::api
