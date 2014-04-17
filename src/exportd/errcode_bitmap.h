/*
**_____________________________________________________________________
*/
typedef struct rozofs_bitmp_write_attributes_err
{
  uint64_t bad_attribute_size;
  uint64_t bad_file_index;
  uint64_t no_bitmap_context;
  uint64_t open_failure;
  uint64_t write_failure;
} rozofs_bitmp_write_attributes_err_t;

static inline void rozofs_bitmp_write_attributes_err_cpt_clear()
{

  rozofs_bitmp_write_attributes_err_cpt.bad_attribute_size = 0;
  rozofs_bitmp_write_attributes_err_cpt.bad_file_index = 0;
  rozofs_bitmp_write_attributes_err_cpt.no_bitmap_context = 0;
  rozofs_bitmp_write_attributes_err_cpt.open_failure = 0;
  rozofs_bitmp_write_attributes_err_cpt.write_failure = 0;
}
/*
**_____________________________________________________________________
*/

typedef struct rozofs_bitmp_read_attributes_err
{
uint64_t bad_file_index;
uint64_t no_bitmap_context;
uint64_t open_failure;
uint64_t read_failure;
} rozofs_bitmp_read_attributes_err_t

rozofs_bitmp_read_attributes_err_t rozofs_bitmp_read_attributes_err_cpt;

static inline void rozofs_bitmp_read_attributes_err_cpt_clear()
{

  rozofs_bitmp_read_attributes_err_cpt.bad_file_index = 0;
  rozofs_bitmp_read_attributes_err_cpt.no_bitmap_context = 0;
  rozofs_bitmp_read_attributes_err_cpt.open_failure = 0;
  rozofs_bitmp_read_attributes_err_cpt.read_failure = 0;
}

/*
**_____________________________________________________________________
*/

typedef struct rozofs_bitmp_clear_attributes_err
{
uint64_t bad_file_index;
uint64_t no_bitmap_context;
uint64_t open_failure;
uint64_t write_failure;
} rozofs_bitmp_clear_attributes_err_t;

rozofs_bitmp_clear_attributes_err_t rozofs_bitmp_clear_attributes_err_cpt;

static inline void rozofs_bitmp_clear_attributes_err_cpt_clear()
{
  rozofs_bitmp_clear_attributes_err_cpt.bad_file_index=0;
  rozofs_bitmp_clear_attributes_err_cpt.no_bitmap_context=0;
  rozofs_bitmp_clear_attributes_err_cpt.open_failure=0;
  rozofs_bitmp_clear_attributes_err_cpt.write_failure=0;
}

/*
**_____________________________________________________________________
*/
typedef struct rozofs_btmap_cache_insert_err
{
  uint64_t no_file_descriptor;
  uint64_t read_failure;
} rozofs_btmap_cache_insert_err_t;

rozofs_btmap_cache_insert_err_t rozofs_btmap_cache_insert_err_cpt;


static inline void rozofs_btmap_cache_insert_err_cpt_clear()
{
   memset(&rozofs_btmap_cache_insert_err_cpt,0,sizeof(rozofs_btmap_cache_insert_err_cpt));
}

/*
**_____________________________________________________________________
*/

rozofs_btmp_update_bitmap_file_header_err
uint64_t no_file_descriptor;
uint64_t write_failure;

rozofs_btmp_file_alloc_chunk_err
uint64_t no_file_descriptor;
uint64_t write_failure;


rozofs_btmp_file_read_err
uint64_t fstat_failure;
uint64_t write_failure;
uint64_t read_failure;


rozofs_btmp_file_release_inode_err
uint64_t no_file_descriptor;
uint64_t write_failure;
uint64_t out_of_memory;
uint64_t read_failure;



rozofs_btmap_alloc_inode_from_file_err
uint64_t no_file_descriptor;


rozofs_btmap_alloc_inode_err
uint64_t no_bitmap_context;



rozofs_btmap_free_inode_err
uint64_t bad_file_index;
uint64_t no_bitmap_context;


rozofs_bitmap_allocate_bitmap_hdr_err
uint64_t out_of_memory;




