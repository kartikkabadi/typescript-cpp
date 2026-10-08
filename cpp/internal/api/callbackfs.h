// Callback filesystem — callbackfs.go. Wraps a base filesystem and delegates
// certain operations to the client via RPC callbacks, allowing the API client
// to provide a virtual filesystem (e.g., in-memory files for testing).
//
// The callbacks to enable are specified at construction time via the
// --callbacks CLI flag. The connection is set via SetConnection after the
// transport connection is established.
#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/vfs/vfs.h"

namespace tsc::api {

// Callback names that can be enabled
inline const std::string callbackReadFile = "readFile";
inline const std::string callbackFileExists = "fileExists";
inline const std::string callbackDirectoryExists = "directoryExists";
inline const std::string callbackGetAccessibleEntries = "getAccessibleEntries";
inline const std::string callbackRealpath = "realpath";
inline const std::string callbackWriteFile = "writeFile";
inline const std::string callbackRemoveFile = "removeFile";

// isCallbackName — callbackfs.go:38.
bool isCallbackName(std::string_view name);

// callbackFS — callbackfs.go:17.
class callbackFS : public vfs::FS {
public:
	// newCallbackFS — callbackfs.go:49. callbacks specifies which filesystem
	// operations should be delegated to the client. Unknown names panic.
	callbackFS(std::shared_ptr<vfs::FS> base,
	           const std::vector<std::string>& callbacks);

	// SetConnection — callbackfs.go:66. Must be called after the transport
	// connection is established but before any filesystem operations that
	// need callbacks.
	void SetConnection(gostd::Context ctx, std::shared_ptr<ipc::Conn> conn);

	// vfs::FS
	bool UseCaseSensitiveFileNames() override;
	std::pair<std::string, bool> ReadFile(const std::string& path) override;
	vfs::Error WriteFile(const std::string& path,
	                     const std::string& data) override;
	vfs::Error AppendFile(const std::string& path,
	                      const std::string& data) override;
	vfs::Error Remove(const std::string& path) override;
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	                   vfs::TimePoint mTime) override;
	bool DirectoryExists(const std::string& path) override;
	vfs::Entries GetAccessibleEntries(const std::string& path) override;
	std::shared_ptr<vfs::FileInfo> Stat(const std::string& path) override;
	std::string Realpath(const std::string& path) override;
	bool FileExists(const std::string& path) override;

private:
	std::shared_ptr<vfs::FS> base;
	std::unordered_set<std::string> enabledCallbacks;

	// conn and ctx are set after connection is established
	std::shared_ptr<ipc::Conn> conn;
	gostd::Context ctx;

	// isEnabled — callbackfs.go:81.
	bool isEnabled(std::string_view name) const;
	// call — callbackfs.go:87.
	std::pair<json::Value, gostd::Error> call(std::string_view name,
	                                          const json::Value& arg);
};

// newCallbackFS — callbackfs.go:49.
std::shared_ptr<callbackFS> newCallbackFS(
    std::shared_ptr<vfs::FS> base, const std::vector<std::string>& callbacks);

}  // namespace tsc::api
