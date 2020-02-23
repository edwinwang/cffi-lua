#include <limits>
#include <type_traits>

#include "ffi.hh"

namespace ffi {

template<typename T>
static inline bool use_ffi_signed(int cv) {
    if (cv & parser::C_CV_SIGNED) {
        return true;
    }
    if (cv & parser::C_CV_UNSIGNED) {
        return false;
    }
    return std::numeric_limits<T>::is_signed;
}

ffi_type *get_ffi_type(parser::c_type const &tp) {
    int cv = tp.cv();

#define INT_CASE(bname, rtype, ftype) \
    case parser::C_BUILTIN_##bname: \
        if (use_ffi_signed<rtype>(cv)) { \
            return &ffi_type_s##ftype; \
        } else { \
            return &ffi_type_u##ftype; \
        }

#define INT_CASE64(bname, rtype) \
    case parser::C_BUILTIN_##bname: \
        if (sizeof(rtype) == 8) { \
            if (cv & parser::C_CV_SIGNED) { \
                return &ffi_type_sint64; \
            } else { \
                return &ffi_type_uint64; \
            } \
        } else if (sizeof(rtype) == 4) { \
            if (cv & parser::C_CV_SIGNED) { \
                return &ffi_type_sint32; \
            } else { \
                return &ffi_type_uint32; \
            } \
        } else if (sizeof(rtype) == 2) { \
            if (cv & parser::C_CV_SIGNED) { \
                return &ffi_type_sint16; \
            } else { \
                return &ffi_type_uint16; \
            } \
        } else { \
            if (cv & parser::C_CV_SIGNED) { \
                return &ffi_type_sint8; \
            } else { \
                return &ffi_type_uint8; \
            } \
        }

    switch (tp.type()) {
        case parser::C_BUILTIN_PTR:
            return &ffi_type_pointer;

        INT_CASE(CHAR, char, char)
        INT_CASE(SHORT, short, short)
        INT_CASE(INT, int, int)
        INT_CASE(LONG, long, long)
        INT_CASE(LLONG, int64_t, int64)
    
        INT_CASE(INT8, int8_t, int8)
        INT_CASE(INT16, int16_t, int16)
        INT_CASE(INT32, int32_t, int32)
        INT_CASE(INT64, int64_t, int64)

        INT_CASE64(SIZE, size_t)
        INT_CASE64(INTPTR, intptr_t)

        case parser::C_BUILTIN_TIME:
            /* FIXME: time_t may be represented in other ways too */
            if (sizeof(time_t) == 8) {
                return &ffi_type_sint64;
            } else {
                return &ffi_type_sint32;
            }

        case parser::C_BUILTIN_FLOAT:
            return &ffi_type_float;
        case parser::C_BUILTIN_DOUBLE:
            return &ffi_type_double;
        case parser::C_BUILTIN_LDOUBLE:
            /* FIXME: this may not be defined */
            return &ffi_type_longdouble;

        case parser::C_BUILTIN_BOOL:
            /* hmm... */
            return &ffi_type_uchar;

        default:
            break;
    }

#undef INT_CASE

    /* TODO: custom types */
    return &ffi_type_sint;
}

void lua_push_cdata(lua_State *L, parser::c_type const &tp, ffi_arg value) {
    switch (tp.type()) {
        /* convert to lua boolean */
        case parser::C_BUILTIN_BOOL:
            lua_pushboolean(L, int(value));
            return;
        /* convert to lua number */
        case parser::C_BUILTIN_FLOAT:
        case parser::C_BUILTIN_DOUBLE:
            lua_pushnumber(L, *reinterpret_cast<double *>(&value));
            return;
        case parser::C_BUILTIN_LDOUBLE:
            lua_pushstring(L, "NYI");
            return;
        case parser::C_BUILTIN_CHAR:
            if (use_ffi_signed<char>(tp.cv())) {
                lua_pushinteger(L, lua_Integer(
                    *reinterpret_cast<ffi_sarg *>(&value)
                ));
            } else {
                lua_pushinteger(L, lua_Integer(value));
            }
            return;
        case parser::C_BUILTIN_SHORT:
        case parser::C_BUILTIN_INT:
        case parser::C_BUILTIN_INT8:
        case parser::C_BUILTIN_INT16:
        case parser::C_BUILTIN_INT32:
            if (use_ffi_signed<int>(tp.cv())) {
                lua_pushinteger(L, lua_Integer(
                    *reinterpret_cast<ffi_sarg *>(&value)
                ));
            } else {
                lua_pushinteger(L, lua_Integer(value));
            }
            return;
        case parser::C_BUILTIN_LONG:
        case parser::C_BUILTIN_INT64:
        case parser::C_BUILTIN_SIZE:
        case parser::C_BUILTIN_INTPTR:
        case parser::C_BUILTIN_TIME:
            lua_pushstring(L, "NYI");
            return;
        default:
            /* FIXME */
            lua_pushstring(L, "value too big");
            return;
    }
}

template<typename T>
static inline void write_int(lua_State *L, int index, int cv, void **stor) {
    lua_Integer v = lua_tointeger(L, index);
    if (use_ffi_signed<T>(cv)) {
        using U = typename std::make_signed<T>::type;
        *reinterpret_cast<U *>(stor) = U(v);
    } else {
        using U = typename std::make_unsigned<T>::type;
        *reinterpret_cast<U *>(stor) = U(v);
    }
}

void lua_check_cdata(
    lua_State *L, parser::c_type const &tp, void **stor, int index
) {
    switch (lua_type(L, index)) {
        case LUA_TNIL:
            switch (tp.type()) {
                case parser::C_BUILTIN_PTR:
                    *stor = nullptr;
                    break;
                default:
                    luaL_error(L, "bad conversion");
                    break;
            }
            break;
        case LUA_TBOOLEAN:
            switch (tp.type()) {
                case parser::C_BUILTIN_BOOL:
                    *reinterpret_cast<bool *>(stor) = lua_toboolean(L, index);
                    break;
                default:
                    luaL_error(L, "bad conversion");
                    break;
            }
            break;
        case LUA_TNUMBER:
            switch (tp.type()) {
                case parser::C_BUILTIN_FLOAT:
                    *reinterpret_cast<float *>(stor) =
                        float(lua_tonumber(L, index));
                    break;
                case parser::C_BUILTIN_DOUBLE:
                    *reinterpret_cast<double *>(stor) =
                        double(lua_tonumber(L, index));
                    break;
                case parser::C_BUILTIN_CHAR:
                    write_int<char>(L, index, tp.cv(), stor);
                    break;
                case parser::C_BUILTIN_SHORT:
                    write_int<short>(L, index, tp.cv(), stor);
                    break;
                case parser::C_BUILTIN_INT:
                    write_int<int>(L, index, tp.cv(), stor);
                    break;
                case parser::C_BUILTIN_LONG:
                    write_int<long>(L, index, tp.cv(), stor);
                    break;
                case parser::C_BUILTIN_LLONG:
                    write_int<long long>(L, index, tp.cv(), stor);
                    break;
                case parser::C_BUILTIN_INT8:
                    write_int<int8_t>(L, index, tp.cv(), stor);
                    break;
                case parser::C_BUILTIN_INT16:
                    write_int<int16_t>(L, index, tp.cv(), stor);
                    break;
                case parser::C_BUILTIN_INT32:
                    write_int<int32_t>(L, index, tp.cv(), stor);
                    break;
                case parser::C_BUILTIN_INT64:
                    write_int<int64_t>(L, index, tp.cv(), stor);
                    break;
                default:
                    luaL_error(L, "bad conversion");
                    break;
            }
            break;
        case LUA_TSTRING:
            switch (tp.type()) {
                case parser::C_BUILTIN_PTR:
                    *reinterpret_cast<char const **>(
                        const_cast<void const **>(stor)
                    ) = lua_tostring(L, index);
                    break;
                default:
                    luaL_error(L, "bad conversion");
                    break;
            }
            break;
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            *stor = lua_touserdata(L, index);
            break;
        case LUA_TTHREAD:
            luaL_error(L, "bad conversion");
            break;
        case LUA_TTABLE:
            luaL_error(L, "table initializers not yet implemented");
            break;
        case LUA_TFUNCTION:
            luaL_error(L, "callbacks not yet implemented");
            break;
        default:
            luaL_error(
                L, "'%s' cannot be used in FFI",
                lua_typename(L, lua_type(L, index))
            );
            break;
    }
}

} /* namespace ffi */