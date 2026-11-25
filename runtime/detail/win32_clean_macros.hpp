// Intentionally no #pragma once: subsequent Windows SDK headers (<objbase.h>,
// <shlobj.h>, <wincred.h>, etc.) may re-define legacy Win16/RPC macros that
// collide with user identifiers in generated C++23 code.
#if defined(_WIN32)
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#ifdef NEAR
#undef NEAR
#endif
#ifdef FAR
#undef FAR
#endif
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif
#ifdef small
#undef small
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef Yield
#undef Yield
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef DELETE
#undef DELETE
#endif
#endif
