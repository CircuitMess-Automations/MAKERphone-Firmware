/*
 * mp24/components/chatter_app/shim_includes/glm.h
 *
 * Arduino-glm compatibility shim. The upstream Chatter firmware
 * was written against the Arduino-GLM library which exposed the
 * full OpenGL Mathematics surface through a single top-level
 * <glm.h> header. Mainline GLM (g-truc/glm, vendored alongside
 * this file under glm/) instead expects <glm/glm.hpp>.
 *
 * Sources in src/Games/ include both styles:
 *   #include <glm.h>                          (Arduino-glm style)
 *   #include <glm/vec2.hpp>                   (mainline GLM)
 *   #include <glm/ext.hpp>
 *   #include <glm/gtx/matrix_transform_2d.hpp>
 *   #include <glm/gtx/vector_angle.hpp>
 *
 * Both styles must resolve. This file provides the first by
 * forwarding to mainline GLM's master header.
 *
 * Vendored at S-MP20/1.
 */
#ifndef MP24_SHIM_GLM_H
#define MP24_SHIM_GLM_H

#include <glm/glm.hpp>
#include <glm/ext.hpp>

#endif /* MP24_SHIM_GLM_H */
