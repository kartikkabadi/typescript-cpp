// Port of tsc/internal/format/rulesmap.go.
#include "internal/format/format.h"

namespace tsc::format {

std::vector<ruleImpl*> getRules(FormattingContext* context, std::vector<ruleImpl*> rules) {
	const std::vector<ruleImpl*>& bucket =
		getRulesMap()[getRuleBucketIndex(context->currentTokenSpan.kind,
										 context->nextTokenSpan.kind)];
	if (!bucket.empty()) {
		ruleAction ruleActionMask = ruleActionNone;
		for (ruleImpl* r : bucket) {
			ruleAction acceptRuleActions = ~getRuleActionExclusion(ruleActionMask);
			if ((r->Action() & acceptRuleActions) != 0) {
				const auto& preds = r->Context();
				bool ok = true;
				for (const contextPredicate& p : preds) {
					if (!p(context)) {
						ok = false;
						break;
					}
				}
				if (!ok) {
					continue;
				}
				rules.push_back(r);
				ruleActionMask |= r->Action();
			}
		}
		return rules;
	}
	return rules;
}

int getRuleBucketIndex(Kind row, Kind column) {
	TSC_ASSERT(row <= KindLastKeyword && column <= KindLastKeyword,
			   "Must compute formatting context from tokens");
	return (static_cast<int>(row) * mapRowLength) + static_cast<int>(column);
}

/**
 * For a given rule action, gets a mask of other rule actions that
 * cannot be applied at the same position.
 */
ruleAction getRuleActionExclusion(ruleAction action) {
	ruleAction mask = ruleActionNone;
	if ((action & ruleActionStopProcessingSpaceActions) != 0) {
		mask |= ruleActionModifySpaceAction;
	}
	if ((action & ruleActionStopProcessingTokenActions) != 0) {
		mask |= ruleActionModifyTokenAction;
	}
	if ((action & ruleActionModifySpaceAction) != 0) {
		mask |= ruleActionModifySpaceAction;
	}
	if ((action & ruleActionModifyTokenAction) != 0) {
		mask |= ruleActionModifyTokenAction;
	}
	return mask;
}

// Go: `var getRulesMap = sync.OnceValue(buildRulesMap)` — function-local magic
// static is the equivalent one-time initialization.
const std::vector<std::vector<ruleImpl*>>& getRulesMap() {
	static const std::vector<std::vector<ruleImpl*>> m = [] {
		std::vector<ruleSpec> rules = getAllRules();
		// Map from bucket index to array of rules
		std::vector<std::vector<ruleImpl*>> m(mapRowLength * mapRowLength);
		// This array is used only during construction of the rulesbucket in the map
		std::vector<int> rulesBucketConstructionStateList(m.size());
		for (ruleSpec& rule : rules) {
			bool specificRule = rule.leftTokenRange.isSpecific && rule.rightTokenRange.isSpecific;

			for (Kind left : rule.leftTokenRange.tokens) {
				for (Kind right : rule.rightTokenRange.tokens) {
					int index = getRuleBucketIndex(left, right);
					m[index] = addRule(std::move(m[index]), rule.rule, specificRule,
									   rulesBucketConstructionStateList, index);
				}
			}
		}
		return m;
	}();
	return m;
}

// The Rules list contains all the inserted rules into a rulebucket in the following order:
//
//	1- Ignore rules with specific token combination
//	2- Ignore rules with any token combination
//	3- Context rules with specific token combination
//	4- Context rules with any token combination
//	5- Non-context rules with specific token combination
//	6- Non-context rules with any token combination
//
// The member rulesInsertionIndexBitmap is used to describe the number of rules
// in each sub-bucket (above) hence can be used to know the index of where to insert
// the next rule. It's a bitmap which contains 6 different sections each is given 5 bits.
//
// Example:
// In order to insert a rule to the end of sub-bucket (3), we get the index by adding
// the values in the bitmap segments 3rd, 2nd, and 1st.
std::vector<ruleImpl*> addRule(std::vector<ruleImpl*> rules, ruleImpl* rule, bool specificTokens,
							   std::vector<int>& constructionState, int rulesBucketIndex) {
	RulesPosition position;
	if ((rule->Action() & ruleActionStopAction) != 0) {
		if (specificTokens) {
			position = RulesPositionStopRulesSpecific;
		} else {
			position = RulesPositionStopRulesAny;
		}
	} else if (!rule->Context().empty()) {
		if (specificTokens) {
			position = RulesPositionContextRulesSpecific;
		} else {
			position = RulesPositionContextRulesAny;
		}
	} else {
		if (specificTokens) {
			position = RulesPositionNoContextRulesSpecific;
		} else {
			position = RulesPositionNoContextRulesAny;
		}
	}

	int state = constructionState[rulesBucketIndex];

	rules.insert(rules.begin() + getRuleInsertionIndex(state, position), rule);
	constructionState[rulesBucketIndex] = increaseInsertionIndex(state, position);
	return rules;
}

int getRuleInsertionIndex(int indexBitmap, RulesPosition maskPosition) {
	int index = 0;
	for (int pos = 0; pos <= maskPosition; pos += maskBitSize) {
		index += indexBitmap & rulesmap_mask;
		indexBitmap >>= maskBitSize;
	}
	return index;
}

int increaseInsertionIndex(int indexBitmap, RulesPosition maskPosition) {
	int value = ((indexBitmap >> maskPosition) & rulesmap_mask) + 1;
	TSC_ASSERT((value & rulesmap_mask) == value,
			   "Adding more rules into the sub-bucket than allowed. Maximum allowed is 32 rules.");
	return (indexBitmap & ~(rulesmap_mask << maskPosition)) | (value << maskPosition);
}

} // namespace tsc::format
