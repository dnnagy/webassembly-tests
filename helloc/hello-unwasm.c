#include <math.h>
#include <string.h>

#include "hello-unwasm.h"
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define LIKELY(x) __builtin_expect(!!(x), 1)

#define TRAP(x) (wasm_rt_trap(WASM_RT_TRAP_##x), 0)

#define FUNC_PROLOGUE                                            \
  if (++wasm_rt_call_stack_depth > WASM_RT_MAX_CALL_STACK_DEPTH) \
    TRAP(EXHAUSTION)

#define FUNC_EPILOGUE --wasm_rt_call_stack_depth

#define UNREACHABLE TRAP(UNREACHABLE)

#define CALL_INDIRECT(table, t, ft, x, ...)          \
  (LIKELY((x) < table.size && table.data[x].func &&  \
          table.data[x].func_type == func_types[ft]) \
       ? ((t)table.data[x].func)(__VA_ARGS__)        \
       : TRAP(CALL_INDIRECT))

#define MEMCHECK(mem, a, t)  \
  if (UNLIKELY((a) + sizeof(t) > mem->size)) TRAP(OOB)

#define DEFINE_LOAD(name, t1, t2, t3)              \
  static inline t3 name(wasm_rt_memory_t* mem, u64 addr) {   \
    MEMCHECK(mem, addr, t1);                       \
    t1 result;                                     \
    memcpy(&result, &mem->data[addr], sizeof(t1)); \
    return (t3)(t2)result;                         \
  }

#define DEFINE_STORE(name, t1, t2)                           \
  static inline void name(wasm_rt_memory_t* mem, u64 addr, t2 value) { \
    MEMCHECK(mem, addr, t1);                                 \
    t1 wrapped = (t1)value;                                  \
    memcpy(&mem->data[addr], &wrapped, sizeof(t1));          \
  }

DEFINE_LOAD(i32_load, u32, u32, u32);
DEFINE_LOAD(i64_load, u64, u64, u64);
DEFINE_LOAD(f32_load, f32, f32, f32);
DEFINE_LOAD(f64_load, f64, f64, f64);
DEFINE_LOAD(i32_load8_s, s8, s32, u32);
DEFINE_LOAD(i64_load8_s, s8, s64, u64);
DEFINE_LOAD(i32_load8_u, u8, u32, u32);
DEFINE_LOAD(i64_load8_u, u8, u64, u64);
DEFINE_LOAD(i32_load16_s, s16, s32, u32);
DEFINE_LOAD(i64_load16_s, s16, s64, u64);
DEFINE_LOAD(i32_load16_u, u16, u32, u32);
DEFINE_LOAD(i64_load16_u, u16, u64, u64);
DEFINE_LOAD(i64_load32_s, s32, s64, u64);
DEFINE_LOAD(i64_load32_u, u32, u64, u64);
DEFINE_STORE(i32_store, u32, u32);
DEFINE_STORE(i64_store, u64, u64);
DEFINE_STORE(f32_store, f32, f32);
DEFINE_STORE(f64_store, f64, f64);
DEFINE_STORE(i32_store8, u8, u32);
DEFINE_STORE(i32_store16, u16, u32);
DEFINE_STORE(i64_store8, u8, u64);
DEFINE_STORE(i64_store16, u16, u64);
DEFINE_STORE(i64_store32, u32, u64);

#define I32_CLZ(x) ((x) ? __builtin_clz(x) : 32)
#define I64_CLZ(x) ((x) ? __builtin_clzll(x) : 64)
#define I32_CTZ(x) ((x) ? __builtin_ctz(x) : 32)
#define I64_CTZ(x) ((x) ? __builtin_ctzll(x) : 64)
#define I32_POPCNT(x) (__builtin_popcount(x))
#define I64_POPCNT(x) (__builtin_popcountll(x))

#define DIV_S(ut, min, x, y)                                 \
   ((UNLIKELY((y) == 0)) ?                TRAP(DIV_BY_ZERO)  \
  : (UNLIKELY((x) == min && (y) == -1)) ? TRAP(INT_OVERFLOW) \
  : (ut)((x) / (y)))

#define REM_S(ut, min, x, y)                                \
   ((UNLIKELY((y) == 0)) ?                TRAP(DIV_BY_ZERO) \
  : (UNLIKELY((x) == min && (y) == -1)) ? 0                 \
  : (ut)((x) % (y)))

#define I32_DIV_S(x, y) DIV_S(u32, INT32_MIN, (s32)x, (s32)y)
#define I64_DIV_S(x, y) DIV_S(u64, INT64_MIN, (s64)x, (s64)y)
#define I32_REM_S(x, y) REM_S(u32, INT32_MIN, (s32)x, (s32)y)
#define I64_REM_S(x, y) REM_S(u64, INT64_MIN, (s64)x, (s64)y)

#define DIVREM_U(op, x, y) \
  ((UNLIKELY((y) == 0)) ? TRAP(DIV_BY_ZERO) : ((x) op (y)))

#define DIV_U(x, y) DIVREM_U(/, x, y)
#define REM_U(x, y) DIVREM_U(%, x, y)

#define ROTL(x, y, mask) \
  (((x) << ((y) & (mask))) | ((x) >> (((mask) - (y) + 1) & (mask))))
#define ROTR(x, y, mask) \
  (((x) >> ((y) & (mask))) | ((x) << (((mask) - (y) + 1) & (mask))))

#define I32_ROTL(x, y) ROTL(x, y, 31)
#define I64_ROTL(x, y) ROTL(x, y, 63)
#define I32_ROTR(x, y) ROTR(x, y, 31)
#define I64_ROTR(x, y) ROTR(x, y, 63)

#define FMIN(x, y)                                          \
   ((UNLIKELY((x) != (x))) ? NAN                            \
  : (UNLIKELY((y) != (y))) ? NAN                            \
  : (UNLIKELY((x) == 0 && (y) == 0)) ? (signbit(x) ? x : y) \
  : (x < y) ? x : y)

#define FMAX(x, y)                                          \
   ((UNLIKELY((x) != (x))) ? NAN                            \
  : (UNLIKELY((y) != (y))) ? NAN                            \
  : (UNLIKELY((x) == 0 && (y) == 0)) ? (signbit(x) ? y : x) \
  : (x > y) ? x : y)

#define TRUNC_S(ut, st, ft, min, max, maxop, x)                             \
   ((UNLIKELY((x) != (x))) ? TRAP(INVALID_CONVERSION)                       \
  : (UNLIKELY((x) < (ft)(min) || (x) maxop (ft)(max))) ? TRAP(INT_OVERFLOW) \
  : (ut)(st)(x))

#define I32_TRUNC_S_F32(x) TRUNC_S(u32, s32, f32, INT32_MIN, INT32_MAX, >=, x)
#define I64_TRUNC_S_F32(x) TRUNC_S(u64, s64, f32, INT64_MIN, INT64_MAX, >=, x)
#define I32_TRUNC_S_F64(x) TRUNC_S(u32, s32, f64, INT32_MIN, INT32_MAX, >,  x)
#define I64_TRUNC_S_F64(x) TRUNC_S(u64, s64, f64, INT64_MIN, INT64_MAX, >=, x)

#define TRUNC_U(ut, ft, max, maxop, x)                                    \
   ((UNLIKELY((x) != (x))) ? TRAP(INVALID_CONVERSION)                     \
  : (UNLIKELY((x) <= (ft)-1 || (x) maxop (ft)(max))) ? TRAP(INT_OVERFLOW) \
  : (ut)(x))

#define I32_TRUNC_U_F32(x) TRUNC_U(u32, f32, UINT32_MAX, >=, x)
#define I64_TRUNC_U_F32(x) TRUNC_U(u64, f32, UINT64_MAX, >=, x)
#define I32_TRUNC_U_F64(x) TRUNC_U(u32, f64, UINT32_MAX, >,  x)
#define I64_TRUNC_U_F64(x) TRUNC_U(u64, f64, UINT64_MAX, >=, x)

#define DEFINE_REINTERPRET(name, t1, t2)  \
  static inline t2 name(t1 x) {           \
    t2 result;                            \
    memcpy(&result, &x, sizeof(result));  \
    return result;                        \
  }

DEFINE_REINTERPRET(f32_reinterpret_i32, u32, f32)
DEFINE_REINTERPRET(i32_reinterpret_f32, f32, u32)
DEFINE_REINTERPRET(f64_reinterpret_i64, u64, f64)
DEFINE_REINTERPRET(i64_reinterpret_f64, f64, u64)


static u32 func_types[35];

static void init_func_types(void) {
  func_types[0] = wasm_rt_register_func_type(3, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[1] = wasm_rt_register_func_type(6, 1, WASM_RT_I32, WASM_RT_F64, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[2] = wasm_rt_register_func_type(2, 0, WASM_RT_I32, WASM_RT_I32);
  func_types[3] = wasm_rt_register_func_type(3, 1, WASM_RT_I32, WASM_RT_I64, WASM_RT_I32, WASM_RT_I64);
  func_types[4] = wasm_rt_register_func_type(4, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[5] = wasm_rt_register_func_type(1, 0, WASM_RT_I32);
  func_types[6] = wasm_rt_register_func_type(0, 1, WASM_RT_I32);
  func_types[7] = wasm_rt_register_func_type(1, 1, WASM_RT_I32, WASM_RT_I32);
  func_types[8] = wasm_rt_register_func_type(0, 0);
  func_types[9] = wasm_rt_register_func_type(2, 1, WASM_RT_F64, WASM_RT_F64, WASM_RT_F64);
  func_types[10] = wasm_rt_register_func_type(2, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[11] = wasm_rt_register_func_type(2, 1, WASM_RT_F64, WASM_RT_I32, WASM_RT_F64);
  func_types[12] = wasm_rt_register_func_type(5, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[13] = wasm_rt_register_func_type(7, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[14] = wasm_rt_register_func_type(3, 0, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[15] = wasm_rt_register_func_type(4, 0, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[16] = wasm_rt_register_func_type(5, 0, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[17] = wasm_rt_register_func_type(2, 1, WASM_RT_I64, WASM_RT_I32, WASM_RT_I32);
  func_types[18] = wasm_rt_register_func_type(3, 1, WASM_RT_I64, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[19] = wasm_rt_register_func_type(1, 1, WASM_RT_F64, WASM_RT_I64);
  func_types[20] = wasm_rt_register_func_type(4, 0, WASM_RT_I32, WASM_RT_I64, WASM_RT_I64, WASM_RT_I32);
  func_types[21] = wasm_rt_register_func_type(2, 1, WASM_RT_I64, WASM_RT_I64, WASM_RT_F64);
  func_types[22] = wasm_rt_register_func_type(1, 1, WASM_RT_I32, WASM_RT_I32);
  func_types[23] = wasm_rt_register_func_type(3, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[24] = wasm_rt_register_func_type(3, 1, WASM_RT_I32, WASM_RT_I64, WASM_RT_I32, WASM_RT_I64);
  func_types[25] = wasm_rt_register_func_type(6, 1, WASM_RT_I32, WASM_RT_F64, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[26] = wasm_rt_register_func_type(2, 0, WASM_RT_I32, WASM_RT_I32);
  func_types[27] = wasm_rt_register_func_type(1, 0, WASM_RT_I32);
  func_types[28] = wasm_rt_register_func_type(0, 1, WASM_RT_I32);
  func_types[29] = wasm_rt_register_func_type(2, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[30] = wasm_rt_register_func_type(4, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[31] = wasm_rt_register_func_type(4, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I64, WASM_RT_I32, WASM_RT_I64);
  func_types[32] = wasm_rt_register_func_type(7, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_F64, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[33] = wasm_rt_register_func_type(3, 0, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
  func_types[34] = wasm_rt_register_func_type(5, 1, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32, WASM_RT_I32);
}

static u32 f6(void);
static void __wasm_call_ctors(void);
static void sayHello(void);
static f64 add(f64, f64);
static u32 greet(u32);
static u32 f11(u32, u32);
static u32 f12(u32, u32, u32);
static u32 f13(u32);
static u64 f14(u32, u64, u32);
static u32 f15(u32);
static void f16(u32);
static u32 __errno_location(void);
static u32 f18(u32);
static u32 f19(void);
static u32 f20(u32, u32, u32);
static u32 f21(void);
static u32 f22(u32, u32);
static f64 f23(f64, u32);
static u32 f24(void);
static void f25(void);
static u32 f26(u32);
static u32 f27(u32, u32, u32);
static u32 f28(u32, u32, u32, u32, u32);
static u32 f29(u32, u32, u32, u32, u32, u32, u32);
static void f30(u32, u32, u32);
static u32 f31(u32);
static void f32_0(u32, u32, u32, u32);
static void f33(u32, u32, u32, u32, u32);
static u32 f34(u64, u32);
static u32 f35(u64, u32, u32);
static u32 f36(u64, u32);
static u32 f37(u32, u32, u32);
static u32 f38(u32, f64, u32, u32, u32, u32);
static void f39(u32, u32);
static u64 f40(f64);
static u32 f41(u32, u32, u32);
static u32 f42(u32, u32);
static u32 f43(u32, u32);
static u32 f44(u32, u32);
static u32 f45(u32);
static u32 f46(u32);
static void f47(u32, u64, u64, u32);
static void f48(u32, u64, u64, u32);
static f64 f49(u64, u64);
static u32 malloc(u32);
static void free(u32);
static u32 f52(u32);
static u32 f53(u32, u32, u32);
static u32 f54(u32, u32, u32);
static void setThrew(u32, u32);
static u32 fflush(u32);
static u32 f57(u32);
static u32 stackSave(void);
static u32 stackAlloc(u32);
static void stackRestore(u32);
static u32 __growWasmMemory(u32);
static u32 dynCall_ii(u32, u32);
static u32 dynCall_iiii(u32, u32, u32, u32);
static u64 f64_0(u32, u32, u64, u32);
static u32 dynCall_iidiiii(u32, u32, f64, u32, u32, u32, u32);
static void dynCall_vii(u32, u32, u32);
static u32 dynCall_jiji(u32, u32, u32, u32, u32);

static u32 g0;
static u32 __data_end;

static void init_globals(void) {
  g0 = 5246496u;
  __data_end = 3616u;
}

static u32 f6(void) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = 3616u;
  FUNC_EPILOGUE;
  return i0;
}

static void __wasm_call_ctors(void) {
  FUNC_PROLOGUE;
  FUNC_EPILOGUE;
}

static void sayHello(void) {
  u32 l0 = 0, l1 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = 1024u;
  l0 = i0;
  i0 = 0u;
  l1 = i0;
  i0 = l0;
  i1 = l1;
  i0 = f11(i0, i1);
  goto Bfunc;
  Bfunc:;
  FUNC_EPILOGUE;
}

static f64 add(f64 p0, f64 p1) {
  u32 l2 = 0, l3 = 0, l4 = 0;
  f64 l5 = 0, l6 = 0, l7 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1;
  f64 d0, d1;
  i0 = g0;
  l2 = i0;
  i0 = 16u;
  l3 = i0;
  i0 = l2;
  i1 = l3;
  i0 -= i1;
  l4 = i0;
  i0 = l4;
  d1 = p0;
  f64_store(Z_envZ_memory, (u64)(i0 + 8), d1);
  i0 = l4;
  d1 = p1;
  f64_store(Z_envZ_memory, (u64)(i0), d1);
  i0 = l4;
  d0 = f64_load(Z_envZ_memory, (u64)(i0 + 8));
  l5 = d0;
  i0 = l4;
  d0 = f64_load(Z_envZ_memory, (u64)(i0));
  l6 = d0;
  d0 = l5;
  d1 = l6;
  d0 += d1;
  l7 = d0;
  d0 = l7;
  goto Bfunc;
  Bfunc:;
  FUNC_EPILOGUE;
  return d0;
}

static u32 greet(u32 p0) {
  u32 l1 = 0, l2 = 0, l3 = 0, l4 = 0, l5 = 0, l6 = 0, l7 = 0, l8 = 0, 
      l9 = 0, l10 = 0, l11 = 0, l12 = 0, l13 = 0, l14 = 0, l15 = 0, l16 = 0, 
      l17 = 0, l18 = 0, l19 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = g0;
  l1 = i0;
  i0 = 288u;
  l2 = i0;
  i0 = l1;
  i1 = l2;
  i0 -= i1;
  l3 = i0;
  i0 = l3;
  g0 = i0;
  i0 = l3;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 284), i1);
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 284));
  l4 = i0;
  i0 = l3;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = 1039u;
  l5 = i0;
  i0 = l5;
  i1 = l3;
  i0 = f11(i0, i1);
  i0 = 1058u;
  l6 = i0;
  i0 = 16u;
  l7 = i0;
  i0 = l3;
  i1 = l7;
  i0 += i1;
  l8 = i0;
  i0 = l8;
  l9 = i0;
  i0 = 256u;
  l10 = i0;
  i0 = l10;
  i0 = malloc(i0);
  l11 = i0;
  i0 = l3;
  i1 = l11;
  i32_store(Z_envZ_memory, (u64)(i0 + 280), i1);
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 280));
  l12 = i0;
  i0 = 1050u;
  l13 = i0;
  i0 = l12;
  i1 = l13;
  i0 = f44(i0, i1);
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 284));
  l14 = i0;
  i0 = l9;
  i1 = l14;
  i0 = f44(i0, i1);
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 280));
  l15 = i0;
  i0 = l15;
  i1 = l9;
  i0 = f42(i0, i1);
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 280));
  l16 = i0;
  i0 = l16;
  i1 = l6;
  i0 = f42(i0, i1);
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 280));
  l17 = i0;
  i0 = 288u;
  l18 = i0;
  i0 = l3;
  i1 = l18;
  i0 += i1;
  l19 = i0;
  i0 = l19;
  g0 = i0;
  i0 = l17;
  goto Bfunc;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f11(u32 p0, u32 p1) {
  u32 l2 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = g0;
  i1 = 16u;
  i0 -= i1;
  l2 = i0;
  g0 = i0;
  i0 = l2;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 1060));
  i1 = p0;
  i2 = p1;
  i0 = f37(i0, i1, i2);
  p1 = i0;
  i0 = l2;
  i1 = 16u;
  i0 += i1;
  g0 = i0;
  i0 = p1;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f12(u32 p0, u32 p1, u32 p2) {
  u32 l3 = 0, l4 = 0, l5 = 0, l6 = 0, l7 = 0, l8 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4, i5;
  u64 j1;
  i0 = g0;
  i1 = 32u;
  i0 -= i1;
  l3 = i0;
  g0 = i0;
  i0 = l3;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 28));
  l4 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
  l5 = i0;
  i0 = l3;
  i1 = p2;
  i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
  i0 = l3;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l3;
  i1 = l5;
  i2 = l4;
  i1 -= i2;
  p1 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 20), i1);
  i0 = p1;
  i1 = p2;
  i0 += i1;
  l6 = i0;
  i0 = 2u;
  l5 = i0;
  i0 = l3;
  i1 = 16u;
  i0 += i1;
  p1 = i0;
  L0: 
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 60));
    i1 = p1;
    i2 = l5;
    i3 = l3;
    i4 = 12u;
    i3 += i4;
    i0 = (*Z_wasi_unstableZ_fd_writeZ_iiiii)(i0, i1, i2, i3);
    i0 = f46(i0);
    i0 = !(i0);
    if (i0) {goto B2;}
    i0 = 4294967295u;
    l4 = i0;
    i0 = l3;
    i1 = 4294967295u;
    i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
    goto B1;
    B2:;
    i0 = l3;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
    l4 = i0;
    B1:;
    i0 = l6;
    i1 = l4;
    i0 = i0 != i1;
    if (i0) {goto B5;}
    i0 = p0;
    i1 = p0;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 44));
    p1 = i1;
    i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
    i0 = p0;
    i1 = p1;
    i32_store(Z_envZ_memory, (u64)(i0 + 20), i1);
    i0 = p0;
    i1 = p1;
    i2 = p0;
    i2 = i32_load(Z_envZ_memory, (u64)(i2 + 48));
    i1 += i2;
    i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
    i0 = p2;
    l4 = i0;
    goto B4;
    B5:;
    i0 = l4;
    i1 = 4294967295u;
    i0 = (u32)((s32)i0 > (s32)i1);
    if (i0) {goto B3;}
    i0 = 0u;
    l4 = i0;
    i0 = p0;
    i1 = 0u;
    i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
    i0 = p0;
    j1 = 0ull;
    i64_store(Z_envZ_memory, (u64)(i0 + 16), j1);
    i0 = p0;
    i1 = p0;
    i1 = i32_load(Z_envZ_memory, (u64)(i1));
    i2 = 32u;
    i1 |= i2;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = l5;
    i1 = 2u;
    i0 = i0 == i1;
    if (i0) {goto B4;}
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
    i0 -= i1;
    l4 = i0;
    B4:;
    i0 = l3;
    i1 = 32u;
    i0 += i1;
    g0 = i0;
    i0 = l4;
    goto Bfunc;
    B3:;
    i0 = p1;
    i1 = 8u;
    i0 += i1;
    i1 = p1;
    i2 = l4;
    i3 = p1;
    i3 = i32_load(Z_envZ_memory, (u64)(i3 + 4));
    l7 = i3;
    i2 = i2 > i3;
    l8 = i2;
    i0 = i2 ? i0 : i1;
    p1 = i0;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1));
    i2 = l4;
    i3 = l7;
    i4 = 0u;
    i5 = l8;
    i3 = i5 ? i3 : i4;
    i2 -= i3;
    l7 = i2;
    i1 += i2;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = p1;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
    i2 = l7;
    i1 -= i2;
    i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
    i0 = l6;
    i1 = l4;
    i0 -= i1;
    l6 = i0;
    i0 = l5;
    i1 = l8;
    i0 -= i1;
    l5 = i0;
    goto L0;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f13(u32 p0) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = 0u;
  FUNC_EPILOGUE;
  return i0;
}

static u64 f14(u32 p0, u64 p1, u32 p2) {
  FUNC_PROLOGUE;
  u64 j0;
  j0 = 0ull;
  FUNC_EPILOGUE;
  return j0;
}

static u32 f15(u32 p0) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = 1u;
  FUNC_EPILOGUE;
  return i0;
}

static void f16(u32 p0) {
  FUNC_PROLOGUE;
  FUNC_EPILOGUE;
}

static u32 __errno_location(void) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = 3032u;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f18(u32 p0) {
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = p0;
  i1 = 4294967248u;
  i0 += i1;
  i1 = 10u;
  i0 = i0 < i1;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f19(void) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = 1756u;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f20(u32 p0, u32 p1, u32 p2) {
  u32 l3 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = 1u;
  l3 = i0;
  i0 = p0;
  i0 = !(i0);
  if (i0) {goto B1;}
  i0 = p1;
  i1 = 127u;
  i0 = i0 <= i1;
  if (i0) {goto B0;}
  i0 = f21();
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 188));
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  if (i0) {goto B3;}
  i0 = p1;
  i1 = 4294967168u;
  i0 &= i1;
  i1 = 57216u;
  i0 = i0 == i1;
  if (i0) {goto B0;}
  i0 = __errno_location();
  i1 = 25u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  goto B2;
  B3:;
  i0 = p1;
  i1 = 2047u;
  i0 = i0 > i1;
  if (i0) {goto B4;}
  i0 = p0;
  i1 = p1;
  i2 = 63u;
  i1 &= i2;
  i2 = 128u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0 + 1), i1);
  i0 = p0;
  i1 = p1;
  i2 = 6u;
  i1 >>= (i2 & 31);
  i2 = 192u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = 2u;
  goto Bfunc;
  B4:;
  i0 = p1;
  i1 = 55296u;
  i0 = i0 < i1;
  if (i0) {goto B6;}
  i0 = p1;
  i1 = 4294959104u;
  i0 &= i1;
  i1 = 57344u;
  i0 = i0 != i1;
  if (i0) {goto B5;}
  B6:;
  i0 = p0;
  i1 = p1;
  i2 = 63u;
  i1 &= i2;
  i2 = 128u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0 + 2), i1);
  i0 = p0;
  i1 = p1;
  i2 = 12u;
  i1 >>= (i2 & 31);
  i2 = 224u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  i2 = 6u;
  i1 >>= (i2 & 31);
  i2 = 63u;
  i1 &= i2;
  i2 = 128u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0 + 1), i1);
  i0 = 3u;
  goto Bfunc;
  B5:;
  i0 = p1;
  i1 = 4294901760u;
  i0 += i1;
  i1 = 1048575u;
  i0 = i0 > i1;
  if (i0) {goto B7;}
  i0 = p0;
  i1 = p1;
  i2 = 63u;
  i1 &= i2;
  i2 = 128u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0 + 3), i1);
  i0 = p0;
  i1 = p1;
  i2 = 18u;
  i1 >>= (i2 & 31);
  i2 = 240u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  i2 = 6u;
  i1 >>= (i2 & 31);
  i2 = 63u;
  i1 &= i2;
  i2 = 128u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0 + 2), i1);
  i0 = p0;
  i1 = p1;
  i2 = 12u;
  i1 >>= (i2 & 31);
  i2 = 63u;
  i1 &= i2;
  i2 = 128u;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0 + 1), i1);
  i0 = 4u;
  goto Bfunc;
  B7:;
  i0 = __errno_location();
  i1 = 25u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  B2:;
  i0 = 4294967295u;
  l3 = i0;
  B1:;
  i0 = l3;
  goto Bfunc;
  B0:;
  i0 = p0;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = 1u;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f21(void) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = f19();
  FUNC_EPILOGUE;
  return i0;
}

static u32 f22(u32 p0, u32 p1) {
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = p0;
  if (i0) {goto B0;}
  i0 = 0u;
  goto Bfunc;
  B0:;
  i0 = p0;
  i1 = p1;
  i2 = 0u;
  i0 = f20(i0, i1, i2);
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static f64 f23(f64 p0, u32 p1) {
  u32 l2 = 0;
  u64 l3 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j0, j1;
  f64 d0, d1;
  d0 = p0;
  j0 = i64_reinterpret_f64(d0);
  l3 = j0;
  j1 = 52ull;
  j0 >>= (j1 & 63);
  i0 = (u32)(j0);
  i1 = 2047u;
  i0 &= i1;
  l2 = i0;
  i1 = 2047u;
  i0 = i0 == i1;
  if (i0) {goto B0;}
  i0 = l2;
  if (i0) {goto B1;}
  d0 = p0;
  d1 = 0;
  i0 = d0 != d1;
  if (i0) {goto B3;}
  i0 = 0u;
  l2 = i0;
  goto B2;
  B3:;
  d0 = p0;
  d1 = 1.8446744073709552e+19;
  d0 *= d1;
  i1 = p1;
  d0 = f23(d0, i1);
  p0 = d0;
  i0 = p1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  i1 = 4294967232u;
  i0 += i1;
  l2 = i0;
  B2:;
  i0 = p1;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  d0 = p0;
  goto Bfunc;
  B1:;
  i0 = p1;
  i1 = l2;
  i2 = 4294966274u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  j0 = l3;
  j1 = 9227875636482146303ull;
  j0 &= j1;
  j1 = 4602678819172646912ull;
  j0 |= j1;
  d0 = f64_reinterpret_i64(j0);
  p0 = d0;
  B0:;
  d0 = p0;
  Bfunc:;
  FUNC_EPILOGUE;
  return d0;
}

static u32 f24(void) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = 3100u;
  (*Z_envZ___lockZ_vi)(i0);
  i0 = 3108u;
  FUNC_EPILOGUE;
  return i0;
}

static void f25(void) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = 3100u;
  (*Z_envZ___unlockZ_vi)(i0);
  FUNC_EPILOGUE;
}

