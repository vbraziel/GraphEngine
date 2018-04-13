#include "GraphEngine.Jit.Native.h"

typedef void(*JitProc)(X86Compiler&, FuncCtx&, VerbSequence&);

static JitRuntime s_runtime;
static std::map<VerbCode, JitProc> s_jitprocs{
    {VerbCode::VC_BGet       , BGet},
    {VerbCode::VC_BSet       , BSet},
    {VerbCode::VC_SGet       , SGet},
    {VerbCode::VC_SSet       , SSet},
    {VerbCode::VC_GSGet      , GSGet},
    {VerbCode::VC_GSSet      , GSSet},
    {VerbCode::VC_LGet       , LGet},
    {VerbCode::VC_LSet       , LSet},
    {VerbCode::VC_LInlineGet , LInlineGet},
    {VerbCode::VC_LInlineSet , LInlineSet},
    {VerbCode::VC_LContains  , LContains},
    {VerbCode::VC_LCount     , LCount},
};

static std::map<TypeCode, TypeId::Id> s_atom_typemap{
    {TypeCode::TC_U8         , TypeId::kU8 },
    {TypeCode::TC_U16        , TypeId::kU16 },
    {TypeCode::TC_U32        , TypeId::kU32 },
    {TypeCode::TC_U64        , TypeId::kU64 },
    {TypeCode::TC_I8         , TypeId::kI8 },
    {TypeCode::TC_I16        , TypeId::kI16 },
    {TypeCode::TC_I32        , TypeId::kI32 },
    {TypeCode::TC_I64        , TypeId::kI64 },
    {TypeCode::TC_F32        , TypeId::kF32 },
    {TypeCode::TC_F64        , TypeId::kF64 },
    {TypeCode::TC_BOOL       , TypeId::kU8 },
    {TypeCode::TC_CHAR       , TypeId::kU16 },
};

TypeId::Id _get_typeid(IN TypeDescriptor* const type)
{
    auto c = static_cast<TypeCode>(type->get_TypeCode());
    auto i = s_atom_typemap.find(c);
    return i != s_atom_typemap.cend() ? i->second : TypeId::kUIntPtr;
}

TypeId::Id _get_retid(IN FunctionDescriptor* fdesc)
{
    if (fdesc->Verbs->is_setter()) return TypeId::kVoid;

    TypeId::Id ret;
    VerbSequence seq(fdesc);
    while (seq.Next())
    {
        switch (seq.CurrentVerb()->Code) {
        case VerbCode::VC_BGet:
            return JitTypeId();
        case VerbCode::VC_LGet:
        case VerbCode::VC_LInlineGet:
        case VerbCode::VC_SGet:
        case VerbCode::VC_GSGet:
            ret = JitTypeId();
            break;
        case VerbCode::VC_LContains:
            return TypeId::kU8;
        case VerbCode::VC_LCount:
            return TypeId::kI32;
        default:
            throw;
        }
    }

    return ret;
}

void _get_args(IN FunctionDescriptor* fdesc, OUT uint8_t* &pargs, OUT int32_t& nargs)
{
    std::vector<uint8_t> vec{ TypeId::kUIntPtr };
    VerbSequence seq(fdesc);
    while (seq.Next())
    {
        switch (seq.CurrentVerb()->Code) {
        case VerbCode::VC_BGet:
            goto out;
        case VerbCode::VC_BSet:
            vec.push_back(JitTypeId());
            goto out;

        case VerbCode::VC_LGet:
            vec.push_back(TypeId::kI32);
            break;

        case VerbCode::VC_LSet:
            vec.push_back(TypeId::kI32);
            /* FALLTHROUGH */
        case VerbCode::VC_LInlineSet:  // no indexer
            vec.push_back(JitTypeId());
            goto out;

        case VerbCode::VC_LContains:
            vec.push_back(JitTypeId());
            goto out;

        case VerbCode::VC_GSGet:
            vec.push_back(TypeId::kUIntPtr);  // char* member_name
            break;
        case VerbCode::VC_SGet:
            vec.push_back(TypeId::kUIntPtr);  // char* member_name
            vec.push_back(JitTypeId()); // then push back the set value
            goto out;
        default:
            throw;
        }
    }

out:

    nargs = vec.size();
    pargs = (uint8_t*)malloc(nargs * sizeof(uint8_t));
    std::copy(vec.begin(), vec.end(), pargs);
}

JitRoutine(Dispatch) { s_jitprocs[seq.CurrentVerb()->Code](cc, ctx, seq); }

DLL_EXPORT void* CompileFunctionToNative(FunctionDescriptor* fdesc)
{
    printf("Enter CompileFunctionToNative");

    CodeHolder      code;
    CodeInfo        ci = s_runtime.getCodeInfo();
    CallConv::Id    ccId = CallConv::kIdHost;
    void*           ret = nullptr;
    FuncSignature   fsig;
    TypeId::Id      retId;
    uint8_t*        pargs;
    int32_t         nargs;
    CCFunc*         func;

    if (code.init(ci)) return nullptr;
    X86Compiler cc(&code);

    retId = _get_typeid(&fdesc->Type);
    _get_args(fdesc, pargs, nargs);

    fsig.init(ccId, retId, pargs, nargs);
    func = cc.addFunc(fsig);

    // 1st arg is always CellAccessor* cellAccessor
    FuncCtx fctx(cc);
    VerbSequence seq(fdesc);
    while (seq.Next() && !fctx.returned)
    {
        Dispatch(cc, fctx, seq);
    }

    if (fctx.finalize()) return nullptr;
    if (s_runtime.add(&ret, &code)) return nullptr;

    fdesc->~FunctionDescriptor();
    free(fdesc);

    return ret;
}
