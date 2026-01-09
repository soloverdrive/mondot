#include "parser.h"
#include "compiler.h"
#include <sstream>
#include <iostream>
#include <cctype>
#include <stdexcept>
#include "value.h"
#include <cmath>
#include "builtin_registry.h"

static int64_t parse_number_intscaled_from_lex(const std::string &lex) {
    size_t pos = lex.find('.');
    int64_t intpart = 0;
    if (pos == std::string::npos) {
        try { intpart = stoll(lex); } catch(...) { intpart = 0; }
        return int64_t(intpart) << INTSCALED_SHIFT;
    }
    std::string s_int = lex.substr(0, pos);
    std::string s_frac = lex.substr(pos+1);
    if (!s_int.empty()) {
        try { intpart = stoll(s_int); } catch(...) { intpart = 0; }
    }
    if (!s_frac.empty()) {
        if ((int)s_frac.size() > 9) s_frac = s_frac.substr(0,9);
        int frac_len = (int)s_frac.size();
        uint64_t frac_int = 0;
        try { frac_int = stoull(s_frac); } catch(...) { frac_int = 0; }
        uint64_t pow10 = 1;
        for (int i=0;i<frac_len;i++) pow10 *= 10ULL;
        long double tmp = (long double)frac_int * (long double)(1ULL<<INTSCALED_SHIFT) / (long double)pow10;
        uint64_t frac_q = (uint64_t) llround(tmp);
        int64_t q = (int64_t(intpart) << INTSCALED_SHIFT) + (int64_t)frac_q;
        return q;
    }
    return int64_t(intpart) << INTSCALED_SHIFT;
}

void Parser::tokenize_all(const std::string& src) {
    tokens_.clear();
    Lexer lx(src);
    while (true) {
        Token t = lx.next();
        tokens_.push_back(t);
        if (t.k == TK::END_FILE) break;
    }
}

Token Parser::peek_token(int lookahead) const {
    int pos = tokpos_ + lookahead;
    if (pos < 0) pos = 0;
    if (pos >= (int)tokens_.size()) return tokens_.back();
    return tokens_[pos];
}

void Parser::advance() {
    tokpos_++;
    curr_ = (tokpos_ < (int)tokens_.size()) ? tokens_[tokpos_] : tokens_.back();
    next_ = (tokpos_+1 < (int)tokens_.size()) ? tokens_[tokpos_+1] : tokens_.back();
}

void Parser::consume(TK k, const std::string& msg) {
    if (curr_.k == k) { advance(); return; }

    owner_->push_diag(msg.empty() ? std::string("Expected token not found") : msg,
                      {curr_.line, curr_.col, (int)curr_.lex.size()},
                      owner_->current_function_);

    int safety = 0;
    while (curr_.k != k && curr_.k != TK::END_FILE && curr_.k != TK::RBRACE && curr_.k != TK::KEY_END && curr_.k != TK::TK_BAD && safety < 2000) {
        advance();
        safety++;
    }
    if (curr_.k == k) advance();
}

Parser::Parser(Compiler* owner, const std::string& src) : owner_(owner), src_text_(src) {
    tokenize_all(src_text_);
    tokpos_ = 0;
    curr_ = tokens_.size()>0?tokens_[0]:Token{TK::END_FILE,"",0,0};
    next_ = tokens_.size()>1?tokens_[1]:Token{TK::END_FILE,"",0,0};
}

Parser::~Parser() {}

void Parser::prescan_functions() {
    for (size_t i = 0; i + 2 < tokens_.size(); ++i) {
        if (tokens_[i].k == TK::ON) {
            Token rett = tokens_[i+1];
            Token name = tokens_[i+2];
            if (rett.k == TK::IDENT && name.k == TK::IDENT) {
                FunctionSig fs;
                fs.name = name.lex;
                fs.return_type = parse_type_name(rett.lex);
                fs.declared_line = name.line;
                fs.label_id = owner_->asm_.make_label();
                owner_->function_table_[fs.name].push_back(fs);
            }
        }
    }
}