static u32 f26(u32 p0) {
  u32 l1 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j1;
  i0 = p0;
  i1 = p0;
  i1 = i32_load8_u(Z_envZ_memory, (u64)(i1 + 74));
  l1 = i1;
  i2 = 4294967295u;
  i1 += i2;
  i2 = l1;
  i1 |= i2;
  i32_store8(Z_envZ_memory, (u64)(i0 + 74), i1);
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l1 = i0;
  i1 = 8u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B0;}
  i0 = p0;
  i1 = l1;
  i2 = 32u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = 4294967295u;
  goto Bfunc;
  B0:;
  i0 = p0;
  j1 = 0ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 4), j1);
  i0 = p0;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 44));
  l1 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
  i0 = p0;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 20), i1);
  i0 = p0;
  i1 = l1;
  i2 = p0;
  i2 = i32_load(Z_envZ_memory, (u64)(i2 + 48));
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = 0u;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f27(u32 p0, u32 p1, u32 p2) {
  u32 l3 = 0, l4 = 0, l5 = 0, l6 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3;
  i0 = p2;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  l3 = i0;
  if (i0) {goto B1;}
  i0 = 0u;
  l4 = i0;
  i0 = p2;
  i0 = f26(i0);
  if (i0) {goto B0;}
  i0 = p2;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  l3 = i0;
  B1:;
  i0 = l3;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 20));
  l5 = i1;
  i0 -= i1;
  i1 = p1;
  i0 = i0 >= i1;
  if (i0) {goto B2;}
  i0 = p2;
  i1 = p0;
  i2 = p1;
  i3 = p2;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 36));
  i0 = CALL_INDIRECT((*Z_envZ_table), u32 (*)(u32, u32, u32), 0, i3, i0, i1, i2);
  goto Bfunc;
  B2:;
  i0 = 0u;
  l6 = i0;
  i0 = p2;
  i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 75));
  i1 = 0u;
  i0 = (u32)((s32)i0 < (s32)i1);
  if (i0) {goto B3;}
  i0 = p1;
  l4 = i0;
  L4: 
    i0 = l4;
    l3 = i0;
    i0 = !(i0);
    if (i0) {goto B3;}
    i0 = p0;
    i1 = l3;
    i2 = 4294967295u;
    i1 += i2;
    l4 = i1;
    i0 += i1;
    i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
    i1 = 10u;
    i0 = i0 != i1;
    if (i0) {goto L4;}
  i0 = p2;
  i1 = p0;
  i2 = l3;
  i3 = p2;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 36));
  i0 = CALL_INDIRECT((*Z_envZ_table), u32 (*)(u32, u32, u32), 0, i3, i0, i1, i2);
  l4 = i0;
  i1 = l3;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = p1;
  i1 = l3;
  i0 -= i1;
  p1 = i0;
  i0 = p0;
  i1 = l3;
  i0 += i1;
  p0 = i0;
  i0 = p2;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
  l5 = i0;
  i0 = l3;
  l6 = i0;
  B3:;
  i0 = l5;
  i1 = p0;
  i2 = p1;
  i0 = f53(i0, i1, i2);
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 20));
  i2 = p1;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 20), i1);
  i0 = l6;
  i1 = p1;
  i0 += i1;
  l4 = i0;
  B0:;
  i0 = l4;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f28(u32 p0, u32 p1, u32 p2, u32 p3, u32 p4) {
  u32 l5 = 0, l6 = 0, l7 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4, i5, i6;
  i0 = g0;
  i1 = 208u;
  i0 -= i1;
  l5 = i0;
  g0 = i0;
  i0 = l5;
  i1 = p2;
  i32_store(Z_envZ_memory, (u64)(i0 + 204), i1);
  i0 = 0u;
  p2 = i0;
  i0 = l5;
  i1 = 160u;
  i0 += i1;
  i1 = 0u;
  i2 = 40u;
  i0 = f54(i0, i1, i2);
  i0 = l5;
  i1 = l5;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 204));
  i32_store(Z_envZ_memory, (u64)(i0 + 200), i1);
  i0 = 0u;
  i1 = p1;
  i2 = l5;
  i3 = 200u;
  i2 += i3;
  i3 = l5;
  i4 = 80u;
  i3 += i4;
  i4 = l5;
  i5 = 160u;
  i4 += i5;
  i5 = p3;
  i6 = p4;
  i0 = f29(i0, i1, i2, i3, i4, i5, i6);
  i1 = 0u;
  i0 = (u32)((s32)i0 >= (s32)i1);
  if (i0) {goto B1;}
  i0 = 4294967295u;
  p1 = i0;
  goto B0;
  B1:;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
  i1 = 0u;
  i0 = (u32)((s32)i0 < (s32)i1);
  if (i0) {goto B2;}
  i0 = p0;
  i0 = f15(i0);
  p2 = i0;
  B2:;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l6 = i0;
  i0 = p0;
  i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 74));
  i1 = 0u;
  i0 = (u32)((s32)i0 > (s32)i1);
  if (i0) {goto B3;}
  i0 = p0;
  i1 = l6;
  i2 = 4294967263u;
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  B3:;
  i0 = l6;
  i1 = 32u;
  i0 &= i1;
  l6 = i0;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 48));
  i0 = !(i0);
  if (i0) {goto B5;}
  i0 = p0;
  i1 = p1;
  i2 = l5;
  i3 = 200u;
  i2 += i3;
  i3 = l5;
  i4 = 80u;
  i3 += i4;
  i4 = l5;
  i5 = 160u;
  i4 += i5;
  i5 = p3;
  i6 = p4;
  i0 = f29(i0, i1, i2, i3, i4, i5, i6);
  p1 = i0;
  goto B4;
  B5:;
  i0 = p0;
  i1 = 80u;
  i32_store(Z_envZ_memory, (u64)(i0 + 48), i1);
  i0 = p0;
  i1 = l5;
  i2 = 80u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 20), i1);
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 44));
  l7 = i0;
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 44), i1);
  i0 = p0;
  i1 = p1;
  i2 = l5;
  i3 = 200u;
  i2 += i3;
  i3 = l5;
  i4 = 80u;
  i3 += i4;
  i4 = l5;
  i5 = 160u;
  i4 += i5;
  i5 = p3;
  i6 = p4;
  i0 = f29(i0, i1, i2, i3, i4, i5, i6);
  p1 = i0;
  i0 = l7;
  i0 = !(i0);
  if (i0) {goto B4;}
  i0 = p0;
  i1 = 0u;
  i2 = 0u;
  i3 = p0;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 36));
  i0 = CALL_INDIRECT((*Z_envZ_table), u32 (*)(u32, u32, u32), 0, i3, i0, i1, i2);
  i0 = p0;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 48), i1);
  i0 = p0;
  i1 = l7;
  i32_store(Z_envZ_memory, (u64)(i0 + 44), i1);
  i0 = p0;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
  i0 = p0;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
  p3 = i0;
  i0 = p0;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 20), i1);
  i0 = p1;
  i1 = 4294967295u;
  i2 = p3;
  i0 = i2 ? i0 : i1;
  p1 = i0;
  B4:;
  i0 = p0;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  p3 = i1;
  i2 = l6;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = 4294967295u;
  i1 = p1;
  i2 = p3;
  i3 = 32u;
  i2 &= i3;
  i0 = i2 ? i0 : i1;
  p1 = i0;
  i0 = p2;
  i0 = !(i0);
  if (i0) {goto B0;}
  i0 = p0;
  f16(i0);
  B0:;
  i0 = l5;
  i1 = 208u;
  i0 += i1;
  g0 = i0;
  i0 = p1;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f29(u32 p0, u32 p1, u32 p2, u32 p3, u32 p4, u32 p5, u32 p6) {
  u32 l7 = 0, l8 = 0, l9 = 0, l10 = 0, l11 = 0, l12 = 0, l13 = 0, l14 = 0, 
      l15 = 0, l16 = 0, l17 = 0, l18 = 0, l19 = 0, l20 = 0, l21 = 0;
  u64 l22 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4, i5, i6;
  u64 j0, j1, j2;
  f64 d1;
  i0 = g0;
  i1 = 80u;
  i0 -= i1;
  l7 = i0;
  g0 = i0;
  i0 = l7;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
  i0 = l7;
  i1 = 55u;
  i0 += i1;
  l8 = i0;
  i0 = l7;
  i1 = 56u;
  i0 += i1;
  l9 = i0;
  i0 = 0u;
  l10 = i0;
  i0 = 0u;
  l11 = i0;
  i0 = 0u;
  p1 = i0;
  L2: 
    i0 = l11;
    i1 = 0u;
    i0 = (u32)((s32)i0 < (s32)i1);
    if (i0) {goto B3;}
    i0 = p1;
    i1 = 2147483647u;
    i2 = l11;
    i1 -= i2;
    i0 = (u32)((s32)i0 <= (s32)i1);
    if (i0) {goto B4;}
    i0 = __errno_location();
    i1 = 61u;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = 4294967295u;
    l11 = i0;
    goto B3;
    B4:;
    i0 = p1;
    i1 = l11;
    i0 += i1;
    l11 = i0;
    B3:;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
    l12 = i0;
    p1 = i0;
    i0 = l12;
    i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
    l13 = i0;
    i0 = !(i0);
    if (i0) {goto B18;}
    L20: 
      i0 = l13;
      i1 = 255u;
      i0 &= i1;
      l13 = i0;
      if (i0) {goto B23;}
      i0 = p1;
      l13 = i0;
      goto B22;
      B23:;
      i0 = l13;
      i1 = 37u;
      i0 = i0 != i1;
      if (i0) {goto B21;}
      i0 = p1;
      l13 = i0;
      L24: 
        i0 = p1;
        i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 1));
        i1 = 37u;
        i0 = i0 != i1;
        if (i0) {goto B22;}
        i0 = l7;
        i1 = p1;
        i2 = 2u;
        i1 += i2;
        l14 = i1;
        i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
        i0 = l13;
        i1 = 1u;
        i0 += i1;
        l13 = i0;
        i0 = p1;
        i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 2));
        l15 = i0;
        i0 = l14;
        p1 = i0;
        i0 = l15;
        i1 = 37u;
        i0 = i0 == i1;
        if (i0) {goto L24;}
      B22:;
      i0 = l13;
      i1 = l12;
      i0 -= i1;
      p1 = i0;
      i0 = p0;
      i0 = !(i0);
      if (i0) {goto B25;}
      i0 = p0;
      i1 = l12;
      i2 = p1;
      f30(i0, i1, i2);
      B25:;
      i0 = p1;
      if (i0) {goto L2;}
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 1));
      i0 = f18(i0);
      l14 = i0;
      i0 = 4294967295u;
      l16 = i0;
      i0 = 1u;
      l13 = i0;
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
      p1 = i0;
      i0 = l14;
      i0 = !(i0);
      if (i0) {goto B26;}
      i0 = p1;
      i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 2));
      i1 = 36u;
      i0 = i0 != i1;
      if (i0) {goto B26;}
      i0 = p1;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 1));
      i1 = 4294967248u;
      i0 += i1;
      l16 = i0;
      i0 = 1u;
      l10 = i0;
      i0 = 3u;
      l13 = i0;
      B26:;
      i0 = l7;
      i1 = p1;
      i2 = l13;
      i1 += i2;
      p1 = i1;
      i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
      i0 = 0u;
      l13 = i0;
      i0 = p1;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0));
      l17 = i0;
      i1 = 4294967264u;
      i0 += i1;
      l15 = i0;
      i1 = 31u;
      i0 = i0 <= i1;
      if (i0) {goto B28;}
      i0 = p1;
      l14 = i0;
      goto B27;
      B28:;
      i0 = p1;
      l14 = i0;
      i0 = 1u;
      i1 = l15;
      i0 <<= (i1 & 31);
      l15 = i0;
      i1 = 75913u;
      i0 &= i1;
      i0 = !(i0);
      if (i0) {goto B27;}
      L29: 
        i0 = l7;
        i1 = p1;
        i2 = 1u;
        i1 += i2;
        l14 = i1;
        i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
        i0 = l15;
        i1 = l13;
        i0 |= i1;
        l13 = i0;
        i0 = p1;
        i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 1));
        l17 = i0;
        i1 = 4294967264u;
        i0 += i1;
        l15 = i0;
        i1 = 31u;
        i0 = i0 > i1;
        if (i0) {goto B27;}
        i0 = l14;
        p1 = i0;
        i0 = 1u;
        i1 = l15;
        i0 <<= (i1 & 31);
        l15 = i0;
        i1 = 75913u;
        i0 &= i1;
        if (i0) {goto L29;}
      B27:;
      i0 = l17;
      i1 = 42u;
      i0 = i0 != i1;
      if (i0) {goto B31;}
      i0 = l14;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 1));
      i0 = f18(i0);
      i0 = !(i0);
      if (i0) {goto B33;}
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
      l14 = i0;
      i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 2));
      i1 = 36u;
      i0 = i0 != i1;
      if (i0) {goto B33;}
      i0 = l14;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 1));
      i1 = 2u;
      i0 <<= (i1 & 31);
      i1 = p4;
      i0 += i1;
      i1 = 4294967104u;
      i0 += i1;
      i1 = 10u;
      i32_store(Z_envZ_memory, (u64)(i0), i1);
      i0 = l14;
      i1 = 3u;
      i0 += i1;
      p1 = i0;
      i0 = l14;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 1));
      i1 = 3u;
      i0 <<= (i1 & 31);
      i1 = p3;
      i0 += i1;
      i1 = 4294966912u;
      i0 += i1;
      i0 = i32_load(Z_envZ_memory, (u64)(i0));
      l18 = i0;
      i0 = 1u;
      l10 = i0;
      goto B32;
      B33:;
      i0 = l10;
      if (i0) {goto B1;}
      i0 = 0u;
      l10 = i0;
      i0 = 0u;
      l18 = i0;
      i0 = p0;
      i0 = !(i0);
      if (i0) {goto B34;}
      i0 = p2;
      i1 = p2;
      i1 = i32_load(Z_envZ_memory, (u64)(i1));
      p1 = i1;
      i2 = 4u;
      i1 += i2;
      i32_store(Z_envZ_memory, (u64)(i0), i1);
      i0 = p1;
      i0 = i32_load(Z_envZ_memory, (u64)(i0));
      l18 = i0;
      B34:;
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
      i1 = 1u;
      i0 += i1;
      p1 = i0;
      B32:;
      i0 = l7;
      i1 = p1;
      i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
      i0 = l18;
      i1 = 4294967295u;
      i0 = (u32)((s32)i0 > (s32)i1);
      if (i0) {goto B30;}
      i0 = 0u;
      i1 = l18;
      i0 -= i1;
      l18 = i0;
      i0 = l13;
      i1 = 8192u;
      i0 |= i1;
      l13 = i0;
      goto B30;
      B31:;
      i0 = l7;
      i1 = 76u;
      i0 += i1;
      i0 = f31(i0);
      l18 = i0;
      i1 = 0u;
      i0 = (u32)((s32)i0 < (s32)i1);
      if (i0) {goto B1;}
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
      p1 = i0;
      B30:;
      i0 = 4294967295u;
      l19 = i0;
      i0 = p1;
      i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
      i1 = 46u;
      i0 = i0 != i1;
      if (i0) {goto B35;}
      i0 = p1;
      i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 1));
      i1 = 42u;
      i0 = i0 != i1;
      if (i0) {goto B36;}
      i0 = p1;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 2));
      i0 = f18(i0);
      i0 = !(i0);
      if (i0) {goto B37;}
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
      p1 = i0;
      i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 3));
      i1 = 36u;
      i0 = i0 != i1;
      if (i0) {goto B37;}
      i0 = p1;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 2));
      i1 = 2u;
      i0 <<= (i1 & 31);
      i1 = p4;
      i0 += i1;
      i1 = 4294967104u;
      i0 += i1;
      i1 = 10u;
      i32_store(Z_envZ_memory, (u64)(i0), i1);
      i0 = p1;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 2));
      i1 = 3u;
      i0 <<= (i1 & 31);
      i1 = p3;
      i0 += i1;
      i1 = 4294966912u;
      i0 += i1;
      i0 = i32_load(Z_envZ_memory, (u64)(i0));
      l19 = i0;
      i0 = l7;
      i1 = p1;
      i2 = 4u;
      i1 += i2;
      p1 = i1;
      i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
      goto B35;
      B37:;
      i0 = l10;
      if (i0) {goto B1;}
      i0 = p0;
      if (i0) {goto B39;}
      i0 = 0u;
      l19 = i0;
      goto B38;
      B39:;
      i0 = p2;
      i1 = p2;
      i1 = i32_load(Z_envZ_memory, (u64)(i1));
      p1 = i1;
      i2 = 4u;
      i1 += i2;
      i32_store(Z_envZ_memory, (u64)(i0), i1);
      i0 = p1;
      i0 = i32_load(Z_envZ_memory, (u64)(i0));
      l19 = i0;
      B38:;
      i0 = l7;
      i1 = l7;
      i1 = i32_load(Z_envZ_memory, (u64)(i1 + 76));
      i2 = 2u;
      i1 += i2;
      p1 = i1;
      i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
      goto B35;
      B36:;
      i0 = l7;
      i1 = p1;
      i2 = 1u;
      i1 += i2;
      i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
      i0 = l7;
      i1 = 76u;
      i0 += i1;
      i0 = f31(i0);
      l19 = i0;
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
      p1 = i0;
      B35:;
      i0 = 0u;
      l14 = i0;
      L40: 
        i0 = l14;
        l15 = i0;
        i0 = 4294967295u;
        l20 = i0;
        i0 = p1;
        i0 = i32_load8_s(Z_envZ_memory, (u64)(i0));
        i1 = 4294967231u;
        i0 += i1;
        i1 = 57u;
        i0 = i0 > i1;
        if (i0) {goto B0;}
        i0 = l7;
        i1 = p1;
        i2 = 1u;
        i1 += i2;
        l17 = i1;
        i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
        i0 = p1;
        i0 = i32_load8_s(Z_envZ_memory, (u64)(i0));
        l14 = i0;
        i0 = l17;
        p1 = i0;
        i0 = l14;
        i1 = l15;
        i2 = 58u;
        i1 *= i2;
        i0 += i1;
        i1 = 1023u;
        i0 += i1;
        i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
        l14 = i0;
        i1 = 4294967295u;
        i0 += i1;
        i1 = 8u;
        i0 = i0 < i1;
        if (i0) {goto L40;}
      i0 = l14;
      i0 = !(i0);
      if (i0) {goto B0;}
      i0 = l14;
      i1 = 19u;
      i0 = i0 != i1;
      if (i0) {goto B44;}
      i0 = 4294967295u;
      l20 = i0;
      i0 = l16;
      i1 = 4294967295u;
      i0 = (u32)((s32)i0 <= (s32)i1);
      if (i0) {goto B43;}
      goto B0;
      B44:;
      i0 = l16;
      i1 = 0u;
      i0 = (u32)((s32)i0 < (s32)i1);
      if (i0) {goto B42;}
      i0 = p4;
      i1 = l16;
      i2 = 2u;
      i1 <<= (i2 & 31);
      i0 += i1;
      i1 = l14;
      i32_store(Z_envZ_memory, (u64)(i0), i1);
      i0 = l7;
      i1 = p3;
      i2 = l16;
      i3 = 3u;
      i2 <<= (i3 & 31);
      i1 += i2;
      j1 = i64_load(Z_envZ_memory, (u64)(i1));
      i64_store(Z_envZ_memory, (u64)(i0 + 64), j1);
      B43:;
      i0 = 0u;
      p1 = i0;
      i0 = p0;
      i0 = !(i0);
      if (i0) {goto L2;}
      goto B41;
      B42:;
      i0 = p0;
      i0 = !(i0);
      if (i0) {goto B5;}
      i0 = l7;
      i1 = 64u;
      i0 += i1;
      i1 = l14;
      i2 = p2;
      i3 = p6;
      f32_0(i0, i1, i2, i3);
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
      l17 = i0;
      B41:;
      i0 = l13;
      i1 = 4294901759u;
      i0 &= i1;
      l21 = i0;
      i1 = l13;
      i2 = l13;
      i3 = 8192u;
      i2 &= i3;
      i0 = i2 ? i0 : i1;
      l13 = i0;
      i0 = 0u;
      l20 = i0;
      i0 = 1064u;
      l16 = i0;
      i0 = l9;
      l14 = i0;
      i0 = l17;
      i1 = 4294967295u;
      i0 += i1;
      i0 = i32_load8_s(Z_envZ_memory, (u64)(i0));
      p1 = i0;
      i1 = 4294967263u;
      i0 &= i1;
      i1 = p1;
      i2 = p1;
      i3 = 15u;
      i2 &= i3;
      i3 = 3u;
      i2 = i2 == i3;
      i0 = i2 ? i0 : i1;
      i1 = p1;
      i2 = l15;
      i0 = i2 ? i0 : i1;
      p1 = i0;
      i1 = 4294967208u;
      i0 += i1;
      l17 = i0;
      i1 = 32u;
      i0 = i0 <= i1;
      if (i0) {goto B19;}
      i0 = p1;
      i1 = 4294967231u;
      i0 += i1;
      l15 = i0;
      i1 = 6u;
      i0 = i0 <= i1;
      if (i0) {goto B49;}
      i0 = p1;
      i1 = 83u;
      i0 = i0 != i1;
      if (i0) {goto B6;}
      i0 = l19;
      i0 = !(i0);
      if (i0) {goto B48;}
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
      l14 = i0;
      goto B46;
      B49:;
      i0 = l15;
      switch (i0) {
        case 0: goto B17;
        case 1: goto B6;
        case 2: goto B47;
        case 3: goto B6;
        case 4: goto B17;
        case 5: goto B17;
        case 6: goto B17;
        default: goto B17;
      }
      B48:;
      i0 = 0u;
      p1 = i0;
      i0 = p0;
      i1 = 32u;
      i2 = l18;
      i3 = 0u;
      i4 = l13;
      f33(i0, i1, i2, i3, i4);
      goto B45;
      B47:;
      i0 = l7;
      i1 = 0u;
      i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
      i0 = l7;
      i1 = l7;
      j1 = i64_load(Z_envZ_memory, (u64)(i1 + 64));
      i64_store32(Z_envZ_memory, (u64)(i0 + 8), j1);
      i0 = l7;
      i1 = l7;
      i2 = 8u;
      i1 += i2;
      i32_store(Z_envZ_memory, (u64)(i0 + 64), i1);
      i0 = 4294967295u;
      l19 = i0;
      i0 = l7;
      i1 = 8u;
      i0 += i1;
      l14 = i0;
      B46:;
      i0 = 0u;
      p1 = i0;
      L51: 
        i0 = l14;
        i0 = i32_load(Z_envZ_memory, (u64)(i0));
        l15 = i0;
        i0 = !(i0);
        if (i0) {goto B50;}
        i0 = l7;
        i1 = 4u;
        i0 += i1;
        i1 = l15;
        i0 = f22(i0, i1);
        l15 = i0;
        i1 = 0u;
        i0 = (u32)((s32)i0 < (s32)i1);
        l12 = i0;
        if (i0) {goto B52;}
        i0 = l15;
        i1 = l19;
        i2 = p1;
        i1 -= i2;
        i0 = i0 > i1;
        if (i0) {goto B52;}
        i0 = l14;
        i1 = 4u;
        i0 += i1;
        l14 = i0;
        i0 = l19;
        i1 = l15;
        i2 = p1;
        i1 += i2;
        p1 = i1;
        i0 = i0 > i1;
        if (i0) {goto L51;}
        goto B50;
        B52:;
      i0 = 4294967295u;
      l20 = i0;
      i0 = l12;
      if (i0) {goto B0;}
      B50:;
      i0 = p0;
      i1 = 32u;
      i2 = l18;
      i3 = p1;
      i4 = l13;
      f33(i0, i1, i2, i3, i4);
      i0 = p1;
      if (i0) {goto B53;}
      i0 = 0u;
      p1 = i0;
      goto B45;
      B53:;
      i0 = 0u;
      l15 = i0;
      i0 = l7;
      i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
      l14 = i0;
      L54: 
        i0 = l14;
        i0 = i32_load(Z_envZ_memory, (u64)(i0));
        l12 = i0;
        i0 = !(i0);
        if (i0) {goto B45;}
        i0 = l7;
        i1 = 4u;
        i0 += i1;
        i1 = l12;
        i0 = f22(i0, i1);
        l12 = i0;
        i1 = l15;
        i0 += i1;
        l15 = i0;
        i1 = p1;
        i0 = (u32)((s32)i0 > (s32)i1);
        if (i0) {goto B45;}
        i0 = p0;
        i1 = l7;
        i2 = 4u;
        i1 += i2;
        i2 = l12;
        f30(i0, i1, i2);
        i0 = l14;
        i1 = 4u;
        i0 += i1;
        l14 = i0;
        i0 = l15;
        i1 = p1;
        i0 = i0 < i1;
        if (i0) {goto L54;}
      B45:;
      i0 = p0;
      i1 = 32u;
      i2 = l18;
      i3 = p1;
      i4 = l13;
      i5 = 8192u;
      i4 ^= i5;
      f33(i0, i1, i2, i3, i4);
      i0 = l18;
      i1 = p1;
      i2 = l18;
      i3 = p1;
      i2 = (u32)((s32)i2 > (s32)i3);
      i0 = i2 ? i0 : i1;
      p1 = i0;
      goto L2;
      B21:;
      i0 = l7;
      i1 = p1;
      i2 = 1u;
      i1 += i2;
      l14 = i1;
      i32_store(Z_envZ_memory, (u64)(i0 + 76), i1);
      i0 = p1;
      i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 1));
      l13 = i0;
      i0 = l14;
      p1 = i0;
      goto L20;
    UNREACHABLE;
    B19:;
    i0 = l17;
    switch (i0) {
      case 0: goto B11;
      case 1: goto B6;
      case 2: goto B6;
      case 3: goto B6;
      case 4: goto B6;
      case 5: goto B6;
      case 6: goto B6;
      case 7: goto B6;
      case 8: goto B6;
      case 9: goto B17;
      case 10: goto B6;
      case 11: goto B15;
      case 12: goto B14;
      case 13: goto B17;
      case 14: goto B17;
      case 15: goto B17;
      case 16: goto B6;
      case 17: goto B14;
      case 18: goto B6;
      case 19: goto B6;
      case 20: goto B6;
      case 21: goto B6;
      case 22: goto B10;
      case 23: goto B13;
      case 24: goto B12;
      case 25: goto B6;
      case 26: goto B6;
      case 27: goto B16;
      case 28: goto B6;
      case 29: goto B9;
      case 30: goto B6;
      case 31: goto B6;
      case 32: goto B11;
      default: goto B11;
    }
    B18:;
    i0 = l11;
    l20 = i0;
    i0 = p0;
    if (i0) {goto B0;}
    i0 = l10;
    i0 = !(i0);
    if (i0) {goto B5;}
    i0 = 1u;
    p1 = i0;
    L56: 
      i0 = p4;
      i1 = p1;
      i2 = 2u;
      i1 <<= (i2 & 31);
      i0 += i1;
      i0 = i32_load(Z_envZ_memory, (u64)(i0));
      l13 = i0;
      i0 = !(i0);
      if (i0) {goto B55;}
      i0 = p3;
      i1 = p1;
      i2 = 3u;
      i1 <<= (i2 & 31);
      i0 += i1;
      i1 = l13;
      i2 = p2;
      i3 = p6;
      f32_0(i0, i1, i2, i3);
      i0 = 1u;
      l20 = i0;
      i0 = p1;
      i1 = 1u;
      i0 += i1;
      p1 = i0;
      i1 = 10u;
      i0 = i0 != i1;
      if (i0) {goto L56;}
      goto B0;
    UNREACHABLE;
    B55:;
    i0 = 1u;
    l20 = i0;
    i0 = p1;
    i1 = 9u;
    i0 = i0 > i1;
    if (i0) {goto B0;}
    i0 = 4294967295u;
    l20 = i0;
    i0 = p4;
    i1 = p1;
    i2 = 2u;
    i1 <<= (i2 & 31);
    i0 += i1;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    if (i0) {goto B0;}
    L58: 
      i0 = p1;
      i1 = 1u;
      i0 += i1;
      p1 = i0;
      i1 = 10u;
      i0 = i0 == i1;
      if (i0) {goto B57;}
      i0 = p4;
      i1 = p1;
      i2 = 2u;
      i1 <<= (i2 & 31);
      i0 += i1;
      i0 = i32_load(Z_envZ_memory, (u64)(i0));
      i0 = !(i0);
      if (i0) {goto L58;}
    B57:;
    i0 = 4294967295u;
    i1 = 1u;
    i2 = p1;
    i3 = 10u;
    i2 = i2 < i3;
    i0 = i2 ? i0 : i1;
    l20 = i0;
    goto B0;
    B17:;
    i0 = p0;
    i1 = l7;
    d1 = f64_load(Z_envZ_memory, (u64)(i1 + 64));
    i2 = l18;
    i3 = l19;
    i4 = l13;
    i5 = p1;
    i6 = p5;
    i0 = CALL_INDIRECT((*Z_envZ_table), u32 (*)(u32, f64, u32, u32, u32, u32), 1, i6, i0, d1, i2, i3, i4, i5);
    p1 = i0;
    goto L2;
    B16:;
    i0 = 0u;
    l20 = i0;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
    p1 = i0;
    i1 = 1074u;
    i2 = p1;
    i0 = i2 ? i0 : i1;
    l12 = i0;
    i1 = 0u;
    i2 = l19;
    i0 = f41(i0, i1, i2);
    p1 = i0;
    i1 = l12;
    i2 = l19;
    i1 += i2;
    i2 = p1;
    i0 = i2 ? i0 : i1;
    l14 = i0;
    i0 = l21;
    l13 = i0;
    i0 = p1;
    i1 = l12;
    i0 -= i1;
    i1 = l19;
    i2 = p1;
    i0 = i2 ? i0 : i1;
    l19 = i0;
    goto B6;
    B15:;
    i0 = l7;
    i1 = l7;
    j1 = i64_load(Z_envZ_memory, (u64)(i1 + 64));
    i64_store8(Z_envZ_memory, (u64)(i0 + 55), j1);
    i0 = 1u;
    l19 = i0;
    i0 = l8;
    l12 = i0;
    i0 = l9;
    l14 = i0;
    i0 = l21;
    l13 = i0;
    goto B6;
    B14:;
    i0 = l7;
    j0 = i64_load(Z_envZ_memory, (u64)(i0 + 64));
    l22 = j0;
    j1 = 18446744073709551615ull;
    i0 = (u64)((s64)j0 > (s64)j1);
    if (i0) {goto B59;}
    i0 = l7;
    j1 = 0ull;
    j2 = l22;
    j1 -= j2;
    l22 = j1;
    i64_store(Z_envZ_memory, (u64)(i0 + 64), j1);
    i0 = 1u;
    l20 = i0;
    i0 = 1064u;
    l16 = i0;
    goto B8;
    B59:;
    i0 = l13;
    i1 = 2048u;
    i0 &= i1;
    i0 = !(i0);
    if (i0) {goto B60;}
    i0 = 1u;
    l20 = i0;
    i0 = 1065u;
    l16 = i0;
    goto B8;
    B60:;
    i0 = 1066u;
    i1 = 1064u;
    i2 = l13;
    i3 = 1u;
    i2 &= i3;
    l20 = i2;
    i0 = i2 ? i0 : i1;
    l16 = i0;
    goto B8;
    B13:;
    i0 = l7;
    j0 = i64_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l9;
    i0 = f34(j0, i1);
    l12 = i0;
    i0 = 0u;
    l20 = i0;
    i0 = 1064u;
    l16 = i0;
    i0 = l13;
    i1 = 8u;
    i0 &= i1;
    i0 = !(i0);
    if (i0) {goto B7;}
    i0 = l19;
    i1 = l9;
    i2 = l12;
    i1 -= i2;
    p1 = i1;
    i2 = 1u;
    i1 += i2;
    i2 = l19;
    i3 = p1;
    i2 = (u32)((s32)i2 > (s32)i3);
    i0 = i2 ? i0 : i1;
    l19 = i0;
    goto B7;
    B12:;
    i0 = l19;
    i1 = 8u;
    i2 = l19;
    i3 = 8u;
    i2 = i2 > i3;
    i0 = i2 ? i0 : i1;
    l19 = i0;
    i0 = l13;
    i1 = 8u;
    i0 |= i1;
    l13 = i0;
    i0 = 120u;
    p1 = i0;
    B11:;
    i0 = l7;
    j0 = i64_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l9;
    i2 = p1;
    i3 = 32u;
    i2 &= i3;
    i0 = f35(j0, i1, i2);
    l12 = i0;
    i0 = 0u;
    l20 = i0;
    i0 = 1064u;
    l16 = i0;
    i0 = l13;
    i1 = 8u;
    i0 &= i1;
    i0 = !(i0);
    if (i0) {goto B7;}
    i0 = l7;
    j0 = i64_load(Z_envZ_memory, (u64)(i0 + 64));
    i0 = !(j0);
    if (i0) {goto B7;}
    i0 = p1;
    i1 = 4u;
    i0 >>= (i1 & 31);
    i1 = 1064u;
    i0 += i1;
    l16 = i0;
    i0 = 2u;
    l20 = i0;
    goto B7;
    B10:;
    i0 = 0u;
    p1 = i0;
    i0 = l15;
    i1 = 255u;
    i0 &= i1;
    l13 = i0;
    i1 = 7u;
    i0 = i0 > i1;
    if (i0) {goto L2;}
    i0 = l13;
    switch (i0) {
      case 0: goto B67;
      case 1: goto B66;
      case 2: goto B65;
      case 3: goto B64;
      case 4: goto B63;
      case 5: goto L2;
      case 6: goto B62;
      case 7: goto B61;
      default: goto B67;
    }
    B67:;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l11;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    goto L2;
    B66:;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l11;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    goto L2;
    B65:;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l11;
    j1 = (u64)(s64)(s32)(i1);
    i64_store(Z_envZ_memory, (u64)(i0), j1);
    goto L2;
    B64:;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l11;
    i32_store16(Z_envZ_memory, (u64)(i0), i1);
    goto L2;
    B63:;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l11;
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    goto L2;
    B62:;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l11;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    goto L2;
    B61:;
    i0 = l7;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 64));
    i1 = l11;
    j1 = (u64)(s64)(s32)(i1);
    i64_store(Z_envZ_memory, (u64)(i0), j1);
    goto L2;
    B9:;
    i0 = 0u;
    l20 = i0;
    i0 = 1064u;
    l16 = i0;
    i0 = l7;
    j0 = i64_load(Z_envZ_memory, (u64)(i0 + 64));
    l22 = j0;
    B8:;
    j0 = l22;
    i1 = l9;
    i0 = f36(j0, i1);
    l12 = i0;
    B7:;
    i0 = l13;
    i1 = 4294901759u;
    i0 &= i1;
    i1 = l13;
    i2 = l19;
    i3 = 4294967295u;
    i2 = (u32)((s32)i2 > (s32)i3);
    i0 = i2 ? i0 : i1;
    l13 = i0;
    i0 = l7;
    j0 = i64_load(Z_envZ_memory, (u64)(i0 + 64));
    l22 = j0;
    i0 = l19;
    if (i0) {goto B69;}
    j0 = l22;
    i0 = !(j0);
    i0 = !(i0);
    if (i0) {goto B69;}
    i0 = 0u;
    l19 = i0;
    i0 = l9;
    l12 = i0;
    goto B68;
    B69:;
    i0 = l19;
    i1 = l9;
    i2 = l12;
    i1 -= i2;
    j2 = l22;
    i2 = !(j2);
    i1 += i2;
    p1 = i1;
    i2 = l19;
    i3 = p1;
    i2 = (u32)((s32)i2 > (s32)i3);
    i0 = i2 ? i0 : i1;
    l19 = i0;
    B68:;
    i0 = l9;
    l14 = i0;
    B6:;
    i0 = p0;
    i1 = 32u;
    i2 = l20;
    i3 = l14;
    i4 = l12;
    i3 -= i4;
    l15 = i3;
    i4 = l19;
    i5 = l19;
    i6 = l15;
    i5 = (u32)((s32)i5 < (s32)i6);
    i3 = i5 ? i3 : i4;
    l17 = i3;
    i2 += i3;
    l14 = i2;
    i3 = l18;
    i4 = l18;
    i5 = l14;
    i4 = (u32)((s32)i4 < (s32)i5);
    i2 = i4 ? i2 : i3;
    p1 = i2;
    i3 = l14;
    i4 = l13;
    f33(i0, i1, i2, i3, i4);
    i0 = p0;
    i1 = l16;
    i2 = l20;
    f30(i0, i1, i2);
    i0 = p0;
    i1 = 48u;
    i2 = p1;
    i3 = l14;
    i4 = l13;
    i5 = 65536u;
    i4 ^= i5;
    f33(i0, i1, i2, i3, i4);
    i0 = p0;
    i1 = 48u;
    i2 = l17;
    i3 = l15;
    i4 = 0u;
    f33(i0, i1, i2, i3, i4);
    i0 = p0;
    i1 = l12;
    i2 = l15;
    f30(i0, i1, i2);
    i0 = p0;
    i1 = 32u;
    i2 = p1;
    i3 = l14;
    i4 = l13;
    i5 = 8192u;
    i4 ^= i5;
    f33(i0, i1, i2, i3, i4);
    goto L2;
    B5:;
  i0 = 0u;
  l20 = i0;
  goto B0;
  B1:;
  i0 = 4294967295u;
  l20 = i0;
  B0:;
  i0 = l7;
  i1 = 80u;
  i0 += i1;
  g0 = i0;
  i0 = l20;
  FUNC_EPILOGUE;
  return i0;
}

