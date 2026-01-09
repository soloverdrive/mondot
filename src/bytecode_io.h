#pragma once
#include <string>
#include "assembler.h"

struct BytecodeIO {
    static void save(const std::string& filename, Assembler& as);
    static void load(const std::string& filename, Assembler& as);
};
