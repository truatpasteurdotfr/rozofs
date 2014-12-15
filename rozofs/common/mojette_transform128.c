/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation, version 2.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <rozofs/rozofs.h>
#include <cpuid.h> /* __get_cpuid_max, __get_cpuid */
#include <mmintrin.h> /* MMX instrinsics  __m64 integer type  */
#include <xmmintrin.h> /* SSE  __m128  float */
#include "transform.h"
#include <rozofs/common/log.h>


#define BIN_UPDATE ((k + k_offsets[l])/* * updated->angle.q */+ l * updated->angle.p - offsets[i])
#define BIN_SET ((k + k_offsets[l])/* * p->angle.q */+ l * p->angle.p - offsets[l])
#define SUP_IDX (l * cols + k + k_offsets[l])

/*
**____________________________________________________________________________
*/
static inline void xor128_1(bin_t *bin,__m128 bin_val) 
{
__m128 work0; 
  work0 = _mm_load_ps((const float *)(bin)); 
  work0 = _mm_xor_ps(work0, bin_val); 
  _mm_store_ps((float*)(bin), work0);
}
/*
**____________________________________________________________________________
*/
static inline void xor128_1_ptr(bin_t *bin,pxl_t * support) 
{
__m128 work0; 
__m128 work1; 
  work0 = _mm_load_ps((const float *)(bin)); 
  work1 = _mm_load_ps((const float *)(support)); 
  work0 = _mm_xor_ps(work0, work1); 
  _mm_store_ps((float*)(bin), work0);
}
/*
**____________________________________________________________________________
*/
static inline __m128 write128_1_ret(pxl_t *support,bin_t *bin) 
{
__m128 work0; 
  work0 = _mm_load_ps((const float *)(bin)); 
  _mm_store_ps((float*)(support), work0);
  return work0; 

}
/*
**____________________________________________________________________________
*/
static inline int compare_slope(const void *e1, const void *e2) {
    projection_t *p1 = (projection_t *) e1;
    projection_t *p2 = (projection_t *) e2;
    double a = (double) p1->angle.p / (double) p1->angle.q;
    double b = (double) p2->angle.p / (double) p2->angle.q;
    return (a < b) ? 1 : (a > b) ? -1 : 0;
}

static inline int max(int a, int b) {
    return a > b ? a : b;
}

/*
**____________________________________________________________________________
*/
/**
* Perform a Mojette transform inverse in 128 bits mode to decode a buffer

  @param support: pointer to the decoded buffer
  @param rows: number of rows
  @param cols: number of colunms in the buffer
  @param np: number of projections involved in the inverse procedure
  @param projections: pointer to the projections contexts
  
*/
void transform128_inverse (pxl_t * support, int rows, int cols, int np,
        projection_t * projections) {
    int s_minus, s_plus, s, i, rdv, k, l;
    //double tmp;
    int *k_offsets, *offsets;
    int transform_buf_offset[1024];
    int transform_buf_k_offsets[1024];    
    k_offsets = transform_buf_k_offsets;
    offsets = transform_buf_offset;

    cols = (cols)/2;
    
    qsort((void *) projections, np, sizeof (projection_t), compare_slope_inline);
    for (i = 0; i < np; i++) {
        offsets[i] =
                projections[i].angle.p <
                0 ? (rows - 1) * projections[i].angle.p : 0;
    }

    // compute s_minus, s_plus, and finally s
    s_minus = s_plus = s = 0;
    for (i = 1; i < rows - 1; i++) {
        s_minus += max_inline(0, -projections[i].angle.p);
        s_plus += max_inline(0, projections[i].angle.p);
    }
    s = s_minus - s_plus;

    // compute the rendez-vous row rdv
    rdv = rows - 1;

    // Determine the initial image column offset for each projection
    k_offsets[rdv] =
            max_inline(max_inline(0, -projections[rdv].angle.p) + s_minus,
            max_inline(0, projections[rdv].angle.p) + s_plus);
    for (i = rdv + 1; i < rows; i++) {
        k_offsets[i] = k_offsets[i - 1] + projections[i - 1].angle.p;
    }
    for (i = rdv - 1; i >= 0; i--) {
        k_offsets[i] = k_offsets[i + 1] + projections[i + 1].angle.p;
    }

    // Reconstruct
    // While all projections aren't needed (avoid if statement in general case)
    for (k = -max_inline(k_offsets[0], k_offsets[rows - 1]); k < 0; k++) {
        for (l = 0; l <= rdv; l++) {
            if (k + k_offsets[l] >= 0) {
                projection_t *p = projections + l;
                __m128 bin = write128_1_ret(&support[2*SUP_IDX],&projections[l].bins[2*BIN_SET]);
                for (i = 0; i < rows; i++) {
		    if (i==l) continue;
                    projection_t *updated = projections + i;
                    xor128_1(&updated->bins[2*BIN_UPDATE],bin);
                }
            }
        }
    }
    // scan the reconstruction path while every projections are used
    for (k = 0; k < cols - max_inline(k_offsets[0], k_offsets[rows - 1]); k++) {
        for (l = 0; l <= rdv; l++) {
            projection_t *p = projections + l;
            __m128 bin = write128_1_ret(&support[2*SUP_IDX],&projections[l].bins[2*BIN_SET]);
            for (i = 0; i < rows; i++) {
		if (i==l) continue;
                projection_t *updated = projections + i;
                xor128_1(&updated->bins[2*BIN_UPDATE],bin);
            }
        }
    }
    // finish the work
    for (k = cols - max_inline(k_offsets[0], k_offsets[rows - 1]); k < cols; k++) {
        for (l = 0; l <= rdv; l++) {
            if (k + k_offsets[l] < cols) {
                projection_t *p = projections + l;
                __m128 bin = write128_1_ret(&support[2*SUP_IDX],&projections[l].bins[2*BIN_SET]);
                for (i = 0; i < rows; i++) {
		    if (i==l) continue;
                    projection_t *updated = projections + i;
                    xor128_1(&updated->bins[2*BIN_UPDATE],bin);
                }
            }
        }
    }
}

