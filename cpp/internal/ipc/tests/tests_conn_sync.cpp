// tests_conn_sync.cpp — port of tsc/internal/ipc/conn_sync_test.go.
#include <memory>
#include <string>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/ipc/ipc.h"
#include "internal/json/json.h"
#include "internal/jsonrpc/jsonrpc.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::ipc {
namespace {

using gostd::testing::T;

struct noOpHandler : Handler {
	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		return {json::Value{}, gostd::Error()};
	}
	gostd::Error
	HandleNotification(gostd::Context ctx, std::string_view method,
	                   const json::Value& params) override {
		return gostd::Error();
	}
};

struct syncFailingResponseProtocol : Protocol {
	Message* message = nullptr;
	gostd::Error responseErr;

	std::pair<Message*, gostd::Error> ReadMessage() override {
		if (message == nullptr) {
			return {nullptr, gostd::io::errEOF};
		}
		auto* m = message;
		message = nullptr;
		return {m, gostd::Error()};
	}
	gostd::Error WriteRequest(const jsonrpc::ID* id, std::string_view method,
	                          const json::Value& params) override {
		return gostd::Error();
	}
	gostd::Error
	WriteNotification(std::string_view method,
	                  const json::Value& params) override {
		return gostd::Error();
	}
	gostd::Error WriteResponse(const jsonrpc::ID* id,
	                           const json::Value& result) override {
		return responseErr;
	}
	gostd::Error
	WriteError(const jsonrpc::ID* id,
	           const jsonrpc::ResponseError* err) override {
		return responseErr;
	}
};

struct panicHandler : Handler {
	std::pair<json::Value, gostd::Error>
	HandleRequest(gostd::Context ctx, std::string_view method,
	              const json::Value& params) override {
		throw std::string("handler panic");
	}
	gostd::Error
	HandleNotification(gostd::Context ctx, std::string_view method,
	                   const json::Value& params) override {
		return gostd::Error();
	}
};

void TestSyncConnRunReturnsResponseWriteFailure(T* t) {
	t->Parallel();
	auto responseErr = gostd::newError("response write failed");
	auto* protocol = new syncFailingResponseProtocol();
	protocol->message = new Message{
	    .Id = jsonrpc::NewIDInt(1), .Method = "transform"};
	protocol->responseErr = responseErr;
	auto conn = NewSyncConn(
	    nullptr, std::shared_ptr<Protocol>(protocol),
	    std::shared_ptr<Handler>(std::make_shared<noOpHandler>()));

	auto err = conn->Run(t->Context());
	if (!gostd::errorIs(err, responseErr))
		t->Error({gostd::sprintf(
		    "expected response write error, got %v", {err->Error()})});
}
REGISTER_UNIT_TEST("ipc.TestSyncConnRunReturnsResponseWriteFailure",
                   TestSyncConnRunReturnsResponseWriteFailure);

void TestSyncConnRunReturnsPanicResponseWriteFailure(T* t) {
	t->Parallel();
	auto responseErr = gostd::newError("response write failed");
	auto* protocol = new syncFailingResponseProtocol();
	protocol->message = new Message{
	    .Id = jsonrpc::NewIDInt(1), .Method = "transform"};
	protocol->responseErr = responseErr;
	auto conn = NewSyncConn(
	    nullptr, std::shared_ptr<Protocol>(protocol),
	    std::shared_ptr<Handler>(std::make_shared<panicHandler>()));

	auto err = conn->Run(t->Context());
	if (!gostd::errorIs(err, responseErr))
		t->Error({gostd::sprintf(
		    "expected panic response write error, got %v",
		    {err->Error()})});
	if (err->Error().find("original panic: handler panic") ==
	    std::string::npos)
		t->Error({gostd::sprintf(
		    "expected error to mention original panic, got %v",
		    {err->Error()})});
}
REGISTER_UNIT_TEST(
    "ipc.TestSyncConnRunReturnsPanicResponseWriteFailure",
    TestSyncConnRunReturnsPanicResponseWriteFailure);

} // namespace
} // namespace tsc::ipc
