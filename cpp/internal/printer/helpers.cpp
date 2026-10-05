// Port of tsc/internal/printer/helpers.go — the EmitHelper globals the
// transformer emits calls to (priority-ordered text snippets) plus
// compareEmitHelpers.
#include "internal/printer/printer.h"

namespace tsc::printer {

int compareEmitHelpers(const EmitHelper* x, const EmitHelper* y) {
	if (x == y) {
		return 0;
	}
	if (x->Priority == y->Priority) {
		return 0;
	}
	if (x->Priority == nullptr) {
		return 1;
	}
	if (y->Priority == nullptr) {
		return -1;
	}
	return *x->Priority - *y->Priority;
}

namespace {
// static Priorities — helpers share Priority pointers so compareEmitHelpers's
// pointer-equality fast path matches Go semantics.
int32_t priority0{0}, priority1{1}, priority2{2}, priority3{3}, priority4{4},
	priority5{5};
} // namespace

// TypeScript Helpers

EmitHelper decorateHelper_{
	.Name = "typescript:decorate",
	.Scoped = false,
	.Text =
		R"(var __decorate = (this && this.__decorate) || function (decorators, target, key, desc) {
    var c = arguments.length, r = c < 3 ? target : desc === null ? desc = Object.getOwnPropertyDescriptor(target, key) : desc, d;
    if (typeof Reflect === "object" && typeof Reflect.decorate === "function") r = Reflect.decorate(decorators, target, key, desc);
    else for (var i = decorators.length - 1; i >= 0; i--) if (d = decorators[i]) r = (c < 3 ? d(r) : c > 3 ? d(target, key, r) : d(target, key)) || r;
    return c > 3 && r && Object.defineProperty(target, key, r), r;
};)",
	.Priority = &priority2,
	.ImportName = "__decorate",
};
EmitHelper* decorateHelper = &decorateHelper_;

EmitHelper metadataHelper_{
	.Name = "typescript:metadata",
	.Scoped = false,
	.Text =
		R"(var __metadata = (this && this.__metadata) || function (k, v) {
    if (typeof Reflect === "object" && typeof Reflect.metadata === "function") return Reflect.metadata(k, v);
};)",
	.Priority = &priority3,
	.ImportName = "__metadata",
};
EmitHelper* metadataHelper = &metadataHelper_;

EmitHelper paramHelper_{
	.Name = "typescript:param",
	.Scoped = false,
	.Text =
		R"(var __param = (this && this.__param) || function (paramIndex, decorator) {
    return function (target, key) { decorator(target, key, paramIndex); }
};)",
	.Priority = &priority4,
	.ImportName = "__param",
};
EmitHelper* paramHelper = &paramHelper_;

// ESNext Helpers

EmitHelper addDisposableResourceHelper_{
	.Name = "typescript:addDisposableResource",
	.Scoped = false,
	.Text =
		R"(var __addDisposableResource = (this && this.__addDisposableResource) || function (env, value, async) {
    if (value !== null && value !== void 0) {
        if (typeof value !== "object" && typeof value !== "function") throw new TypeError("Object expected.");
        var dispose, inner;
        if (async) {
            if (!Symbol.asyncDispose) throw new TypeError("Symbol.asyncDispose is not defined.");
            dispose = value[Symbol.asyncDispose];
        }
        if (dispose === void 0) {
            if (!Symbol.dispose) throw new TypeError("Symbol.dispose is not defined.");
            dispose = value[Symbol.dispose];
            if (async) inner = dispose;
        }
        if (typeof dispose !== "function") throw new TypeError("Object not disposable.");
        if (inner) dispose = function() { try { inner.call(this); } catch (e) { return Promise.reject(e); } };
        env.stack.push({ value: value, dispose: dispose, async: async });
    }
    else if (async) {
        env.stack.push({ async: true });
    }
    return value;
};)",
	.ImportName = "__addDisposableResource",
};
EmitHelper* addDisposableResourceHelper = &addDisposableResourceHelper_;