static void f30(u32 p0, u32 p1, u32 p2) {
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = p0;
  i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
  i1 = 32u;
  i0 &= i1;
  if (i0) {goto B0;}
  i0 = p1;
  i1 = p2;
  i2 = p0;
  i0 = f27(i0, i1, i2);
  B0:;
  FUNC_EPILOGUE;
}

static u32 f31(u32 p0) {
  u32 l1 = 0, l2 = 0, l3 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = 0u;
  l1 = i0;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  i0 = i32_load8_s(Z_envZ_memory, (u64)(i0));
  i0 = f18(i0);
  i0 = !(i0);
  if (i0) {goto B0;}
  L1: 
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l2 = i0;
    i0 = i32_load8_s(Z_envZ_memory, (u64)(i0));
    l3 = i0;
    i0 = p0;
    i1 = l2;
    i2 = 1u;
    i1 += i2;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = l3;
    i1 = l1;
    i2 = 10u;
    i1 *= i2;
    i0 += i1;
    i1 = 4294967248u;
    i0 += i1;
    l1 = i0;
    i0 = l2;
    i0 = i32_load8_s(Z_envZ_memory, (u64)(i0 + 1));
    i0 = f18(i0);
    if (i0) {goto L1;}
  B0:;
  i0 = l1;
  FUNC_EPILOGUE;
  return i0;
}

static void f32_0(u32 p0, u32 p1, u32 p2, u32 p3) {
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j1;
  i0 = p1;
  i1 = 20u;
  i0 = i0 > i1;
  if (i0) {goto B0;}
  i0 = p1;
  i1 = 4294967287u;
  i0 += i1;
  p1 = i0;
  i1 = 9u;
  i0 = i0 > i1;
  if (i0) {goto B0;}
  i0 = p1;
  switch (i0) {
    case 0: goto B10;
    case 1: goto B9;
    case 2: goto B8;
    case 3: goto B7;
    case 4: goto B6;
    case 5: goto B5;
    case 6: goto B4;
    case 7: goto B3;
    case 8: goto B2;
    case 9: goto B1;
    default: goto B10;
  }
  B10:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  p1 = i1;
  i2 = 4u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  goto Bfunc;
  B9:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  p1 = i1;
  i2 = 4u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  j1 = i64_load32_s(Z_envZ_memory, (u64)(i1));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  goto Bfunc;
  B8:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  p1 = i1;
  i2 = 4u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  j1 = i64_load32_u(Z_envZ_memory, (u64)(i1));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  goto Bfunc;
  B7:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  i2 = 7u;
  i1 += i2;
  i2 = 4294967288u;
  i1 &= i2;
  p1 = i1;
  i2 = 8u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  j1 = i64_load(Z_envZ_memory, (u64)(i1));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  goto Bfunc;
  B6:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  p1 = i1;
  i2 = 4u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  j1 = i64_load16_s(Z_envZ_memory, (u64)(i1));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  goto Bfunc;
  B5:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  p1 = i1;
  i2 = 4u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  j1 = i32_load16_u(Z_envZ_memory, (u64)(i1));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  goto Bfunc;
  B4:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  p1 = i1;
  i2 = 4u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  j1 = i64_load8_s(Z_envZ_memory, (u64)(i1));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  goto Bfunc;
  B3:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  p1 = i1;
  i2 = 4u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  j1 = i64_load8_u(Z_envZ_memory, (u64)(i1));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  goto Bfunc;
  B2:;
  i0 = p2;
  i1 = p2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  i2 = 7u;
  i1 += i2;
  i2 = 4294967288u;
  i1 &= i2;
  p1 = i1;
  i2 = 8u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  j1 = i64_load(Z_envZ_memory, (u64)(i1));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  goto Bfunc;
  B1:;
  i0 = p0;
  i1 = p2;
  i2 = p3;
  CALL_INDIRECT((*Z_envZ_table), void (*)(u32, u32), 2, i2, i0, i1);
  B0:;
  Bfunc:;
  FUNC_EPILOGUE;
}

static void f33(u32 p0, u32 p1, u32 p2, u32 p3, u32 p4) {
  u32 l5 = 0, l6 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4, i5;
  i0 = g0;
  i1 = 256u;
  i0 -= i1;
  l5 = i0;
  g0 = i0;
  i0 = p2;
  i1 = p3;
  i0 = (u32)((s32)i0 <= (s32)i1);
  if (i0) {goto B0;}
  i0 = p4;
  i1 = 73728u;
  i0 &= i1;
  if (i0) {goto B0;}
  i0 = l5;
  i1 = p1;
  i2 = p2;
  i3 = p3;
  i2 -= i3;
  p4 = i2;
  i3 = 256u;
  i4 = p4;
  i5 = 256u;
  i4 = i4 < i5;
  l6 = i4;
  i2 = i4 ? i2 : i3;
  i0 = f54(i0, i1, i2);
  i0 = l6;
  if (i0) {goto B1;}
  i0 = p2;
  i1 = p3;
  i0 -= i1;
  p2 = i0;
  L2: 
    i0 = p0;
    i1 = l5;
    i2 = 256u;
    f30(i0, i1, i2);
    i0 = p4;
    i1 = 4294967040u;
    i0 += i1;
    p4 = i0;
    i1 = 255u;
    i0 = i0 > i1;
    if (i0) {goto L2;}
  i0 = p2;
  i1 = 255u;
  i0 &= i1;
  p4 = i0;
  B1:;
  i0 = p0;
  i1 = l5;
  i2 = p4;
  f30(i0, i1, i2);
  B0:;
  i0 = l5;
  i1 = 256u;
  i0 += i1;
  g0 = i0;
  FUNC_EPILOGUE;
}

