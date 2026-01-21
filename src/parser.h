#pragma once

#include <string>
#include <vector>
#include <functional>
#include "lexer.h"
#include "source_manager.h"
#include "value.h"
#include "compiler.h"

struct ScopeGuard {
    Compiler* c;
    explicit ScopeGuard(Compiler* c): c(c) { if (c) c->begin_scope(); }
    ~ScopeGuard() { if (c) c->end_scope(); }
};

struct ExprResult {
    int reg = -1;
    TypeKind type = TY_UNKNOWN;
    bool is_const = false;
    Value const_value;

    ExprResult() = default;
    static ExprResult make_const(const Value &v, TypeKind t) {
        ExprResult r; r.is_const = true; r.const_value = v; r.type = t; r.reg = -1; return r;
    }
    static ExprResult make_reg(int reg, TypeKind t) {
        ExprResult r; r.is_const = false; r.reg = reg; r.type = t; return r;
    }
};

class Parser {
public:
    Parser(Compiler* owner, const std::string& src);
    ~Parser();

    // main entry
    void compile_unit(SourceManager* sm);

private:
    Compiler* owner_ = nullptr;
    std::vector<Token> tokens_;
    int tokpos_ = 0;
    Token curr_;
    Token next_;
    std::string src_text_;

    // tokenize
    void tokenize_all(const std::string& src);
    Token peek_token(int lookahead = 0) const;
    void advance();
    void consume(TK k, const std::string& msg);

    // parsing & codegen
    void prescan_functions();

    // public API
    ExprResult compile_expr(int min_prec = 0);

    // internal helpers
    ExprResult compile_expr_internal(int min_prec = 0);
    ExprResult compile_atom_internal();

    std::pair<TypeKind,int> resolve_type_name(const std::string &s);
    void compile_stmt();

    // helpers
    int ensure_reg(ExprResult &er, int line);
    int make_string_const(const std::string &s, int line);
    int make_nil_const(int line);
    int emit_call_helper(int line, FunctionSig* fs, const std::vector<int>& arg_regs);
    template<typename F>
    void parse_delimited(TK open, TK close, F element_cb);
    bool parse_param_pair(std::string &out_name, TypeKind &out_type);
};
