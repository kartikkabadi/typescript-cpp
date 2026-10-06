// codeactions_fixclassincorrectlyimplementsinterface.go — quickfix for
// "class incorrectly implements interface": inserts missing members
// (index signatures + property/method declarations) into the class.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/locale/locale.h"
#include "internal/scanner/scanner.h"

namespace tsc::ls {

namespace {

const std::string fixClassIncorrectlyImplementsInterfaceFixID =
    "fixClassIncorrectlyImplementsInterface";

const std::vector<int32_t> fixClassIncorrectlyImplementsInterfaceErrorCodes{
    Class_0_incorrectly_implements_interface_1->code,
    Class_0_incorrectly_implements_class_1_Did_you_mean_to_extend_1_and_inherit_its_members_as_a_subclass
        ->code,
};

// getClass — codeactions_fixclassincorrectlyimplementsinterface.go:156.
Node* getClass(SourceFile* sourceFile, TextRange span) {
	auto* token = astnav::getTokenAtPosition(sourceFile, span.pos());
	if (token == nullptr) {
		return nullptr;
	}
	return getContainingClass(token);
}

// getConstructor — codeactions_fixclassincorrectlyimplementsinterface.go:164.
Node* getConstructor(Node* classDeclaration) {
	if (classDeclaration == nullptr ||
	    classDeclaration->memberList() == nullptr) {
		return nullptr;
	}
	for (auto* member : classDeclaration->memberList()->nodes) {
		if (member != nullptr && isConstructorDeclaration(member)) {
			return member;
		}
	}
	return nullptr;
}

// getInheritedMembers — codeactions_fixclassincorrectlyimplementsinterface.go:207.
SymbolTable getInheritedMembers(checker::Checker* typeChecker,
                                Node* classDeclaration) {
	auto* typeNode = getClassExtendsHeritageElement(classDeclaration);
	if (typeNode == nullptr) {
		return SymbolTable{};
	}

	auto* baseType = typeChecker->GetTypeAtLocation(typeNode->asNode());
	if (baseType == nullptr) {
		return SymbolTable{};
	}

	SymbolTable inheritedMembers;
	for (auto* symbol : typeChecker->GetPropertiesOfType(baseType)) {
		if (symbol == nullptr) {
			continue;
		}
		auto flags = checker::GetDeclarationModifierFlagsFromSymbol(symbol);
		if ((flags & ModifierFlagsPrivate) == 0) {
			inheritedMembers[symbol->name] = symbol;
		}
	}
	return inheritedMembers;
}

// getMissingMembers — codeactions_fixclassincorrectlyimplementsinterface.go:176.
std::vector<Symbol*> getMissingMembers(
    checker::Checker* typeChecker, Node* classDeclaration,
    const std::vector<checker::Type*>& implementedTypes) {
	auto inheritedMembers =
	    getInheritedMembers(typeChecker, classDeclaration);
	SymbolTable seenMembers;

	SymbolTable classMembers;
	if (classDeclaration->symbol() != nullptr) {
		classMembers = classDeclaration->symbol()->members;
	}

	std::vector<Symbol*> missingMembers;
	for (auto* implementedType : implementedTypes) {
		for (auto* symbol :
		     typeChecker->GetPropertiesOfType(implementedType)) {
			if (symbol == nullptr) {
				continue;
			}
			if (classMembers[symbol->name] != nullptr) {
				continue;
			}
			if (inheritedMembers[symbol->name] != nullptr ||
			    seenMembers[symbol->name] != nullptr) {
				continue;
			}
			auto flags =
			    checker::GetDeclarationModifierFlagsFromSymbol(symbol);
			if ((flags & ModifierFlagsPrivate) == 0) {
				seenMembers[symbol->name] = symbol;
				missingMembers.push_back(symbol);
			}
		}
	}
	return missingMembers;
}

// createImportAdder — codeactions_fixclassincorrectlyimplementsinterface.go:231.
std::pair<std::unique_ptr<autoimport::ImportAdder>, gostd::Error>
createImportAdder(const gostd::Context& ctx, CodeFixContext* fixContext,
                  checker::Checker* typeChecker) {
	auto [view, err] = fixContext->LS->getPreparedAutoImportView(
	    fixContext->SourceFile, typeChecker);
	if (err != nullptr) {
		return {nullptr, err};
	}
	if (view == nullptr) {
		return {nullptr, nullptr};
	}
	return {autoimport::NewImportAdder(
	            ctx, fixContext->Program, typeChecker,
	            fixContext->SourceFile, view,
	            fixContext->LS->FormatOptions(),
	            fixContext->LS->converters,
	            fixContext->LS->UserPreferences()),
	        nullptr};
}

// getChanges — codeactions_fixclassincorrectlyimplementsinterface.go:136.
std::vector<lsproto::TextEdit*> getChanges(
    change::Tracker* changeTracker, autoimport::ImportAdder* importAdder,
    SourceFile* sourceFile) {
	auto [changes, unmappable] = changeTracker->GetChanges();
	if (unmappable.size() != 0) {
		return {};
	}
	auto fileChanges = changes[sourceFile->OriginalFileName()];
	if (importAdder != nullptr && importAdder->HasFixes()) {
		auto edits = importAdder->Edits();
		fileChanges.insert(fileChanges.end(), edits.begin(),
		                   edits.end());
	}
	return fileChanges;
}

// insertInterfaceMemberNode — codeactions_fixclassincorrectlyimplementsinterface.go:148.
void insertInterfaceMemberNode(change::Tracker* changeTracker,
                               SourceFile* sourceFile,
                               Node* classDeclaration, Node* constructor,
                               Node* member) {
	if (constructor == nullptr) {
		changeTracker->InsertMemberAtStart(sourceFile, classDeclaration,
		                                   member);
	} else {
		changeTracker->InsertNodeAfter(sourceFile, constructor, member);
	}
}

// addChanges — codeactions_fixclassincorrectlyimplementsinterface.go:107.
void addChanges(const gostd::Context& ctx, CodeFixContext* fixContext,
                change::Tracker* changeTracker,
                autoimport::ImportAdder* importAdder,
                checker::Checker* typeChecker, Node* classDeclaration,
                Node* implementedTypeNode) {
	auto* missingMemberFixer = newMissingMemberFixer(
	    changeTracker, fixContext->Program, typeChecker,
	    fixContext->LS->UserPreferences(), importAdder,
	    locale::fromContext(ctx));
	auto* constructor = getConstructor(classDeclaration);
	auto* implementedType =
	    typeChecker->GetTypeAtLocation(implementedTypeNode);
	auto* classType = typeChecker->GetTypeAtLocation(classDeclaration);

	if (typeChecker->GetNumberIndexType(classType) == nullptr) {
		auto* member =
		    missingMemberFixer->createIndexSignatureDeclarationFromType(
		        classDeclaration, implementedType,
		        typeChecker->GetNumberType());
		if (member != nullptr) {
			insertInterfaceMemberNode(changeTracker,
			                          fixContext->SourceFile,
			                          classDeclaration, constructor,
			                          member);
		}
	}

	if (typeChecker->GetStringIndexType(classType) == nullptr) {
		auto* member =
		    missingMemberFixer->createIndexSignatureDeclarationFromType(
		        classDeclaration, implementedType,
		        typeChecker->GetStringType());
		if (member != nullptr) {
			insertInterfaceMemberNode(changeTracker,
			                          fixContext->SourceFile,
			                          classDeclaration, constructor,
			                          member);
		}
	}

	auto missingMembers = getMissingMembers(
	    typeChecker, classDeclaration, {implementedType});
	for (auto* member : missingMembers) {
		auto memberNodes = missingMemberFixer->createMemberFromSymbol(
		    member, classDeclaration, fixContext->SourceFile,
		    nullptr /*body*/, preserveOptionalFlagsAll,
		    false /*abstract*/);
		for (auto* memberNode : memberNodes) {
			insertInterfaceMemberNode(changeTracker,
			                          fixContext->SourceFile,
			                          classDeclaration, constructor,
			                          memberNode);
		}
	}
}

// getCodeActionsToFixClassIncorrectlyImplementsInterface —
// codeactions_fixclassincorrectlyimplementsinterface.go:33.
std::pair<std::vector<CodeAction*>, gostd::Error>
getCodeActionsToFixClassIncorrectlyImplementsInterface(
    const gostd::Context& ctx, CodeFixContext* fixContext) {
	auto* classDeclaration =
	    getClass(fixContext->SourceFile, fixContext->Span);
	if (classDeclaration == nullptr) {
		return {{}, nullptr};
	}

	auto implementsTypes =
	    getHeritageElements(classDeclaration, Kind::ImplementsKeyword);
	auto loc = locale::fromContext(ctx);

	auto [typeChecker, done] =
	    fixContext->Program->GetTypeCheckerForFileExclusive(
	        fixContext->SourceFile);
	struct doneGuard {
		std::function<void()> f;
		~doneGuard() { if (f) f(); }
	} _done{done};

	std::vector<CodeAction*> actions;
	for (auto* implementedTypeNode : implementsTypes) {
		auto* changeTracker = change::NewTracker(
		    ctx, fixContext->Program->Options(),
		    fixContext->LS->FormatOptions(), fixContext->LS->converters);
		auto [importAdder, err] =
		    createImportAdder(ctx, fixContext, typeChecker);
		if (err != nullptr) {
			return {{}, err};
		}

		addChanges(ctx, fixContext, changeTracker, importAdder.get(),
		           typeChecker, classDeclaration, implementedTypeNode);
		auto changes = getChanges(changeTracker, importAdder.get(),
		                          fixContext->SourceFile);
		if (changes.empty()) {
			continue;
		}

		actions.push_back(new CodeAction{
		    .Description =
		        ::tsc::localize(loc, Implement_interface_0, "",
		                        {getTextOfNode(implementedTypeNode)}),
		    .Changes = changes,
		    .FixID = fixClassIncorrectlyImplementsInterfaceFixID,
		    .FixAllDescription =
		        ::tsc::localize(loc, Implement_all_unimplemented_interfaces,
		                        "", {}),
		});
	}
	return {actions, nullptr};
}

// getAllCodeActionsToFixClassIncorrectlyImplementsInterface —
// codeactions_fixclassincorrectlyimplementsinterface.go:69.
std::pair<CombinedCodeActions*, gostd::Error>
getAllCodeActionsToFixClassIncorrectlyImplementsInterface(
    const gostd::Context& ctx, CodeFixContext* fixContext) {
	auto [typeChecker, done] =
	    fixContext->Program->GetTypeCheckerForFileExclusive(
	        fixContext->SourceFile);
	struct doneGuard {
		std::function<void()> f;
		~doneGuard() { if (f) f(); }
	} _done{done};

	auto* changeTracker = change::NewTracker(
	    ctx, fixContext->Program->Options(), fixContext->LS->FormatOptions(),
	    fixContext->LS->converters);
	auto [importAdder, err] = createImportAdder(ctx, fixContext, typeChecker);
	if (err != nullptr) {
		return {nullptr, err};
	}

	collections::Set<Node*> seenClassDeclarations;

	for (auto* diag : getAllDiagnostics(ctx, fixContext->Program,
	                                  fixContext->SourceFile)) {
		if (isFixableDiagnostic(
		        diag, fixClassIncorrectlyImplementsInterfaceErrorCodes)) {
			auto* classDeclaration =
			    getClass(fixContext->SourceFile,
			             TextRange{diag->Pos(), diag->End()});
			if (classDeclaration == nullptr) {
				continue;
			}
			if (!seenClassDeclarations.Has(classDeclaration)) {
				seenClassDeclarations.Add(classDeclaration);
				auto implementsTypes = getHeritageElements(
				    classDeclaration, Kind::ImplementsKeyword);
				for (auto* implementedTypeNode : implementsTypes) {
					addChanges(ctx, fixContext, changeTracker,
					           importAdder.get(), typeChecker,
					           classDeclaration, implementedTypeNode);
				}
			}
		}
	}

	auto changes = getChanges(changeTracker, importAdder.get(),
	                          fixContext->SourceFile);
	if (changes.empty()) {
		return {nullptr, nullptr};
	}

	return {new CombinedCodeActions{
	            .Description = ::tsc::localize(
	                locale::fromContext(ctx),
	                Implement_all_unimplemented_interfaces, "", {}),
	            .Changes = changes},
	        nullptr};
}

} // namespace

// FixClassIncorrectlyImplementsInterfaceProvider —
// codeactions_fixclassincorrectlyimplementsinterface.go:26.
CodeFixProvider* FixClassIncorrectlyImplementsInterfaceProvider =
    new CodeFixProvider{
        .ErrorCodes =
            fixClassIncorrectlyImplementsInterfaceErrorCodes,
        .GetCodeActions =
            [](const gostd::Context& ctx, CodeFixContext* fixContext) {
	            return getCodeActionsToFixClassIncorrectlyImplementsInterface(
	                ctx, fixContext);
            },
        .FixIds = {fixClassIncorrectlyImplementsInterfaceFixID},
        .GetAllCodeActions =
            [](const gostd::Context& ctx, CodeFixContext* fixContext) {
	            return getAllCodeActionsToFixClassIncorrectlyImplementsInterface(
	                ctx, fixContext);
            },
    };

} // namespace tsc::ls