std::pair<int, TypeKind> Parser::compile_expr(int min_prec) {
    auto left_pair = compile_atom();
    int left = left_pair.first; TypeKind left_t = left_pair.second;
    while (true) {
        TK op = curr_.k;
        int prec = 0; OpCode opcode = OP_ADD;
        if (op == TK::MUL || op == TK::DIV) { prec = 3; opcode = (op==TK::MUL)?OP_MUL:OP_DIV; }
        else if (op == TK::PLUS || op == TK::MINUS) { prec = 2; opcode = (op==TK::PLUS)?OP_ADD:OP_SUB; }
        else if (op == TK::LT || op == TK::GT || op == TK::EQ) { prec = 1; if (op==TK::LT) opcode=OP_LT; else if (op==TK::GT) opcode=OP_GT; else opcode=OP_EQ; }
        else break;
        if (prec < min_prec) break;
        advance();
        auto right_pair = compile_expr(prec + 1);
        int right = right_pair.first; TypeKind right_t = right_pair.second;
        TypeKind result_t = TY_UNKNOWN;
        if (opcode==OP_ADD || opcode==OP_SUB || opcode==OP_MUL || opcode==OP_DIV) {
            if (left_t != TY_NUMBER || right_t != TY_NUMBER)
                owner_->push_diag("Arithmetic operation applied to non-number types", {curr_.line, curr_.col, 1}, owner_->current_function_);
            result_t = TY_NUMBER;
        } else if (opcode==OP_LT || opcode==OP_GT || opcode==OP_EQ) result_t = TY_BOOL;
        owner_->asm_.emit(opcode, curr_.line, left, left, right);
        left_t = result_t;
    }
    return {left, left_t};
}

