/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation; either version 3 of the License,
 or (at your option) any later version.

 Rozofs is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see
 <http://www.gnu.org/licenses/>.
 */

#ifndef _PROFILE_H
#define _PROFILE_H

#include <sys/time.h>
#include <stdint.h>

#include <rozofs/rpc/spproto.h>
#include <rozofs/rpc/mpproto.h>

#define MICROLONG(time) ((unsigned long long)time.tv_sec * 1000000 + time.tv_usec)

#define DECLARE_PROFILING(the_type) extern the_type gprofiler

#define DEFINE_PROFILING(the_type) the_type gprofiler

#define P_COUNT     0
#define P_ELAPSE    1
#define P_BYTES     2

#define START_PROFILING(the_probe)\
    uint64_t tic, toc;\
    struct timeval tv;\
    {\
        gprofiler.the_probe[P_COUNT]++;\
        gettimeofday(&tv,(struct timezone *)0);\
        tic = MICROLONG(tv);\
    }

#define START_PROFILING_IO(the_probe, the_bytes)\
    uint64_t tic, toc;\
    struct timeval tv;\
    {\
        gprofiler.the_probe[P_COUNT]++;\
        gettimeofday(&tv,(struct timezone *)0);\
        tic = MICROLONG(tv);\
        gprofiler.the_probe[P_BYTES] += the_bytes;\
    }

#define STOP_PROFILING(the_probe)\
    {\
        gettimeofday(&tv,(struct timezone *)0);\
        toc = MICROLONG(tv);\
        gprofiler.the_probe[P_ELAPSE] += (toc - tic);\
    }

#define SET_PROBE_VALUE(the_probe, the_value)\
    {\
        gprofiler.the_probe = the_value;\
    }\

#define CLEAR_PROBE(the_probe)\
    {\
        memset(gprofiler.the_probe, 0, sizeof(gprofiler.the_probe));\
    }



#endif
