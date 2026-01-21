#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>
#include "lexer.h"
#include "assembler.h"
#include "source_manager.h"
#include "value.h"

struct Diagnostic { std::string msg; SourceLocation loc; std::string func; };

struct CompilerOptions {
    // 0 = no optimizations, 1 = basic, 2 = aggressive, higher = iterative
    int opt_level = 2;
    // maximum number of optimization iterations when iterating to fixpoint
    int max_opt_iters = 8;
};

struct FunctionSig {
    std::string name;
    std::string internal_name;
    std::vector<TypeKind> param_types;
    TypeKind return_type = TY_VOID;
    int user_return_type_id = -1;
    int label_id = -1;
    int declared_line = 0;
    bool is_builtin = false;
};

struct LocalEntry { std::string name; int depth; int slot; TypeKind type; int user_type_id; };
class Parser;

class RegAllocator {
    int next_reg_ = 0;
    std::vector<int> free_regs_;
public:
    int alloc() {
        if (!free_regs_.empty()) { int r = free_regs_.back(); free_regs_.pop_back(); return r; }
        return next_reg_++;
    }
    void free(int r) { if (r >= 0) free_regs_.push_back(r); }
    void reset() { next_reg_ = 0; free_regs_.clear(); }
};

class Compiler {
public:
    Assembler asm_;
    Compiler(const std::string& source, const CompilerOptions& opts = {});
    ~Compiler();

    void compile_unit(SourceManager* sm);

    void push_diag(const std::string &m, SourceLocation loc, const std::string &fn = "");
    int resolve_local(const std::string& name);

    int define_local(const std::string& name, TypeKind t = TY_UNKNOWN, int user_type_id = -1);

    int emit_const(Value v, int line);
    void begin_scope();
    void end_scope();
    std::string type_kind_to_string(TypeKind t);

    // options
    CompilerOptions options;

private:
    friend class Parser;
    Parser* parser_ = nullptr;

    std::string source_text_;
    std::vector<LocalEntry> locals_;
    int scope_depth_ = 0;
    std::map<std::string, std::vector<FunctionSig>> function_table_;
    std::vector<Diagnostic> diagnostics_;
    std::string current_function_;
    TypeKind expected_return_ = TY_UNKNOWN;

    struct ItemType { int id; std::string name; int parent_id; std::vector<std::pair<std::string, TypeKind>> fields; };
    std::vector<ItemType> item_types_;
    std::map<std::string,int> item_name_to_id_;

    FunctionSig* resolve_function(const std::string &name, const std::vector<TypeKind> &arg_types);

    int register_item_type(const std::string &name, const std::string &parent_name, const std::vector<std::pair<std::string, TypeKind>>& fields);
    int find_item_id_by_name(const std::string &name) const;
    const std::vector<std::pair<std::string, TypeKind>>& get_item_fields(int id) const;

    RegAllocator regalloc_;
    std::string mangle_name(const std::string &name, const std::vector<TypeKind>& types);
    void register_builtin_signatures();
};
