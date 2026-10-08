// testfs.h — port of Go's testing/fstest.TestFS (stdlib testfs.go),
// used by vfstest_test.go. Faithful port of the same checks; the only
// deviation is iotest.TestReader, which exercises io.Seeker/io.ReaderAt —
// this port's fs.File has Read/Stat/Close only, so the small-read checks
// are kept and the seek/readat checks do not apply.
#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/vfs/vfs.h"

namespace tsc::vfs::vfstest::fstest::testfs {

using tsc::vfs::DirEntry;
using tsc::vfs::Error;
using tsc::vfs::File;
using tsc::vfs::FileInfo;
using tsc::vfs::FileMode;
using tsc::vfs::IoFS;
using tsc::vfs::ReadDirFile;

inline std::string modeStr(FileMode m) {
	// fmt %v analog sufficient for equality comparisons: type bits + perm.
	return gostd::sprintf(
	    "%v:%v",
	    std::vector<gostd::fmtArg>{static_cast<int64_t>(m.Type().v),
	                               static_cast<int64_t>(m.Perm().v)});
}

// formatEntry — testfs.go.
inline std::string formatEntry(const std::shared_ptr<DirEntry>& entry) {
	return gostd::sprintf("%s IsDir=%v Type=%v",
	                      {entry->Name(), entry->IsDir(),
	                       modeStr(entry->Type())});
}

// formatInfoEntry — testfs.go.
inline std::string formatInfoEntry(const std::shared_ptr<FileInfo>& info) {
	return gostd::sprintf("%s IsDir=%v Type=%v",
	                      {info->Name(), info->IsDir(),
	                       modeStr(info->Mode().Type())});
}

// formatInfo — testfs.go.
inline std::string formatInfo(const std::shared_ptr<FileInfo>& info) {
	return gostd::sprintf(
	    "%s IsDir=%v Mode=%v Size=%d ModTime=%v",
	    {info->Name(), info->IsDir(), modeStr(info->Mode()), info->Size(),
	     static_cast<int64_t>(
	         info->ModTime().time_since_epoch().count())});
}

// fsTester — testfs.go.
struct fsTester {
	std::shared_ptr<IoFS> fsys;
	std::vector<std::string> errors;
	std::vector<std::string> dirs;
	std::vector<std::string> files;

	void errorf(const std::string& msg) { errors.push_back(msg); }

	// openDir — testfs.go.
	std::shared_ptr<ReadDirFile> openDir(const std::string& dir) {
		auto [f, err] = fsys->Open(dir);
		if (err) {
			errorf(dir + ": Open: " + err.str());
			return nullptr;
		}
		auto d = std::dynamic_pointer_cast<ReadDirFile>(f);
		if (!d) {
			f->Close();
			errorf(dir + ": Open returned File type not a fs.ReadDirFile");
			return nullptr;
		}
		return d;
	}

	// checkBadPath — testfs.go.
	void checkBadPath(const std::string& file, const std::string& desc,
	                  const std::function<Error(const std::string&)>&
	                      open) {
		std::vector<std::string> bad{"/" + file, file + "/."};
		if (file == ".") {
			bad.push_back("/");
		}
		if (auto i = file.find("/"); i != std::string::npos) {
			bad.push_back(file.substr(0, i) + "//" + file.substr(i + 1));
			bad.push_back(file.substr(0, i) + "/./" + file.substr(i + 1));
			bad.push_back(file.substr(0, i) + "\\" + file.substr(i + 1));
			bad.push_back(file.substr(0, i) + "/../" + file);
		}
		if (auto i = file.rfind("/"); i != std::string::npos) {
			bad.push_back(file.substr(0, i) + "//" + file.substr(i + 1));
			bad.push_back(file.substr(0, i) + "/./" + file.substr(i + 1));
			bad.push_back(file.substr(0, i) + "\\" + file.substr(i + 1));
			bad.push_back(file + "/../" + file.substr(i + 1));
		}
		for (auto& b : bad) {
			if (auto err = open(b); !err) {
				errorf(file + ": " + desc + "(" + b +
				       ") succeeded, want error");
			}
		}
	}

