// Port of tsc/internal/api/session_batch_test.go (package api).
#include <memory>
#include <string>
#include <vector>

#include "internal/api/proto.h"
#include "internal/api/session.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/testutil/projecttestutil/projecttestutil.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace tsc::api {
namespace {

using gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace projecttestutil = tsc::testutil::projecttestutil;

// sessionOwner — Go `defer projectSession.Close(); defer session.Close()`.
struct sessionCloser {
	project::Session* s = nullptr;
	~sessionCloser() {
		if (s) s->Close();
	}
};
struct apiSessionCloser {
	std::shared_ptr<Session> s;
	~apiSessionCloser() {
		if (s) s->Close();
	}
};

} // namespace

// TestHandleBatchRequests — session_batch_test.go:12.
void TestHandleBatchRequests(T* t) {
	t->Parallel();

	Session session;
	BatchRequestsParams params;
	params.Requests = {
	    BatchRequest{"ping", json::Value{}},
	    BatchRequest{"unknown", json::Value{}},
	};
	auto [response, err] = handleBatchRequests(&session, 
	    gostd::contextBackground(), &params);

	assert::NilError(t, err);
	assert::Equal(t, (int)response->Responses.size(), 2);
	assert::Equal(t, response->Responses[0].Method, Method("ping"));
	assert::Equal(t, std::string(response->Responses[0].Result),
	              std::string("\"pong\""));
	assert::Equal(t, response->Responses[0].Error, std::string(""));
	assert::Equal(t, response->Responses[1].Method, Method("unknown"));
	auto requestErr = response->Responses[1].Error;
	assert::Assert(t, requestErr.find("unknown API method") !=
	                      std::string::npos);

	auto [encoded, merr] = json::marshal(*response);
	assert::Assert(t, merr.empty());
	assert::Assert(t,
	               encoded.find("\"error\":\"api: invalid request: unknown API method \\\"unknown\\\"\"") !=
	                   std::string::npos);
}
REGISTER_UNIT_TEST("api.TestHandleBatchRequests", TestHandleBatchRequests);

// TestHandleBatchRequestsRecoversPerRequestPanics —
// session_batch_test.go:36.
void TestHandleBatchRequestsRecoversPerRequestPanics(T* t) {
	t->Parallel();

	Session* session = nullptr;
	BatchRequestsParams params;
	params.Requests = {
	    BatchRequest{"ping", json::Value{}},
	    BatchRequest{MethodGetAnyType,
	                 json::Value("{\"snapshot\":1,\"project\":\"project\"}")},
	    BatchRequest{"ping", json::Value{}},
	};
	auto [response, err] = handleBatchRequests(session, 
	    gostd::contextBackground(), &params);

	assert::NilError(t, err);
	assert::Equal(t, std::string(response->Responses[0].Result),
	              std::string("\"pong\""));
	assert::Assert(t, response->Responses[1].Error.find("panic:") !=
	                      std::string::npos);
	assert::Equal(t, std::string(response->Responses[2].Result),
	              std::string("\"pong\""));
}
REGISTER_UNIT_TEST("api.TestHandleBatchRequestsRecoversPerRequestPanics",
                   TestHandleBatchRequestsRecoversPerRequestPanics);

// TestBatchResponseEncodesEmptyResult — session_batch_test.go:55.
void TestBatchResponseEncodesEmptyResult(T* t) {
	t->Parallel();

	BatchResponse r;
	r.Method = MethodGetSignaturesOfType;
	r.Result = json::Value("[]");
	auto [encoded, err] = json::marshal(r);
	assert::Assert(t, err.empty());
	assert::Equal(t, encoded,
	              std::string(
	                  "{\"method\":\"getSignaturesOfType\",\"result\":[]}"));
}
REGISTER_UNIT_TEST("api.TestBatchResponseEncodesEmptyResult",
                   TestBatchResponseEncodesEmptyResult);

