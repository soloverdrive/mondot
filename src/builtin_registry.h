#pragma once
#include <string>
#include <vector>
#include <functional>
#include "value.h"

// (argc, argv, ctx) -> Value
using BuiltinFn = Value(*)(int, const Value*, void*);

struct BuiltinEntry {
    std::string name;
    BuiltinFn fn;
    void* ctx;
    TypeKind return_type;
    std::vector<TypeKind> param_types;
};

struct BuiltinRegistry {
    static int register_builtin(const std::string &name, BuiltinFn fn, void* ctx, TypeKind ret, const std::vector<TypeKind>& params);
    static const BuiltinEntry* get_entry(int id);
    static int lookup_name(const std::string &name);
    static int lookup_name(const std::string &name, const std::vector<TypeKind>& params);
    static const std::vector<BuiltinEntry>& all_entries();
};