	// checkOpen — testfs.go.
	void checkOpen(const std::string& file) {
		checkBadPath(file, "Open", [this](const std::string& f) {
			auto [fp, err] = fsys->Open(f);
			if (!err) {
				fp->Close();
			}
			return err;
		});
	}

	// checkDirList — testfs.go.
	void
	checkDirList(const std::string& dir, const std::string& desc,
	             const std::vector<std::shared_ptr<DirEntry>>& list1,
	             const std::vector<std::shared_ptr<DirEntry>>& list2) {
		std::unordered_map<std::string, std::shared_ptr<DirEntry>> old;
		auto checkMode = [this, &dir](
		                     const std::shared_ptr<DirEntry>& entry) {
			if (entry->IsDir() !=
			    ((entry->Type().v & FileMode::kDir) != 0)) {
				if (entry->IsDir()) {
					errorf(dir + ": ReadDir returned " + entry->Name() +
					       " with IsDir() = true, Type() & ModeDir = 0");
				} else {
					errorf(dir + ": ReadDir returned " + entry->Name() +
					       " with IsDir() = false, Type() & ModeDir = "
					       "ModeDir");
				}
			}
		};

		for (auto& entry1 : list1) {
			old[entry1->Name()] = entry1;
			checkMode(entry1);
		}

		std::vector<std::string> diffs;
		for (auto& entry2 : list2) {
			auto it = old.find(entry2->Name());
			if (it == old.end()) {
				checkMode(entry2);
				diffs.push_back("+ " + formatEntry(entry2));
				continue;
			}
			if (formatEntry(it->second) != formatEntry(entry2)) {
				diffs.push_back("- " + formatEntry(it->second));
				diffs.push_back("+ " + formatEntry(entry2));
			}
			old.erase(it);
		}
		for (auto& [name, entry1] : old) {
			diffs.push_back("- " + formatEntry(entry1));
		}
		if (diffs.empty()) {
			return;
		}
		std::sort(diffs.begin(), diffs.end(),
		          [](const std::string& a, const std::string& b) {
			          // sort by name then +/-, like Go's SortFunc.
			          auto fields1 = a.substr(0, a.find(' '));
			          auto fields2 = b.substr(0, b.find(' '));
			          return (fields1 + " " + b.substr(0, 1)) <
			                 (fields2 + " " + a.substr(0, 1));
		          });
		std::string joined;
		for (auto& d : diffs) {
			joined += "\n\t" + d;
		}
		errorf(dir + ": diff " + desc + ":" + joined);
	}

	// checkStat — testfs.go.
	void checkStat(const std::string& path,
	               const std::shared_ptr<DirEntry>& entry) {
		auto [file, err] = fsys->Open(path);
		if (err) {
			errorf(path + ": Open: " + err.str());
			return;
		}
		auto [info, serr] = file->Stat();
		file->Close();
		if (serr) {
			errorf(path + ": Stat: " + serr.str());
			return;
		}
		auto fentry = formatEntry(entry);
		auto fientry = formatInfoEntry(info);
		// Note: mismatch here is OK for symlink, because Open
		// dereferences symlink.
		if (fentry != fientry && !(entry->Type().v & FileMode::kSymlink)) {
			errorf(path + ": mismatch:\n\tentry = " + fentry +
			       "\n\tfile.Stat() = " + fientry);
		}

		auto [einfo, eierr] = entry->Info();
		if (eierr) {
			errorf(path + ": entry.Info: " + eierr.str());
			return;
		}
		auto finfo = formatInfo(info);
		if (entry->Type().v & FileMode::kSymlink) {
			// For symlink, just check that entry.Info matches entry
			// on common fields. Open dereferences symlink, so info
			// itself may differ.
			auto feentry = formatInfoEntry(einfo);
			if (fentry != feentry) {
				errorf(path + ": mismatch\n\tentry = " + fentry +
				       "\n\tentry.Info() = " + feentry + "\n");
			}
		} else {
			auto feinfo = formatInfo(einfo);
			if (feinfo != finfo) {
				errorf(path + ": mismatch:\n\tentry.Info() = " +
				       feinfo + "\n\tfile.Stat() = " + finfo + "\n");
			}
		}

		// Stat should be the same as Open+Stat, even for symlinks.
		auto [info2, err2] = tsc::vfs::fsStat(fsys, path);
		if (err2) {
			errorf(path + ": fs.Stat: " + err2.str());
			return;
		}
		auto finfo2 = formatInfo(info2);
		if (finfo2 != finfo) {
			errorf(path + ": fs.Stat(...) = " + finfo2 + "\n\twant " +
			       finfo);
		}

		if (auto stfs = std::dynamic_pointer_cast<StatFS>(fsys)) {
			auto [info3, err3] = stfs->Stat(path);
			if (err3) {
				errorf(path + ": fsys.Stat: " + err3.str());
				return;
			}
			auto finfo3 = formatInfo(info3);
			if (finfo3 != finfo) {
				errorf(path + ": fsys.Stat(...) = " + finfo3 +
				       "\n\twant " + finfo);
			}
		}

		if (auto rlfs = std::dynamic_pointer_cast<ReadLinkFS>(fsys)) {
			auto [info3, err3] = rlfs->Lstat(path);
			if (err3) {
				errorf(path + ": fsys.Lstat: " + err3.str());
				return;
			}
			auto fientry2 = formatInfoEntry(info3);
			if (fentry != fientry2) {
				errorf(path + ": mismatch:\n\tentry = " + fentry +
				       "\n\tfsys.Lstat(...) = " + fientry2);
			}
			auto feinfo = formatInfo(einfo);
			auto finfo3 = formatInfo(info3);
			if (feinfo != finfo3) {
				errorf(path + ": mismatch:\n\tentry.Info() = " +
				       feinfo + "\n\tfsys.Lstat(...) = " + finfo3);
			}
		}
	}

