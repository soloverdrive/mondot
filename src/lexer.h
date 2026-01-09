#pragma once
#include <string>

enum class TK {
    TK_BAD, END_FILE, IDENT, NUMBER, STRING, BOOL, NIL, UNIT, ON, IF, ELSE, WHILE,
    KEY_END, VAR, PLUS, MINUS, MUL, DIV, ASSIGN, EQ, LT, GT, LP, RP,
    LBRACE, RBRACE, COMMA, DOT, COLON, AS, LBRACK, RBRACK, RETURN
};

struct Token { TK k; std::string lex; int line; int col; };

struct Lexer {
    std::string src;
    size_t i = 0;
    int line = 1;
    int col = 1;
    Lexer(const std::string& s): src(s) {}
    char peek(int offset = 0) const { return (i + offset < src.size()) ? src[i + offset] : '\0'; }
    char advance();
    bool match(char c);
    Token next();
};
