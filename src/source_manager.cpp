#include "source_manager.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>

SourceManager::SourceManager(const std::string& src, const std::string& p) : source(src), path(p) {
    std::stringstream ss(source);
    std::string line;
    while (std::getline(ss, line)) lines.push_back(line);
}

void SourceManager::report(const std::string& title, SourceLocation loc, const std::string& msg) {
    std::cerr << "\n\033[1;31m" << title << ":\033[0m " << msg << "\n";
    if (!path.empty()) std::cerr << "    at " << path << "\n";
    if (loc.line > 0 && loc.line <= (int)lines.size()) {
        std::string code_line = lines[loc.line - 1];
        std::string print_line = code_line;
        std::replace(print_line.begin(), print_line.end(), '\t', ' ');
        std::cerr << "    |\n" << std::setw(3) << loc.line << " | " << print_line << "\n    | ";
        for (int i = 1; i < loc.col; i++) std::cerr << " ";
        std::cerr << "\033[1;33m";
        for (int i = 0; i < std::max(1, loc.length); i++) std::cerr << "^";
        std::cerr << " " << msg << "\033[0m\n    |\n";
    }
    //else std::cerr << "    (no source context)\n";
}
