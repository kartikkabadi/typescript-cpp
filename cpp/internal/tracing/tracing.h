// Port of tsc/internal/tracing — interface surface consumed by the checker.
// The tracing session itself (event/file writing, type dumping) is ported
// separately; only the types and API the checker calls are declared here.
#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"

namespace tsc {
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
	virtual std::function<void()> Push(Phase phase, const std::string& name,
									   const TraceArgs& args,
									   bool separateBeginAndEnd) = 0;
	virtual void Instant(Phase phase, const std::string& name, const TraceArgs& args) = 0;
	virtual Tracer* NewTypeTracer(int checkerIndex) = 0;
};

}  // namespace tracing
}  // namespace tsc
