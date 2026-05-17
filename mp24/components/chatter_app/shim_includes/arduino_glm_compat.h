/*
 * mp24/components/chatter_app/shim_includes/arduino_glm_compat.h
 *
 * Force-included AFTER -include Arduino.h so that any Arduino /
 * Arduino-esp32 preprocessor macros that collide with glm function
 * names are undefined before any TU has a chance to include a glm
 * header (directly via <glm/...> or via our <glm.h> shim).
 *
 * Why this is needed
 * ------------------
 * Arduino.h transitively pulls WMath.h which defines symbols like
 *
 *     #define radians(deg) ((deg) * DEG_TO_RAD)
 *     #define degrees(rad) ((rad) * RAD_TO_DEG)
 *     #define sq(x)        ((x) * (x))
 *     #define constrain(x, low, high) ...
 *     #define round(x)     ...
 *     #define bit(b)       ...
 *
 * as preprocessor MACROS, not functions. glm declares free
 * functions in the glm:: namespace with the same names:
 *
 *     template<length_t L, typename T, qualifier Q>
 *     vec<L, T, Q> radians(vec<L, T, Q> const& degrees);
 *     template<length_t L, typename T, qualifier Q>
 *     vec<L, T, Q> degrees(vec<L, T, Q> const& radians);
 *
 * Once the macro is in scope, the C preprocessor textually rewrites
 * the glm declaration and the build explodes with errors like
 * "macro 'radians' passed 3 arguments, but takes just 1".
 *
 * Strategy
 * --------
 * #undef the conflicting macros HERE, after Arduino.h has already
 * been pre-included. Subsequent <glm/...> parses cleanly because
 * the macros are no longer defined.
 *
 * What is preserved
 * -----------------
 * `abs` is intentionally NOT undef'd: three callsites in upstream
 * code (src/JigHWTest/JigHWTest.cpp:187, src/Games/Pong/GameState.cpp
 * x2) use Arduino's `abs(...)` macro form. Keeping it defined is
 * cheaper than patching three call sites.
 *
 * `min`, `max` are NOT undef'd either: heavily used in upstream
 * code as Arduino macros and glm never declares those at namespace
 * scope (it spells them `glm::min` / `glm::max` and member access
 * via `.` doesn't hit a macro).
 *
 * Added at S-MP20/3 to unblock S-MP20/2 (first glm TU in SRCS).
 */
#ifndef MP24_SHIM_ARDUINO_GLM_COMPAT_H
#define MP24_SHIM_ARDUINO_GLM_COMPAT_H

#ifdef radians
#undef radians
#endif
#ifdef degrees
#undef degrees
#endif
#ifdef sq
#undef sq
#endif
#ifdef constrain
#undef constrain
#endif
#ifdef round
#undef round
#endif
#ifdef bit
#undef bit
#endif

#endif /* MP24_SHIM_ARDUINO_GLM_COMPAT_H */
