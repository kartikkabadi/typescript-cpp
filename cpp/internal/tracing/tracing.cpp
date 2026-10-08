// Port of tsc/internal/tracing/tracing.go — the tracing session itself:
// buffered trace-event file writing (trace.json), per-checker type tracers
// (types_N.json), and the legend file (legend.json). The interface surface
// consumed by the checker (TracedType/Tracer/Tracing/Phase/TraceArgs) lives
// in tracing.h so the checker never depends on this implementation.
#include "internal/tracing/tracing.h"

#include "internal/ast/ast.h"
#include "internal/ast/symbol.h"
#include "internal/gostd/gostd.h"
#include "internal/json/json.h"
#include "internal/scanner/scanner.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"
#include "internal/xxh3/xxh3.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <compare>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace tsc::tracing {

// tracing.go:91 — sampleInterval matches TypeScript's 10ms sampling interval.
// Events with separateBeginAndEnd=false are only recorded if their duration
// crosses a 10ms sampling boundary.
constexpr std::chrono::nanoseconds sampleInterval =
    std::chrono::milliseconds(10);

constexpr const char* traceFileName = "trace.json";

constexpr int mainThreadID = 1;
constexpr int firstSyntheticThreadID = 2;
constexpr int firstFileThreadID = 1'000'000;
constexpr uint64_t fileThreadIDHashRange = 1'000'000'000;

// tracing.go:105 — traceThreadArgKeys.
constexpr std::string_view traceThreadArgKeys[] = {
    "path", "fileName", "containingFileName", "jsFilePath",
    "declarationFilePath",
};

// tracing.go:107 — flushThreshold is the size at which buffered trace content
// is flushed to disk via AppendFile.
constexpr size_t flushThreshold = 256 * 1024;

// ---------------------------------------------------------------------------
// JSON marshaling — Go encodes trace records through encoding/json/v2. We
// emit the same field order and omitzero behavior via marshalJSONTo.
// ---------------------------------------------------------------------------

// writeAnyValue — Go `any` values inside TraceArgs. Covers every dynamic type
// the trace call sites produce; an unknown type is a port bug (Go would have
// marshaled it), so unreachable.
static std::string writeAnyValue(json::Encoder& enc, const std::any& v) {
    const std::type_info& ti = v.type();
    if (ti == typeid(bool)) return enc.writeToken(json::tokenBool(std::any_cast<bool>(v)));
    if (ti == typeid(int)) return enc.writeToken(json::tokenInt(std::any_cast<int>(v)));
    if (ti == typeid(int64_t)) return enc.writeToken(json::tokenInt(std::any_cast<int64_t>(v)));
    if (ti == typeid(uint32_t)) return enc.writeToken(json::tokenUint(std::any_cast<uint32_t>(v)));
    if (ti == typeid(uint64_t)) return enc.writeToken(json::tokenUint(std::any_cast<uint64_t>(v)));
    if (ti == typeid(size_t)) return enc.writeToken(json::tokenUint(std::any_cast<size_t>(v)));
    if (ti == typeid(double)) return enc.writeToken(json::tokenFloat(std::any_cast<double>(v)));
    if (ti == typeid(float)) return enc.writeToken(json::tokenFloat(std::any_cast<float>(v)));
    if (ti == typeid(std::string)) return enc.writeToken(json::tokenString(std::any_cast<const std::string&>(v)));
    if (ti == typeid(const char*)) return enc.writeToken(json::tokenString(std::any_cast<const char*>(v)));
    if (ti == typeid(std::string_view)) return enc.writeToken(json::tokenString(std::any_cast<std::string_view>(v)));
    if (ti == typeid(tsc::Kind)) return enc.writeToken(json::tokenInt(static_cast<int64_t>(std::any_cast<tsc::Kind>(v))));
    if (ti == typeid(std::vector<std::string>)) {
        auto err = enc.writeToken(json::BeginArray);
        if (!err.empty()) return err;
        for (const auto& s : std::any_cast<const std::vector<std::string>&>(v)) {
            if ((err = enc.writeToken(json::tokenString(s))), !err.empty()) return err;
        }
        return enc.writeToken(json::EndArray);
    }
    TSC_UNREACHABLE("writeAnyValue: unsupported arg type");
}

// writeArgsObject — Go `map[string]any` under Deterministic(true): keys are
// emitted in sorted order.
static std::string writeArgsObject(json::Encoder& enc, const TraceArgs& args) {
    auto err = enc.writeToken(json::BeginObject);
    if (!err.empty()) return err;
    std::vector<std::string> keys;
    keys.reserve(args.size());
    for (const auto& kv : args) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    for (const auto& k : keys) {
        if ((err = enc.writeToken(json::tokenString(k))), !err.empty()) return err;
        if ((err = writeAnyValue(enc, args.at(k))), !err.empty()) return err;
    }
    return enc.writeToken(json::EndObject);
}

