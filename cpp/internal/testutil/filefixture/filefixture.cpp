// filefixture.cpp — port of tsc/internal/testutil/filefixture/filefixture.go.
#include "internal/testutil/filefixture/filefixture.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace tsc::testutil::filefixture {

namespace {

// fromFile — filefixture.go:16.
struct fromFile final : Fixture {
	std::string name;
	std::string path;
	// contents — sync.OnceValues(func() (string, error)): cached read.
	mutable std::once_flag contentsOnce;
	mutable std::pair<std::string, gostd::Error> contents_;

	std::pair<std::string, gostd::Error> contents() {
		std::call_once(contentsOnce, [this] {
			std::ifstream in(path, std::ios::binary);
			if (!in) {
				contents_ = {"", gostd::errorf("open %s: no such file or directory",
				                             {path})};
				return;
			}
			std::ostringstream ss;
			ss << in.rdbuf();
			contents_ = {ss.str(), nullptr};
		});
		return contents_;
	}

	std::string Name() override { return name; }
	std::string Path() override { return path; }

	void SkipIfNotExist(gostd::testing::T* t) override {
		t->Helper();
		std::error_code ec;
		std::filesystem::status(path, ec);
		if (ec) {
			t->Skipf("Test fixture %q does not exist", {path});
		}
	}

	std::string ReadFile(gostd::testing::T* t) override {
		t->Helper();
		auto [text, err] = contents();
		if (err != nullptr) {
			t->Fatalf("Failed to read test fixture %q: %v", {path, err});
		}
		return text;
	}
};

// fromString — filefixture.go:55.
struct fromString final : Fixture {
	std::string name;
	std::string path;
	std::string contents;

	std::string Name() override { return name; }
	std::string Path() override { return path; }
	void SkipIfNotExist(gostd::testing::T*) override {}
	std::string ReadFile(gostd::testing::T*) override { return contents; }
};

}  // namespace

// FromFile (filefixture.go:22).
std::shared_ptr<Fixture> FromFile(std::string name, std::string path) {
	auto f = std::make_shared<fromFile>();
	f->name = std::move(name);
	f->path = std::move(path);
	return f;
}

// FromString (filefixture.go:61).
std::shared_ptr<Fixture> FromString(std::string name, std::string path,
                                    std::string contents) {
	auto f = std::make_shared<fromString>();
	f->name = std::move(name);
	f->path = std::move(path);
	f->contents = std::move(contents);
	return f;
}

}  // namespace tsc::testutil::filefixture
