#pragma once
#include <string>
#include <vector>

struct SourceLocation { int line; int col; int length; };

struct SourceManager {
    std::string source;
    std::vector<std::string> lines;
    std::string path;

    SourceManager(const std::string& src = "", const std::string& p = "");
    void report(const std::string& title, SourceLocation loc, const std::string& msg);
};