// traceEvent — tracing.go:79.
struct traceEvent {
    int PID;
    int TID;
    std::string PH;
    std::string Cat;
    double TS;
    std::string Name;
    std::string S;
    std::optional<double> Dur;
    TraceArgs Args;
};

// marshalJSONTo — fields in Go declaration order; `,omitzero` fields (name,
// s, dur, args) are skipped when empty/unset.
static std::string marshalJSONTo(json::Encoder& enc, const traceEvent& e) {
    auto err = enc.writeToken(json::BeginObject);
    if (!err.empty()) return err;
    auto wn = [&](std::string_view name, auto&& val) -> std::string {
        if (auto e2 = enc.writeToken(json::tokenString(name)); !e2.empty()) return e2;
        return json::detail::marshalInto(enc, val);
    };
    // std::string fields cannot go through marshalInto: json::Value is a
    // std::string alias meaning "raw JSON", so strings route to writeToken.
    auto ws = [&](std::string_view name, std::string_view val) -> std::string {
        if (auto e2 = enc.writeToken(json::tokenString(name)); !e2.empty()) return e2;
        return enc.writeToken(json::tokenString(val));
    };
    if ((err = wn("pid", e.PID)), !err.empty()) return err;
    if ((err = wn("tid", e.TID)), !err.empty()) return err;
    if ((err = ws("ph", e.PH)), !err.empty()) return err;
    if ((err = ws("cat", e.Cat)), !err.empty()) return err;
    if ((err = wn("ts", e.TS)), !err.empty()) return err;
    if (!e.Name.empty() && (err = ws("name", e.Name), !err.empty())) return err;
    if (!e.S.empty() && (err = ws("s", e.S), !err.empty())) return err;
    if (e.Dur.has_value() && (err = wn("dur", *e.Dur), !err.empty())) return err;
    if (!e.Args.empty()) {
        if ((err = enc.writeToken(json::tokenString("args"))), !err.empty()) return err;
        if ((err = writeArgsObject(enc, e.Args)), !err.empty()) return err;
    }
    return enc.writeToken(json::EndObject);
}

// TraceRecord — tracing.go:72.
struct TraceRecord {
    std::string ConfigFilePath;
    std::string TracePath;
    std::string TypesPath;
    int CheckerID;
};

static std::string marshalJSONTo(json::Encoder& enc, const TraceRecord& r) {
    auto err = enc.writeToken(json::BeginObject);
    if (!err.empty()) return err;
    auto w = [&](std::string_view name, auto&& val) -> std::string {
        if (auto e2 = enc.writeToken(json::tokenString(name)); !e2.empty()) return e2;
        return json::detail::marshalInto(enc, val);
    };
    auto ws = [&](std::string_view name, std::string_view val) -> std::string {
        if (auto e2 = enc.writeToken(json::tokenString(name)); !e2.empty()) return e2;
        return enc.writeToken(json::tokenString(val));
    };
    if (!r.ConfigFilePath.empty() && (err = ws("configFilePath", r.ConfigFilePath), !err.empty())) return err;
    if (!r.TracePath.empty() && (err = ws("tracePath", r.TracePath), !err.empty())) return err;
    if (!r.TypesPath.empty() && (err = ws("typesPath", r.TypesPath), !err.empty())) return err;
    if ((err = w("checkerId", r.CheckerID)), !err.empty()) return err;
    return enc.writeToken(json::EndObject);
}

// LineAndChar / Location — tracing.go:569-579.
struct LineAndChar {
    int Line;
    int Character;
};

static std::string marshalJSONTo(json::Encoder& enc, const LineAndChar& lc) {
    auto err = enc.writeToken(json::BeginObject);
    if (!err.empty()) return err;
    if ((err = enc.writeToken(json::tokenString("line"))), !err.empty()) return err;
    if ((err = json::detail::marshalInto(enc, lc.Line)), !err.empty()) return err;
    if ((err = enc.writeToken(json::tokenString("character"))), !err.empty()) return err;
    if ((err = json::detail::marshalInto(enc, lc.Character)), !err.empty()) return err;
    return enc.writeToken(json::EndObject);
}

struct Location {
    std::string Path;
    std::optional<LineAndChar> Start;
    std::optional<LineAndChar> End;
};

static std::string marshalJSONTo(json::Encoder& enc, const Location& loc) {
    auto err = enc.writeToken(json::BeginObject);
    if (!err.empty()) return err;
    auto w = [&](std::string_view name, auto&& val) -> std::string {
        if (auto e2 = enc.writeToken(json::tokenString(name)); !e2.empty()) return e2;
        return json::detail::marshalInto(enc, val);
    };
    if ((err = enc.writeToken(json::tokenString("path"))), !err.empty()) return err;
    if ((err = enc.writeToken(json::tokenString(loc.Path))), !err.empty()) return err;
    if (loc.Start.has_value() && (err = w("start", *loc.Start), !err.empty())) return err;
    if (loc.End.has_value() && (err = w("end", *loc.End), !err.empty())) return err;
    return enc.writeToken(json::EndObject);
}