static u32 f34(u64 p0, u32 p1) {
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j0, j1;
  j0 = p0;
  i0 = !(j0);
  if (i0) {goto B0;}
  L1: 
    i0 = p1;
    i1 = 4294967295u;
    i0 += i1;
    p1 = i0;
    j1 = p0;
    i1 = (u32)(j1);
    i2 = 7u;
    i1 &= i2;
    i2 = 48u;
    i1 |= i2;
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    j0 = p0;
    j1 = 3ull;
    j0 >>= (j1 & 63);
    p0 = j0;
    j1 = 0ull;
    i0 = j0 != j1;
    if (i0) {goto L1;}
  B0:;
  i0 = p1;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f35(u64 p0, u32 p1, u32 p2) {
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j0, j1;
  j0 = p0;
  i0 = !(j0);
  if (i0) {goto B0;}
  L1: 
    i0 = p1;
    i1 = 4294967295u;
    i0 += i1;
    p1 = i0;
    j1 = p0;
    i1 = (u32)(j1);
    i2 = 15u;
    i1 &= i2;
    i2 = 1552u;
    i1 += i2;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1));
    i2 = p2;
    i1 |= i2;
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    j0 = p0;
    j1 = 4ull;
    j0 >>= (j1 & 63);
    p0 = j0;
    j1 = 0ull;
    i0 = j0 != j1;
    if (i0) {goto L1;}
  B0:;
  i0 = p1;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f36(u64 p0, u32 p1) {
  u32 l2 = 0, l3 = 0, l4 = 0;
  u64 l5 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3;
  u64 j0, j1, j2, j3;
  j0 = p0;
  j1 = 4294967296ull;
  i0 = j0 >= j1;
  if (i0) {goto B1;}
  j0 = p0;
  l5 = j0;
  goto B0;
  B1:;
  L2: 
    i0 = p1;
    i1 = 4294967295u;
    i0 += i1;
    p1 = i0;
    j1 = p0;
    j2 = p0;
    j3 = 10ull;
    j2 = DIV_U(j2, j3);
    l5 = j2;
    j3 = 10ull;
    j2 *= j3;
    j1 -= j2;
    i1 = (u32)(j1);
    i2 = 48u;
    i1 |= i2;
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    j0 = p0;
    j1 = 42949672959ull;
    i0 = j0 > j1;
    l2 = i0;
    j0 = l5;
    p0 = j0;
    i0 = l2;
    if (i0) {goto L2;}
  B0:;
  j0 = l5;
  i0 = (u32)(j0);
  l2 = i0;
  i0 = !(i0);
  if (i0) {goto B3;}
  L4: 
    i0 = p1;
    i1 = 4294967295u;
    i0 += i1;
    p1 = i0;
    i1 = l2;
    i2 = l2;
    i3 = 10u;
    i2 = DIV_U(i2, i3);
    l3 = i2;
    i3 = 10u;
    i2 *= i3;
    i1 -= i2;
    i2 = 48u;
    i1 |= i2;
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    i0 = l2;
    i1 = 9u;
    i0 = i0 > i1;
    l4 = i0;
    i0 = l3;
    l2 = i0;
    i0 = l4;
    if (i0) {goto L4;}
  B3:;
  i0 = p1;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f37(u32 p0, u32 p1, u32 p2) {
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4;
  i0 = p0;
  i1 = p1;
  i2 = p2;
  i3 = 4u;
  i4 = 5u;
  i0 = f28(i0, i1, i2, i3, i4);
  FUNC_EPILOGUE;
  return i0;
}

static u32 f38(u32 p0, f64 p1, u32 p2, u32 p3, u32 p4, u32 p5) {
  u32 l6 = 0, l7 = 0, l8 = 0, l9 = 0, l10 = 0, l11 = 0, l12 = 0, l13 = 0, 
      l14 = 0, l15 = 0, l16 = 0, l17 = 0, l18 = 0, l19 = 0, l20 = 0, l21 = 0;
  u64 l22 = 0, l23 = 0;
  f64 l24 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4, i5;
  u64 j0, j1, j2, j3;
  f64 d0, d1, d2, d3, d4;
  i0 = g0;
  i1 = 560u;
  i0 -= i1;
  l6 = i0;
  g0 = i0;
  i0 = l6;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 44), i1);
  d0 = p1;
  j0 = f40(d0);
  l22 = j0;
  j1 = 18446744073709551615ull;
  i0 = (u64)((s64)j0 > (s64)j1);
  if (i0) {goto B1;}
  d0 = p1;
  d0 = -(d0);
  p1 = d0;
  j0 = f40(d0);
  l22 = j0;
  i0 = 1u;
  l7 = i0;
  i0 = 1568u;
  l8 = i0;
  goto B0;
  B1:;
  i0 = p4;
  i1 = 2048u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B2;}
  i0 = 1u;
  l7 = i0;
  i0 = 1571u;
  l8 = i0;
  goto B0;
  B2:;
  i0 = 1574u;
  i1 = 1569u;
  i2 = p4;
  i3 = 1u;
  i2 &= i3;
  l7 = i2;
  i0 = i2 ? i0 : i1;
  l8 = i0;
  B0:;
  j0 = l22;
  j1 = 9218868437227405312ull;
  j0 &= j1;
  j1 = 9218868437227405312ull;
  i0 = j0 != j1;
  if (i0) {goto B4;}
  i0 = p0;
  i1 = 32u;
  i2 = p2;
  i3 = l7;
  i4 = 3u;
  i3 += i4;
  l9 = i3;
  i4 = p4;
  i5 = 4294901759u;
  i4 &= i5;
  f33(i0, i1, i2, i3, i4);
  i0 = p0;
  i1 = l8;
  i2 = l7;
  f30(i0, i1, i2);
  i0 = p0;
  i1 = 1595u;
  i2 = 1599u;
  i3 = p5;
  i4 = 5u;
  i3 >>= (i4 & 31);
  i4 = 1u;
  i3 &= i4;
  l10 = i3;
  i1 = i3 ? i1 : i2;
  i2 = 1587u;
  i3 = 1591u;
  i4 = l10;
  i2 = i4 ? i2 : i3;
  d3 = p1;
  d4 = p1;
  i3 = d3 != d4;
  i1 = i3 ? i1 : i2;
  i2 = 3u;
  f30(i0, i1, i2);
  i0 = p0;
  i1 = 32u;
  i2 = p2;
  i3 = l9;
  i4 = p4;
  i5 = 8192u;
  i4 ^= i5;
  f33(i0, i1, i2, i3, i4);
  goto B3;
  B4:;
  d0 = p1;
  i1 = l6;
  i2 = 44u;
  i1 += i2;
  d0 = f23(d0, i1);
  p1 = d0;
  d1 = p1;
  d0 += d1;
  p1 = d0;
  d1 = 0;
  i0 = d0 == d1;
  if (i0) {goto B5;}
  i0 = l6;
  i1 = l6;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 44));
  i2 = 4294967295u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 44), i1);
  B5:;
  i0 = l6;
  i1 = 16u;
  i0 += i1;
  l11 = i0;
  i0 = p5;
  i1 = 32u;
  i0 |= i1;
  l12 = i0;
  i1 = 97u;
  i0 = i0 != i1;
  if (i0) {goto B6;}
  i0 = l8;
  i1 = 9u;
  i0 += i1;
  i1 = l8;
  i2 = p5;
  i3 = 32u;
  i2 &= i3;
  l13 = i2;
  i0 = i2 ? i0 : i1;
  l14 = i0;
  i0 = p3;
  i1 = 11u;
  i0 = i0 > i1;
  if (i0) {goto B7;}
  i0 = 12u;
  i1 = p3;
  i0 -= i1;
  l10 = i0;
  i0 = !(i0);
  if (i0) {goto B7;}
  d0 = 8;
  l24 = d0;
  L8: 
    d0 = l24;
    d1 = 16;
    d0 *= d1;
    l24 = d0;
    i0 = l10;
    i1 = 4294967295u;
    i0 += i1;
    l10 = i0;
    if (i0) {goto L8;}
  i0 = l14;
  i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
  i1 = 45u;
  i0 = i0 != i1;
  if (i0) {goto B9;}
  d0 = l24;
  d1 = p1;
  d1 = -(d1);
  d2 = l24;
  d1 -= d2;
  d0 += d1;
  d0 = -(d0);
  p1 = d0;
  goto B7;
  B9:;
  d0 = p1;
  d1 = l24;
  d0 += d1;
  d1 = l24;
  d0 -= d1;
  p1 = d0;
  B7:;
  i0 = l6;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 44));
  l10 = i0;
  i1 = l10;
  i2 = 31u;
  i1 = (u32)((s32)i1 >> (i2 & 31));
  l10 = i1;
  i0 += i1;
  i1 = l10;
  i0 ^= i1;
  j0 = (u64)(i0);
  i1 = l11;
  i0 = f36(j0, i1);
  l10 = i0;
  i1 = l11;
  i0 = i0 != i1;
  if (i0) {goto B10;}
  i0 = l6;
  i1 = 48u;
  i32_store8(Z_envZ_memory, (u64)(i0 + 15), i1);
  i0 = l6;
  i1 = 15u;
  i0 += i1;
  l10 = i0;
  B10:;
  i0 = l7;
  i1 = 2u;
  i0 |= i1;
  l15 = i0;
  i0 = l6;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 44));
  l16 = i0;
  i0 = l10;
  i1 = 4294967294u;
  i0 += i1;
  l17 = i0;
  i1 = p5;
  i2 = 15u;
  i1 += i2;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = l10;
  i1 = 4294967295u;
  i0 += i1;
  i1 = 45u;
  i2 = 43u;
  i3 = l16;
  i4 = 0u;
  i3 = (u32)((s32)i3 < (s32)i4);
  i1 = i3 ? i1 : i2;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = p4;
  i1 = 8u;
  i0 &= i1;
  l18 = i0;
  i0 = l6;
  i1 = 16u;
  i0 += i1;
  l16 = i0;
  L11: 
    i0 = l16;
    l10 = i0;
    d0 = p1;
    d0 = fabs(d0);
    d1 = 2147483648;
    i0 = d0 < d1;
    i0 = !(i0);
    if (i0) {goto B13;}
    d0 = p1;
    i0 = I32_TRUNC_S_F64(d0);
    l16 = i0;
    goto B12;
    B13:;
    i0 = 2147483648u;
    l16 = i0;
    B12:;
    i0 = l10;
    i1 = l16;
    i2 = 1552u;
    i1 += i2;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1));
    i2 = l13;
    i1 |= i2;
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    d0 = p1;
    i1 = l16;
    d1 = (f64)(s32)(i1);
    d0 -= d1;
    d1 = 16;
    d0 *= d1;
    p1 = d0;
    i0 = l10;
    i1 = 1u;
    i0 += i1;
    l16 = i0;
    i1 = l6;
    i2 = 16u;
    i1 += i2;
    i0 -= i1;
    i1 = 1u;
    i0 = i0 != i1;
    if (i0) {goto B14;}
    i0 = l18;
    if (i0) {goto B15;}
    i0 = p3;
    i1 = 0u;
    i0 = (u32)((s32)i0 > (s32)i1);
    if (i0) {goto B15;}
    d0 = p1;
    d1 = 0;
    i0 = d0 == d1;
    if (i0) {goto B14;}
    B15:;
    i0 = l10;
    i1 = 46u;
    i32_store8(Z_envZ_memory, (u64)(i0 + 1), i1);
    i0 = l10;
    i1 = 2u;
    i0 += i1;
    l16 = i0;
    B14:;
    d0 = p1;
    d1 = 0;
    i0 = d0 != d1;
    if (i0) {goto L11;}
  i0 = p3;
  i0 = !(i0);
  if (i0) {goto B17;}
  i0 = l16;
  i1 = l6;
  i2 = 16u;
  i1 += i2;
  i0 -= i1;
  i1 = 4294967294u;
  i0 += i1;
  i1 = p3;
  i0 = (u32)((s32)i0 >= (s32)i1);
  if (i0) {goto B17;}
  i0 = p3;
  i1 = l11;
  i0 += i1;
  i1 = l17;
  i0 -= i1;
  i1 = 2u;
  i0 += i1;
  l10 = i0;
  goto B16;
  B17:;
  i0 = l11;
  i1 = l6;
  i2 = 16u;
  i1 += i2;
  i0 -= i1;
  i1 = l17;
  i0 -= i1;
  i1 = l16;
  i0 += i1;
  l10 = i0;
  B16:;
  i0 = p0;
  i1 = 32u;
  i2 = p2;
  i3 = l10;
  i4 = l15;
  i3 += i4;
  l9 = i3;
  i4 = p4;
  f33(i0, i1, i2, i3, i4);
  i0 = p0;
  i1 = l14;
  i2 = l15;
  f30(i0, i1, i2);
  i0 = p0;
  i1 = 48u;
  i2 = p2;
  i3 = l9;
  i4 = p4;
  i5 = 65536u;
  i4 ^= i5;
  f33(i0, i1, i2, i3, i4);
  i0 = p0;
  i1 = l6;
  i2 = 16u;
  i1 += i2;
  i2 = l16;
  i3 = l6;
  i4 = 16u;
  i3 += i4;
  i2 -= i3;
  l16 = i2;
  f30(i0, i1, i2);
  i0 = p0;
  i1 = 48u;
  i2 = l10;
  i3 = l16;
  i4 = l11;
  i5 = l17;
  i4 -= i5;
  l13 = i4;
  i3 += i4;
  i2 -= i3;
  i3 = 0u;
  i4 = 0u;
  f33(i0, i1, i2, i3, i4);
  i0 = p0;
  i1 = l17;
  i2 = l13;
  f30(i0, i1, i2);
  i0 = p0;
  i1 = 32u;
  i2 = p2;
  i3 = l9;
  i4 = p4;
  i5 = 8192u;
  i4 ^= i5;
  f33(i0, i1, i2, i3, i4);
  goto B3;
  B6:;
  i0 = p3;
  i1 = 0u;
  i0 = (u32)((s32)i0 < (s32)i1);
  l10 = i0;
  d0 = p1;
  d1 = 0;
  i0 = d0 != d1;
  if (i0) {goto B19;}
  i0 = l6;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 44));
  l18 = i0;
  goto B18;
  B19:;
  i0 = l6;
  i1 = l6;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 44));
  i2 = 4294967268u;
  i1 += i2;
  l18 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 44), i1);
  d0 = p1;
  d1 = 268435456;
  d0 *= d1;
  p1 = d0;
  B18:;
  i0 = 6u;
  i1 = p3;
  i2 = l10;
  i0 = i2 ? i0 : i1;
  l14 = i0;
  i0 = l6;
  i1 = 48u;
  i0 += i1;
  i1 = l6;
  i2 = 336u;
  i1 += i2;
  i2 = l18;
  i3 = 0u;
  i2 = (u32)((s32)i2 < (s32)i3);
  i0 = i2 ? i0 : i1;
  l19 = i0;
  l13 = i0;
  L20: 
    d0 = p1;
    d1 = 4294967296;
    i0 = d0 < d1;
    d1 = p1;
    d2 = 0;
    i1 = d1 >= d2;
    i0 &= i1;
    i0 = !(i0);
    if (i0) {goto B22;}
    d0 = p1;
    i0 = I32_TRUNC_U_F64(d0);
    l10 = i0;
    goto B21;
    B22:;
    i0 = 0u;
    l10 = i0;
    B21:;
    i0 = l13;
    i1 = l10;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = l13;
    i1 = 4u;
    i0 += i1;
    l13 = i0;
    d0 = p1;
    i1 = l10;
    d1 = (f64)(i1);
    d0 -= d1;
    d1 = 1000000000;
    d0 *= d1;
    p1 = d0;
    d1 = 0;
    i0 = d0 != d1;
    if (i0) {goto L20;}
  i0 = l18;
  i1 = 1u;
  i0 = (u32)((s32)i0 >= (s32)i1);
  if (i0) {goto B24;}
  i0 = l13;
  l10 = i0;
  i0 = l19;
  l16 = i0;
  goto B23;
  B24:;
  i0 = l19;
  l16 = i0;
  L25: 
    i0 = l18;
    i1 = 29u;
    i2 = l18;
    i3 = 29u;
    i2 = (u32)((s32)i2 < (s32)i3);
    i0 = i2 ? i0 : i1;
    l18 = i0;
    i0 = l13;
    i1 = 4294967292u;
    i0 += i1;
    l10 = i0;
    i1 = l16;
    i0 = i0 < i1;
    if (i0) {goto B26;}
    i0 = l18;
    j0 = (u64)(i0);
    l23 = j0;
    j0 = 0ull;
    l22 = j0;
    L27: 
      i0 = l10;
      i1 = l10;
      j1 = i64_load32_u(Z_envZ_memory, (u64)(i1));
      j2 = l23;
      j1 <<= (j2 & 63);
      j2 = l22;
      j3 = 4294967295ull;
      j2 &= j3;
      j1 += j2;
      l22 = j1;
      j2 = l22;
      j3 = 1000000000ull;
      j2 = DIV_U(j2, j3);
      l22 = j2;
      j3 = 1000000000ull;
      j2 *= j3;
      j1 -= j2;
      i64_store32(Z_envZ_memory, (u64)(i0), j1);
      i0 = l10;
      i1 = 4294967292u;
      i0 += i1;
      l10 = i0;
      i1 = l16;
      i0 = i0 >= i1;
      if (i0) {goto L27;}
    j0 = l22;
    i0 = (u32)(j0);
    l10 = i0;
    i0 = !(i0);
    if (i0) {goto B26;}
    i0 = l16;
    i1 = 4294967292u;
    i0 += i1;
    l16 = i0;
    i1 = l10;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    B26:;
    L29: 
      i0 = l13;
      l10 = i0;
      i1 = l16;
      i0 = i0 <= i1;
      if (i0) {goto B28;}
      i0 = l10;
      i1 = 4294967292u;
      i0 += i1;
      l13 = i0;
      i0 = i32_load(Z_envZ_memory, (u64)(i0));
      i0 = !(i0);
      if (i0) {goto L29;}
    B28:;
    i0 = l6;
    i1 = l6;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 44));
    i2 = l18;
    i1 -= i2;
    l18 = i1;
    i32_store(Z_envZ_memory, (u64)(i0 + 44), i1);
    i0 = l10;
    l13 = i0;
    i0 = l18;
    i1 = 0u;
    i0 = (u32)((s32)i0 > (s32)i1);
    if (i0) {goto L25;}
  B23:;
  i0 = l18;
  i1 = 4294967295u;
  i0 = (u32)((s32)i0 > (s32)i1);
  if (i0) {goto B30;}
  i0 = l14;
  i1 = 25u;
  i0 += i1;
  i1 = 9u;
  i0 = I32_DIV_S(i0, i1);
  i1 = 1u;
  i0 += i1;
  l20 = i0;
  i0 = l12;
  i1 = 102u;
  i0 = i0 == i1;
  l21 = i0;
  L31: 
    i0 = 9u;
    i1 = 0u;
    i2 = l18;
    i1 -= i2;
    i2 = l18;
    i3 = 4294967287u;
    i2 = (u32)((s32)i2 < (s32)i3);
    i0 = i2 ? i0 : i1;
    l9 = i0;
    i0 = l16;
    i1 = l10;
    i0 = i0 < i1;
    if (i0) {goto B33;}
    i0 = l16;
    i1 = l16;
    i2 = 4u;
    i1 += i2;
    i2 = l16;
    i2 = i32_load(Z_envZ_memory, (u64)(i2));
    i0 = i2 ? i0 : i1;
    l16 = i0;
    goto B32;
    B33:;
    i0 = 1000000000u;
    i1 = l9;
    i0 >>= (i1 & 31);
    l17 = i0;
    i0 = 4294967295u;
    i1 = l9;
    i0 <<= (i1 & 31);
    i1 = 4294967295u;
    i0 ^= i1;
    l15 = i0;
    i0 = 0u;
    l18 = i0;
    i0 = l16;
    l13 = i0;
    L34: 
      i0 = l13;
      i1 = l13;
      i1 = i32_load(Z_envZ_memory, (u64)(i1));
      p3 = i1;
      i2 = l9;
      i1 >>= (i2 & 31);
      i2 = l18;
      i1 += i2;
      i32_store(Z_envZ_memory, (u64)(i0), i1);
      i0 = p3;
      i1 = l15;
      i0 &= i1;
      i1 = l17;
      i0 *= i1;
      l18 = i0;
      i0 = l13;
      i1 = 4u;
      i0 += i1;
      l13 = i0;
      i1 = l10;
      i0 = i0 < i1;
      if (i0) {goto L34;}
    i0 = l16;
    i1 = l16;
    i2 = 4u;
    i1 += i2;
    i2 = l16;
    i2 = i32_load(Z_envZ_memory, (u64)(i2));
    i0 = i2 ? i0 : i1;
    l16 = i0;
    i0 = l18;
    i0 = !(i0);
    if (i0) {goto B32;}
    i0 = l10;
    i1 = l18;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = l10;
    i1 = 4u;
    i0 += i1;
    l10 = i0;
    B32:;
    i0 = l6;
    i1 = l6;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 44));
    i2 = l9;
    i1 += i2;
    l18 = i1;
    i32_store(Z_envZ_memory, (u64)(i0 + 44), i1);
    i0 = l19;
    i1 = l16;
    i2 = l21;
    i0 = i2 ? i0 : i1;
    l13 = i0;
    i1 = l20;
    i2 = 2u;
    i1 <<= (i2 & 31);
    i0 += i1;
    i1 = l10;
    i2 = l10;
    i3 = l13;
    i2 -= i3;
    i3 = 2u;
    i2 = (u32)((s32)i2 >> (i3 & 31));
    i3 = l20;
    i2 = (u32)((s32)i2 > (s32)i3);
    i0 = i2 ? i0 : i1;
    l10 = i0;
    i0 = l18;
    i1 = 0u;
    i0 = (u32)((s32)i0 < (s32)i1);
    if (i0) {goto L31;}
  B30:;
  i0 = 0u;
  l13 = i0;
  i0 = l16;
  i1 = l10;
  i0 = i0 >= i1;
  if (i0) {goto B35;}
  i0 = l19;
  i1 = l16;
  i0 -= i1;
  i1 = 2u;
  i0 = (u32)((s32)i0 >> (i1 & 31));
  i1 = 9u;
  i0 *= i1;
  l13 = i0;
  i0 = 10u;
  l18 = i0;
  i0 = l16;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  p3 = i0;
  i1 = 10u;
  i0 = i0 < i1;
  if (i0) {goto B35;}
  L36: 
    i0 = l13;
    i1 = 1u;
    i0 += i1;
    l13 = i0;
    i0 = p3;
    i1 = l18;
    i2 = 10u;
    i1 *= i2;
    l18 = i1;
    i0 = i0 >= i1;
    if (i0) {goto L36;}
  B35:;
  i0 = l14;
  i1 = 0u;
  i2 = l13;
  i3 = l12;
  i4 = 102u;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 -= i1;
  i1 = l14;
  i2 = 0u;
  i1 = i1 != i2;
  i2 = l12;
  i3 = 103u;
  i2 = i2 == i3;
  i1 &= i2;
  i0 -= i1;
  l18 = i0;
  i1 = l10;
  i2 = l19;
  i1 -= i2;
  i2 = 2u;
  i1 = (u32)((s32)i1 >> (i2 & 31));
  i2 = 9u;
  i1 *= i2;
  i2 = 4294967287u;
  i1 += i2;
  i0 = (u32)((s32)i0 >= (s32)i1);
  if (i0) {goto B37;}
  i0 = l18;
  i1 = 9216u;
  i0 += i1;
  p3 = i0;
  i1 = 9u;
  i0 = I32_DIV_S(i0, i1);
  l17 = i0;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = l19;
  i0 += i1;
  i1 = 4294963204u;
  i0 += i1;
  l9 = i0;
  i0 = 10u;
  l18 = i0;
  i0 = p3;
  i1 = l17;
  i2 = 9u;
  i1 *= i2;
  i0 -= i1;
  i1 = 1u;
  i0 += i1;
  p3 = i0;
  i1 = 8u;
  i0 = (u32)((s32)i0 > (s32)i1);
  if (i0) {goto B38;}
  L39: 
    i0 = l18;
    i1 = 10u;
    i0 *= i1;
    l18 = i0;
    i0 = p3;
    i1 = 1u;
    i0 += i1;
    p3 = i0;
    i1 = 9u;
    i0 = i0 != i1;
    if (i0) {goto L39;}
  B38:;
  i0 = l9;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l17 = i0;
  i1 = l17;
  i2 = l18;
  i1 = DIV_U(i1, i2);
  l15 = i1;
  i2 = l18;
  i1 *= i2;
  i0 -= i1;
  p3 = i0;
  i0 = l9;
  i1 = 4u;
  i0 += i1;
  l20 = i0;
  i1 = l10;
  i0 = i0 != i1;
  if (i0) {goto B41;}
  i0 = p3;
  i0 = !(i0);
  if (i0) {goto B40;}
  B41:;
  d0 = 0.5;
  d1 = 1;
  d2 = 1.5;
  i3 = p3;
  i4 = l18;
  i5 = 1u;
  i4 >>= (i5 & 31);
  l21 = i4;
  i3 = i3 == i4;
  d1 = i3 ? d1 : d2;
  d2 = 1.5;
  i3 = l20;
  i4 = l10;
  i3 = i3 == i4;
  d1 = i3 ? d1 : d2;
  i2 = p3;
  i3 = l21;
  i2 = i2 < i3;
  d0 = i2 ? d0 : d1;
  l24 = d0;
  d0 = 9007199254740994;
  d1 = 9007199254740992;
  i2 = l15;
  i3 = 1u;
  i2 &= i3;
  d0 = i2 ? d0 : d1;
  p1 = d0;
  i0 = l7;
  i0 = !(i0);
  if (i0) {goto B42;}
  i0 = l8;
  i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
  i1 = 45u;
  i0 = i0 != i1;
  if (i0) {goto B42;}
  d0 = l24;
  d0 = -(d0);
  l24 = d0;
  d0 = p1;
  d0 = -(d0);
  p1 = d0;
  B42:;
  i0 = l9;
  i1 = l17;
  i2 = p3;
  i1 -= i2;
  p3 = i1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  d0 = p1;
  d1 = l24;
  d0 += d1;
  d1 = p1;
  i0 = d0 == d1;
  if (i0) {goto B40;}
  i0 = l9;
  i1 = p3;
  i2 = l18;
  i1 += i2;
  l13 = i1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l13;
  i1 = 1000000000u;
  i0 = i0 < i1;
  if (i0) {goto B43;}
  L44: 
    i0 = l9;
    i1 = 0u;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = l9;
    i1 = 4294967292u;
    i0 += i1;
    l9 = i0;
    i1 = l16;
    i0 = i0 >= i1;
    if (i0) {goto B45;}
    i0 = l16;
    i1 = 4294967292u;
    i0 += i1;
    l16 = i0;
    i1 = 0u;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    B45:;
    i0 = l9;
    i1 = l9;
    i1 = i32_load(Z_envZ_memory, (u64)(i1));
    i2 = 1u;
    i1 += i2;
    l13 = i1;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = l13;
    i1 = 999999999u;
    i0 = i0 > i1;
    if (i0) {goto L44;}
  B43:;
  i0 = l19;
  i1 = l16;
  i0 -= i1;
  i1 = 2u;
  i0 = (u32)((s32)i0 >> (i1 & 31));
  i1 = 9u;
  i0 *= i1;
  l13 = i0;
  i0 = 10u;
  l18 = i0;
  i0 = l16;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  p3 = i0;
  i1 = 10u;
  i0 = i0 < i1;
  if (i0) {goto B40;}
  L46: 
    i0 = l13;
    i1 = 1u;
    i0 += i1;
    l13 = i0;
    i0 = p3;
    i1 = l18;
    i2 = 10u;
    i1 *= i2;
    l18 = i1;
    i0 = i0 >= i1;
    if (i0) {goto L46;}
  B40:;
  i0 = l9;
  i1 = 4u;
  i0 += i1;
  l18 = i0;
  i1 = l10;
  i2 = l10;
  i3 = l18;
  i2 = i2 > i3;
  i0 = i2 ? i0 : i1;
  l10 = i0;
  B37:;
  L48: 
    i0 = l10;
    l18 = i0;
    i1 = l16;
    i0 = i0 > i1;
    if (i0) {goto B49;}
    i0 = 0u;
    l21 = i0;
    goto B47;
    B49:;
    i0 = l18;
    i1 = 4294967292u;
    i0 += i1;
    l10 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    i0 = !(i0);
    if (i0) {goto L48;}
  i0 = 1u;
  l21 = i0;
  B47:;
  i0 = l12;
  i1 = 103u;
  i0 = i0 == i1;
  if (i0) {goto B51;}
  i0 = p4;
  i1 = 8u;
  i0 &= i1;
  l15 = i0;
  goto B50;
  B51:;
  i0 = l13;
  i1 = 4294967295u;
  i0 ^= i1;
  i1 = 4294967295u;
  i2 = l14;
  i3 = 1u;
  i4 = l14;
  i2 = i4 ? i2 : i3;
  l10 = i2;
  i3 = l13;
  i2 = (u32)((s32)i2 > (s32)i3);
  i3 = l13;
  i4 = 4294967291u;
  i3 = (u32)((s32)i3 > (s32)i4);
  i2 &= i3;
  p3 = i2;
  i0 = i2 ? i0 : i1;
  i1 = l10;
  i0 += i1;
  l14 = i0;
  i0 = 4294967295u;
  i1 = 4294967294u;
  i2 = p3;
  i0 = i2 ? i0 : i1;
  i1 = p5;
  i0 += i1;
  p5 = i0;
  i0 = p4;
  i1 = 8u;
  i0 &= i1;
  l15 = i0;
  if (i0) {goto B50;}
  i0 = 9u;
  l10 = i0;
  i0 = l21;
  i0 = !(i0);
  if (i0) {goto B52;}
  i0 = 9u;
  l10 = i0;
  i0 = l18;
  i1 = 4294967292u;
  i0 += i1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l9 = i0;
  i0 = !(i0);
  if (i0) {goto B52;}
  i0 = 10u;
  p3 = i0;
  i0 = 0u;
  l10 = i0;
  i0 = l9;
  i1 = 10u;
  i0 = REM_U(i0, i1);
  if (i0) {goto B52;}
  L53: 
    i0 = l10;
    i1 = 1u;
    i0 += i1;
    l10 = i0;
    i0 = l9;
    i1 = p3;
    i2 = 10u;
    i1 *= i2;
    p3 = i1;
    i0 = REM_U(i0, i1);
    i0 = !(i0);
    if (i0) {goto L53;}
  B52:;
  i0 = l18;
  i1 = l19;
  i0 -= i1;
  i1 = 2u;
  i0 = (u32)((s32)i0 >> (i1 & 31));
  i1 = 9u;
  i0 *= i1;
  i1 = 4294967287u;
  i0 += i1;
  p3 = i0;
  i0 = p5;
  i1 = 32u;
  i0 |= i1;
  i1 = 102u;
  i0 = i0 != i1;
  if (i0) {goto B54;}
  i0 = 0u;
  l15 = i0;
  i0 = l14;
  i1 = p3;
  i2 = l10;
  i1 -= i2;
  l10 = i1;
  i2 = 0u;
  i3 = l10;
  i4 = 0u;
  i3 = (u32)((s32)i3 > (s32)i4);
  i1 = i3 ? i1 : i2;
  l10 = i1;
  i2 = l14;
  i3 = l10;
  i2 = (u32)((s32)i2 < (s32)i3);
  i0 = i2 ? i0 : i1;
  l14 = i0;
  goto B50;
  B54:;
  i0 = 0u;
  l15 = i0;
  i0 = l14;
  i1 = p3;
  i2 = l13;
  i1 += i2;
  i2 = l10;
  i1 -= i2;
  l10 = i1;
  i2 = 0u;
  i3 = l10;
  i4 = 0u;
  i3 = (u32)((s32)i3 > (s32)i4);
  i1 = i3 ? i1 : i2;
  l10 = i1;
  i2 = l14;
  i3 = l10;
  i2 = (u32)((s32)i2 < (s32)i3);
  i0 = i2 ? i0 : i1;
  l14 = i0;
  B50:;
  i0 = l14;
  i1 = l15;
  i0 |= i1;
  l12 = i0;
  i1 = 0u;
  i0 = i0 != i1;
  p3 = i0;
  i0 = p5;
  i1 = 32u;
  i0 |= i1;
  l17 = i0;
  i1 = 102u;
  i0 = i0 != i1;
  if (i0) {goto B56;}
  i0 = l13;
  i1 = 0u;
  i2 = l13;
  i3 = 0u;
  i2 = (u32)((s32)i2 > (s32)i3);
  i0 = i2 ? i0 : i1;
  l10 = i0;
  goto B55;
  B56:;
  i0 = l11;
  i1 = l13;
  i2 = l13;
  i3 = 31u;
  i2 = (u32)((s32)i2 >> (i3 & 31));
  l10 = i2;
  i1 += i2;
  i2 = l10;
  i1 ^= i2;
  j1 = (u64)(i1);
  i2 = l11;
  i1 = f36(j1, i2);
  l10 = i1;
  i0 -= i1;
  i1 = 1u;
  i0 = (u32)((s32)i0 > (s32)i1);
  if (i0) {goto B57;}
  L58: 
    i0 = l10;
    i1 = 4294967295u;
    i0 += i1;
    l10 = i0;
    i1 = 48u;
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    i0 = l11;
    i1 = l10;
    i0 -= i1;
    i1 = 2u;
    i0 = (u32)((s32)i0 < (s32)i1);
    if (i0) {goto L58;}
  B57:;
  i0 = l10;
  i1 = 4294967294u;
  i0 += i1;
  l20 = i0;
  i1 = p5;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = l10;
  i1 = 4294967295u;
  i0 += i1;
  i1 = 45u;
  i2 = 43u;
  i3 = l13;
  i4 = 0u;
  i3 = (u32)((s32)i3 < (s32)i4);
  i1 = i3 ? i1 : i2;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = l11;
  i1 = l20;
  i0 -= i1;
  l10 = i0;
  B55:;
  i0 = p0;
  i1 = 32u;
  i2 = p2;
  i3 = l7;
  i4 = l14;
  i3 += i4;
  i4 = p3;
  i3 += i4;
  i4 = l10;
  i3 += i4;
  i4 = 1u;
  i3 += i4;
  l9 = i3;
  i4 = p4;
  f33(i0, i1, i2, i3, i4);
  i0 = p0;
  i1 = l8;
  i2 = l7;
  f30(i0, i1, i2);
  i0 = p0;
  i1 = 48u;
  i2 = p2;
  i3 = l9;
  i4 = p4;
  i5 = 65536u;
  i4 ^= i5;
  f33(i0, i1, i2, i3, i4);
  i0 = l17;
  i1 = 102u;
  i0 = i0 != i1;
  if (i0) {goto B60;}
  i0 = l6;
  i1 = 16u;
  i0 += i1;
  i1 = 8u;
  i0 |= i1;
  l17 = i0;
  i0 = l6;
  i1 = 16u;
  i0 += i1;
  i1 = 9u;
  i0 |= i1;
  l13 = i0;
  i0 = l19;
  i1 = l16;
  i2 = l16;
  i3 = l19;
  i2 = i2 > i3;
  i0 = i2 ? i0 : i1;
  p3 = i0;
  l16 = i0;
  L61: 
    i0 = l16;
    j0 = i64_load32_u(Z_envZ_memory, (u64)(i0));
    i1 = l13;
    i0 = f36(j0, i1);
    l10 = i0;
    i0 = l16;
    i1 = p3;
    i0 = i0 == i1;
    if (i0) {goto B63;}
    i0 = l10;
    i1 = l6;
    i2 = 16u;
    i1 += i2;
    i0 = i0 <= i1;
    if (i0) {goto B62;}
    L64: 
      i0 = l10;
      i1 = 4294967295u;
      i0 += i1;
      l10 = i0;
      i1 = 48u;
      i32_store8(Z_envZ_memory, (u64)(i0), i1);
      i0 = l10;
      i1 = l6;
      i2 = 16u;
      i1 += i2;
      i0 = i0 > i1;
      if (i0) {goto L64;}
      goto B62;
    UNREACHABLE;
    B63:;
    i0 = l10;
    i1 = l13;
    i0 = i0 != i1;
    if (i0) {goto B62;}
    i0 = l6;
    i1 = 48u;
    i32_store8(Z_envZ_memory, (u64)(i0 + 24), i1);
    i0 = l17;
    l10 = i0;
    B62:;
    i0 = p0;
    i1 = l10;
    i2 = l13;
    i3 = l10;
    i2 -= i3;
    f30(i0, i1, i2);
    i0 = l16;
    i1 = 4u;
    i0 += i1;
    l16 = i0;
    i1 = l19;
    i0 = i0 <= i1;
    if (i0) {goto L61;}
  i0 = l12;
  i0 = !(i0);
  if (i0) {goto B65;}
  i0 = p0;
  i1 = 1603u;
  i2 = 1u;
  f30(i0, i1, i2);
  B65:;
  i0 = l16;
  i1 = l18;
  i0 = i0 >= i1;
  if (i0) {goto B66;}
  i0 = l14;
  i1 = 1u;
  i0 = (u32)((s32)i0 < (s32)i1);
  if (i0) {goto B66;}
  L67: 
    i0 = l16;
    j0 = i64_load32_u(Z_envZ_memory, (u64)(i0));
    i1 = l13;
    i0 = f36(j0, i1);
    l10 = i0;
    i1 = l6;
    i2 = 16u;
    i1 += i2;
    i0 = i0 <= i1;
    if (i0) {goto B68;}
    L69: 
      i0 = l10;
      i1 = 4294967295u;
      i0 += i1;
      l10 = i0;
      i1 = 48u;
      i32_store8(Z_envZ_memory, (u64)(i0), i1);
      i0 = l10;
      i1 = l6;
      i2 = 16u;
      i1 += i2;
      i0 = i0 > i1;
      if (i0) {goto L69;}
    B68:;
    i0 = p0;
    i1 = l10;
    i2 = l14;
    i3 = 9u;
    i4 = l14;
    i5 = 9u;
    i4 = (u32)((s32)i4 < (s32)i5);
    i2 = i4 ? i2 : i3;
    f30(i0, i1, i2);
    i0 = l14;
    i1 = 4294967287u;
    i0 += i1;
    l14 = i0;
    i0 = l16;
    i1 = 4u;
    i0 += i1;
    l16 = i0;
    i1 = l18;
    i0 = i0 >= i1;
    if (i0) {goto B66;}
    i0 = l14;
    i1 = 0u;
    i0 = (u32)((s32)i0 > (s32)i1);
    if (i0) {goto L67;}
  B66:;
  i0 = p0;
  i1 = 48u;
  i2 = l14;
  i3 = 9u;
  i2 += i3;
  i3 = 9u;
  i4 = 0u;
  f33(i0, i1, i2, i3, i4);
  goto B59;
  B60:;
  i0 = l14;
  i1 = 0u;
  i0 = (u32)((s32)i0 < (s32)i1);
  if (i0) {goto B70;}
  i0 = l18;
  i1 = l16;
  i2 = 4u;
  i1 += i2;
  i2 = l21;
  i0 = i2 ? i0 : i1;
  l17 = i0;
  i0 = l6;
  i1 = 16u;
  i0 += i1;
  i1 = 8u;
  i0 |= i1;
  l19 = i0;
  i0 = l6;
  i1 = 16u;
  i0 += i1;
  i1 = 9u;
  i0 |= i1;
  l18 = i0;
  i0 = l16;
  l13 = i0;
  L71: 
    i0 = l13;
    j0 = i64_load32_u(Z_envZ_memory, (u64)(i0));
    i1 = l18;
    i0 = f36(j0, i1);
    l10 = i0;
    i1 = l18;
    i0 = i0 != i1;
    if (i0) {goto B72;}
    i0 = l6;
    i1 = 48u;
    i32_store8(Z_envZ_memory, (u64)(i0 + 24), i1);
    i0 = l19;
    l10 = i0;
    B72:;
    i0 = l13;
    i1 = l16;
    i0 = i0 == i1;
    if (i0) {goto B74;}
    i0 = l10;
    i1 = l6;
    i2 = 16u;
    i1 += i2;
    i0 = i0 <= i1;
    if (i0) {goto B73;}
    L75: 
      i0 = l10;
      i1 = 4294967295u;
      i0 += i1;
      l10 = i0;
      i1 = 48u;
      i32_store8(Z_envZ_memory, (u64)(i0), i1);
      i0 = l10;
      i1 = l6;
      i2 = 16u;
      i1 += i2;
      i0 = i0 > i1;
      if (i0) {goto L75;}
      goto B73;
    UNREACHABLE;
    B74:;
    i0 = p0;
    i1 = l10;
    i2 = 1u;
    f30(i0, i1, i2);
    i0 = l10;
    i1 = 1u;
    i0 += i1;
    l10 = i0;
    i0 = l15;
    if (i0) {goto B76;}
    i0 = l14;
    i1 = 1u;
    i0 = (u32)((s32)i0 < (s32)i1);
    if (i0) {goto B73;}
    B76:;
    i0 = p0;
    i1 = 1603u;
    i2 = 1u;
    f30(i0, i1, i2);
    B73:;
    i0 = p0;
    i1 = l10;
    i2 = l18;
    i3 = l10;
    i2 -= i3;
    p3 = i2;
    i3 = l14;
    i4 = l14;
    i5 = p3;
    i4 = (u32)((s32)i4 > (s32)i5);
    i2 = i4 ? i2 : i3;
    f30(i0, i1, i2);
    i0 = l14;
    i1 = p3;
    i0 -= i1;
    l14 = i0;
    i0 = l13;
    i1 = 4u;
    i0 += i1;
    l13 = i0;
    i1 = l17;
    i0 = i0 >= i1;
    if (i0) {goto B70;}
    i0 = l14;
    i1 = 4294967295u;
    i0 = (u32)((s32)i0 > (s32)i1);
    if (i0) {goto L71;}
  B70:;
  i0 = p0;
  i1 = 48u;
  i2 = l14;
  i3 = 18u;
  i2 += i3;
  i3 = 18u;
  i4 = 0u;
  f33(i0, i1, i2, i3, i4);
  i0 = p0;
  i1 = l20;
  i2 = l11;
  i3 = l20;
  i2 -= i3;
  f30(i0, i1, i2);
  B59:;
  i0 = p0;
  i1 = 32u;
  i2 = p2;
  i3 = l9;
  i4 = p4;
  i5 = 8192u;
  i4 ^= i5;
  f33(i0, i1, i2, i3, i4);
  B3:;
  i0 = l6;
  i1 = 560u;
  i0 += i1;
  g0 = i0;
  i0 = p2;
  i1 = l9;
  i2 = l9;
  i3 = p2;
  i2 = (u32)((s32)i2 < (s32)i3);
  i0 = i2 ? i0 : i1;
  FUNC_EPILOGUE;
  return i0;
}

