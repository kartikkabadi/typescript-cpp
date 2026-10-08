// crossproject.go — cross-project orchestration types and result combiners.
// handleCrossProject is a member template defined in ls.h.
#include "internal/ls/ls.h"

#include <optional>
#include <unordered_map>

namespace tsc::ls {

// combineReferences — crossproject.go:322.
lsproto::ReferencesResponse combineReferences(
    const std::function<void(
        const std::function<bool(lsproto::ReferencesResponse&)>&)>&
        resultsSeq) {
	return lsproto::LocationsOrNull{
	    .Locations = combineResponseLocations(resultsSeq)};
}

// combineVSReferences — crossproject.go:326. Re-numbers IDs across projects
// to maintain unique IDs and correct definition references.
lsproto::VSReferencesResponse combineVSReferences(
    const std::function<void(
        const std::function<bool(lsproto::VSReferencesResponse&)>&)>&
        resultsSeq) {
	std::vector<std::shared_ptr<lsproto::VSReferenceItem>> combined;
	int32_t nextId = 0;
	resultsSeq([&](lsproto::VSReferencesResponse& resp) -> bool {
		if (resp.VSReferenceItems == nullptr) {
			return true;
		}
		// Map old IDs to new IDs for this batch
		std::unordered_map<int32_t, int32_t> idMap;
		for (auto& item : **resp.VSReferenceItems) {
			auto oldId = item->VSId;
			auto newId = nextId;
			idMap[oldId] = newId;
			nextId++;

			auto newItem =
				std::make_shared<lsproto::VSReferenceItem>(*item);
			newItem->VSId = newId;
			if (item->VSDefinitionId.has_value()) {
				auto newDefId = idMap[*item->VSDefinitionId];
				newItem->VSDefinitionId = newDefId;
			}
			combined.push_back(newItem);
		}
		return true;
	});
	return lsproto::VSReferenceItemsOrNull{
	    .VSReferenceItems = std::make_shared<lsproto::Slice<
	        std::shared_ptr<lsproto::VSReferenceItem>>>(combined)};
}

// combineImplementations — crossproject.go:354.
lsproto::ImplementationResponse combineImplementations(
    const std::function<void(
        const std::function<bool(lsproto::ImplementationResponse&)>&)>&
        resultsSeq) {
	std::vector<std::shared_ptr<lsproto::LocationLink>> combined;
	collections::Set<lsproto::Location> seenLocations;
	std::optional<lsproto::ImplementationResponse> early;
	resultsSeq(
	    [&](lsproto::ImplementationResponse& resp) -> bool {
		    if (resp.DefinitionLinks != nullptr) {
			    combined = combineLocationArray(
			        combined, resp.DefinitionLinks, &seenLocations);
			    return true;
		    }
		    if (resp.Locations != nullptr) {
			    // Go returns here mid-range; the seq re-iterates
			    // from the start inside combineResponseLocations,
			    // matching Go's nested fresh iteration.
			    early = lsproto::
			        LocationOrLocationsOrDefinitionLinksOrNull{
			            .Locations =
			                combineResponseLocations(resultsSeq)};
			    return false;
		    }
		    return true;
	    });
	if (early.has_value()) {
		return *early;
	}
	return lsproto::LocationOrLocationsOrDefinitionLinksOrNull{
	    .DefinitionLinks = std::make_shared<lsproto::Slice<
	        std::shared_ptr<lsproto::LocationLink>>>(combined)};
}

// combineRenameResponse — crossproject.go:367.
lsproto::RenameResponse combineRenameResponse(
    const std::function<void(
        const std::function<bool(lsproto::RenameResponse&)>&)>&
        resultsSeq) {
	lsproto::Map<lsproto::DocumentUri,
	             lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
	    combined;
	lsproto::Map<lsproto::DocumentUri, collections::Set<lsproto::Range>*>
	    seenChanges;
	std::vector<
	    lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>
	    documentChanges;
	collections::Set<std::string> seenRenames;

	resultsSeq([&](lsproto::RenameResponse& resp) -> bool {
		if (resp.WorkspaceEdit != nullptr &&
		    resp.WorkspaceEdit->DocumentChanges != nullptr) {
			for (auto& change :
			     **resp.WorkspaceEdit->DocumentChanges) {
				if (change.RenameFile != nullptr) {
					std::string key =
					    change.RenameFile->OldUri + "\x1f" +
					    change.RenameFile->NewUri;
					if (!seenRenames.Has(key)) {
						seenRenames.Add(key);
						documentChanges.push_back(change);
					}
				} else {
					documentChanges.push_back(change);
				}
			}
		}
		if (resp.WorkspaceEdit != nullptr &&
		    resp.WorkspaceEdit->Changes != nullptr) {
			for (auto& [doc, changes] :
			     *resp.WorkspaceEdit->Changes) {
				auto it = seenChanges.find(doc);
				collections::Set<lsproto::Range>* seenSet;
				if (it == seenChanges.end()) {
					seenSet = new collections::Set<
					    lsproto::Range>();
					seenChanges[doc] = seenSet;
				} else {
					seenSet = it->second;
				}
				auto& changesForDoc = combined[doc];
				if (!changesForDoc.has_value()) {
					changesForDoc = std::vector<
					    std::shared_ptr<lsproto::TextEdit>>{};
				}
				for (auto& change : *changes) {
					if (!seenSet->Has(change->Range)) {
						seenSet->Add(change->Range);
						changesForDoc->push_back(change);
					}
				}
			}
		}
		return true;
	});
	if (!documentChanges.empty() || !combined.empty()) {
		auto workspaceEdit = std::make_shared<lsproto::WorkspaceEdit>();
		if (!documentChanges.empty()) {
			workspaceEdit->DocumentChanges =
			    std::make_shared<lsproto::Slice<lsproto::
			                                      TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>>(
			        std::move(documentChanges));
		}
		if (!combined.empty()) {
			workspaceEdit->Changes =
			    std::make_shared<lsproto::Map<
			        lsproto::DocumentUri,
			        lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>>(
			        std::move(combined));
		}
		return lsproto::RenameResponse{
		    .WorkspaceEdit = workspaceEdit,
		};
	}
	return lsproto::RenameResponse{};
}

// combineIncomingCalls — crossproject.go:423.
lsproto::CallHierarchyIncomingCallsResponse combineIncomingCalls(
    const std::function<void(
        const std::function<
            bool(lsproto::CallHierarchyIncomingCallsResponse&)>&)>&
        resultsSeq) {
	std::vector<std::shared_ptr<lsproto::CallHierarchyIncomingCall>>
	    combined;
	collections::Set<lsproto::Location> seenCalls;
	resultsSeq(
	    [&](lsproto::CallHierarchyIncomingCallsResponse& resp) -> bool {
		    if (resp.CallHierarchyIncomingCalls != nullptr) {
			    for (auto& call :
			         **resp.CallHierarchyIncomingCalls) {
				    auto callLoc = call->From->GetLocation();
				    if (!seenCalls.Has(callLoc)) {
					    seenCalls.Add(callLoc);
					    combined.push_back(call);
				    }
			    }
		    }
		    return true;
	    });
	return lsproto::CallHierarchyIncomingCallsOrNull{
	    .CallHierarchyIncomingCalls =
	        std::make_shared<lsproto::Slice<std::shared_ptr<
	            lsproto::CallHierarchyIncomingCall>>>(combined)};
}

} // namespace tsc::ls