// TypeDescriptor — tracing.go:534.
struct TypeDescriptor {
    uint32_t ID{};
    std::string IntrinsicName;
    std::string SymbolName;
    std::optional<int> RecursionID;
    bool IsTuple = false;
    std::vector<uint32_t> UnionTypes;
    std::vector<uint32_t> IntersectionTypes;
    std::vector<uint32_t> AliasTypeArguments;
    std::optional<uint32_t> KeyofType;
    std::optional<uint32_t> IndexedAccessObjectType;
    std::optional<uint32_t> IndexedAccessIndexType;
    std::optional<uint32_t> ConditionalCheckType;
    std::optional<uint32_t> ConditionalExtendsType;
    // *int32 (not *uint32): unresolved conditional branches serialize as -1.
    std::optional<int32_t> ConditionalTrueType;
    std::optional<int32_t> ConditionalFalseType;
    std::optional<uint32_t> SubstitutionBaseType;
    std::optional<uint32_t> ConstraintType;
    std::optional<uint32_t> InstantiatedType;
    std::vector<uint32_t> TypeArguments;
    std::optional<Location> ReferenceLocation;
    std::optional<uint32_t> ReverseMappedSourceType;
    std::optional<uint32_t> ReverseMappedMappedType;
    std::optional<uint32_t> ReverseMappedConstraintType;
    std::optional<uint32_t> EvolvingArrayElementType;
    std::optional<uint32_t> EvolvingArrayFinalType;
    std::optional<Location> DestructuringPattern;
    std::optional<Location> FirstDeclaration;
    std::vector<std::string> Flags;
    std::string Display;
};

static std::string marshalJSONTo(json::Encoder& enc, const TypeDescriptor& d) {
    auto err = enc.writeToken(json::BeginObject);
    if (!err.empty()) return err;
    auto w = [&](std::string_view name, auto&& val) -> std::string {
        if (auto e2 = enc.writeToken(json::tokenString(name)); !e2.empty()) return e2;
        return json::detail::marshalInto(enc, val);
    };
    auto wo = [&](std::string_view name, const auto& opt) -> std::string {
        if (!opt.has_value()) return std::string{};
        return w(name, *opt);
    };
    auto wv = [&](std::string_view name, const auto& vec) -> std::string {
        if (vec.empty()) return std::string{};
        return w(name, vec);
    };
    auto ws = [&](std::string_view name, std::string_view val) -> std::string {
        if (auto e2 = enc.writeToken(json::tokenString(name)); !e2.empty()) return e2;
        return enc.writeToken(json::tokenString(val));
    };
    // d.Flags is a vector<string>: marshalInto would treat each element as
    // raw JSON (json::Value aliases std::string), so write it manually.
    // `flags` has no omitzero tag — always emitted.
    auto wflags = [&]() -> std::string {
        if (auto e2 = enc.writeToken(json::tokenString("flags")); !e2.empty()) return e2;
        if (auto e2 = enc.writeToken(json::BeginArray); !e2.empty()) return e2;
        for (const auto& f : d.Flags) {
            if (auto e2 = enc.writeToken(json::tokenString(f)); !e2.empty()) return e2;
        }
        return enc.writeToken(json::EndArray);
    };
    if ((err = w("id", d.ID)), !err.empty()) return err;
    if (!d.IntrinsicName.empty() && (err = ws("intrinsicName", d.IntrinsicName), !err.empty())) return err;
    if (!d.SymbolName.empty() && (err = ws("symbolName", d.SymbolName), !err.empty())) return err;
    if ((err = wo("recursionId", d.RecursionID)), !err.empty()) return err;
    if (d.IsTuple && (err = w("isTuple", d.IsTuple), !err.empty())) return err;
    if ((err = wv("unionTypes", d.UnionTypes)), !err.empty()) return err;
    if ((err = wv("intersectionTypes", d.IntersectionTypes)), !err.empty()) return err;
    if ((err = wv("aliasTypeArguments", d.AliasTypeArguments)), !err.empty()) return err;
    if ((err = wo("keyofType", d.KeyofType)), !err.empty()) return err;
    if ((err = wo("indexedAccessObjectType", d.IndexedAccessObjectType)), !err.empty()) return err;
    if ((err = wo("indexedAccessIndexType", d.IndexedAccessIndexType)), !err.empty()) return err;
    if ((err = wo("conditionalCheckType", d.ConditionalCheckType)), !err.empty()) return err;
    if ((err = wo("conditionalExtendsType", d.ConditionalExtendsType)), !err.empty()) return err;
    if ((err = wo("conditionalTrueType", d.ConditionalTrueType)), !err.empty()) return err;
    if ((err = wo("conditionalFalseType", d.ConditionalFalseType)), !err.empty()) return err;
    if ((err = wo("substitutionBaseType", d.SubstitutionBaseType)), !err.empty()) return err;
    if ((err = wo("constraintType", d.ConstraintType)), !err.empty()) return err;
    if ((err = wo("instantiatedType", d.InstantiatedType)), !err.empty()) return err;
    if ((err = wv("typeArguments", d.TypeArguments)), !err.empty()) return err;
    if ((err = wo("referenceLocation", d.ReferenceLocation)), !err.empty()) return err;
    if ((err = wo("reverseMappedSourceType", d.ReverseMappedSourceType)), !err.empty()) return err;
    if ((err = wo("reverseMappedMappedType", d.ReverseMappedMappedType)), !err.empty()) return err;
    if ((err = wo("reverseMappedConstraintType", d.ReverseMappedConstraintType)), !err.empty()) return err;
    if ((err = wo("evolvingArrayElementType", d.EvolvingArrayElementType)), !err.empty()) return err;
    if ((err = wo("evolvingArrayFinalType", d.EvolvingArrayFinalType)), !err.empty()) return err;
    if ((err = wo("destructuringPattern", d.DestructuringPattern)), !err.empty()) return err;
    if ((err = wo("firstDeclaration", d.FirstDeclaration)), !err.empty()) return err;
    // `flags` has no omitzero — always emitted, even empty.
    if ((err = wflags()), !err.empty()) return err;
    if (!d.Display.empty() && (err = ws("display", d.Display), !err.empty())) return err;
    return enc.writeToken(json::EndObject);
}

