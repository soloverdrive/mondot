#pragma once
#include <vector>
#include "value.h"

enum OpCode : uint8_t {
    OP_CONST, OP_MOVE, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_LT, OP_GT, OP_EQ,
    OP_JMP, OP_JMP_FALSE, OP_CALL, OP_CALL_OBJ, OP_RETURN,
    OP_TABLE_SET, OP_TABLE_NEW, OP_INDEX,
    OP_STRUCT_NEW, OP_STRUCT_SET, OP_STRUCT_GET,
    OP_LIST_NEW, OP_LIST_PUSH, OP_LIST_GET, OP_LIST_SET, OP_LIST_LEN,
};

struct Instr {
    OpCode op;
    int a, b, c;
    int line;
};

struct Label {
    int target_pc = -1;
    std::vector<int> refs;
};

struct Assembler {
    std::vector<Instr> code;
    std::vector<Value> constants;
    std::vector<Label> labels;

    int make_label();
    void bind_label(int id);
    int emit(OpCode op, int line, int a = 0, int b = 0, int c = 0);
    void emit_jump(OpCode op, int line, int cond_reg, int label_id);
    int add_constant(Value v);

    int emit_call(int line, int dest_reg, int label_id, int argc);
    int emit_call_obj(int line, int dest_reg, int func_reg, int argc);
};
