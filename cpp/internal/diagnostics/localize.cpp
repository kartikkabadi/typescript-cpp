// keyToMessage / Format / getLocalizedMessages / Localize —
// tsc/internal/diagnostics/diagnostics.go:69-152

#include "internal/diagnostics/diagnostics.h"

#include <iterator>
#include <mutex>

#include "internal/diagnostics/loc_generated.h"
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

// getLocalizedMessages — diagnostics.go:101. Localized tables live in
// loc_generated.h (generated from tsc/internal/diagnostics/loc/*.json by
// tools/genloc.py); the supported-tag list mirrors loc_generated.go's
// language.NewMatcher list (English is the nil index-0 entry).
struct SupportedLocale {
	locale::detail::Tag tag;
	const std::unordered_map<Key, std::string>* table;
};

const std::vector<SupportedLocale>& supportedLocales() {
	static const std::vector<SupportedLocale> v = [] {
		std::vector<SupportedLocale> out;
		auto add = [&](const char* tag,
		               const std::unordered_map<Key, std::string>* table) {
			out.push_back({locale::parse(tag).first.tag, table});
		};
		add("zh-CN", &localized_messages::zh_CN());
		add("zh-TW", &localized_messages::zh_TW());
		add("cs-CZ", &localized_messages::cs_CZ());
		add("de-DE", &localized_messages::de_DE());
		add("es-ES", &localized_messages::es_ES());
		add("fr-FR", &localized_messages::fr_FR());
		add("it-IT", &localized_messages::it_IT());
		add("ja-JP", &localized_messages::ja_JP());
		add("ko-KR", &localized_messages::ko_KR());
		add("pl-PL", &localized_messages::pl_PL());
		add("pt-BR", &localized_messages::pt_BR());
		add("ru-RU", &localized_messages::ru_RU());
		add("tr-TR", &localized_messages::tr_TR());
		return out;
	}();
	return v;
}

const std::unordered_map<Key, std::string>* getLocalizedMessages(
	const locale::Locale& loc) {
	if (loc == locale::Default) {
		return nullptr; // language.Und
	}

	// localizedMessagesCache — diagnostics.go:105.
	static std::mutex cacheMutex;
	static std::unordered_map<std::string,
	                          const std::unordered_map<Key, std::string>*>
	    cache;

	// Go caches on language.Tag (the parsed ID fields); tag.str is empty for
	// compact parsed forms, so key on the canonical String() instead.
	const std::string cacheKey = loc.tag.String();
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		if (auto it = cache.find(cacheKey); it != cache.end()) {
			return it->second;
		}
	}

	// Simplified language.Matcher: same-language candidates ranked by
	// region and script agreement (declared order breaks ties, matching
	// the matcher list order in loc_generated.go).
	const locale::detail::Tag& want = loc.tag;
	const std::unordered_map<Key, std::string>* messages = nullptr;
	{
		int bestScore = -1;
		const locale::detail::Tag hant = locale::parse("zh-Hant").first.tag;
		const locale::detail::Tag zhCN = locale::parse("zh-CN").first.tag;
		for (const auto& e : supportedLocales()) {
			if (e.tag.langID != want.langID) {
				continue;
			}
			int score = 1;
			if (want.regionID != 0 && e.tag.regionID == want.regionID) {
				score += 4;
			}
			if (want.scriptID != 0 && e.tag.scriptID == want.scriptID) {
				score += 2;
			}
			// zh-Hant* requests prefer zh-TW over zh-CN.
			if (e.tag.langID == zhCN.langID &&
			    want.scriptID == hant.scriptID && hant.scriptID != 0 &&
			    e.tag.regionID == zhCN.regionID) {
				score -= 1;
			}
			if (score > bestScore) {
				bestScore = score;
				messages = e.table;
			}
		}
	}

	std::lock_guard<std::mutex> lock(cacheMutex);
	cache.emplace(cacheKey, messages);
	return messages;
}

// Format — diagnostics.go:129 (renamed: `tsc::format` is a namespace).
std::string formatText(std::string_view text, const std::vector<std::string>& args) {
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
			char32_t ch = decodeUtf8RuneStrict(rest, &width);
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

	return formatText(text, args);
}

} // namespace tsc
