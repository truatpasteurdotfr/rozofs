

#define ROZOFS_STORCLI_TIMER_BUCKET 3
typedef struct _rozofs_storcli_read_clk_t
{
  uint32_t        bucket_cur;
  ruc_obj_desc_t  bucket[ROZOFS_STORCLI_TIMER_BUCKET];  /**< link list of the context waiting on timer */
} rozofs_storcli_read_clk_t


rozofs_storcli_read_clk_t  rozofs_storcli_read_clk;

/*
**____________________________________________________
*/
/**
* start the read guard timer: must be called upon the reception of the first projection

  @param p: read main context
  
 @retval none
*/
void rozofs_storcli_start_read_guard_timer(rozofs_storcli_ctx_t  *p)
{

   ruc_objRemove((ruc_obj_desc_t*) p);
   ruc_objInsertTail((ruc_obj_desc_t*)rozofs_storcli_read_clk.bucket[rozofs_storcli_read_clk.bucket_cur],
                    (ruc_obj_desc_t*) p);
   

}
/*
**____________________________________________________
*/
/**
* stop the read guard timer

  @param p: read main context
  
 @retval none
*/
void rozofs_storcli_stop_read_guard_timer(rozofs_storcli_ctx_t  *p)
{
   ruc_objRemove((ruc_obj_desc_t*) p);
}

/*
**____________________________________________________
*/
/*
  Periodic timer expiration
  
   @param param: Not significant
*/
void rozofs_storcli_periodic_ticker(void * param) 
{
   ruc_obj_desc_t   *bucket_head_p;
   rozofs_storcli_ctx_t   *read_ctx_p;
   
   bucket_idx = rozofs_storcli_read_clk.bucket_cur;
   bucket_idx = (bucket_idx+1)%ROZOFS_STORCLI_TIMER_BUCKET;
   bucket_head_p = &rozofs_storcli_read_clk.bucket[bucket_idx];
   rozofs_storcli_read_clk.bucket_cur = bucket_idx;


    while  ((read_ctx_p =(rozofs_storcli_ctx_t *)ruc_objGetFirst(bucket_head_p)) !=NULL) 
    {
       ruc_objRemove((ruc_obj_desc_t*) read_ctx_p);
       rozofs_storcli_read_timeout(read_ctx_p);    
    }
           
}
/*
**____________________________________________________
*/
/*
  start a periodic timer to chech wether the export LBG is down
  When the export is restarted its port may change, and so
  the previous configuration of the LBG is not valid any more
*/
void rozofs_storcli_start_timer() {
  struct timer_cell * periodic_timer;

  periodic_timer = ruc_timer_alloc(0,0);
  if (periodic_timer == NULL) {
    severe("no timer");
    return;
  }
  ruc_periodic_timer_start (periodic_timer, 
                            100,
 	                        rozofs_storcli_periodic_ticker,
 			                0);

}