static void f39(u32 p0, u32 p1) {
  u32 l2 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j1, j2;
  f64 d1;
  i0 = p1;
  i1 = p1;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  i2 = 15u;
  i1 += i2;
  i2 = 4294967280u;
  i1 &= i2;
  l2 = i1;
  i2 = 16u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = l2;
  j1 = i64_load(Z_envZ_memory, (u64)(i1));
  i2 = l2;
  j2 = i64_load(Z_envZ_memory, (u64)(i2 + 8));
  d1 = f49(j1, j2);
  f64_store(Z_envZ_memory, (u64)(i0), d1);
  FUNC_EPILOGUE;
}

static u64 f40(f64 p0) {
  FUNC_PROLOGUE;
  u64 j0;
  f64 d0;
  d0 = p0;
  j0 = i64_reinterpret_f64(d0);
  FUNC_EPILOGUE;
  return j0;
}

static u32 f41(u32 p0, u32 p1, u32 p2) {
  u32 l3 = 0, l4 = 0, l5 = 0, l6 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = p2;
  i1 = 0u;
  i0 = i0 != i1;
  l3 = i0;
  i0 = p2;
  i0 = !(i0);
  if (i0) {goto B3;}
  i0 = p0;
  i1 = 3u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B3;}
  i0 = p1;
  i1 = 255u;
  i0 &= i1;
  l4 = i0;
  L4: 
    i0 = p0;
    i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
    i1 = l4;
    i0 = i0 == i1;
    if (i0) {goto B2;}
    i0 = p0;
    i1 = 1u;
    i0 += i1;
    p0 = i0;
    i0 = p2;
    i1 = 4294967295u;
    i0 += i1;
    p2 = i0;
    i1 = 0u;
    i0 = i0 != i1;
    l3 = i0;
    i0 = p2;
    i0 = !(i0);
    if (i0) {goto B3;}
    i0 = p0;
    i1 = 3u;
    i0 &= i1;
    if (i0) {goto L4;}
  B3:;
  i0 = l3;
  i0 = !(i0);
  if (i0) {goto B1;}
  B2:;
  i0 = p0;
  i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
  i1 = p1;
  i2 = 255u;
  i1 &= i2;
  i0 = i0 == i1;
  if (i0) {goto B0;}
  i0 = p2;
  i1 = 4u;
  i0 = i0 < i1;
  if (i0) {goto B6;}
  i0 = p1;
  i1 = 255u;
  i0 &= i1;
  i1 = 16843009u;
  i0 *= i1;
  l4 = i0;
  i0 = p2;
  i1 = 4294967292u;
  i0 += i1;
  l3 = i0;
  i1 = l3;
  i2 = 4294967292u;
  i1 &= i2;
  l3 = i1;
  i0 -= i1;
  l5 = i0;
  i0 = l3;
  i1 = p0;
  i0 += i1;
  i1 = 4u;
  i0 += i1;
  l6 = i0;
  L7: 
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    i1 = l4;
    i0 ^= i1;
    l3 = i0;
    i1 = 4294967295u;
    i0 ^= i1;
    i1 = l3;
    i2 = 4278124287u;
    i1 += i2;
    i0 &= i1;
    i1 = 2155905152u;
    i0 &= i1;
    if (i0) {goto B5;}
    i0 = p0;
    i1 = 4u;
    i0 += i1;
    p0 = i0;
    i0 = p2;
    i1 = 4294967292u;
    i0 += i1;
    p2 = i0;
    i1 = 3u;
    i0 = i0 > i1;
    if (i0) {goto L7;}
  i0 = l5;
  p2 = i0;
  i0 = l6;
  p0 = i0;
  B6:;
  i0 = p2;
  i0 = !(i0);
  if (i0) {goto B1;}
  B5:;
  i0 = p1;
  i1 = 255u;
  i0 &= i1;
  l3 = i0;
  L8: 
    i0 = p0;
    i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
    i1 = l3;
    i0 = i0 == i1;
    if (i0) {goto B0;}
    i0 = p0;
    i1 = 1u;
    i0 += i1;
    p0 = i0;
    i0 = p2;
    i1 = 4294967295u;
    i0 += i1;
    p2 = i0;
    if (i0) {goto L8;}
  B1:;
  i0 = 0u;
  goto Bfunc;
  B0:;
  i0 = p0;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f42(u32 p0, u32 p1) {
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = p0;
  i1 = p0;
  i1 = f45(i1);
  i0 += i1;
  i1 = p1;
  i0 = f44(i0, i1);
  i0 = p0;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f43(u32 p0, u32 p1) {
  u32 l2 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = p1;
  i1 = p0;
  i0 ^= i1;
  i1 = 3u;
  i0 &= i1;
  if (i0) {goto B1;}
  i0 = p1;
  i1 = 3u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B2;}
  L3: 
    i0 = p0;
    i1 = p1;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1));
    l2 = i1;
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    i0 = l2;
    i0 = !(i0);
    if (i0) {goto B0;}
    i0 = p0;
    i1 = 1u;
    i0 += i1;
    p0 = i0;
    i0 = p1;
    i1 = 1u;
    i0 += i1;
    p1 = i0;
    i1 = 3u;
    i0 &= i1;
    if (i0) {goto L3;}
  B2:;
  i0 = p1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l2 = i0;
  i1 = 4294967295u;
  i0 ^= i1;
  i1 = l2;
  i2 = 4278124287u;
  i1 += i2;
  i0 &= i1;
  i1 = 2155905152u;
  i0 &= i1;
  if (i0) {goto B1;}
  L4: 
    i0 = p0;
    i1 = l2;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = p1;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
    l2 = i0;
    i0 = p0;
    i1 = 4u;
    i0 += i1;
    p0 = i0;
    i0 = p1;
    i1 = 4u;
    i0 += i1;
    p1 = i0;
    i0 = l2;
    i1 = 4294967295u;
    i0 ^= i1;
    i1 = l2;
    i2 = 4278124287u;
    i1 += i2;
    i0 &= i1;
    i1 = 2155905152u;
    i0 &= i1;
    i0 = !(i0);
    if (i0) {goto L4;}
  B1:;
  i0 = p0;
  i1 = p1;
  i1 = i32_load8_u(Z_envZ_memory, (u64)(i1));
  l2 = i1;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = l2;
  i0 = !(i0);
  if (i0) {goto B0;}
  L5: 
    i0 = p0;
    i1 = p1;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1 + 1));
    l2 = i1;
    i32_store8(Z_envZ_memory, (u64)(i0 + 1), i1);
    i0 = p0;
    i1 = 1u;
    i0 += i1;
    p0 = i0;
    i0 = p1;
    i1 = 1u;
    i0 += i1;
    p1 = i0;
    i0 = l2;
    if (i0) {goto L5;}
  B0:;
  i0 = p0;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f44(u32 p0, u32 p1) {
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = p0;
  i1 = p1;
  i0 = f43(i0, i1);
  i0 = p0;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f45(u32 p0) {
  u32 l1 = 0, l2 = 0, l3 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = p0;
  l1 = i0;
  i0 = p0;
  i1 = 3u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B1;}
  i0 = p0;
  i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
  if (i0) {goto B2;}
  i0 = p0;
  l1 = i0;
  goto B0;
  B2:;
  i0 = p0;
  l1 = i0;
  L3: 
    i0 = l1;
    i1 = 1u;
    i0 += i1;
    l1 = i0;
    i1 = 3u;
    i0 &= i1;
    i0 = !(i0);
    if (i0) {goto B1;}
    i0 = l1;
    i0 = i32_load8_u(Z_envZ_memory, (u64)(i0));
    i0 = !(i0);
    if (i0) {goto B0;}
    goto L3;
  UNREACHABLE;
  B1:;
  L4: 
    i0 = l1;
    l2 = i0;
    i1 = 4u;
    i0 += i1;
    l1 = i0;
    i0 = l2;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l3 = i0;
    i1 = 4294967295u;
    i0 ^= i1;
    i1 = l3;
    i2 = 4278124287u;
    i1 += i2;
    i0 &= i1;
    i1 = 2155905152u;
    i0 &= i1;
    i0 = !(i0);
    if (i0) {goto L4;}
  i0 = l3;
  i1 = 255u;
  i0 &= i1;
  if (i0) {goto B5;}
  i0 = l2;
  l1 = i0;
  goto B0;
  B5:;
  L6: 
    i0 = l2;
    i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 1));
    l3 = i0;
    i0 = l2;
    i1 = 1u;
    i0 += i1;
    l1 = i0;
    l2 = i0;
    i0 = l3;
    if (i0) {goto L6;}
  B0:;
  i0 = l1;
  i1 = p0;
  i0 -= i1;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f46(u32 p0) {
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = p0;
  if (i0) {goto B0;}
  i0 = 0u;
  goto Bfunc;
  B0:;
  i0 = __errno_location();
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = 4294967295u;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static void f47(u32 p0, u64 p1, u64 p2, u32 p3) {
  u64 l4 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j0, j1, j2;
  i0 = p3;
  i1 = 64u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B2;}
  j0 = p2;
  i1 = p3;
  i2 = 4294967232u;
  i1 += i2;
  j1 = (u64)(i1);
  j0 >>= (j1 & 63);
  p1 = j0;
  j0 = 0ull;
  l4 = j0;
  j0 = 0ull;
  p2 = j0;
  goto B1;
  B2:;
  i0 = p3;
  i0 = !(i0);
  if (i0) {goto B0;}
  j0 = p2;
  i1 = 64u;
  i2 = p3;
  i1 -= i2;
  j1 = (u64)(i1);
  j0 <<= (j1 & 63);
  j1 = p1;
  i2 = p3;
  j2 = (u64)(i2);
  l4 = j2;
  j1 >>= (j2 & 63);
  j0 |= j1;
  p1 = j0;
  j0 = p2;
  j1 = l4;
  j0 >>= (j1 & 63);
  p2 = j0;
  j0 = 0ull;
  l4 = j0;
  B1:;
  j0 = l4;
  j1 = p1;
  j0 |= j1;
  p1 = j0;
  B0:;
  i0 = p0;
  j1 = p1;
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  i0 = p0;
  j1 = p2;
  i64_store(Z_envZ_memory, (u64)(i0 + 8), j1);
  FUNC_EPILOGUE;
}

static void f48(u32 p0, u64 p1, u64 p2, u32 p3) {
  u64 l4 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j0, j1, j2;
  i0 = p3;
  i1 = 64u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B2;}
  j0 = p1;
  i1 = p3;
  i2 = 4294967232u;
  i1 += i2;
  j1 = (u64)(i1);
  j0 <<= (j1 & 63);
  p2 = j0;
  j0 = 0ull;
  p1 = j0;
  goto B1;
  B2:;
  i0 = p3;
  i0 = !(i0);
  if (i0) {goto B0;}
  j0 = p1;
  i1 = 64u;
  i2 = p3;
  i1 -= i2;
  j1 = (u64)(i1);
  j0 >>= (j1 & 63);
  j1 = p2;
  i2 = p3;
  j2 = (u64)(i2);
  l4 = j2;
  j1 <<= (j2 & 63);
  j0 |= j1;
  p2 = j0;
  j0 = p1;
  j1 = l4;
  j0 <<= (j1 & 63);
  p1 = j0;
  B1:;
  j0 = p2;
  j1 = 0ull;
  j0 |= j1;
  p2 = j0;
  B0:;
  i0 = p0;
  j1 = p1;
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  i0 = p0;
  j1 = p2;
  i64_store(Z_envZ_memory, (u64)(i0 + 8), j1);
  FUNC_EPILOGUE;
}

static f64 f49(u64 p0, u64 p1) {
  u32 l2 = 0, l3 = 0;
  u64 l4 = 0, l5 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4;
  u64 j0, j1, j2, j3;
  f64 d0;
  i0 = g0;
  i1 = 32u;
  i0 -= i1;
  l2 = i0;
  g0 = i0;
  j0 = p1;
  j1 = 9223372036854775807ull;
  j0 &= j1;
  l4 = j0;
  j1 = 14123006956457164800ull;
  j0 += j1;
  j1 = l4;
  j2 = 13547109154107162624ull;
  j1 += j2;
  i0 = j0 >= j1;
  if (i0) {goto B1;}
  j0 = p0;
  j1 = 60ull;
  j0 >>= (j1 & 63);
  j1 = p1;
  j2 = 4ull;
  j1 <<= (j2 & 63);
  j0 |= j1;
  l4 = j0;
  j0 = p0;
  j1 = 1152921504606846975ull;
  j0 &= j1;
  p0 = j0;
  j1 = 576460752303423489ull;
  i0 = j0 < j1;
  if (i0) {goto B2;}
  j0 = l4;
  j1 = 4611686018427387905ull;
  j0 += j1;
  l5 = j0;
  goto B0;
  B2:;
  j0 = l4;
  j1 = 4611686018427387904ull;
  j0 += j1;
  l5 = j0;
  j0 = p0;
  j1 = 576460752303423488ull;
  j0 ^= j1;
  j1 = 0ull;
  i0 = j0 != j1;
  if (i0) {goto B0;}
  j0 = l5;
  j1 = 1ull;
  j0 &= j1;
  j1 = l5;
  j0 += j1;
  l5 = j0;
  goto B0;
  B1:;
  j0 = p0;
  i0 = !(j0);
  j1 = l4;
  j2 = 9223090561878065152ull;
  i1 = j1 < j2;
  j2 = l4;
  j3 = 9223090561878065152ull;
  i2 = j2 == j3;
  i0 = i2 ? i0 : i1;
  if (i0) {goto B3;}
  j0 = p0;
  j1 = 60ull;
  j0 >>= (j1 & 63);
  j1 = p1;
  j2 = 4ull;
  j1 <<= (j2 & 63);
  j0 |= j1;
  j1 = 2251799813685247ull;
  j0 &= j1;
  j1 = 9221120237041090560ull;
  j0 |= j1;
  l5 = j0;
  goto B0;
  B3:;
  j0 = 9218868437227405312ull;
  l5 = j0;
  j0 = l4;
  j1 = 4899634919602388991ull;
  i0 = j0 > j1;
  if (i0) {goto B0;}
  j0 = 0ull;
  l5 = j0;
  j0 = l4;
  j1 = 48ull;
  j0 >>= (j1 & 63);
  i0 = (u32)(j0);
  l3 = i0;
  i1 = 15249u;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = l2;
  j1 = p0;
  j2 = p1;
  j3 = 281474976710655ull;
  j2 &= j3;
  j3 = 281474976710656ull;
  j2 |= j3;
  l4 = j2;
  i3 = 15361u;
  i4 = l3;
  i3 -= i4;
  f47(i0, j1, j2, i3);
  i0 = l2;
  i1 = 16u;
  i0 += i1;
  j1 = p0;
  j2 = l4;
  i3 = l3;
  i4 = 4294952063u;
  i3 += i4;
  f48(i0, j1, j2, i3);
  i0 = l2;
  j0 = i64_load(Z_envZ_memory, (u64)(i0));
  l4 = j0;
  j1 = 60ull;
  j0 >>= (j1 & 63);
  i1 = l2;
  i2 = 8u;
  i1 += i2;
  j1 = i64_load(Z_envZ_memory, (u64)(i1));
  j2 = 4ull;
  j1 <<= (j2 & 63);
  j0 |= j1;
  l5 = j0;
  j0 = l4;
  j1 = 1152921504606846975ull;
  j0 &= j1;
  i1 = l2;
  j1 = i64_load(Z_envZ_memory, (u64)(i1 + 16));
  i2 = l2;
  i3 = 16u;
  i2 += i3;
  i3 = 8u;
  i2 += i3;
  j2 = i64_load(Z_envZ_memory, (u64)(i2));
  j1 |= j2;
  j2 = 0ull;
  i1 = j1 != j2;
  j1 = (u64)(i1);
  j0 |= j1;
  l4 = j0;
  j1 = 576460752303423489ull;
  i0 = j0 < j1;
  if (i0) {goto B4;}
  j0 = l5;
  j1 = 1ull;
  j0 += j1;
  l5 = j0;
  goto B0;
  B4:;
  j0 = l4;
  j1 = 576460752303423488ull;
  j0 ^= j1;
  j1 = 0ull;
  i0 = j0 != j1;
  if (i0) {goto B0;}
  j0 = l5;
  j1 = 1ull;
  j0 &= j1;
  j1 = l5;
  j0 += j1;
  l5 = j0;
  B0:;
  i0 = l2;
  i1 = 32u;
  i0 += i1;
  g0 = i0;
  j0 = l5;
  j1 = p1;
  j2 = 9223372036854775808ull;
  j1 &= j2;
  j0 |= j1;
  d0 = f64_reinterpret_i64(j0);
  FUNC_EPILOGUE;
  return d0;
}

