// logger.go — port of tsc/internal/lsp/logger.go.
#include "internal/lsp/lsp.h"

namespace tsc::lsp {

// Go's methods on (*logger)(nil) early-return; the server's logger field is
// always non-nil after NewServer, so the C++ guards live at the (non-nil)
// receiver — no caller can observe a difference.

// newLogger — logger.go:19.
logger::logger(Server* s) : server(s), verbosity(lsproto::LogVerbosityInfo) {}

// maxVerbosityForMessageType — logger.go:28. Returns the least-verbose log
// level at which messages of the given LSP MessageType should still be sent.
lsproto::LogVerbosity maxVerbosityForMessageType(lsproto::MessageType msgType) {
	switch (msgType) {
	case lsproto::MessageTypeError:
		return lsproto::LogVerbosityError;
	case lsproto::MessageTypeWarning:
		return lsproto::LogVerbosityWarning;
	case lsproto::MessageTypeInfo:
		return lsproto::LogVerbosityInfo;
	case lsproto::MessageTypeDebug:
		return lsproto::LogVerbosityDebug;
	default:
		return lsproto::LogVerbosityInfo;
	}
}

// isValidLogVerbosity — logger.go:44. Reports whether v is one of the
// defined LogVerbosity values.
bool isValidLogVerbosity(lsproto::LogVerbosity v) {
	return v >= lsproto::LogVerbosityOff && v <= lsproto::LogVerbosityError;
}

// sendLogMessage — logger.go:48.
void logger::sendLogMessage(lsproto::MessageType msgType,
                            const std::string& message) {
	if (!server->initStarted.load()) {
		// fmt.Fprintln(l.server.stderr, message)
		if (server->stderr != nullptr) {
			server->stderr->write(message + "\n");
		}
		return;
	}

	// Don't send messages that the client will filter out anyway.
	lsproto::LogVerbosity verbosity;
	{
		std::lock_guard<std::mutex> lk(mu);
		verbosity = this->verbosity;
	}
	if (verbosity == lsproto::LogVerbosityOff ||
	    verbosity > maxVerbosityForMessageType(msgType)) {
		return;
	}

	auto params = std::make_shared<lsproto::LogMessageParams>();
	params->Type = msgType;
	params->Message = message;
	auto notification =
		lsproto::WindowLogMessageInfo.NewNotificationMessage(params);

	if (auto err = server->outgoingQueue.Put(server->backgroundCtx,
	                                         notification->toMessage());
	    err != nullptr) {
		if (gostd::ctxErr(server->backgroundCtx) != nullptr) {
			if (server->stderr != nullptr) {
				server->stderr->write(message + "\n");
			}
		}
	}
}

// Log — logger.go:78.
void logger::Log(std::initializer_list<gostd::fmtArg> msg) {
	sendLogMessage(lsproto::MessageTypeInfo, detail::sprint(msg));
}

// Logf — logger.go:85.
void logger::Logf(std::string_view format,
                  std::initializer_list<gostd::fmtArg> args) {
	sendLogMessage(lsproto::MessageTypeInfo, gostd::sprintf(format, args));
}

// Verbose — logger.go:92.
tsc::logging::Logger* logger::Verbose() {
	std::lock_guard<std::mutex> lk(mu);
	if (verbosity == lsproto::LogVerbosityOff ||
	    verbosity > lsproto::LogVerbosityDebug) {
		return nullptr;
	}
	return this;
}

// IsVerbose — logger.go:104.
bool logger::IsVerbose() {
	std::lock_guard<std::mutex> lk(mu);
	return verbosity >= lsproto::LogVerbosityTrace &&
	       verbosity <= lsproto::LogVerbosityDebug;
}

// SetVerbose — logger.go:113.
void logger::SetVerbose(bool verbose) {
	std::lock_guard<std::mutex> lk(mu);
	if (verbose) {
		verbosity = lsproto::LogVerbosityDebug;
	} else {
		verbosity = lsproto::LogVerbosityInfo;
	}
}

// IsTracing — logger.go:126.
bool logger::IsTracing() {
	std::lock_guard<std::mutex> lk(mu);
	return verbosity == lsproto::LogVerbosityTrace;
}

// SetVerbosity — logger.go:135.
void logger::SetVerbosity(lsproto::LogVerbosity verbosity) {
	std::lock_guard<std::mutex> lk(mu);
	this->verbosity = verbosity;
}

// Error — logger.go:144.
void logger::Error(std::initializer_list<gostd::fmtArg> msg) {
	sendLogMessage(lsproto::MessageTypeError, detail::sprint(msg));
}

// Errorf — logger.go:151.
void logger::Errorf(std::string_view format,
                    std::initializer_list<gostd::fmtArg> args) {
	sendLogMessage(lsproto::MessageTypeError, gostd::sprintf(format, args));
}

// Warn — logger.go:158.
void logger::Warn(std::initializer_list<gostd::fmtArg> msg) {
	sendLogMessage(lsproto::MessageTypeWarning, detail::sprint(msg));
}

// Warnf — logger.go:165.
void logger::Warnf(std::string_view format,
                   std::initializer_list<gostd::fmtArg> args) {
	sendLogMessage(lsproto::MessageTypeWarning, gostd::sprintf(format, args));
}

// Info — logger.go:172.
void logger::Info(std::initializer_list<gostd::fmtArg> msg) {
	sendLogMessage(lsproto::MessageTypeInfo, detail::sprint(msg));
}

// Infof — logger.go:179.
void logger::Infof(std::string_view format,
                   std::initializer_list<gostd::fmtArg> args) {
	sendLogMessage(lsproto::MessageTypeInfo, gostd::sprintf(format, args));
}

} // namespace tsc::lsp