std::pair<int, TypeKind> Parser::compile_atom() {
    int line = curr_.line;

    if (curr_.k == TK::TK_BAD) {
        owner_->push_diag(std::string("Unknown token: '") + curr_.lex + std::string("'"),
                          {curr_.line, curr_.col, (int)curr_.lex.size()}, owner_->current_function_);
        advance();
        int r = owner_->define_local("", TY_UNKNOWN);
        owner_->asm_.emit(OP_CONST, line, r, owner_->asm_.add_constant(Value::make_nil()));
        return {r, TY_UNKNOWN};
    }

    if (curr_.k == TK::NUMBER) {
        int64_t q = parse_number_intscaled_from_lex(curr_.lex);
        advance();
        int r = owner_->load_const(Value::make_intscaled(q), line);
        return {r, TY_NUMBER};
    }

    if (curr_.k == TK::STRING) {
        ObjString* s = new ObjString(curr_.lex); advance();
        int r = owner_->load_const(Value::make_obj(s), line);
        return {r, TY_STRING};
    }
    if (curr_.k == TK::BOOL) {
        bool b = (curr_.lex == "true"); advance();
        int r = owner_->load_const(Value::make_bool(b), line);
        return {r, TY_BOOL};
    }
    if (curr_.k == TK::NIL) {
        advance();
        int r = owner_->load_const(Value::make_nil(), line);
        return {r, TY_UNKNOWN};
    }

    // array literal: [ expr, expr, ... ]  -> runtime construction via OP_TABLE_NEW/SET
    if (curr_.k == TK::LBRACK) {
        advance();
        int dest = owner_->define_local("", TY_ARRAY);
        owner_->asm_.emit(OP_TABLE_NEW, line, dest);
        int elem_index = 0;
        if (curr_.k != TK::RBRACK) {
            while (true) {
                auto p = compile_expr();
                int keyreg = owner_->load_const(Value::make_int(elem_index), line);
                owner_->asm_.emit(OP_TABLE_SET, line, dest, keyreg, p.first);
                elem_index++;
                if (curr_.k == TK::COMMA) { advance(); continue; }
                break;
            }
        }
        if (curr_.k == TK::RBRACK) advance();
        else consume(TK::RBRACK, "Expected ']'");
        return {dest, TY_ARRAY};
    }

    // Table literal: { val, key: val, ... } -> runtime construction
    if (curr_.k == TK::LBRACE) {
        advance();
        int dest = owner_->define_local("", TY_TABLE);
        owner_->asm_.emit(OP_TABLE_NEW, line, dest);
        int next_index = 0;
        while (curr_.k != TK::RBRACE && curr_.k != TK::END_FILE) {
            if (curr_.k == TK::IDENT && peek_token(1).k == TK::COLON) {
                std::string key = curr_.lex;
                advance();
                advance();
                auto val = compile_expr();
                ObjString* s = new ObjString(key);
                int keyreg = owner_->load_const(Value::make_obj(s), line);
                owner_->asm_.emit(OP_TABLE_SET, line, dest, keyreg, val.first);
            } else {
                auto val = compile_expr();
                int keyreg = owner_->load_const(Value::make_int(next_index), line);
                owner_->asm_.emit(OP_TABLE_SET, line, dest, keyreg, val.first);
                next_index++;
            }
            if (curr_.k == TK::COMMA) { advance(); continue; }
            else break;
        }
        if (curr_.k == TK::RBRACE) advance();
        else consume(TK::RBRACE, "Expected '}' for table literal");
        return {dest, TY_TABLE};
    }

    if (curr_.k == TK::IDENT) {
        std::string name = curr_.lex; advance();

        // function call: ident '(' ...
        if (curr_.k == TK::LP) {
            advance();
            std::vector<int> arg_regs;
            std::vector<TypeKind> arg_types;
            if (curr_.k != TK::RP) {
                while (true) {
                    auto p = compile_expr();
                    arg_regs.push_back(p.first);
                    arg_types.push_back(p.second);
                    if (curr_.k == TK::COMMA) { advance(); continue; }
                    break;
                }
            }
            consume(TK::RP, "Expected ')'");
            FunctionSig* fs = owner_->resolve_function(name, arg_types);
            if (!fs) {
                std::string hint = "Unknown function or invalid overload: " + name;
                auto it = owner_->function_table_.find(name);
                if (it != owner_->function_table_.end()) {
                    hint += ". Available overloads: ";
                    bool first = true;
                    for (auto &ofs : it->second) {
                        if (!first) hint += " | ";
                        first = false;
                        hint += ofs.name + "(";
                        for (size_t i = 0; i < ofs.param_types.size(); ++i) {
                            if (i) hint += ", ";
                            hint += owner_->type_kind_to_string(ofs.param_types[i]);
                        }
                        hint += ")";
                    }
                }
                owner_->push_diag(hint, {line, 1, (int)name.size()}, owner_->current_function_);
                int r = owner_->load_const(Value::make_nil(), line);
                return {r, TY_UNKNOWN};
            }
            if (fs->is_builtin) {
                // Resolve builtin id quando possível (fallback -1)
                int bid = BuiltinRegistry::lookup_name(fs->name);
                // Cria um ObjFunction como constante — evita emitir OP_CALL com label -1
                ObjFunction* of = new ObjFunction(bid, fs->return_type, fs->param_types, fs->name);
                int func_reg = owner_->load_const(Value::make_obj(of), line);

                // preparar destino e argumentos (mesma lógica do emit_call_helper)
                int dest = owner_->define_local("", fs->return_type);
                std::vector<int> call_arg_slots;
                call_arg_slots.reserve(arg_regs.size());
                for (size_t i = 0; i < arg_regs.size(); ++i) {
                    TypeKind pk = TY_UNKNOWN;
                    if (i < fs->param_types.size()) pk = fs->param_types[i];
                    int argslot = owner_->define_local("", pk);
                    call_arg_slots.push_back(argslot);
                }
                for (size_t i = 0; i < arg_regs.size(); ++i)
                    owner_->asm_.emit(OP_MOVE, line, call_arg_slots[i], arg_regs[i]);

                // emitir OP_CALL_OBJ: a = dest_rel, b = func_reg (relative), c = argc
                owner_->asm_.emit(OP_CALL_OBJ, line, dest, func_reg, (int)arg_regs.size());
                return {dest, fs->return_type};
            }

            int dest = emit_call_helper(line, fs, arg_regs);
            return {dest, fs->return_type};
        }

        int loc = owner_->resolve_local(name);
        if (loc == -1) {
            owner_->push_diag("Undefined variable: " + name, {line, curr_.col, (int)name.size()}, owner_->current_function_);
            int r = owner_->define_local("", TY_UNKNOWN);
            owner_->asm_.emit(OP_CONST, line, r, owner_->asm_.add_constant(Value::make_nil()));

            while (curr_.k == TK::DOT || curr_.k == TK::LBRACK) {
                if (curr_.k == TK::DOT) {
                    advance();
                    if (curr_.k == TK::IDENT) {
                        advance();
                        if (curr_.k == TK::LP) {
                            advance();
                            int safety = 0;
                            while (curr_.k != TK::RP && curr_.k != TK::END_FILE && safety++ < 2000) {
                                compile_expr();
                                if (curr_.k == TK::COMMA) { advance(); continue; }
                                else break;
                            }
                            if (curr_.k == TK::RP) advance();
                        }
                    } else if (curr_.k == TK::NUMBER)
                        advance();
                    else
                        break;
                } else {
                    advance();
                    int safety = 0;
                    while (curr_.k != TK::RBRACK && curr_.k != TK::END_FILE && safety++ < 2000) {
                        compile_expr();
                        if (curr_.k == TK::COMMA) { advance(); continue; }
                        else break;
                    }
                    if (curr_.k == TK::RBRACK) advance();
                }
            }

            return {r, TY_UNKNOWN};
        }

        int tmp = owner_->define_local("", owner_->locals_[loc].type);
        owner_->asm_.emit(OP_MOVE, line, tmp, loc);

        // member/index access loop
        while (true) {
            if (curr_.k == TK::DOT) {
                advance();
                if (curr_.k == TK::IDENT) {
                    std::string member = curr_.lex;
                    advance();
                    ObjString* s = new ObjString(member);
                    int keyreg = owner_->load_const(Value::make_obj(s), line);
                    int dest = owner_->define_local("", TY_UNKNOWN);
                    owner_->asm_.emit(OP_INDEX, line, dest, tmp, keyreg);
                    tmp = dest;
                    continue;
                } else if (curr_.k == TK::NUMBER) {
                    long long idxval = 0;
                    try { idxval = stoll(curr_.lex); } catch(...) { idxval = 0; }
                    advance();
                    int idxreg = owner_->load_const(Value::make_int(idxval - 1), line);
                    int dest = owner_->define_local("", TY_UNKNOWN);
                    owner_->asm_.emit(OP_INDEX, line, dest, tmp, idxreg);
                    tmp = dest;
                    continue;
                } else {
                    owner_->push_diag("Unexpected token after '.'", {curr_.line, curr_.col, (int)curr_.lex.size()}, owner_->current_function_);
                    break;
                }
            } else if (curr_.k == TK::LBRACK) {
                advance();
                auto p = compile_expr();
                consume(TK::RBRACK, "Expected ']'");
                int negone = owner_->load_const(Value::make_int(-1), line);
                owner_->asm_.emit(OP_ADD, line, p.first, p.first, negone);
                int dest = owner_->define_local("", TY_UNKNOWN);
                owner_->asm_.emit(OP_INDEX, line, dest, tmp, p.first);
                tmp = dest;
                continue;
            } else break;
        }

        return {tmp, owner_->locals_[tmp].type};
    }

    if (curr_.k == TK::LP) {
        advance();
        auto p = compile_expr();
        consume(TK::RP, "Expected ')'");
        return p;
    }

    owner_->push_diag("Invalid expression", {curr_.line, curr_.col, (int)curr_.lex.size()}, owner_->current_function_);
    int r = owner_->define_local("", TY_UNKNOWN);
    owner_->asm_.emit(OP_CONST, line, r, owner_->asm_.add_constant(Value::make_nil()));
    if (curr_.k != TK::END_FILE) advance();
    return {r, TY_UNKNOWN};
}

