// Port of tsc/internal/lsp/lsproto/baseproto_test.go.
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/lsp/lsproto/lsproto_runtime.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

namespace assert = tsc::gotest::assert;
namespace gostd = tsc::gostd;
namespace lsproto = tsc::lsp::lsproto;
using tsc::gostd::testing::T;
using tsc::gostd::Error;

// stringReader — bytes.NewReader.
struct stringReader : gostd::io::Reader {
	std::string data;
	size_t pos = 0;
	explicit stringReader(std::string s) : data(std::move(s)) {}
	std::pair<int, Error> read(std::span<char> buf) override {
		if (pos >= data.size()) {
			return {0, gostd::io::errEOF};
		}
		size_t n = std::min(buf.size(), data.size() - pos);
		std::memcpy(buf.data(), data.data() + pos, n);
		pos += n;
		return {(int)n, nullptr};
	}
};

// bufWriter — bytes.Buffer as io.Writer.
struct bufWriter : gostd::io::Writer {
	std::string buf;
	std::pair<int, Error> write(std::string_view data) override {
		buf.append(data);
		return {(int)data.size(), nullptr};
	}
};

// errorWriter — the test's errorWriter.
struct errorWriter : gostd::io::Writer {
	std::pair<int, Error> write(std::string_view) override {
		return {0, gostd::newError("test error")};
	}
};

void TestBaseReader(T* t) {
	t->Parallel();
	struct Case {
		std::string name;
		std::string input;
		std::string value;
		std::string err;
	};
	std::vector<Case> tests{
	    {"empty", "Content-Length: 0\r\n\r\n", "",
	     "jsonrpc: no content length"},
	    {"early end", "oops", "", "EOF"},
	    {"negative length", "Content-Length: -1\r\n\r\n", "",
	     "jsonrpc: invalid content length: negative value -1"},
	    {"invalid content", "Content-Length: 1\r\n\r\n{", "{", ""},
	    {"valid content", "Content-Length: 2\r\n\r\n{}", "{}", ""},
	    {"extra header values", "Content-Length: 2\r\nExtra: 1\r\n\r\n{}",
	     "{}", ""},
	    {"too long content length", "Content-Length: 100\r\n\r\n{}", "",
	     "jsonrpc: read content: unexpected EOF"},
	    {"missing content length", "Content-Length: \r\n\r\n{}", "",
	     "jsonrpc: invalid content length: parse error: "
	     "strconv.ParseInt: parsing \"\": invalid syntax"},
	    {"invalid header", "Nope\r\n\r\n{}", "",
	     "jsonrpc: invalid header: \"Nope\\r\\n\""},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&](T* t) {
			t->Parallel();
			stringReader sr(tt.input);
			auto r = lsproto::NewBaseReader(&sr);

			auto [out, err] = r.Read();
			if (!tt.err.empty()) {
				assert::Error(t, err, tt.err);
			}
			assert::DeepEqual(t, out, tt.value);
		});
	}
}
REGISTER_UNIT_TEST("lsproto.TestBaseReader", TestBaseReader);

void TestBaseReaderMultipleReads(T* t) {
	t->Parallel();

	std::string data = "Content-Length: 4\r\n\r\n1234"
	                   "Content-Length: 2\r\n\r\n{}";
	stringReader sr(data);
	auto r = lsproto::NewBaseReader(&sr);

	auto [v1, err] = r.Read();
	assert::NilError(t, err);
	assert::DeepEqual(t, v1, std::string("1234"));

	auto [v2, err2] = r.Read();
	assert::NilError(t, err2);
	assert::DeepEqual(t, v2, std::string("{}"));

	auto [v3, err3] = r.Read();
	assert::Error(t, err3, "EOF");
}
REGISTER_UNIT_TEST("lsproto.TestBaseReaderMultipleReads",
                   TestBaseReaderMultipleReads);

void TestBaseWriter(T* t) {
	t->Parallel();
	struct Case {
		std::string name;
		std::string value;
		std::string input;
	};
	std::vector<Case> tests{
	    {"empty", "{}", "Content-Length: 2\r\n\r\n{}"},
	    {"bigger object", "{\"key\":\"value\"}",
	     "Content-Length: 15\r\n\r\n{\"key\":\"value\"}"},
	};

	for (auto& tt : tests) {
		t->Run(tt.name, [&](T* t) {
			t->Parallel();
			bufWriter b;
			auto w = lsproto::NewBaseWriter(&b);
			auto err = w.Write(tt.value);
			assert::NilError(t, err);
			assert::DeepEqual(t, b.buf, tt.input);
		});
	}
}
REGISTER_UNIT_TEST("lsproto.TestBaseWriter", TestBaseWriter);

void TestBaseWriterWriteError(T* t) {
	t->Parallel();

	errorWriter ew;
	auto w = lsproto::NewBaseWriter(&ew);
	auto err = w.Write("{}");
	assert::Error(t, err, "test error");
}
REGISTER_UNIT_TEST("lsproto.TestBaseWriterWriteError",
                   TestBaseWriterWriteError);

}  // namespace