EmitHelper disposeResourcesHelper_{
	.Name = "typescript:disposeResources",
	.Scoped = false,
	.Text =
		R"(var __disposeResources = (this && this.__disposeResources) || (function (SuppressedError) {
    return function (env) {
        function fail(e) {
            env.error = env.hasError ? new SuppressedError(e, env.error, "An error was suppressed during disposal.") : e;
            env.hasError = true;
        }
        var r, s = 0;
        function next() {
            while (r = env.stack.pop()) {
                try {
                    if (!r.async && s === 1) return s = 0, env.stack.push(r), Promise.resolve().then(next);
                    if (r.dispose) {
                        var result = r.dispose.call(r.value);
                        if (r.async) return s |= 2, Promise.resolve(result).then(next, function(e) { fail(e); return next(); });
                    }
                    else s |= 1;
                }
                catch (e) {
                    fail(e);
                }
            }
            if (s === 1) return env.hasError ? Promise.reject(env.error) : Promise.resolve();
            if (env.hasError) throw env.error;
        }
        return next();
    };
})(typeof SuppressedError === "function" ? SuppressedError : function (error, suppressed, message) {
    var e = new Error(message);
    return e.name = "SuppressedError", e.error = error, e.suppressed = suppressed, e;
});)",
	.ImportName = "__disposeResources",
};
EmitHelper* disposeResourcesHelper = &disposeResourcesHelper_;

// Class Fields Helpers

EmitHelper classPrivateFieldGetHelper_{
	.Name = "typescript:classPrivateFieldGet",
	.Scoped = false,
	.Text =
		R"(var __classPrivateFieldGet = (this && this.__classPrivateFieldGet) || function (receiver, state, kind, f) {
    if (kind === "a" && !f) throw new TypeError("Private accessor was defined without a getter");
    if (typeof state === "function" ? receiver !== state || !f : !state.has(receiver)) throw new TypeError("Cannot read private member from an object whose class did not declare it");
    return kind === "m" ? f : kind === "a" ? f.call(receiver) : f ? f.value : state.get(receiver);
};)",
	.ImportName = "__classPrivateFieldGet",
};
EmitHelper* classPrivateFieldGetHelper = &classPrivateFieldGetHelper_;

EmitHelper classPrivateFieldSetHelper_{
	.Name = "typescript:classPrivateFieldSet",
	.Scoped = false,
	.Text =
		R"(var __classPrivateFieldSet = (this && this.__classPrivateFieldSet) || function (receiver, state, value, kind, f) {
    if (kind === "m") throw new TypeError("Private method is not writable");
    if (kind === "a" && !f) throw new TypeError("Private accessor was defined without a setter");
    if (typeof state === "function" ? receiver !== state || !f : !state.has(receiver)) throw new TypeError("Cannot write private member to an object whose class did not declare it");
    return (kind === "a" ? f.call(receiver, value) : f ? f.value = value : state.set(receiver, value)), value;
};)",
	.ImportName = "__classPrivateFieldSet",
};
EmitHelper* classPrivateFieldSetHelper = &classPrivateFieldSetHelper_;

EmitHelper classPrivateFieldInHelper_{
	.Name = "typescript:classPrivateFieldIn",
	.Scoped = false,
	.Text =
		R"(var __classPrivateFieldIn = (this && this.__classPrivateFieldIn) || function(state, receiver) {
    if (receiver === null || (typeof receiver !== "object" && typeof receiver !== "function")) throw new TypeError("Cannot use 'in' operator on non-object");
    return typeof state === "function" ? receiver === state : state.has(receiver);
};)",
	.ImportName = "__classPrivateFieldIn",
};
EmitHelper* classPrivateFieldInHelper = &classPrivateFieldInHelper_;

// ES2018 Helpers

EmitHelper awaitHelper_{
	.Name = "typescript:await",
	.Scoped = false,
	.Text =
		R"(var __await = (this && this.__await) || function (v) { return this instanceof __await ? (this.v = v, this) : new __await(v); })",
	.ImportName = "__await",
};
EmitHelper* awaitHelper = &awaitHelper_;