// ---------------------------------------------------------------------------
// traceThreadKey — tracing.go:353-411.
// ---------------------------------------------------------------------------

using traceThreadKind = std::string_view;
constexpr traceThreadKind traceThreadKindChecker = "checker";
constexpr traceThreadKind traceThreadKindFile = "file";

struct traceThreadKey {
    traceThreadKind kind{};
    std::string text;
    int index = 0;
    bool hasIndex = false;
    auto operator<=>(const traceThreadKey&) const = default;
};

static std::pair<traceThreadKey, bool> traceThreadKeyFromArgs(const TraceArgs& args) {
    if (args.empty()) {
        return {{}, false};
    }

    if (auto it = args.find("checkerId"); it != args.end()) {
        if (auto* checkerID = std::any_cast<int>(&it->second); checkerID != nullptr) {
            return {{traceThreadKindChecker, {}, *checkerID, true}, true};
        }
    }

    for (std::string_view key : traceThreadArgKeys) {
        if (auto it = args.find(std::string(key)); it != args.end()) {
            if (auto* path = std::any_cast<std::string>(&it->second);
                path != nullptr && !path->empty()) {
                return {{traceThreadKindFile, *path, 0, false}, true};
            }
        }
    }

    return {{}, false};
}

// stableTraceThreadID — tracing.go:401. xxh3.New + WriteString + Sum64 is the
// 64-bit XXH3 hash of kind:index-or-text.
static int stableTraceThreadID(const traceThreadKey& key) {
    std::string input;
    input.append(key.kind);
    input.push_back(':');
    if (key.hasIndex) {
        input.append(std::to_string(key.index));
    } else {
        input.append(key.text);
    }
    return firstFileThreadID +
        static_cast<int>(xxh3::hash64(input) % fileThreadIDHashRange);
}

static int defaultThreadID(const traceThreadKey& key) {
    if (key.kind == traceThreadKindChecker && key.hasIndex && key.index >= 0) {
        return firstSyntheticThreadID + key.index;
    }
    return stableTraceThreadID(key);
}

static std::string displayName(const traceThreadKey& key) {
    std::string out{key.kind};
    out.push_back(':');
    if (key.hasIndex) {
        out += std::to_string(key.index);
    } else {
        out += key.text;
    }
    return out;
}

// ---------------------------------------------------------------------------
// typeTracer — tracing.go:485. Per-checker Tracer impl.
// ---------------------------------------------------------------------------

class typeTracer final : public Tracer {
public:
    vfs::FS* fs = nullptr;
    int checkerIndex = 0;
    std::string typesPath;

    // RecordType — tracing.go:493.
    void RecordType(TracedType* typ) override {
        std::lock_guard<std::mutex> lock(mu_);
        types_.push_back(typ);
    }

    // DumpTypes — tracing.go:499. Returns the Go error (empty = nil).
    void DumpTypes() override {
        // Copy the types slice under lock, then release so Display() calls
        // during buildTypeDescriptor don't deadlock when they create new
        // types.
        std::vector<TracedType*> types;
        {
            std::lock_guard<std::mutex> lock(mu_);
            types = types_;
        }
        if (types.empty()) {
            return;
        }

        std::string sb;
        // Write opening bracket (no newline so type ID matches line number)
        sb += '[';

        std::unordered_map<void*, int> recursionIdentityMap;

        for (size_t i = 0; i < types.size(); i++) {
            TypeDescriptor descriptor =
                buildTypeDescriptor(types[i], recursionIdentityMap);

            // json.MarshalWrite(&sb, descriptor) — deterministic not needed:
            // all fields are emitted in declaration order.
            std::ostringstream os;
            json::Encoder enc(os);
            if (auto err = marshalJSONTo(enc, descriptor); !err.empty()) {
                // Go panics/returns error on marshal failure; ours can't fail.
                TSC_UNREACHABLE("marshal type descriptor failed");
            }
            sb += os.str();

            if (i < types.size() - 1) {
                sb += ",\n";
            }
        }

        sb += "]\n";

        if (auto err = fs->WriteFile(typesPath, sb); err) {
            // Go returns the write error; the Tracer interface's DumpTypes is
            // void, so surface it through the Go channel: StopTracing turns
            // DumpTypes errors into errors — keep that by recording it.
            dumpErr_ = gostd::newError("failed to write types file: " + err.str());
        }
    }

