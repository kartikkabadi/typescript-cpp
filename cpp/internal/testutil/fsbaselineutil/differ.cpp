// differ.go — fsbaselineutil.FSDiffer.
#include "internal/testutil/fsbaselineutil/differ.h"

#include <algorithm>
#include <map>
#include <regex>

namespace tsc::testutil::fsbaselineutil {

// MapFs — differ.go:37.
vfs::vfstest::MapFS* FSDiffer::MapFs() {
	return dynamic_cast<vfs::vfstest::MapFS*>(FS->FSys().get());
}

// BaselineFSwithDiff — differ.go:45.
void FSDiffer::BaselineFSwithDiff(gostd::io::Writer* baseline) {
	// todo: baselines the entire fs, possibly doesn't correctly diff
	// all cases of emitted files, since emit isn't fully implemented
	// and doesn't always emit the same way as strada
	std::unordered_map<std::string, std::shared_ptr<DiffEntry>> snap;

	std::unordered_map<std::string, std::string> diffs;

	for (auto& [path, file] : MapFs()->Entries()) {
		if (file->Mode.Type() == vfs::ModeSymlink) {
			auto [target, ok] = MapFs()->GetTargetOfSymlink(path);
			if (!ok) {
				throw std::runtime_error(
				    "Failed to resolve symlink target: " + path);
			}
			auto newEntry = std::make_shared<DiffEntry>(
			    DiffEntry{.SymlinkTarget = target});
			snap[path] = newEntry;
			addFsEntryDiff(diffs, newEntry, path);
			continue;
		} else if (file->Mode.IsRegular()) {
			auto content =
			    SanitizeInternalSymbolName(file->Data);
			auto newEntry = std::make_shared<DiffEntry>(
			    DiffEntry{.Content = content,
			              .MTime = file->ModTime,
			              .IsWritten = WrittenFiles->Has(path)});
			snap[path] = newEntry;
			addFsEntryDiff(diffs, newEntry, path);
		}
	}
	if (serializedDiff != nullptr) {
		for (auto& [path, _] : serializedDiff->Snap) {
			if (MapFs()->GetFileInfo(path) == nullptr) {
				// report deleted
				addFsEntryDiff(diffs, nullptr, path);
			}
		}
	}
	collections::SyncSet<std::string> defaultLibs;
	if (DefaultLibs != nullptr && DefaultLibs() != nullptr) {
		DefaultLibs()->Range([&](const std::string& libPath) {
			defaultLibs.Add(libPath);
			return true;
		});
	}
	serializedDiff = std::make_shared<Snapshot>(Snapshot{
	    .Snap = std::move(snap),
	    .DefaultLibs =
	        std::make_shared<collections::SyncSet<std::string>>(
	            std::move(defaultLibs)),
	});
	std::vector<std::string> diffKeys;
	diffKeys.reserve(diffs.size());
	for (auto& [k, _] : diffs) diffKeys.push_back(k);
	std::sort(diffKeys.begin(), diffKeys.end());
	for (auto& path : diffKeys) {
		baseline->write("//// [" + path + "] " + diffs[path] +
		                "\n");
	}
	baseline->write("\n");
	*WrittenFiles = collections::SyncSet<std::string>
	    {}; // Reset written files after baseline
}

// addFsEntryDiff — differ.go:120.
void FSDiffer::addFsEntryDiff(
    std::unordered_map<std::string, std::string>& diffs,
    std::shared_ptr<DiffEntry> newDirContent,
    const std::string& path) {
	std::shared_ptr<DiffEntry> oldDirContent;
	collections::SyncSet<std::string>* defaultLibs = nullptr;
	if (serializedDiff != nullptr) {
		auto it = serializedDiff->Snap.find(path);
		if (it != serializedDiff->Snap.end()) {
			oldDirContent = it->second;
		}
		defaultLibs = serializedDiff->DefaultLibs.get();
	}
	// todo handle more cases of fs changes
	if (oldDirContent == nullptr) {
		if (DefaultLibs == nullptr || DefaultLibs() == nullptr ||
		    !DefaultLibs()->Has(path)) {
			if (newDirContent->SymlinkTarget != "") {
				diffs[path] = "-> " +
				              newDirContent->SymlinkTarget +
				              " *new*";
			} else {
				diffs[path] =
				    "*new* \n" + newDirContent->Content;
			}
		}
	} else if (newDirContent == nullptr) {
		diffs[path] = "*deleted*";
	} else if (newDirContent->Content != oldDirContent->Content) {
		diffs[path] = "*modified* \n" + newDirContent->Content;
	} else if (newDirContent->IsWritten) {
		diffs[path] = "*rewrite with same content*";
	} else if (newDirContent->MTime != oldDirContent->MTime) {
		diffs[path] = "*mTime changed*";
	} else if (defaultLibs != nullptr && defaultLibs->Has(path) &&
	           DefaultLibs != nullptr && DefaultLibs() != nullptr &&
	           !DefaultLibs()->Has(path)) {
		// Lib file that was read
		diffs[path] = "*Lib*\n" + newDirContent->Content;
	}
}

// internalSymbolRegex — differ.go:105. Go regexp:
// \x{FFFD}@[^@]+@[0-9]+. Implemented as a manual UTF-8 scan: the Go
// pattern matches U+FFFD, '@', one or more non-'@' runes, '@', then one
// or more ASCII digits.
namespace {

std::string utf8ReplacementChar() { return "\xEF\xBF\xBD"; }

} // namespace

// SanitizeInternalSymbolName — differ.go:110.
std::string SanitizeInternalSymbolName(const std::string& s) {
	if (s.find("\xEF\xBF\xBD@") == std::string::npos) {
		return s;
	}
	const std::string repChar = utf8ReplacementChar();
	std::string out;
	size_t pos = 0;
	while (pos < s.size()) {
		auto at = s.find(repChar + "@", pos);
		if (at == std::string::npos) {
			out += s.substr(pos);
			break;
		}
		// Look for <non-@>+ @ <digits>+ after the match start.
		size_t i = at + repChar.size() + 1; // after "\uFFFD@"
		size_t nameStart = i;
		while (i < s.size() && s[i] != '@') {
			// skip one UTF-8 rune
			unsigned char c = (unsigned char)s[i];
			size_t len =
			    c < 0x80
			        ? 1
			        : c < 0xE0 ? 2
			                   : c < 0xF0 ? 3 : 4;
			i += len;
		}
		if (i == nameStart || i >= s.size() || s[i] != '@') {
			// Not a full match: copy through repChar+"@" and
			// continue scanning after it.
			out += s.substr(pos, at + repChar.size() + 1 - pos);
			pos = at + repChar.size() + 1;
			continue;
		}
		size_t digitsStart = i + 1;
		size_t j = digitsStart;
		while (j < s.size() && s[j] >= '0' && s[j] <= '9') {
			j++;
		}
		if (j == digitsStart) {
			out += s.substr(pos,
			                at + repChar.size() + 1 - pos);
			pos = at + repChar.size() + 1;
			continue;
		}
		// Match s[at, j): keep "<FFFD>@<name>@<symbolId>"
		out += s.substr(pos, at - pos);
		out += s.substr(at, i + 1 - at); // <FFFD>@<name>@
		out += "<symbolId>";
		pos = j;
	}
	return out;
}

// ChangedPaths — differ.go:157.
std::vector<FileChange> FSDiffer::ChangedPaths() {
	if (serializedDiff == nullptr) {
		return {};
	}

	std::vector<FileChange> changes;
	auto& oldSnap = serializedDiff;

	// Check current files against previous snapshot.
	for (auto& [path, file] : MapFs()->Entries()) {
		if (file->Mode.Type() == vfs::ModeSymlink ||
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
	for (auto& [path, _] : oldSnap->Snap) {
		if (MapFs()->GetFileInfo(path) == nullptr) {
			changes.push_back(
			    FileChange{.Path = path, .Deleted = true});
		}
	}

	return changes;
}

} // namespace tsc::testutil::fsbaselineutil
