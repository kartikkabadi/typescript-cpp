// Method declarations for struct Node; included inside the struct
// body in ast.h. Generated; DO NOT EDIT.
	template <class F> bool forEachChild(F&& v) const;
	std::vector<::tsc::Diagnostic*> bindDiagnostics() const;
	std::vector<::tsc::Diagnostic*> diagnostics() const;
	std::vector<::tsc::Node*> imports() const;
	std::vector<::tsc::Diagnostic*> jSDiagnostics() const;
	std::vector<::tsc::Diagnostic*> jSDocDiagnostics() const;
	::tsc::Node* name() const;
	::tsc::SourceFileParseOptions parseOptions() const;
	DeclarationDataRef declarationData();
	ExportableDataRef exportableData();
	LocalsContainerDataRef localsContainerData();
	FlowNodeDataRef flowNodeData();
	FunctionLikeDataRef functionLikeData();
	BodyDataRef bodyData();
	ClassLikeDataRef classLikeData();
	LiteralLikeDataRef literalLikeData();
	TemplateLiteralLikeDataRef templateLiteralLikeData();
	ModifierList* modifiers() const;
	void setModifiers(ModifierList* m);
	SubtreeFacts propagateSubtreeFacts() const;
	SubtreeFacts computeSubtreeFacts() const;
	SubtreeFacts subtreeFacts() const;
