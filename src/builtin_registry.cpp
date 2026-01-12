#include "builtin_registry.h"
#include <mutex>

static std::vector<BuiltinEntry> g_builtin_entries;
static std::mutex g_builtin_mutex;

int BuiltinRegistry::register_builtin(const std::string &name, BuiltinFn fn, void* ctx, TypeKind ret, const std::vector<TypeKind>& params) {
    std::lock_guard<std::mutex> lk(g_builtin_mutex);
    BuiltinEntry e;
    e.name = name;
    e.fn = fn;
    e.ctx = ctx;
    e.return_type = ret;
    e.param_types = params;
    g_builtin_entries.push_back(std::move(e));
    return (int)g_builtin_entries.size() - 1;
}

const BuiltinEntry* BuiltinRegistry::get_entry(int id) {
    std::lock_guard<std::mutex> lk(g_builtin_mutex);
    if (id < 0 || id >= (int)g_builtin_entries.size()) return nullptr;
    return &g_builtin_entries[id];
}

int BuiltinRegistry::lookup_name(const std::string &name) {
    std::lock_guard<std::mutex> lk(g_builtin_mutex);
    for (int i = 0; i < (int)g_builtin_entries.size(); ++i) {
        if (g_builtin_entries[i].name == name) return i;
    }
    return -1;
}

int BuiltinRegistry::lookup_name(const std::string &name, const std::vector<TypeKind>& params) {
    std::lock_guard<std::mutex> lk(g_builtin_mutex);
    for (int i = 0; i < (int)g_builtin_entries.size(); ++i) {
        if (g_builtin_entries[i].name != name) continue;
        if (g_builtin_entries[i].param_types.size() != params.size()) continue;
        bool ok = true;
        for (size_t j = 0; j < params.size(); ++j) {
            if (params[j] == TY_UNKNOWN) continue;
            if (g_builtin_entries[i].param_types[j] != TY_UNKNOWN && g_builtin_entries[i].param_types[j] != params[j]) { ok = false; break; }
        }
        if (ok) return i;
    }
    return lookup_name(name);
}

const std::vector<BuiltinEntry>& BuiltinRegistry::all_entries() {
    return g_builtin_entries;
}