void transform128_inverse_copy (pxl_t * support, int rows, int cols, int np,
        projection_t * projections,int max_prj_sz_intf) {
    int s_minus, s_plus, s, i, rdv, k, l;
    //double tmp;
    int *k_offsets, *offsets;
    int transform_buf_offset[1024];
    int transform_buf_k_offsets[1024];    
    k_offsets = transform_buf_k_offsets;
    offsets = transform_buf_offset;
        
    char buff_all_bins[1024*18];
    int bufsz = 0;
    int max_prj_sz;
    
    max_prj_sz = max_prj_sz_intf+512/*+256*/;
    char *buff_all_bins_p;

    cols = (cols)/2;
    
    buff_all_bins_p = buff_all_bins;
    buff_all_bins_p +=64;
    uint64_t aligned128 = (uint64_t)(buff_all_bins_p);
    aligned128 = ((aligned128>>5)<<5);
    buff_all_bins_p = (char*)aligned128;
    /*
    ** copy the projection in the local buffer: to avoid corruption of the next
    ** projection in sequence
    */
    for (i = 0; i < np; i++) {
	char *src = (char *) projections[i].bins;
	char *dst = buff_all_bins_p;
        memcpy(dst,src,projections[i].size*16);
	projections[i].bins = (bin_t *)buff_all_bins_p;
	buff_all_bins_p += max_prj_sz;
	bufsz += max_prj_sz;

    }    
    /*
    ** sort the projection in the increasing order of angle p
    */ 
    qsort((void *) projections, np, sizeof (projection_t), compare_slope_inline);
    for (i = 0; i < np; i++) {
        offsets[i] =
                projections[i].angle.p <
                0 ? (rows - 1) * projections[i].angle.p : 0;

    }
    
    // compute s_minus, s_plus, and finally s
    s_minus = s_plus = s = 0;
    for (i = 1; i < rows - 1; i++) {
        s_minus += max_inline(0, -projections[i].angle.p);
        s_plus += max_inline(0, projections[i].angle.p);
    }
    s = s_minus - s_plus;

    // compute the rendez-vous row rdv
    rdv = rows - 1;

    // Determine the initial image column offset for each projection
    k_offsets[rdv] =
            max_inline(max_inline(0, -projections[rdv].angle.p) + s_minus,
            max_inline(0, projections[rdv].angle.p) + s_plus);
    for (i = rdv + 1; i < rows; i++) {
        k_offsets[i] = k_offsets[i - 1] + projections[i - 1].angle.p;
    }
    for (i = rdv - 1; i >= 0; i--) {
        k_offsets[i] = k_offsets[i + 1] + projections[i + 1].angle.p;
    }

    // Reconstruct
    // While all projections aren't needed (avoid if statement in general case)
    for (k = -max_inline(k_offsets[0], k_offsets[rows - 1]); k < 0; k++) {
        for (l = 0; l <= rdv; l++) {
            if (k + k_offsets[l] >= 0) {
                projection_t *p = projections + l;
                __m128 bin = write128_1_ret(&support[2*SUP_IDX],&projections[l].bins[2*BIN_SET]);
                for (i = 0; i < rows; i++) {
		    if (i==l) continue;
                    projection_t *updated = projections + i;
                    xor128_1(&updated->bins[2*BIN_UPDATE],bin);
                }
            }
        }
    }
    // scan the reconstruction path while every projections are used
    for (k = 0; k < cols - max_inline(k_offsets[0], k_offsets[rows - 1]); k++) {
        for (l = 0; l <= rdv; l++) {
            projection_t *p = projections + l;
            __m128 bin = write128_1_ret(&support[2*SUP_IDX],&projections[l].bins[2*BIN_SET]);
            for (i = 0; i < rows; i++) {
		if (i==l) continue;
                projection_t *updated = projections + i;
                xor128_1(&updated->bins[2*BIN_UPDATE],bin);
            }
        }
    }
    // finish the work
    for (k = cols - max_inline(k_offsets[0], k_offsets[rows - 1]); k < cols; k++) {
        for (l = 0; l <= rdv; l++) {
            if (k + k_offsets[l] < cols) {
                projection_t *p = projections + l;
                __m128 bin = write128_1_ret(&support[2*SUP_IDX],&projections[l].bins[2*BIN_SET]);
                for (i = 0; i < rows; i++) {
		    if (i==l) continue;
                    projection_t *updated = projections + i;
                    xor128_1(&updated->bins[2*BIN_UPDATE],bin);
                }
            }
        }
    }
}