static u32 malloc(u32 p0) {
  u32 l1 = 0, l2 = 0, l3 = 0, l4 = 0, l5 = 0, l6 = 0, l7 = 0, l8 = 0, 
      l9 = 0, l10 = 0, l11 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4, i5;
  u64 j1;
  i0 = g0;
  i1 = 16u;
  i0 -= i1;
  l1 = i0;
  g0 = i0;
  i0 = p0;
  i1 = 244u;
  i0 = i0 > i1;
  if (i0) {goto B11;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3112));
  l2 = i0;
  i1 = 16u;
  i2 = p0;
  i3 = 11u;
  i2 += i3;
  i3 = 4294967288u;
  i2 &= i3;
  i3 = p0;
  i4 = 11u;
  i3 = i3 < i4;
  i1 = i3 ? i1 : i2;
  l3 = i1;
  i2 = 3u;
  i1 >>= (i2 & 31);
  l4 = i1;
  i0 >>= (i1 & 31);
  p0 = i0;
  i1 = 3u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B12;}
  i0 = p0;
  i1 = 4294967295u;
  i0 ^= i1;
  i1 = 1u;
  i0 &= i1;
  i1 = l4;
  i0 += i1;
  l3 = i0;
  i1 = 3u;
  i0 <<= (i1 & 31);
  l5 = i0;
  i1 = 3160u;
  i0 += i1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l4 = i0;
  i1 = 8u;
  i0 += i1;
  p0 = i0;
  i0 = l4;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l6 = i0;
  i1 = l5;
  i2 = 3152u;
  i1 += i2;
  l5 = i1;
  i0 = i0 != i1;
  if (i0) {goto B14;}
  i0 = 0u;
  i1 = l2;
  i2 = 4294967294u;
  i3 = l3;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  goto B13;
  B14:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3128));
  i1 = l6;
  i0 = i0 > i1;
  i0 = l6;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  B13:;
  i0 = l4;
  i1 = l3;
  i2 = 3u;
  i1 <<= (i2 & 31);
  l6 = i1;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l4;
  i1 = l6;
  i0 += i1;
  l4 = i0;
  i1 = l4;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  goto B0;
  B12:;
  i0 = l3;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3120));
  l7 = i1;
  i0 = i0 <= i1;
  if (i0) {goto B10;}
  i0 = p0;
  i0 = !(i0);
  if (i0) {goto B15;}
  i0 = p0;
  i1 = l4;
  i0 <<= (i1 & 31);
  i1 = 2u;
  i2 = l4;
  i1 <<= (i2 & 31);
  p0 = i1;
  i2 = 0u;
  i3 = p0;
  i2 -= i3;
  i1 |= i2;
  i0 &= i1;
  p0 = i0;
  i1 = 0u;
  i2 = p0;
  i1 -= i2;
  i0 &= i1;
  i1 = 4294967295u;
  i0 += i1;
  p0 = i0;
  i1 = p0;
  i2 = 12u;
  i1 >>= (i2 & 31);
  i2 = 16u;
  i1 &= i2;
  p0 = i1;
  i0 >>= (i1 & 31);
  l4 = i0;
  i1 = 5u;
  i0 >>= (i1 & 31);
  i1 = 8u;
  i0 &= i1;
  l6 = i0;
  i1 = p0;
  i0 |= i1;
  i1 = l4;
  i2 = l6;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 2u;
  i1 >>= (i2 & 31);
  i2 = 4u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 1u;
  i1 >>= (i2 & 31);
  i2 = 2u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 1u;
  i1 >>= (i2 & 31);
  i2 = 1u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  i0 += i1;
  l6 = i0;
  i1 = 3u;
  i0 <<= (i1 & 31);
  l5 = i0;
  i1 = 3160u;
  i0 += i1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l4 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  p0 = i0;
  i1 = l5;
  i2 = 3152u;
  i1 += i2;
  l5 = i1;
  i0 = i0 != i1;
  if (i0) {goto B17;}
  i0 = 0u;
  i1 = l2;
  i2 = 4294967294u;
  i3 = l6;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  l2 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  goto B16;
  B17:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3128));
  i1 = p0;
  i0 = i0 > i1;
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  B16:;
  i0 = l4;
  i1 = 8u;
  i0 += i1;
  p0 = i0;
  i0 = l4;
  i1 = l3;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l4;
  i1 = l3;
  i0 += i1;
  l5 = i0;
  i1 = l6;
  i2 = 3u;
  i1 <<= (i2 & 31);
  l8 = i1;
  i2 = l3;
  i1 -= i2;
  l6 = i1;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l4;
  i1 = l8;
  i0 += i1;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l7;
  i0 = !(i0);
  if (i0) {goto B18;}
  i0 = l7;
  i1 = 3u;
  i0 >>= (i1 & 31);
  l8 = i0;
  i1 = 3u;
  i0 <<= (i1 & 31);
  i1 = 3152u;
  i0 += i1;
  l3 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3132));
  l4 = i0;
  i0 = l2;
  i1 = 1u;
  i2 = l8;
  i1 <<= (i2 & 31);
  l8 = i1;
  i0 &= i1;
  if (i0) {goto B20;}
  i0 = 0u;
  i1 = l2;
  i2 = l8;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  i0 = l3;
  l8 = i0;
  goto B19;
  B20:;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l8 = i0;
  B19:;
  i0 = l3;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l8;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = l3;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = l8;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  B18:;
  i0 = 0u;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 3132), i1);
  i0 = 0u;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  goto B0;
  B15:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3116));
  l9 = i0;
  i0 = !(i0);
  if (i0) {goto B10;}
  i0 = l9;
  i1 = 0u;
  i2 = l9;
  i1 -= i2;
  i0 &= i1;
  i1 = 4294967295u;
  i0 += i1;
  p0 = i0;
  i1 = p0;
  i2 = 12u;
  i1 >>= (i2 & 31);
  i2 = 16u;
  i1 &= i2;
  p0 = i1;
  i0 >>= (i1 & 31);
  l4 = i0;
  i1 = 5u;
  i0 >>= (i1 & 31);
  i1 = 8u;
  i0 &= i1;
  l6 = i0;
  i1 = p0;
  i0 |= i1;
  i1 = l4;
  i2 = l6;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 2u;
  i1 >>= (i2 & 31);
  i2 = 4u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 1u;
  i1 >>= (i2 & 31);
  i2 = 2u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 1u;
  i1 >>= (i2 & 31);
  i2 = 1u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  i0 += i1;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l5 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
  i1 = 4294967288u;
  i0 &= i1;
  i1 = l3;
  i0 -= i1;
  l4 = i0;
  i0 = l5;
  l6 = i0;
  L22: 
    i0 = l6;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
    p0 = i0;
    if (i0) {goto B23;}
    i0 = l6;
    i1 = 20u;
    i0 += i1;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    p0 = i0;
    i0 = !(i0);
    if (i0) {goto B21;}
    B23:;
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
    i1 = 4294967288u;
    i0 &= i1;
    i1 = l3;
    i0 -= i1;
    l6 = i0;
    i1 = l4;
    i2 = l6;
    i3 = l4;
    i2 = i2 < i3;
    l6 = i2;
    i0 = i2 ? i0 : i1;
    l4 = i0;
    i0 = p0;
    i1 = l5;
    i2 = l6;
    i0 = i2 ? i0 : i1;
    l5 = i0;
    i0 = p0;
    l6 = i0;
    goto L22;
  UNREACHABLE;
  B21:;
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 24));
  l10 = i0;
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  l8 = i0;
  i1 = l5;
  i0 = i0 == i1;
  if (i0) {goto B24;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3128));
  i1 = l5;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 8));
  p0 = i1;
  i0 = i0 > i1;
  if (i0) {goto B25;}
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  i1 = l5;
  i0 = i0 != i1;
  B25:;
  i0 = p0;
  i1 = l8;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l8;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B1;
  B24:;
  i0 = l5;
  i1 = 20u;
  i0 += i1;
  l6 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  p0 = i0;
  if (i0) {goto B26;}
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B9;}
  i0 = l5;
  i1 = 16u;
  i0 += i1;
  l6 = i0;
  B26:;
  L27: 
    i0 = l6;
    l11 = i0;
    i0 = p0;
    l8 = i0;
    i1 = 20u;
    i0 += i1;
    l6 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    p0 = i0;
    if (i0) {goto L27;}
    i0 = l8;
    i1 = 16u;
    i0 += i1;
    l6 = i0;
    i0 = l8;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
    p0 = i0;
    if (i0) {goto L27;}
  i0 = l11;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  goto B1;
  B11:;
  i0 = 4294967295u;
  l3 = i0;
  i0 = p0;
  i1 = 4294967231u;
  i0 = i0 > i1;
  if (i0) {goto B10;}
  i0 = p0;
  i1 = 11u;
  i0 += i1;
  p0 = i0;
  i1 = 4294967288u;
  i0 &= i1;
  l3 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3116));
  l7 = i0;
  i0 = !(i0);
  if (i0) {goto B10;}
  i0 = 0u;
  l11 = i0;
  i0 = p0;
  i1 = 8u;
  i0 >>= (i1 & 31);
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B28;}
  i0 = 31u;
  l11 = i0;
  i0 = l3;
  i1 = 16777215u;
  i0 = i0 > i1;
  if (i0) {goto B28;}
  i0 = p0;
  i1 = p0;
  i2 = 1048320u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 8u;
  i1 &= i2;
  l4 = i1;
  i0 <<= (i1 & 31);
  p0 = i0;
  i1 = p0;
  i2 = 520192u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 4u;
  i1 &= i2;
  p0 = i1;
  i0 <<= (i1 & 31);
  l6 = i0;
  i1 = l6;
  i2 = 245760u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 2u;
  i1 &= i2;
  l6 = i1;
  i0 <<= (i1 & 31);
  i1 = 15u;
  i0 >>= (i1 & 31);
  i1 = p0;
  i2 = l4;
  i1 |= i2;
  i2 = l6;
  i1 |= i2;
  i0 -= i1;
  p0 = i0;
  i1 = 1u;
  i0 <<= (i1 & 31);
  i1 = l3;
  i2 = p0;
  i3 = 21u;
  i2 += i3;
  i1 >>= (i2 & 31);
  i2 = 1u;
  i1 &= i2;
  i0 |= i1;
  i1 = 28u;
  i0 += i1;
  l11 = i0;
  B28:;
  i0 = 0u;
  i1 = l3;
  i0 -= i1;
  l6 = i0;
  i0 = l11;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l4 = i0;
  if (i0) {goto B32;}
  i0 = 0u;
  p0 = i0;
  i0 = 0u;
  l8 = i0;
  goto B31;
  B32:;
  i0 = l3;
  i1 = 0u;
  i2 = 25u;
  i3 = l11;
  i4 = 1u;
  i3 >>= (i4 & 31);
  i2 -= i3;
  i3 = l11;
  i4 = 31u;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 <<= (i1 & 31);
  l5 = i0;
  i0 = 0u;
  p0 = i0;
  i0 = 0u;
  l8 = i0;
  L33: 
    i0 = l4;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
    i1 = 4294967288u;
    i0 &= i1;
    i1 = l3;
    i0 -= i1;
    l2 = i0;
    i1 = l6;
    i0 = i0 >= i1;
    if (i0) {goto B34;}
    i0 = l2;
    l6 = i0;
    i0 = l4;
    l8 = i0;
    i0 = l2;
    if (i0) {goto B34;}
    i0 = 0u;
    l6 = i0;
    i0 = l4;
    l8 = i0;
    i0 = l4;
    p0 = i0;
    goto B30;
    B34:;
    i0 = p0;
    i1 = l4;
    i2 = 20u;
    i1 += i2;
    i1 = i32_load(Z_envZ_memory, (u64)(i1));
    l2 = i1;
    i2 = l2;
    i3 = l4;
    i4 = l5;
    i5 = 29u;
    i4 >>= (i5 & 31);
    i5 = 4u;
    i4 &= i5;
    i3 += i4;
    i4 = 16u;
    i3 += i4;
    i3 = i32_load(Z_envZ_memory, (u64)(i3));
    l4 = i3;
    i2 = i2 == i3;
    i0 = i2 ? i0 : i1;
    i1 = p0;
    i2 = l2;
    i0 = i2 ? i0 : i1;
    p0 = i0;
    i0 = l5;
    i1 = l4;
    i2 = 0u;
    i1 = i1 != i2;
    i0 <<= (i1 & 31);
    l5 = i0;
    i0 = l4;
    if (i0) {goto L33;}
  B31:;
  i0 = p0;
  i1 = l8;
  i0 |= i1;
  if (i0) {goto B35;}
  i0 = 2u;
  i1 = l11;
  i0 <<= (i1 & 31);
  p0 = i0;
  i1 = 0u;
  i2 = p0;
  i1 -= i2;
  i0 |= i1;
  i1 = l7;
  i0 &= i1;
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B10;}
  i0 = p0;
  i1 = 0u;
  i2 = p0;
  i1 -= i2;
  i0 &= i1;
  i1 = 4294967295u;
  i0 += i1;
  p0 = i0;
  i1 = p0;
  i2 = 12u;
  i1 >>= (i2 & 31);
  i2 = 16u;
  i1 &= i2;
  p0 = i1;
  i0 >>= (i1 & 31);
  l4 = i0;
  i1 = 5u;
  i0 >>= (i1 & 31);
  i1 = 8u;
  i0 &= i1;
  l5 = i0;
  i1 = p0;
  i0 |= i1;
  i1 = l4;
  i2 = l5;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 2u;
  i1 >>= (i2 & 31);
  i2 = 4u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 1u;
  i1 >>= (i2 & 31);
  i2 = 2u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  p0 = i1;
  i2 = 1u;
  i1 >>= (i2 & 31);
  i2 = 1u;
  i1 &= i2;
  l4 = i1;
  i0 |= i1;
  i1 = p0;
  i2 = l4;
  i1 >>= (i2 & 31);
  i0 += i1;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  p0 = i0;
  B35:;
  i0 = p0;
  i0 = !(i0);
  if (i0) {goto B29;}
  B30:;
  L36: 
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
    i1 = 4294967288u;
    i0 &= i1;
    i1 = l3;
    i0 -= i1;
    l2 = i0;
    i1 = l6;
    i0 = i0 < i1;
    l5 = i0;
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
    l4 = i0;
    if (i0) {goto B37;}
    i0 = p0;
    i1 = 20u;
    i0 += i1;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l4 = i0;
    B37:;
    i0 = l2;
    i1 = l6;
    i2 = l5;
    i0 = i2 ? i0 : i1;
    l6 = i0;
    i0 = p0;
    i1 = l8;
    i2 = l5;
    i0 = i2 ? i0 : i1;
    l8 = i0;
    i0 = l4;
    p0 = i0;
    i0 = l4;
    if (i0) {goto L36;}
  B29:;
  i0 = l8;
  i0 = !(i0);
  if (i0) {goto B10;}
  i0 = l6;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3120));
  i2 = l3;
  i1 -= i2;
  i0 = i0 >= i1;
  if (i0) {goto B10;}
  i0 = l8;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 24));
  l11 = i0;
  i0 = l8;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  l5 = i0;
  i1 = l8;
  i0 = i0 == i1;
  if (i0) {goto B38;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3128));
  i1 = l8;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 8));
  p0 = i1;
  i0 = i0 > i1;
  if (i0) {goto B39;}
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  i1 = l8;
  i0 = i0 != i1;
  B39:;
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B2;
  B38:;
  i0 = l8;
  i1 = 20u;
  i0 += i1;
  l4 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  p0 = i0;
  if (i0) {goto B40;}
  i0 = l8;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B8;}
  i0 = l8;
  i1 = 16u;
  i0 += i1;
  l4 = i0;
  B40:;
  L41: 
    i0 = l4;
    l2 = i0;
    i0 = p0;
    l5 = i0;
    i1 = 20u;
    i0 += i1;
    l4 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    p0 = i0;
    if (i0) {goto L41;}
    i0 = l5;
    i1 = 16u;
    i0 += i1;
    l4 = i0;
    i0 = l5;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
    p0 = i0;
    if (i0) {goto L41;}
  i0 = l2;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  goto B2;
  B10:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3120));
  p0 = i0;
  i1 = l3;
  i0 = i0 < i1;
  if (i0) {goto B42;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3132));
  l4 = i0;
  i0 = p0;
  i1 = l3;
  i0 -= i1;
  l6 = i0;
  i1 = 16u;
  i0 = i0 < i1;
  if (i0) {goto B44;}
  i0 = 0u;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  i0 = 0u;
  i1 = l4;
  i2 = l3;
  i1 += i2;
  l5 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3132), i1);
  i0 = l5;
  i1 = l6;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l4;
  i1 = p0;
  i0 += i1;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l4;
  i1 = l3;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  goto B43;
  B44:;
  i0 = 0u;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3132), i1);
  i0 = 0u;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  i0 = l4;
  i1 = p0;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l4;
  i1 = p0;
  i0 += i1;
  p0 = i0;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  B43:;
  i0 = l4;
  i1 = 8u;
  i0 += i1;
  p0 = i0;
  goto B0;
  B42:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3124));
  l5 = i0;
  i1 = l3;
  i0 = i0 <= i1;
  if (i0) {goto B45;}
  i0 = 0u;
  i1 = l5;
  i2 = l3;
  i1 -= i2;
  l4 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3124), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3136));
  p0 = i1;
  i2 = l3;
  i1 += i2;
  l6 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3136), i1);
  i0 = l6;
  i1 = l4;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = p0;
  i1 = l3;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = p0;
  i1 = 8u;
  i0 += i1;
  p0 = i0;
  goto B0;
  B45:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3584));
  i0 = !(i0);
  if (i0) {goto B47;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3592));
  l4 = i0;
  goto B46;
  B47:;
  i0 = 0u;
  j1 = 18446744073709551615ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 3596), j1);
  i0 = 0u;
  j1 = 17592186048512ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 3588), j1);
  i0 = 0u;
  i1 = l1;
  i2 = 12u;
  i1 += i2;
  i2 = 4294967280u;
  i1 &= i2;
  i2 = 1431655768u;
  i1 ^= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3584), i1);
  i0 = 0u;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3604), i1);
  i0 = 0u;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3556), i1);
  i0 = 4096u;
  l4 = i0;
  B46:;
  i0 = 0u;
  p0 = i0;
  i0 = l4;
  i1 = l3;
  i2 = 47u;
  i1 += i2;
  l7 = i1;
  i0 += i1;
  l2 = i0;
  i1 = 0u;
  i2 = l4;
  i1 -= i2;
  l11 = i1;
  i0 &= i1;
  l8 = i0;
  i1 = l3;
  i0 = i0 <= i1;
  if (i0) {goto B0;}
  i0 = 0u;
  p0 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3552));
  l4 = i0;
  i0 = !(i0);
  if (i0) {goto B48;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3544));
  l6 = i0;
  i1 = l8;
  i0 += i1;
  l9 = i0;
  i1 = l6;
  i0 = i0 <= i1;
  if (i0) {goto B0;}
  i0 = l9;
  i1 = l4;
  i0 = i0 > i1;
  if (i0) {goto B0;}
  B48:;
  i0 = 0u;
  i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 3556));
  i1 = 4u;
  i0 &= i1;
  if (i0) {goto B5;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3136));
  l4 = i0;
  i0 = !(i0);
  if (i0) {goto B51;}
  i0 = 3560u;
  p0 = i0;
  L52: 
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l6 = i0;
    i1 = l4;
    i0 = i0 > i1;
    if (i0) {goto B53;}
    i0 = l6;
    i1 = p0;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
    i0 += i1;
    i1 = l4;
    i0 = i0 > i1;
    if (i0) {goto B50;}
    B53:;
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
    p0 = i0;
    if (i0) {goto L52;}
  B51:;
  i0 = 0u;
  i0 = f52(i0);
  l5 = i0;
  i1 = 4294967295u;
  i0 = i0 == i1;
  if (i0) {goto B6;}
  i0 = l8;
  l2 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3588));
  p0 = i0;
  i1 = 4294967295u;
  i0 += i1;
  l4 = i0;
  i1 = l5;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B54;}
  i0 = l8;
  i1 = l5;
  i0 -= i1;
  i1 = l4;
  i2 = l5;
  i1 += i2;
  i2 = 0u;
  i3 = p0;
  i2 -= i3;
  i1 &= i2;
  i0 += i1;
  l2 = i0;
  B54:;
  i0 = l2;
  i1 = l3;
  i0 = i0 <= i1;
  if (i0) {goto B6;}
  i0 = l2;
  i1 = 2147483646u;
  i0 = i0 > i1;
  if (i0) {goto B6;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3552));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B55;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3544));
  l4 = i0;
  i1 = l2;
  i0 += i1;
  l6 = i0;
  i1 = l4;
  i0 = i0 <= i1;
  if (i0) {goto B6;}
  i0 = l6;
  i1 = p0;
  i0 = i0 > i1;
  if (i0) {goto B6;}
  B55:;
  i0 = l2;
  i0 = f52(i0);
  p0 = i0;
  i1 = l5;
  i0 = i0 != i1;
  if (i0) {goto B49;}
  goto B4;
  B50:;
  i0 = l2;
  i1 = l5;
  i0 -= i1;
  i1 = l11;
  i0 &= i1;
  l2 = i0;
  i1 = 2147483646u;
  i0 = i0 > i1;
  if (i0) {goto B6;}
  i0 = l2;
  i0 = f52(i0);
  l5 = i0;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  i2 = p0;
  i2 = i32_load(Z_envZ_memory, (u64)(i2 + 4));
  i1 += i2;
  i0 = i0 == i1;
  if (i0) {goto B7;}
  i0 = l5;
  p0 = i0;
  B49:;
  i0 = p0;
  l5 = i0;
  i0 = l3;
  i1 = 48u;
  i0 += i1;
  i1 = l2;
  i0 = i0 <= i1;
  if (i0) {goto B56;}
  i0 = l2;
  i1 = 2147483646u;
  i0 = i0 > i1;
  if (i0) {goto B56;}
  i0 = l5;
  i1 = 4294967295u;
  i0 = i0 == i1;
  if (i0) {goto B56;}
  i0 = l7;
  i1 = l2;
  i0 -= i1;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3592));
  p0 = i1;
  i0 += i1;
  i1 = 0u;
  i2 = p0;
  i1 -= i2;
  i0 &= i1;
  p0 = i0;
  i1 = 2147483646u;
  i0 = i0 > i1;
  if (i0) {goto B4;}
  i0 = p0;
  i0 = f52(i0);
  i1 = 4294967295u;
  i0 = i0 == i1;
  if (i0) {goto B57;}
  i0 = p0;
  i1 = l2;
  i0 += i1;
  l2 = i0;
  goto B4;
  B57:;
  i0 = 0u;
  i1 = l2;
  i0 -= i1;
  i0 = f52(i0);
  goto B6;
  B56:;
  i0 = l5;
  i1 = 4294967295u;
  i0 = i0 != i1;
  if (i0) {goto B4;}
  goto B6;
  B9:;
  i0 = 0u;
  l8 = i0;
  goto B1;
  B8:;
  i0 = 0u;
  l5 = i0;
  goto B2;
  B7:;
  i0 = l5;
  i1 = 4294967295u;
  i0 = i0 != i1;
  if (i0) {goto B4;}
  B6:;
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3556));
  i2 = 4u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3556), i1);
  B5:;
  i0 = l8;
  i1 = 2147483646u;
  i0 = i0 > i1;
  if (i0) {goto B3;}
  i0 = l8;
  i0 = f52(i0);
  l5 = i0;
  i1 = 0u;
  i1 = f52(i1);
  p0 = i1;
  i0 = i0 >= i1;
  if (i0) {goto B3;}
  i0 = l5;
  i1 = 4294967295u;
  i0 = i0 == i1;
  if (i0) {goto B3;}
  i0 = p0;
  i1 = 4294967295u;
  i0 = i0 == i1;
  if (i0) {goto B3;}
  i0 = p0;
  i1 = l5;
  i0 -= i1;
  l2 = i0;
  i1 = l3;
  i2 = 40u;
  i1 += i2;
  i0 = i0 <= i1;
  if (i0) {goto B3;}
  B4:;
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3544));
  i2 = l2;
  i1 += i2;
  p0 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3544), i1);
  i0 = p0;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3548));
  i0 = i0 <= i1;
  if (i0) {goto B58;}
  i0 = 0u;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 3548), i1);
  B58:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3136));
  l4 = i0;
  i0 = !(i0);
  if (i0) {goto B62;}
  i0 = 3560u;
  p0 = i0;
  L63: 
    i0 = l5;
    i1 = p0;
    i1 = i32_load(Z_envZ_memory, (u64)(i1));
    l6 = i1;
    i2 = p0;
    i2 = i32_load(Z_envZ_memory, (u64)(i2 + 4));
    l8 = i2;
    i1 += i2;
    i0 = i0 == i1;
    if (i0) {goto B61;}
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
    p0 = i0;
    if (i0) {goto L63;}
    goto B60;
  UNREACHABLE;
  B62:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3128));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B65;}
  i0 = l5;
  i1 = p0;
  i0 = i0 >= i1;
  if (i0) {goto B64;}
  B65:;
  i0 = 0u;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 3128), i1);
  B64:;
  i0 = 0u;
  p0 = i0;
  i0 = 0u;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3564), i1);
  i0 = 0u;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 3560), i1);
  i0 = 0u;
  i1 = 4294967295u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3144), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3584));
  i32_store(Z_envZ_memory, (u64)(i0 + 3148), i1);
  i0 = 0u;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3572), i1);
  L66: 
    i0 = p0;
    i1 = 3u;
    i0 <<= (i1 & 31);
    l4 = i0;
    i1 = 3160u;
    i0 += i1;
    i1 = l4;
    i2 = 3152u;
    i1 += i2;
    l6 = i1;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = l4;
    i1 = 3164u;
    i0 += i1;
    i1 = l6;
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = p0;
    i1 = 1u;
    i0 += i1;
    p0 = i0;
    i1 = 32u;
    i0 = i0 != i1;
    if (i0) {goto L66;}
  i0 = 0u;
  i1 = l2;
  i2 = 4294967256u;
  i1 += i2;
  p0 = i1;
  i2 = 4294967288u;
  i3 = l5;
  i2 -= i3;
  i3 = 7u;
  i2 &= i3;
  i3 = 0u;
  i4 = l5;
  i5 = 8u;
  i4 += i5;
  i5 = 7u;
  i4 &= i5;
  i2 = i4 ? i2 : i3;
  l4 = i2;
  i1 -= i2;
  l6 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3124), i1);
  i0 = 0u;
  i1 = l5;
  i2 = l4;
  i1 += i2;
  l4 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3136), i1);
  i0 = l4;
  i1 = l6;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l5;
  i1 = p0;
  i0 += i1;
  i1 = 40u;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3600));
  i32_store(Z_envZ_memory, (u64)(i0 + 3140), i1);
  goto B59;
  B61:;
  i0 = p0;
  i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 12));
  i1 = 8u;
  i0 &= i1;
  if (i0) {goto B60;}
  i0 = l5;
  i1 = l4;
  i0 = i0 <= i1;
  if (i0) {goto B60;}
  i0 = l6;
  i1 = l4;
  i0 = i0 > i1;
  if (i0) {goto B60;}
  i0 = p0;
  i1 = l8;
  i2 = l2;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = 0u;
  i1 = l4;
  i2 = 4294967288u;
  i3 = l4;
  i2 -= i3;
  i3 = 7u;
  i2 &= i3;
  i3 = 0u;
  i4 = l4;
  i5 = 8u;
  i4 += i5;
  i5 = 7u;
  i4 &= i5;
  i2 = i4 ? i2 : i3;
  p0 = i2;
  i1 += i2;
  l6 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3136), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3124));
  i2 = l2;
  i1 += i2;
  l5 = i1;
  i2 = p0;
  i1 -= i2;
  p0 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3124), i1);
  i0 = l6;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l4;
  i1 = l5;
  i0 += i1;
  i1 = 40u;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3600));
  i32_store(Z_envZ_memory, (u64)(i0 + 3140), i1);
  goto B59;
  B60:;
  i0 = l5;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3128));
  l8 = i1;
  i0 = i0 >= i1;
  if (i0) {goto B67;}
  i0 = 0u;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 3128), i1);
  i0 = l5;
  l8 = i0;
  B67:;
  i0 = l5;
  i1 = l2;
  i0 += i1;
  l6 = i0;
  i0 = 3560u;
  p0 = i0;
  L75: 
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    i1 = l6;
    i0 = i0 == i1;
    if (i0) {goto B74;}
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
    p0 = i0;
    if (i0) {goto L75;}
    goto B73;
  UNREACHABLE;
  B74:;
  i0 = p0;
  i0 = i32_load8_u(Z_envZ_memory, (u64)(i0 + 12));
  i1 = 8u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B72;}
  B73:;
  i0 = 3560u;
  p0 = i0;
  L76: 
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l6 = i0;
    i1 = l4;
    i0 = i0 > i1;
    if (i0) {goto B77;}
    i0 = l6;
    i1 = p0;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
    i0 += i1;
    l6 = i0;
    i1 = l4;
    i0 = i0 > i1;
    if (i0) {goto B71;}
    B77:;
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
    p0 = i0;
    goto L76;
  UNREACHABLE;
  B72:;
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
  i2 = l2;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l5;
  i1 = 4294967288u;
  i2 = l5;
  i1 -= i2;
  i2 = 7u;
  i1 &= i2;
  i2 = 0u;
  i3 = l5;
  i4 = 8u;
  i3 += i4;
  i4 = 7u;
  i3 &= i4;
  i1 = i3 ? i1 : i2;
  i0 += i1;
  l11 = i0;
  i1 = l3;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l6;
  i1 = 4294967288u;
  i2 = l6;
  i1 -= i2;
  i2 = 7u;
  i1 &= i2;
  i2 = 0u;
  i3 = l6;
  i4 = 8u;
  i3 += i4;
  i4 = 7u;
  i3 &= i4;
  i1 = i3 ? i1 : i2;
  i0 += i1;
  l5 = i0;
  i1 = l11;
  i0 -= i1;
  i1 = l3;
  i0 -= i1;
  p0 = i0;
  i0 = l11;
  i1 = l3;
  i0 += i1;
  l6 = i0;
  i0 = l4;
  i1 = l5;
  i0 = i0 != i1;
  if (i0) {goto B78;}
  i0 = 0u;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 3136), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3124));
  i2 = p0;
  i1 += i2;
  p0 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3124), i1);
  i0 = l6;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  goto B69;
  B78:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3132));
  i1 = l5;
  i0 = i0 != i1;
  if (i0) {goto B79;}
  i0 = 0u;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 3132), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3120));
  i2 = p0;
  i1 += i2;
  p0 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  i0 = l6;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l6;
  i1 = p0;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  goto B69;
  B79:;
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
  l4 = i0;
  i1 = 3u;
  i0 &= i1;
  i1 = 1u;
  i0 = i0 != i1;
  if (i0) {goto B80;}
  i0 = l4;
  i1 = 4294967288u;
  i0 &= i1;
  l7 = i0;
  i0 = l4;
  i1 = 255u;
  i0 = i0 > i1;
  if (i0) {goto B82;}
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  l3 = i0;
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l2 = i0;
  i1 = l4;
  i2 = 3u;
  i1 >>= (i2 & 31);
  l9 = i1;
  i2 = 3u;
  i1 <<= (i2 & 31);
  i2 = 3152u;
  i1 += i2;
  l4 = i1;
  i0 = i0 == i1;
  if (i0) {goto B83;}
  i0 = l8;
  i1 = l2;
  i0 = i0 > i1;
  B83:;
  i0 = l3;
  i1 = l2;
  i0 = i0 != i1;
  if (i0) {goto B84;}
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3112));
  i2 = 4294967294u;
  i3 = l9;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  goto B81;
  B84:;
  i0 = l3;
  i1 = l4;
  i0 = i0 == i1;
  if (i0) {goto B85;}
  i0 = l8;
  i1 = l3;
  i0 = i0 > i1;
  B85:;
  i0 = l2;
  i1 = l3;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l3;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B81;
  B82:;
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 24));
  l9 = i0;
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  l2 = i0;
  i1 = l5;
  i0 = i0 == i1;
  if (i0) {goto B87;}
  i0 = l8;
  i1 = l5;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 8));
  l4 = i1;
  i0 = i0 > i1;
  if (i0) {goto B88;}
  i0 = l4;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  i1 = l5;
  i0 = i0 != i1;
  B88:;
  i0 = l4;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l2;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B86;
  B87:;
  i0 = l5;
  i1 = 20u;
  i0 += i1;
  l4 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l3 = i0;
  if (i0) {goto B89;}
  i0 = l5;
  i1 = 16u;
  i0 += i1;
  l4 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l3 = i0;
  if (i0) {goto B89;}
  i0 = 0u;
  l2 = i0;
  goto B86;
  B89:;
  L90: 
    i0 = l4;
    l8 = i0;
    i0 = l3;
    l2 = i0;
    i1 = 20u;
    i0 += i1;
    l4 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l3 = i0;
    if (i0) {goto L90;}
    i0 = l2;
    i1 = 16u;
    i0 += i1;
    l4 = i0;
    i0 = l2;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
    l3 = i0;
    if (i0) {goto L90;}
  i0 = l8;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  B86:;
  i0 = l9;
  i0 = !(i0);
  if (i0) {goto B81;}
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 28));
  l3 = i0;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  l4 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  i1 = l5;
  i0 = i0 != i1;
  if (i0) {goto B92;}
  i0 = l4;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l2;
  if (i0) {goto B91;}
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3116));
  i2 = 4294967294u;
  i3 = l3;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  goto B81;
  B92:;
  i0 = l9;
  i1 = 16u;
  i2 = 20u;
  i3 = l9;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 16));
  i4 = l5;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 += i1;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l2;
  i0 = !(i0);
  if (i0) {goto B81;}
  B91:;
  i0 = l2;
  i1 = l9;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  l4 = i0;
  i0 = !(i0);
  if (i0) {goto B93;}
  i0 = l2;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = l4;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B93:;
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
  l4 = i0;
  i0 = !(i0);
  if (i0) {goto B81;}
  i0 = l2;
  i1 = 20u;
  i0 += i1;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l4;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B81:;
  i0 = l7;
  i1 = p0;
  i0 += i1;
  p0 = i0;
  i0 = l5;
  i1 = l7;
  i0 += i1;
  l5 = i0;
  B80:;
  i0 = l5;
  i1 = l5;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
  i2 = 4294967294u;
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l6;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l6;
  i1 = p0;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = 255u;
  i0 = i0 > i1;
  if (i0) {goto B94;}
  i0 = p0;
  i1 = 3u;
  i0 >>= (i1 & 31);
  l4 = i0;
  i1 = 3u;
  i0 <<= (i1 & 31);
  i1 = 3152u;
  i0 += i1;
  p0 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3112));
  l3 = i0;
  i1 = 1u;
  i2 = l4;
  i1 <<= (i2 & 31);
  l4 = i1;
  i0 &= i1;
  if (i0) {goto B96;}
  i0 = 0u;
  i1 = l3;
  i2 = l4;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  i0 = p0;
  l4 = i0;
  goto B95;
  B96:;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l4 = i0;
  B95:;
  i0 = p0;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l4;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l6;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l6;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B69;
  B94:;
  i0 = 0u;
  l4 = i0;
  i0 = p0;
  i1 = 8u;
  i0 >>= (i1 & 31);
  l3 = i0;
  i0 = !(i0);
  if (i0) {goto B97;}
  i0 = 31u;
  l4 = i0;
  i0 = p0;
  i1 = 16777215u;
  i0 = i0 > i1;
  if (i0) {goto B97;}
  i0 = l3;
  i1 = l3;
  i2 = 1048320u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 8u;
  i1 &= i2;
  l4 = i1;
  i0 <<= (i1 & 31);
  l3 = i0;
  i1 = l3;
  i2 = 520192u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 4u;
  i1 &= i2;
  l3 = i1;
  i0 <<= (i1 & 31);
  l5 = i0;
  i1 = l5;
  i2 = 245760u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 2u;
  i1 &= i2;
  l5 = i1;
  i0 <<= (i1 & 31);
  i1 = 15u;
  i0 >>= (i1 & 31);
  i1 = l3;
  i2 = l4;
  i1 |= i2;
  i2 = l5;
  i1 |= i2;
  i0 -= i1;
  l4 = i0;
  i1 = 1u;
  i0 <<= (i1 & 31);
  i1 = p0;
  i2 = l4;
  i3 = 21u;
  i2 += i3;
  i1 >>= (i2 & 31);
  i2 = 1u;
  i1 &= i2;
  i0 |= i1;
  i1 = 28u;
  i0 += i1;
  l4 = i0;
  B97:;
  i0 = l6;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
  i0 = l6;
  j1 = 0ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 16), j1);
  i0 = l4;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  l3 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3116));
  l5 = i0;
  i1 = 1u;
  i2 = l4;
  i1 <<= (i2 & 31);
  l8 = i1;
  i0 &= i1;
  if (i0) {goto B99;}
  i0 = 0u;
  i1 = l5;
  i2 = l8;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  i0 = l3;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l6;
  i1 = l3;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  goto B98;
  B99:;
  i0 = p0;
  i1 = 0u;
  i2 = 25u;
  i3 = l4;
  i4 = 1u;
  i3 >>= (i4 & 31);
  i2 -= i3;
  i3 = l4;
  i4 = 31u;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 <<= (i1 & 31);
  l4 = i0;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l5 = i0;
  L100: 
    i0 = l5;
    l3 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
    i1 = 4294967288u;
    i0 &= i1;
    i1 = p0;
    i0 = i0 == i1;
    if (i0) {goto B70;}
    i0 = l4;
    i1 = 29u;
    i0 >>= (i1 & 31);
    l5 = i0;
    i0 = l4;
    i1 = 1u;
    i0 <<= (i1 & 31);
    l4 = i0;
    i0 = l3;
    i1 = l5;
    i2 = 4u;
    i1 &= i2;
    i0 += i1;
    i1 = 16u;
    i0 += i1;
    l8 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l5 = i0;
    if (i0) {goto L100;}
  i0 = l8;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l6;
  i1 = l3;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B98:;
  i0 = l6;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l6;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B69;
  B71:;
  i0 = 0u;
  i1 = l2;
  i2 = 4294967256u;
  i1 += i2;
  p0 = i1;
  i2 = 4294967288u;
  i3 = l5;
  i2 -= i3;
  i3 = 7u;
  i2 &= i3;
  i3 = 0u;
  i4 = l5;
  i5 = 8u;
  i4 += i5;
  i5 = 7u;
  i4 &= i5;
  i2 = i4 ? i2 : i3;
  l8 = i2;
  i1 -= i2;
  l11 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3124), i1);
  i0 = 0u;
  i1 = l5;
  i2 = l8;
  i1 += i2;
  l8 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3136), i1);
  i0 = l8;
  i1 = l11;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l5;
  i1 = p0;
  i0 += i1;
  i1 = 40u;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3600));
  i32_store(Z_envZ_memory, (u64)(i0 + 3140), i1);
  i0 = l4;
  i1 = l6;
  i2 = 39u;
  i3 = l6;
  i2 -= i3;
  i3 = 7u;
  i2 &= i3;
  i3 = 0u;
  i4 = l6;
  i5 = 4294967257u;
  i4 += i5;
  i5 = 7u;
  i4 &= i5;
  i2 = i4 ? i2 : i3;
  i1 += i2;
  i2 = 4294967249u;
  i1 += i2;
  p0 = i1;
  i2 = p0;
  i3 = l4;
  i4 = 16u;
  i3 += i4;
  i2 = i2 < i3;
  i0 = i2 ? i0 : i1;
  l8 = i0;
  i1 = 27u;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l8;
  i1 = 16u;
  i0 += i1;
  i1 = 0u;
  j1 = i64_load(Z_envZ_memory, (u64)(i1 + 3568));
  i64_store(Z_envZ_memory, (u64)(i0), j1);
  i0 = l8;
  i1 = 0u;
  j1 = i64_load(Z_envZ_memory, (u64)(i1 + 3560));
  i64_store(Z_envZ_memory, (u64)(i0 + 8), j1);
  i0 = 0u;
  i1 = l8;
  i2 = 8u;
  i1 += i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3568), i1);
  i0 = 0u;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3564), i1);
  i0 = 0u;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 3560), i1);
  i0 = 0u;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3572), i1);
  i0 = l8;
  i1 = 24u;
  i0 += i1;
  p0 = i0;
  L101: 
    i0 = p0;
    i1 = 7u;
    i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
    i0 = p0;
    i1 = 8u;
    i0 += i1;
    l5 = i0;
    i0 = p0;
    i1 = 4u;
    i0 += i1;
    p0 = i0;
    i0 = l5;
    i1 = l6;
    i0 = i0 < i1;
    if (i0) {goto L101;}
  i0 = l8;
  i1 = l4;
  i0 = i0 == i1;
  if (i0) {goto B59;}
  i0 = l8;
  i1 = l8;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
  i2 = 4294967294u;
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l4;
  i1 = l8;
  i2 = l4;
  i1 -= i2;
  l2 = i1;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l8;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l2;
  i1 = 255u;
  i0 = i0 > i1;
  if (i0) {goto B102;}
  i0 = l2;
  i1 = 3u;
  i0 >>= (i1 & 31);
  l6 = i0;
  i1 = 3u;
  i0 <<= (i1 & 31);
  i1 = 3152u;
  i0 += i1;
  p0 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3112));
  l5 = i0;
  i1 = 1u;
  i2 = l6;
  i1 <<= (i2 & 31);
  l6 = i1;
  i0 &= i1;
  if (i0) {goto B104;}
  i0 = 0u;
  i1 = l5;
  i2 = l6;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  i0 = p0;
  l6 = i0;
  goto B103;
  B104:;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l6 = i0;
  B103:;
  i0 = p0;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l6;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B59;
  B102:;
  i0 = 0u;
  p0 = i0;
  i0 = l2;
  i1 = 8u;
  i0 >>= (i1 & 31);
  l6 = i0;
  i0 = !(i0);
  if (i0) {goto B105;}
  i0 = 31u;
  p0 = i0;
  i0 = l2;
  i1 = 16777215u;
  i0 = i0 > i1;
  if (i0) {goto B105;}
  i0 = l6;
  i1 = l6;
  i2 = 1048320u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 8u;
  i1 &= i2;
  p0 = i1;
  i0 <<= (i1 & 31);
  l6 = i0;
  i1 = l6;
  i2 = 520192u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 4u;
  i1 &= i2;
  l6 = i1;
  i0 <<= (i1 & 31);
  l5 = i0;
  i1 = l5;
  i2 = 245760u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 2u;
  i1 &= i2;
  l5 = i1;
  i0 <<= (i1 & 31);
  i1 = 15u;
  i0 >>= (i1 & 31);
  i1 = l6;
  i2 = p0;
  i1 |= i2;
  i2 = l5;
  i1 |= i2;
  i0 -= i1;
  p0 = i0;
  i1 = 1u;
  i0 <<= (i1 & 31);
  i1 = l2;
  i2 = p0;
  i3 = 21u;
  i2 += i3;
  i1 >>= (i2 & 31);
  i2 = 1u;
  i1 &= i2;
  i0 |= i1;
  i1 = 28u;
  i0 += i1;
  p0 = i0;
  B105:;
  i0 = l4;
  j1 = 0ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 16), j1);
  i0 = l4;
  i1 = 28u;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  l6 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3116));
  l5 = i0;
  i1 = 1u;
  i2 = p0;
  i1 <<= (i2 & 31);
  l8 = i1;
  i0 &= i1;
  if (i0) {goto B107;}
  i0 = 0u;
  i1 = l5;
  i2 = l8;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  i0 = l6;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l4;
  i1 = 24u;
  i0 += i1;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  goto B106;
  B107:;
  i0 = l2;
  i1 = 0u;
  i2 = 25u;
  i3 = p0;
  i4 = 1u;
  i3 >>= (i4 & 31);
  i2 -= i3;
  i3 = p0;
  i4 = 31u;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 <<= (i1 & 31);
  p0 = i0;
  i0 = l6;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l5 = i0;
  L108: 
    i0 = l5;
    l6 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
    i1 = 4294967288u;
    i0 &= i1;
    i1 = l2;
    i0 = i0 == i1;
    if (i0) {goto B68;}
    i0 = p0;
    i1 = 29u;
    i0 >>= (i1 & 31);
    l5 = i0;
    i0 = p0;
    i1 = 1u;
    i0 <<= (i1 & 31);
    p0 = i0;
    i0 = l6;
    i1 = l5;
    i2 = 4u;
    i1 &= i2;
    i0 += i1;
    i1 = 16u;
    i0 += i1;
    l8 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l5 = i0;
    if (i0) {goto L108;}
  i0 = l8;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l4;
  i1 = 24u;
  i0 += i1;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  B106:;
  i0 = l4;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B59;
  B70:;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  p0 = i0;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l3;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l6;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l6;
  i1 = l3;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l6;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  B69:;
  i0 = l11;
  i1 = 8u;
  i0 += i1;
  p0 = i0;
  goto B0;
  B68:;
  i0 = l6;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  p0 = i0;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l6;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l4;
  i1 = 24u;
  i0 += i1;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l4;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  B59:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3124));
  p0 = i0;
  i1 = l3;
  i0 = i0 <= i1;
  if (i0) {goto B3;}
  i0 = 0u;
  i1 = p0;
  i2 = l3;
  i1 -= i2;
  l4 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3124), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3136));
  p0 = i1;
  i2 = l3;
  i1 += i2;
  l6 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3136), i1);
  i0 = l6;
  i1 = l4;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = p0;
  i1 = l3;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = p0;
  i1 = 8u;
  i0 += i1;
  p0 = i0;
  goto B0;
  B3:;
  i0 = __errno_location();
  i1 = 48u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = 0u;
  p0 = i0;
  goto B0;
  B2:;
  i0 = l11;
  i0 = !(i0);
  if (i0) {goto B109;}
  i0 = l8;
  i1 = l8;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 28));
  l4 = i1;
  i2 = 2u;
  i1 <<= (i2 & 31);
  i2 = 3416u;
  i1 += i2;
  p0 = i1;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  i0 = i0 != i1;
  if (i0) {goto B111;}
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l5;
  if (i0) {goto B110;}
  i0 = 0u;
  i1 = l7;
  i2 = 4294967294u;
  i3 = l4;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  l7 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  goto B109;
  B111:;
  i0 = l11;
  i1 = 16u;
  i2 = 20u;
  i3 = l11;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 16));
  i4 = l8;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 += i1;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l5;
  i0 = !(i0);
  if (i0) {goto B109;}
  B110:;
  i0 = l5;
  i1 = l11;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l8;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B112;}
  i0 = l5;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B112:;
  i0 = l8;
  i1 = 20u;
  i0 += i1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B109;}
  i0 = l5;
  i1 = 20u;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B109:;
  i0 = l6;
  i1 = 15u;
  i0 = i0 > i1;
  if (i0) {goto B114;}
  i0 = l8;
  i1 = l6;
  i2 = l3;
  i1 += i2;
  p0 = i1;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l8;
  i1 = p0;
  i0 += i1;
  p0 = i0;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  goto B113;
  B114:;
  i0 = l8;
  i1 = l3;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l8;
  i1 = l3;
  i0 += i1;
  l5 = i0;
  i1 = l6;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l5;
  i1 = l6;
  i0 += i1;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l6;
  i1 = 255u;
  i0 = i0 > i1;
  if (i0) {goto B115;}
  i0 = l6;
  i1 = 3u;
  i0 >>= (i1 & 31);
  l4 = i0;
  i1 = 3u;
  i0 <<= (i1 & 31);
  i1 = 3152u;
  i0 += i1;
  p0 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3112));
  l6 = i0;
  i1 = 1u;
  i2 = l4;
  i1 <<= (i2 & 31);
  l4 = i1;
  i0 &= i1;
  if (i0) {goto B117;}
  i0 = 0u;
  i1 = l6;
  i2 = l4;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  i0 = p0;
  l4 = i0;
  goto B116;
  B117:;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l4 = i0;
  B116:;
  i0 = p0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l4;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B113;
  B115:;
  i0 = l6;
  i1 = 8u;
  i0 >>= (i1 & 31);
  l4 = i0;
  if (i0) {goto B119;}
  i0 = 0u;
  p0 = i0;
  goto B118;
  B119:;
  i0 = 31u;
  p0 = i0;
  i0 = l6;
  i1 = 16777215u;
  i0 = i0 > i1;
  if (i0) {goto B118;}
  i0 = l4;
  i1 = l4;
  i2 = 1048320u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 8u;
  i1 &= i2;
  p0 = i1;
  i0 <<= (i1 & 31);
  l4 = i0;
  i1 = l4;
  i2 = 520192u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 4u;
  i1 &= i2;
  l4 = i1;
  i0 <<= (i1 & 31);
  l3 = i0;
  i1 = l3;
  i2 = 245760u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 2u;
  i1 &= i2;
  l3 = i1;
  i0 <<= (i1 & 31);
  i1 = 15u;
  i0 >>= (i1 & 31);
  i1 = l4;
  i2 = p0;
  i1 |= i2;
  i2 = l3;
  i1 |= i2;
  i0 -= i1;
  p0 = i0;
  i1 = 1u;
  i0 <<= (i1 & 31);
  i1 = l6;
  i2 = p0;
  i3 = 21u;
  i2 += i3;
  i1 >>= (i2 & 31);
  i2 = 1u;
  i1 &= i2;
  i0 |= i1;
  i1 = 28u;
  i0 += i1;
  p0 = i0;
  B118:;
  i0 = l5;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
  i0 = l5;
  j1 = 0ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 16), j1);
  i0 = p0;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  l4 = i0;
  i0 = l7;
  i1 = 1u;
  i2 = p0;
  i1 <<= (i2 & 31);
  l3 = i1;
  i0 &= i1;
  if (i0) {goto B122;}
  i0 = 0u;
  i1 = l7;
  i2 = l3;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  i0 = l4;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l5;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  goto B121;
  B122:;
  i0 = l6;
  i1 = 0u;
  i2 = 25u;
  i3 = p0;
  i4 = 1u;
  i3 >>= (i4 & 31);
  i2 -= i3;
  i3 = p0;
  i4 = 31u;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 <<= (i1 & 31);
  p0 = i0;
  i0 = l4;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l3 = i0;
  L123: 
    i0 = l3;
    l4 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
    i1 = 4294967288u;
    i0 &= i1;
    i1 = l6;
    i0 = i0 == i1;
    if (i0) {goto B120;}
    i0 = p0;
    i1 = 29u;
    i0 >>= (i1 & 31);
    l3 = i0;
    i0 = p0;
    i1 = 1u;
    i0 <<= (i1 & 31);
    p0 = i0;
    i0 = l4;
    i1 = l3;
    i2 = 4u;
    i1 &= i2;
    i0 += i1;
    i1 = 16u;
    i0 += i1;
    l2 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l3 = i0;
    if (i0) {goto L123;}
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l5;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B121:;
  i0 = l5;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B113;
  B120:;
  i0 = l4;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  p0 = i0;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l5;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l5;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  B113:;
  i0 = l8;
  i1 = 8u;
  i0 += i1;
  p0 = i0;
  goto B0;
  B1:;
  i0 = l10;
  i0 = !(i0);
  if (i0) {goto B124;}
  i0 = l5;
  i1 = l5;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 28));
  l6 = i1;
  i2 = 2u;
  i1 <<= (i2 & 31);
  i2 = 3416u;
  i1 += i2;
  p0 = i1;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  i0 = i0 != i1;
  if (i0) {goto B126;}
  i0 = p0;
  i1 = l8;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l8;
  if (i0) {goto B125;}
  i0 = 0u;
  i1 = l9;
  i2 = 4294967294u;
  i3 = l6;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  goto B124;
  B126:;
  i0 = l10;
  i1 = 16u;
  i2 = 20u;
  i3 = l10;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 16));
  i4 = l5;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 += i1;
  i1 = l8;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l8;
  i0 = !(i0);
  if (i0) {goto B124;}
  B125:;
  i0 = l8;
  i1 = l10;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l5;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B127;}
  i0 = l8;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = p0;
  i1 = l8;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B127:;
  i0 = l5;
  i1 = 20u;
  i0 += i1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B124;}
  i0 = l8;
  i1 = 20u;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = l8;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B124:;
  i0 = l4;
  i1 = 15u;
  i0 = i0 > i1;
  if (i0) {goto B129;}
  i0 = l5;
  i1 = l4;
  i2 = l3;
  i1 += i2;
  p0 = i1;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l5;
  i1 = p0;
  i0 += i1;
  p0 = i0;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  goto B128;
  B129:;
  i0 = l5;
  i1 = l3;
  i2 = 3u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l5;
  i1 = l3;
  i0 += i1;
  l6 = i0;
  i1 = l4;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l6;
  i1 = l4;
  i0 += i1;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l7;
  i0 = !(i0);
  if (i0) {goto B130;}
  i0 = l7;
  i1 = 3u;
  i0 >>= (i1 & 31);
  l8 = i0;
  i1 = 3u;
  i0 <<= (i1 & 31);
  i1 = 3152u;
  i0 += i1;
  l3 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3132));
  p0 = i0;
  i0 = 1u;
  i1 = l8;
  i0 <<= (i1 & 31);
  l8 = i0;
  i1 = l2;
  i0 &= i1;
  if (i0) {goto B132;}
  i0 = 0u;
  i1 = l8;
  i2 = l2;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  i0 = l3;
  l8 = i0;
  goto B131;
  B132:;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l8 = i0;
  B131:;
  i0 = l3;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l8;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = p0;
  i1 = l3;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = p0;
  i1 = l8;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  B130:;
  i0 = 0u;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 3132), i1);
  i0 = 0u;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  B128:;
  i0 = l5;
  i1 = 8u;
  i0 += i1;
  p0 = i0;
  B0:;
  i0 = l1;
  i1 = 16u;
  i0 += i1;
  g0 = i0;
  i0 = p0;
  FUNC_EPILOGUE;
  return i0;
}

