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
    std::pair<int, TypeKind> compile_expr(int min_prec = 0);
    std::pair<int, TypeKind> compile_atom();
    void compile_stmt();

    // helpers
    int make_string_const(const std::string &s, int line);
    int make_nil_const(int line);
    int emit_call_helper(int line, FunctionSig* fs, const std::vector<int>& arg_regs);
    template<typename F>
    void parse_delimited(TK open, TK close, F element_cb);
    bool parse_param_pair(std::string &out_name, TypeKind &out_type);
};
