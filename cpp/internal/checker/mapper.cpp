// Port of tsc/internal/checker/mapper.go
#include "internal/checker/mapper.h"

#include <algorithm>

#include "internal/checker/checker.h"

namespace tsc {
namespace checker {

Type* getMappedType(Type* t, TypeMapper* mapper) {
	return mapper->map(getNonDistributedTypeParameter(t));
}

TypeMapper* newTypeMapper(std::vector<Type*> sources, std::vector<Type*> targets) {
	if (sources.size() == 1) {
		return newSimpleTypeMapper(sources[0], targets[0]);
	}
	return newArrayTypeMapper(std::move(sources), std::move(targets));
}

TypeMapper* Checker::combineTypeMappers(TypeMapper* m1, TypeMapper* m2) {
	if (m1 != nullptr) {
		auto* m = new CompositeTypeMapper();
		m->c = this;
		m->m1 = m1;
		m->m2 = m2;
		return m;
	}
	return m2;
}

Type* Checker::mapTypeWithCompositeMapper(Type* t, TypeMapper* m1, TypeMapper* m2) {
	if (m1 == nullptr) {
		return getMappedType(t, m2);
	}
	Type* t1 = getMappedType(t, m1);
	if (t1 != t) {
		return instantiateType(t1, m2);
	}
	return getMappedType(t, m2);
}

TypeMapper* mergeTypeMappers(TypeMapper* m1, TypeMapper* m2) {
	if (m1 != nullptr) {
		auto* m = new MergedTypeMapper();
		m->m1 = m1;
		m->m2 = m2;
		return m;
	}
	return m2;
}

TypeMapper* prependTypeMapping(Type* source, Type* target, TypeMapper* mapper) {
	if (mapper == nullptr) {
		return newSimpleTypeMapper(getNonDistributedTypeParameter(source), target);
	}
	return mergeTypeMappers(newSimpleTypeMapper(getNonDistributedTypeParameter(source), target), mapper);
}

TypeMapper* appendTypeMapping(TypeMapper* mapper, Type* source, Type* target) {
	if (mapper == nullptr) {
		return newSimpleTypeMapper(getNonDistributedTypeParameter(source), target);
	}
	return mergeTypeMappers(mapper, newSimpleTypeMapper(getNonDistributedTypeParameter(source), target));
}

// Maps forward-references to later types parameters to the empty object type.
// This is used during inference when instantiating type parameter defaults.
TypeMapper* Checker::newBackreferenceMapper(InferenceContext* context, int index) {
	std::vector<Type*> typeParameters;
	for (size_t i = index; i < context->inferences.size(); i++) {
		typeParameters.push_back(context->inferences[i]->typeParameter);
	}
	return newArrayToSingleTypeMapper(std::move(typeParameters), unknownType);
}

TypeMapper* newSimpleTypeMapper(Type* source, Type* target) {
	auto* m = new SimpleTypeMapper();
	m->source = source;
	m->target = target;
	return m;
}

Type* SimpleTypeMapper::map(Type* t) {
	if (t == source) {
		return target;
	}
	return t;
}

bool SimpleTypeMapper::mapsThisOnly() const {
	return isThisTypeParameter(source);
}

TypeMapper* newArrayTypeMapper(std::vector<Type*> sources, std::vector<Type*> targets) {
	auto* m = new ArrayTypeMapper();
	m->sources = std::move(sources);
	m->targets = std::move(targets);
	return m;
}

Type* ArrayTypeMapper::map(Type* t) {
	for (size_t i = 0; i < sources.size(); i++) {
		if (t == sources[i]) {
			return targets[i];
		}
	}
	return t;
}

bool ArrayTypeMapper::mapsThisOnly() const {
	return sources.size() == 1 && isThisTypeParameter(sources[0]);
}

TypeMapper* newArrayToSingleTypeMapper(std::vector<Type*> sources, Type* target) {
	auto* m = new ArrayToSingleTypeMapper();
	m->sources = std::move(sources);
	m->target = target;
	return m;
}

Type* ArrayToSingleTypeMapper::map(Type* t) {
	if (std::find(sources.begin(), sources.end(), t) != sources.end()) {
		return target;
	}
	return t;
}

bool ArrayToSingleTypeMapper::mapsThisOnly() const {
	return sources.size() == 1 && isThisTypeParameter(sources[0]);
}

TypeMapper* newDeferredTypeMapper(std::vector<Type*> sources, std::vector<std::function<Type*()>> targets) {
	auto* m = new DeferredTypeMapper();
	m->sources = std::move(sources);
	m->targets = std::move(targets);
	return m;
}

Type* DeferredTypeMapper::map(Type* t) {
	for (size_t i = 0; i < sources.size(); i++) {
		if (t == sources[i]) {
			return targets[i]();
		}
	}
	return t;
}

bool DeferredTypeMapper::mapsThisOnly() const {
	return sources.size() == 1 && isThisTypeParameter(sources[0]);
}

TypeMapper* newFunctionTypeMapper(std::function<Type*(Type*)> fn) {
	auto* m = new FunctionTypeMapper();
	m->fn = std::move(fn);
	return m;
}

Type* CompositeTypeMapper::map(Type* t) {
	Type* t1 = m1->map(t);
	if (t1 != t) {
		return c->instantiateType(t1, m2);
	}
	return m2->map(t);
}

TypeMapper* Checker::newInferenceTypeMapper(InferenceContext* n, bool fixing) {
	auto* m = new InferenceTypeMapper();
	m->c = this;
	m->n = n;
	m->fixing = fixing;
	return m;
}

Type* InferenceTypeMapper::map(Type* t) {
	for (size_t i = 0; i < n->inferences.size(); i++) {
		InferenceInfo* inference = n->inferences[i];
		if (t == inference->typeParameter) {
			if (fixing && !inference->isFixed) {
				// Before we commit to a particular inference (and thus lock out any further
				// inferences), we infer from any intra-expression inference sites we have collected.
				c->inferFromIntraExpressionSites(n);
				clearCachedInferences(n->inferences);
				inference->isFixed = true;
			}
			return c->getInferredType(n, i);
		}
	}
	return t;
}

}  // namespace checker
}  // namespace tsc