void Parser::compile_unit(SourceManager* sm) {
    prescan_functions();

    int entry_label = owner_->asm_.make_label();
    owner_->asm_.emit_jump(OP_JMP, 0, 0, entry_label);

    if (curr_.k != TK::UNIT) {
        owner_->push_diag("Expected 'unit' at the beginning", {curr_.line, curr_.col, (int)curr_.lex.size()}, "");
        return;
    }
    advance();
    if (curr_.k != TK::IDENT) {
        owner_->push_diag("Expected unit name", {curr_.line, curr_.col, (int)curr_.lex.size()}, "");
        return;
    }
    std::string unit_name = curr_.lex;
    advance();

    if (curr_.k == TK::COLON) {
        advance();
        while (curr_.k == TK::IDENT) {
            advance();
            if (curr_.k == TK::AS) { advance(); if (curr_.k == TK::IDENT) advance(); }
            if (curr_.k == TK::COMMA) { advance(); continue; }
            break;
        }
    }

    consume(TK::LBRACE, "Expected '{' token after unit header");

    while (curr_.k != TK::RBRACE && curr_.k != TK::END_FILE) {
        if (curr_.k == TK::ON) {
            advance();
            if (curr_.k != TK::IDENT) {
                owner_->push_diag("Expected return type after 'on'", {curr_.line, curr_.col, (int)curr_.lex.size()}, "");
                break;
            }
            TypeKind rett = parse_type_name(curr_.lex);
            if (rett == TY_UNKNOWN) owner_->push_diag("Unknown return type: " + curr_.lex, {curr_.line, curr_.col, (int)curr_.lex.size()}, "");
            advance();

            if (curr_.k != TK::IDENT) {
                owner_->push_diag("Expected function name after type", {curr_.line, curr_.col, (int)curr_.lex.size()}, "");
                break;
            }
            std::string fname = curr_.lex;
            advance();

            int chosen = -1;
            auto &vec = owner_->function_table_[fname];
            for (auto &fs : vec) {
                if (fs.label_id >= 0 && fs.label_id < (int)owner_->asm_.labels.size() && owner_->asm_.labels[fs.label_id].target_pc == -1) {
                    chosen = fs.label_id;
                    break;
                }
            }
            if (chosen == -1) {
                chosen = owner_->asm_.make_label();
                FunctionSig fs; fs.name = fname; fs.label_id = chosen; fs.return_type = rett; fs.declared_line = curr_.line;
                owner_->function_table_[fname].push_back(fs);
            }
            else for (auto &fs : owner_->function_table_[fname]) if (fs.label_id == chosen) fs.return_type = rett;

            owner_->asm_.bind_label(chosen);
            owner_->current_function_ = fname;

            consume(TK::LP, "Expected '(' token after function name");
            std::vector<std::string> pnames;
            std::vector<TypeKind> ptypes;
            if (curr_.k != TK::RP) {
                while (true) {
                    if (curr_.k != TK::IDENT) {
                        owner_->push_diag("Expected param name", {curr_.line, curr_.col, (int)curr_.lex.size()}, owner_->current_function_);
                        break;
                    }
                    std::string pname = curr_.lex;
                    advance();
                    consume(TK::COLON, "Expected ':' token after param name");
                    if (curr_.k != TK::IDENT) {
                        owner_->push_diag("Expected param type", {curr_.line, curr_.col, (int)curr_.lex.size()}, owner_->current_function_);
                        break;
                    }
                    TypeKind pk = parse_type_name(curr_.lex);
                    if (pk == TY_UNKNOWN) owner_->push_diag("Unknown type for the param: " + curr_.lex, {curr_.line,curr_.col,(int)curr_.lex.size()}, owner_->current_function_);
                    advance();
                    pnames.push_back(pname);
                    ptypes.push_back(pk);
                    if (curr_.k == TK::COMMA) { advance(); continue; }
                    break;
                }
            }
            consume(TK::RP, "Expected ')'");
            for (auto &fs : owner_->function_table_[fname]) if (fs.label_id == chosen) { fs.param_types = ptypes; fs.return_type = rett; break; }

            {
                ScopeGuard sg(owner_);
                for (size_t i = 0; i < pnames.size(); ++i) owner_->define_local(pnames[i], ptypes[i]);

                if (curr_.k == TK::LBRACE) advance();
                while (curr_.k != TK::KEY_END && curr_.k != TK::RBRACE && curr_.k != TK::END_FILE) compile_stmt();
                if (curr_.k == TK::RBRACE) advance(); else consume(TK::KEY_END, "Expected 'end' token after function");

                int nilreg = owner_->load_const(Value::make_nil(), curr_.line);
                owner_->asm_.emit(OP_RETURN, curr_.line, nilreg);
            }

            owner_->current_function_.clear();
        }
        else {
            owner_->push_diag("expected 'on <type> <func>'", {curr_.line, curr_.col, (int)curr_.lex.size()}, "");
            advance();
        }
    }

    consume(TK::RBRACE, "Expected '}' on unit's end");

    owner_->asm_.bind_label(entry_label);

    std::vector<TypeKind> main_args;
    FunctionSig* mainfs = owner_->resolve_function("main", main_args);
    if (mainfs) {
        if (mainfs->return_type == TY_VOID) {
            int dummy = owner_->define_local("", TY_UNKNOWN);
            owner_->asm_.emit_call(curr_.line, dummy, mainfs->label_id, mainfs->param_types.size());
        }
        else {
            int dest = owner_->define_local("___main_ret", mainfs->return_type);
            owner_->asm_.emit_call(curr_.line, dest, mainfs->label_id, mainfs->param_types.size());
        }
    }
    else owner_->push_diag("Function 'main' not found", {0,0,0}, "");
    

    int nilreg = owner_->load_const(Value::make_nil(), curr_.line);
    owner_->asm_.emit(OP_RETURN, curr_.line, nilreg);

    if (!owner_->diagnostics_.empty()) {
        if (sm) {
            for (auto &d : owner_->diagnostics_) sm->report("Compilation error", d.loc, d.msg);
            throw std::runtime_error(owner_->diagnostics_.front().msg);
        }
        else {
            for (auto &d : owner_->diagnostics_) std::cerr << "Error: " << d.msg << " (line " << d.loc.line << ")\n";
            throw std::runtime_error("Compilation errors");
        }
    }
}