EmitHelper asyncGeneratorHelper_{
	.Name = "typescript:asyncGenerator",
	.Scoped = false,
	.Text =
		R"(var __asyncGenerator = (this && this.__asyncGenerator) || function (thisArg, _arguments, generator) {
    if (!Symbol.asyncIterator) throw new TypeError("Symbol.asyncIterator is not defined.");
    var g = generator.apply(thisArg, _arguments || []), i, q = [];
    return i = Object.create((typeof AsyncIterator === "function" ? AsyncIterator : Object).prototype), verb("next"), verb("throw"), verb("return", awaitReturn), i[Symbol.asyncIterator] = function () { return this; }, i;
    function awaitReturn(f) { return function (v) { return Promise.resolve(v).then(f, reject); }; }
    function verb(n, f) { if (g[n]) { i[n] = function (v) { return new Promise(function (a, b) { q.push([n, v, a, b]) > 1 || resume(n, v); }); }; if (f) i[n] = f(i[n]); } }
    function resume(n, v) { try { step(g[n](v)); } catch (e) { settle(q[0][3], e); } }
    function step(r) { r.value instanceof __await ? Promise.resolve(r.value.v).then(fulfill, reject) : settle(q[0][2], r); }
    function fulfill(value) { resume("next", value); }
    function reject(value) { resume("throw", value); }
    function settle(f, v) { if (f(v), q.shift(), q.length) resume(q[0][0], q[0][1]); }
};)",
	.Dependencies = {awaitHelper},
	.ImportName = "__asyncGenerator",
};
EmitHelper* asyncGeneratorHelper = &asyncGeneratorHelper_;

EmitHelper asyncDelegatorHelper_{
	.Name = "typescript:asyncDelegator",
	.Scoped = false,
	.Text =
		R"(var __asyncDelegator = (this && this.__asyncDelegator) || function (o) {
    var i, p;
    return i = {}, verb("next"), verb("throw", function (e) { throw e; }), verb("return"), i[Symbol.iterator] = function () { return this; }, i;
    function verb(n, f) { i[n] = o[n] ? function (v) { return (p = !p) ? { value: __await(o[n](v)), done: false } : f ? f(v) : v; } : f; }
};)",
	.Dependencies = {awaitHelper},
	.ImportName = "__asyncDelegator",
};
EmitHelper* asyncDelegatorHelper = &asyncDelegatorHelper_;

EmitHelper asyncValuesHelper_{
	.Name = "typescript:asyncValues",
	.Scoped = false,
	.Text =
		R"(var __asyncValues = (this && this.__asyncValues) || function (o) {
    if (!Symbol.asyncIterator) throw new TypeError("Symbol.asyncIterator is not defined.");
    var m = o[Symbol.asyncIterator], i;
    return m ? m.call(o) : (o = typeof __values === "function" ? __values(o) : o[Symbol.iterator](), i = {}, verb("next"), verb("throw"), verb("return"), i[Symbol.asyncIterator] = function () { return this; }, i);
    function verb(n) { i[n] = o[n] && function (v) { return new Promise(function (resolve, reject) { v = o[n](v), settle(resolve, reject, v.done, v.value); }); }; }
    function settle(resolve, reject, d, v) { Promise.resolve(v).then(function(v) { resolve({ value: v, done: d }); }, reject); }
};)",
	.ImportName = "__asyncValues",
};
EmitHelper* asyncValuesHelper = &asyncValuesHelper_;

// ES2018 Destructuring Helpers

EmitHelper restHelper_{
	.Name = "typescript:rest",
	.Scoped = false,
	.Text =
		R"(var __rest = (this && this.__rest) || function (s, e) {
    var t = {};
    for (var p in s) if (Object.prototype.hasOwnProperty.call(s, p) && e.indexOf(p) < 0)
        t[p] = s[p];
    if (s != null && typeof Object.getOwnPropertySymbols === "function")
        for (var i = 0, p = Object.getOwnPropertySymbols(s); i < p.length; i++) {
            if (e.indexOf(p[i]) < 0 && Object.prototype.propertyIsEnumerable.call(s, p[i]))
                t[p[i]] = s[p[i]];
        }
    return t;
};)",
	.ImportName = "__rest",
};
EmitHelper* restHelper = &restHelper_;

EmitHelper awaiterHelper_{
	.Name = "typescript:awaiter",
	.Scoped = false,
	.Text =
		R"(var __awaiter = (this && this.__awaiter) || function (thisArg, _arguments, P, generator) {
    function adopt(value) { return value instanceof P ? value : new P(function (resolve) { resolve(value); }); }
    return new (P || (P = Promise))(function (resolve, reject) {
        function fulfilled(value) { try { step(generator.next(value)); } catch (e) { reject(e); } }
        function rejected(value) { try { step(generator["throw"](value)); } catch (e) { reject(e); } }
        function step(result) { result.done ? resolve(result.value) : adopt(result.value).then(fulfilled, rejected); }
        step((generator = generator.apply(thisArg, _arguments || [])).next());
    });
};)",
	.Priority = &priority5,
	.ImportName = "__awaiter",
};
EmitHelper* awaiterHelper = &awaiterHelper_;

