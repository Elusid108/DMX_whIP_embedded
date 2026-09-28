#pragma once

#if defined(BOARD_PROFILE_MATRIX)
#include "board_matrix.h"
#elif defined(BOARD_PROFILE_C5)
#include "board_c5.h"
#elif defined(BOARD_PROFILE_XIAO_C5)
#include "board_xiao_c5.h"
#elif defined(BOARD_PROFILE_XIAO_S3)
#include "board_xiao_s3.h"
#elif defined(BOARD_PROFILE_XIAO_C3)
#include "board_xiao_c3.h"
#elif defined(BOARD_PROFILE_XIAO_C6)
#include "board_xiao_c6.h"
#else
#error Define BOARD_PROFILE_* (see README Adding a board)
#endif