	// checkGlob — testfs.go.
	void
	checkGlob(const std::string& dir,
	          const std::vector<std::shared_ptr<DirEntry>>& list) {
		auto gfs = std::dynamic_pointer_cast<GlobFS>(fsys);
		if (!gfs) {
			return;
		}

		// Make a complex glob pattern prefix that only matches dir.
		std::string glob;
		if (dir != ".") {
			std::vector<std::string> elem;
			size_t start = 0;
			while (true) {
				auto slash = dir.find('/', start);
				elem.push_back(slash == std::string::npos
				                   ? dir.substr(start)
				                   : dir.substr(start, slash - start));
				if (slash == std::string::npos) {
					break;
				}
				start = slash + 1;
			}
			for (size_t i = 0; i < elem.size(); i++) {
				std::string pattern;
				size_t j = 0;
				for (char r : elem[i]) {
					if (r == '*' || r == '?' || r == '\\' ||
					    r == '[' || r == '-') {
						pattern += '\\';
						pattern += r;
						j++;
						continue;
					}
					switch ((i + j) % 5) {
					case 0:
						pattern += r;
						break;
					case 1:
						pattern += '[';
						pattern += r;
						pattern += ']';
						break;
					case 2:
						pattern += '[';
						pattern += r;
						pattern += '-';
						pattern += r;
						pattern += ']';
						break;
					case 3:
						pattern += '[';
						pattern += '\\';
						pattern += r;
						pattern += ']';
						break;
					case 4:
						pattern += '[';
						pattern += '\\';
						pattern += r;
						pattern += '-';
						pattern += '\\';
						pattern += r;
						pattern += ']';
						break;
					}
					j++;
				}
				elem[i] = pattern;
			}
			for (size_t i = 0; i < elem.size(); i++) {
				if (i) {
					glob += "/";
				}
				glob += elem[i];
			}
			glob += "/";
		}

		// Test that malformed patterns are detected. The error is
		// likely path.ErrBadPattern but need not be.
		if (auto [names, err] = gfs->Glob(glob + "nonexist/[]"); !err) {
			errorf(dir + ": Glob(" + glob + "nonexist/[]" +
			       "): bad pattern not detected");
		}

		// Try to find a letter that appears in only some of the
		// final names.
		char c = 'a';
		for (; c <= 'z'; c++) {
			bool have = false, haveNot = false;
			for (auto& d : list) {
				if (d->Name().find(c) != std::string::npos) {
					have = true;
				} else {
					haveNot = true;
				}
			}
			if (have && haveNot) {
				break;
			}
		}
		if (c > 'z') {
			c = 'a';
		}
		glob += "*";
		glob += c;
		glob += "*";

		std::vector<std::string> want;
		for (auto& d : list) {
			if (d->Name().find(c) != std::string::npos) {
				want.push_back(
				    tsc::vfs::pathJoin({dir, d->Name()}));
			}
		}
		std::sort(want.begin(), want.end());

		auto [names, gerr] = gfs->Glob(glob);
		if (gerr) {
			errorf(dir + ": Glob(" + glob + "): " + gerr.str());
			return;
		}
		if (want == names) {
			return;
		}

		if (!std::is_sorted(names.begin(), names.end())) {
			std::string joined;
			for (auto& n : names) {
				joined += n + "\n";
			}
			errorf(dir + ": Glob(" + glob + "): unsorted output:\n" +
			       joined);
			std::sort(names.begin(), names.end());
		}

		std::vector<std::string> problems;
		size_t wi = 0, ni = 0;
		while (wi < want.size() || ni < names.size()) {
			if (wi < want.size() && ni < names.size() &&
			    want[wi] == names[ni]) {
				wi++;
				ni++;
			} else if (wi < want.size() &&
			           (ni >= names.size() || want[wi] < names[ni])) {
				problems.push_back("missing: " + want[wi]);
				wi++;
			} else {
				problems.push_back("extra: " + names[ni]);
				ni++;
			}
		}
		std::string joined;
		for (auto& p : problems) {
			joined += p + "\n";
		}
		errorf(dir + ": Glob(" + glob + "): wrong output:\n" + joined);
	}

