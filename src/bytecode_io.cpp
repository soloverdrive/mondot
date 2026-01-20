#include "bytecode_io.h"
#include "value.h"
#include "builtin_registry.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include "facts.h"

static constexpr uint8_t FILE_TAG_FUNC = 0x10;
static constexpr uint8_t FILE_TAG_LIST = 0x12;
static constexpr uint8_t FILE_TAG_STRUCT = 0x11;

void BytecodeIO::save(const std::string& filename, Assembler& as, bool alsoVisual) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) throw std::runtime_error("It was not possible to create a file " + filename);
    const char magic[] = "MDOT";
    out.write(magic, 4);

    std::function<void(const Value&)> write_value = [&](const Value& v) {
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
            else if (o->type == OBJ_STRUCT) {
                uint8_t tag = FILE_TAG_STRUCT;
                out.write((char*)&tag, 1);
                ObjStruct* os = (ObjStruct*)o;
                int32_t itemid = os->item_type_id;
                out.write((char*)&itemid, sizeof(int32_t));
                uint32_t fcount = (uint32_t)os->fields.size();
                out.write((char*)&fcount, sizeof(uint32_t));
                for (uint32_t i = 0; i < fcount; ++i) {
                    write_value(os->fields[i]);
                }
            }
            else if (o->type == OBJ_LIST) {
                uint8_t tag = FILE_TAG_LIST;
                out.write((char*)&tag, 1);
                ObjList* ol = (ObjList*)o;
                size_t cnt = ol->elements.size();
                out.write((char*)&cnt, sizeof(size_t));
                for (size_t i = 0; i < cnt; ++i) {
                    write_value(ol->elements[i]);
                }
            }
            else {
                uint8_t tag = TAG_NIL;
                out.write((char*)&tag, 1);
            }
        } else {
            uint8_t tag = TAG_NIL;
            out.write((char*)&tag, 1);
        }
    };

    size_t n_consts = as.constants.size();
    out.write((char*)&n_consts, sizeof(size_t));
    for (const auto& v : as.constants) {
        write_value(v);
    }

    size_t n_code = as.code.size();
    out.write((char*)&n_code, sizeof(size_t));
    out.write((char*)as.code.data(), n_code * sizeof(Instr));
    std::cout << "Compiled successfully for " << filename << std::endl;

    if (alsoVisual) {
        std::string txtfile = filename + ".txt";
        save_text(txtfile, as);
        std::cout << "Saved readable dump to " << txtfile << std::endl;
    }
}

