#include <iostream>
#include <fstream>
#include <sstream>
#include "compiler.h"
#include "bytecode_io.h"
#include "vm.h"
#include "source_manager.h"
#include "builtin_std.h"

void print_help() {
    std::cout << "MonDot Compiler & VM\n";
    std::cout << "Usage:\n";
    std::cout << "  mondot build <file.mon> -o <output.mdotc>\n";
    std::cout << "  mondot run <file.mdotc>\n";
    std::cout << "  mondot <file.mon> (compiles and runs on memory)\n";
}

int main(int argc, char* argv[])
{
    register_default_builtins(); //io module, math module, etc

    if (argc < 2) {
        print_help();
        return 0;
    }

    std::string mode = argv[1];
    if (mode == "build") {
        if (argc < 5) { print_help(); return 1; }
        std::string input_file = argv[2];
        std::string output_file = argv[4];
        std::ifstream f(input_file);
        if (!f) { std::cerr << "Error when opening " << input_file << std::endl; return 1; }
        std::stringstream buffer; buffer << f.rdbuf();
        SourceManager sm(buffer.str(), input_file);
        try {
            Compiler comp(buffer.str());
            comp.compile_unit(&sm);
            BytecodeIO::save(output_file, comp.asm_, true);
        } catch (std::exception& e) {
            return 1;
        }
        return 0;
    } else if (mode == "run") {
        if (argc < 3) { print_help(); return 1; }
        std::string input_file = argv[2];
        try {
            Assembler as;
            BytecodeIO::load(input_file, as);
            VM vm(as);
            vm.run();
        } catch (std::exception& e) {
            return 1;
        } 
        return 0;
    } else {
        std::ifstream f(mode);
        if (!f) { std::cerr << "File not found: " << mode << std::endl; return 1; }
        std::stringstream buffer; buffer << f.rdbuf();
        SourceManager sm(buffer.str(), mode);
        try {
            Compiler comp(buffer.str());
            comp.compile_unit(&sm);
            VM vm(comp.asm_, &sm);
            vm.run();
        } catch (std::exception& e) {
            return 1;
        }
        return 0;
    }
}
