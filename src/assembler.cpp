#include "assembler.h"
#include <cassert>

int Assembler::make_label() { labels.emplace_back(); return (int)labels.size() - 1; }

void Assembler::bind_label(int id) {
    labels[id].target_pc = (int)code.size();
    for (int instr_idx : labels[id].refs) {
        if (instr_idx >= 0 && instr_idx < (int)code.size()) code[instr_idx].b = labels[id].target_pc;
    }
    labels[id].refs.clear();
}

int Assembler::emit(OpCode op, int line, int a, int b, int c) {
    code.push_back({op, a, b, c, line});
    return (int)code.size() - 1;
}

void Assembler::emit_jump(OpCode op, int line, int cond_reg, int label_id) {
    int target = labels[label_id].target_pc;
    int idx = emit(op, line, cond_reg, target, 0);
    if (target == -1) labels[label_id].refs.push_back(idx);
}

int Assembler::add_constant(Value v) {
    for (size_t i = 0; i < constants.size(); ++i) if (constants[i].raw == v.raw) return (int)i;
    constants.push_back(v);
    retain(v);
    return (int)constants.size() - 1;
}

int Assembler::emit_call(int line, int dest_reg, int label_id, int argc) {
    int target = labels[label_id].target_pc;
    int idx = emit(OP_CALL, line, dest_reg, target, argc);
    if (target == -1) labels[label_id].refs.push_back(idx);
    return idx;
}

int Assembler::emit_call_obj(int line, int dest_reg, int func_reg, int argc) {
    return emit(OP_CALL_OBJ, line, dest_reg, func_reg, argc);
}

void Assembler::run_optimizations(int level, int max_iters) {
    bool changed = false;
    int iter = 0;
    do {
        changed = false;
        if (level >= 1) changed |= pass_peep_hole();
        if (level >= 1) changed |= pass_constant_fold_and_propagate();
        iter++;
    } while (changed && iter < max_iters);
}

bool Assembler::pass_peep_hole() {
    bool changed = false;
    std::vector<int> removed(code.size(), 0);
    for (size_t i = 0; i + 1 < code.size(); ++i) {
        auto &ins = code[i];
        auto &insn = code[i+1];
        if (ins.op == OP_CONST && insn.op == OP_MOVE && insn.b == ins.a) {
            insn.op = OP_CONST;
            insn.b = ins.b;
            insn.c = 0;
            removed[i] = 1; changed = true;
        }
        if (ins.op == OP_MOVE && ins.a == ins.b) { removed[i] = 1; changed = true; }
    }
    if (changed) {
        std::vector<int> rem_idx;
        for (size_t i = 0; i < removed.size(); ++i) if (removed[i]) rem_idx.push_back((int)i);
        compact_and_rewrite_labels(rem_idx);
    }
    return changed;
}

bool Assembler::pass_constant_fold_and_propagate() {
    bool changed = false;
    std::vector<int> removed(code.size(), 0);

    for (size_t i = 0; i < code.size(); ++i) {
        Instr &ins = code[i];
        if (ins.op == OP_ADD || ins.op == OP_SUB || ins.op == OP_MUL || ins.op == OP_DIV) {
            if (ins.a >= 0 && ins.b >= 0) {
                if (i >= 2) {
                    Instr &i1 = code[i-2];
                    Instr &i2 = code[i-1];
                    if (i1.op == OP_CONST && i2.op == OP_CONST) {
                        if (i1.a == ins.a && i2.a == ins.b) {
                            Value v1 = constants[i1.b];
                            Value v2 = constants[i2.b];
                            if (v1.is_num() && v2.is_num()) {
                                long long n1 = v1.as_intscaled();
                                long long n2 = v2.as_intscaled();
                                long long result = 0;
                                bool ok = true;
                                switch (ins.op) {
                                    case OP_ADD: result = n1 + n2; break;
                                    case OP_SUB: result = n1 - n2; break;
                                    case OP_MUL: result = n1 * n2; break;
                                    case OP_DIV: if (n2 == 0) ok = false; else result = n1 / n2; break;
                                    default: ok = false; break;
                                }
                                if (ok) {
                                    Value newv = Value::make_intscaled(result);
                                    int new_const = add_constant(newv);
                                    ins.op = OP_CONST;
                                    ins.b = new_const;
                                    ins.c = 0;
                                    removed[i-2] = 1; removed[i-1] = 1;
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (changed) {
        std::vector<int> rem_idx;
        for (size_t i = 0; i < removed.size(); ++i) if (removed[i]) rem_idx.push_back((int)i);
        compact_and_rewrite_labels(rem_idx);
    }

    return changed;
}

void Assembler::compact_and_rewrite_labels(const std::vector<int>& removed) {
    if (removed.empty()) return;
    std::vector<char> rem(code.size(), 0);
    for (int r : removed) if (r >= 0 && r < (int)rem.size()) rem[r] = 1;

    for (const auto &lab : labels) {
        if (lab.target_pc >= 0 && lab.target_pc < (int)rem.size()) {
            rem[lab.target_pc] = 0;
        }
    }

    std::vector<int> remap(code.size(), -1);
    std::vector<Instr> newcode;
    newcode.reserve(code.size());
    for (size_t i = 0; i < code.size(); ++i) {
        if (!rem[i]) {
            remap[i] = (int)newcode.size();
            newcode.push_back(code[i]);
        }
    }

    for (auto &lab : labels) {
        std::vector<int> newrefs;
        for (int idx : lab.refs) {
            if (idx >= 0 && idx < (int)remap.size() && remap[idx] != -1) newrefs.push_back(remap[idx]);
        }
        lab.refs = std::move(newrefs);

        if (lab.target_pc >= 0 && lab.target_pc < (int)remap.size())
            lab.target_pc = remap[lab.target_pc];
        else if (lab.target_pc >= 0) 
            lab.target_pc = -1;
    }

    for (auto &ins : newcode) {
        if (ins.op == OP_JMP || ins.op == OP_JMP_FALSE || ins.op == OP_CALL) {
            int oldb = ins.b;
            if (oldb >= 0 && oldb < (int)remap.size()) ins.b = remap[oldb];
            else ins.b = -1;
        }
    }

    code.swap(newcode);
}