EmitHelper AsyncSuperHelper_{
	.Name = "typescript:async-super",
	.Scoped = true,
	.TextCallback =
		[](const std::function<std::string(const std::string&)>& makeUniqueName)
		-> std::string {
		return "\nconst " + makeUniqueName("_superIndex") +
		       " = name => super[name];";
	},
};
EmitHelper* AsyncSuperHelper = &AsyncSuperHelper_;

EmitHelper AdvancedAsyncSuperHelper_{
	.Name = "typescript:advanced-async-super",
	.Scoped = true,
	.TextCallback =
		[](const std::function<std::string(const std::string&)>& makeUniqueName)
		-> std::string {
		return "\nconst " + makeUniqueName("_superIndex") +
		       " = (function (geti, seti) {\n"
		       "    const cache = Object.create(null);\n"
		       "    return name => cache[name] || (cache[name] = { get value() { "
		       "return geti(name); }, set value(v) { seti(name, v); } });\n"
		       "})(name => super[name], (name, value) => super[name] = value);";
	},
};
EmitHelper* AdvancedAsyncSuperHelper = &AdvancedAsyncSuperHelper_;

// ES Decorator Helpers

EmitHelper esDecorateHelper_{
	.Name = "typescript:esDecorate",
	.Scoped = false,
	.Text =
		R"(var __esDecorate = (this && this.__esDecorate) || function (ctor, descriptorIn, decorators, contextIn, initializers, extraInitializers) {
    function accept(f) { if (f !== void 0 && typeof f !== "function") throw new TypeError("Function expected"); return f; }
    var kind = contextIn.kind, key = kind === "getter" ? "get" : kind === "setter" ? "set" : "value";
    var target = !descriptorIn && ctor ? contextIn["static"] ? ctor : ctor.prototype : null;
    var descriptor = descriptorIn || (target ? Object.getOwnPropertyDescriptor(target, contextIn.name) : {});
    var _, done = false;
    for (var i = decorators.length - 1; i >= 0; i--) {
        var context = {};
        for (var p in contextIn) context[p] = p === "access" ? {} : contextIn[p];
        for (var p in contextIn.access) context.access[p] = contextIn.access[p];
        context.addInitializer = function (f) { if (done) throw new TypeError("Cannot add initializers after decoration has completed"); extraInitializers.push(accept(f || null)); };
        var result = (0, decorators[i])(kind === "accessor" ? { get: descriptor.get, set: descriptor.set } : descriptor[key], context);
        if (kind === "accessor") {
            if (result === void 0) continue;
            if (result === null || typeof result !== "object") throw new TypeError("Object expected");
            if (_ = accept(result.get)) descriptor.get = _;
            if (_ = accept(result.set)) descriptor.set = _;
            if (_ = accept(result.init)) initializers.unshift(_);
        }
        else if (_ = accept(result)) {
            if (kind === "field") initializers.unshift(_);
            else descriptor[key] = _;
        }
    }
    if (target) Object.defineProperty(target, contextIn.name, descriptor);
    done = true;
};)",
	.Priority = &priority2,
	.ImportName = "__esDecorate",
};
EmitHelper* esDecorateHelper = &esDecorateHelper_;

EmitHelper runInitializersHelper_{
	.Name = "typescript:runInitializers",
	.Scoped = false,
	.Text =
		R"(var __runInitializers = (this && this.__runInitializers) || function (thisArg, initializers, value) {
    var useValue = arguments.length > 2;
    for (var i = 0; i < initializers.length; i++) {
        value = useValue ? initializers[i].call(thisArg, value) : initializers[i].call(thisArg);
    }
    return useValue ? value : void 0;
};)",
	.Priority = &priority2,
	.ImportName = "__runInitializers",
};
EmitHelper* runInitializersHelper = &runInitializersHelper_;

// ES2015 Helpers

