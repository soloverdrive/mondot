#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <utility>

using RawVal = uint64_t;
enum Tag : uint8_t { TAG_NIL=0, TAG_BOOL=1, TAG_NUM=2, TAG_OBJ=3 };

static constexpr int INTSCALED_SHIFT = 32;
static constexpr uint64_t INTSCALED_ONE = (1ULL << INTSCALED_SHIFT);

struct Obj;
struct ObjString;
struct ObjList;
struct ObjTable;
struct ObjFunction;

enum TypeKind { TY_UNKNOWN=0, TY_VOID=1, TY_NUMBER=2, TY_STRING=3, TY_BOOL=4, TY_LIST=5, TY_TABLE=6 };

inline TypeKind parse_type_name(const std::string& s) {
    if (s == "void") return TY_VOID;
    if (s == "number") return TY_NUMBER;
    if (s == "string") return TY_STRING;
    if (s == "bool") return TY_BOOL;
    if (s == "list") return TY_LIST;
    if (s == "table") return TY_TABLE;
    return TY_UNKNOWN;
}

struct Value {
    RawVal raw;
    static Value make_nil() { return { (RawVal)TAG_NIL }; }
    static Value make_bool(bool b) { return { (RawVal(uint64_t(b ? 1ULL : 0ULL)) << 3) | TAG_BOOL }; }

    static Value make_int(int64_t i) {
        int64_t q = (int64_t)(i) << INTSCALED_SHIFT;
        uint64_t uq = (uint64_t) q;
        return { (uq << 3) | TAG_NUM };
    }

    static Value make_intscaled(int64_t q) {
        uint64_t uq = (uint64_t) q;
        return { (uq << 3) | TAG_NUM };
    }

    static Value make_obj(Obj* p);

    bool is_nil() const { return (raw & 7) == TAG_NIL; }
    bool is_bool() const { return (raw & 7) == TAG_BOOL; }
    bool is_num() const { return (raw & 7) == TAG_NUM; }
    bool is_obj() const { return (raw & 7) == TAG_OBJ; }

    int64_t as_intscaled() const { return (int64_t)raw >> 3; }
    double as_num() const { return double(as_intscaled()) / double(INTSCALED_ONE); }
    bool as_bool() const { return (raw >> 3) != 0; }
    Obj* as_obj() const { return reinterpret_cast<Obj*>(raw & ~7ULL); }
};

enum ObjType { OBJ_STRING=1, OBJ_LIST=2, OBJ_TABLE=3, OBJ_FUNCTION=4 };

struct Obj {
    int type;
    int refcount;
    Obj(int t): type(t), refcount(1) {}
    virtual ~Obj() {}
};
struct ObjString : Obj {
    std::string str;
    ObjString(std::string s): Obj(OBJ_STRING), str(std::move(s)) {}
};
struct ObjList : Obj {
    std::vector<Value> elements;
    ObjList(): Obj(OBJ_LIST) {}
};
struct ObjTable : Obj {
    std::vector<std::pair<Value, Value>> entries;
    ObjTable(): Obj(OBJ_TABLE) {}
};

struct ObjFunction : Obj {
    int builtin_id;
    TypeKind return_type;
    std::vector<TypeKind> param_types;
    std::string name;

    ObjFunction(int bid = -1, TypeKind ret = TY_UNKNOWN, std::vector<TypeKind> params = {}, std::string nm = "")
      : Obj(OBJ_FUNCTION), builtin_id(bid), return_type(ret), param_types(std::move(params)), name(std::move(nm)) {}
};

inline Value Value::make_obj(Obj* p) {
    RawVal v = (RawVal) reinterpret_cast<uint64_t>(p);
    return { v | TAG_OBJ };
}

inline void retain(Value v) { if (v.is_obj()) v.as_obj()->refcount++; }
inline void release(Value v) {
    if (v.is_obj()) {
        Obj* o = v.as_obj();
        if (--o->refcount <= 0) delete o;
    }
}
inline TypeKind type_of_value(const Value& v) {
    if (v.is_num()) return TY_NUMBER;
    if (v.is_bool()) return TY_BOOL;
    if (v.is_obj()) {
        Obj* o = v.as_obj();
        if (!o) return TY_UNKNOWN;
        if (o->type == OBJ_STRING) return TY_STRING;
        if (o->type == OBJ_LIST) return TY_LIST;
        if (o->type == OBJ_TABLE) return TY_TABLE;
    }
    return TY_UNKNOWN;
}
