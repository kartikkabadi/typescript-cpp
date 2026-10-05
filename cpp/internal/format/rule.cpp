// Port of tsc/internal/format/rule.go.
#include "internal/format/format.h"

#include <deque>

namespace tsc::format {

/**
 * A rule takes a two tokens (left/right) and a particular context
 * for which you're meant to look at them. You then declare what should the
 * whitespace annotation be between these tokens via the action param.
 *
 * @param debugName Name to print
 * @param left The left side of the comparison
 * @param right The right side of the comparison
 * @param context A set of filters to narrow down the space in which this formatter rule applies
 * @param action a declaration of the expected whitespace
 * @param flags whether the rule deletes a line or not, defaults to no-op
 */
ruleSpec rule(std::string debugName, ruleTokenArg left, ruleTokenArg right,
			  std::vector<contextPredicate> context, ruleAction action, ruleFlags flags) {
	tokenRange leftRange = toTokenRange(left);
	tokenRange rightRange = toTokenRange(right);
	// Go heap-allocates `*ruleImpl` per rule; the rules map is global and lives
	// forever, so a static pool is the faithful equivalent.
	static std::deque<ruleImpl> pool;
	ruleImpl* impl = &pool.emplace_back();
	impl->debugName = std::move(debugName);
	impl->context = std::move(context);
	impl->action = action;
	impl->flags = flags;
	return ruleSpec{
		leftRange,
		rightRange,
		impl,
	};
}

tokenRange toTokenRange(ruleTokenArg e) {
	return e.range;
}

tokenRange tokenRangeFrom(std::initializer_list<Kind> tokens) {
	return tokenRange{std::vector<Kind>(tokens), true};
}

tokenRange tokenRangeFromEx(const std::vector<Kind>& prefix, std::initializer_list<Kind> tokens) {
	std::vector<Kind> all = prefix;
	for (Kind t : tokens) {
		all.push_back(t);
	}
	return tokenRange{std::move(all), true};
}

tokenRange tokenRangeFromRange(Kind start, Kind end) {
	std::vector<Kind> tokens;
	tokens.reserve(static_cast<int>(end) - static_cast<int>(start) + 1);
	for (int t = static_cast<int>(start); t <= static_cast<int>(end); t++) {
		tokens.push_back(static_cast<Kind>(t));
	}
	return tokenRange{std::move(tokens), true};
}

} // namespace tsc::format