    // First dump error, surfaced by TracingImpl::stop like Go's DumpTypes
    // error path.
    gostd::Error dumpErr() {
        std::lock_guard<std::mutex> lock(mu_);
        return dumpErr_;
    }

private:
    std::vector<TracedType*> types_;
    std::mutex mu_;
    gostd::Error dumpErr_;

    // mapTypeIds — tracing.go:726.
    static std::vector<uint32_t> mapTypeIds(const std::vector<TracedType*>& ts) {
        std::vector<uint32_t> ids;
        ids.reserve(ts.size());
        for (TracedType* t : ts) {
            ids.push_back(t != nullptr ? t->Id() : 0);
        }
        return ids;
    }

    // getLocation — tracing.go:739.
    static std::optional<Location> getLocation(tsc::Node* node) {
        if (node == nullptr) {
            return std::nullopt;
        }
        auto* file = tsc::getSourceFileOfNode(node);
        if (file == nullptr) {
            return std::nullopt;
        }

        int startPos = tsc::getTokenPosOfNode(node, file, false);
        auto [startLine, startChar] =
            tsc::getECMALineAndUTF16CharacterOfPosition(file, startPos);
        auto [endLine, endChar] =
            tsc::getECMALineAndUTF16CharacterOfPosition(
                file, node->end());

        return Location{
            std::string(tsc::tspath::toPath(file->FileName(), "", false)),
            LineAndChar{startLine + 1, startChar + 1},
            LineAndChar{endLine + 1, endChar + 1},
        };
    }

