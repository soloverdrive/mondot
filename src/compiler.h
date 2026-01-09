#pragma once
#include <string>
#include <vector>
#include <map>
#include "lexer.h"
#include "assembler.h"
#include "source_manager.h"
#include "value.h"

struct Diagnostic { std::string msg; SourceLocation loc; std::string func; };

struct FunctionSig {
    std::string name;
    std::vector<TypeKind> param_types;
    TypeKind return_type = TY_VOID;
    int label_id = -1;
    int declared_line = 0;
    bool is_builtin = false;
};

struct LocalEntry { std::string name; int depth; int slot; TypeKind type; };
class Parser;

class Compiler {
public:
    Assembler asm_;
    Compiler(const std::string& source);
    ~Compiler();

    void compile_unit(SourceManager* sm);

    void push_diag(const std::string &m, SourceLocation loc, const std::string &fn = "");
    int resolve_local(const std::string& name);
    int define_local(const std::string& name, TypeKind t = TY_UNKNOWN);
    int load_const(Value v, int line);
    void begin_scope();
    void end_scope();
    std::string type_kind_to_string(TypeKind t);

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

    FunctionSig* resolve_function(const std::string &name, const std::vector<TypeKind> &arg_types);
};