/*
**____________________________________________________________________________
*/
/**
*   perform a Mojette forward transform on 128 bits. The input parameters are 
    still the one computed for the 64 bits case
    
    @param support: pointer to the buffer to encode
    @param rows: numbers of rows in which the buffer is divided
    @param cols: numbers of colunms
    @param np: number of projections to generate
    @param projections: projection contexts
*/
void transform128_forward(bin_t * support, int rows, int cols, int np,
        projection_t * projections) {
    int *offsets;
    int i, l, k;
    int transform_buf_offset[1024];
    
    cols = (cols)/2;

    offsets = transform_buf_offset;
    for (i = 0; i < np; i++) {
        offsets[i] =
                projections[i].angle.p <
                0 ? (rows - 1) * projections[i].angle.p : 0;
	/*
	** always add 3 extra bins to avoid issue related to the usage 128 bits bins
	*/
        memset(projections[i].bins, 0, (projections[i].size) * 2*sizeof (bin_t));
    }
    for (i = 0; i < np; i++) {
        projection_t *p = projections + i;
        pxl_t *ppix = support;
        pxl_t *ppix_ref = support;
        int row_size = cols*2;
        int support_idx = 0;
        bin_t *pbin; // = p->bins - offsets[i];
	int last_pbin_idx = p->size*2;

        for (l = 0; l < rows; l++) {
            pbin = p->bins + 2*(l * p->angle.p - offsets[i]);
            support_idx = 2*(l * p->angle.p - offsets[i]);
	    int loop = last_pbin_idx - support_idx;
            loop = loop/2;
	    if (loop > cols) loop=cols;
            for (k = loop / 8; k > 0; k--) {
                xor128_1_ptr(&pbin[0],&ppix[2*0]);
                xor128_1_ptr(&pbin[2],&ppix[2*1]);
                xor128_1_ptr(&pbin[4],&ppix[2*2]);
                xor128_1_ptr(&pbin[6],&ppix[2*3]);
                xor128_1_ptr(&pbin[8],&ppix[2*4]);
                xor128_1_ptr(&pbin[10],&ppix[2*5]);
                xor128_1_ptr(&pbin[12],&ppix[2*6]);
                xor128_1_ptr(&pbin[14],&ppix[2*7]);
//		support_idx +=8*2;
                pbin += 8*2;
                ppix += 8*2;
            }
            for (k = loop % 8; k > 0; k--) {
                xor128_1_ptr(&pbin[0],&ppix[2*0]);
//		support_idx +=1*2;
                pbin += 1*2;
                ppix += 1*2;
            }
	    ppix = ppix_ref+(l+1)*row_size;
        }
    }            
}


void transform128_forward_one_proj(bin_t * support, int rows, int cols,
        uint8_t proj_id, projection_t * projections) {
    int offset;
    int l, k;
    cols = (cols)/2;

    offset = projections[proj_id].angle.p < 0 ? (rows - 1) *
            projections[proj_id].angle.p : 0;
     
     memset(projections[proj_id].bins, 0, (projections[proj_id].size) * 2*sizeof (bin_t));


    projection_t *p = projections + proj_id;
    pxl_t *ppix = support;
    int support_idx = 0;
    bin_t *pbin;
    for (l = 0; l < rows; l++) {
        pbin = p->bins + 2*(l * p->angle.p - offset);
        for (k = cols / 8; k > 0; k--) {
           xor128_1_ptr(&pbin[0],&ppix[2*0]);
           xor128_1_ptr(&pbin[2],&ppix[2*1]);
           xor128_1_ptr(&pbin[4],&ppix[2*2]);
           xor128_1_ptr(&pbin[6],&ppix[2*3]);
           xor128_1_ptr(&pbin[8],&ppix[2*4]);
           xor128_1_ptr(&pbin[10],&ppix[2*5]);
           xor128_1_ptr(&pbin[12],&ppix[2*6]);
           xor128_1_ptr(&pbin[14],&ppix[2*7]);
	   support_idx +=8*2;
           pbin += 8*2;
           ppix += 8*2;
        }
    }
}

