#pragma once

#if defined(LGFX_SDL)
// Desktop sim — handled by sim_include/LGFX.h
#elif defined(BOARD_FREENOVE_S3)
#include "LGFX_FreenoveS3.h"
#else
#include "LGFX_CYD.h"
#endif
