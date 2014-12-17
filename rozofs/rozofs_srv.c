/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation; either version 2 of the License,
 or (at your option) any later version.

 Rozofs is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see
 <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <string.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include "rozofs.h"
#include "rozofs_srv.h"

/**
*  Layout for bins of 128bits
*/
static int prj_sz_2048_layout0_128bits[] = {  64, 64, 64};
static int prj_sz_4096_layout0_128bits[] = {  128, 128, 128};
static int prj_sz_8192_layout0_128bits[] = {  256, 256, 256};

static int prj_sz_2048_layout1_128bits[] = {  32, 34, 34, 32, 33, 32};
static int prj_sz_4096_layout1_128bits[] = {  64, 66, 66, 64, 65, 64};
static int prj_sz_8192_layout1_128bits[] = {  128, 130, 130, 128, 129, 128};

static int prj_sz_2048_layout2_128bits[] = {  16, 21, 24, 25, 24, 21, 16, 20, 22, 22, 20, 16};
static int prj_sz_4096_layout2_128bits[] = {  32, 37, 40, 41, 40, 37, 32, 36, 38, 38, 36, 32};
static int prj_sz_8192_layout2_128bits[] = {  64, 69, 72, 73, 72, 69, 64, 68, 70, 70, 68, 64};

static int *layout0_128bits_tb[] =
{
  prj_sz_2048_layout0_128bits,
  prj_sz_4096_layout0_128bits,
  prj_sz_8192_layout0_128bits,
  
};

static int *layout1_128bits_tb[] =
{
  prj_sz_2048_layout1_128bits,
  prj_sz_4096_layout1_128bits,
  prj_sz_8192_layout1_128bits,
  
};

static int *layout2_128bits_tb[] =
{
  prj_sz_2048_layout2_128bits,
  prj_sz_4096_layout2_128bits,
  prj_sz_8192_layout2_128bits,
  
};
rozofs_conf_layout_t rozofs_conf_layout_table[LAYOUT_MAX]={{0}};

