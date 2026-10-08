// validatepackagename.cpp — port of
// tsc/internal/project/ata/validatepackagename.go.
#include "internal/project/ata/ata.h"

#include "internal/gostd/gostd.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ata {
namespace {

// url.QueryEscape — net/url encodeQueryComponent: unreserved
// [A-Za-z0-9-_.~] kept, space -> '+', all else -> %XX (uppercase).
std::string queryEscape(std::string_view s) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		unsigned char uc = (unsigned char)c;
		if ((uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z') ||
		    (uc >= '0' && uc <= '9') || uc == '-' || uc == '_' ||
		    uc == '.' || uc == '~') {
			out += c;
		} else if (uc == ' ') {
			out += '+';
		} else {
			out += '%';
			out += hex[uc >> 4];
			out += hex[uc & 0xF];
		}
	}
	return out;
}

// strings.Cut — split on first sep; returns (before, after, found).
struct CutResult {
	std::string before;
	std::string after;
	bool found;
};
CutResult cut(std::string_view s, std::string_view sep) {
	size_t i = s.find(sep);
	if (i == std::string_view::npos) {
		return {std::string(s), "", false};
	}
	return {std::string(s.substr(0, i)),
	        std::string(s.substr(i + sep.size())), true};
}

ValidatePackageNameResult validatePackageNameWorker(
    const std::string& packageName, bool supportScopedPackage) {
	size_t packageNameLen = packageName.size();
	if (packageNameLen == 0) {
		return {EmptyName, "", false};
	}
	if (packageNameLen > (size_t)maxPackageNameLength) {
		return {NameTooLong, "", false};
	}
	int w = 0;
	char32_t firstChar =
	    decodeUtf8RuneStrict(packageName, &w);
	if (firstChar == '.') {
		return {NameStartsWithDot, "", false};
	}
	if (firstChar == '_') {
		return {NameStartsWithUnderscore, "", false};
	}
	// check if name is scope package like: starts with @ and has one '/'
	// in the middle
	// scoped packages are not currently supported
	if (supportScopedPackage) {
		if (packageName.size() > 0 && packageName[0] == '@') {
			std::string_view withoutScope(packageName.data() + 1,
			                              packageName.size() - 1);
			auto scopeCut = cut(withoutScope, "/");
			if (scopeCut.found && !scopeCut.before.empty() &&
			    !scopeCut.after.empty() &&
			    scopeCut.after.find('/') == std::string::npos) {
				auto scopeResult = validatePackageNameWorker(
				    scopeCut.before, /*supportScopedPackage*/ false);
				if (scopeResult.result != NameOk) {
					return {scopeResult.result, scopeCut.before, true};
				}
				auto packageResult = validatePackageNameWorker(
				    scopeCut.after, /*supportScopedPackage*/ false);
				if (packageResult.result != NameOk) {
					return {packageResult.result, scopeCut.after,
					        false};
				}
				return {NameOk, "", false};
			}
		}
	}
	if (queryEscape(packageName) != packageName) {
		return {NameContainsNonURISafeCharacters, "", false};
	}
	return {NameOk, "", false};
}

} // namespace

// ValidatePackageName — package name rules per
// https://docs.npmjs.com/files/package.json. @internal
ValidatePackageNameResult ValidatePackageName(
    const std::string& packageName) {
	return validatePackageNameWorker(packageName,
	                                 /*supportScopedPackage*/ true);
}

std::string renderPackageNameValidationFailure(
    const std::string& typing, NameValidationResult result,
    const std::string& nameIn, bool isScopeName) {
	std::string_view kind = isScopeName ? "Scope" : "Package";
	const std::string& name = nameIn.empty() ? typing : nameIn;
	switch (result) {
	case EmptyName:
		return gostd::sprintf("'%s':: %s name '%s' cannot be empty",
		                      {typing.c_str(), kind.data(), name.c_str()});
	case NameTooLong:
		return gostd::sprintf(
		    "'%s':: %s name '%s' should be less than %d characters",
		    {typing.c_str(), kind.data(), name.c_str(),
		     maxPackageNameLength});
	case NameStartsWithDot:
		return gostd::sprintf(
		    "'%s':: %s name '%s' cannot start with '.'",
		    {typing.c_str(), kind.data(), name.c_str()});
	case NameStartsWithUnderscore:
		return gostd::sprintf(
		    "'%s':: %s name '%s' cannot start with '_'",
		    {typing.c_str(), kind.data(), name.c_str()});
	case NameContainsNonURISafeCharacters:
		return gostd::sprintf(
		    "'%s':: %s name '%s' contains non URI safe characters",
		    {typing.c_str(), kind.data(), name.c_str()});
	case NameOk:
		TSC_UNREACHABLE("Unexpected Ok result");
	default:
		TSC_UNREACHABLE("Unknown package name validation result");
	}
}

} // namespace tsc::ata