    // buildTypeDescriptor — tracing.go:581.
    static TypeDescriptor buildTypeDescriptor(
        TracedType* typ, std::unordered_map<void*, int>& recursionIdentityMap) {
        tsc::Symbol* symbol = typ->Symbol();
        tsc::Symbol* aliasSymbol = typ->AliasSymbol();

        TypeDescriptor desc;
        desc.ID = typ->Id();
        desc.Flags = typ->FormatFlags();

        // Assign a unique integer token per recursion identity, matching
        // TypeScript's behavior. This lets trace analysis tools detect which
        // types share the same recursion identity.
        if (void* identity = typ->RecursionIdentity(); identity != nullptr) {
            auto [it, inserted] =
                recursionIdentityMap.try_emplace(identity, (int)recursionIdentityMap.size());
            desc.RecursionID = it->second;
        }

        // Intrinsic name
        if (std::string name = typ->IntrinsicName(); !name.empty()) {
            desc.IntrinsicName = std::move(name);
        }

        // Symbol name — escape the internal symbol name prefix for valid JSON
        if (tsc::Symbol* sym = aliasSymbol; sym != nullptr) {
            desc.SymbolName = tsc::escapeAllInternalSymbolNames(sym->name);
        } else if (symbol != nullptr) {
            desc.SymbolName = tsc::escapeAllInternalSymbolNames(symbol->name);
        }

        // Tuple flag
        if (typ->IsTuple()) {
            desc.IsTuple = true;
        }

        // Union types
        if (auto types = typ->UnionTypes(); !types.empty()) {
            desc.UnionTypes = mapTypeIds(types);
        }

        // Intersection types
        if (auto types = typ->IntersectionTypes(); !types.empty()) {
            desc.IntersectionTypes = mapTypeIds(types);
        }

        // Alias type arguments
        if (auto args = typ->AliasTypeArguments(); !args.empty()) {
            desc.AliasTypeArguments = mapTypeIds(args);
        }

        // Index type (keyof)
        if (TracedType* indexType = typ->IndexType(); indexType != nullptr) {
            desc.KeyofType = indexType->Id();
        }

        // Indexed access type
        if (TracedType* objType = typ->IndexedAccessObjectType(); objType != nullptr) {
            desc.IndexedAccessObjectType = objType->Id();
        }
        if (TracedType* idxType = typ->IndexedAccessIndexType(); idxType != nullptr) {
            desc.IndexedAccessIndexType = idxType->Id();
        }

        // Conditional type
        if (typ->IsConditional()) {
            if (TracedType* checkType = typ->ConditionalCheckType(); checkType != nullptr) {
                desc.ConditionalCheckType = checkType->Id();
            }
            if (TracedType* extendsType = typ->ConditionalExtendsType(); extendsType != nullptr) {
                desc.ConditionalExtendsType = extendsType->Id();
            }
            if (TracedType* trueType = typ->ConditionalTrueType(); trueType != nullptr) {
                desc.ConditionalTrueType = static_cast<int32_t>(trueType->Id());
            } else {
                desc.ConditionalTrueType = -1;
            }
            if (TracedType* falseType = typ->ConditionalFalseType(); falseType != nullptr) {
                desc.ConditionalFalseType = static_cast<int32_t>(falseType->Id());
            } else {
                desc.ConditionalFalseType = -1;
            }
        }

        // Substitution type
        if (TracedType* baseType = typ->SubstitutionBaseType(); baseType != nullptr) {
            desc.SubstitutionBaseType = baseType->Id();
        }
        if (TracedType* constraint = typ->SubstitutionConstraintType(); constraint != nullptr) {
            desc.ConstraintType = constraint->Id();
        }

        // Reference type
        if (TracedType* target = typ->ReferenceTarget(); target != nullptr) {
            desc.InstantiatedType = target->Id();
        }
        if (auto args = typ->ReferenceTypeArguments(); !args.empty()) {
            desc.TypeArguments = mapTypeIds(args);
        }
        if (tsc::Node* node = typ->ReferenceNode(); node != nullptr) {
            desc.ReferenceLocation = getLocation(node);
        }

        // Reverse mapped type
        if (TracedType* sourceType = typ->ReverseMappedSourceType(); sourceType != nullptr) {
            desc.ReverseMappedSourceType = sourceType->Id();
        }
        if (TracedType* mappedType = typ->ReverseMappedMappedType(); mappedType != nullptr) {
            desc.ReverseMappedMappedType = mappedType->Id();
        }
        if (TracedType* constraintType = typ->ReverseMappedConstraintType(); constraintType != nullptr) {
            desc.ReverseMappedConstraintType = constraintType->Id();
        }

        // Evolving array type
        if (TracedType* elemType = typ->EvolvingArrayElementType(); elemType != nullptr) {
            desc.EvolvingArrayElementType = elemType->Id();
        }
        if (TracedType* finalType = typ->EvolvingArrayFinalType(); finalType != nullptr) {
            desc.EvolvingArrayFinalType = finalType->Id();
        }

        // Pattern (destructuring)
        if (tsc::Node* pattern = typ->Pattern(); pattern != nullptr) {
            desc.DestructuringPattern = getLocation(pattern);
        }

        // First declaration — prefer aliasSymbol, matching TypeScript's
        // `aliasSymbol ?? symbol`
        tsc::Symbol* firstDeclSymbol = aliasSymbol;
        if (firstDeclSymbol == nullptr) {
            firstDeclSymbol = symbol;
        }
        if (firstDeclSymbol != nullptr && !firstDeclSymbol->declarations.empty()) {
            desc.FirstDeclaration = getLocation(firstDeclSymbol->declarations[0]);
        }

        // Display text
        if (std::string display = typ->Display(); !display.empty()) {
            desc.Display = std::move(display);
        }

        return desc;
    }
};

// ---------------------------------------------------------------------------
// Tracing — tracing.go:113. The session implementation.
// ---------------------------------------------------------------------------

class TracingImpl final : public Tracing {
public:
    vfs::FS* fs = nullptr;
    std::string traceDir;
    std::string tracePath;
    std::string configFilePath;
    std::vector<TraceRecord> legend;
    std::vector<typeTracer*> tracers;
    std::string traceContent;
    std::atomic<bool> traceStarted{false};
    std::map<traceThreadKey, int> threadIDs;
    std::map<int, traceThreadKey> threadKeys;
    double metadataTS = 0;
    bool deterministic = false; // when true, use monotonic counter instead of real time
    uint64_t timestampCounter = 0; // only used in deterministic mode
    std::chrono::steady_clock::time_point startTime;
    std::mutex mu;
    // flushErr holds the first error encountered while appending the trace
    // buffer to disk. Once set, subsequent flushes become no-ops and the
    // error is surfaced from StopTracing so that transient I/O failures
    // don't crash the compiler.
    gostd::Error flushErr;

    // timestamp — tracing.go:193. Microseconds; monotonic counter in
    // deterministic mode.
    double timestamp() {
        if (deterministic) {
            return static_cast<double>(++timestampCounter);
        }
        return std::chrono::duration<double, std::micro>(
                   std::chrono::steady_clock::now() - startTime)
            .count();
    }

    void writeEvent(const traceEvent& event) {
        // json.MarshalWrite(&buf, event, json.Deterministic(true))
        std::ostringstream os;
        json::Encoder enc(os, json::Options{.deterministic = true});
        if (auto err = marshalJSONTo(enc, event); !err.empty()) {
            // tracing.go:203 — Go panics on marshal failure.
            TSC_UNREACHABLE(("failed to marshal trace event: " + err +
                             " (ph=" + event.PH + " name=" + event.Name + ")")
                                .c_str());
        }
        traceContent += os.str();
    }

