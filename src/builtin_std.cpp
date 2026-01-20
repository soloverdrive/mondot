#include "builtin_std.h"
#include "builtin_registry.h"
// #include "builtin_bindings.h" // unused
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace {
    std::string value_to_short_string(const Value& v) {
        if (v.is_obj() && v.as_obj()->type == OBJ_STRING) return ((ObjString*)v.as_obj())->str;
        if (v.is_num()) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(6) << v.as_num();
            std::string out = ss.str();
            if (out.find('.') != std::string::npos) {
                while (!out.empty() && out.back() == '0') out.pop_back();
                if (!out.empty() && out.back() == '.') out.pop_back();
            }
            return out;
        }
        if (v.is_bool()) return v.as_bool() ? "true" : "false";
        if (v.is_obj() && v.as_obj()->type == OBJ_LIST) {
            ObjList* a = (ObjList*)v.as_obj();
            std::string s = "[";
            for (size_t i = 0; i < a->elements.size() && i < 8; ++i) {
                if (i) s += ", ";
                s += value_to_short_string(a->elements[i]);
            }
            if (a->elements.size() > 8) s += ", ...";
            s += "]";
            return s;
        }
        return "nil";
    }

    Value builtin_print_string(int argc, const Value* argv, [[maybe_unused]] void* ctx) {
        if (argc < 1) { std::cout << "\n"; return Value::make_nil(); }
        const Value& v = argv[0];
        if (v.is_obj() && v.as_obj()->type == OBJ_STRING) std::cout << ((ObjString*)v.as_obj())->str;
        else std::cout << value_to_short_string(v);
        std::cout << "\n";
        return Value::make_nil();
    }

    Value builtin_print_number(int argc, const Value* argv, [[maybe_unused]] void* ctx) {
        if (argc < 1) { std::cout << "\n"; return Value::make_nil(); }
        const Value& v = argv[0];
        if (!v.is_num()) { std::cout << "nil\n"; return Value::make_nil(); }
        double d = v.as_num();
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6) << d;
        std::string out = ss.str();
        if (out.find('.') != std::string::npos) {
            while (!out.empty() && out.back() == '0') out.pop_back();
            if (!out.empty() && out.back() == '.') out.pop_back();
        }
        std::cout << out << "\n";
        return Value::make_nil();
    }

    Value builtin_print_array(int argc, const Value* argv, [[maybe_unused]] void* ctx) {
        if (argc < 1) { std::cout << "[]\n"; return Value::make_nil(); }
        const Value& v = argv[0];
        if (!(v.is_obj() && v.as_obj()->type == OBJ_LIST)) { std::cout << "nil\n"; return Value::make_nil(); }
        ObjList* arr = (ObjList*)v.as_obj();
        std::cout << "[";
        for (size_t i = 0; i < arr->elements.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << value_to_short_string(arr->elements[i]);
        }
        std::cout << "]\n";
        return Value::make_nil();
    }

    Value builtin_len_string(int argc, const Value* argv, [[maybe_unused]] void* ctx) {
        if (argc < 1) return Value::make_nil();
        const Value& v = argv[0];
        if (!(v.is_obj() && v.as_obj()->type == OBJ_STRING)) return Value::make_nil();
        ObjString* s = (ObjString*)v.as_obj();
        int64_t len = (int64_t)s->str.size();
        return Value::make_int(len);
    }

    Value builtin_sin_1(int argc, const Value* argv, [[maybe_unused]] void* ctx) {
        if (argc < 1 || !argv[0].is_num()) return Value::make_nil();
        double x = argv[0].as_num();
        double r = sin(x);
        int64_t q = (int64_t) llround(r * (long double)INTSCALED_ONE);
        return Value::make_intscaled(q);
    }

    Value builtin_cos_1(int argc, const Value* argv, [[maybe_unused]] void* ctx) {
        if (argc < 1 || !argv[0].is_num()) return Value::make_nil();
        double x = argv[0].as_num();
        double r = cos(x);
        int64_t q = (int64_t) llround(r * (long double)INTSCALED_ONE);
        return Value::make_intscaled(q);
    }
}

void register_default_builtins() {
    BuiltinRegistry::register_builtin("print", &builtin_print_string, nullptr, TY_VOID, {TY_STRING});
    BuiltinRegistry::register_builtin("print", &builtin_print_number, nullptr, TY_VOID, {TY_NUMBER});
    BuiltinRegistry::register_builtin("print", &builtin_print_array, nullptr, TY_VOID, {TY_LIST});
    BuiltinRegistry::register_builtin("len", &builtin_len_string, nullptr, TY_NUMBER, {TY_STRING});
    BuiltinRegistry::register_builtin("sin", &builtin_sin_1, nullptr, TY_NUMBER, {TY_NUMBER});
    BuiltinRegistry::register_builtin("cos", &builtin_cos_1, nullptr, TY_NUMBER, {TY_NUMBER});
}
