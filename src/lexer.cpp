#include "lexer.h"
#include <cctype>

char Lexer::advance() {
    char c = peek();
    if (c == '\0') return c;
    i++;
    if (c == '\n') { line++; col = 1; } else col++;
    return c;
}
bool Lexer::match(char c) {
    if (peek() == c) { advance(); return true; }
    return false;
}

Token Lexer::next() {
    while (std::isspace((unsigned char)peek())) advance();
    int start_col = col;
    int start_line = line;
    char c = peek();
    if (c == '\0') return {TK::END_FILE, "", start_line, start_col};
    if (std::isalpha((unsigned char)c) || c == '_') {
        std::string s;
        while (std::isalnum((unsigned char)peek()) || peek() == '_') s += advance();
        if (s == "unit") return {TK::UNIT, s, start_line, start_col};
        if (s == "on") return {TK::ON, s, start_line, start_col};
        if (s == "if") return {TK::IF, s, start_line, start_col};
        if (s == "else") return {TK::ELSE, s, start_line, start_col};
        if (s == "while") return {TK::WHILE, s, start_line, start_col};
        if (s == "end") return {TK::KEY_END, s, start_line, start_col};
        if (s == "var") return {TK::VAR, s, start_line, start_col};
        if (s == "true" || s == "false") return {TK::BOOL, s, start_line, start_col};
        if (s == "nil") return {TK::NIL, s, start_line, start_col};
        if (s == "as") return {TK::AS, s, start_line, start_col};
        if (s == "return") return {TK::RETURN, s, start_line, start_col};
        return {TK::IDENT, s, start_line, start_col};
    }
    if (std::isdigit((unsigned char)c)) {
        std::string s;
        while (std::isdigit((unsigned char)peek())) s += advance();
        if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
            s += advance();
            while (std::isdigit((unsigned char)peek())) s += advance();
        }
        return {TK::NUMBER, s, start_line, start_col};
    }
    if (c == '"') {
        advance();
        std::string s;
        while (peek() != '"' && peek() != '\0') {
            char ch = advance();
            if (ch == '\\' && peek() != '\0') {
                char nx = advance();
                if (nx == 'n') s.push_back('\n');
                else if (nx == 't') s.push_back('\t');
                else s.push_back(nx);
            } else s.push_back(ch);
        }
        if (peek() == '"') advance();
        return {TK::STRING, s, start_line, start_col};
    }
    advance();
    switch (c) {
        case '+': return {TK::PLUS, "+", start_line, start_col};
        case '-': return {TK::MINUS, "-", start_line, start_col};
        case '*': return {TK::MUL, "*", start_line, start_col};
        case '/': return {TK::DIV, "/", start_line, start_col};
        case '=': if (match('=')) return {TK::EQ, "==", start_line, start_col}; return {TK::ASSIGN, "=", start_line, start_col};
        case '<': return {TK::LT, "<", start_line, start_col};
        case '>': return {TK::GT, ">", start_line, start_col};
        case '(': return {TK::LP, "(", start_line, start_col};
        case ')': return {TK::RP, ")", start_line, start_col};
        case '{': return {TK::LBRACE, "{", start_line, start_col};
        case '}': return {TK::RBRACE, "}", start_line, start_col};
        case '[': return {TK::LBRACK, "[", start_line, start_col};
        case ']': return {TK::RBRACK, "]", start_line, start_col};
        case ',': return {TK::COMMA, ",", start_line, start_col};
        case '.': return {TK::DOT, ".", start_line, start_col};
        case ':': return {TK::COLON, ":", start_line, start_col};
        default: {
            std::string s(1, c);
            return {TK::TK_BAD, s, start_line, start_col};
        }
    }
}
