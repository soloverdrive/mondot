#include "parser.h"
#include "compiler.h"
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

std::pair<TypeKind,int> Parser::resolve_type_name(const std::string &s) {
    TypeKind tk = parse_type_name(s);
    if (tk != TY_UNKNOWN) return {tk, -1};
    int id = owner_->find_item_id_by_name(s);
    if (id >= 0) return {TY_ITEM, id};
    return {TY_UNKNOWN, -1};
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
        int dest = owner_->define_local("", TY_LIST);
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
        return {dest, TY_LIST};
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
            if (fs->user_return_type_id >= 0) {
                int itemid = fs->user_return_type_id;
                int dest = owner_->define_local("", TY_ITEM, itemid);

                owner_->asm_.emit(OP_STRUCT_NEW, line, dest, itemid, (int)owner_->get_item_fields(itemid).size());

                const auto &fields = owner_->get_item_fields(itemid);
                for (size_t i = 0; i < arg_regs.size() && i < fields.size(); ++i) {
                    // a = struct_reg, b = field_index, c = value_reg
                    owner_->asm_.emit(OP_STRUCT_SET, line, dest, (int)i, arg_regs[i]);
                }

                return {dest, TY_ITEM};
            }
            if (fs->is_builtin) {
                int bid = BuiltinRegistry::lookup_name(fs->name);
                ObjFunction* of = new ObjFunction(bid, fs->return_type, fs->param_types, fs->name);
                int func_reg = owner_->load_const(Value::make_obj(of), line);

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

        int tmp = owner_->define_local("", owner_->locals_[loc].type, owner_->locals_[loc].user_type_id);
        owner_->asm_.emit(OP_MOVE, line, tmp, loc);

        while (true) {
            if (curr_.k == TK::DOT) {
                advance();
                if (curr_.k == TK::IDENT) {
                    std::string member = curr_.lex;
                    advance();

                    int base_user_id = -1;
                    if (tmp >= 0 && tmp < (int)owner_->locals_.size()) base_user_id = owner_->locals_[tmp].user_type_id;

                    if (base_user_id >= 0) {
                        const auto &fields = owner_->get_item_fields(base_user_id);
                        int found_idx = -1;
                        TypeKind field_type = TY_UNKNOWN;
                        for (size_t fi = 0; fi < fields.size(); ++fi) {
                            if (fields[fi].first == member) { found_idx = (int)fi; field_type = fields[fi].second; break; }
                        }
                        if (found_idx >= 0) {
                            int dest = owner_->define_local("", field_type);
                            owner_->asm_.emit(OP_STRUCT_GET, line, dest, tmp, found_idx);
                            tmp = dest;
                            continue;
                        }
                    }

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

            std::string rett_tok = curr_.lex;
            auto [rett_kind, rett_user_id] = resolve_type_name(rett_tok);
            if (rett_kind == TY_UNKNOWN) owner_->push_diag("Unknown return type: " + rett_tok, {curr_.line, curr_.col, (int)curr_.lex.size()}, "");
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
                FunctionSig fs; fs.name = fname; fs.label_id = chosen; fs.return_type = rett_kind; fs.declared_line = curr_.line;
                fs.user_return_type_id = rett_user_id;
                owner_->function_table_[fname].push_back(fs);
            } else
                for (auto &fs : owner_->function_table_[fname]) if (fs.label_id == chosen) { fs.return_type = rett_kind; fs.user_return_type_id = rett_user_id; break; }

            owner_->asm_.bind_label(chosen);
            owner_->current_function_ = fname;

            consume(TK::LP, "Expected '(' token after function name");
            std::vector<std::string> pnames;
            std::vector<TypeKind> ptypes;
            std::vector<int> puserids;
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

                    std::string ptype_tok = curr_.lex;
                    auto [pk, puid] = resolve_type_name(ptype_tok);
                    if (pk == TY_UNKNOWN) owner_->push_diag("Unknown type for the param: " + ptype_tok, {curr_.line,curr_.col,(int)ptype_tok.size()}, owner_->current_function_);
                    advance();
                    pnames.push_back(pname);
                    ptypes.push_back(pk);
                    puserids.push_back(puid);

                    if (curr_.k == TK::COMMA) { advance(); continue; }
                    break;
                }
            }
            consume(TK::RP, "Expected ')'");

            for (auto &fs : owner_->function_table_[fname]) if (fs.label_id == chosen) {
                fs.param_types = ptypes;
                fs.return_type = rett_kind;
                fs.user_return_type_id = rett_user_id;
                break;
            }

            {
                ScopeGuard sg(owner_);
                for (size_t i = 0; i < pnames.size(); ++i) {
                    int uid = (i < puserids.size()) ? puserids[i] : -1;
                    owner_->define_local(pnames[i], ptypes[i], uid);
                }

                if (curr_.k == TK::LBRACE) advance();
                while (curr_.k != TK::KEY_END && curr_.k != TK::RBRACE && curr_.k != TK::END_FILE) compile_stmt();
                if (curr_.k == TK::RBRACE) advance(); else consume(TK::KEY_END, "Expected 'end' token after function");

                int nilreg = owner_->load_const(Value::make_nil(), curr_.line);
                owner_->asm_.emit(OP_RETURN, curr_.line, nilreg);
            }

            owner_->current_function_.clear();
            continue;
        }

        else if (curr_.k == TK::ITEM) {
            advance();
            if (curr_.k != TK::IDENT) { owner_->push_diag("Expected item name", {curr_.line,curr_.col,(int)curr_.lex.size()}, ""); break; }
            std::string item_name = curr_.lex; advance();
            std::string parent_name;
            if (curr_.k == TK::COLON) {
                advance();
                if (curr_.k == TK::IDENT) { parent_name = curr_.lex; advance(); }
            }
            consume(TK::LP, "Expected '(' after item header");
            std::vector<std::pair<std::string, TypeKind>> fields;
            if (curr_.k != TK::RP) {
                while (true) {
                    if (curr_.k != TK::IDENT) { owner_->push_diag("Expected field type", {curr_.line,curr_.col,(int)curr_.lex.size()}, ""); break; }
                    std::string type_tok = curr_.lex; advance();
                    TypeKind ftk = parse_type_name(type_tok);
                    if (ftk == TY_UNKNOWN) {
                        int iid = owner_->find_item_id_by_name(type_tok);
                        if (iid >= 0) ftk = TY_TABLE; else owner_->push_diag("Unknown field type: " + type_tok, {curr_.line,curr_.col,(int)type_tok.size()}, "");
                    }
                    if (curr_.k != TK::IDENT) { owner_->push_diag("Expected field name", {curr_.line,curr_.col,(int)curr_.lex.size()}, ""); break; }
                    std::string fname = curr_.lex; advance();
                    fields.push_back({fname, ftk});
                    if (curr_.k == TK::COMMA) { advance(); continue; }
                    break;
                }
            }
            consume(TK::RP, "Expected ')'");
            owner_->register_item_type(item_name, parent_name, fields);
            continue;
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

    if (curr_.k == TK::IDENT && peek_token(1).k != TK::LP) {
        int save_tokpos = tokpos_;
        Token nameTok = curr_;
        advance();

        int loc = owner_->resolve_local(nameTok.lex);
        if (loc != -1) {
            int tmp = owner_->define_local("", owner_->locals_[loc].type, owner_->locals_[loc].user_type_id);
            owner_->asm_.emit(OP_MOVE, line, tmp, loc);

            struct ChainOp { enum Kind { DOT, LBRACK } kind; std::string member; int key_reg; };
            std::vector<ChainOp> chain;

            bool failed_parse_chain = false;
            while (curr_.k == TK::DOT || curr_.k == TK::LBRACK) {
                if (curr_.k == TK::DOT) {
                    advance();
                    if (curr_.k != TK::IDENT) { failed_parse_chain = true; break; }
                    std::string member = curr_.lex;
                    advance();
                    chain.push_back({ChainOp::DOT, member, -1});
                } else {
                    advance();
                    auto p = compile_expr();
                    // 0-index
                    int negone = owner_->load_const(Value::make_int(-1), line);
                    owner_->asm_.emit(OP_ADD, line, p.first, p.first, negone);
                    consume(TK::RBRACK, "Expected ']'");
                    chain.push_back({ChainOp::LBRACK, "", p.first});
                }
            }

            if (curr_.k == TK::ASSIGN) {
                if (failed_parse_chain) {
                    tokpos_ = save_tokpos;
                    curr_ = peek_token(0);
                    next_ = peek_token(1);
                } else {
                    advance();

                    auto [rreg, rt] = compile_expr();

                    if (chain.empty()) {
                        owner_->asm_.emit(OP_MOVE, line, loc, rreg);
                        return;
                    }

                    for (size_t i = 0; i + 1 < chain.size(); ++i) {
                        ChainOp &op = chain[i];
                        if (op.kind == ChainOp::DOT) {
                            int base_user_id = -1;
                            if (tmp >= 0 && tmp < (int)owner_->locals_.size()) base_user_id = owner_->locals_[tmp].user_type_id;
                            if (base_user_id >= 0) {
                                const auto &fields = owner_->get_item_fields(base_user_id);
                                int found_idx = -1;
                                TypeKind field_type = TY_UNKNOWN;
                                for (size_t fi = 0; fi < fields.size(); ++fi) {
                                    if (fields[fi].first == op.member) { found_idx = (int)fi; field_type = fields[fi].second; break; }
                                }
                                if (found_idx >= 0) {
                                    int newtmp = owner_->define_local("", field_type);
                                    owner_->asm_.emit(OP_STRUCT_GET, line, newtmp, tmp, found_idx);
                                    tmp = newtmp;
                                    continue;
                                }
                            }
                            ObjString* s = new ObjString(op.member);
                            int keyreg = owner_->load_const(Value::make_obj(s), line);
                            int newtmp = owner_->define_local("", TY_UNKNOWN);
                            owner_->asm_.emit(OP_INDEX, line, newtmp, tmp, keyreg);
                            tmp = newtmp;
                            continue;
                        } else {
                            int newtmp = owner_->define_local("", TY_UNKNOWN);
                            owner_->asm_.emit(OP_INDEX, line, newtmp, tmp, op.key_reg);
                            tmp = newtmp;
                            continue;
                        }
                    }

                    ChainOp &last = chain.back();
                    if (last.kind == ChainOp::DOT) {
                        int base_user_id = -1;
                        if (tmp >= 0 && tmp < (int)owner_->locals_.size()) base_user_id = owner_->locals_[tmp].user_type_id;
                        if (base_user_id >= 0) {
                            const auto &fields = owner_->get_item_fields(base_user_id);
                            int found_idx = -1;
                            for (size_t fi = 0; fi < fields.size(); ++fi) {
                                if (fields[fi].first == last.member) { found_idx = (int)fi; break; }
                            }
                            if (found_idx >= 0) {
                                // emit OP_STRUCT_SET: struct_reg = tmp, field_index = found_idx, value = rreg
                                owner_->asm_.emit(OP_STRUCT_SET, line, tmp, found_idx, rreg);
                                return;
                            }
                        }
                        ObjString* s = new ObjString(last.member);
                        int keyreg = owner_->load_const(Value::make_obj(s), line);
                        owner_->asm_.emit(OP_TABLE_SET, line, tmp, keyreg, rreg);
                        return;
                    } else {
                        owner_->asm_.emit(OP_TABLE_SET, line, tmp, last.key_reg, rreg);
                        return;
                    }
                }
            } else {
                tokpos_ = save_tokpos;
                curr_ = peek_token(0);
                next_ = peek_token(1);
            }
        } else {
            tokpos_ = save_tokpos;
            curr_ = peek_token(0);
            next_ = peek_token(1);
        }
    }

    if (curr_.k == TK::IDENT && next_.k == TK::IDENT) {
        Token t3 = peek_token(2);
        if (t3.k == TK::ASSIGN) {
            std::string type_tok = curr_.lex;
            auto [tk, tuid] = resolve_type_name(type_tok);

            std::string var_name = next_.lex;
            advance(); // type
            advance(); // var name
            advance(); // =

            TypeKind prev_expected = owner_->expected_return_;
            owner_->expected_return_ = (tk == TY_UNKNOWN) ? prev_expected : tk;
            auto [r, rt] = compile_expr();
            owner_->expected_return_ = prev_expected;

            int user_id = -1;
            if (tk == TY_ITEM) user_id = tuid;

            int slot = owner_->define_local(var_name, (tk == TY_UNKNOWN ? rt : tk), user_id);
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
    int dest = owner_->define_local("", fs->return_type, fs->user_return_type_id);
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
