#pragma once
#include <vector>
#include "value.h"
#include "assembler.h"
#include "source_manager.h"

struct CallFrame {
    int return_addr; int base_reg; int ret_slot;
};

struct VM {
    std::vector<Value> stack;
    std::vector<CallFrame> frames;
    std::vector<Instr> code;
    std::vector<Value> constants;
    SourceManager* sm = nullptr;
    size_t ip = 0;

    VM(Assembler& a, SourceManager* mgr = nullptr);
    void run();
    ~VM();
};