EmitHelper makeTemplateObjectHelper_{
	.Name = "typescript:makeTemplateObject",
	.Scoped = false,
	.Text =
		R"(var __makeTemplateObject = (this && this.__makeTemplateObject) || function (cooked, raw) {
    if (Object.defineProperty) { Object.defineProperty(cooked, "raw", { value: raw }); } else { cooked.raw = raw; }
    return cooked;
};)",
	.Priority = &priority0,
	.ImportName = "__makeTemplateObject",
};
EmitHelper* makeTemplateObjectHelper = &makeTemplateObjectHelper_;

EmitHelper propKeyHelper_{
	.Name = "typescript:propKey",
	.Scoped = false,
	.Text =
		R"(var __propKey = (this && this.__propKey) || function (x) {
    return typeof x === "symbol" ? x : "".concat(x);
};)",
	.ImportName = "__propKey",
};
EmitHelper* propKeyHelper = &propKeyHelper_;

// https://tc39.es/ecma262/#sec-setfunctionname
EmitHelper setFunctionNameHelper_{
	.Name = "typescript:setFunctionName",
	.Scoped = false,
	.Text =
		R"(var __setFunctionName = (this && this.__setFunctionName) || function (f, name, prefix) {
    if (typeof name === "symbol") name = name.description ? "[".concat(name.description, "]") : "";
    return Object.defineProperty(f, "name", { configurable: true, value: prefix ? "".concat(prefix, " ", name) : name });
};)",
	.ImportName = "__setFunctionName",
};
EmitHelper* setFunctionNameHelper = &setFunctionNameHelper_;

// ES Module Helpers

EmitHelper createBindingHelper_{
	.Name = "typescript:commonjscreatebinding",
	.Scoped = false,
	.Text =
		R"(var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));)",
	.Priority = &priority1,
	.ImportName = "__createBinding",
};
EmitHelper* createBindingHelper = &createBindingHelper_;

EmitHelper setModuleDefaultHelper_{
	.Name = "typescript:commonjscreatevalue",
	.Scoped = false,
	.Text =
		R"(var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});)",
	.Priority = &priority1,
	.ImportName = "__setModuleDefault",
};
EmitHelper* setModuleDefaultHelper = &setModuleDefaultHelper_;

EmitHelper importStarHelper_{
	.Name = "typescript:commonjsimportstar",
	.Scoped = false,
	.Text =
		R"(var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();)",
	.Priority = &priority2,
	.Dependencies = {createBindingHelper, setModuleDefaultHelper},
	.ImportName = "__importStar",
};
EmitHelper* importStarHelper = &importStarHelper_;

EmitHelper importDefaultHelper_{
	.Name = "typescript:commonjsimportdefault",
	.Scoped = false,
	.Text =
		R"(var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};)",
	.ImportName = "__importDefault",
};
EmitHelper* importDefaultHelper = &importDefaultHelper_;

EmitHelper exportStarHelper_{
	.Name = "typescript:export-star",
	.Scoped = false,
	.Text =
		R"(var __exportStar = (this && this.__exportStar) || function(m, exports) {
    for (var p in m) if (p !== "default" && !Object.prototype.hasOwnProperty.call(exports, p)) __createBinding(exports, m, p);
};)",
	.Priority = &priority2,
	.Dependencies = {createBindingHelper},
	.ImportName = "__exportStar",
};
EmitHelper* exportStarHelper = &exportStarHelper_;

EmitHelper rewriteRelativeImportExtensionsHelper_{
	.Name = "typescript:rewriteRelativeImportExtensions",
	.Scoped = false,
	.Text =
		R"(var __rewriteRelativeImportExtension = (this && this.__rewriteRelativeImportExtension) || function (path, preserveJsx) {
    if (typeof path === "string" && /^\.\.?\//.test(path)) {
        return path.replace(/\.(tsx)$|((?:\.d)?)((?:\.[^./]+?)?)\.([cm]?)ts$/i, function (m, tsx, d, ext, cm) {
            return tsx ? preserveJsx ? ".jsx" : ".js" : d && (!ext || !cm) ? m : (d + ext + "." + cm.toLowerCase() + "js");
        });
    }
    return path;
};)",
	.ImportName = "__rewriteRelativeImportExtension",
};
EmitHelper* rewriteRelativeImportExtensionsHelper =
	&rewriteRelativeImportExtensionsHelper_;

} // namespace tsc::printer
