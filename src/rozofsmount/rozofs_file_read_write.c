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

typedef struct _rozo_buf_cache {

    char *buffer;
    int buf_write_wait;
    int buf_read_wait;
    uint64_t read_pos;  /**< absolute position of the first available byte to read*/
    uint64_t read_from; /**< absolute position of the last available byte to read */
    uint64_t write_pos;  /**< absolute position of the first available byte to read*/
    uint64_t write_from; /**< absolute position of the last available byte to read */
} file_t;


#define CLEAR_WRITE(p) \
{ \
  p->write_pos = 0;\
  p->write_from = 0;\
  p->buf_write_wait = 0;\
}


#define CLEAR_READ(p) \
{ \
  p->read_pos = 0;\
  p->read_from = 0;\
  p->buf_read_wait = 0;\
}

/*
**______________________________________
*/
static inline int read_section_empty(p)
{
   return (p->read_from == p->read_pos)?1;0;
}
/*
**______________________________________
*/
static inline int write_section_empty(p)
{
   return (p->write_from == p->write_pos)?1;0;
}

/*
**______________________________________
*/
statuc inline int check_empty(p)
{
 
 if (read_section_empty(p)!= 0)
 {
   return 0;
 }
 return write_section_empty(p);
}

/*
**______________________________________
*/
int64_t file_write(file_t * f, uint64_t off, const char *buf, uint32_t len) 
{

uint64_t off_requested = off;
uint64_t pos_requested = off+len;
int state;

/*
** check the start point 
*/
  while(1)
  {
    if (!check_empty(p))
    {
      /*
      ** we have either a read or write section, need to check read section first
      */
      if (!read_section_empty(p))
      {
        if (off_requested >= p->read_from) 
        {
           if (off_requested <= p->read_pos)
           {
             state = BUF_READ_INSIDE;  
             break;   
           } 
           state = BUF_READ_AFTER;
           break;    
        }
        /*
        ** the write start off is out side of the cache buffer
        */
        state = BUF_READ_BEFORE;
        break;
      }
      /*
      ** the read section is empty the write section is not empty 
      */
      if (off_requested >= p->write_from)
      {
         /*
         ** check if the start is after the last bytes to the buffer
         */
         if (off_requested > (p->write_pos+1)
         {
           state = BUF_WRITE_AFTER;
           break;
         }
         state = BUF_WRITE_INSIDE;
         break;      
      }
      /*
      ** start is before
      */
      state = BUF_WRITE_BEFORE;
      break;
    }
    /*
    ** the buffer is empty (neither read nor write occurs
    */
    state = BUF_EMPTY;
    break;
  }
  /*
  ** ok, now check the end
  */
  switch (state)
  {
    case BUF_EMPTY:
      action = BUF_ACT_COPY_EMPTY;
      break;
    case BUF_READ_BEFORE:
        action = BUF_ACT_FLUSH_THEN_COPY_NEW;
        break;    

    case BUF_READ_INSIDE:
      /*
      ** the start is inside the write buffer check if the len does not exceed the buffer length
      */
      if ((pos_requested - p->read_from) >= MAX_BUFSIZE)
      {
        action = BUF_ACT_FLUSH_ALIGN_THEN_COPY_NEW;
        break;      
      }
      action = BUF_ACT_COPY;
      break;
      
    case BUF_READ_AFTER:
        action = BUF_ACT_FLUSH_THEN_COPY_NEW;
        break;      
  
    case BUF_WRITE_BEFORE:
        action = BUF_ACT_FLUSH_THEN_COPY_NEW;
        break;  
          
    case BUF_WRITE_INSIDE:
      if ((pos_requested - p->write_from) >= MAX_BUFSIZE)
      {
        action = BUF_ACT_FLUSH_ALIGN_THEN_COPY_NEW;
        break;  
      }  
      action = BUF_ACT_COPY;
      break;
      
    case BUF_WRITE_AFTER:
        action = BUF_ACT_FLUSH_THEN_COPY_NEW;
        break;        
  }
  /*
  ** OK now perform the required action
  */
  switch (action)
  {
    case BUF_ACT_COPY_EMPTY:
      p->write_from = off_requested; 
      p->write_pos  = pos_requested; 
      p->buf_write_wait = 1;
      /*
      ** copy the buffer
      */
      break;

    case BUF_ACT_COPY:
      p->write_from = off_requested; 
      p->write_pos  = pos_requested; 
      p->buf_write_wait = 1;
      /*
      ** copy the buffer
      */
      break;  
  
    case BUF_ACT_FLUSH_THEN_COPY_NEW:
      /*
      ** flush
      */
      p->write_from = off_requested; 
      p->write_pos  = pos_requested; 
      p->buf_write_wait = 1;
      /*
      ** copy the buffer
      */
      break;    
  
    case BUF_ACT_FLUSH_ALIGN_THEN_COPY_NEW:
      /*
      ** flush
      */
      p->write_from = off_requested; 
      p->write_pos  = pos_requested; 
      p->buf_write_wait = 1;
      /*
      ** copy the buffer
      */
      break;      
  
  }
} 



}