void Parser::compile_stmt() {
    int line = curr_.line;

    if (curr_.k == TK::TK_BAD) {
        owner_->push_diag(std::string("Unexpected token: '") + curr_.lex + std::string("'"),
                          {curr_.line, curr_.col, (int)curr_.lex.size()}, owner_->current_function_);
        advance();
        return;
    }

    if (curr_.k == TK::IDENT && next_.k == TK::IDENT) {
        Token t3 = peek_token(2);
        if (t3.k == TK::ASSIGN) {
            std::string type_name = curr_.lex;
            std::string var_name = next_.lex;
            TypeKind tk = parse_type_name(type_name);
            advance(); // type
            advance(); // var name
            advance(); // =
            TypeKind prev_expected = owner_->expected_return_;
            owner_->expected_return_ = tk;
            auto [r, rt] = compile_expr();
            owner_->expected_return_ = prev_expected;
            int slot = owner_->define_local(var_name, tk == TY_UNKNOWN ? rt : tk);
            owner_->asm_.emit(OP_MOVE, line, slot, r);
            return;
        }
    }

    if (curr_.k == TK::VAR) {
        advance();
        if (curr_.k != TK::IDENT) {
            owner_->push_diag("Expected variable name", {curr_.line,curr_.col,(int)curr_.lex.size()}, owner_->current_function_);
            if (curr_.k != TK::ASSIGN) advance();
            return;
        }
        std::string name = curr_.lex; advance();
        consume(TK::ASSIGN, "Expected '=' after variable name");
        auto [r,t] = compile_expr();
        int v = owner_->define_local(name, t);
        owner_->asm_.emit(OP_MOVE, line, v, r);
        return;
    }

    if (curr_.k == TK::IDENT && next_.k == TK::ASSIGN) {
        std::string name = curr_.lex;
        advance(); advance();
        auto [r,t] = compile_expr();
        int v = owner_->resolve_local(name);
        if (v == -1) {
            owner_->push_diag("Unknown variable: " + name, {line, curr_.col, (int)name.size()}, owner_->current_function_);
            return;
        }
        if (owner_->locals_[v].type != TY_UNKNOWN && t != TY_UNKNOWN && owner_->locals_[v].type != t)
            owner_->push_diag("Assigning with incompatible type to " + name, {line, curr_.col, (int)name.size()}, owner_->current_function_);
        owner_->asm_.emit(OP_MOVE, line, v, r);
        return;
    }

    if (curr_.k == TK::RETURN) {
        advance();
        if (curr_.k == TK::KEY_END || curr_.k == TK::RBRACE || curr_.k == TK::END_FILE) {
            int nilreg = owner_->load_const(Value::make_nil(), line);
            owner_->asm_.emit(OP_RETURN, line, nilreg);
            return;
        } else {
            auto [r, rt] = compile_expr();
            owner_->asm_.emit(OP_RETURN, line, r);
            return;
        }
    }

    if (curr_.k == TK::IF) {
        advance();
        consume(TK::LP, "Expected '(' after 'if'");
        auto [cond_reg, cond_t] = compile_expr();
        consume(TK::RP, "Expected ')'");
        int else_l = owner_->asm_.make_label(), end_l = owner_->asm_.make_label();
        owner_->asm_.emit_jump(OP_JMP_FALSE, line, cond_reg, else_l);
        owner_->begin_scope();
        while (curr_.k != TK::KEY_END && curr_.k != TK::ELSE && curr_.k != TK::END_FILE) compile_stmt();
        owner_->end_scope();
        owner_->asm_.emit_jump(OP_JMP, line, 0, end_l);
        owner_->asm_.bind_label(else_l);
        if (curr_.k == TK::ELSE) {
            advance();
            if (curr_.k == TK::IF)
                compile_stmt();
            else {
                owner_->begin_scope();
                while (curr_.k != TK::KEY_END && curr_.k != TK::END_FILE) compile_stmt();
                owner_->end_scope();
                consume(TK::KEY_END, "Expected 'end' token after else");
            }
        } else
            consume(TK::KEY_END, "Expected 'end' token after if");
        owner_->asm_.bind_label(end_l);
        return;
    }

    if (curr_.k == TK::WHILE) {
        advance();
        int start = owner_->asm_.make_label(), end = owner_->asm_.make_label();
        owner_->asm_.bind_label(start);
        consume(TK::LP, "Expected '(' after 'while'");
        auto [cond_reg, cond_t] = compile_expr();
        consume(TK::RP, "Expected ')'");
        owner_->asm_.emit_jump(OP_JMP_FALSE, line, cond_reg, end);
        owner_->begin_scope();
        while (curr_.k != TK::KEY_END && curr_.k != TK::END_FILE) compile_stmt();
        owner_->end_scope();
        owner_->asm_.emit_jump(OP_JMP, line, 0, start);
        consume(TK::KEY_END, "Expected 'end' token after while");
        owner_->asm_.bind_label(end);
        return;
    }

    compile_expr();
}