static void free(u32 p0) {
  u32 l1 = 0, l2 = 0, l3 = 0, l4 = 0, l5 = 0, l6 = 0, l7 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3, i4;
  u64 j1;
  i0 = p0;
  i0 = !(i0);
  if (i0) {goto B0;}
  i0 = p0;
  i1 = 4294967288u;
  i0 += i1;
  l1 = i0;
  i1 = p0;
  i2 = 4294967292u;
  i1 += i2;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  l2 = i1;
  i2 = 4294967288u;
  i1 &= i2;
  p0 = i1;
  i0 += i1;
  l3 = i0;
  i0 = l2;
  i1 = 1u;
  i0 &= i1;
  if (i0) {goto B1;}
  i0 = l2;
  i1 = 3u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B0;}
  i0 = l1;
  i1 = l1;
  i1 = i32_load(Z_envZ_memory, (u64)(i1));
  l2 = i1;
  i0 -= i1;
  l1 = i0;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3128));
  l4 = i1;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = l2;
  i1 = p0;
  i0 += i1;
  p0 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3132));
  i1 = l1;
  i0 = i0 == i1;
  if (i0) {goto B2;}
  i0 = l2;
  i1 = 255u;
  i0 = i0 > i1;
  if (i0) {goto B3;}
  i0 = l1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  l5 = i0;
  i0 = l1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l6 = i0;
  i1 = l2;
  i2 = 3u;
  i1 >>= (i2 & 31);
  l7 = i1;
  i2 = 3u;
  i1 <<= (i2 & 31);
  i2 = 3152u;
  i1 += i2;
  l2 = i1;
  i0 = i0 == i1;
  if (i0) {goto B4;}
  i0 = l4;
  i1 = l6;
  i0 = i0 > i1;
  B4:;
  i0 = l5;
  i1 = l6;
  i0 = i0 != i1;
  if (i0) {goto B5;}
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3112));
  i2 = 4294967294u;
  i3 = l7;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  goto B1;
  B5:;
  i0 = l5;
  i1 = l2;
  i0 = i0 == i1;
  if (i0) {goto B6;}
  i0 = l4;
  i1 = l5;
  i0 = i0 > i1;
  B6:;
  i0 = l6;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = l6;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B1;
  B3:;
  i0 = l1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 24));
  l7 = i0;
  i0 = l1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  l5 = i0;
  i1 = l1;
  i0 = i0 == i1;
  if (i0) {goto B8;}
  i0 = l4;
  i1 = l1;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 8));
  l2 = i1;
  i0 = i0 > i1;
  if (i0) {goto B9;}
  i0 = l2;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  i1 = l1;
  i0 = i0 != i1;
  B9:;
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B7;
  B8:;
  i0 = l1;
  i1 = 20u;
  i0 += i1;
  l2 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l4 = i0;
  if (i0) {goto B10;}
  i0 = l1;
  i1 = 16u;
  i0 += i1;
  l2 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l4 = i0;
  if (i0) {goto B10;}
  i0 = 0u;
  l5 = i0;
  goto B7;
  B10:;
  L11: 
    i0 = l2;
    l6 = i0;
    i0 = l4;
    l5 = i0;
    i1 = 20u;
    i0 += i1;
    l2 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l4 = i0;
    if (i0) {goto L11;}
    i0 = l5;
    i1 = 16u;
    i0 += i1;
    l2 = i0;
    i0 = l5;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
    l4 = i0;
    if (i0) {goto L11;}
  i0 = l6;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  B7:;
  i0 = l7;
  i0 = !(i0);
  if (i0) {goto B1;}
  i0 = l1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 28));
  l4 = i0;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  l2 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  i1 = l1;
  i0 = i0 != i1;
  if (i0) {goto B13;}
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l5;
  if (i0) {goto B12;}
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3116));
  i2 = 4294967294u;
  i3 = l4;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  goto B1;
  B13:;
  i0 = l7;
  i1 = 16u;
  i2 = 20u;
  i3 = l7;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 16));
  i4 = l1;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 += i1;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l5;
  i0 = !(i0);
  if (i0) {goto B1;}
  B12:;
  i0 = l5;
  i1 = l7;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  l2 = i0;
  i0 = !(i0);
  if (i0) {goto B14;}
  i0 = l5;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B14:;
  i0 = l1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
  l2 = i0;
  i0 = !(i0);
  if (i0) {goto B1;}
  i0 = l5;
  i1 = 20u;
  i0 += i1;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  goto B1;
  B2:;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
  l2 = i0;
  i1 = 3u;
  i0 &= i1;
  i1 = 3u;
  i0 = i0 != i1;
  if (i0) {goto B1;}
  i0 = 0u;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  i0 = l3;
  i1 = l2;
  i2 = 4294967294u;
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l1;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l1;
  i1 = p0;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  goto Bfunc;
  B1:;
  i0 = l3;
  i1 = l1;
  i0 = i0 <= i1;
  if (i0) {goto B0;}
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
  l2 = i0;
  i1 = 1u;
  i0 &= i1;
  i0 = !(i0);
  if (i0) {goto B0;}
  i0 = l2;
  i1 = 2u;
  i0 &= i1;
  if (i0) {goto B16;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3136));
  i1 = l3;
  i0 = i0 != i1;
  if (i0) {goto B17;}
  i0 = 0u;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3136), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3124));
  i2 = p0;
  i1 += i2;
  p0 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3124), i1);
  i0 = l1;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l1;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3132));
  i0 = i0 != i1;
  if (i0) {goto B0;}
  i0 = 0u;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  i0 = 0u;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3132), i1);
  goto Bfunc;
  B17:;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3132));
  i1 = l3;
  i0 = i0 != i1;
  if (i0) {goto B18;}
  i0 = 0u;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3132), i1);
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3120));
  i2 = p0;
  i1 += i2;
  p0 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  i0 = l1;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l1;
  i1 = p0;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  goto Bfunc;
  B18:;
  i0 = l2;
  i1 = 4294967288u;
  i0 &= i1;
  i1 = p0;
  i0 += i1;
  p0 = i0;
  i0 = l2;
  i1 = 255u;
  i0 = i0 > i1;
  if (i0) {goto B20;}
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  l4 = i0;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l5 = i0;
  i1 = l2;
  i2 = 3u;
  i1 >>= (i2 & 31);
  l3 = i1;
  i2 = 3u;
  i1 <<= (i2 & 31);
  i2 = 3152u;
  i1 += i2;
  l2 = i1;
  i0 = i0 == i1;
  if (i0) {goto B21;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3128));
  i1 = l5;
  i0 = i0 > i1;
  B21:;
  i0 = l4;
  i1 = l5;
  i0 = i0 != i1;
  if (i0) {goto B22;}
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3112));
  i2 = 4294967294u;
  i3 = l3;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  goto B19;
  B22:;
  i0 = l4;
  i1 = l2;
  i0 = i0 == i1;
  if (i0) {goto B23;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3128));
  i1 = l4;
  i0 = i0 > i1;
  B23:;
  i0 = l5;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B19;
  B20:;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 24));
  l7 = i0;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  l5 = i0;
  i1 = l3;
  i0 = i0 == i1;
  if (i0) {goto B25;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3128));
  i1 = l3;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 8));
  l2 = i1;
  i0 = i0 > i1;
  if (i0) {goto B26;}
  i0 = l2;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 12));
  i1 = l3;
  i0 = i0 != i1;
  B26:;
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l5;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B24;
  B25:;
  i0 = l3;
  i1 = 20u;
  i0 += i1;
  l2 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l4 = i0;
  if (i0) {goto B27;}
  i0 = l3;
  i1 = 16u;
  i0 += i1;
  l2 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l4 = i0;
  if (i0) {goto B27;}
  i0 = 0u;
  l5 = i0;
  goto B24;
  B27:;
  L28: 
    i0 = l2;
    l6 = i0;
    i0 = l4;
    l5 = i0;
    i1 = 20u;
    i0 += i1;
    l2 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l4 = i0;
    if (i0) {goto L28;}
    i0 = l5;
    i1 = 16u;
    i0 += i1;
    l2 = i0;
    i0 = l5;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
    l4 = i0;
    if (i0) {goto L28;}
  i0 = l6;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  B24:;
  i0 = l7;
  i0 = !(i0);
  if (i0) {goto B19;}
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 28));
  l4 = i0;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  l2 = i0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  i1 = l3;
  i0 = i0 != i1;
  if (i0) {goto B30;}
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l5;
  if (i0) {goto B29;}
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3116));
  i2 = 4294967294u;
  i3 = l4;
  i2 = I32_ROTL(i2, i3);
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  goto B19;
  B30:;
  i0 = l7;
  i1 = 16u;
  i2 = 20u;
  i3 = l7;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 16));
  i4 = l3;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 += i1;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l5;
  i0 = !(i0);
  if (i0) {goto B19;}
  B29:;
  i0 = l5;
  i1 = l7;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 16));
  l2 = i0;
  i0 = !(i0);
  if (i0) {goto B31;}
  i0 = l5;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B31:;
  i0 = l3;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
  l2 = i0;
  i0 = !(i0);
  if (i0) {goto B19;}
  i0 = l5;
  i1 = 20u;
  i0 += i1;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l2;
  i1 = l5;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  B19:;
  i0 = l1;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l1;
  i1 = p0;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l1;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3132));
  i0 = i0 != i1;
  if (i0) {goto B15;}
  i0 = 0u;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 3120), i1);
  goto Bfunc;
  B16:;
  i0 = l3;
  i1 = l2;
  i2 = 4294967294u;
  i1 &= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l1;
  i1 = p0;
  i2 = 1u;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = l1;
  i1 = p0;
  i0 += i1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  B15:;
  i0 = p0;
  i1 = 255u;
  i0 = i0 > i1;
  if (i0) {goto B32;}
  i0 = p0;
  i1 = 3u;
  i0 >>= (i1 & 31);
  l2 = i0;
  i1 = 3u;
  i0 <<= (i1 & 31);
  i1 = 3152u;
  i0 += i1;
  p0 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3112));
  l4 = i0;
  i1 = 1u;
  i2 = l2;
  i1 <<= (i2 & 31);
  l2 = i1;
  i0 &= i1;
  if (i0) {goto B34;}
  i0 = 0u;
  i1 = l4;
  i2 = l2;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3112), i1);
  i0 = p0;
  l2 = i0;
  goto B33;
  B34:;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  l2 = i0;
  B33:;
  i0 = p0;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l2;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l1;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto Bfunc;
  B32:;
  i0 = 0u;
  l2 = i0;
  i0 = p0;
  i1 = 8u;
  i0 >>= (i1 & 31);
  l4 = i0;
  i0 = !(i0);
  if (i0) {goto B35;}
  i0 = 31u;
  l2 = i0;
  i0 = p0;
  i1 = 16777215u;
  i0 = i0 > i1;
  if (i0) {goto B35;}
  i0 = l4;
  i1 = l4;
  i2 = 1048320u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 8u;
  i1 &= i2;
  l2 = i1;
  i0 <<= (i1 & 31);
  l4 = i0;
  i1 = l4;
  i2 = 520192u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 4u;
  i1 &= i2;
  l4 = i1;
  i0 <<= (i1 & 31);
  l5 = i0;
  i1 = l5;
  i2 = 245760u;
  i1 += i2;
  i2 = 16u;
  i1 >>= (i2 & 31);
  i2 = 2u;
  i1 &= i2;
  l5 = i1;
  i0 <<= (i1 & 31);
  i1 = 15u;
  i0 >>= (i1 & 31);
  i1 = l4;
  i2 = l2;
  i1 |= i2;
  i2 = l5;
  i1 |= i2;
  i0 -= i1;
  l2 = i0;
  i1 = 1u;
  i0 <<= (i1 & 31);
  i1 = p0;
  i2 = l2;
  i3 = 21u;
  i2 += i3;
  i1 >>= (i2 & 31);
  i2 = 1u;
  i1 &= i2;
  i0 |= i1;
  i1 = 28u;
  i0 += i1;
  l2 = i0;
  B35:;
  i0 = l1;
  j1 = 0ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 16), j1);
  i0 = l1;
  i1 = 28u;
  i0 += i1;
  i1 = l2;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l2;
  i1 = 2u;
  i0 <<= (i1 & 31);
  i1 = 3416u;
  i0 += i1;
  l4 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3116));
  l5 = i0;
  i1 = 1u;
  i2 = l2;
  i1 <<= (i2 & 31);
  l3 = i1;
  i0 &= i1;
  if (i0) {goto B37;}
  i0 = 0u;
  i1 = l5;
  i2 = l3;
  i1 |= i2;
  i32_store(Z_envZ_memory, (u64)(i0 + 3116), i1);
  i0 = l4;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l1;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l1;
  i1 = 24u;
  i0 += i1;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l1;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B36;
  B37:;
  i0 = p0;
  i1 = 0u;
  i2 = 25u;
  i3 = l2;
  i4 = 1u;
  i3 >>= (i4 & 31);
  i2 -= i3;
  i3 = l2;
  i4 = 31u;
  i3 = i3 == i4;
  i1 = i3 ? i1 : i2;
  i0 <<= (i1 & 31);
  l2 = i0;
  i0 = l4;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l5 = i0;
  L39: 
    i0 = l5;
    l4 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
    i1 = 4294967288u;
    i0 &= i1;
    i1 = p0;
    i0 = i0 == i1;
    if (i0) {goto B38;}
    i0 = l2;
    i1 = 29u;
    i0 >>= (i1 & 31);
    l5 = i0;
    i0 = l2;
    i1 = 1u;
    i0 <<= (i1 & 31);
    l2 = i0;
    i0 = l4;
    i1 = l5;
    i2 = 4u;
    i1 &= i2;
    i0 += i1;
    i1 = 16u;
    i0 += i1;
    l3 = i0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    l5 = i0;
    if (i0) {goto L39;}
  i0 = l3;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l1;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l1;
  i1 = 24u;
  i0 += i1;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l1;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  goto B36;
  B38:;
  i0 = l4;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 8));
  p0 = i0;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l4;
  i1 = l1;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l1;
  i1 = 24u;
  i0 += i1;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l1;
  i1 = l4;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = l1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  B36:;
  i0 = 0u;
  i1 = 0u;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 3144));
  i2 = 4294967295u;
  i1 += i2;
  l1 = i1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3144), i1);
  i0 = l1;
  if (i0) {goto B0;}
  i0 = 3568u;
  l1 = i0;
  L40: 
    i0 = l1;
    i0 = i32_load(Z_envZ_memory, (u64)(i0));
    p0 = i0;
    i1 = 8u;
    i0 += i1;
    l1 = i0;
    i0 = p0;
    if (i0) {goto L40;}
  i0 = 0u;
  i1 = 4294967295u;
  i32_store(Z_envZ_memory, (u64)(i0 + 3144), i1);
  B0:;
  Bfunc:;
  FUNC_EPILOGUE;
}

