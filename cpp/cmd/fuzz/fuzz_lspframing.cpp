// fuzz_lspframing — libFuzzer driver for the LSP wire path.
//
// Feeds raw bytes through exactly what `tsc tsc --lsp` reads off stdio:
//   jsonrpc::Reader::Read()   — Content-Length header framing (baseproto)
//   json::unmarshal           — payload → lsproto::Message (JSON-RPC decode)
// via lsp::ToReader → lspReader::Read — the same function the server loop
// calls per message. One fuzz input may frame several messages; the loop
// reads until the first transport error, capped to keep iteration cheap.

#include <cstdint>

#include "internal/gostd/gostd.h"
#include "internal/lsp/lsp.h"

#include "cmd/fuzz/fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size == 0) {
		return 0;
	}
	fuzz::runOnBigStack(
	    [](const uint8_t* d, size_t n) {
		    fuzz::SliceReader sr;
		    sr.data = d;
		    sr.size = n;
		    auto reader = tsc::lsp::ToReader(&sr);
		    for (int i = 0; i < 64; i++) {
			    auto [msg, err] = reader->Read();
			    if (err != nullptr) {
				    break;
			    }
		    }
	    },
	    data, size);
	return 0;
}
