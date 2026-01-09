#include "assembler.h"

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