// TestHandleBatchRequestsPaginatesResponses — session_batch_test.go:62.
void TestHandleBatchRequestsPaginatesResponses(T* t) {
	t->Parallel();

	auto [projectSession, _utils] = projecttestutil::Setup({});
	sessionCloser pclose{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser sclose{session};

	const int maxResponseBytesPerPage = 150;
	std::vector<BatchRequest> requests(10,
	                                   BatchRequest{"ping", json::Value{}});

	BatchRequestsParams params;
	params.Requests = requests;
	params.MaxResponseBytesPerPage = maxResponseBytesPerPage;
	auto [response0, err0] = handleBatchRequests(session.get(), 
	    gostd::contextBackground(), &params);
	assert::NilError(t, err0);
	std::unique_ptr<BatchRequestsResponse> response = std::move(response0);

	std::vector<BatchResponse> responses;
	for (;;) {
		auto [encoded, merr] = json::marshal(*response);
		assert::Assert(t, merr.empty());
		assert::Assert(t, (int)encoded.size() <= maxResponseBytesPerPage);
		// BatchRequestsResponse has no unmarshalJSONFrom (Go decodes it via
		// reflection); read the wire fields through json::parse instead.
		auto [dom, perr] = json::parse(encoded);
		assert::Assert(t, perr == nullptr);
		std::string continuationToken;
		for (auto& [name, member] : dom.obj) {
			if (name == "responses") {
				for (auto& rdom : member.arr) {
					BatchResponse r;
					for (auto& [fname, fval] : rdom.obj) {
						if (fname == "method") r.Method = fval.strVal;
						if (fname == "result") r.Result = json::Value(fval.raw);
						if (fname == "error") r.Error = fval.strVal;
					}
					responses.push_back(r);
				}
			}
			if (name == "continuationToken") {
				continuationToken = member.strVal;
			}
		}
		if (continuationToken.empty()) {
			break;
		}
		BatchRequestsParams next;
		next.ContinuationToken = continuationToken;
		next.MaxResponseBytesPerPage = maxResponseBytesPerPage;
		auto [nextResp, nextErr] = handleBatchRequests(session.get(), 
		    gostd::contextBackground(), &next);
		assert::NilError(t, nextErr);
		response = std::move(nextResp);
	}

	assert::Equal(t, (int)responses.size(), (int)requests.size());
	for (auto& resp : responses) {
		assert::Equal(t, resp.Method, Method("ping"));
		assert::Equal(t, std::string(resp.Result), std::string("\"pong\""));
	}
}
REGISTER_UNIT_TEST("api.TestHandleBatchRequestsPaginatesResponses",
                   TestHandleBatchRequestsPaginatesResponses);

// TestHandleBatchRequestsAllowsOversizedSingleResponse —
// session_batch_test.go:102.
void TestHandleBatchRequestsAllowsOversizedSingleResponse(T* t) {
	t->Parallel();

	auto [projectSession, _utils] = projecttestutil::Setup({});
	sessionCloser pclose{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser sclose{session};

	BatchRequestsParams params;
	params.Requests = {BatchRequest{"ping", json::Value{}}};
	params.MaxResponseBytesPerPage = 1;
	auto [response, err] = handleBatchRequests(session.get(), 
	    gostd::contextBackground(), &params);
	assert::NilError(t, err);
	assert::Equal(t, (int)response->Responses.size(), 1);
	assert::Equal(t, response->ContinuationToken, std::string(""));
}
REGISTER_UNIT_TEST("api.TestHandleBatchRequestsAllowsOversizedSingleResponse",
                   TestHandleBatchRequestsAllowsOversizedSingleResponse);

// TestHandleBatchRequestsPageLimitIsRequestScoped —
// session_batch_test.go:118.
void TestHandleBatchRequestsPageLimitIsRequestScoped(T* t) {
	t->Parallel();

	auto [projectSession, _utils] = projecttestutil::Setup({});
	sessionCloser pclose{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser sclose{session};

	std::vector<BatchRequest> requests = {
	    BatchRequest{"ping", json::Value{}},
	    BatchRequest{"ping", json::Value{}},
	};

	BatchRequestsParams limitedParams;
	limitedParams.Requests = requests;
	limitedParams.MaxResponseBytesPerPage = 1;
	auto [limited, err1] = handleBatchRequests(session.get(), 
	    gostd::contextBackground(), &limitedParams);
	assert::NilError(t, err1);
	assert::Equal(t, (int)limited->Responses.size(), 1);
	assert::Assert(t, !limited->ContinuationToken.empty());

	BatchRequestsParams unlimitedParams;
	unlimitedParams.Requests = requests;
	auto [unlimited, err2] = handleBatchRequests(session.get(), 
	    gostd::contextBackground(), &unlimitedParams);
	assert::NilError(t, err2);
	assert::Equal(t, (int)unlimited->Responses.size(), (int)requests.size());
	assert::Equal(t, unlimited->ContinuationToken, std::string(""));
}
REGISTER_UNIT_TEST("api.TestHandleBatchRequestsPageLimitIsRequestScoped",
                   TestHandleBatchRequestsPageLimitIsRequestScoped);

// TestHandleBatchRequestsRejectsInvalidContinuationToken —
// session_batch_test.go:144.
void TestHandleBatchRequestsRejectsInvalidContinuationToken(T* t) {
	t->Parallel();

	auto [projectSession, _utils] = projecttestutil::Setup({});
	sessionCloser pclose{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser sclose{session};

	BatchRequestsParams params;
	params.ContinuationToken = "invalid";
	auto [_r, err] = handleBatchRequests(session.get(), gostd::contextBackground(),
	                                            &params);
	assert::Assert(t, err != nullptr &&
	                      err->Error().find("invalid batch continuation token") !=
	                          std::string::npos,
	               "expected 'invalid batch continuation token' error");
}
REGISTER_UNIT_TEST("api.TestHandleBatchRequestsRejectsInvalidContinuationToken",
                   TestHandleBatchRequestsRejectsInvalidContinuationToken);

// TestHandleBatchRequestsRejectsNestedBatch — session_batch_test.go:159.
void TestHandleBatchRequestsRejectsNestedBatch(T* t) {
	t->Parallel();

	auto [projectSession, _utils] = projecttestutil::Setup({});
	sessionCloser pclose{projectSession};
	auto session = NewLSPSession(projectSession, nullptr);
	apiSessionCloser sclose{session};

	BatchRequestsParams params;
	params.Requests = {BatchRequest{MethodBatchRequests,
	                              json::Value("{\"requests\":[]}")}};
	auto [response, err] = handleBatchRequests(session.get(), 
	    gostd::contextBackground(), &params);
	assert::NilError(t, err);
	assert::Equal(t, (int)response->Responses.size(), 1);
	assert::Assert(t,
	               response->Responses[0].Error.find(
	                   "batchRequests cannot be nested") != std::string::npos);
}
REGISTER_UNIT_TEST("api.TestHandleBatchRequestsRejectsNestedBatch",
                   TestHandleBatchRequestsRejectsNestedBatch);

} // namespace tsc::api
