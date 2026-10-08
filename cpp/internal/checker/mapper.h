// Port of tsc/internal/checker/mapper.go
#pragma once
#include <functional>
#include <vector>

#include "internal/checker/types.h"

namespace tsc {
namespace checker {

struct InferenceContext;

enum class TypeMapperKind : int32_t {
	Unknown,
	Simple,
	Array,
	Merged,
};

// TypeMapper — Go models this as a struct wrapping a TypeMapperData interface;
// we use a plain abstract base class.
struct TypeMapper {
	virtual ~TypeMapper() = default;
	virtual Type* map(Type* t) { return t; }
	virtual TypeMapperKind kind() const { return TypeMapperKind::Unknown; }
	virtual bool mapsThisOnly() const { return false; }
};

Type* getMappedType(Type* t, TypeMapper* mapper);
TypeMapper* newTypeMapper(std::vector<Type*> sources, std::vector<Type*> targets);
TypeMapper* newSimpleTypeMapper(Type* source, Type* target);
TypeMapper* newArrayTypeMapper(std::vector<Type*> sources, std::vector<Type*> targets);
TypeMapper* newArrayToSingleTypeMapper(std::vector<Type*> sources, Type* target);
TypeMapper* newDeferredTypeMapper(std::vector<Type*> sources, std::vector<std::function<Type*()>> targets);
TypeMapper* newFunctionTypeMapper(std::function<Type*(Type*)> fn);
TypeMapper* mergeTypeMappers(TypeMapper* m1, TypeMapper* m2);
TypeMapper* prependTypeMapping(Type* source, Type* target, TypeMapper* mapper);
TypeMapper* appendTypeMapping(TypeMapper* mapper, Type* source, Type* target);

struct SimpleTypeMapper : TypeMapper {
	Type* source;
	Type* target;
	Type* map(Type* t) override;
	TypeMapperKind kind() const override { return TypeMapperKind::Simple; }
	bool mapsThisOnly() const override;
};

struct ArrayTypeMapper : TypeMapper {
	std::vector<Type*> sources;
	std::vector<Type*> targets;
	Type* map(Type* t) override;
	TypeMapperKind kind() const override { return TypeMapperKind::Array; }
	bool mapsThisOnly() const override;
};

struct ArrayToSingleTypeMapper : TypeMapper {
	std::vector<Type*> sources;
	Type* target;
	Type* map(Type* t) override;
	bool mapsThisOnly() const override;
};

struct DeferredTypeMapper : TypeMapper {
	std::vector<Type*> sources;
	std::vector<std::function<Type*()>> targets;
	Type* map(Type* t) override;
	bool mapsThisOnly() const override;
};

struct FunctionTypeMapper : TypeMapper {
	std::function<Type*(Type*)> fn;
	Type* map(Type* t) override { return fn(t); }
};

struct MergedTypeMapper : TypeMapper {
	TypeMapper* m1;
	TypeMapper* m2;
	Type* map(Type* t) override { return m2->map(m1->map(t)); }
	TypeMapperKind kind() const override { return TypeMapperKind::Merged; }
};

struct CompositeTypeMapper : TypeMapper {
	Checker* c;
	TypeMapper* m1;
	TypeMapper* m2;
	Type* map(Type* t) override;
};

struct InferenceTypeMapper : TypeMapper {
	Checker* c;
	InferenceContext* n;
	bool fixing;
	Type* map(Type* t) override;
};

}  // namespace checker
}  // namespace tsc
