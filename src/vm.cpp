#include "vm.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include "builtin_registry.h"

VM::VM(Assembler& a, SourceManager* mgr)
    : code(a.code), constants(a.constants), sm(mgr) {
    stack.resize(4096);
}

static inline int64_t to_intscaled_from_value(const Value &v) {
    return v.as_intscaled();
}

static inline Value from_intscaled(int64_t q) {
    return Value::make_intscaled(q);
}

void VM::run() {
    frames.push_back({-1, 0, -1});
    ip = 0;
    Instr* instructions = code.data();
    size_t code_size = code.size();
    Value* consts = constants.data();
    const int FRAME_SIZE = 256;

    while (ip < code_size) {
        Instr ins = instructions[ip];
        int base = frames.back().base_reg;

        switch (ins.op) {
            case OP_CONST: {
                // ins.a = dest, ins.b = const index
                int dst = base + ins.a;
                release(stack[dst]);
                stack[dst] = consts[ins.b];
                retain(stack[dst]);
                break;
            }
            case OP_MOVE: {
                int dst = base + ins.a;
                int src = base + ins.b;
                release(stack[dst]);
                stack[dst] = stack[src];
                retain(stack[dst]);
                break;
            }

            case OP_ADD: {
                int dst = base + ins.a;
                int a = base + ins.b;
                int b = base + ins.c;
                int64_t fa = to_intscaled_from_value(stack[a]);
                int64_t fb = to_intscaled_from_value(stack[b]);
                int64_t fres = fa + fb;
                release(stack[dst]);
                stack[dst] = from_intscaled(fres);
                break;
            }
            case OP_SUB: {
                int dst = base + ins.a;
                int a = base + ins.b;
                int b = base + ins.c;
                int64_t fa = to_intscaled_from_value(stack[a]);
                int64_t fb = to_intscaled_from_value(stack[b]);
                int64_t fres = fa - fb;
                release(stack[dst]);
                stack[dst] = from_intscaled(fres);
                break;
            }
            case OP_MUL: {
                int dst = base + ins.a;
                int a = base + ins.b;
                int b = base + ins.c;
                int64_t fa = to_intscaled_from_value(stack[a]);
                int64_t fb = to_intscaled_from_value(stack[b]);
                // multiply in 128-bit to keep precision: (fa * fb) >> INTSCALED_SHIFT
                __int128 tmp = (__int128)fa * (__int128)fb;
                int64_t fres = (int64_t)(tmp >> INTSCALED_SHIFT);
                release(stack[dst]);
                stack[dst] = from_intscaled(fres);
                break;
            }
            case OP_DIV: {
                int dst = base + ins.a;
                int a = base + ins.b;
                int b = base + ins.c;
                int64_t fa = to_intscaled_from_value(stack[a]);
                int64_t fb = to_intscaled_from_value(stack[b]);
                if (fb == 0) {
                    release(stack[dst]);
                    stack[dst] = Value::make_nil();
                    break;
                }
                __int128 numer = (__int128)fa << INTSCALED_SHIFT;
                int64_t fres = (int64_t)(numer / (__int128)fb);
                release(stack[dst]);
                stack[dst] = from_intscaled(fres);
                break;
            }

            case OP_LT: {
                int dst = base + ins.a;
                int a = base + ins.b;
                int b = base + ins.c;
                int64_t fa = to_intscaled_from_value(stack[a]);
                int64_t fb = to_intscaled_from_value(stack[b]);
                release(stack[dst]);
                stack[dst] = Value::make_bool(fa < fb);
                break;
            }
            case OP_GT: {
                int dst = base + ins.a;
                int a = base + ins.b;
                int b = base + ins.c;
                int64_t fa = to_intscaled_from_value(stack[a]);
                int64_t fb = to_intscaled_from_value(stack[b]);
                release(stack[dst]);
                stack[dst] = Value::make_bool(fa > fb);
                break;
            }
            case OP_EQ: {
                int dst = base + ins.a;
                int a = base + ins.b;
                int b = base + ins.c;
                bool eq = (stack[a].raw == stack[b].raw);
                release(stack[dst]);
                stack[dst] = Value::make_bool(eq);
                break;
            }

            case OP_CALL: {
                int dest_rel = ins.a;
                int target_pc = ins.b;
                int argc = ins.c;

                int caller_base = frames.back().base_reg;
                int dest_abs = caller_base + dest_rel;

                int new_base = caller_base + FRAME_SIZE;

                for (int i = 0; i < argc; i++) {
                    Value v = stack[caller_base + dest_rel + 1 + i];
                    release(stack[new_base + i]);
                    stack[new_base + i] = v;
                    retain(stack[new_base + i]);
                }

                frames.push_back({ (int)ip + 1, new_base, dest_abs });
                ip = target_pc;
                continue;
            }

            case OP_CALL_OBJ: {
                int dest_rel = ins.a;
                int func_rel = ins.b;
                int argc = ins.c;

                int caller_base = frames.back().base_reg;
                int dest_abs = caller_base + dest_rel;
                int func_abs = caller_base + func_rel;
                int arg0_abs = dest_abs + 1;

                Value fv = stack[func_abs];
                if (!fv.is_obj() || fv.as_obj()->type != OBJ_FUNCTION) {
                    release(stack[dest_abs]);
                    stack[dest_abs] = Value::make_nil();
                    retain(stack[dest_abs]);
                    break;
                }
                ObjFunction* of = (ObjFunction*)fv.as_obj();

                if (of->builtin_id >= 0) {
                    const BuiltinEntry* be = BuiltinRegistry::get_entry(of->builtin_id);
                    if (!be || !be->fn) {
                        release(stack[dest_abs]);
                        stack[dest_abs] = Value::make_nil();
                        retain(stack[dest_abs]);
                        break;
                    }
                    const Value* args = (argc > 0) ? &stack[arg0_abs] : nullptr;
                    Value result = be->fn(argc, args, be->ctx);
                    release(stack[dest_abs]);
                    stack[dest_abs] = result;
                    retain(stack[dest_abs]);
                    break;
                } else {
                    release(stack[dest_abs]);
                    stack[dest_abs] = Value::make_nil();
                    retain(stack[dest_abs]);
                    break;
                }
            }

            case OP_JMP_FALSE: {
                Value v = stack[base + ins.a];
                bool cond_false = false;
                if (v.is_bool()) cond_false = !v.as_bool();
                else cond_false = v.is_nil();
                if (cond_false) { ip = ins.b; continue; }
                break;
            }
            case OP_JMP:
                ip = ins.b;
                continue;

            case OP_RETURN: {
                int src_rel = ins.a;
                int callee_base = frames.back().base_reg;
                Value retv = stack[callee_base + src_rel];
                CallFrame fr = frames.back();
                frames.pop_back();
                if (frames.empty()) return;
                int ret_dst = fr.ret_slot;
                release(stack[ret_dst]);
                stack[ret_dst] = retv;
                retain(stack[ret_dst]);
                ip = fr.return_addr;
                continue;
            }

            case OP_TABLE_NEW: {
                // ins.a = dest_reg (relative)
                int dst = base + ins.a;
                ObjTable* t = new ObjTable();
                Value v = Value::make_obj(t);

                release(stack[dst]);
                stack[dst] = v;
                retain(stack[dst]);
                break;
            }

            case OP_TABLE_SET: {
                int tbl_reg = base + ins.a;
                int key_reg = base + ins.b;
                int val_reg = base + ins.c;
                Value tblv = stack[tbl_reg];
                if (!tblv.is_obj() || tblv.as_obj()->type != OBJ_TABLE) {
                    ObjTable* tnew = new ObjTable();
                    Value newv = Value::make_obj(tnew);
                    release(stack[tbl_reg]);
                    stack[tbl_reg] = newv;
                    retain(stack[tbl_reg]);
                    tblv = stack[tbl_reg];
                }
                ObjTable* tbl = (ObjTable*)tblv.as_obj();
                Value key = stack[key_reg];
                Value val = stack[val_reg];
                bool replaced = false;
                for (auto &kv : tbl->entries) {
                    if (kv.first.raw == key.raw) {
                        release(kv.second);
                        kv.second = val;
                        retain(kv.second);
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    retain(key);
                    retain(val);
                    tbl->entries.emplace_back(key, val);
                }
                break;
            }

            case OP_INDEX: {
                int dest = base + ins.a;
                int tbl_reg = base + ins.b;
                int key_reg = base + ins.c;
                Value tblv = stack[tbl_reg];
                Value result = Value::make_nil();
                if (tblv.is_obj() && tblv.as_obj()->type == OBJ_TABLE) {
                    ObjTable* tbl = (ObjTable*)tblv.as_obj();
                    Value key = stack[key_reg];
                    bool found = false;
                    for (auto &kv : tbl->entries) {
                        if (kv.first.raw == key.raw) {
                            result = kv.second;
                            found = true;
                            break;
                        }
                    }
                    if (!found) result = Value::make_nil();
                }
                else result = Value::make_nil();
                
                release(stack[dest]);
                stack[dest] = result;
                retain(stack[dest]);
                break;
            }

            default:
                break;
        }
        ip++;
    }
}

VM::~VM() {
    for (auto v : constants) release(v);
    for (auto v : stack) release(v);
}