void rozofs_layout_initialize() {
    int i;
    uint8_t layout;
    rozofs_conf_layout_t *p;
    uint32_t bsize;
    float sum;
    float sum_128;
    int **local_tb = layout0_128bits_tb;
    int *cur_prj_sz_tb = NULL;

    p = rozofs_conf_layout_table;
    memset(p, 0, sizeof (rozofs_conf_layout_t) * LAYOUT_MAX);
    for (layout = 0; layout < LAYOUT_MAX; layout++, p++) {
        switch (layout) {
            case LAYOUT_2_3_4:
                p->rozofs_safe = 4;
                p->rozofs_forward = 3;
                p->rozofs_inverse = 2;
                local_tb = layout0_128bits_tb;
                break;
            case LAYOUT_4_6_8:
                p->rozofs_safe = 8;
                p->rozofs_forward = 6;
                p->rozofs_inverse = 4;
                local_tb = layout1_128bits_tb;
                break;
            case LAYOUT_8_12_16:
                p->rozofs_safe = 16;
                p->rozofs_forward = 12;
                p->rozofs_inverse = 8;
                local_tb = layout2_128bits_tb;
                break;
            default:
	        fatal("Unexpected layout %d",layout);
                break;
        }

        DEBUG("initialize rozofs with inverse: %u, forward: %u, safe: %u",
                p->rozofs_inverse, p->rozofs_forward, p->rozofs_safe);

        /* Compute angles */
        p->rozofs_angles = xmalloc(sizeof (angle_t) * p->rozofs_forward);
        for (i = 0; i < p->rozofs_forward; i++) {
            p->rozofs_angles[i].p = i - p->rozofs_forward / 2;
            p->rozofs_angles[i].q = 1; 
	}

        /* Compute block sizes */
	for (bsize=ROZOFS_BSIZE_MIN; bsize<=ROZOFS_BSIZE_MAX; bsize++) {
	
            p->sizes[bsize].rozofs_psizes     = xmalloc(sizeof (uint16_t) * p->rozofs_forward);
            p->sizes[bsize].rozofs_128bits_psizes     = xmalloc(sizeof (uint16_t) * p->rozofs_forward);
            p->sizes[bsize].rozofs_eff_psizes = xmalloc(sizeof (uint16_t) * p->rozofs_forward);
	    
	    sum = 0;
	    sum_128 = 0;
	    /*
	    ** need to index at bsize+1 since the Optimized Mojette starts at 2048 bytes
	    */
            cur_prj_sz_tb = local_tb[bsize+1];
            for (i = 0; i < p->rozofs_forward; i++) {	    
        	p->sizes[bsize].rozofs_psizes[i] = abs(i - p->rozofs_forward / 2) * (p->rozofs_inverse - 1)
                	+ (ROZOFS_BSIZE_BYTES(bsize) / sizeof (pxl_t) / p->rozofs_inverse - 1) + 1;

        	p->sizes[bsize].rozofs_128bits_psizes[i] = abs(i - p->rozofs_forward / 2) * (p->rozofs_inverse - 1)
                	+ (ROZOFS_BSIZE_BYTES(bsize) / (2*sizeof (pxl_t)) / p->rozofs_inverse - 1) + 1;
			
        	if (p->sizes[bsize].rozofs_128bits_psizes[i] > p->sizes[bsize].rozofs_128bits_psizes_max) {
		    p->sizes[bsize].rozofs_128bits_psizes_max = p->sizes[bsize].rozofs_128bits_psizes[i];
		}
		/*
		** make sure that the total size is modulo 128 bits, otherwise need adjustment
		*/
		int modulo = (p->sizes[bsize].rozofs_psizes[i]*sizeof (pxl_t))%(2*sizeof(uint64_t));
		if (modulo != 0) p->sizes[bsize].rozofs_psizes[i]+=1;
        	if (p->sizes[bsize].rozofs_psizes[i] > p->sizes[bsize].rozofs_psizes_max) {
		    p->sizes[bsize].rozofs_psizes_max = p->sizes[bsize].rozofs_psizes[i];
		} 
		sum += (p->sizes[bsize].rozofs_psizes[i] * 8
		       +sizeof(rozofs_stor_bins_footer_t)
		       +sizeof(rozofs_stor_bins_hdr_t));
		/*
		** compute the effective size
		*/
		if (bsize > ROZOFS_BSIZE_8K)
		{
		  /*
		  ** value greater than 8K are not supported with optimized Mojette
		  */
		  p->sizes[bsize].rozofs_eff_psizes[i] = 0;
		  p->sizes[bsize].rozofs_eff_psizes_max = 0;
		  continue;		
		}

		p->sizes[bsize].rozofs_eff_psizes[i] = cur_prj_sz_tb[i];
                if (p->sizes[bsize].rozofs_eff_psizes[i] > p->sizes[bsize].rozofs_eff_psizes_max) 
		    p->sizes[bsize].rozofs_eff_psizes_max = p->sizes[bsize].rozofs_eff_psizes[i];
	       	sum_128 += (p->sizes[bsize].rozofs_eff_psizes[i] * (8*2)
		           +sizeof(rozofs_stor_bins_footer_t)
		           +sizeof(rozofs_stor_bins_hdr_t));
	    }
	    p->sizes[bsize].redundancyCoeff_128 = sum_128 ;
	    p->sizes[bsize].redundancyCoeff_128 /= ROZOFS_BSIZE_BYTES(bsize);	    
	    p->sizes[bsize].redundancyCoeff = sum ;
	    p->sizes[bsize].redundancyCoeff /= ROZOFS_BSIZE_BYTES(bsize);
        }
    }
}

void rozofs_layout_release() {
    uint8_t layout;
    rozofs_conf_layout_t *p;
    uint32_t bsize;

    p = rozofs_conf_layout_table;

    for (layout = 0; layout < LAYOUT_MAX; layout++, p++) {

        if (p->rozofs_angles) {
            free(p->rozofs_angles);
	    p->rozofs_angles = NULL;
	}    
	for (bsize=ROZOFS_BSIZE_MIN; bsize<=ROZOFS_BSIZE_MAX; bsize++) {
            if (p->sizes[bsize].rozofs_psizes) {
	        free(p->sizes[bsize].rozofs_psizes);
		p->sizes[bsize].rozofs_psizes = NULL;
	    }    
            if (p->sizes[bsize].rozofs_eff_psizes) {
	        free(p->sizes[bsize].rozofs_eff_psizes);
		p->sizes[bsize].rozofs_eff_psizes = NULL;
	    }   
	}
    }
}