    // maybeFlushLocked — tracing.go:215. Caller must hold mu.
    void maybeFlushLocked() {
        if (flushErr) {
            traceContent.clear();
            return;
        }
        if (traceContent.size() < flushThreshold) {
            return;
        }
        if (auto err = fs->AppendFile(tracePath, traceContent); err) {
            flushErr = gostd::newError("failed to flush trace file: " + err.str());
        }
        traceContent.clear();
    }

    // Instant — tracing.go:231.
    void Instant(Phase phase, const std::string& name,
                 const TraceArgs& args) override {
        if (!traceStarted.load()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mu);

        // Re-check under the lock: StopTracing may have run between the load
        // above and acquiring the lock. Once stopped, further writes would
        // land in a buffer that has already been flushed and the closing "]"
        // written.
        if (!traceStarted.load()) {
            return;
        }

        double ts = timestamp();
        int tid = threadIDLocked(args);
        traceContent += ",\n";
        writeEvent(traceEvent{1, tid, "I", std::string(phase), ts, name, "g",
                              std::nullopt, args});
        maybeFlushLocked();
    }

    // Push — tracing.go:267. Returns the pop function. `args` is the caller's
    // live map (shared_ptr): for separateBeginAndEnd events the "E" event
    // serializes it at pop time, so mutations made after Push appear — e.g.
    // relater.go:1346's `variances` arg.
    std::function<void()> Push(Phase phase, const std::string& name,
                               std::shared_ptr<TraceArgs> args,
                               bool separateBeginAndEnd) override {
        if (!traceStarted.load()) {
            return []() {};
        }

        if (separateBeginAndEnd) {
            int tid;
            {
                std::lock_guard<std::mutex> lock(mu);
                if (!traceStarted.load()) {
                    return []() {};
                }
                double ts = timestamp();
                tid = threadIDLocked(*args);
                traceContent += ",\n";
                writeEvent(traceEvent{1, tid, "B", std::string(phase), ts,
                                      name, {}, std::nullopt, *args});
                maybeFlushLocked();
            }

            return [this, tid, phase = std::move(phase), name = name,
                    args = std::move(args)]() {
                std::lock_guard<std::mutex> lock(mu);
                if (!traceStarted.load()) {
                    return;
                }
                double endTs = timestamp();
                traceContent += ",\n";
                writeEvent(traceEvent{1, tid, "E", std::string(phase), endTs,
                                      name, {}, std::nullopt, *args});
                maybeFlushLocked();
            };
        }

        // Sampled event: only record if duration crosses a sampling boundary.
        // In deterministic mode, sampled events are skipped entirely to avoid
        // flaky baselines, so avoid the cost of cloning args / capturing the
        // start time.
        if (deterministic) {
            return []() {};
        }
        auto start = std::chrono::steady_clock::now();
        // Go `maps.Clone(args)`: post-Push mutations do NOT reach the X event.
        TraceArgs clonedArgs = *args;
        return [this, start, phase = std::move(phase), name = name,
                args = std::move(clonedArgs)]() {
            double dur = std::chrono::duration<double, std::micro>(
                             std::chrono::steady_clock::now() - start)
                             .count();
            double startMicros = std::chrono::duration<double, std::micro>(
                                     start - startTime)
                                     .count();
            double intervalMicros =
                std::chrono::duration<double, std::micro>(sampleInterval)
                    .count();
            if (intervalMicros - std::fmod(startMicros, intervalMicros) > dur) {
                return;
            }
            std::lock_guard<std::mutex> lock(mu);
            if (!traceStarted.load()) {
                return;
            }
            int tid = threadIDLocked(args);
            traceContent += ",\n";
            writeEvent(traceEvent{1, tid, "X", std::string(phase), startMicros,
                                  name, {}, dur, args});
            maybeFlushLocked();
        };
    }

    // threadIDLocked — tracing.go:325. Caller must hold mu.
    int threadIDLocked(const TraceArgs& args) {
        auto [key, ok] = traceThreadKeyFromArgs(args);
        if (!ok) {
            return mainThreadID;
        }

        if (auto it = threadIDs.find(key); it != threadIDs.end()) {
            return it->second;
        }

        int tid = defaultThreadID(key);
        for (;;) {
            auto it = threadKeys.find(tid);
            if (it == threadKeys.end() || it->second == key) {
                break;
            }
            tid++;
        }
        threadIDs[key] = tid;
        threadKeys[tid] = key;
        writeThreadNameEventLocked(tid, displayName(key));
        return tid;
    }

    // writeThreadNameEventLocked — tracing.go:348.
    void writeThreadNameEventLocked(int tid, const std::string& name) {
        traceContent += ",\n";
        writeEvent(traceEvent{1, tid, "M", "__metadata", metadataTS,
                              "thread_name", {}, std::nullopt,
                              TraceArgs{{"name", name}}});
    }

