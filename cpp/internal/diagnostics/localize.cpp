// keyToMessage / Format / getLocalizedMessages / Localize —
// tsc/internal/diagnostics/diagnostics.go:69-152

#include "internal/diagnostics/diagnostics.h"

#include <iterator>

#include "internal/diagnostics/messages_generated.h"
#include "internal/locale/locale.h"
#include "internal/stringutil/stringutil.h"

namespace tsc {

namespace {

// messagesByKey — diagnostics.go:69
const std::unordered_map<Key, const DiagnosticMessage*>& messagesByKey() {
	static const std::unordered_map<Key, const DiagnosticMessage*>* map =
	    [] {
		    auto* m = new std::unordered_map<Key, const DiagnosticMessage*>();
		    m->reserve(std::size(kDiagnosticMessages));
		    for (const DiagnosticMessage& message : kDiagnosticMessages) {
			    (*m)[Key(message.key)] = &message;
		    }
		    return m;
	    }();
	return *map;
}

} // namespace

const DiagnosticMessage* keyToMessage(std::string_view key) {
	auto it = messagesByKey().find(Key(key));
	return it != messagesByKey().end() ? it->second : nullptr;
}

// getLocalizedMessages — diagnostics.go:101. Localized message tables are
// generated data (diagnostics/loc_generated.go + the language.Matcher over
// localeFuncs); no localized tables are ported, so this always finds nothing.
const std::unordered_map<Key, std::string>* getLocalizedMessages(
	const locale::Locale& loc) {
	if (loc == locale::Default) {
		return nullptr; // language.Und
	}
	return nullptr;
}

// Format — diagnostics.go:129
std::string format(std::string_view text, const std::vector<std::string>& args) {
	if (args.empty()) {
		return std::string(text);
	}

	// Replace invalid UTF-8 with Unicode replacement character
	std::vector<std::string> cleanArgs(args);
	for (std::string& arg : cleanArgs) {
		// strings.ToValidUTF8(arg, "\uFFFD")
		std::string cleaned;
		std::string_view rest(arg);
		while (!rest.empty()) {
			int width;
			char32_t ch = decodeUtf8Rune(rest, &width);
			if (ch == kRuneError && width <= 1) {
				cleaned += "\xEF\xBF\xBD"; // U+FFFD
				rest.remove_prefix(1);
			} else {
				cleaned.append(rest.data(), width);
				rest.remove_prefix(width);
			}
		}
		arg = std::move(cleaned);
	}

	// placeholderRegexp.ReplaceAllStringFunc — {(\d+)}
	std::string out;
	size_t i = 0;
	while (i < text.size()) {
		if (text[i] == '{') {
			size_t j = i + 1;
			while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
				j++;
			}
			if (j > i + 1 && j < text.size() && text[j] == '}') {
				long long index = std::stoll(std::string(text.substr(i + 1, j - i - 1)));
				if (index < 0 || index >= (long long)cleanArgs.size()) {
					tscUnreachable("Invalid formatting placeholder");
				}
				out += cleanArgs[index];
				i = j + 1;
				continue;
			}
		}
		out += text[i];
		i++;
	}
	return out;
}

// Localize — diagnostics.go:82
std::string localize(const locale::Locale& loc, const DiagnosticMessage* message,
                     Key key, const std::vector<std::string>& args) {
	if (message == nullptr) {
		message = keyToMessage(key);
	}
	if (message == nullptr) {
		tscUnreachable("Unknown diagnostic message");
	}

	std::string_view text = message->text;
	if (const auto* localized = getLocalizedMessages(loc)) {
		if (auto it = localized->find(Key(message->key)); it != localized->end()) {
			text = it->second;
		}
	}

	return format(text, args);
}

} // namespace tsc
