#pragma once

// programcounter.go — programCounter: refcounts compiler.Program pointers.

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "internal/ast/ast.h" // tscUnreachable

namespace tsc::compiler {
class SimpleProgram;
}

namespace tsc::project {

// programCounter — programcounter.go.
struct programCounter {
	std::mutex mu;
	std::unordered_map<compiler::SimpleProgram*, int32_t> refs;

	// Ref increments the reference count for a program. If the program is
	// not yet tracked, it is added with a reference count of 1.
	void Ref(compiler::SimpleProgram* program) {
		std::lock_guard<std::mutex> lk(mu);
		refs[program]++;
	}

	bool Deref(compiler::SimpleProgram* program) {
		std::lock_guard<std::mutex> lk(mu);
		auto it = refs.find(program);
		if (it == refs.end()) {
			return false;
		}
		int32_t count = it->second - 1;
		if (count < 0) {
			TSC_UNREACHABLE("program reference count went below zero");
		}
		if (count == 0) {
			refs.erase(it);
			return true;
		}
		it->second = count;
		return false;
	}

	int Len() {
		std::lock_guard<std::mutex> lk(mu);
		return static_cast<int>(refs.size());
	}
};

} // namespace tsc::project
