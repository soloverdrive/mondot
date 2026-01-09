#include "bytecode_io.h"
#include "value.h"
#include "builtin_registry.h"
#include <fstream>
#include <iostream>
#include <cstring>

static constexpr uint8_t FILE_TAG_FUNC = 0x10;

void BytecodeIO::save(const std::string& filename, Assembler& as) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) throw std::runtime_error("It was not possible to create a file " + filename);
    const char magic[] = "MDOT";
    out.write(magic, 4);

    size_t n_consts = as.constants.size();
    out.write((char*)&n_consts, sizeof(size_t));
    for (const auto& v : as.constants) {
        if (v.is_num()) {
            uint8_t tag = TAG_NUM;
            int64_t q = v.as_intscaled();
            out.write((char*)&tag, 1);
            out.write((char*)&q, sizeof(int64_t));
        } else if (v.is_bool()) {
            uint8_t tag = TAG_BOOL;
            bool val = v.as_bool();
            out.write((char*)&tag, 1);
            out.write((char*)&val, sizeof(bool));
        } else if (v.is_nil()) {
            uint8_t tag = TAG_NIL;
            out.write((char*)&tag, 1);
        } else if (v.is_obj()) {
            Obj* o = v.as_obj();
            if (o->type == OBJ_STRING) {
                uint8_t tag = TAG_OBJ;
                std::string s = ((ObjString*)o)->str;
                out.write((char*)&tag, 1);
                size_t len = s.size();
                out.write((char*)&len, sizeof(size_t));
                out.write(s.c_str(), len);
            }
            else if (o->type == OBJ_FUNCTION) {
                uint8_t tag = FILE_TAG_FUNC;
                out.write((char*)&tag, 1);
                ObjFunction* of = (ObjFunction*)o;
                int32_t bid = of->builtin_id;
                out.write((char*)&bid, sizeof(int32_t));
                uint8_t ret = (uint8_t)of->return_type;
                out.write((char*)&ret, 1);
                uint8_t argc = (uint8_t)of->param_types.size();
                out.write((char*)&argc, 1);
                for (auto t : of->param_types) {
                    uint8_t tb = (uint8_t)t;
                    out.write((char*)&tb, 1);
                }
                if (bid == -1) {
                    size_t len = of->name.size();
                    out.write((char*)&len, sizeof(size_t));
                    out.write(of->name.c_str(), len);
                }
            }
        }
    }

    size_t n_code = as.code.size();
    out.write((char*)&n_code, sizeof(size_t));
    out.write((char*)as.code.data(), n_code * sizeof(Instr));
    std::cout << "Compiled successfully for " << filename << std::endl;
}

void BytecodeIO::load(const std::string& filename, Assembler& as) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) throw std::runtime_error("File not found: " + filename);
    char magic[4];
    in.read(magic, 4);
    if (std::strncmp(magic, "MDOT", 4) != 0) throw std::runtime_error("Invalid file format (Magic Header)");

    size_t n_consts;
    in.read((char*)&n_consts, sizeof(size_t));
    for (size_t i = 0; i < n_consts; ++i) {
        uint8_t tag; in.read((char*)&tag, 1);
        if (tag == TAG_NUM) {
            int64_t q; in.read((char*)&q, sizeof(int64_t));
            as.add_constant(Value::make_intscaled(q));
        } else if (tag == TAG_BOOL) {
            bool val; in.read((char*)&val, sizeof(bool));
            as.add_constant(Value::make_bool(val));
        }
        else if (tag == TAG_NIL) as.add_constant(Value::make_nil());
        else if (tag == TAG_OBJ) {
            size_t len; in.read((char*)&len, sizeof(size_t));
            std::string s(len, '\0');
            in.read(&s[0], len);
            as.add_constant(Value::make_obj(new ObjString(s)));
        } else if (tag == FILE_TAG_FUNC) {
            int32_t bid; in.read((char*)&bid, sizeof(int32_t));
            uint8_t ret; in.read((char*)&ret, 1);
            uint8_t argc; in.read((char*)&argc, 1);
            std::vector<TypeKind> params;
            params.reserve(argc);
            for (int j=0;j<argc;j++) { uint8_t tb; in.read((char*)&tb,1); params.push_back((TypeKind)tb); }
            std::string name;
            if (bid == -1) {
                size_t len; in.read((char*)&len, sizeof(size_t));
                name.assign(len, '\0'); in.read(&name[0], len);
            }
            if (bid >= 0) {
                const BuiltinEntry* e = BuiltinRegistry::get_entry(bid);
                if (e) {
                    ObjFunction* of = new ObjFunction(bid, e->return_type, e->param_types, e->name);
                    as.add_constant(Value::make_obj(of));
                    continue;
                }
            }
            if (!name.empty()) {
                int id = BuiltinRegistry::lookup_name(name, params);
                if (id >= 0) {
                    const BuiltinEntry* e = BuiltinRegistry::get_entry(id);
                    if (e) {
                        ObjFunction* of = new ObjFunction(id, e->return_type, e->param_types, e->name);
                        as.add_constant(Value::make_obj(of));
                        continue;
                    }
                }
            }
            as.add_constant(Value::make_nil());
        } else throw std::runtime_error("Unknown constant tag in bytecode");
    }

    size_t n_code;
    in.read((char*)&n_code, sizeof(size_t));
    as.code.resize(n_code);
    in.read((char*)as.code.data(), n_code * sizeof(Instr));
}
