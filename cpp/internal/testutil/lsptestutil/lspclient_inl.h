// lspclient_inl.h — template methods of LSPClient (lspclient.go SendRequest
// / SendRequestAsync / SendNotification).
#pragma once

namespace tsc::testutil::lsptestutil {

// SendRequest — lspclient.go:250.
template <class Params, class Resp>
std::tuple<std::shared_ptr<lsproto::Message>, Resp, bool>
LSPClient::SendRequest(gostd::testing::T* t,
                       const lsproto::RequestInfo<Params, Resp>& info,
                       const Params& params) {
	int32_t id = NextID();
	auto reqID = lsproto::NewID(lsproto::IntegerOrString{
	    .Integer = std::make_shared<int32_t>(id),
	    .String = nullptr,
	});
	auto req = info.NewRequestMessage(reqID, params);

	auto [resp, ok] = SendRequestWorker(t, req, reqID);
	if (!ok) {
		return {nullptr, Resp{}, false};
	}
	// The result arrives as a raw json.Value; decode it into Resp.
	auto [result, err] = info.UnmarshalResult(resp->Result);
	return {resp->toMessage(), result, err == nullptr};
}

// SendRequestAsync — lspclient.go:265.
template <class Params, class Resp>
std::function<std::tuple<std::shared_ptr<lsproto::Message>, Resp, bool>()>
LSPClient::SendRequestAsync(
    gostd::testing::T* t, const lsproto::RequestInfo<Params, Resp>& info,
    const Params& params) {
	int32_t id = NextID();
	auto reqID = lsproto::NewID(lsproto::IntegerOrString{
	    .Integer = std::make_shared<int32_t>(id),
	    .String = nullptr,
	});
	auto req = info.NewRequestMessage(reqID, params);

	std::shared_ptr<responseChan> responseChan_ =
	    startRequestWorker(t, req, reqID);
	return [this, t, reqID, responseChan_, info]()
	           -> std::tuple<std::shared_ptr<lsproto::Message>, Resp,
	                         bool> {
		auto [resp, ok] = waitForResponse(t, reqID, responseChan_);
		if (!ok) {
			return {nullptr, Resp{}, false};
		}
		auto [result, err] = info.UnmarshalResult(resp->Result);
		return {resp->toMessage(), result, err == nullptr};
	};
}

// SendNotification — lspclient.go:317.
template <class Params>
void LSPClient::SendNotification(
    gostd::testing::T* t, const lsproto::NotificationInfo<Params>& info,
    const Params& params) {
	auto notification = info.NewNotificationMessage(params);
	WriteMsg(t, notification->toMessage());
}

}  // namespace tsc::testutil::lsptestutil
