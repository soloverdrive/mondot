#include "compiler.h"
#include "parser.h"
#include "source_manager.h"

#include "builtin_registry.h"
#include "value.h"
#include <sstream>

Compiler::Compiler(const std::string& source, const CompilerOptions& opts) : source_text_(source), options(opts) {
    register_builtin_signatures();
    parser_ = new Parser(this, source_text_);
}

Compiler::~Compiler() { delete parser_; }

void Compiler::register_builtin_signatures() {
    for (const auto &be : BuiltinRegistry::all_entries()) {
        FunctionSig fs;
        fs.name = be.name;
        fs.param_types = be.param_types;
        fs.return_type = be.return_type;
        fs.is_builtin = true;
        fs.label_id = -1;
        fs.internal_name = mangle_name(fs.name, fs.param_types);
        function_table_[fs.name].push_back(fs);
    }
}

std::string Compiler::mangle_name(const std::string &name, const std::vector<TypeKind>& types) {
    std::ostringstream ss;
    ss << name << "#" << types.size();
    for (auto t : types) ss << "." << (int)t;
    return ss.str();
}

void Compiler::compile_unit(SourceManager* sm) {
    parser_->compile_unit(sm);
    if (options.opt_level > 0)
        asm_.run_optimizations(options.opt_level, options.max_opt_iters);
}

void Compiler::push_diag(const std::string &m, SourceLocation loc, const std::string &fn) {
    diagnostics_.push_back({m, loc, fn});
}

int Compiler::resolve_local(const std::string& name) {
    for (int i = (int)locals_.size() - 1; i >= 0; --i) if (locals_[i].name == name) return locals_[i].slot;
    return -1;
}
int Compiler::define_local(const std::string& name, TypeKind t, int user_type_id) {
    int slot = (int)locals_.size();
    locals_.push_back({name, scope_depth_, slot, t, user_type_id});
    return slot;
}

int Compiler::emit_const(Value v, int line) {
    int const_idx = asm_.add_constant(v);
    int reg = define_local("", TY_UNKNOWN);
    asm_.emit(OP_CONST, line, reg, const_idx, 0);
    return reg;
}

void Compiler::begin_scope() { scope_depth_++; }
void Compiler::end_scope() {
    scope_depth_--;
    while (!locals_.empty() && locals_.back().depth > scope_depth_) locals_.pop_back();
}

std::string Compiler::type_kind_to_string(TypeKind t) {
    switch (t) {
        case TY_NUMBER: return "number";
        case TY_STRING: return "string";
        case TY_BOOL:   return "bool";
        case TY_VOID:   return "void";
        case TY_LIST:   return "list";
        case TY_ITEM:   return "item";
        case TY_TABLE:  return "table";
        case TY_UNKNOWN: return "unknown";
        default: return "unknown";
    }
}

int Compiler::register_item_type(const std::string &name, const std::string &parent_name, const std::vector<std::pair<std::string, TypeKind>>& fields) {
    static int next_id = 0;
    auto itdup = item_name_to_id_.find(name);
    if (itdup != item_name_to_id_.end()) {
        push_diag(std::string("Duplicate item type: ") + name, {0,0,0}, "");
        return itdup->second;
    }

    int parent_id = -1;
    if (!parent_name.empty()) {
        auto it = item_name_to_id_.find(parent_name);
        if (it != item_name_to_id_.end()) parent_id = it->second;
        else {
            push_diag(std::string("Unknown parent item type: ") + parent_name, {0,0,0}, "");
            parent_id = -1;
        }
    }

    int id = next_id++;
    ItemType itp;
    itp.id = id;
    itp.name = name;
    itp.parent_id = parent_id;

    if (parent_id >= 0 && parent_id < (int)item_types_.size()) {
        const auto &parent_fields = item_types_[parent_id].fields;
        itp.fields.insert(itp.fields.end(), parent_fields.begin(), parent_fields.end());
    }

    itp.fields.insert(itp.fields.end(), fields.begin(), fields.end());

    item_types_.push_back(itp);
    item_name_to_id_[name] = id;

    FunctionSig fs;
    fs.name = "create";
    fs.return_type = TY_ITEM;
    fs.user_return_type_id = id;
    fs.label_id = -1;
    fs.is_builtin = false;
    for (auto &f : itp.fields) fs.param_types.push_back(f.second);
    fs.internal_name = mangle_name(fs.name, fs.param_types);
    function_table_[fs.name].push_back(fs);

    return id;
}

int Compiler::find_item_id_by_name(const std::string &name) const {
    auto it = item_name_to_id_.find(name);
    if (it == item_name_to_id_.end()) return -1;
    return it->second;
}

const std::vector<std::pair<std::string, TypeKind>>& Compiler::get_item_fields(int id) const {
    static std::vector<std::pair<std::string, TypeKind>> empty;
    if (id < 0 || id >= (int)item_types_.size()) return empty;
    return item_types_[id].fields;
}

FunctionSig* Compiler::resolve_function(const std::string &name, const std::vector<TypeKind> &arg_types) {
    auto it = function_table_.find(name);
    if (it == function_table_.end()) return nullptr;
    FunctionSig* best = nullptr;
    for (auto &fs : it->second) {
        if ((int)fs.param_types.size() != (int)arg_types.size()) continue;
        bool ok = true;
        for (int i = 0; i < (int)arg_types.size(); ++i) {
            if (arg_types[i] == TY_UNKNOWN) continue;
            if (fs.param_types[i] != TY_UNKNOWN && fs.param_types[i] != arg_types[i]) { ok = false; break; }
        }
        if (!ok) continue;
        if (expected_return_ != TY_UNKNOWN && fs.return_type == expected_return_) return &fs;
        if (!best) best = &fs;
    }
    if (best) return best;
    // return any overload with same arity as fallback if none matched by type
    for (auto &fs : it->second) {
        if ((int)fs.param_types.size() == (int)arg_types.size()) return &fs;
    }
    return nullptr;
}
