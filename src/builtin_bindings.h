#pragma once
#include <functional>
#include "builtin_registry.h"
#include <type_traits>
#include <cmath>

inline double value_to_number(const Value& v) {
    if (v.is_num()) return v.as_num();
    return 0.0;
}

inline std::string value_to_string(const Value& v) {
    if (v.is_obj() && v.as_obj()->type == OBJ_STRING) return ((ObjString*)v.as_obj())->str;
    return std::string();
}

inline Value number_to_value(double d) {
    int64_t q = (int64_t) llround(d * (long double)INTSCALED_ONE);
    return Value::make_intscaled(q);
}

inline Value string_to_value(const std::string& s) {
    ObjString* o = new ObjString(s);
    return Value::make_obj(o);
}
inline Value bool_to_value(bool b) { return Value::make_bool(b); }

template<typename R, typename... Args>
int register_native_simple(const std::string &name, R(*fn)(Args...),
                           TypeKind ret = TY_UNKNOWN, std::vector<TypeKind> params = {}) {
    auto *heap_fn = new std::function<R(Args...)>(fn);
    BuiltinFn bridge = [](int argc, const Value* argv, void* ctx)->Value {
        auto *h = (std::function<R(Args...)>*)ctx;
        return Value::make_nil();
    };
    int id = BuiltinRegistry::register_builtin(name, bridge, heap_fn, ret, params);
    return id;
}
