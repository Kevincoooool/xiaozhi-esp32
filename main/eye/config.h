
#pragma once
// #include "defaultEye.h"      // Standard human-ish hazel eye -OR-
// #include "data/dragonEye.h"     // Slit pupil fiery dragon/demon eye -OR-
// #include "data/noScleraEye.h"   // Large iris, no sclera -OR-
// #include "data/goatEye.h"       // Horizontal pupil goat/Krampus eye -OR-
// #include "data/newtEye.h"       // Eye of newt -OR-
// #include "data/terminatorEye.h" // Git to da choppah!
// #include "data/catEye.h"        // Cartoonish cat (flat "2D" colors)
// #include "data/owlEye.h"        // Minerva the owl (DISABLE TRACKING)
// #include "data/naugaEye.h"      // Nauga googly eye (DISABLE TRACKING)
// #include "data/doeEye.h"        // Cartoon deer eye (DISABLE TRACKING)
#include "defaultEye.h"
#include "catEye.h"
#include "dragonEye.h"
#include "goatEye.h"
#include "newtEye.h"
#include "terminatorEye.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#define REAL_SCREEN_WIDTH  230
#define REAL_SCREEN_HEIGHT  230

// EYE LIST ----------------------------------------------------------------
#define NUM_EYES 1 // Number of eyes to display (1 or 2)

#define TRACKING      // 启用眼睑跟踪
#define AUTOBLINK    // 启用自动眨眼

// #define NOBLINK 0       // Not currently engaged in a blink
// #define ENBLINK 1       // Eyelid is currently closing
// #define DEBLINK 2       // Eyelid is currently opening

  #define LIGHT_CURVE  0.33 // Light sensor adjustment curve
  #define LIGHT_MIN       0 // Minimum useful reading from light sensor
  #define LIGHT_MAX    1023 // Maximum useful reading from sensor

#define IRIS_SMOOTH         // If enabled, filter input from IRIS_PIN
#if !defined(IRIS_MIN)      // Each eye might have its own MIN/MAX
#define IRIS_MIN       90 // Iris size (0-1023) in brightest light
#endif
#if !defined(IRIS_MAX)
  #define IRIS_MAX      130 // Iris size (0-1023) in darkest light
#endif
