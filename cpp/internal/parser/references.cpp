// references.cpp — port of tsc/internal/parser/references.go.
// Collects external module references / ambient module names for a parsed
// SourceFile.

#include "internal/parser/parser.h"
#include "internal/core/nodemodules.h"
#include "internal/tspath/tspath.h"

namespace tsc {

static void collectModuleReferences(SourceFile* file, Node* node,
                                    bool inAmbientModule);

void collectExternalModuleReferences(SourceFile* file) {
	for (Node* node : file->Statements->nodes) {
		collectModuleReferences(file, node, false /*inAmbientModule*/);
	}

	if (file->flags & NodeFlagsPossiblyContainsDynamicImport ||
	    isInJSFile(file)) {
		forEachDynamicImportOrRequireCall(
		    file, /*includeTypeSpaceImports*/ true,
		    /*requireStringLiteralLikeArgument*/ true,
		    [file](Node* node, Node* moduleSpecifier) -> bool {
			    file->imports.push_back(moduleSpecifier);
			    return false;
		    });
	}
}

static void collectModuleReferences(SourceFile* file, Node* node,
                                    bool inAmbientModule) {
	if (isAnyImportOrReExport(node)) {
		Node* moduleNameExpr = getExternalModuleName(node);
		// TypeScript 1.0 spec (April 2014): 12.1.6
		// An ExternalImportDeclaration in an AmbientExternalModuleDeclaration
		// may reference other external modules only through top-level
		// external module names. Relative external module names are not
		// permitted.
		if (moduleNameExpr != nullptr && isStringLiteral(moduleNameExpr)) {
			std::string_view moduleName = moduleNameExpr->text();
			if (!moduleName.empty() &&
			    (!inAmbientModule ||
			     !tspath::isExternalModuleNameRelative(moduleName))) {
				file->imports.push_back(moduleNameExpr);
				if (file->UsesUriStyleNodeCoreModules != Tristate::True &&
				    !file->IsDeclarationFile) {
					if (moduleName.starts_with("node:") &&
					    !ExclusivelyPrefixedNodeCoreModules.count(
					        std::string(moduleName))) {
						// Presence of `node:` prefix takes precedence over
						// unprefixed node core modules
						file->UsesUriStyleNodeCoreModules = Tristate::True;
					} else if (file->UsesUriStyleNodeCoreModules ==
					               Tristate::Unknown &&
					           UnprefixedNodeCoreModules.count(
					               std::string(moduleName))) {
						// Avoid `unprefixedNodeCoreModules.has` for every
						// import
						file->UsesUriStyleNodeCoreModules = Tristate::False;
					}
				}
			}
		}
		return;
	}
	if (isModuleDeclaration(node) && isAmbientModule(node) &&
	    (inAmbientModule ||
	     hasSyntacticModifier(node, ModifierFlagsAmbient) ||
	     file->IsDeclarationFile)) {
		std::string_view nameText =
		    node->as<ModuleDeclaration>()->name->text();
		// Ambient module declarations can be interpreted as augmentations for
		// some existing external modules. This will happen in two cases:
		// - if current file is external module then module augmentation is a
		//   ambient module declaration defined in the top level scope
		// - if current file is not external module then module augmentation
		//   is an ambient module declaration with non-relative module name
		//   immediately nested in top level ambient module declaration.
		if (isExternalModule(file) ||
		    (inAmbientModule &&
		     !tspath::isExternalModuleNameRelative(nameText))) {
			file->ModuleAugmentations.push_back(
			    node->as<ModuleDeclaration>()->name);
		} else if (!inAmbientModule) {
			file->AmbientModuleNames.emplace_back(nameText);
			// An AmbientExternalModuleDeclaration declares an external
			// module. This type of declaration is permitted only in the
			// global module. The StringLiteral must specify a top-level
			// external module name. Relative external module names are not
			// permitted. NOTE: body of ambient module is always a module
			// block, if it exists.
			if (node->body() != nullptr) {
				for (Node* statement : node->body()->statements()) {
					collectModuleReferences(file, statement,
					                        true /*inAmbientModule*/);
				}
			}
		}
	}
}

}  // namespace tsc