void BytecodeIO::load(const std::string& filename, Assembler& as) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) throw std::runtime_error("File not found: " + filename);
    char magic[4];
    in.read(magic, 4);
    if (std::strncmp(magic, "MDOT", 4) != 0) throw std::runtime_error("Invalid file format (Magic Header)");

    static constexpr uint8_t FILE_TAG_STRUCT = 0x11;

    auto read_value = [&](auto&& self) -> Value {
        uint8_t tag; in.read((char*)&tag, 1);
        if (tag == TAG_NUM) {
            int64_t q; in.read((char*)&q, sizeof(int64_t));
            return Value::make_intscaled(q);
        }
        else if (tag == TAG_BOOL) {
            bool b; in.read((char*)&b, sizeof(bool));
            return Value::make_bool(b);
        }
        else if (tag == TAG_NIL) {
            return Value::make_nil();
        }
        else if (tag == TAG_OBJ) {
            size_t len; in.read((char*)&len, sizeof(size_t));
            std::string s(len, '\0');
            in.read(&s[0], len);
            return Value::make_obj(new ObjString(s));
        }
        else if (tag == FILE_TAG_FUNC) {
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
                    return Value::make_obj(of);
                }
            }
            if (!name.empty()) {
                int id = BuiltinRegistry::lookup_name(name, params);
                if (id >= 0) {
                    const BuiltinEntry* e = BuiltinRegistry::get_entry(id);
                    if (e) {
                        ObjFunction* of = new ObjFunction(id, e->return_type, e->param_types, e->name);
                        return Value::make_obj(of);
                    }
                }
            }
            return Value::make_nil();
        }
        else if (tag == FILE_TAG_STRUCT) {
            int32_t itemid; in.read((char*)&itemid, sizeof(int32_t));
            uint32_t fcount; in.read((char*)&fcount, sizeof(uint32_t));
            ObjStruct* os = new ObjStruct(itemid);
            os->fields.resize(fcount);
            for (uint32_t i = 0; i < fcount; ++i) {
                Value fv = self(self);
                os->fields[i] = fv;
                retain(os->fields[i]);
            }
            return Value::make_obj(os);
        }
        else if (tag == FILE_TAG_LIST) {
            size_t cnt; in.read((char*)&cnt, sizeof(size_t));
            ObjList* ol = new ObjList();
            ol->elements.resize(cnt);
            for (size_t i = 0; i < cnt; ++i) {
                Value ev = self(self);
                ol->elements[i] = ev;
                retain(ol->elements[i]);
            }
            return Value::make_obj(ol);
        }
        else
            throw std::runtime_error("Unknown constant tag in bytecode (load)");
    };

    size_t n_consts;
    in.read((char*)&n_consts, sizeof(size_t));
    for (size_t i = 0; i < n_consts; ++i) {
        Value v = read_value(read_value);
        as.add_constant(v);
    }

    size_t n_code;
    in.read((char*)&n_code, sizeof(size_t));
    as.code.resize(n_code);
    in.read((char*)as.code.data(), n_code * sizeof(Instr));
}

std::string BytecodeIO::escape_string(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '\\') { out += "\\\\"; }
        else if (c == '\"') { out += "\\\""; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else if (c == '\t') { out += "\\t"; }
        else if (std::isprint(c)) { out.push_back(c); }
        else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return out;
}

std::string BytecodeIO::instr_to_string(const Instr& i) {
    std::string s = opcode_to_string(i.op);
    s += " a=" + std::to_string((int)i.a);
    s += " b=" + std::to_string((int)i.b);
    s += " c=" + std::to_string((int)i.c);
    return s;
}

void BytecodeIO::save_text(const std::string& filename_txt, Assembler& as) {
    std::ofstream out(filename_txt);
    if (!out) {
        std::cerr << "Warning: could not create text dump " << filename_txt << std::endl;
        return;
    }

    out << "CONSTANTS (" << as.constants.size() << ")\n";
    for (size_t i = 0; i < as.constants.size(); ++i) {
        const Value& v = as.constants[i];
        out << i << " -> ";
        if (v.is_num()) {
            out << "NUM " << v.as_intscaled();
        } else if (v.is_bool()) {
            out << "BOOL " << (v.as_bool() ? "true" : "false");
        } else if (v.is_nil()) {
            out << "NIL";
        } else if (v.is_obj()) {
            Obj* o = v.as_obj();
            if (o->type == OBJ_STRING)
                out << "STRING \"" << escape_string(((ObjString*)o)->str) << "\"";
            else if (o->type == OBJ_FUNCTION) {
                ObjFunction* of = (ObjFunction*)o;
                out << "FUNC ";
                if (of->builtin_id >= 0) out << "[builtin#" << of->builtin_id << "]";
                out << of->name << " -> ret=" << (int)of->return_type << " params=";
                for (size_t k = 0; k < of->param_types.size(); ++k) {
                    if (k) out << ",";
                    out << (int)of->param_types[k];
                }
            } else
                out << "OBJ(type=" << (int)o->type << ")";
        } else
            out << "UNKNOWN_CONST";
        
        out << "\n";
    }

    out << "\n";
    for (size_t pc = 0; pc < as.code.size(); ++pc) {
        const Instr& ins = as.code[pc];
        out<< pc << "; " << instr_to_string(ins) << "\n";
    }

    out << std::flush;
}
