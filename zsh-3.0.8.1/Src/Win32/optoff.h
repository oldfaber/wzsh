/*
 * Compiler-dependant optimization disable. Needed for fork()
 */

#if defined(_MSC_VER)
#pragma optimize("",off)
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 4)
#pragma GCC push_options
#pragma GCC optimize("-O0")
#endif
