#include "compiler.h"
#include "parser.h"
#include "bytecode_io.h"
#include "vm.h"
#include "source_manager.h"
#include <iostream>

#include "builtin_registry.h"

Compiler::Compiler(const std::string& source) : source_text_(source) {
    for (const auto &be : BuiltinRegistry::all_entries()) {
        FunctionSig fs;
        fs.name = be.name;
        fs.param_types = be.param_types;
        fs.return_type = be.return_type;
        fs.is_builtin = true;
        fs.label_id = -1;
        function_table_[fs.name].push_back(fs);
    }
    parser_ = new Parser(this, source_text_);
}

Compiler::~Compiler() { delete parser_; }

void Compiler::compile_unit(SourceManager* sm) {
    parser_->compile_unit(sm);
}

void Compiler::push_diag(const std::string &m, SourceLocation loc, const std::string &fn) {
    diagnostics_.push_back({m, loc, fn});
}

int Compiler::resolve_local(const std::string& name) {
    for (int i = (int)locals_.size() - 1; i >= 0; --i) if (locals_[i].name == name) return locals_[i].slot;
    return -1;
}
int Compiler::define_local(const std::string& name, TypeKind t) {
    int slot = (int)locals_.size();
    locals_.push_back({name, scope_depth_, slot, t});
    return slot;
}

int Compiler::load_const(Value v, int line) {
    int idx = asm_.add_constant(v);
    int reg = define_local("", type_of_value(v));
    asm_.emit(OP_CONST, line, reg, idx);
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
        case TY_UNKNOWN: return "unknown";
        default: return "unknown";
    }
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
