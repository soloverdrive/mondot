#pragma once
#include <string>
#include "assembler.h"

struct BytecodeIO {
    static void save(const std::string& filename, Assembler& as, bool alsoVisual = false);
    static void load(const std::string& filename, Assembler& as);

private:
    static void save_text(const std::string& filename_txt, Assembler& as);
    static std::string instr_to_string(const Instr& i);
    static std::string escape_string(const std::string& s);
};