    // NewTypeTracer — tracing.go:415.
    Tracer* NewTypeTracer(int checkerIndex) override {
        std::lock_guard<std::mutex> lock(mu);

        std::string typesPath = tspath::combinePaths(
            traceDir, {gostd::sprintf("types_%d.json", {checkerIndex})});
        auto* tracer = new typeTracer();
        tracer->fs = fs;
        tracer->checkerIndex = checkerIndex;
        tracer->typesPath = typesPath;
        tracers.push_back(tracer);
        legend.push_back(TraceRecord{configFilePath, tracePath, typesPath,
                                     checkerIndex});
        return tracer;
    }

    // stop — tracing.go:437 StopTracing.
    gostd::Error stop() {
        // Dump types from all tracers BEFORE acquiring the lock, because
        // DumpTypes → buildTypeDescriptor → Display() → TypeToString can
        // re-enter the checker which calls Push/Pop (which need mu).
        for (typeTracer* tracer : tracers) {
            tracer->DumpTypes();
            if (auto err = tracer->dumpErr(); err) {
                return gostd::errorf(
                    "failed to dump types for checker %d: %w",
                    {tracer->checkerIndex, err});
            }
        }

        {
            std::lock_guard<std::mutex> lock(mu);

            // Close the trace file(s)
            if (traceStarted.load()) {
                // Surface any buffered flush failure before attempting the
                // final write.
                if (flushErr) {
                    traceContent.clear();
                    traceStarted.store(false);
                    return flushErr;
                }
                // Flush any remaining buffered content and close the JSON
                // array.
                traceContent += "\n]\n";
                if (auto err = fs->AppendFile(tracePath, traceContent); err) {
                    return gostd::newError("failed to write trace file: " + err.str());
                }
                traceContent.clear();
                traceStarted.store(false);
            }
        }

        // Sort legend entries by typesPath for deterministic output
        std::sort(legend.begin(), legend.end(),
                  [](const TraceRecord& a, const TraceRecord& b) {
                      return a.TypesPath < b.TypesPath;
                  });

        // Write the legend file
        std::string legendPath =
            tspath::combinePaths(traceDir, {"legend.json"});
        std::ostringstream os;
        {
            json::Encoder enc(
                os, json::Options{.multiline = true, .indent = "  ",
                                  .indentPrefix = ""});
            if (auto err = json::detail::marshalInto(enc, legend); !err.empty()) {
                return gostd::errorf("failed to marshal legend file: %w",
                                     {gostd::newError(err)});
            }
        }
        if (auto err = fs->WriteFile(legendPath, os.str()); err) {
            return gostd::newError("failed to write legend file: " + err.str());
        }

        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// StartTracing / StopTracing — tracing.go:152 / execute/execute.go wrappers.
// ---------------------------------------------------------------------------

// StartTracing creates a new tracing session.
// When deterministic is true, timestamps use a monotonic counter instead of
// real wall-clock time, producing stable output for test baselines.
std::pair<Tracing*, gostd::Error> StartTracing(
    vfs::FS* fs, const std::string& traceDir,
    const std::string& configFilePath, bool deterministic) {
    auto* tr = new TracingImpl();
    tr->fs = fs;
    tr->traceDir = traceDir;
    tr->tracePath = tspath::combinePaths(traceDir, {traceFileName});
    tr->configFilePath = configFilePath;
    tr->deterministic = deterministic;
    tr->startTime = std::chrono::steady_clock::now();
    tr->traceStarted.store(true);

    // Write the trace file header with metadata events
    tr->traceContent += "[\n";

    // Write metadata events (matching TypeScript's format)
    double metaTs = tr->timestamp();
    tr->metadataTS = metaTs;
    tr->writeEvent(traceEvent{1, mainThreadID, "M", "__metadata", metaTs,
                              "process_name", {}, std::nullopt,
                              TraceArgs{{"name", std::string("tsgo")}}});
    tr->traceContent += ",\n";
    tr->writeEvent(traceEvent{1, mainThreadID, "M", "__metadata", metaTs,
                              "thread_name", {}, std::nullopt,
                              TraceArgs{{"name", std::string("Main")}}});
    tr->traceContent += ",\n";
    tr->writeEvent(traceEvent{1, mainThreadID, "M",
                              "disabled-by-default-devtools.timeline", metaTs,
                              "TracingStartedInBrowser", {}, std::nullopt,
                              {}});

    // Truncate any existing trace file with the header so subsequent
    // AppendFile calls extend a clean file.
    if (auto err = tr->fs->WriteFile(tr->tracePath, tr->traceContent); err) {
        return {nullptr,
                gostd::newError("failed to write trace file header: " + err.str())};
    }
    tr->traceContent.clear();

    return {tr, nullptr};
}

// StopTracing — tracing.go:437.
gostd::Error StopTracing(Tracing* tr) {
    if (tr == nullptr) {
        return nullptr;
    }
    return static_cast<TracingImpl*>(tr)->stop();
}

} // namespace tsc::tracing