	// checkFileRead — testfs.go.
	void checkFileRead(const std::string& file, const std::string& desc,
	                   const std::string& data1,
	                   const std::string& data2) {
		if (data1 != data2) {
			errorf(file + ": " + desc +
			       ": different data returned\n\t" + data1 +
			       "\n\t" + data2);
		}
	}

	// checkFile — testfs.go.
	void checkFile(const std::string& file) {
		files.push_back(file);

		auto [f, err] = fsys->Open(file);
		if (err) {
			errorf(file + ": Open: " + err.str());
			return;
		}
		std::string data;
		{
			char buf[4096];
			for (;;) {
				auto [n, rerr] = f->Read(
				    std::span<char>(buf, sizeof(buf)));
				data.append(buf, n);
				if (rerr) {
					if (!rerr.is(tsc::vfs::ErrEOF)) {
						f->Close();
						errorf(file +
						       ": Open+ReadAll: " + rerr.str());
						return;
					}
					break;
				}
				if (n == 0) {
					break;
				}
			}
		}
		if (auto cerr = f->Close(); cerr) {
			errorf(file + ": Close: " + cerr.str());
		}
		f->Close(); // closing twice must not crash

		// Check that ReadFile works if present.
		if (auto rffs = std::dynamic_pointer_cast<ReadFileFS>(fsys)) {
			auto [data2, rerr] = rffs->ReadFile(file);
			if (rerr) {
				errorf(file + ": fsys.ReadFile: " + rerr.str());
				return;
			}
			checkFileRead(file, "ReadAll vs fsys.ReadFile", data,
			              data2);

			// Modify the data and check it again. Modifying the
			// returned byte slice should not affect the next call.
			std::string data3 = data2;
			for (auto& ch : data3) {
				ch++;
			}
			auto [data4, rerr2] = rffs->ReadFile(file);
			if (rerr2) {
				errorf(file + ": second call to fsys.ReadFile: " +
				       rerr2.str());
				return;
			}
			checkFileRead(file, "Readall vs second fsys.ReadFile",
			              data, data4);

			checkBadPath(file, "ReadFile",
			             [rffs](const std::string& name) {
				             auto [d, e] = rffs->ReadFile(name);
				             return e;
			             });
		}

		// Check that fs.ReadFile works with t.fsys.
		auto [data2, ferr] = tsc::vfs::fsReadFile(fsys, file);
		if (ferr) {
			errorf(file + ": fs.ReadFile: " + ferr.str());
			return;
		}
		checkFileRead(file, "ReadAll vs fs.ReadFile", data, data2);

		// iotest.TestReader equivalent for the parts this port's
		// File supports (Read with small buffers; no Seek/ReadAt
		// in the interface).
		auto [f2, oerr] = fsys->Open(file);
		if (oerr) {
			errorf(file + ": second Open: " + oerr.str());
			return;
		}
		std::string small;
		for (;;) {
			char b[2];
			auto [n, rerr] = f2->Read(std::span<char>(b, 2));
			small.append(b, n);
			if (rerr) {
				break;
			}
			if (n == 0) {
				break;
			}
		}
		f2->Close();
		if (small != data) {
			errorf(file + ": failed TestReader: short reads differ");
		}
	}