// ---------------- helpers ----------------
int Parser::make_string_const(const std::string &s, int line) {
    ObjString* o = new ObjString(s);
    return owner_->load_const(Value::make_obj(o), line);
}

int Parser::make_nil_const(int line) {
    return owner_->load_const(Value::make_nil(), line);
}

int Parser::emit_call_helper(int line, FunctionSig* fs, const std::vector<int>& arg_regs) {
    int dest = owner_->define_local("", fs->return_type);
    std::vector<int> call_arg_slots;
    call_arg_slots.reserve(arg_regs.size());
    for (size_t i = 0; i < arg_regs.size(); ++i) {
        TypeKind pk = TY_UNKNOWN;
        if (i < fs->param_types.size()) pk = fs->param_types[i];
        int argslot = owner_->define_local("", pk);
        call_arg_slots.push_back(argslot);
    }
    for (size_t i = 0; i < arg_regs.size(); ++i)
        owner_->asm_.emit(OP_MOVE, line, call_arg_slots[i], arg_regs[i]);

    owner_->asm_.emit_call(line, dest, fs->label_id, (int)arg_regs.size());
    return dest;
}

template<typename F>
void Parser::parse_delimited(TK open, TK close, F element_cb) {
    if (curr_.k != open) return;
    advance();
    if (curr_.k == close) { advance(); return; }
    while (true) {
        element_cb();
        if (curr_.k == close) { advance(); break; }
        consume(TK::COMMA, "Expected ',' in list");
    }
}

bool Parser::parse_param_pair(std::string &out_name, TypeKind &out_type) {
    if (curr_.k != TK::IDENT) return false;
    out_name = curr_.lex; advance();
    consume(TK::COLON, "Expected ':' after parameter name");
    if (curr_.k != TK::IDENT) return false;
    out_type = parse_type_name(curr_.lex);
    advance();
    return true;
}
