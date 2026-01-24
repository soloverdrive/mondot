#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <utility>
#include <cstddef>
#include <type_traits>

using RawVal = uint64_t;
enum Tag : uint8_t { TAG_NIL=0, TAG_BOOL=1, TAG_NUM=2, TAG_OBJ=3 };

static constexpr int INTSCALED_SHIFT = 32;
static constexpr uint64_t INTSCALED_ONE = (1ULL << INTSCALED_SHIFT);

struct Obj;
struct ObjString;
struct ObjList;
struct ObjTable;
struct ObjFunction;
struct ObjStruct;

enum TypeKind { TY_UNKNOWN=0, TY_VOID=1, TY_NUMBER=2, TY_STRING=3, TY_BOOL=4, TY_LIST=5, TY_TABLE=6, TY_ITEM=7 };

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

    static Value make_nil() { return { static_cast<RawVal>(TAG_NIL) }; }

    static Value make_bool(bool b) {
        RawVal payload = (static_cast<RawVal>(b ? 1ULL : 0ULL) << 3);
        return { payload | static_cast<RawVal>(TAG_BOOL) };
    }

    static Value make_int(int64_t i) {
        int64_t q = (int64_t(i) << INTSCALED_SHIFT);
        uint64_t uq = static_cast<uint64_t>(q);
        return { (uq << 3) | static_cast<RawVal>(TAG_NUM) };
    }

    static Value make_intscaled(int64_t q) {
        uint64_t uq = static_cast<uint64_t>(q);
        return { (uq << 3) | static_cast<RawVal>(TAG_NUM) };
    }

    static Value make_obj(Obj* p);

    bool is_nil() const { return raw == static_cast<RawVal>(TAG_NIL); }
    bool is_bool() const { return (raw & 7) == static_cast<RawVal>(TAG_BOOL); }
    bool is_num() const { return (raw & 7) == static_cast<RawVal>(TAG_NUM); }
    bool is_obj() const { return (raw & 7) == static_cast<RawVal>(TAG_OBJ); }

    int64_t as_intscaled() const { return static_cast<int64_t>(raw) >> 3; }
    double as_num() const { return double(as_intscaled()) / double(INTSCALED_ONE); }
    bool as_bool() const { return ((raw >> 3) != 0); }
    Obj* as_obj() const {
        uintptr_t ptrval = static_cast<uintptr_t>(raw & ~static_cast<RawVal>(7ULL));
        return reinterpret_cast<Obj*>(ptrval);
    }
};

enum ObjType { OBJ_STRING=1, OBJ_LIST=2, OBJ_TABLE=3, OBJ_FUNCTION=4, OBJ_STRUCT=5 };

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

struct ObjStruct : Obj {
    int item_type_id;
    std::vector<Value> fields;
    ObjStruct(int item_id = -1) : Obj(OBJ_STRUCT), item_type_id(item_id) {}
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
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    RawVal rv = static_cast<RawVal>(v);
    return { rv | static_cast<RawVal>(TAG_OBJ) };
}

inline void retain(Value v) {
    if (v.is_obj()) {
        Obj* o = v.as_obj();
        if (o) ++o->refcount;
    }
}
inline void release(Value v) {
    if (v.is_obj()) {
        Obj* o = v.as_obj();
        if (!o) return;
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
        if (o->type == OBJ_STRUCT) return TY_ITEM;
    }
    return TY_UNKNOWN;
}

inline bool value_equal(const Value& a, const Value& b) {
    // quick tag check
    uint8_t taga = static_cast<uint8_t>(a.raw & 7);
    uint8_t tagb = static_cast<uint8_t>(b.raw & 7);
    if (taga != tagb) return false;
    if (a.is_nil()) return true;
    if (a.is_bool()) return a.as_bool() == b.as_bool();
    if (a.is_num()) return a.as_intscaled() == b.as_intscaled();
    if (a.is_obj()) {
        Obj* oa = a.as_obj();
        Obj* ob = b.as_obj();
        if (!oa || !ob) return false;
        if (oa->type != ob->type) return false;
        if (oa->type == OBJ_STRING) {
            return ((ObjString*)oa)->str == ((ObjString*)ob)->str;
        }
        // design choice
        return oa == ob;
    }
    return false;
}