	// checkDir — testfs.go.
	void checkDir(const std::string& dir) {
		dirs.push_back(dir);
		auto d = openDir(dir);
		if (!d) {
			return;
		}
		auto [list, rerr] = d->ReadDir(-1);
		if (rerr) {
			d->Close();
			errorf(dir + ": ReadDir(-1): " + rerr.str());
			return;
		}

		std::string prefix = dir == "." ? "" : dir + "/";
		for (auto& info : list) {
			auto name = info->Name();
			if (name == "." || name == ".." || name.empty()) {
				errorf(dir + ": ReadDir: child has invalid name: " +
				       name);
				continue;
			}
			if (name.find('/') != std::string::npos) {
				errorf(dir +
				       ": ReadDir: child name contains slash: " +
				       name);
				continue;
			}
			if (name.find('\\') != std::string::npos) {
				errorf(dir +
				       ": ReadDir: child name contains "
				       "backslash: " +
				       name);
				continue;
			}
			auto path = prefix + name;
			checkStat(path, info);
			checkOpen(path);
			if (info->Type().v & FileMode::kDir) {
				checkDir(path);
			} else if (info->Type().v & FileMode::kSymlink) {
				files.push_back(path);
			} else {
				checkFile(path);
			}
		}

		// Check ReadDir(-1) at EOF.
		auto [list2, err2] = d->ReadDir(-1);
		if (!list2.empty() || err2) {
			d->Close();
			errorf(dir + ": ReadDir(-1) at EOF = " +
			       std::to_string(list2.size()) +
			       " entries, wanted 0 entries, nil");
			return;
		}

		// Check ReadDir(1) at EOF (different results).
		std::tie(list2, err2) = d->ReadDir(1);
		if (!list2.empty() || !err2.is(tsc::vfs::ErrEOF)) {
			d->Close();
			errorf(dir + ": ReadDir(1) at EOF = " +
			       std::to_string(list2.size()) +
			       " entries, wanted 0 entries, EOF");
			return;
		}

		if (auto cerr = d->Close(); cerr) {
			errorf(dir + ": Close: " + cerr.str());
		}
		d->Close(); // closing twice must not crash

		// Reopen directory, read a second time, make sure contents
		// match.
		d = openDir(dir);
		if (!d) {
			return;
		}
		std::vector<std::shared_ptr<DirEntry>> list2b;
		Error e2;
		std::tie(list2b, e2) = d->ReadDir(-1);
		if (e2) {
			d->Close();
			errorf(dir + ": second Open+ReadDir(-1): " + e2.str());
			return;
		}
		checkDirList(dir,
		             "first Open+ReadDir(-1) vs second "
		             "Open+ReadDir(-1)",
		             list, list2b);
		d->Close();

		// Reopen directory, read a third time in pieces.
		d = openDir(dir);
		if (!d) {
			return;
		}
		list2b.clear();
		bool fail = false;
		for (;;) {
			int n = list2b.empty() ? 1 : 2;
			auto [frag, ferr] = d->ReadDir(n);
			if (static_cast<int>(frag.size()) > n) {
				errorf(dir + ": third Open: ReadDir(" +
				       std::to_string(n) + "): too many entries");
				fail = true;
				break;
			}
			list2b.insert(list2b.end(), frag.begin(), frag.end());
			if (ferr.is(tsc::vfs::ErrEOF)) {
				break;
			}
			if (ferr) {
				errorf(dir + ": third Open: ReadDir(" +
				       std::to_string(n) + "): " + ferr.str());
				fail = true;
				break;
			}
			if (n == 0) {
				errorf(dir + ": third Open: ReadDir(0): 0 entries "
				       "but nil error");
				fail = true;
				break;
			}
		}
		d->Close();
		if (fail) {
			return;
		}
		checkDirList(dir,
		             "first Open+ReadDir(-1) vs third "
		             "Open+ReadDir(1,2) loop",
		             list, list2b);

		// If fsys has ReadDir, check that it matches and is sorted.
		if (auto rdfs = std::dynamic_pointer_cast<ReadDirFS>(fsys)) {
			auto [list3, e3] = rdfs->ReadDir(dir);
			if (e3) {
				errorf(dir + ": fsys.ReadDir: " + e3.str());
				return;
			}
			checkDirList(dir,
			             "first Open+ReadDir(-1) vs fsys.ReadDir",
			             list, list3);
			for (size_t i = 0; i + 1 < list3.size(); i++) {
				if (list3[i]->Name() >= list3[i + 1]->Name()) {
					errorf(dir + ": fsys.ReadDir: list not "
					       "sorted");
				}
			}
		}

		// Check fs.ReadDir as well.
		auto [list4, e4] = tsc::vfs::fsReadDir(fsys, dir);
		if (e4) {
			errorf(dir + ": fs.ReadDir: " + e4.str());
			return;
		}
		checkDirList(dir,
		             "first Open+ReadDir(-1) vs fs.ReadDir", list,
		             list4);
		for (size_t i = 0; i + 1 < list4.size(); i++) {
			if (list4[i]->Name() >= list4[i + 1]->Name()) {
				errorf(dir + ": fs.ReadDir: list not sorted");
			}
		}

		checkGlob(dir, list4);
	}
};

// testFS — testfs.go.
inline Error testFS(const std::shared_ptr<IoFS>& fsys,
                    const std::vector<std::string>& expected) {
	fsTester t{fsys};
	t.checkDir(".");
	t.checkOpen(".");
	std::unordered_map<std::string, bool> found;
	for (auto& dir : t.dirs) {
		found[dir] = true;
	}
	for (auto& file : t.files) {
		found[file] = true;
	}
	found.erase(".");
	if (expected.empty() && !found.empty()) {
		std::vector<std::string> list;
		for (auto& [k, _] : found) {
			list.push_back(k);
		}
		std::sort(list.begin(), list.end());
		if (list.size() > 15) {
			list.resize(10);
			list.push_back("...");
		}
		std::string joined;
		for (auto& l : list) {
			joined += l + "\n";
		}
		t.errorf("expected empty file system but found files:\n" +
		         joined);
	}
	for (auto& name : expected) {
		if (!found.count(name)) {
			t.errorf("expected but not found: " + name);
		}
	}
	if (t.errors.empty()) {
		return Error{};
	}
	std::string joined = "TestFS found errors:";
	for (auto& e : t.errors) {
		joined += "\n" + e;
	}
	return Error::newError(joined);
}

// TestFS — testfs.go.
inline Error TestFS(const std::shared_ptr<IoFS>& fsys,
                    std::initializer_list<const char*> expected) {
	std::vector<std::string> exp(expected.begin(), expected.end());
	if (auto err = testFS(fsys, exp); err) {
		return err;
	}
	for (auto& name : exp) {
		if (auto i = name.find('/'); i != std::string::npos) {
			auto dir = name.substr(0, i);
			auto dirSlash = name.substr(0, i + 1);
			std::vector<std::string> subExpected;
			for (auto& n : exp) {
				if (n.starts_with(dirSlash)) {
					subExpected.push_back(
					    n.substr(dirSlash.size()));
				}
			}
			auto [sub, serr] = tsc::vfs::fsSub(fsys, dir);
			if (serr) {
				return serr;
			}
			if (auto err = testFS(sub, subExpected); err) {
				return Error::newError("testing fs.Sub(fsys, " +
				                       dir + "): " + err.str());
			}
			break; // one sub-test is enough
		}
	}
	return Error{};
}

} // namespace tsc::vfs::vfstest::fstest::testfs
