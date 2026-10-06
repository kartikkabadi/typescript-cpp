// === slice: fourslash ===
// Go standard-library helpers used by the fourslash port that have no
// existing analog in gostd: strings.Builder, unicode/utf8 decoding,
// slices/maps helpers, io.Pipe, errgroup, chan, iter.Seq, and a defer
// scope guard.
#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"

namespace tsc::gostr {

// ---------------------------------------------------------------------------
// strings.Builder — minimal faithful surface (Builder, WriteString/WriteByte,
// Len, String, Reset; invalid to copy after first use is not modeled).
// ---------------------------------------------------------------------------
struct Builder : gostd::io::Writer {
	std::string buf;
	void WriteString(std::string_view s) { buf += s; }
	void WriteByte(char b) { buf += b; }
	// io.Writer surface (implements Go's io.Writer usage via fmt.Fprint).
	std::pair<int, gostd::Error> write(std::string_view data) override {
		buf += data;
		return {(int)data.size(), nullptr};
	}
	size_t Len() const { return buf.size(); }
	std::string String() const { return buf; }
	void Reset() { buf.clear(); }
};

// ---------------------------------------------------------------------------
// unicode/utf8: DecodeRuneInString, RuneLen (utf16.RuneLen), RuneError.
// ---------------------------------------------------------------------------
inline constexpr char32_t runeError = 0xFFFD;
inline constexpr char32_t maxRune = 0x10FFFF;

// Returns (rune, size). Decodes per Go's utf8.DecodeRuneInString: invalid
// sequences yield (RuneError, 1); empty yields (RuneError, 0).
inline std::pair<char32_t, int> decodeRuneInString(std::string_view s) {
	if (s.empty()) {
		return {runeError, 0};
	}
	const auto* p = reinterpret_cast<const unsigned char*>(s.data());
	unsigned char c0 = p[0];
	if (c0 < 0x80) {
		return {(char32_t)c0, 1};
	}
	if (c0 < 0xC2) {
		return {runeError, 1};
	}
	int size;
	char32_t min;
	if (c0 < 0xE0) {
		size = 2;
		min = 0x80;
	} else if (c0 < 0xF0) {
		size = 3;
		min = 0x800;
	} else if (c0 < 0xF5) {
		size = 4;
		min = 0x10000;
	} else {
		return {runeError, 1};
	}
	if ((int)s.size() < size) {
		return {runeError, 1};
	}
	char32_t r = c0 & ((1 << (7 - size)) - 1);
	for (int i = 1; i < size; i++) {
		unsigned char cx = p[i];
		if (cx < 0x80 || cx >= 0xC0) {
			return {runeError, 1};
		}
		r = (r << 6) | (cx & 0x3F);
	}
	if (r < min || r > maxRune || (r >= 0xD800 && r <= 0xDFFF)) {
		return {runeError, 1};
	}
	return {r, size};
}

// Iterates decoded runes of s (like Go's range-over-string).
template <typename F>
void forEachRune(std::string_view s, F&& f) {
	for (size_t i = 0; i < s.size();) {
		auto [r, size] = decodeRuneInString(s.substr(i));
		f(r, i);
		i += size;
	}
}

// utf16.RuneLen — UTF-16 code units needed for r.
inline int utf16RuneLen(char32_t r) {
	return r > 0xFFFF ? 2 : 1;
}

// ---------------------------------------------------------------------------
// strings helpers (Go strings package semantics)
// ---------------------------------------------------------------------------
inline bool hasPrefix(std::string_view s, std::string_view prefix) {
	return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}
inline bool hasSuffix(std::string_view s, std::string_view suffix) {
	return s.size() >= suffix.size() &&
	       s.substr(s.size() - suffix.size()) == suffix;
}
inline bool contains(std::string_view s, std::string_view substr) {
	return s.find(substr) != std::string_view::npos;
}
inline int index(std::string_view s, std::string_view substr) {
	auto p = s.find(substr);
	return p == std::string_view::npos ? -1 : (int)p;
}
inline int lastIndex(std::string_view s, std::string_view substr) {
	auto p = s.rfind(substr);
	return p == std::string_view::npos ? -1 : (int)p;
}
inline std::string trimPrefix(std::string_view s, std::string_view prefix) {
	return hasPrefix(s, prefix) ? std::string(s.substr(prefix.size()))
	                            : std::string(s);
}
inline std::string trimSuffix(std::string_view s, std::string_view suffix) {
	return hasSuffix(s, suffix)
	           ? std::string(s.substr(0, s.size() - suffix.size()))
	           : std::string(s);
}
// strings.CutPrefix — (after, found).
inline std::pair<std::string, bool> cutPrefix(std::string_view s,
                                              std::string_view prefix) {
	if (hasPrefix(s, prefix)) {
		return {std::string(s.substr(prefix.size())), true};
	}
	return {std::string(s), false};
}
// strings.CutSuffix — (before, found).
inline std::pair<std::string, bool> cutSuffix(std::string_view s,
                                              std::string_view suffix) {
	if (hasSuffix(s, suffix)) {
		return {std::string(s.substr(0, s.size() - suffix.size())), true};
	}
	return {std::string(s), false};
}
// strings.Cut — (before, after, found).
inline std::tuple<std::string, std::string, bool>
cut(std::string_view s, std::string_view sep) {
	auto p = s.find(sep);
	if (p == std::string_view::npos) {
		return {std::string(s), "", false};
	}
	return {std::string(s.substr(0, p)),
	        std::string(s.substr(p + sep.size())), true};
}
inline bool isSpace(char32_t r) {
	return r == ' ' || r == '\t' || r == '\n' || r == '\v' || r == '\f' ||
	       r == '\r' || r == 0x85 || r == 0xA0 || r == 0x1680 ||
	       (r >= 0x2000 && r <= 0x200A) || r == 0x2028 || r == 0x2029 ||
	       r == 0x202F || r == 0x205F || r == 0x3000 || r == 0xFEFF;
}
inline std::string trimSpace(std::string_view s) {
	size_t b = 0, e = s.size();
	while (b < e) {
		auto [r, size] = decodeRuneInString(s.substr(b, e - b));
		if (!isSpace(r)) break;
		b += size;
	}
	while (e > b) {
		// decode backwards — scan for last rune start byte.
		size_t i = e - 1;
		while (i > b && (s[i] & 0xC0) == 0x80) i--;
		auto [r, size] = decodeRuneInString(s.substr(i, e - i));
		if (!isSpace(r) || i + size != e) break;
		e = i;
	}
	return std::string(s.substr(b, e - b));
}
inline std::string trimRight(std::string_view s, std::string_view cutset) {
	while (!s.empty() && cutset.find(s.back()) != std::string_view::npos) {
		s = s.substr(0, s.size() - 1);
	}
	return std::string(s);
}
inline std::string repeat(std::string_view s, int count) {
	std::string out;
	out.reserve(s.size() * count);
	for (int i = 0; i < count; i++) out += s;
	return out;
}
inline std::vector<std::string> split(std::string_view s,
                                      std::string_view sep) {
	std::vector<std::string> out;
	if (sep.empty()) {
		forEachRune(s, [&](char32_t r, size_t i) {
			out.push_back(std::string(s.substr(i, 1)));
			(void)r;
		});
		return out;
	}
	size_t pos = 0;
	while (true) {
		auto p = s.find(sep, pos);
		if (p == std::string_view::npos) {
			out.push_back(std::string(s.substr(pos)));
			break;
		}
		out.push_back(std::string(s.substr(pos, p - pos)));
		pos = p + sep.size();
	}
	return out;
}
inline std::string join(const std::vector<std::string>& elems,
                        std::string_view sep) {
	std::string out;
	for (size_t i = 0; i < elems.size(); i++) {
		if (i) out += sep;
		out += elems[i];
	}
	return out;
}
inline std::string replaceAll(std::string_view s, std::string_view oldS,
                              std::string_view newS) {
	std::string out;
	size_t pos = 0;
	while (true) {
		auto p = s.find(oldS, pos);
		if (p == std::string_view::npos) {
			out += s.substr(pos);
			break;
		}
		out += s.substr(pos, p - pos);
		out += newS;
		pos = p + oldS.size();
	}
	return out;
}
inline std::string replace(std::string_view s, std::string_view oldS,
                           std::string_view newS, int n) {
	// strings.Replace(s, old, new, n): n<0 = ReplaceAll.
	std::string out;
	size_t pos = 0;
	int done = 0;
	while (n < 0 || done < n) {
		auto p = s.find(oldS, pos);
		if (p == std::string_view::npos) {
			break;
		}
		out += s.substr(pos, p - pos);
		out += newS;
		pos = p + oldS.size();
		done++;
	}
	out += s.substr(pos);
	return out;
}

// string(rune) — encode a single rune as UTF-8.
inline std::string fromRune(char32_t r) {
	if (r < 0x80) {
		return std::string(1, (char)r);
	}
	if (r < 0x800) {
		std::string out;
		out += (char)(0xC0 | (r >> 6));
		out += (char)(0x80 | (r & 0x3F));
		return out;
	}
	if (r < 0x10000) {
		std::string out;
		out += (char)(0xE0 | (r >> 12));
		out += (char)(0x80 | ((r >> 6) & 0x3F));
		out += (char)(0x80 | (r & 0x3F));
		return out;
	}
	std::string out;
	out += (char)(0xF0 | (r >> 18));
	out += (char)(0x80 | ((r >> 12) & 0x3F));
	out += (char)(0x80 | ((r >> 6) & 0x3F));
	out += (char)(0x80 | (r & 0x3F));
	return out;
}

// []rune conversions (Go []rune(string) / string([]rune)).
inline std::vector<char32_t> toRunes(std::string_view s) {
	std::vector<char32_t> out;
	size_t i = 0;
	while (i < s.size()) {
		auto [r, size] = decodeRuneInString(s.substr(i));
		out.push_back(r);
		i += (size_t)size;
	}
	return out;
}
inline std::string fromRunes(const std::vector<char32_t>& runes) {
	std::string out;
	for (char32_t r : runes) {
		if (r < 0x80) {
			out += (char)r;
		} else if (r < 0x800) {
			out += (char)(0xC0 | (r >> 6));
			out += (char)(0x80 | (r & 0x3F));
		} else if (r < 0x10000) {
			out += (char)(0xE0 | (r >> 12));
			out += (char)(0x80 | ((r >> 6) & 0x3F));
			out += (char)(0x80 | (r & 0x3F));
		} else {
			out += (char)(0xF0 | (r >> 18));
			out += (char)(0x80 | ((r >> 12) & 0x3F));
			out += (char)(0x80 | ((r >> 6) & 0x3F));
			out += (char)(0x80 | (r & 0x3F));
		}
	}
	return out;
}

inline int count(std::string_view s, std::string_view substr) {
	if (substr.empty()) return -1; // Go: len(utf8 runes)+1 — unused here
	int n = 0;
	size_t pos = 0;
	while ((pos = s.find(substr, pos)) != std::string_view::npos) {
		n++;
		pos += substr.size();
	}
	return n;
}
inline std::string toLower(std::string_view s) {
	std::string out(s);
	std::transform(out.begin(), out.end(), out.begin(),
	               [](unsigned char c) { return (char)std::tolower(c); });
	return out;
}
// strings.Fields — split on runs of unicode spaces.
inline std::vector<std::string> fields(std::string_view s) {
	std::vector<std::string> out;
	size_t i = 0;
	while (i < s.size()) {
		while (i < s.size()) {
			auto [r, size] = decodeRuneInString(s.substr(i));
			if (!isSpace(r)) break;
			i += size;
		}
		if (i >= s.size()) break;
		size_t start = i;
		while (i < s.size()) {
			auto [r, size] = decodeRuneInString(s.substr(i));
			if (isSpace(r)) break;
			i += size;
		}
		out.push_back(std::string(s.substr(start, i - start)));
	}
	return out;
}

// ---------------------------------------------------------------------------
// slices helpers
// ---------------------------------------------------------------------------
template <typename T, typename Cmp>
void sortFunc(std::vector<T>& v, Cmp&& cmp) {
	std::sort(v.begin(), v.end(),
	          [&](const T& a, const T& b) { return cmp(a, b) < 0; });
}
template <typename T, typename Cmp>
void sortStableFunc(std::vector<T>& v, Cmp&& cmp) {
	std::stable_sort(v.begin(), v.end(),
	                 [&](const T& a, const T& b) { return cmp(a, b) < 0; });
}
template <typename T>
std::vector<T> clone(const std::vector<T>& v) {
	return v;
}
template <typename T>
bool slicesContains(const std::vector<T>& v, const T& x) {
	return std::find(v.begin(), v.end(), x) != v.end();
}
template <typename T>
bool slicesEqual(const std::vector<T>& a, const std::vector<T>& b) {
	return a == b;
}
template <typename T, typename F>
int indexFunc(const std::vector<T>& v, F&& pred) {
	for (int i = 0; i < (int)v.size(); i++) {
		if (pred(v[i])) return i;
	}
	return -1;
}
template <typename T>
std::vector<T> insert(std::vector<T> v, int i, const T& x) {
	v.insert(v.begin() + i, x);
	return v;
}
template <typename T>
std::vector<T> slicesConcat(std::initializer_list<std::vector<T>> vs) {
	std::vector<T> out;
	for (auto& v : vs) {
		out.insert(out.end(), v.begin(), v.end());
	}
	return out;
}
// slices.BinarySearch — returns (index, found-exact).
template <typename T>
std::pair<int, bool> binarySearch(const std::vector<T>& v, const T& target) {
	auto it = std::lower_bound(v.begin(), v.end(), target);
	int i = (int)(it - v.begin());
	return {i, it != v.end() && *it == target};
}

// ---------------------------------------------------------------------------
// maps helpers — iteration order in Go is unspecified; callers that depend on
// order sort explicitly, so unordered_map + materialized key vectors mirrors it.
// ---------------------------------------------------------------------------
template <typename K, typename V>
std::vector<K> mapKeys(const std::unordered_map<K, V>& m) {
	std::vector<K> out;
	out.reserve(m.size());
	for (auto& kv : m) out.push_back(kv.first);
	return out;
}

// maps.Equal — same key set and values (== on V).
template <typename K, typename V>
bool mapsEqual(const std::unordered_map<K, V>& a,
               const std::unordered_map<K, V>& b) {
	if (a.size() != b.size()) return false;
	for (auto& [k, v] : a) {
		auto it = b.find(k);
		if (it == b.end() || !(it->second == v)) return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// cmp.Compare — Go cmp.Compare[T cmp.Ordered]: -1, 0, +1.
// ---------------------------------------------------------------------------
template <typename T>
int cmpOrdered(const T& a, const T& b) {
	if (a < b) return -1;
	if (a > b) return 1;
	return 0;
}

// ---------------------------------------------------------------------------
// core helpers used widely
// ---------------------------------------------------------------------------
template <typename T, typename F>
auto coreMap(const std::vector<T>& v, F&& f)
    -> std::vector<std::decay_t<decltype(f(v[0]))>> {
	using R = std::decay_t<decltype(f(v[0]))>;
	std::vector<R> out;
	out.reserve(v.size());
	for (auto& x : v) out.push_back(f(x));
	return out;
}
template <typename T, typename F>
std::vector<T> coreFilter(const std::vector<T>& v, F&& f) {
	std::vector<T> out;
	for (auto& x : v) {
		if (f(x)) out.push_back(x);
	}
	return out;
}
// core.MapFiltered — applies f, keeps non-nil results (f returns a
// Go `*R`; modelled as std::optional<R>).
template <typename T, typename F>
auto coreMapFiltered(const std::vector<T>& v, F&& f)
    -> std::vector<std::decay_t<decltype(*f(v[0]))>> {
	using R = std::decay_t<decltype(*f(v[0]))>;
	std::vector<R> out;
	for (auto& x : v) {
		if (auto r = f(x)) out.push_back(*r);
	}
	return out;
}
template <typename T, typename F>
T coreFind(const std::vector<T>& v, F&& f) {
	for (auto& x : v) {
		if (f(x)) return x;
	}
	return T{};
}
template <typename T>
T firstOrNil(const std::vector<T>& v) {
	return v.empty() ? T{} : v.front();
}
template <typename T>
T lastOrNil(const std::vector<T>& v) {
	return v.empty() ? T{} : v.back();
}
template <typename T>
T orElse(const T& v, const T& fallback) {
	return v ? v : fallback;
}
// %T-style name of a variant's payload (used in verify switch errors).
template <typename... Ts>
inline std::string variantTypeName(const std::variant<Ts...>& v) {
	return std::visit(
	    [](const auto& x) -> std::string { return typeid(x).name(); },
	    v);
}

// core.IfElse — Go ternary.
template <typename T>
T ifElse(bool cond, const T& a, const T& b) {
	return cond ? a : b;
}


// lowerFirstChar — lowers the first rune of the joined name (Go
// `strutil.LowerFirstChar` equivalent used by normalizeCommandName).
inline std::string lowerFirstChar(std::string_view s) {
	if (s.empty()) return std::string(s);
	std::string out(s);
	out[0] = (char)std::tolower((unsigned char)out[0]);
	return out;
}

// fprint — fmt.Fprint(w, s): writes a preformatted string to an
// io.Writer (fmt.Fprintf call sites preformat with gostd::sprintf).
inline void fprint(gostd::io::Writer* w, std::string_view s) {
	w->write(s);
}

// padRight — Go's `%-*s` (left-justify in a field of width n) used by
// diffTable::print.
inline std::string padRight(std::string_view s, int width) {
	std::string out(s);
	if ((int)out.size() < width) out.append(width - out.size(), ' ');
	return out;
}

} // namespace tsc::gostr

// ---------------------------------------------------------------------------
// iter.Seq[T] — Go's func(yield func(T) bool) sequence.
// ---------------------------------------------------------------------------
namespace tsc::goseq {
template <typename T>
using Seq = std::function<void(const std::function<bool(T)>&)>;

// Collect iterates a Seq into a vector (slices.Collect).
template <typename T>
std::vector<T> collect(const Seq<T>& seq) {
	std::vector<T> out;
	if (seq) {
		seq([&](T v) {
			out.push_back(std::move(v));
			return true;
		});
	}
	return out;
}

// Sorted collects and sorts (slices.Sorted).
template <typename T>
std::vector<T> sorted(const Seq<T>& seq) {
	auto out = collect(seq);
	std::sort(out.begin(), out.end());
	return out;
}
} // namespace tsc::goseq

// ---------------------------------------------------------------------------
// defer — Go defer statement scope guard.
// ---------------------------------------------------------------------------
namespace tsc::gostd::detail {
struct deferGuard {
	std::function<void()> f;
	explicit deferGuard(std::function<void()> fn) : f(std::move(fn)) {}
	deferGuard(const deferGuard&) = delete;
	~deferGuard() { f(); }
};
} // namespace tsc::gostd::detail

#define TSC_DEFER_CONCAT_(a, b) a##b
#define TSC_DEFER_CONCAT(a, b) TSC_DEFER_CONCAT_(a, b)
#define TSC_DEFER(expr)                                                    \
	::tsc::gostd::detail::deferGuard TSC_DEFER_CONCAT(_defer_, __LINE__) { \
		[&] { expr; }                                                    \
	}
#define TSC_DEFER_FN(fn)                                                   \
	::tsc::gostd::detail::deferGuard TSC_DEFER_CONCAT(_defer_, __LINE__) { \
		fn;                                                            \
	}

// ---------------------------------------------------------------------------
// chan — minimal Go channel analog (buffered; close supported).
// ---------------------------------------------------------------------------
namespace tsc::gostd {

template <typename T>
class Chan {
public:
	explicit Chan(int capacity = 0) : cap_(capacity) {}

	// send blocks while the buffer is full; returns false when closed.
	bool send(T v) {
		std::unique_lock<std::mutex> lk(mu_);
		full_.wait(lk, [&] { return closed_ || (int)q_.size() < cap_ || cap_ == 0 && waiters_ > 0; });
		if (closed_) return false;
		q_.push_back(std::move(v));
		lk.unlock();
		empty_.notify_one();
		return true;
	}
	// trySend is for sends that Go writes with a select default; not used here.
	std::pair<T, bool> recv() {
		std::unique_lock<std::mutex> lk(mu_);
		waiters_++;
		empty_.wait(lk, [&] { return !q_.empty() || closed_; });
		waiters_--;
		if (q_.empty()) {
			return {T{}, false}; // closed and drained
		}
		T v = std::move(q_.front());
		q_.pop_front();
		lk.unlock();
		full_.notify_one();
		return {std::move(v), true};
	}
	void close() {
		std::lock_guard<std::mutex> lk(mu_);
		closed_ = true;
		empty_.notify_all();
		full_.notify_all();
	}

private:
	int cap_;
	std::mutex mu_;
	std::condition_variable empty_, full_;
	std::deque<T> q_;
	bool closed_ = false;
	int waiters_ = 0;
};

// ---------------------------------------------------------------------------
// io.Pipe — synchronous in-memory pipe (io.PipeReader/io.PipeWriter).
// ---------------------------------------------------------------------------
class pipeState {
public:
	std::pair<int, Error> read(std::span<char> buf) {
		std::unique_lock<std::mutex> lk(mu_);
		cv_.wait(lk, [&] { return !data_.empty() || closed_ || readClosed_; });
		if (data_.empty()) {
			if (readClosed_) {
				return {0, newError("io: read/write on closed pipe")};
			}
			return {0, io::errEOF};
		}
		int n = (int)std::min(buf.size(), data_.size());
		std::memcpy(buf.data(), data_.data(), n);
		data_.erase(0, n);
		lk.unlock();
		cv_.notify_all();
		return {n, nullptr};
	}
	std::pair<int, Error> write(std::string_view data) {
		std::unique_lock<std::mutex> lk(mu_);
		if (closed_ || readClosed_) {
			return {0, newError("io: write on closed pipe")};
		}
		data_.append(data);
		cv_.notify_all();
		// Go's io.Pipe is synchronous: a Write blocks until all data is
		// consumed by Reads (or the pipe is closed).
		cv_.wait(lk, [&] { return data_.empty() || closed_ || readClosed_; });
		if (closed_ || readClosed_) {
			return {0, newError("io: write on closed pipe")};
		}
		return {(int)data.size(), nullptr};
	}
	Error closeWrite() {
		std::lock_guard<std::mutex> lk(mu_);
		closed_ = true;
		cv_.notify_all();
		return nullptr;
	}
	Error closeRead() {
		std::lock_guard<std::mutex> lk(mu_);
		readClosed_ = true;
		cv_.notify_all();
		return nullptr;
	}

private:
	std::mutex mu_;
	std::condition_variable cv_;
	std::string data_;
	bool closed_ = false;
	bool readClosed_ = false;
};

struct pipeReader : io::Reader, io::Closer {
	std::shared_ptr<pipeState> s;
	explicit pipeReader(std::shared_ptr<pipeState> st) : s(std::move(st)) {}
	std::pair<int, Error> read(std::span<char> buf) override {
		return s->read(buf);
	}
	Error close() override { return s->closeRead(); }
};
struct pipeWriter : io::Writer, io::Closer {
	std::shared_ptr<pipeState> s;
	explicit pipeWriter(std::shared_ptr<pipeState> st) : s(std::move(st)) {}
	std::pair<int, Error> write(std::string_view data) override {
		return s->write(data);
	}
	Error close() override { return s->closeWrite(); }
};

// io.Pipe — returns (reader, writer) sharing one synchronous pipe.
inline std::pair<std::shared_ptr<pipeReader>, std::shared_ptr<pipeWriter>>
ioPipe() {
	auto s = std::make_shared<pipeState>();
	return {std::make_shared<pipeReader>(s), std::make_shared<pipeWriter>(s)};
}

// ---------------------------------------------------------------------------
// errgroup — golang.org/x/sync/errgroup: WithContext, Go, Wait.
// ---------------------------------------------------------------------------
struct errgroup {
	Context ctx;
	CancelFunc cancel;
	std::mutex mu;
	Error firstErr;
	std::vector<std::thread> threads;

	void Go(std::function<Error()> fn) {
		threads.emplace_back([this, fn = std::move(fn)] {
			Error err = fn();
			if (err) {
				std::lock_guard<std::mutex> lk(mu);
				if (!firstErr) {
					firstErr = err;
					if (cancel) cancel();
				}
			}
		});
	}
	// Wait joins every Go'ed routine and returns the first error.
	Error Wait() {
		for (auto& th : threads) {
			if (th.joinable()) th.join();
		}
		if (cancel) cancel();
		return firstErr;
	}
};

// errgroup.WithContext — returns (group, ctx) where ctx is canceled on the
// first error or when Wait returns.
inline std::pair<std::shared_ptr<errgroup>, Context>
errgroupWithContext(const Context& parent) {
	auto [ctx, cancel] = contextWithCancel(parent);
	auto g = std::make_shared<errgroup>();
	g->ctx = ctx;
	g->cancel = std::move(cancel);
	return {g, ctx};
}

} // namespace tsc::gostd