static u32 f52(u32 p0) {
  u32 l1 = 0, l2 = 0, l3 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = f6();
  l1 = i0;
  i0 = (*Z_envZ_memory).pages;
  l2 = i0;
  i0 = l1;
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  l3 = i0;
  i1 = p0;
  i0 += i1;
  p0 = i0;
  i1 = l2;
  i2 = 16u;
  i1 <<= (i2 & 31);
  i0 = i0 <= i1;
  if (i0) {goto B0;}
  i0 = p0;
  i0 = (*Z_envZ_emscripten_resize_heapZ_ii)(i0);
  if (i0) {goto B0;}
  i0 = __errno_location();
  i1 = 48u;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = 4294967295u;
  goto Bfunc;
  B0:;
  i0 = l1;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l3;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f53(u32 p0, u32 p1, u32 p2) {
  u32 l3 = 0, l4 = 0, l5 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = p2;
  i1 = 8192u;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = p0;
  i1 = p1;
  i2 = p2;
  i0 = (*Z_envZ_emscripten_memcpy_bigZ_iiii)(i0, i1, i2);
  i0 = p0;
  goto Bfunc;
  B0:;
  i0 = p0;
  i1 = p2;
  i0 += i1;
  l3 = i0;
  i0 = p1;
  i1 = p0;
  i0 ^= i1;
  i1 = 3u;
  i0 &= i1;
  if (i0) {goto B2;}
  i0 = p2;
  i1 = 1u;
  i0 = (u32)((s32)i0 >= (s32)i1);
  if (i0) {goto B4;}
  i0 = p0;
  p2 = i0;
  goto B3;
  B4:;
  i0 = p0;
  i1 = 3u;
  i0 &= i1;
  if (i0) {goto B5;}
  i0 = p0;
  p2 = i0;
  goto B3;
  B5:;
  i0 = p0;
  p2 = i0;
  L6: 
    i0 = p2;
    i1 = p1;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1));
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    i0 = p1;
    i1 = 1u;
    i0 += i1;
    p1 = i0;
    i0 = p2;
    i1 = 1u;
    i0 += i1;
    p2 = i0;
    i1 = l3;
    i0 = i0 >= i1;
    if (i0) {goto B3;}
    i0 = p2;
    i1 = 3u;
    i0 &= i1;
    if (i0) {goto L6;}
  B3:;
  i0 = l3;
  i1 = 4294967292u;
  i0 &= i1;
  l4 = i0;
  i1 = 64u;
  i0 = i0 < i1;
  if (i0) {goto B7;}
  i0 = p2;
  i1 = l4;
  i2 = 4294967232u;
  i1 += i2;
  l5 = i1;
  i0 = i0 > i1;
  if (i0) {goto B7;}
  L8: 
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1));
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 4));
    i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 8));
    i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 12));
    i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 16));
    i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 20));
    i32_store(Z_envZ_memory, (u64)(i0 + 20), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 24));
    i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 28));
    i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 32));
    i32_store(Z_envZ_memory, (u64)(i0 + 32), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 36));
    i32_store(Z_envZ_memory, (u64)(i0 + 36), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 40));
    i32_store(Z_envZ_memory, (u64)(i0 + 40), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 44));
    i32_store(Z_envZ_memory, (u64)(i0 + 44), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 48));
    i32_store(Z_envZ_memory, (u64)(i0 + 48), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 52));
    i32_store(Z_envZ_memory, (u64)(i0 + 52), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 56));
    i32_store(Z_envZ_memory, (u64)(i0 + 56), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 60));
    i32_store(Z_envZ_memory, (u64)(i0 + 60), i1);
    i0 = p1;
    i1 = 64u;
    i0 += i1;
    p1 = i0;
    i0 = p2;
    i1 = 64u;
    i0 += i1;
    p2 = i0;
    i1 = l5;
    i0 = i0 <= i1;
    if (i0) {goto L8;}
  B7:;
  i0 = p2;
  i1 = l4;
  i0 = i0 >= i1;
  if (i0) {goto B1;}
  L9: 
    i0 = p2;
    i1 = p1;
    i1 = i32_load(Z_envZ_memory, (u64)(i1));
    i32_store(Z_envZ_memory, (u64)(i0), i1);
    i0 = p1;
    i1 = 4u;
    i0 += i1;
    p1 = i0;
    i0 = p2;
    i1 = 4u;
    i0 += i1;
    p2 = i0;
    i1 = l4;
    i0 = i0 < i1;
    if (i0) {goto L9;}
    goto B1;
  UNREACHABLE;
  B2:;
  i0 = l3;
  i1 = 4u;
  i0 = i0 >= i1;
  if (i0) {goto B10;}
  i0 = p0;
  p2 = i0;
  goto B1;
  B10:;
  i0 = l3;
  i1 = 4294967292u;
  i0 += i1;
  l4 = i0;
  i1 = p0;
  i0 = i0 >= i1;
  if (i0) {goto B11;}
  i0 = p0;
  p2 = i0;
  goto B1;
  B11:;
  i0 = p0;
  p2 = i0;
  L12: 
    i0 = p2;
    i1 = p1;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1));
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1 + 1));
    i32_store8(Z_envZ_memory, (u64)(i0 + 1), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1 + 2));
    i32_store8(Z_envZ_memory, (u64)(i0 + 2), i1);
    i0 = p2;
    i1 = p1;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1 + 3));
    i32_store8(Z_envZ_memory, (u64)(i0 + 3), i1);
    i0 = p1;
    i1 = 4u;
    i0 += i1;
    p1 = i0;
    i0 = p2;
    i1 = 4u;
    i0 += i1;
    p2 = i0;
    i1 = l4;
    i0 = i0 <= i1;
    if (i0) {goto L12;}
  B1:;
  i0 = p2;
  i1 = l3;
  i0 = i0 >= i1;
  if (i0) {goto B13;}
  L14: 
    i0 = p2;
    i1 = p1;
    i1 = i32_load8_u(Z_envZ_memory, (u64)(i1));
    i32_store8(Z_envZ_memory, (u64)(i0), i1);
    i0 = p1;
    i1 = 1u;
    i0 += i1;
    p1 = i0;
    i0 = p2;
    i1 = 1u;
    i0 += i1;
    p2 = i0;
    i1 = l3;
    i0 = i0 != i1;
    if (i0) {goto L14;}
  B13:;
  i0 = p0;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f54(u32 p0, u32 p1, u32 p2) {
  u32 l3 = 0, l4 = 0, l5 = 0;
  u64 l6 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  u64 j0, j1;
  i0 = p2;
  i0 = !(i0);
  if (i0) {goto B0;}
  i0 = p2;
  i1 = p0;
  i0 += i1;
  l3 = i0;
  i1 = 4294967295u;
  i0 += i1;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = p2;
  i1 = 3u;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = l3;
  i1 = 4294967294u;
  i0 += i1;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0 + 1), i1);
  i0 = l3;
  i1 = 4294967293u;
  i0 += i1;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0 + 2), i1);
  i0 = p2;
  i1 = 7u;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = l3;
  i1 = 4294967292u;
  i0 += i1;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0), i1);
  i0 = p0;
  i1 = p1;
  i32_store8(Z_envZ_memory, (u64)(i0 + 3), i1);
  i0 = p2;
  i1 = 9u;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = p0;
  i1 = 0u;
  i2 = p0;
  i1 -= i2;
  i2 = 3u;
  i1 &= i2;
  l4 = i1;
  i0 += i1;
  l3 = i0;
  i1 = p1;
  i2 = 255u;
  i1 &= i2;
  i2 = 16843009u;
  i1 *= i2;
  p1 = i1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l3;
  i1 = p2;
  i2 = l4;
  i1 -= i2;
  i2 = 4294967292u;
  i1 &= i2;
  l4 = i1;
  i0 += i1;
  p2 = i0;
  i1 = 4294967292u;
  i0 += i1;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l4;
  i1 = 9u;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = l3;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 8), i1);
  i0 = l3;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 4), i1);
  i0 = p2;
  i1 = 4294967288u;
  i0 += i1;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p2;
  i1 = 4294967284u;
  i0 += i1;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l4;
  i1 = 25u;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = l3;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 24), i1);
  i0 = l3;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 20), i1);
  i0 = l3;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 16), i1);
  i0 = l3;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 12), i1);
  i0 = p2;
  i1 = 4294967280u;
  i0 += i1;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p2;
  i1 = 4294967276u;
  i0 += i1;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p2;
  i1 = 4294967272u;
  i0 += i1;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = p2;
  i1 = 4294967268u;
  i0 += i1;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0), i1);
  i0 = l4;
  i1 = l3;
  i2 = 4u;
  i1 &= i2;
  i2 = 24u;
  i1 |= i2;
  l5 = i1;
  i0 -= i1;
  p2 = i0;
  i1 = 32u;
  i0 = i0 < i1;
  if (i0) {goto B0;}
  i0 = p1;
  j0 = (u64)(i0);
  l6 = j0;
  j1 = 32ull;
  j0 <<= (j1 & 63);
  j1 = l6;
  j0 |= j1;
  l6 = j0;
  i0 = l3;
  i1 = l5;
  i0 += i1;
  p1 = i0;
  L1: 
    i0 = p1;
    j1 = l6;
    i64_store(Z_envZ_memory, (u64)(i0 + 24), j1);
    i0 = p1;
    j1 = l6;
    i64_store(Z_envZ_memory, (u64)(i0 + 16), j1);
    i0 = p1;
    j1 = l6;
    i64_store(Z_envZ_memory, (u64)(i0 + 8), j1);
    i0 = p1;
    j1 = l6;
    i64_store(Z_envZ_memory, (u64)(i0), j1);
    i0 = p1;
    i1 = 32u;
    i0 += i1;
    p1 = i0;
    i0 = p2;
    i1 = 4294967264u;
    i0 += i1;
    p2 = i0;
    i1 = 31u;
    i0 = i0 > i1;
    if (i0) {goto L1;}
  B0:;
  i0 = p0;
  FUNC_EPILOGUE;
  return i0;
}

static void setThrew(u32 p0, u32 p1) {
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 3608));
  if (i0) {goto B0;}
  i0 = 0u;
  i1 = p1;
  i32_store(Z_envZ_memory, (u64)(i0 + 3612), i1);
  i0 = 0u;
  i1 = p0;
  i32_store(Z_envZ_memory, (u64)(i0 + 3608), i1);
  B0:;
  FUNC_EPILOGUE;
}

static u32 fflush(u32 p0) {
  u32 l1 = 0, l2 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = p0;
  i0 = !(i0);
  if (i0) {goto B1;}
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
  i1 = 4294967295u;
  i0 = (u32)((s32)i0 > (s32)i1);
  if (i0) {goto B2;}
  i0 = p0;
  i0 = f57(i0);
  goto Bfunc;
  B2:;
  i0 = p0;
  i0 = f15(i0);
  l1 = i0;
  i0 = p0;
  i0 = f57(i0);
  l2 = i0;
  i0 = l1;
  i0 = !(i0);
  if (i0) {goto B0;}
  i0 = p0;
  f16(i0);
  i0 = l2;
  goto Bfunc;
  B1:;
  i0 = 0u;
  l2 = i0;
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 1752));
  i0 = !(i0);
  if (i0) {goto B3;}
  i0 = 0u;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 1752));
  i0 = fflush(i0);
  l2 = i0;
  B3:;
  i0 = f24();
  i0 = i32_load(Z_envZ_memory, (u64)(i0));
  p0 = i0;
  i0 = !(i0);
  if (i0) {goto B4;}
  L5: 
    i0 = 0u;
    l1 = i0;
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 76));
    i1 = 0u;
    i0 = (u32)((s32)i0 < (s32)i1);
    if (i0) {goto B6;}
    i0 = p0;
    i0 = f15(i0);
    l1 = i0;
    B6:;
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
    i1 = p0;
    i1 = i32_load(Z_envZ_memory, (u64)(i1 + 28));
    i0 = i0 <= i1;
    if (i0) {goto B7;}
    i0 = p0;
    i0 = f57(i0);
    i1 = l2;
    i0 |= i1;
    l2 = i0;
    B7:;
    i0 = l1;
    i0 = !(i0);
    if (i0) {goto B8;}
    i0 = p0;
    f16(i0);
    B8:;
    i0 = p0;
    i0 = i32_load(Z_envZ_memory, (u64)(i0 + 56));
    p0 = i0;
    if (i0) {goto L5;}
  B4:;
  f25();
  B0:;
  i0 = l2;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 f57(u32 p0) {
  u32 l1 = 0, l2 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3;
  u64 j0, j1;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 28));
  i0 = i0 <= i1;
  if (i0) {goto B0;}
  i0 = p0;
  i1 = 0u;
  i2 = 0u;
  i3 = p0;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 36));
  i0 = CALL_INDIRECT((*Z_envZ_table), u32 (*)(u32, u32, u32), 0, i3, i0, i1, i2);
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 20));
  if (i0) {goto B0;}
  i0 = 4294967295u;
  goto Bfunc;
  B0:;
  i0 = p0;
  i0 = i32_load(Z_envZ_memory, (u64)(i0 + 4));
  l1 = i0;
  i1 = p0;
  i1 = i32_load(Z_envZ_memory, (u64)(i1 + 8));
  l2 = i1;
  i0 = i0 >= i1;
  if (i0) {goto B1;}
  i0 = p0;
  i1 = l1;
  i2 = l2;
  i1 -= i2;
  j1 = (u64)(s64)(s32)(i1);
  i2 = 1u;
  i3 = p0;
  i3 = i32_load(Z_envZ_memory, (u64)(i3 + 40));
  j0 = CALL_INDIRECT((*Z_envZ_table), u64 (*)(u32, u64, u32), 3, i3, i0, j1, i2);
  B1:;
  i0 = p0;
  i1 = 0u;
  i32_store(Z_envZ_memory, (u64)(i0 + 28), i1);
  i0 = p0;
  j1 = 0ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 16), j1);
  i0 = p0;
  j1 = 0ull;
  i64_store(Z_envZ_memory, (u64)(i0 + 4), j1);
  i0 = 0u;
  Bfunc:;
  FUNC_EPILOGUE;
  return i0;
}

static u32 stackSave(void) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = g0;
  FUNC_EPILOGUE;
  return i0;
}

static u32 stackAlloc(u32 p0) {
  u32 l1 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = g0;
  i1 = p0;
  i0 -= i1;
  i1 = 4294967280u;
  i0 &= i1;
  l1 = i0;
  g0 = i0;
  i0 = l1;
  FUNC_EPILOGUE;
  return i0;
}

static void stackRestore(u32 p0) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = p0;
  g0 = i0;
  FUNC_EPILOGUE;
}

static u32 __growWasmMemory(u32 p0) {
  FUNC_PROLOGUE;
  u32 i0;
  i0 = p0;
  i0 = wasm_rt_grow_memory(Z_envZ_memory, i0);
  FUNC_EPILOGUE;
  return i0;
}

static u32 dynCall_ii(u32 p0, u32 p1) {
  FUNC_PROLOGUE;
  u32 i0, i1;
  i0 = p1;
  i1 = p0;
  i0 = CALL_INDIRECT((*Z_envZ_table), u32 (*)(u32), 22, i1, i0);
  FUNC_EPILOGUE;
  return i0;
}

static u32 dynCall_iiii(u32 p0, u32 p1, u32 p2, u32 p3) {
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3;
  i0 = p1;
  i1 = p2;
  i2 = p3;
  i3 = p0;
  i0 = CALL_INDIRECT((*Z_envZ_table), u32 (*)(u32, u32, u32), 23, i3, i0, i1, i2);
  FUNC_EPILOGUE;
  return i0;
}

static u64 f64_0(u32 p0, u32 p1, u64 p2, u32 p3) {
  FUNC_PROLOGUE;
  u32 i0, i2, i3;
  u64 j0, j1;
  i0 = p1;
  j1 = p2;
  i2 = p3;
  i3 = p0;
  j0 = CALL_INDIRECT((*Z_envZ_table), u64 (*)(u32, u64, u32), 24, i3, i0, j1, i2);
  FUNC_EPILOGUE;
  return j0;
}

static u32 dynCall_iidiiii(u32 p0, u32 p1, f64 p2, u32 p3, u32 p4, u32 p5, u32 p6) {
  FUNC_PROLOGUE;
  u32 i0, i2, i3, i4, i5, i6;
  f64 d1;
  i0 = p1;
  d1 = p2;
  i2 = p3;
  i3 = p4;
  i4 = p5;
  i5 = p6;
  i6 = p0;
  i0 = CALL_INDIRECT((*Z_envZ_table), u32 (*)(u32, f64, u32, u32, u32, u32), 25, i6, i0, d1, i2, i3, i4, i5);
  FUNC_EPILOGUE;
  return i0;
}

static void dynCall_vii(u32 p0, u32 p1, u32 p2) {
  FUNC_PROLOGUE;
  u32 i0, i1, i2;
  i0 = p1;
  i1 = p2;
  i2 = p0;
  CALL_INDIRECT((*Z_envZ_table), void (*)(u32, u32), 26, i2, i0, i1);
  FUNC_EPILOGUE;
}

static u32 dynCall_jiji(u32 p0, u32 p1, u32 p2, u32 p3, u32 p4) {
  u64 l5 = 0;
  FUNC_PROLOGUE;
  u32 i0, i1, i2, i3;
  u64 j0, j1, j2, j3, j4;
  i0 = p0;
  i1 = p1;
  i2 = p2;
  j2 = (u64)(i2);
  i3 = p3;
  j3 = (u64)(i3);
  j4 = 32ull;
  j3 <<= (j4 & 63);
  j2 |= j3;
  i3 = p4;
  j0 = f64_0(i0, i1, j2, i3);
  l5 = j0;
  j0 = l5;
  j1 = 32ull;
  j0 >>= (j1 & 63);
  i0 = (u32)(j0);
  (*Z_envZ_setTempRet0Z_vi)(i0);
  j0 = l5;
  i0 = (u32)(j0);
  FUNC_EPILOGUE;
  return i0;
}

static const u8 data_segment_data_0[] = {
  0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c, 0x20, 0x57, 0x6f, 0x72, 0x6c, 0x64, 
  0x21, 0x0a, 0x00, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x3a, 0x20, 0x25, 0x73, 
  0x0a, 0x00, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c, 0x20, 0x00, 0x21, 0x00, 
  0x48, 0x06, 0x00, 0x00, 0x2d, 0x2b, 0x20, 0x20, 0x20, 0x30, 0x58, 0x30, 
  0x78, 0x00, 0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x29, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x0a, 0x00, 0x11, 0x11, 0x11, 0x00, 
  0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 
  0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x11, 0x00, 0x0f, 0x0a, 0x11, 0x11, 0x11, 0x03, 0x0a, 0x07, 0x00, 0x01, 
  0x13, 0x09, 0x0b, 0x0b, 0x00, 0x00, 0x09, 0x06, 0x0b, 0x00, 0x00, 0x0b, 
  0x00, 0x06, 0x11, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 
  0x0a, 0x0a, 0x11, 0x11, 0x11, 0x00, 0x0a, 0x00, 0x00, 0x02, 0x00, 0x09, 
  0x0b, 0x00, 0x00, 0x00, 0x09, 0x00, 0x0b, 0x00, 0x00, 0x0b, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 
  0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x09, 0x0c, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 
  0x00, 0x04, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x09, 0x0e, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x0e, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 
  0x0f, 0x00, 0x00, 0x00, 0x00, 0x09, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x12, 0x12, 
  0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 
  0x00, 0x09, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x0b, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x09, 
  0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x0c, 0x00, 0x00, 
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 
  0x43, 0x44, 0x45, 0x46, 0x2d, 0x30, 0x58, 0x2b, 0x30, 0x58, 0x20, 0x30, 
  0x58, 0x2d, 0x30, 0x78, 0x2b, 0x30, 0x78, 0x20, 0x30, 0x78, 0x00, 0x69, 
  0x6e, 0x66, 0x00, 0x49, 0x4e, 0x46, 0x00, 0x6e, 0x61, 0x6e, 0x00, 0x4e, 
  0x41, 0x4e, 0x00, 0x2e, 0x00, 
};

static const u8 data_segment_data_1[] = {
  0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xd8, 0x07, 0x00, 0x00, 
  0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x0a, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x48, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x04, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
};

static const u8 data_segment_data_2[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
};

static void init_memory(void) {
  memcpy(&((*Z_envZ_memory).data[1024u]), data_segment_data_0, 581);
  memcpy(&((*Z_envZ_memory).data[1608u]), data_segment_data_1, 392);
  memcpy(&((*Z_envZ_memory).data[2000u]), data_segment_data_2, 1616);
}

static void init_table(void) {
  uint32_t offset;
  offset = 1u;
  (*Z_envZ_table).data[offset + 0] = (wasm_rt_elem_t){func_types[7], (wasm_rt_anyfunc_t)(&f13)};
  (*Z_envZ_table).data[offset + 1] = (wasm_rt_elem_t){func_types[0], (wasm_rt_anyfunc_t)(&f12)};
  (*Z_envZ_table).data[offset + 2] = (wasm_rt_elem_t){func_types[3], (wasm_rt_anyfunc_t)(&f14)};
  (*Z_envZ_table).data[offset + 3] = (wasm_rt_elem_t){func_types[1], (wasm_rt_anyfunc_t)(&f38)};
  (*Z_envZ_table).data[offset + 4] = (wasm_rt_elem_t){func_types[2], (wasm_rt_anyfunc_t)(&f39)};
}

/* export: '__wasm_call_ctors' */
void (*WASM_RT_ADD_PREFIX(Z___wasm_call_ctorsZ_vv))(void);
/* export: 'sayHello' */
void (*WASM_RT_ADD_PREFIX(Z_sayHelloZ_vv))(void);
/* export: 'add' */
f64 (*WASM_RT_ADD_PREFIX(Z_addZ_ddd))(f64, f64);
/* export: 'greet' */
u32 (*WASM_RT_ADD_PREFIX(Z_greetZ_ii))(u32);
/* export: 'malloc' */
u32 (*WASM_RT_ADD_PREFIX(Z_mallocZ_ii))(u32);
/* export: '__errno_location' */
u32 (*WASM_RT_ADD_PREFIX(Z___errno_locationZ_iv))(void);
/* export: 'fflush' */
u32 (*WASM_RT_ADD_PREFIX(Z_fflushZ_ii))(u32);
/* export: 'setThrew' */
void (*WASM_RT_ADD_PREFIX(Z_setThrewZ_vii))(u32, u32);
/* export: 'free' */
void (*WASM_RT_ADD_PREFIX(Z_freeZ_vi))(u32);
/* export: '__data_end' */
u32 (*WASM_RT_ADD_PREFIX(Z___data_endZ_i));
/* export: 'stackSave' */
u32 (*WASM_RT_ADD_PREFIX(Z_stackSaveZ_iv))(void);
/* export: 'stackAlloc' */
u32 (*WASM_RT_ADD_PREFIX(Z_stackAllocZ_ii))(u32);
/* export: 'stackRestore' */
void (*WASM_RT_ADD_PREFIX(Z_stackRestoreZ_vi))(u32);
/* export: '__growWasmMemory' */
u32 (*WASM_RT_ADD_PREFIX(Z___growWasmMemoryZ_ii))(u32);
/* export: 'dynCall_ii' */
u32 (*WASM_RT_ADD_PREFIX(Z_dynCall_iiZ_iii))(u32, u32);
/* export: 'dynCall_iiii' */
u32 (*WASM_RT_ADD_PREFIX(Z_dynCall_iiiiZ_iiiii))(u32, u32, u32, u32);
/* export: 'dynCall_jiji' */
u32 (*WASM_RT_ADD_PREFIX(Z_dynCall_jijiZ_iiiiii))(u32, u32, u32, u32, u32);
/* export: 'dynCall_iidiiii' */
u32 (*WASM_RT_ADD_PREFIX(Z_dynCall_iidiiiiZ_iiidiiii))(u32, u32, f64, u32, u32, u32, u32);
/* export: 'dynCall_vii' */
void (*WASM_RT_ADD_PREFIX(Z_dynCall_viiZ_viii))(u32, u32, u32);

static void init_exports(void) {
  /* export: '__wasm_call_ctors' */
  WASM_RT_ADD_PREFIX(Z___wasm_call_ctorsZ_vv) = (&__wasm_call_ctors);
  /* export: 'sayHello' */
  WASM_RT_ADD_PREFIX(Z_sayHelloZ_vv) = (&sayHello);
  /* export: 'add' */
  WASM_RT_ADD_PREFIX(Z_addZ_ddd) = (&add);
  /* export: 'greet' */
  WASM_RT_ADD_PREFIX(Z_greetZ_ii) = (&greet);
  /* export: 'malloc' */
  WASM_RT_ADD_PREFIX(Z_mallocZ_ii) = (&malloc);
  /* export: '__errno_location' */
  WASM_RT_ADD_PREFIX(Z___errno_locationZ_iv) = (&__errno_location);
  /* export: 'fflush' */
  WASM_RT_ADD_PREFIX(Z_fflushZ_ii) = (&fflush);
  /* export: 'setThrew' */
  WASM_RT_ADD_PREFIX(Z_setThrewZ_vii) = (&setThrew);
  /* export: 'free' */
  WASM_RT_ADD_PREFIX(Z_freeZ_vi) = (&free);
  /* export: '__data_end' */
  WASM_RT_ADD_PREFIX(Z___data_endZ_i) = (&__data_end);
  /* export: 'stackSave' */
  WASM_RT_ADD_PREFIX(Z_stackSaveZ_iv) = (&stackSave);
  /* export: 'stackAlloc' */
  WASM_RT_ADD_PREFIX(Z_stackAllocZ_ii) = (&stackAlloc);
  /* export: 'stackRestore' */
  WASM_RT_ADD_PREFIX(Z_stackRestoreZ_vi) = (&stackRestore);
  /* export: '__growWasmMemory' */
  WASM_RT_ADD_PREFIX(Z___growWasmMemoryZ_ii) = (&__growWasmMemory);
  /* export: 'dynCall_ii' */
  WASM_RT_ADD_PREFIX(Z_dynCall_iiZ_iii) = (&dynCall_ii);
  /* export: 'dynCall_iiii' */
  WASM_RT_ADD_PREFIX(Z_dynCall_iiiiZ_iiiii) = (&dynCall_iiii);
  /* export: 'dynCall_jiji' */
  WASM_RT_ADD_PREFIX(Z_dynCall_jijiZ_iiiiii) = (&dynCall_jiji);
  /* export: 'dynCall_iidiiii' */
  WASM_RT_ADD_PREFIX(Z_dynCall_iidiiiiZ_iiidiiii) = (&dynCall_iidiiii);
  /* export: 'dynCall_vii' */
  WASM_RT_ADD_PREFIX(Z_dynCall_viiZ_viii) = (&dynCall_vii);
}

void WASM_RT_ADD_PREFIX(init)(void) {
  init_func_types();
  init_globals();
  init_memory();
  init_table();
  init_exports();
}
