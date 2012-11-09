/*
 * Compiler-dependant optimization enable. Needed for fork()
 */

#if defined(_MSC_VER)
#pragma optimize("",on)
#endif

#if defined(__GNUC__) && (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 4)
#pragma GCC pop_options
#endif
