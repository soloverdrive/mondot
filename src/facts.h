#pragma once
#include "assembler.h"

inline const char* opcode_to_string(OpCode op) {
    switch (op) {
        case OP_CONST:     return "OP_CONST";
        case OP_MOVE:      return "OP_MOVE";
        case OP_ADD:       return "OP_ADD";
        case OP_SUB:       return "OP_SUB";
        case OP_MUL:       return "OP_MUL";
        case OP_DIV:       return "OP_DIV";
        case OP_LT:        return "OP_LT";
        case OP_GT:        return "OP_GT";
        case OP_EQ:        return "OP_EQ";
        case OP_JMP:       return "OP_JMP";
        case OP_JMP_FALSE: return "OP_JMP_FALSE";
        case OP_CALL:      return "OP_CALL";
        case OP_CALL_OBJ:  return "OP_CALL_OBJ";
        case OP_RETURN:    return "OP_RETURN";
        case OP_TABLE_SET: return "OP_TABLE_SET";
        case OP_TABLE_NEW: return "OP_TABLE_NEW";
        case OP_INDEX:     return "OP_INDEX";
        case OP_STRUCT_NEW: return "OP_STRUCT_NEW";
        case OP_STRUCT_SET: return "OP_STRUCT_SET";
        case OP_STRUCT_GET: return "OP_STRUCT_GET";
        case OP_LIST_NEW:   return "OP_LIST_NEW";
        case OP_LIST_PUSH:  return "OP_LIST_PUSH";
        case OP_LIST_GET:   return "OP_LIST_GET";
        case OP_LIST_SET:   return "OP_LIST_SET";
        case OP_LIST_LEN:   return "OP_LIST_LEN";
        default:           return "OP_UNKNOWN";
    }
}
