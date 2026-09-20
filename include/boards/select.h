#pragma once

#if defined(BOARD_PROFILE_MATRIX)
#include "board_matrix.h"
#elif defined(BOARD_PROFILE_C5)
#include "board_c5.h"
#else
#error Define BOARD_PROFILE_* (see README Adding a board)
#endif
