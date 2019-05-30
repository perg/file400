/* begin_generated_IBM_copyright_prolog                              */         
/* This is an automatically generated copyright prolog.              */         
/* After initializing,  DO NOT MODIFY OR MOVE                        */         
/* ----------------------------------------------------------------- */         
/*                                                                   */         
/* Product(s):                                                       */         
/*     5716-CX2                                                      */         
/*     5716-CX4                                                      */         
/*     5716-CX5                                                      */         
/*     5722-SS1                                                      */         
/*     5761-SS1                                                      */         
/*     5770-SS1                                                      */         
/*                                                                   */         
/* (C)Copyright IBM Corp.  1991, 2013                                */         
/*                                                                   */         
/* All rights reserved.                                              */         
/* US Government Users Restricted Rights -                           */         
/* Use, duplication or disclosure restricted                         */         
/* by GSA ADP Schedule Contract with IBM Corp.                       */         
/*                                                                   */         
/* Licensed Materials-Property of IBM                                */         
/*                                                                   */         
/*  ---------------------------------------------------------------  */         
/*                                                                   */         
/* end_generated_IBM_copyright_prolog                                */         
#ifndef   __recio_h                                                             
  #define __recio_h                                                             
                                                                                
#ifdef __cplusplus                                                              
 extern "C" {                                                                   
 #pragma info(none)                                                             
#else                                                                           
 #pragma nomargins nosequence                                                   
 #pragma checkout(suspend)                                                      
#endif                                                                          
                                                                                
/* ================================================================== */        
/*  Header File Name: recio.h                                         */        
/*                                                                    */        
/*  This header contains the declarations used by the record I/O      */        
/*  functions.                                                        */        
/* ================================================================== */        
                                                                                
  #if defined (__EXTENDED__)                                                    
                                                                                
    #ifndef   __string_h                                                        
      #include <string.h>                                                       
    #endif                                                                      
                                                                                
    #ifndef   __stddef_h                                                        
      #include <stddef.h>                                                       
    #endif                                                                      
                                                                                
    #ifndef   __xxfdbk_h                                                        
      #include <xxfdbk.h>                                                       
    #endif                                                                      
                                                                                
    #pragma datamodel(P128)                                                     
                                                                                
    /*   Macros                                                       */        
                                                                                
    #define EOF           (-1)                                                  
    #define _FILENAME_MAX  39                                                   
                                                                                
    /* limited by the amount of storage available                     */        
    #define _ROPEN_MAX     32767                                                
                                                                                
    #define __RRN_EQ       0x08000300                                           
                                                                                
    #define __KEY_EQ       0x0B000100                                           
    #define __KEY_GT       0x0D000100                                           
    #define __KEY_LT       0x09000100                                           
    #define __KEY_LE       0x0A000100                                           
    #define __KEY_GE       0x0C000100                                           
    #define __KEY_NEXTUNQ  0x05000100                                           
    #define __KEY_PREVUNQ  0x06000100                                           
    #define __KEY_NEXTEQ   0x0E000100                                           
    #define __KEY_PREVEQ   0x0F000100                                           
                                                                                
    #define __FIRST        0x01000300                                           
    #define __LAST         0x02000300                                           
    #define __NEXT         0x03000300                                           
    #define __PREVIOUS     0x04000300                                           
                                                                                
    #define __START_FRC    0x03000004                                           
    #define __START        0x01000004                                           
    #define __END_FRC      0x04000004                                           
    #define __END          0x02000004                                           
                                                                                
    #define __NO_LOCK      0x00000001                                           
    #define __DFT          0x0B000100                                           
    #define __NO_POSITION  0x00100000                                           
    #define __PRIOR        0x00001000                                           
    #define __DATA_ONLY    0x00000002                                           
    #define __NULL_KEY_MAP 0x00000008                                           
                                                                                
    #define __READ_NEXT    3                                                    
    #define __READ_PREV    4                                                    
                                                                                
    #define __NOT_NULL_VALUE '0'                                                
    #define __NULL_VALUE     '1'                                                
    #define __MAPPING_ERROR  '2'                                                
                                                                                
    #define __DK_YES       1                                                    
    #define __DK_NO        0                                                    
                                                                                
    extern int       _C2M_MSG;                                                  
                                                                                
                                                                                
    /*  typedefs & structs                                            */        
                                                                                
    typedef struct {               /* Major Minor return code struct  */        
      char                         major_rc[2];                                 
      char                         minor_rc[2];                                 
    } _Maj_Min_rc_T;                                                            
                                                                                
    typedef struct {               /* System specific information     */        
      void                        *sysparm_ext;                                 
      _Maj_Min_rc_T                _Maj_Min;                                    
      char                         reserved1[12];                               
    } _Sys_Struct_T;                                                            
                                                                                
    typedef struct {                                                            
      unsigned char               *key;                                         
      _Sys_Struct_T               *sysparm;                                     
      unsigned long                rrn;                                         
      long                         num_bytes;                                   
      short                        blk_count;                                   
      char                         blk_filled_by;                               
      int                          dup_key   : 1;                               
      int                          icf_locate: 1;                               
      int                          reserved1 : 6;                               
      char                         reserved2[20];                               
    } _RIOFB_T;                                                                 
                                                                                
    typedef _Packed struct {                                                    
      char                         reserved1[16];                               
      volatile void  *const *const in_buf;                                      
      volatile void  *const *const out_buf;                                     
      char                         reserved2[48];                               
      _RIOFB_T                     riofb;                                       
      char                         reserved3[32];                               
      const unsigned int           buf_length;                                  
      char                         reserved4[28];                               
      volatile char  *const        in_null_map;                                 
      volatile char  *const        out_null_map;                                
      volatile char  *const        null_key_map;                                
      char                         reserved5[48];                               
      const int                    min_length;                                  
      short                        null_map_len;                                
      short                        null_key_map_len;                            
      char                         reserved6[8];                                
    } _RFILE;                                                                   
                                                                                
    typedef char _SYSindara[99];                                                
                                                                                
                                                                                
    /*  Function Declarations                                         */        
                                                                                
    _RFILE       *_Ropen     (const  char *, const  char *, ...);               
     int          _Rclose    (_RFILE *);                                        
    _RIOFB_T     *_Rwrite    (_RFILE *, void *, size_t);                        
    _RIOFB_T     *_Rreadk    (_RFILE *, void *, size_t, int,                    
                                        void *, unsigned int);                  
    _RIOFB_T     *_Rreadd    (_RFILE *, void *, size_t, int, long);             
    _RIOFB_T     *_Rwrited   (_RFILE *, void *, size_t, unsigned long);         
    _RIOFB_T     *_Rupdate   (_RFILE *, void *, size_t);                        
    _RIOFB_T     *_Rdelete   (_RFILE *);                                        
    int           _Rrlslck   (_RFILE *);                                        
    _RIOFB_T     *_Rlocate   (_RFILE *, void *, int, int);                      
    _RIOFB_T     *_Rwrread   (_RFILE *, void *, size_t, void *, size_t);        
    _RIOFB_T     *_Rreadnc   (_RFILE *, void *, size_t);                        
    int           _Rfeod     (_RFILE *);                                        
    int           _Rfeov     (_RFILE *);                                        
    _RIOFB_T     *_Rupfb     (_RFILE *);                                        
    void          _Rformat   (_RFILE *, char *);                                
    int           _Rcommit   (char *);                                          
    int           _Rrollbck  (void);                                            
    int           _Racquire  (_RFILE *, char *);                                
    int           _Rrelease  (_RFILE *, char *);                                
    int           _Rpgmdev   (_RFILE *, char *);                                
    void          _Rindara   (_RFILE *, char *);                                
    _RIOFB_T     *_Rreadindv (_RFILE *, void *, size_t, int);                   
    _XXIOFB_T    *_Riofbk    (_RFILE *);                                        
    _XXOPFB_T    *_Ropnfbk   (_RFILE *);                                        
    _XXDEV_ATR_T *_Rdevatr   (_RFILE *, char *);                                
    _RIOFB_T     *__reads    (_RFILE *, void *, size_t, int, char);             
    int           _C_Qry_Null_Map(_RFILE *, int);                               
    int           _C_Qry_Null_Key_Map(_RFILE *, int, int);                      
                                                                                
    _RIOFB_T     *_Rreadn    (_RFILE *, void *, size_t, int);                   
    _RIOFB_T     *_Rreadp    (_RFILE *, void *, size_t, int);                   
    _RIOFB_T     *_Rreadf    (_RFILE *, void *, size_t, int);                   
    _RIOFB_T     *_Rreadl    (_RFILE *, void *, size_t, int);                   
    _RIOFB_T     *_Rreads    (_RFILE *, void *, size_t, int);                   
    _RIOFB_T     *_Rwriterd  (_RFILE *, void *, size_t);                        
                                                                                
#if (defined(__UTF32__) || defined(__CCSID_NEUTRAL__))                          
  #pragma map (_Ropen,    "_C_NEU_DM_ropen")                                    
#endif                                                                          
                                                                                
    #ifndef __cplusplus_nomacro__                                               
      #define _Rreadf(__fp,__buf,__sz,__opt)  \                                 
               (__reads((__fp),(__buf),(__sz),(__opt),0x01))                    
                                                                                
      #define _Rreadl(__fp,__buf,__sz,__opt)  \                                 
               (__reads((__fp),(__buf),(__sz),(__opt),0x02))                    
                                                                                
      #define _Rreadn(__fp,__buf,__sz,__opt)  \                                 
               (__reads((__fp),(__buf),(__sz),(__opt),0x03))                    
                                                                                
      #define _Rreadp(__fp,__buf,__sz,__opt)  \                                 
               (__reads((__fp),(__buf),(__sz),(__opt),0x04))                    
                                                                                
      #define _Rreads(__fp,__buf,__sz,__opt)  \                                 
               (__reads((__fp),(__buf),(__sz),(__opt),0x21))                    
                                                                                
      #define _Rwriterd(__fp,__buf,__sz)      \                                 
               (_Rwrread((__fp),(__buf),(__sz),(__buf),(__sz)))                 
                                                                                
    #endif /* ifndef __cplusplus_nomacro__ */                                   
                                                                                
    #define _CLEAR_NULL_MAP(__file,__type)                                    \ 
           (memset((char *__ptr128)(__file)->out_null_map, __NOT_NULL_VALUE,  \ 
                             sizeof(__type)))                                   
                                                                                
    #define _CLEAR_UPDATE_NULL_MAP(__file,__type)                             \ 
           (memset((char *__ptr128)(__file)->in_null_map, __NOT_NULL_VALUE,   \ 
                             sizeof(__type)) )                                  
                                                                                
    #define _QRY_NULL_MAP(__file,__type)                                      \ 
                 _C_Qry_Null_Map(__file, sizeof(__type))                        
                                                                                
    #define _SET_NULL_MAP_FIELD(__file,__type,__field)                        \ 
           (*((__file)->out_null_map + offsetof(__type,__field)) =            \ 
                                                           __NULL_VALUE)        
                                                                                
    #define _SET_UPDATE_NULL_MAP_FIELD(__file,__type,__field)                 \ 
           (*((__file)->in_null_map + offsetof(__type,__field)) =             \ 
                                                           __NULL_VALUE)        
                                                                                
    #define _QRY_NULL_MAP_FIELD(__file,__type,__field)                        \ 
           (*((__file)->in_null_map + offsetof(__type,__field)) ==            \ 
                                                           __NULL_VALUE)        
                                                                                
    #define _CLEAR_NULL_KEY_MAP(__file,__type)                                \ 
           (memset((char *__ptr128)(__file)->null_key_map, __NOT_NULL_VALUE,  \ 
                         sizeof(__type)))                                       
                                                                                
    #define _SET_NULL_KEY_MAP_FIELD(__file,__type,__field)                    \ 
           (*((__file)->null_key_map + offsetof(__type,__field)) =            \ 
                                                           __NULL_VALUE)        
                                                                                
    #define _QRY_NULL_KEY_MAP(__file,__type)                                  \ 
                 _C_Qry_Null_Key_Map(__file, sizeof(__type), -1)                
                                                                                
    #define _QRY_NULL_KEY_MAP_FIELD(__file,__type,__field)                    \ 
                 _C_Qry_Null_Key_Map(__file, sizeof(__type),                  \ 
                                     offsetof(__type, __field))                 
                                                                                
    #pragma datamodel(pop)                                                      
                                                                                
  #endif  /* ifdef __EXTENDED__                                       */        
                                                                                
#ifdef __cplusplus                                                              
 #pragma info(restore)                                                          
 }                                                                              
#else                                                                           
 #pragma checkout(resume)                                                       
#endif                                                                          
                                                                                
#endif  /* ifndef __recio_h                                           */        
