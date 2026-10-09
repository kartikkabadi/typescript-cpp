// fsbaselineutil.cpp — port of tsc/internal/testutil/fsbaselineutil/
// differ.go.
#include "internal/testutil/fsbaselineutil/fsbaselineutil.h"

#include <algorithm>

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/gostd/regexp.h"

namespace tsc::testutil::fsbaselineutil {

// MapFs — differ.go:38.
std::shared_ptr<vfstest::MapFS> FSDiffer::MapFs() {
	// d.FS.FSys().(*vfstest.MapFS) — unchecked type assertion (panics in
	// Go if the FSys isn't a MapFS).
	auto m = std::dynamic_pointer_cast<vfstest::MapFS>(FS->FSys());
	if (!m) {
		TSC_UNREACHABLE("FSDiffer::MapFs — FSys is not a vfstest::MapFS");
	}
	return m;
}

// BaselineFSwithDiff — differ.go:46.
void FSDiffer::BaselineFSwithDiff(gostd::io::Writer* baseline) {
	// todo: baselines the entire fs, possibly doesn't correctly diff all
	// cases of emitted files, since emit isn't fully implemented and
	// doesn't always emit the same way as strada
	std::unordered_map<std::string, std::shared_ptr<DiffEntry>> snap;
	std::unordered_map<std::string, std::string> diffs;

	for (const auto& [path, file] : MapFs()->Entries()) {
		if ((file->Mode.v & vfs::FileMode::kSymlink) != 0) {
			auto [target, ok] = MapFs()->GetTargetOfSymlink(path);
			if (!ok) {
				std::string msg =
				    "Failed to resolve symlink target: " + path;
				TSC_UNREACHABLE(msg.c_str());
			}
			auto newEntry = std::make_shared<DiffEntry>();
			newEntry->SymlinkTarget = target;
			snap[path] = newEntry;
			addFsEntryDiff(diffs, newEntry, path);
			continue;
		} else if (file->Mode.IsRegular()) {
			std::string content =
			    SanitizeInternalSymbolName(file->Data);
			auto newEntry = std::make_shared<DiffEntry>();
			newEntry->Content = content;
			newEntry->MTime = file->ModTime;
			newEntry->IsWritten =
			    WrittenFiles ? WrittenFiles->Has(path) : false;
			snap[path] = newEntry;
			addFsEntryDiff(diffs, newEntry, path);
		}
	}
	if (serializedDiff) {
		for (const auto& [path, _] : serializedDiff->Snap) {
			if (!MapFs()->GetFileInfo(path)) {
				// report deleted
				addFsEntryDiff(diffs, nullptr, path);
			}
		}
	}
	collections::SyncSet<std::string> defaultLibs;
	if (DefaultLibs && DefaultLibs() != nullptr) {
		DefaultLibs()->Range(
		    [&](const std::string& libPath) -> bool {
			    defaultLibs.Add(libPath);
			    return true;
		    });
	}
	auto newDiff = std::make_unique<Snapshot>();
	newDiff->Snap = std::move(snap);
	newDiff->DefaultLibs =
	    std::make_unique<collections::SyncSet<std::string>>(
	        std::move(defaultLibs));
	serializedDiff = std::move(newDiff);
	std::vector<std::string> diffKeys;
	diffKeys.reserve(diffs.size());
	for (const auto& [path, _] : diffs) {
		diffKeys.push_back(path);
	}
	std::sort(diffKeys.begin(), diffKeys.end());
	for (const auto& path : diffKeys) {
		baseline->write("//// [" + path + "] " + diffs[path] + "\n");
	}
	baseline->write("\n");
	// Reset written files after baseline.
	if (WrittenFiles) {
		*WrittenFiles = collections::SyncSet<std::string>();
	}
}

namespace {

// internalSymbolRegex — differ.go:97: `\x{FFFD}@[^@]+@[0-9]+` — verbatim
// now that the engine is RE2 (std::regex lacked \x{...}).
const gostd::regexp::Regexp& internalSymbolRegex() {
	static const gostd::regexp::Regexp re(R"(\x{FFFD}@[^@]+@[0-9]+)");
	return re;
}

}  // namespace

// SanitizeInternalSymbolName — differ.go:101.
std::string SanitizeInternalSymbolName(std::string_view s) {
	// strings.Contains(s, "\uFFFD@")
	if (s.find("\xEF\xBF\xBD@") == std::string_view::npos) {
		return std::string(s);
	}
	// regexp.ReplaceAllStringFunc — implemented manually since gostd's
	// Regexp has no Func variant.
	std::string input(s);
	std::string out;
	size_t last = 0;
	for (auto [pos, mend] : internalSymbolRegex().FindAllStringIndex(input,
	                                                               -1)) {
		out += input.substr(last, (size_t)pos - last);
		std::string match = input.substr((size_t)pos,
		                               (size_t)(mend - pos));
		size_t idStart = match.rfind('@');
		out += match.substr(0, idStart) + "@<symbolId>";
		last = (size_t)mend;
	}
	out += input.substr(last);
	return out;
}

// addFsEntryDiff — differ.go:111.
void FSDiffer::addFsEntryDiff(
    std::unordered_map<std::string, std::string>& diffs,
    const std::shared_ptr<DiffEntry>& newDirContent,
    const std::string& path) {
	std::shared_ptr<DiffEntry> oldDirContent;
	collections::SyncSet<std::string>* defaultLibs = nullptr;
	if (serializedDiff) {
		auto it = serializedDiff->Snap.find(path);
		if (it != serializedDiff->Snap.end()) {
			oldDirContent = it->second;
		}
		defaultLibs = serializedDiff->DefaultLibs.get();
	}
	// todo handle more cases of fs changes
	if (!oldDirContent) {
		if (!DefaultLibs || DefaultLibs() == nullptr ||
		    !DefaultLibs()->Has(path)) {
			if (!newDirContent->SymlinkTarget.empty()) {
				diffs[path] =
				    "-> " + newDirContent->SymlinkTarget + " *new*";
			} else {
				diffs[path] = "*new* \n" + newDirContent->Content;
			}
		}
	} else if (!newDirContent) {
		diffs[path] = "*deleted*";
	} else if (newDirContent->Content != oldDirContent->Content) {
		diffs[path] = "*modified* \n" + newDirContent->Content;
	} else if (newDirContent->IsWritten) {
		diffs[path] = "*rewrite with same content*";
	} else if (newDirContent->MTime != oldDirContent->MTime) {
		diffs[path] = "*mTime changed*";
	} else if (defaultLibs != nullptr && defaultLibs->Has(path) &&
	           DefaultLibs && DefaultLibs() != nullptr &&
	           !DefaultLibs()->Has(path)) {
		// Lib file that was read
		diffs[path] = "*Lib*\n" + newDirContent->Content;
	}
}

// ChangedPaths — differ.go:147.
std::vector<FileChange> FSDiffer::ChangedPaths() {
	if (!serializedDiff) {
		return {};
	}

	std::vector<FileChange> changes;
	Snapshot* oldSnap = serializedDiff.get();

	// Check current files against previous snapshot.
	for (const auto& [path, file] : MapFs()->Entries()) {
		if ((file->Mode.v & vfs::FileMode::kSymlink) != 0 ||
		    !file->Mode.IsRegular()) {
			continue;
		}
		auto it = oldSnap->Snap.find(path);
		if (it == oldSnap->Snap.end()) {
			// New file.
			changes.push_back(FileChange{.Path = path});
		} else if (file->Data != it->second->Content ||
		           file->ModTime != it->second->MTime) {
			// Modified or touched file.
			changes.push_back(FileChange{.Path = path});
		}
	}

	// Check for deleted files.
	for (const auto& [path, _] : oldSnap->Snap) {
		if (!MapFs()->GetFileInfo(path)) {
			changes.push_back(
			    FileChange{.Path = path, .Deleted = true});
		}
	}

	return changes;
}

}  // namespace tsc::testutil::fsbaselineutil
