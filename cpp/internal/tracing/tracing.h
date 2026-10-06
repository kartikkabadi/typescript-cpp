// Port of tsc/internal/tracing — interface surface consumed by the checker.
// The session implementation (event/file writing, type dumping) lives in
// tracing.cpp; only the types and API the checker calls are declared here.
#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/gostd/gostd.h"

namespace tsc {

namespace vfs { struct FS; }

namespace tracing {

// Phase — Go `type Phase string`.
using Phase = std::string;
inline const Phase PhaseParse = "parse";
inline const Phase PhaseProgram = "program";
inline const Phase PhaseBind = "bind";
inline const Phase PhaseCheck = "check";
inline const Phase PhaseCheckTypes = "checkTypes";
inline const Phase PhaseEmit = "emit";
inline const Phase PhaseSession = "session";

// Trace event args — Go `map[string]any`.
using TraceArgs = std::unordered_map<std::string, std::any>;

// TracedType — port of tracing.TracedType. Lets the tracing package work with
// checker types without a circular dependency.
class TracedType {
public:
	virtual ~TracedType() = default;
	virtual uint32_t Id() = 0;
	virtual std::vector<std::string> FormatFlags() = 0;
	virtual bool IsConditional() = 0;
	virtual tsc::Symbol* Symbol() = 0;
	virtual tsc::Symbol* AliasSymbol() = 0;
	virtual std::vector<TracedType*> AliasTypeArguments() = 0;

	// Type-specific data accessors
	virtual std::string IntrinsicName() = 0;
	virtual std::vector<TracedType*> UnionTypes() = 0;
	virtual std::vector<TracedType*> IntersectionTypes() = 0;
	virtual TracedType* IndexType() = 0;
	virtual TracedType* IndexedAccessObjectType() = 0;
	virtual TracedType* IndexedAccessIndexType() = 0;
	virtual TracedType* ConditionalCheckType() = 0;
	virtual TracedType* ConditionalExtendsType() = 0;
	virtual TracedType* ConditionalTrueType() = 0;
	virtual TracedType* ConditionalFalseType() = 0;
	virtual TracedType* SubstitutionBaseType() = 0;
	virtual TracedType* SubstitutionConstraintType() = 0;
	virtual TracedType* ReferenceTarget() = 0;
	virtual std::vector<TracedType*> ReferenceTypeArguments() = 0;
	virtual tsc::Node* ReferenceNode() = 0;
	virtual TracedType* ReverseMappedSourceType() = 0;
	virtual TracedType* ReverseMappedMappedType() = 0;
	virtual TracedType* ReverseMappedConstraintType() = 0;
	virtual TracedType* EvolvingArrayElementType() = 0;
	virtual TracedType* EvolvingArrayFinalType() = 0;
	virtual bool IsTuple() = 0;
	virtual tsc::Node* Pattern() = 0;
	// Go `RecursionIdentity() any` — the identity is always a *Node, *Symbol or
	// *Type, so a pointer carries it faithfully.
	virtual void* RecursionIdentity() = 0;

	// Display is an optional string representation of the type
	virtual std::string Display() = 0;
};

// Tracer — port of tracing.Tracer: the per-checker type recorder interface.
// Each checker should have its own Tracer instance to avoid sharing types
// between checkers.
class Tracer {
public:
	virtual ~Tracer() = default;
	// RecordType records a type for later dumping.
	virtual void RecordType(TracedType* t) = 0;
	// DumpTypes writes all recorded types to disk (Go returns error).
	virtual void DumpTypes() = 0;
};

// Tracing — the tracing session. The file-writing implementation is ported
// with the tracing package slice; the API surface the checker uses is
// declared here so checker::Tracer compiles and stays faithful.
class Tracing {
public:
	virtual ~Tracing() = default;
	// Push takes the args as a shared_ptr: in Go the map is captured by the
	// pop closure, so for separateBeginAndEnd events mutations the caller
	// makes after Push (e.g. relater's `variances` arg) appear in the "E"
	// event. Callers that don't mutate can use the by-value overload below.
	virtual std::function<void()> Push(Phase phase, const std::string& name,
									   std::shared_ptr<TraceArgs> args,
									   bool separateBeginAndEnd) = 0;
	std::function<void()> Push(Phase phase, const std::string& name,
							   const TraceArgs& args, bool separateBeginAndEnd) {
		return Push(std::move(phase), name,
					std::make_shared<TraceArgs>(args), separateBeginAndEnd);
	}
	virtual void Instant(Phase phase, const std::string& name, const TraceArgs& args) = 0;
	virtual Tracer* NewTypeTracer(int checkerIndex) = 0;
};

// TraceScope — RAII helper emulating Go's `defer tr.Push(phase, name, args,
// sep)()` at call sites. Null-tracing and empty closures are safe.
class TraceScope {
public:
	// `Pusher` is any type with `Push(Phase, name, TraceArgs, bool)` —
	// tracing::Tracing or checker::Tracer (whose Push adds checkerId).
	template <class Pusher>
	TraceScope(Pusher* tr, Phase phase, const std::string& name,
	           const TraceArgs& args, bool separateBeginAndEnd)
	    : pop_(tr != nullptr
	               ? tr->Push(phase, name, args, separateBeginAndEnd)
	               : std::function<void()>()) {}
	// For callers whose Push already returns the pop closure (e.g.
	// checker::Tracer::Push).
	explicit TraceScope(std::function<void()> pop)
	    : pop_(std::move(pop)) {}
	~TraceScope() {
		if (pop_) {
			pop_();
		}
	}
	TraceScope(const TraceScope&) = delete;
	TraceScope& operator=(const TraceScope&) = delete;

private:
	std::function<void()> pop_;
};

// StartTracing — tracing.go:152. Creates a tracing session writing
// trace.json/legend.json/types_N.json under traceDir; deterministic mode
// substitutes a monotonic counter for wall-clock timestamps (test baselines).
std::pair<Tracing*, gostd::Error> StartTracing(
    vfs::FS* fs, const std::string& traceDir,
    const std::string& configFilePath, bool deterministic);

// StopTracing — tracing.go:437.
gostd::Error StopTracing(Tracing* tr);

}  // namespace tracing
}  // namespace tsc
