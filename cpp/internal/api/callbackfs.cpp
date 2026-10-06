// Callback filesystem — callbackfs.go:1-243.
#include "internal/api/callbackfs.h"

#include "internal/api/proto.h"

#include <optional>
#include <stdexcept>

namespace tsc::api {

// isCallbackName — callbackfs.go:38.
bool isCallbackName(std::string_view name) {
	return name == callbackReadFile || name == callbackFileExists ||
	       name == callbackDirectoryExists ||
	       name == callbackGetAccessibleEntries || name == callbackRealpath ||
	       name == callbackWriteFile || name == callbackRemoveFile;
}

// newCallbackFS — callbackfs.go:49.
callbackFS::callbackFS(std::shared_ptr<vfs::FS> b,
                       const std::vector<std::string>& callbacks)
    : base(std::move(b)) {
	enabledCallbacks.reserve(callbacks.size());
	for (const auto& cb : callbacks) {
		if (!isCallbackName(cb)) {
			throw std::runtime_error("unknown callback name: " + cb);
		}
		enabledCallbacks.insert(cb);
	}
}

std::shared_ptr<callbackFS> newCallbackFS(
    std::shared_ptr<vfs::FS> base, const std::vector<std::string>& callbacks) {
	return std::make_shared<callbackFS>(std::move(base), callbacks);
}

// SetConnection — callbackfs.go:66.
void callbackFS::SetConnection(gostd::Context c, std::shared_ptr<ipc::Conn> cn) {
	ctx = std::move(c);
	conn = std::move(cn);
}

// isEnabled — callbackfs.go:81.
bool callbackFS::isEnabled(std::string_view name) const {
	return enabledCallbacks.contains(std::string(name));
}

// call — callbackfs.go:86.
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

// UseCaseSensitiveFileNames — callbackfs.go:99.
bool callbackFS::UseCaseSensitiveFileNames() {
	return base->UseCaseSensitiveFileNames();
}

// ReadFile — callbackfs.go:104. The readFile callback uses a wrapped
// response format to distinguish three states:
//   - undefined (fall back to real FS): null or empty on wire
//   - null (not found, no fallback): {"content": null}
//   - string content: {"content": "..."}
std::pair<std::string, bool> callbackFS::ReadFile(const std::string& path) {
	if (isEnabled(callbackReadFile)) {
		auto [result, err] = call(callbackReadFile, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		if (!result.empty() && result != "null") {
			// `struct { Content *string }` — optional models the Go pointer.
			struct wrapper {
				std::optional<std::string> Content; // `json:"content"`
				std::string unmarshalJSONFrom(json::Decoder& d) {
					return tsc::api::readFields(d, [this](std::string_view n, json::Decoder& f) -> std::string {
						if (tsc::api::fieldIs(n, "content")) {
							return json::unmarshalDecode(f, &Content);
						}
						return f.skipValue();
					});
				}
			};
			wrapper w;
			if (auto e = json::unmarshal(result, &w); !e.empty()) {
				throw std::runtime_error(e);
			}
			if (!w.Content.has_value()) {
				return {"", false};
			}
			return {*w.Content, true};
		}
	}
	return base->ReadFile(path);
}

// FileExists — callbackfs.go:130.
bool callbackFS::FileExists(const std::string& path) {
	if (isEnabled(callbackFileExists)) {
		auto [result, err] = call(callbackFileExists, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		if (!result.empty() && result != "null") {
			return result == "true";
		}
	}
	return base->FileExists(path);
}

// DirectoryExists — callbackfs.go:145.
bool callbackFS::DirectoryExists(const std::string& path) {
	if (isEnabled(callbackDirectoryExists)) {
		auto [result, err] =
		    call(callbackDirectoryExists, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		if (!result.empty() && result != "null") {
			return result == "true";
		}
	}
	return base->DirectoryExists(path);
}

// GetAccessibleEntries — callbackfs.go:160.
vfs::Entries callbackFS::GetAccessibleEntries(const std::string& path) {
	if (isEnabled(callbackGetAccessibleEntries)) {
		auto [result, err] = call(callbackGetAccessibleEntries,
		                          json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		if (!result.empty()) {
			struct rawEntries {
				std::vector<std::string> Files;       // `json:"files"`
				std::vector<std::string> Directories; // `json:"directories"`
				std::string unmarshalJSONFrom(json::Decoder& d) {
					auto [t, err] = d.readToken();
					if (!err.empty()) return err;
					if (t.k == 'n') return {};
					return tsc::api::readFields(d, [this](std::string_view n, json::Decoder& f) -> std::string {
						if (tsc::api::fieldIs(n, "files")) return json::unmarshalDecode(f, &Files);
						if (tsc::api::fieldIs(n, "directories")) return json::unmarshalDecode(f, &Directories);
						return f.skipValue();
					});
				}
			};
			rawEntries entries;
			bool null_ = result == "null";
			if (!null_) {
				if (auto e = json::unmarshal(result, &entries); !e.empty()) {
					throw std::runtime_error(e);
				}
				return vfs::Entries{entries.Files, entries.Directories, std::nullopt};
			}
		}
	}
	return base->GetAccessibleEntries(path);
}

// Realpath — callbackfs.go:184.
std::string callbackFS::Realpath(const std::string& path) {
	if (isEnabled(callbackRealpath)) {
		auto [result, err] = call(callbackRealpath, json::Value(json::marshalString(path)));
		if (err) {
			throw std::runtime_error(err->Error());
		}
		if (!result.empty() && result != "null") {
			std::string realpath;
			if (auto e = json::unmarshal(result, &realpath); !e.empty()) {
				throw std::runtime_error(e);
			}
			return realpath;
		}
	}
	return base->Realpath(path);
}

// WriteFile — callbackfs.go:200.
vfs::Error callbackFS::WriteFile(const std::string& path,
                                 const std::string& data) {
	if (isEnabled(callbackWriteFile)) {
		// {"path": path, "data": data}
		std::string payload = "{\"path\":" + json::marshalString(path) +
		                      ",\"data\":" + json::marshalString(data) + "}";
		auto [result, err] = call(callbackWriteFile, json::Value(payload));
		if (err) {
			return vfs::Error::newError(err->Error());
		}
		return {};
	}

	return base->WriteFile(path, data);
}

// AppendFile — callbackfs.go:219 - always delegates to base (no callback
// support).
vfs::Error callbackFS::AppendFile(const std::string& path,
                                  const std::string& data) {
	return base->AppendFile(path, data);
}

// Remove — callbackfs.go:224.
vfs::Error callbackFS::Remove(const std::string& path) {
	if (isEnabled(callbackRemoveFile)) {
		auto [result, err] =
		    call(callbackRemoveFile, json::Value(json::marshalString(path)));
		if (err) {
			return vfs::Error::newError(err->Error());
		}
		return {};
	}
	return base->Remove(path);
}

// Chtimes — callbackfs.go:232 - always delegates to base (no callback
// support).
vfs::Error callbackFS::Chtimes(const std::string& path, vfs::TimePoint aTime,
                               vfs::TimePoint mTime) {
	return base->Chtimes(path, aTime, mTime);
}

// Stat — callbackfs.go:237 - always delegates to base (no callback support).
std::shared_ptr<vfs::FileInfo> callbackFS::Stat(const std::string& path) {
	return base->Stat(path);
}

}  // namespace tsc::api
