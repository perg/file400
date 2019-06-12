/*
 * file400  AS/400 Database(file) access
 *
 *--------------------------------------------------------------------
 * Copyright (c) 2019 by Per Gummedal.
 *
 * per.gummedal@gmail.com
 *
 * By obtaining, using, and/or copying this software and/or its
 * associated documentation, you agree that you have read, understood,
 * and will comply with the following terms and conditions:
 *
 * Permission to use, copy, modify, and distribute this software and its
 * associated documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appears in all
 * copies, and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the author
 * not to be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission.
 *
 * THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *------------------------------------------------------------------------
 */

#include "Python.h"
#include <signal.h>
#include <iconv.h>
#include <float.h>
#include "as400_types.h"
#include "as400_protos.h"

/* Open modes */
#define OPEN_READ 10
#define OPEN_UPDATE 12
#define OPEN_WRITE 14
/* output */
#define LIST 0
#define OBJ 1
#define DICT 2

static char reclevacc_lib[11] = "";

static long int convccsid_array[30];
static iconv_t convdesc_array[30];
static int conv_size = 0;

extern PyTypeObject File400_Type;

#define File400Object_Check(v) ((v)->ob_type == &File400_Type)
#define PyClass_Check(obj) PyObject_IsInstance(obj, (PyObject *)&PyType_Type)

static PyObject *file400Error;
static PyObject *fileRowClass;

static result_type_t result_type = RESULT_INT32;

static unsigned long long actmark = 0;


/* File info */
typedef struct {
	char     name[11];
	char     lib[11];
	int      omode;
	int      recLen;
	int      keyLen;
	int      fieldCount;
	int      keyCount;
} FileInfo;

/* Field information structure */
typedef struct {
	char name[11];
	char desc[51];
	unsigned short type;
	int  offset;
	int  len;
	int  digits;
	int  dec;
	int  ccsid;
	int  alwnull;
	int  dft;
} fieldInfoStruct;

/* File object type */
typedef struct {
	PyObject_HEAD
	int      fileno;
	FileInfo fi;
	fieldInfoStruct *fieldArr;
	fieldInfoStruct *keyArr;
    PyObject *fieldDict;	/* dictionary over fields */
    PyObject *keyDict;	    /* dictionary over keys */
    char *recbuf;
} File400Object;

#define ROUND_QUAD(x) (((size_t)(x) + 0xf) & ~0xf)

ILEpointer *int2zonedTarget = NULL;
static char int2zonedTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *float2zonedTarget = NULL;
static char float2zonedTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *int2packedTarget = NULL;
static char int2packedTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *float2packedTarget = NULL;
static char float2packedTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileNewTarget = NULL;
static char fileNewTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileInitTarget = NULL;
static char fileInitTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileOpenTarget = NULL;
static char fileOpenTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileCloseTarget = NULL;
static char fileCloseTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileClearTarget = NULL;
static char fileClearTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileFreeTarget = NULL;
static char fileFreeTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileRlsLockTarget = NULL;
static char fileRlsLockTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *filePosbTarget = NULL;
static char filePosbTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *filePosaTarget = NULL;
static char filePosaTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *filePosfTarget = NULL;
static char filePosfTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *filePoslTarget = NULL;
static char filePoslTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileReadeqTarget = NULL;
static char fileReadeqTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileReadrrnTarget = NULL;
static char fileReadrrnTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileReadfTarget = NULL;
static char fileReadfTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileReadlTarget = NULL;
static char fileReadlTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileReadnTarget = NULL;
static char fileReadnTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileReadpTarget = NULL;
static char fileReadpTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileReadneTarget = NULL;
static char fileReadneTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileReadpeTarget = NULL;
static char fileReadpeTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileGetDataTarget = NULL;
static char fileGetDataTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileGetStructTarget = NULL;
static char fileGetStructTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileGetFieldsTarget = NULL;
static char fileGetFieldsTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileGetKeyFieldsTarget = NULL;
static char fileGetKeyFieldsTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileWriteTarget = NULL;
static char fileWriteTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileUpdateTarget = NULL;
static char fileUpdateTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileDeleteTarget = NULL;
static char fileDeleteTarget_buf[sizeof(ILEpointer) + 15];
ILEpointer *fileGetRrnTarget = NULL;
static char fileGetRrnTarget_buf[sizeof(ILEpointer) + 15];

static ILEpointer * loadFunction(char *buf, char *name) {
    ILEpointer *target = (ILEpointer*)ROUND_QUAD(buf);
    if (_ILESYMX(target, actmark, name) == -1) {
        fprintf(stderr, "Error loading function %s\n", name);
        abort();
    }
    return target;
}

static void loadSrvpgm() {
    char pgmname[22] = "";
    sprintf(pgmname, "%s/RECLEVACC", reclevacc_lib);
    actmark = _ILELOADX(pgmname, ILELOAD_LIBOBJ);
    if (actmark == -1) {
        fprintf(stderr, "Error loading srvpgm\n");
        abort();
    }
    // Load all functions
    int2zonedTarget = loadFunction(int2zonedTarget_buf, "int2zoned");
    float2zonedTarget = loadFunction(float2zonedTarget_buf, "float2zoned");
    int2packedTarget = loadFunction(int2packedTarget_buf, "int2packed");
    float2packedTarget = loadFunction(float2packedTarget_buf, "float2packed");
    fileNewTarget = loadFunction(fileNewTarget_buf, "fileNew");
    fileInitTarget = loadFunction(fileInitTarget_buf, "fileInit");
    fileOpenTarget = loadFunction(fileOpenTarget_buf, "fileOpen");
    fileCloseTarget = loadFunction(fileCloseTarget_buf, "fileClose");
    fileClearTarget = loadFunction(fileClearTarget_buf, "fileClear");
    fileFreeTarget = loadFunction(fileFreeTarget_buf, "fileFree");
    fileRlsLockTarget = loadFunction(fileRlsLockTarget_buf, "fileRlsLock");
    filePosbTarget = loadFunction(filePosbTarget_buf, "filePosb");
    filePosaTarget = loadFunction(filePosaTarget_buf, "filePosa");
    filePosfTarget = loadFunction(filePosfTarget_buf, "filePosf");
    filePoslTarget = loadFunction(filePoslTarget_buf, "filePosl");
    fileReadeqTarget = loadFunction(fileReadeqTarget_buf, "fileReadeq");
    fileReadrrnTarget = loadFunction(fileReadrrnTarget_buf, "fileReadrrn");
    fileReadfTarget = loadFunction(fileReadfTarget_buf, "fileReadf");
    fileReadlTarget = loadFunction(fileReadlTarget_buf, "fileReadl");
    fileReadnTarget = loadFunction(fileReadnTarget_buf, "fileReadn");
    fileReadpTarget = loadFunction(fileReadpTarget_buf, "fileReadp");
    fileReadneTarget = loadFunction(fileReadneTarget_buf, "fileReadne");
    fileReadpeTarget = loadFunction(fileReadpeTarget_buf, "fileReadpe");
    fileGetDataTarget = loadFunction(fileGetDataTarget_buf, "fileGetData");
    fileGetStructTarget = loadFunction(fileGetStructTarget_buf, "fileGetStruct");
    fileGetFieldsTarget = loadFunction(fileGetFieldsTarget_buf, "fileGetFields");
    fileGetKeyFieldsTarget = loadFunction(fileGetKeyFieldsTarget_buf, "fileGetKeyFields");
    fileWriteTarget = loadFunction(fileWriteTarget_buf, "fileWrite");
    fileUpdateTarget = loadFunction(fileUpdateTarget_buf, "fileUpdate");
    fileDeleteTarget = loadFunction(fileDeleteTarget_buf, "fileDelete");
    fileGetRrnTarget = loadFunction(fileGetRrnTarget_buf, "fileGetRrn");
}

typedef struct
 { ILEarglist_base base; ILEpointer buf; int32 len; int32 dec; int32 value; }
 intconv_St;
static arg_type_t
 intconv_Sign[] = { ARG_MEMPTR, ARG_INT32, ARG_INT32, ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; ILEpointer buf; int32 len; int32 dec; float64 value; }
 floatconv_St;
static arg_type_t
 floatconv_Sign[] = { ARG_MEMPTR, ARG_INT32, ARG_INT32, ARG_FLOAT64, ARG_END };

typedef struct
 { ILEarglist_base base; ILEpointer name; ILEpointer lib; ILEpointer mbr; int32 mode; }
 fileNew_St;
static arg_type_t
 fileNew_Sign[] = { ARG_MEMPTR, ARG_MEMPTR, ARG_MEMPTR, ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; int32 fileno; }
 file_St;
static arg_type_t
 file_Sign[] = { ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; int32 fileno; ILEpointer key; int32 keylen; int32 lock; }
 filePos_St;
static arg_type_t
 filePos_Sign[] = { ARG_INT32, ARG_MEMPTR, ARG_INT32, ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; int32 fileno; ILEpointer recbuf; int32 lock; }
 fileRead_St;
static arg_type_t
 fileRead_Sign[] = { ARG_INT32, ARG_MEMPTR, ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; int32 fileno; ILEpointer recbuf; ILEpointer key; int32 keylen; int32 lock; }
 fileReadeq_St;
static arg_type_t
 fileReadeq_Sign[] = { ARG_INT32, ARG_MEMPTR, ARG_MEMPTR, ARG_INT32, ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; int32 fileno; ILEpointer recbuf; int32 keylen; int32 lock; }
 fileReadne_St;
static arg_type_t
 fileReadne_Sign[] = { ARG_INT32, ARG_MEMPTR, ARG_INT32, ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; int32 fileno; ILEpointer recbuf; int32 rrn; int32 lock; }
 fileReadrrn_St;
static arg_type_t
 fileReadrrn_Sign[] = { ARG_INT32, ARG_MEMPTR, ARG_INT32, ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; int32 fileno; ILEpointer buf; int32 size; }
 fileGetData_St;
static arg_type_t
 fileGetData_Sign[] = { ARG_INT32, ARG_MEMPTR, ARG_INT32, ARG_END };

typedef struct
 { ILEarglist_base base; int32 fileno; ILEpointer buf; }
 fileBuf_St;
static arg_type_t
 fileBuf_Sign[] = { ARG_INT32, ARG_MEMPTR, ARG_END };

static int call_int2zoned(char *buf, int len, int dec, int value)
{
    char ILEarglist_buf[sizeof(intconv_St) + 15];
    if (!actmark) loadSrvpgm();
    intconv_St *ILEarglist = (intconv_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->buf.s.addr = (ulong)buf;
    ILEarglist->len = len;
    ILEarglist->dec = dec;
    ILEarglist->value = value;
    _ILECALL(int2zonedTarget, &ILEarglist->base, intconv_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_float2zoned(char *buf, int len, int dec, double value)
{
    char ILEarglist_buf[sizeof(floatconv_St) + 15];
    if (!actmark) loadSrvpgm();
    floatconv_St *ILEarglist = (floatconv_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->buf.s.addr = (ulong)buf;
    ILEarglist->len = len;
    ILEarglist->dec = dec;
    ILEarglist->value = value;
    _ILECALL(float2zonedTarget, &ILEarglist->base, floatconv_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_int2packed(char *buf, int len, int dec, int value)
{
    char ILEarglist_buf[sizeof(intconv_St) + 15];
    if (!actmark) loadSrvpgm();
    intconv_St *ILEarglist = (intconv_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->buf.s.addr = (ulong)buf;
    ILEarglist->len = len;
    ILEarglist->dec = dec;
    ILEarglist->value = value;
    _ILECALL(int2packedTarget, &ILEarglist->base, intconv_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_float2packed(char *buf, int len, int dec, double value)
{
    char ILEarglist_buf[sizeof(floatconv_St) + 15];
    if (!actmark) loadSrvpgm();
    floatconv_St *ILEarglist = (floatconv_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->buf.s.addr = (ulong)buf;
    ILEarglist->len = len;
    ILEarglist->dec = dec;
    ILEarglist->value = value;
    _ILECALL(float2packedTarget, &ILEarglist->base, floatconv_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileNew(char *name, char *lib, char *mbr, int mode)
{
    char ILEarglist_buf[sizeof(fileNew_St) + 15];
    if (!actmark) loadSrvpgm();
    fileNew_St *ILEarglist = (fileNew_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->name.s.addr = (ulong)name;
    ILEarglist->lib.s.addr = (ulong)lib;
    ILEarglist->mbr.s.addr = (ulong)mbr;
    ILEarglist->mode = mode;
    _ILECALL(fileNewTarget, &ILEarglist->base, fileNew_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileInit(int fileno)
{
    char ILEarglist_buf[sizeof(file_St) + 15];
    if (!actmark) loadSrvpgm();
    file_St *ILEarglist = (file_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    _ILECALL(fileInitTarget, &ILEarglist->base, file_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileOpen(int fileno)
{
    char ILEarglist_buf[sizeof(file_St) + 15];
    if (!actmark) loadSrvpgm();
    file_St *ILEarglist = (file_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    _ILECALL(fileOpenTarget, &ILEarglist->base, file_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileClose(int fileno)
{
    char ILEarglist_buf[sizeof(file_St) + 15];
    if (!actmark) loadSrvpgm();
    file_St *ILEarglist = (file_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    _ILECALL(fileCloseTarget, &ILEarglist->base, file_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileClear(int fileno)
{
    char ILEarglist_buf[sizeof(file_St) + 15];
    if (!actmark) loadSrvpgm();
    file_St *ILEarglist = (file_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    _ILECALL(fileClearTarget, &ILEarglist->base, file_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileFree(int fileno)
{
    char ILEarglist_buf[sizeof(file_St) + 15];
    if (!actmark) loadSrvpgm();
    file_St *ILEarglist = (file_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    _ILECALL(fileFreeTarget, &ILEarglist->base, file_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileRlsLock(int fileno)
{
    char ILEarglist_buf[sizeof(file_St) + 15];
    if (!actmark) loadSrvpgm();
    file_St *ILEarglist = (file_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    _ILECALL(fileRlsLockTarget, &ILEarglist->base, file_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_filePosb(int fileno, char *key, int keylen, int lock)
{
    char ILEarglist_buf[sizeof(filePos_St) + 15];
    if (!actmark) loadSrvpgm();
    filePos_St *ILEarglist = (filePos_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->key.s.addr = (ulong)key;
    ILEarglist->keylen = keylen;
    ILEarglist->lock = lock;
    _ILECALL(filePosbTarget, &ILEarglist->base, filePos_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_filePosa(int fileno, char *key, int keylen, int lock)
{
    char ILEarglist_buf[sizeof(filePos_St) + 15];
    if (!actmark) loadSrvpgm();
    filePos_St *ILEarglist = (filePos_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->key.s.addr = (ulong)key;
    ILEarglist->keylen = keylen;
    ILEarglist->lock = lock;
    _ILECALL(filePosaTarget, &ILEarglist->base, filePos_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_filePosf(int fileno, int lock)
{
    char ILEarglist_buf[sizeof(fileRead_St) + 15];
    if (!actmark) loadSrvpgm();
    fileRead_St *ILEarglist = (fileRead_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->lock = lock;
    _ILECALL(filePosfTarget, &ILEarglist->base, fileRead_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_filePosl(int fileno, int lock)
{
    char ILEarglist_buf[sizeof(fileRead_St) + 15];
    if (!actmark) loadSrvpgm();
    fileRead_St *ILEarglist = (fileRead_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->lock = lock;
    _ILECALL(filePoslTarget, &ILEarglist->base, fileRead_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileReadeq(int fileno, char *recbuf, char *key, int keylen, int lock)
{
    char ILEarglist_buf[sizeof(fileReadeq_St) + 15];
    if (!actmark) loadSrvpgm();
    fileReadeq_St *ILEarglist = (fileReadeq_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->recbuf.s.addr = (ulong)recbuf;
    ILEarglist->key.s.addr = (ulong)key;
    ILEarglist->keylen = keylen;
    ILEarglist->lock = lock;
    _ILECALL(fileReadeqTarget, &ILEarglist->base, fileReadeq_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileReadrrn(int fileno, char *recbuf, int rrn, int lock)
{
    char ILEarglist_buf[sizeof(fileReadrrn_St) + 15];
    if (!actmark) loadSrvpgm();
    fileReadrrn_St *ILEarglist = (fileReadrrn_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->recbuf.s.addr = (ulong)recbuf;
    ILEarglist->rrn = rrn;
    ILEarglist->lock = lock;
    _ILECALL(fileReadrrnTarget, &ILEarglist->base, fileReadrrn_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileReadne(int fileno, char *recbuf, int keylen, int lock)
{
    char ILEarglist_buf[sizeof(fileReadne_St) + 15];
    if (!actmark) loadSrvpgm();
    fileReadne_St *ILEarglist = (fileReadne_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->recbuf.s.addr = (ulong)recbuf;
    ILEarglist->keylen = keylen;
    ILEarglist->lock = lock;
    _ILECALL(fileReadneTarget, &ILEarglist->base, fileReadne_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileReadpe(int fileno, char *recbuf, int keylen, int lock)
{
    char ILEarglist_buf[sizeof(fileReadne_St) + 15];
    if (!actmark) loadSrvpgm();
    fileReadne_St *ILEarglist = (fileReadne_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->recbuf.s.addr = (ulong)recbuf;
    ILEarglist->keylen = keylen;
    ILEarglist->lock = lock;
    _ILECALL(fileReadpeTarget, &ILEarglist->base, fileReadne_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileReadf(int fileno, char *recbuf, int lock)
{
    char ILEarglist_buf[sizeof(fileRead_St) + 15];
    if (!actmark) loadSrvpgm();
    fileRead_St *ILEarglist = (fileRead_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->recbuf.s.addr = (ulong)recbuf;
    ILEarglist->lock = lock;
    _ILECALL(fileReadfTarget, &ILEarglist->base, fileRead_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileReadl(int fileno, char *recbuf, int lock)
{
    char ILEarglist_buf[sizeof(fileRead_St) + 15];
    if (!actmark) loadSrvpgm();
    fileRead_St *ILEarglist = (fileRead_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->recbuf.s.addr = (ulong)recbuf;
    ILEarglist->lock = lock;
    _ILECALL(fileReadlTarget, &ILEarglist->base, fileRead_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileReadn(int fileno, char *recbuf, int lock)
{
    char ILEarglist_buf[sizeof(fileRead_St) + 15];
    if (!actmark) loadSrvpgm();
    fileRead_St *ILEarglist = (fileRead_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->recbuf.s.addr = (ulong)recbuf;
    ILEarglist->lock = lock;
    _ILECALL(fileReadnTarget, &ILEarglist->base, fileRead_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileReadp(int fileno, char *recbuf, int lock)
{
    char ILEarglist_buf[sizeof(fileRead_St) + 15];
    if (!actmark) loadSrvpgm();
    fileRead_St *ILEarglist = (fileRead_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->recbuf.s.addr = (ulong)recbuf;
    ILEarglist->lock = lock;
    _ILECALL(fileReadpTarget, &ILEarglist->base, fileRead_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileGetData(int fileno, char *buf, int size)
{
    char ILEarglist_buf[sizeof(fileGetData_St) + 15];
    if (!actmark) loadSrvpgm();
    fileGetData_St *ILEarglist = (fileGetData_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->buf.s.addr = (ulong)buf;
    ILEarglist->size = size;
    _ILECALL(fileGetDataTarget, &ILEarglist->base, fileGetData_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileGetStruct(int fileno, FileInfo *info)
{
    char ILEarglist_buf[sizeof(fileBuf_St) + 15];
    if (!actmark) loadSrvpgm();
    fileBuf_St *ILEarglist = (fileBuf_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->buf.s.addr = (ulong)info;
    _ILECALL(fileGetStructTarget, &ILEarglist->base, fileBuf_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileGetFields(int fileno, fieldInfoStruct *fi, int size)
{
    char ILEarglist_buf[sizeof(fileGetData_St) + 15];
    if (!actmark) loadSrvpgm();
    fileGetData_St *ILEarglist = (fileGetData_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->buf.s.addr = (ulong)fi;
    ILEarglist->size = size;
    _ILECALL(fileGetFieldsTarget, &ILEarglist->base, fileGetData_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileGetKeyFields(int fileno, fieldInfoStruct *fi, int size)
{
    char ILEarglist_buf[sizeof(fileGetData_St) + 15];
    if (!actmark) loadSrvpgm();
    fileGetData_St *ILEarglist = (fileGetData_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->buf.s.addr = (ulong)fi;
    ILEarglist->size = size;
    _ILECALL(fileGetKeyFieldsTarget, &ILEarglist->base, fileGetData_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileWrite(int fileno, char *buf)
{
    char ILEarglist_buf[sizeof(fileBuf_St) + 15];
    if (!actmark) loadSrvpgm();
    fileBuf_St *ILEarglist = (fileBuf_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->buf.s.addr = (ulong)buf;
    _ILECALL(fileWriteTarget, &ILEarglist->base, fileBuf_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileUpdate(int fileno, char *buf)
{
    char ILEarglist_buf[sizeof(fileBuf_St) + 15];
    if (!actmark) loadSrvpgm();
    fileBuf_St *ILEarglist = (fileBuf_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    ILEarglist->buf.s.addr = (ulong)buf;
    _ILECALL(fileUpdateTarget, &ILEarglist->base, fileBuf_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileDelete(int fileno)
{
    char ILEarglist_buf[sizeof(file_St) + 15];
    if (!actmark) loadSrvpgm();
    file_St *ILEarglist = (file_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    _ILECALL(fileDeleteTarget, &ILEarglist->base, file_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}

static int call_fileGetRrn(int fileno)
{
    char ILEarglist_buf[sizeof(file_St) + 15];
    if (!actmark) loadSrvpgm();
    file_St *ILEarglist = (file_St*)ROUND_QUAD(ILEarglist_buf);
    ILEarglist->fileno = fileno;
    _ILECALL(fileGetRrnTarget, &ILEarglist->base, file_Sign, result_type);
    return ILEarglist->base.result.s_int32.r_int32;
}
static int
file400_initFile(File400Object *f);

static PyObject*
File400_open(File400Object *self, PyObject *args);

static PyObject*
File400_new(PyTypeObject *type, PyObject *args, PyObject *keywds);

/* convert string to upper */
static char * strtolower(char *s)
{
    int c = 0;
    while (s[c] != '\0') {
        if (s[c] >= 'A' && s[c] <= 'Z') {
            s[c] = s[c] + 32;
        }
        c++;
    }
    return s;
}

/* get length of as400 string */
static int
f_strlen(char *s, int len)
{
    char *p;
    p = s + len - 1;
    while (p >= s && *p == 0x40)
        p--;
    return (p - s) + 1;
}

/* get length of ascii/utf8 string */
static int
f_utflen(char *s, int len)
{
    char *p;
    p = s + len - 1;
    while (p >= s && *p == 0x20)
        p--;
    return (p - s) + 1;
}

/* get length of unicode string */
static int
f_unilen(char *s, int len)
{
    unsigned short *p, *b;
    b = (unsigned short *)s;
    p = b + ((len - 2) / 2);
    while (p >= b && *p == 0x0020)
        p--;
    return ((p - b) + 1) * 2;
}

/* returns conversion descriptors  */
static iconv_t
initConvert(int fromccsid, int toccsid) {
    char *qtfrom;
    char *qtto;
    qtfrom = (char *) ccsidtocs(fromccsid);
    qtto = (char *) ccsidtocs(toccsid);
    return iconv_open(qtto, qtfrom);
}

iconv_t
getConvDesc(int fromccsid, int toccsid) {
    long int ccsid;
    int i;
    ccsid = (fromccsid << 16) | toccsid;
    for (i = 0; i < conv_size; i++) {
        if (ccsid == convccsid_array[i])
            return convdesc_array[i];
    }
    if (i < 30) {
        conv_size += 1;
        convdesc_array[i] = initConvert(fromccsid, toccsid);
        convccsid_array[i] = ccsid;
        return convdesc_array[i];
    } else
        return initConvert(fromccsid, toccsid);
}

/* convert form one ccsid to another, returns bytes converted */
int
convertstr(iconv_t cd, char *in, int in_size, char *out, int out_size)
{
    char *p1;
    char *p2;
    size_t insize, outsize;
    p1 = in;
    p2 = out;
    insize = in_size;
    outsize = out_size;
    if (iconv(cd, &p1, &insize, &p2, &outsize) < 0)
        return -1;
    return (out_size - outsize);
}

/* convert form one ccsid to another, returns a PyUnicode */
PyObject *
convertString(iconv_t cd, char *in, int size)
{
    char *p1, *p2, *p3;
    size_t insize, outsize, newsize;
    int pos;
    PyObject *outobj;
    if (cd < 0) {
        PyErr_SetString(file400Error, "iconv descriptor not valid.");
        return NULL;
    }
    if (size <= 0)
        return PyUnicode_FromStringAndSize("", 0);
    p1 = in;
    insize = size;
    newsize = outsize = size * 1.05 + 2;
    p2 = p3 = malloc(newsize + 1);
    iconv(cd, &p1, &insize, &p3, &outsize);
    while (insize > 0) {
        pos = newsize - outsize;
        newsize += insize * 2;
        p2 = realloc(p2, newsize + 1);
        p3 = p2 + pos;
        outsize = newsize - pos;
        iconv(cd, &p1, &insize, &p3, &outsize);
        /* if outsize still is same size there is a bug */
        if (outsize == newsize - pos)
            return NULL;
    }
    outobj = PyUnicode_FromStringAndSize(p2, newsize - outsize);
    free(p2);
    return outobj;
}

/* convert from ebcdic ccsid to utf-8 */
PyObject *
ebcdicToString(int fromccsid, char *in, int size)
{
    return convertString(getConvDesc(fromccsid, 1208), in, size);
}

/* convert packed to string (ascii) */
char *
packedtostr(char *buf, char *p, int digits, int dec, char decsep)
{
    unsigned char c;
    int decstart, i, j;
    /* size should be odd */
    if ((digits % 2) == 0)
        digits++;
    decstart = digits - dec;
    /* check if negative */
    j = 0;
    if ((p[digits/2] | 0xF0) != 0xFF) {
        buf[j] = '-';
        j++;
    }
    /* get all digits */
    for (i = 0; i < digits; i++) {
        if (i % 2)
            c = (p[i / 2] & 0x0F) | 0x30;
        else
            c = (p[i / 2] >> 4) | 0x30;
        /* decimal point */
        if (i == decstart) {
            if (j == 0 || (j == 1 && buf[0] == '-')) {
                buf[j] = '0';
                j++;
            }
            buf[j] = decsep;
            j++;
        }
        if (c != 0x30 || j > 1 || (j > 0 && buf[0] != '-'))  {
            buf[j] = c;
            j++;
        }
    }
    if (j == 0) {
        buf[j] = '0';
        j++;
    }
    buf[j] = '\0';
    return buf;
}

/* convert zoned to string (ascii)*/
char *
zonedtostr(char *buf, char *p, int digits, int dec, char decsep)
{
    unsigned char c;
    int decstart, i, j;
    decstart = digits - dec;
    /* check if negative */
    j = 0;
    if (((p[digits - 1] >> 4) | 0xF0) != 0xFF) {
        buf[j] = '-';
        j++;
    }
    /* get all digits */
    for (i = 0; i < digits; i++) {
        c = (p[i] & 0x0f) | 0x30;
        /* decimal point */
        if (i == decstart) {
            if (j == 0 || (j == 1 && buf[0] == '-')) {
                buf[j] = '0';
                j++;
            }
            buf[j] = decsep;
            j++;
        }
        if (c != 0x30 || j > 1 || (j > 0 && buf[0] != '-'))  {
            buf[j] = c;
            j++;
        }
    }
    if (j == 0) {
        buf[j] = '0';
        j++;
    }
    buf[j] = '\0';
    return buf;
}

/* convert field to Python format */
static PyObject *
f_cvtToPy(char * fb, fieldInfoStruct *field)
{
    short dsh, varlen;
    char buf[100], *ss;
    char *p;
    long dl;
    int len;
    long long dll;
    float dfl;
    double ddbl;

    p = fb + field->offset;
    len = field->len;
    switch (field->type) {
    /* binary */
    case 0:
        if (len == 2) {
            memcpy(&dsh, p, 2);
            return PyLong_FromLong(dsh);
        } else if (len == 4) {
            memcpy(&dl, p, 4);
            return PyLong_FromLong(dl);
        } else if (len == 8) {
            memcpy(&dll, p, 8);
            return PyLong_FromLongLong(dll);
        } else {
            PyErr_SetString(file400Error, "Data type error.");
            return NULL;
        }
    /* float */
    case 1:
        if (len == 4) {
            memcpy(&dfl, p, 4);
            return PyFloat_FromDouble((double)dfl);
        }
        else if (len == 8) {
            memcpy(&ddbl, p, 8);
            return PyFloat_FromDouble((double)ddbl);
        } else {
            PyErr_SetString(file400Error, "Data type error.");
            return NULL;
        }
    /* zoned */
    case 2:
        zonedtostr((char *)buf, p, field->digits, field->dec, '.');
        if (field->dec == 0)
            return PyLong_FromLongLong(strtoll((char *)buf, &ss, 10));
        else
            return PyFloat_FromDouble(strtod((char *)buf, &ss));
    /* packed */
    case 3:
        packedtostr((char *)buf, p, field->digits, field->dec, '.');
        if (field->dec == 0)
            return PyLong_FromLongLong(strtoll((char *)buf, &ss, 10));
        else
            return PyFloat_FromDouble(strtod((char *)buf, &ss));
    /* char */
    case 4:
        if (field->ccsid == 1208)
            return PyUnicode_FromStringAndSize(p, f_utflen(p, len));
        else if (field->ccsid == 65535)
            return PyBytes_FromStringAndSize(p, len);
        else
            return ebcdicToString(field->ccsid, p, f_strlen(p, len));
    /* graphic (unicode) */
    case 5:
        if (field->ccsid == 1200)
            return PyUnicode_DecodeUTF16(p, f_unilen(p, len), "", 0);
        else if (field->ccsid == 13488)
            return PyUnicode_FromUnicode((Py_UNICODE*)p, f_unilen(p, len) / 2);
        else
            return PyUnicode_FromStringAndSize(p, len);
    /* date, time, timestamp */
    case 11: case 12: case 13:
        if (field->ccsid == 65535 || field->ccsid == 1208)
            return PyUnicode_FromStringAndSize(p, len);
        else
            return ebcdicToString(field->ccsid, p, len);
    /* varchar */
    case 0x8004:
        memcpy(&varlen, p, sizeof(short));
        p += sizeof(short);
        if (field->ccsid == 1208)
            return PyUnicode_FromStringAndSize(p, f_utflen(p, varlen));
        else if (field->ccsid == 65535)
            return PyBytes_FromStringAndSize(p, varlen);
        else
            return ebcdicToString(field->ccsid, p, f_strlen(p, varlen));
    /* vargraphic(unicode) */
    case 0x8005:
        memcpy(&varlen, p, sizeof(short));
        p += sizeof(short);
        if (field->ccsid == 1200)
            return PyUnicode_DecodeUTF16(p, f_unilen(p, varlen), "", 0);
        else if (field->ccsid == 13488)
            return PyUnicode_FromUnicode((Py_UNICODE*)p, f_unilen(p, varlen) / 2);
        else
            return PyUnicode_FromStringAndSize(p, varlen);
    default:
        return PyUnicode_FromStringAndSize(p, len);
    }
}

/* convert field from Python format */
static int
f_cvtFromPy(char *fb, fieldInfoStruct *field, PyObject *o)
{
    short dsh;
    long dlo;
    long long dll;
    float dfl;
    double ddbl;
    char *c;
    char *p;
    int len, i, digits, dec;
    Py_ssize_t si;

    p = fb + field->offset;
    len = field->len;
    switch (field->type) {
    /* binary */
    case 0:
        if (PyLong_Check(o)) {
            if (len == 2) {
                dsh = PyLong_AsLong(o);
                memcpy(p, &dsh, len);
                break;
            } else if (len == 4) {
                dlo = PyLong_AsLong(o);
                memcpy(p, &dlo, len);
                break;
            } else if (len == 8) {
                dll = PyLong_AsLong(o);
                memcpy(p, &dll, len);
                break;
            }
        }
        PyErr_SetString(file400Error, "Data conversion error.");
        return -1;
    /* float */
    case 1:
        if (len == 4) {
            if (PyLong_Check(o))
                dfl = PyLong_AS_LONG(o);
            else if (PyFloat_Check(o))
                dfl = PyFloat_AS_DOUBLE(o);
            else {
                PyErr_SetString(file400Error, "Data conversion error.");
                return -1;
            }
            memcpy(p, &dfl, len);
            break;
        } else {
            if (PyLong_Check(o))
                ddbl = PyLong_AsLong(o);
            else if (PyFloat_Check(o))
                ddbl = PyFloat_AS_DOUBLE(o);
            else {
                PyErr_SetString(file400Error, "Data conversion error.");
                return -1;
            }
            memcpy(p, &ddbl, len);
            break;
        }
    /* zoned */
    case 2:
        digits = field->digits;
        dec = field->dec;
        if (PyLong_Check(o)) {
            if ((digits - dec) < 10)
                call_int2zoned(p, digits, dec, PyLong_AsLong(o));
            else
                call_float2zoned(p, digits, dec, PyLong_AsDouble(o));
            break;
        } else if (PyFloat_Check(o)) {
            call_float2zoned(p, digits, dec, PyFloat_AS_DOUBLE(o));
            break;
        }
        PyErr_SetString(file400Error, "Data conversion error.");
        return -1;
    /* packed */
    case 3:
        digits = field->digits;
        dec = field->dec;
        if (PyLong_Check(o)) {
            if ((digits - dec) < 10)
                call_int2packed(p, digits, dec, PyLong_AsLong(o));
            else
                call_float2packed(p, digits, dec, PyLong_AsDouble(o));
            break;
        } else if (PyFloat_Check(o)) {
            call_float2packed(p, digits, dec, PyFloat_AS_DOUBLE(o));
            break;
        }
        PyErr_SetString(file400Error, "Data conversion error.");
        return -1;
    /* char, date, time, timestamp */
    case 4: case 11: case 12: case 13:
        if (PyBytes_Check(o) || PyUnicode_Check(o)) {
            if (PyBytes_Check(o)) {
                c = PyBytes_AsString(o);
                i = PyBytes_GET_SIZE(o);
                if (i >= len)
                    memcpy(p, c, len);
                else {
                    memcpy(p, c, i);
                    p += i;
                    memset(p, ' ', len - i);
                }
            } else {
                c = PyUnicode_AsUTF8AndSize(o, &si);
                if (si > 0) {
                    dsh = convertstr(getConvDesc(1208, field->ccsid), c, si, p, len);
                    if (dsh < 0) {
                        PyErr_SetString(file400Error, "iconv failed.");
                        return -1;
                    }
                } else
                    dsh = 0;
                if (dsh < len)
                    memset(p + dsh, 0x40, len - dsh);
            }
            break;
        }
        PyErr_SetString(file400Error, "Data conversion error.");
        return -1;
    /* Graphic (unicode) */
    case 5:
        if (PyUnicode_Check(o)) {
            short *s, *e;
            wchar_t *w = PyUnicode_AsWideCharString(o, &si);
            if (field->ccsid == 13488|| field->ccsid == 1200 || field->ccsid == 65535) {
                if (si >= len)
                    memcpy(p, w, len);
                else {
                    memcpy(p, w, si);
                    s = (short *)(p + si);
                    e = s + len / 2;
                    while (s < e) {
                        *s = 0x0020;
                        s++;
                    }
                }
                break;
            }
        }
        PyErr_SetString(file400Error, "Data conversion error.");
        return -1;
    /* varchar */
    case 0x8004:
        if (PyUnicode_Check(o)) {
            c = PyUnicode_AsUTF8AndSize(o, &si);
            if (field->ccsid == 65535 || field->ccsid == 1208) {
                if (si > (len - 2))
                    dsh = (len - 2);
                else
                    dsh = si;
                memcpy(p, &dsh, 2);
                memcpy(p + 2, c, dsh);
            } else {
                dsh = (short)convertstr(getConvDesc(1208, field->ccsid), c, si, p + 2, len - 2);
                memcpy(p, &dsh, 2);
            }
            break;
        } else  if (PyBytes_Check(o)) {
            c = PyBytes_AsString(o);
            i = PyBytes_GET_SIZE(o);
            if (i > (len - 2))
                dsh = (len - 2);
            else
                dsh = i;
            memcpy(p, &dsh, 2);
            memcpy(p + 2, c, dsh);
            break;
        }
        PyErr_SetString(file400Error, "Data conversion error.");
        return -1;
    /* Graphic (unicode) */
    case 0x8005:
        if (PyUnicode_Check(o)) {
            short *s, *e;
            wchar_t *w = PyUnicode_AsWideCharString(o, &si);
            if (field->ccsid == 13488|| field->ccsid == 1200 || field->ccsid == 65535) {
                if (si > (len - 2))
                    dsh = (len - 2);
                else
                    dsh = si;
                memcpy(p, &dsh, 2);
                p += 2;
                memcpy(p, w, si);
                s = (short *)(p + si);
                e = s + len / 2;
                while (s < e) {
                    *s = 0x0020;
                    s++;
                }
                break;
            }
        }
        PyErr_SetString(file400Error, "Data conversion error.");
        return -1;
    default:
        if (PyBytes_Check(o)) {
            c = PyBytes_AsString(o);
            i = PyBytes_GET_SIZE(o);
            if (i >= len)
                memcpy(p, c, len);
            else
                memcpy(p, c, i);
            break;
        }
        PyErr_SetString(file400Error, "Data type not supported.");
        return -1;
    }
    return 0;
}

/* clear the record */
static int
f_clear(File400Object *file, char *record)
{
    int i;
    char * p;
    short *s, *e;
    /* set all fields that don't hve blanks as default */
    for (i = 0; i < file->fi.fieldCount; i++) {
        p = record + file->fieldArr[i].offset;
        switch(file->fieldArr[i].type) {
        /* binary, float*/
        case 0: case 1:
            memset(p, 0x00, file->fieldArr[i].len);
            break;
        /* zoned */
        case 2:
            memset(p, 0xf0, file->fieldArr[i].len);
            break;
        /* packed */
        case 3:
            memset(p, 0x00, file->fieldArr[i].len);
            p += file->fieldArr[i].len - 1;
            memset(p, 0x0f, 1);
            break;
        /* char */
        case 4:
            if (file->fieldArr[i].ccsid == 1208)
                memset(p, 0x20, file->fieldArr[i].len);
            else
                memset(p, 0x40, file->fieldArr[i].len);
            break;
        /* Grapics (unicode) */
        case 5:
            s = (short *)p;
            e = s + file->fieldArr[i].len / 2;
            while (s < e) {
                *s = 0x0020;
                s++;
            }
            break;
        /* date, time, timestamp */
        case 11: case 12: case 13:
            memset(p, 0x00, file->fieldArr[i].len);
            break;
        /* varchar */
        case 0x8004: case 0x8005:
            memset(p, 0x00, file->fieldArr[i].len);
            break;
        default:
            memset(p, 0x00, file->fieldArr[i].len);
        }
    }
    return 0;
}

/* internal routine to get field value from record */
static PyObject *
f_getFieldValue(File400Object *self, int pos, char *p)
{
    return f_cvtToPy(p, &self->fieldArr[pos]);
}

/* internal routine to set field value */
static int
f_setFieldValue(File400Object *self, int pos, PyObject *o, char *p)
{
    if (self->fi.omode == OPEN_WRITE)
        return f_cvtFromPy(p, &self->fieldArr[pos], o);
    else
        return f_cvtFromPy(p, &self->fieldArr[pos], o);
}

/* internal routine to get field position */
static int
f_getFieldPos(File400Object *self, PyObject *field)
{
    PyObject *posO;
    int i = -1;

    Py_INCREF(field);
    if (PyLong_Check(field)) {
        i = PyLong_AS_LONG(field);
        if (i >= self->fi.fieldCount)
            i = -1;
    } else if (PyUnicode_Check(field)) {
        /* convert to upper, first check */
        posO = PyDict_GetItem(self->fieldDict, field);
        if (posO != NULL)
            i = PyLong_AS_LONG(posO);
    }
    Py_DECREF(field);
    return i;
}

/* check if file is initialized */
static int
f_initialize(File400Object *self)
{
    if (self->fieldArr == NULL) {
        if (file400_initFile(self) < 0) {
            return 0;
        }
    }
    return 1;
}

/* check if file is open */
static int
f_isOpen(File400Object *self)
{
    if (self->fieldArr == NULL) {
        if (file400_initFile(self) < 0) {
            return 0;
        }
    }
    return 1;
}

/* get key length and update key buffer */
static int
f_keylen(File400Object *self, PyObject *key, char *keyval)
{
    int i, keyLen, keyCnt;
    /* The key can be None (all keys) */
    /* or an integer with number of keys to search */
    if (keyval == NULL && PyLong_Check(key)) {
        /* find key length */
        keyCnt = PyLong_AS_LONG(key);
        if (keyCnt < 0 || keyCnt > self->fi.keyCount) {
            PyErr_SetString(file400Error, "Number of key fields not valid.");
            return -1;
        }
        keyLen = 0;
        for (i = 0; i < keyCnt; i++)
            keyLen += self->keyArr[i].len;
    } else if (keyval != NULL && PySequence_Check(key)) {
        PyObject *ko;
        /* check number of key fields */
        keyCnt = PySequence_Length(key);
        if (keyCnt > self->fi.keyCount) {
            PyErr_SetString(file400Error, "Too many key fields.");
            return -1;
        }
        /* put key values  */
        keyLen = 0;
        for (i = 0; i < keyCnt; i++) {
            ko = PySequence_GetItem(key, i);
            if (f_cvtFromPy(keyval, &self->keyArr[i], ko) < 0)
                return -1;
            Py_DECREF(ko);
            keyLen += self->keyArr[i].len;
        }
    } else {
        PyErr_SetString(file400Error, "Key not valid.");
        return -1;
    }
    return keyLen;
}

/* creates object from class or type */
static PyObject *
f_createObject(PyObject *cls)
{
    PyObject *obj, *args, *kwds;
    args = PyTuple_New(0);
    kwds = PyDict_New();
    obj = ((PyTypeObject *)cls)->tp_new((PyTypeObject *)cls, args, kwds);
    Py_DECREF(args);
    Py_DECREF(kwds);
    return obj;
}

/* File400 methods */

static void
File400_dealloc(File400Object *self)
{
    if (self->recbuf) PyMem_Free(self->recbuf);
    if (self->fieldArr) PyMem_Free(self->fieldArr);
    if (self->keyArr) PyMem_Free(self->keyArr);
    Py_XDECREF(self->fieldDict);
    Py_XDECREF(self->keyDict);
    call_fileFree(self->fileno);
    PyObject_Del(self);
}

static char open_doc[] =
"f.open() -> None\n\
\n\
Open the file.";

static PyObject*
File400_open(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":open"))
        return NULL;
    if (!self->recbuf) {
        if (file400_initFile(self) < 0) {
            return NULL;
        }
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char close_doc[] =
"f.close() -> None.\n\
\n\
Close the file.";

static PyObject *
File400_close(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":close"))
        return NULL;
    if (self->fieldArr == NULL || call_fileFree(self->fileno) < 0) {
        PyErr_SetString(file400Error, "Error closing file.");
        return NULL;
    }
    if (self->recbuf) PyMem_Free(self->recbuf);
    if (self->fieldArr) PyMem_Free(self->fieldArr);
    if (self->keyArr) PyMem_Free(self->keyArr);
    Py_XDECREF(self->fieldDict);
    Py_XDECREF(self->keyDict);
    self->recbuf = NULL;
    self->fieldArr = NULL;
    self->keyArr = NULL;
    self->fieldDict = NULL;
    self->keyDict = NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static char posb_doc[] =
"f.posb(key[lock]) -> returns 1 - if key found otherwise 0.\n\
\n\
Positions before the first record that has a key equal to the specified key.\n\
The key value can be a number that says number of keyfields\n\
to use from the key buffer, or it can be a sequence of key values.\n\
Use lock value, if given, on the following reads";

static PyObject *
File400_posb(File400Object *self, PyObject *args, PyObject *keywds)
{
    int result, keyLen, lock = -1;
    char *keybuf;
    PyObject *key;
    static char *kwlist[] = {"key","lock", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "O|i:posb", kwlist, &key, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    keybuf = PyMem_Malloc(self->fi.keyLen + 1);
    keyLen = f_keylen(self, key, keybuf);
    if (keyLen < 0) {
        PyMem_Free(keybuf);
        return NULL;
    }
    result = call_filePosb(self->fileno, keybuf, keyLen, lock);
    PyMem_Free(keybuf);
    if (result == -1) {
        PyErr_SetString(file400Error, "posb failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char posa_doc[] =
"f.posa(key[lock]) -> None.\n\
\n\
Positions after the last record that has a key equal to the specified key.\n\
The key value can be a number that says number of keyfields\n\
to use from the key buffer, or it can be a sequence of key values\n\
Uses the lock value, if given, on the following reads";

static PyObject *
File400_posa(File400Object *self, PyObject *args, PyObject *keywds)
{
    int  keyLen, result, lock = -1;
    char *keybuf;
    PyObject *key;
    static char *kwlist[] = {"key","lock", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "O|i:posa", kwlist, &key, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    keybuf = PyMem_Malloc(self->fi.keyLen + 1);
    keyLen = f_keylen(self, key, keybuf);
    if (keyLen < 0) {
        PyMem_Free(keybuf);
        return NULL;
    }
    result = call_filePosa(self->fileno, keybuf, keyLen, lock);
    PyMem_Free(keybuf);
    if (result == -1) {
        PyErr_SetString(file400Error, "posa failed.");
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char posf_doc[] =
"f.posf([lock]) -> None.\n\
\n\
Positions before the first record.\n\
Uses the lock value, if given, on the following reads";

static PyObject *
File400_posf(File400Object *self, PyObject *args, PyObject *keywds)
{
    int result, lock = -1;
    static char *kwlist[] = {"lock", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|i:posf", kwlist, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_filePosf(self->fileno, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "posa failed.");
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char posl_doc[] =
"f.posl([lock]) -> None.\n\
\n\
Positions after the last record.\n\
Uses the lock value, if given, on the following reads";

static PyObject *
File400_posl(File400Object *self, PyObject *args, PyObject *keywds)
{
    int result, lock = -1;
    static char *kwlist[] = {"lock", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|i:posl", kwlist, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_filePosl(self->fileno, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "posa failed.");
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char readrrn_doc[] =
"f.readrrn(rrn[lock]) -> 0 (found), 1(not found).\n\
\n\
Read by relative record number.\n\
Lock (for mode 'r+'). 1 - lock(default) 0 - no lock.";

static PyObject *
File400_readrrn(File400Object *self, PyObject *args)
{
    int result, rrn, lock = 1;

    if (!PyArg_ParseTuple(args, "i|i:readrrn", &rrn, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_fileReadrrn(self->fileno, self->recbuf, rrn, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "readrrn failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char readeq_doc[] =
"f.readeq(key[lock]) -> 0 (found), 1(not found).\n\
\n\
Read first record equal with the specified key.\n\
The key value can be a number that says number of keyfields\n\
to use from the key buffer, or it can be a sequence of key values.\n\
Lock (for mode 'r+'). 1 - lock(default) 0 - no lock.";

static PyObject *
File400_readeq(File400Object *self, PyObject *args, PyObject *keywds)
{
    int result, keyLen, lock = 1;
    char *keybuf;
    static char *kwlist[] = {"key","lock", NULL};
    PyObject *key = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "O|i:readeq", kwlist, &key, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    keybuf = PyMem_Malloc(self->fi.keyLen + 1);
    keyLen = f_keylen(self, key, keybuf);
    if (keyLen < 0) {
        PyMem_Free(keybuf);
        return NULL;
    }
    result = call_fileReadeq(self->fileno, self->recbuf, keybuf, keyLen, lock);
    PyMem_Free(keybuf);
    if (result == -1) {
        PyErr_SetString(file400Error, "readeq failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char readn_doc[] =
"f.readn([lock]) -> 0 (found), 1(not found).\n\
\n\
Read next record into the buffer.\n\
Lock (for mode 'r+'). 1 - lock(default) 0 - no lock.";

static PyObject *
File400_readn(File400Object *self, PyObject *args)
{
    int result, lock = -1;
    if (!PyArg_ParseTuple(args, "|i:readn", &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_fileReadn(self->fileno, self->recbuf, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "readn failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char readne_doc[] =
"f.readne([key][lock]) -> 0 (found), 1(not found).\n\
\n\
Read next record if equal with the specified key.\n\
The key should be a number that says number of keyfields\n\
to use from the key buffer.\n\
If it's left out, the key from last posa/posb/readeq is used.\n\
Lock (for mode 'r+'). 1 - lock(default) 0 - no lock.";

static PyObject *
File400_readne(File400Object *self, PyObject *args)
{
    int result, keyLen = 0, lock = -1;
    PyObject *key = Py_None;

    if (!PyArg_ParseTuple(args, "|Oi:readne", &key, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    if (key != Py_None) {
        if (!PyLong_Check(key)) {
            PyErr_SetString(file400Error, "readne Key must be a number of key fields.");
            return NULL;
        }
        keyLen = f_keylen(self, key, NULL);
        if (keyLen == -1)
            return NULL;
    }
    result = call_fileReadne(self->fileno, self->recbuf, keyLen, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "readne failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char readpe_doc[] =
"f.readpe([key][lock]) -> 0 (found), 1(not found).\n\
\n\
Read previous record if equal with the specified key.\n\
The key should be a number that says number of keyfields\n\
to use from the key buffer.\n\
If it's left out, the key from last posa/posb/readeq is used.\n\
Lock (for mode 'r+'). 1 - lock(default) 0 - no lock.";

static PyObject *
File400_readpe(File400Object *self, PyObject *args)
{
    int result, keyLen = 0, lock = -1;
    PyObject *key = Py_None;

    if (!PyArg_ParseTuple(args, "|Oi:readpe", &key, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    if (key != Py_None) {
        if (!PyLong_Check(key)) {
            PyErr_SetString(file400Error, "readne Key must be a number of key fields.");
            return NULL;
        }
        keyLen = f_keylen(self, key, NULL);
        if (keyLen == -1)
            return NULL;
    }
    result = call_fileReadpe(self->fileno, self->recbuf, keyLen, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "readpe failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char readp_doc[] =
"f.readp([lock]) -> 0 (found), 1(not found).\n\
\n\
Read previous record into the buffer.\n\
Lock (for mode 'r+'). 1 - lock(default) 0 - no lock.";

static PyObject *
File400_readp(File400Object *self, PyObject *args)
{
    int result, lock = -1;
    if (!PyArg_ParseTuple(args, "|i:readp", &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_fileReadp(self->fileno, self->recbuf, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "readp failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char readf_doc[] =
"f.readf([lock]) -> 0 (found), 1(not found).\n\
\n\
Read first record into the buffer.\n\
Lock (for mode 'r+'). 1 - lock(default) 0 - no lock.";

static PyObject *
File400_readf(File400Object *self, PyObject *args, PyObject *keywds)
{
    int result, lock = 1;
    static char *kwlist[] = {"lock", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|i:readf", kwlist, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_fileReadf(self->fileno, self->recbuf, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "readf failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char readl_doc[] =
"f.readl([lock]) -> 0 (found), 1(not found).\n\
\n\
Read last record into the buffer.\n\
Lock (for mode 'r+'). 1 - lock(default) 0 - no lock.";

static PyObject *
File400_readl(File400Object *self, PyObject *args, PyObject *keywds)
{
    int result, lock = 1;
    static char *kwlist[] = {"lock", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|i:readl", kwlist, &lock))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_fileReadl(self->fileno, self->recbuf, lock);
    if (result == -1) {
        PyErr_SetString(file400Error, "readl failed.");
        return NULL;
    }
    return PyLong_FromLong(result);
}

static char write_doc[] =
"f.write() -> None.\n\
\n\
Appends the buffer to the file.";

static PyObject *
File400_write(File400Object *self, PyObject *args)
{
    int result;
    if (!PyArg_ParseTuple(args, ":write"))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    if (self->fi.omode == OPEN_READ) {
        PyErr_SetString(file400Error, "File not opened for write.");
        return NULL;
    }
    result = call_fileWrite(self->fileno, self->recbuf);
    if (result == -1) {
        PyErr_SetString(file400Error, "write failed.");
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char delete_doc[] =
"f.delete() -> None.\n\
\n\
Deletes the currently locked record with the content of the buffer.";

static PyObject *
File400_delete(File400Object *self, PyObject *args)
{
    int result;
    if (!PyArg_ParseTuple(args, ":delete"))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    if (self->fi.omode != OPEN_UPDATE) {
        PyErr_SetString(file400Error, "File not opened for update.");
        return NULL;
    }
    result = call_fileDelete(self->fileno);
    if (result == -1) {
        PyErr_SetString(file400Error, "delete failed.");
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char update_doc[] =
"f.update() -> None.\n\
\n\
Updates the currently locked record with the content of the buffer.";

static PyObject *
File400_update(File400Object *self, PyObject *args)
{
    int result;
    if (!PyArg_ParseTuple(args, ":update"))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    if (self->fi.omode != OPEN_UPDATE) {
        PyErr_SetString(file400Error, "File not opened for update.");
        return NULL;
    }
    result = call_fileUpdate(self->fileno, self->recbuf);
    if (result == -1) {
        PyErr_SetString(file400Error, "update failed.");
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char rlsLock_doc[] =
"f.rlsLock() -> None.\n\
\n\
Releases the lock on the currently locked record.";

static PyObject *
File400_rlsLock(File400Object *self, PyObject *args)
{
    int result;
    if (!PyArg_ParseTuple(args, ":rlsLock"))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_fileRlsLock(self->fileno);
    if (result == -1) {
        PyErr_SetString(file400Error, "rlsLock failed.");
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char get_doc[] =
"f.get([fields][output][cls][labels]) -> List or Value.\n\
\n\
Get information from the buffer.\n\
f.get() - Get list of all values.\n\
f.get('cusno') - Value of the field cusno. Same as f['cusno']\n\
f.get(0) - Value of the first field. Same as f[0]\n\
f.get(('cusno','name')) - Returns list of values.\n\
f.get((0,1)) - Returns list of values.\n\
output can have the following values:\n\
 0 - output as a list(default)\n\
 1 - output as a object.\n\
 2 - output as a dictionary.\n\
cls is class of object if output object is selected.\n\
labels could be a tuple of labels to use insted of fieldnames\n\
if output is dictionary.";


static PyObject *
File400_get(File400Object *self, PyObject *args, PyObject *keywds)
{
    PyObject *o = Py_None, *labels = Py_None, *cls = Py_None,
        *fo, *va, *obj, *dict;
    int i, pos, len, output = LIST;
    static char *kwlist[] = {"fields","output","cls","labels", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|OiOO:get", kwlist,
                                     &o, &output, &cls, &labels))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    if (!cls || cls == Py_None)
        cls = fileRowClass;
    else if(output == OBJ && !PyClass_Check(cls) && !PyType_Check(cls)) {
        PyErr_SetString(file400Error, "cls not a valid type.");
        return NULL;
    }
    /* if none, return all fields*/
    if (o == NULL || o == Py_None) {
        if (output == LIST) {
            obj = PyList_New(self->fi.fieldCount);
            for (i = 0; i < self->fi.fieldCount; i++) {
                va = f_getFieldValue(self, i, self->recbuf);
                if (va == NULL) return NULL;
                PyList_SetItem(obj, i, va);
            }
        } else {
            char *label;
            if (output == OBJ) {
                obj = f_createObject(cls);
                if(obj == NULL) {
                    PyErr_SetString(file400Error, "Could not create object.");
                    return NULL;
                }
                dict = PyObject_GetAttrString(obj, "__dict__");
                if(dict == NULL) {
                    PyErr_SetString(file400Error, "Can not get objects __dict__.");
                    return NULL;
                }
                Py_DECREF(dict);
            } else
                obj = dict = PyDict_New();
            for (i = 0; i < self->fi.fieldCount; i++) {
                if (PyTuple_Check(labels) && PyTuple_Size(labels) > i) {
                    label = PyUnicode_AsUTF8(PyTuple_GetItem(labels, i));
                } else
                    label = self->fieldArr[i].name;
                va = f_getFieldValue(self, i, self->recbuf);
                if (va == NULL) return NULL;
                PyDict_SetItemString(dict, label, va);
                Py_DECREF(va);
            }
        }
        return obj;
    }
    /* one value */
    else if (PyLong_Check(o) || PyUnicode_Check(o)) {
        pos = f_getFieldPos(self, o);
        if (pos < 0) {
            PyErr_SetString(file400Error, "Parameter not valid.");
            return NULL;
        } else
            return f_getFieldValue(self, pos, self->recbuf);
    }
    /* if tuple get specified fields */
    else if (PySequence_Check(o)) {
        char *label;
        len = PySequence_Length(o);
        if (output == LIST)
            obj = PyList_New(len);
        else {
            if (output == OBJ) {
                obj = f_createObject(cls);
                if(obj == NULL) {
                    PyErr_SetString(file400Error, "Could not create object.");
                    return NULL;
                }
                dict = PyObject_GetAttrString(obj, "__dict__");
                if(dict == NULL) {
                    PyErr_SetString(file400Error, "Object has no __dict__.");
                    return NULL;
                }
                Py_DECREF(dict);
            } else
                obj = dict = PyDict_New();
        }
        for (i = 0; i < len; i++) {
            fo = PySequence_GetItem(o, i);
            pos = f_getFieldPos(self, fo);
            Py_DECREF(fo);
            if (pos < 0 ) {
                PyErr_SetString(file400Error, "Field not valid.");
                Py_DECREF(obj);
                return NULL;
            } else {
                if (output == LIST) {
                    va = f_getFieldValue(self, pos, self->recbuf);
                    if (va == NULL) return NULL;
                    PyList_SetItem(obj, i, va);
                } else {
                    if (PyTuple_Check(labels) && PyTuple_Size(labels) > i) {
                        label = PyUnicode_AsUTF8(PyTuple_GetItem(labels, i));
                    } else
                        label = self->fieldArr[pos].name;
                    va = f_getFieldValue(self, pos, self->recbuf);
                    if (va == NULL) return NULL;
                    PyDict_SetItemString(dict, label, va);
                    Py_DECREF(va);
                }
            }
        }
        return obj;
    }
    PyErr_SetString(file400Error, "Request failed.");
    return NULL;
}

static char clear_doc[] =
"f.clear() -> None.\n\
\n\
Clear the record buffer.\n\
Numeric fields are set to 0 and chars to blank.";

static PyObject *
File400_clear(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":clear"))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    f_clear(self, self->recbuf);
    Py_INCREF(Py_None);
    return Py_None;
}


static char getRrn_doc[] =
"f.getRrn() -> Long.\n\
\n\
Get the current record number.";

static PyObject *
File400_getRrn(File400Object *self, PyObject *args)
{
    int result;
    if (!PyArg_ParseTuple(args, ":getRrn"))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    result = call_fileGetRrn(self->fileno);
    return PyLong_FromLong(result);
}


static char isOpen_doc[] =
"f.isOpen() -> Int.\n\
\n\
Returns 1 if open otherwise 0.";

static PyObject *
File400_isOpen(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":isOpen"))
        return NULL;
    return PyLong_FromLong((self->fieldArr != NULL));
}

static char mode_doc[] =
"f.mode() -> String.\n\
\n\
Returns open mode.";

static PyObject *
File400_mode(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":mode"))
        return NULL;
    if (self->fi.omode == OPEN_READ)
        return PyUnicode_FromString("r");
    else if (self->fi.omode == OPEN_UPDATE)
        return PyUnicode_FromString("r+");
    else if (self->fi.omode == OPEN_WRITE)
        return PyUnicode_FromString("a");
    else {
        Py_INCREF(Py_None);
        return Py_None;
    }
}

static char recordSize_doc[] =
"f.recordSize() -> Long.\n\
\n\
Get the size of the record in bytes.";

static PyObject *
File400_recordSize(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":recordSize"))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    return PyLong_FromLong(self->fi.recLen);
}

static char fileName_doc[] =
"f.fileName() -> String.\n\
\n\
Get name of file.";

static PyObject *
File400_fileName(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":fileName"))
        return NULL;
    return PyUnicode_FromString(self->fi.name);
}

static char libName_doc[] =
"f.libName() -> String.\n\
\n\
Get name of library.";

static PyObject *
File400_libName(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":libName"))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    return PyUnicode_FromString(self->fi.lib);
}

static char fieldCount_doc[] =
"f.fieldCount() -> Int.\n\
\n\
Get number of fields.";

static PyObject *
File400_fieldCount(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":fieldCount"))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    return PyLong_FromLong(self->fi.fieldCount);
}

static char fieldDesc_doc[] =
"f.fieldDesc(Value) -> String.\n\
\n\
Get description for field.\n\
Value can be both number and field name";

static PyObject *
File400_fieldDesc(File400Object *self, PyObject *args)
{
    int pos;
    PyObject *name;
    if (!PyArg_ParseTuple(args, "O:fieldDesc", &name))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    pos = f_getFieldPos(self, name);
    if (pos == -1) {
        Py_INCREF(Py_None);
        return Py_None;
    } else
        return PyUnicode_FromString(self->fieldArr[pos].desc);
}

static char fieldSize_doc[] =
"f.fieldSize(Value) -> Int.\n\
\n\
Get field size in bytes.\n\
Value can be both number and field name";

static PyObject *
File400_fieldSize(File400Object *self, PyObject *args)
{
    int pos;
    PyObject *name;
    if (!PyArg_ParseTuple(args, "O:fieldSize", &name))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    pos = f_getFieldPos(self, name);
    if (pos == -1) {
        Py_INCREF(Py_None);
        return Py_None;
    } else
        return PyLong_FromLong(self->fieldArr[pos].len);
}

static char fieldType_doc[] =
"f.fieldType(Value) -> String.\n\
\n\
Get field type.\n\
Value can be both number and field name";

static PyObject *
File400_fieldType(File400Object *self, PyObject *args)
{
    int pos, itype;
    PyObject *name;
    char *type;
    if (!PyArg_ParseTuple(args, "O:fieldType", &name))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    pos = f_getFieldPos(self, name);
    if (pos == -1) {
        Py_INCREF(Py_None);
        return Py_None;
    } else {
        itype = self->fieldArr[pos].type;
        if (itype == 0)
            type = "BINARY";
        else if (itype == 1)
            type = "FLOAT";
        else if (itype == 2)
            type = "ZONED";
        else if (itype == 3)
            type = "PACKED";
        else if (itype == 4)
            type = "CHAR";
        else if (itype == 0x8004)
            type = "VARCHAR";
        else if (itype == 5)
            type = "GRAPHIC";
        else if (itype == 6)
            type = "DBCS";
        else if (itype == 0x8005)
            type = "VARGRAPHIC";
        else if (itype == 0x8006)
            type = "VARDBCS";
        else if (itype == 11)
            type = "DATE";
        else if (itype == 12)
            type = "TIME";
        else if (itype == 13)
            type = "TIMESTAMP";
        else if (itype == 0x4004)
            type = "BLOB";
        else if (itype == 0x4005)
            type = "DBCLOB";
        else if (itype == 0x4006)
            type = "CLOBOPEN";
        else if (itype == 0x802c)
            type = "LINKCHAR";
        else if (itype == 0x802e)
            type = "LINKOPEN";
        else type = "UNKNOWN";
        return PyUnicode_FromString(type);
    }
}

static char fieldList_doc[] =
"f.fieldList([full=0]) -> Tuple.\n\
\n\
If full is False, returns a tuple of field names,\n\
else returns tuples with name, desc, type, size, digits, decimals, ccsid, alwnull, default.";

static PyObject *
File400_fieldList(File400Object *self, PyObject *args)
{
    PyObject *fld, *ftype, *fargs;
    int i, full = 0;
    fieldInfoStruct fi;

    if (!PyArg_ParseTuple(args, "|i:fieldList", &full))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    fld = PyTuple_New(self->fi.fieldCount);
    if (!fld)
        return NULL;
    for (i = 0; i < self->fi.fieldCount; i++) {
        if (full) {
            fi = self->fieldArr[i];
            fargs = Py_BuildValue("(i)", i);
            ftype = File400_fieldType(self, fargs);
            PyTuple_SetItem(fld, i, Py_BuildValue("ssOiiiiii", fi.name, fi.desc,
                            ftype, fi.len, fi.digits, fi.dec, fi.ccsid, fi.alwnull, fi.dft));
            Py_DECREF(fargs);
            Py_XDECREF(ftype);
        } else
            PyTuple_SetItem(fld, i, PyUnicode_FromString(self->fieldArr[i].name));
    }
    return fld;
}

static char keyCount_doc[] =
"f.keyCount() -> Int.\n\
\n\
Get number of key fields.";

static PyObject *
File400_keyCount(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":keyCount"))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    return PyLong_FromLong(self->fi.keyCount);
}

static char keyList_doc[] =
"f.keyList() -> Tuple.\n\
\n\
Get a list(tuple) of key field names.";

static PyObject *
File400_keyList(File400Object *self, PyObject *args)
{
    PyObject *key;
    int i;

    if (!PyArg_ParseTuple(args, ":keyList"))
        return NULL;
    if (!f_initialize(self))
        return NULL;
    key = PyTuple_New(self->fi.keyCount);
    if (!key)
        return NULL;
    for (i = 0; i < self->fi.keyCount; i++) {
        PyTuple_SetItem(key, i, PyUnicode_FromString(self->keyArr[i].name));
    }
    return key;
}

static char getBuffer_doc[] =
"f.getBuffer() -> String.\n\
\n\
Get the raw value of buffer as a String.\n\
The data is presented in the ccsid of the field or file.\n\
Same as getBuffer() for the record.";

static PyObject *
File400_getBuffer(File400Object *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":getBuffer"))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    return PyBytes_FromStringAndSize(self->recbuf, self->fi.recLen);
}

static char set_doc[] =
"f.set(Field, Value) -> String.\n\
\n\
Put value from Field into the buffer.\n\
Field can be both number and field name.\n\
Same as f['CUSNO'] = 123, or f[0] = 123, or f._CUSNO = 123";

static PyObject *
File400_set(File400Object *self, PyObject *args)
{
    PyObject *k, *o;
    int i, pos;

    if (!PyArg_ParseTuple(args, "O|O:set", &k, &o))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    /* if dict update fields in dict with values */
    if (PyDict_Check(k)) {
        PyObject *key, *value;
        Py_ssize_t dpos = 0;
        while (PyDict_Next(k, &dpos, &key, &value)) {
            pos = f_getFieldPos(self, key);
            if (pos >= 0) {
                i = f_setFieldValue(self, pos, value, self->recbuf);
                if (i)
                    return NULL;
            }
        }
    }
    /* one value */
    else {
        pos = f_getFieldPos(self, k);
        if (pos < 0) {
            PyErr_SetString(file400Error, "Parameter not valid.");
            return NULL;
        }
        else {
            i = f_setFieldValue(self, pos, o, self->recbuf);
        }
        if (i)
            return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char setBuffer_doc[] =
"f.setBuffer(String) -> None.\n\
\n\
Copy the value of string to the internal buffer.\n\
Use this with caution";

static PyObject *
File400_setBuffer(File400Object *self, PyObject *args)
{
    char *buf;
    int size;

    if (!PyArg_ParseTuple(args, "y#:setBuffer", &buf, &size))
        return NULL;
    if (!f_isOpen(self))
        return NULL;
    if (self->fi.recLen > size)
        memcpy(self->recbuf, buf, size);
    else
        memcpy(self->recbuf, buf, self->fi.recLen);
    Py_INCREF(Py_None);
    return Py_None;
}

static int
File400_length(File400Object *self)
{
    return self->fi.fieldCount;
}

static int
File400_ass_subscript(File400Object *self, PyObject *v, PyObject *w)
{
    int pos;
    if (!f_isOpen(self))
        return -1;
    if (w == NULL) {
        PyErr_SetString(file400Error, "NULL value not supported.");
        return -1;
    } else {
        pos = f_getFieldPos(self, v);
        if (pos < 0) {
            PyErr_SetString(file400Error, "Field not found.");
            return -1;
        } else
            return f_setFieldValue(self, pos, w, self->recbuf);
    }
}

static PyObject *
File400_subscript(File400Object *self, PyObject *v)
{
    int pos;
    if (!f_isOpen(self))
        return NULL;
    pos = f_getFieldPos(self, v);
    if (pos < 0) {
        PyErr_SetString(file400Error, "Parameter not valid.");
        return NULL;
    } else
        return f_getFieldValue(self, pos, self->recbuf);
}

static PyObject *
File400_iternext(File400Object *self)
{
    PyObject *res;
    res = File400_readne(self, Py_BuildValue("()"));
    if (res == NULL || PyLong_AS_LONG(res) == 1)
        return NULL;
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyMappingMethods File400_as_mapping = {
    (lenfunc)File400_length, /*mp_length*/
    (binaryfunc)File400_subscript, /*mp_subscript*/
    (objobjargproc)File400_ass_subscript, /*mp_ass_subscript*/
};

PyDoc_STRVAR(exit_doc,
	     "__exit__(*excinfo) -> None.  release lock.");

static PyObject *
File400_exit(File400Object *self, PyObject *args)
{
    if (!f_isOpen(self))
        return NULL;
    call_fileRlsLock(self->fileno);
    Py_INCREF(Py_None);
    return Py_None;
}
PyDoc_STRVAR(enter_doc,
	     "__enter__() -> self.");

static PyObject *
File400_self(File400Object *self)
{
	Py_INCREF(self);
	return (PyObject *)self;
}

static PyMethodDef File400Object_methods[] = {
    {"open",    (PyCFunction)File400_open,  METH_VARARGS|METH_KEYWORDS, open_doc},
    {"close",   (PyCFunction)File400_close, METH_VARARGS,close_doc},
    {"readn",   (PyCFunction)File400_readn, METH_VARARGS, readn_doc},
    {"readp",   (PyCFunction)File400_readp, METH_VARARGS, readp_doc},
    {"readrrn",  (PyCFunction)File400_readrrn,METH_VARARGS, readrrn_doc},
    {"readeq",  (PyCFunction)File400_readeq,METH_VARARGS|METH_KEYWORDS, readeq_doc},
    {"readne",  (PyCFunction)File400_readne,METH_VARARGS, readne_doc},
    {"readpe",  (PyCFunction)File400_readpe,METH_VARARGS, readpe_doc},
    {"readf",   (PyCFunction)File400_readf, METH_VARARGS|METH_KEYWORDS, readf_doc},
    {"readl",   (PyCFunction)File400_readl, METH_VARARGS|METH_KEYWORDS, readl_doc},
    {"posa",   (PyCFunction)File400_posa,   METH_VARARGS|METH_KEYWORDS, posa_doc},
    {"posb",   (PyCFunction)File400_posb,   METH_VARARGS|METH_KEYWORDS, posb_doc},
    {"posf",    (PyCFunction)File400_posf,  METH_VARARGS|METH_KEYWORDS, posf_doc},
    {"posl",    (PyCFunction)File400_posl,  METH_VARARGS|METH_KEYWORDS, posl_doc},
    {"write",   (PyCFunction)File400_write, METH_VARARGS, write_doc},
    {"delete",  (PyCFunction)File400_delete,METH_VARARGS, delete_doc},
    {"update",  (PyCFunction)File400_update,METH_VARARGS, update_doc},
    {"rlsLock", (PyCFunction)File400_rlsLock,METH_VARARGS, rlsLock_doc},
    {"clear",   (PyCFunction)File400_clear, METH_VARARGS, clear_doc},
    {"get",     (PyCFunction)File400_get, METH_VARARGS|METH_KEYWORDS, get_doc},
    {"getBuffer",(PyCFunction)File400_getBuffer, METH_VARARGS, getBuffer_doc},
    {"getRrn",  (PyCFunction)File400_getRrn, METH_VARARGS, getRrn_doc},
    {"isOpen",  (PyCFunction)File400_isOpen, METH_VARARGS, isOpen_doc},
    {"mode",    (PyCFunction)File400_mode,   METH_VARARGS, mode_doc},
    {"fileName",(PyCFunction)File400_fileName, METH_VARARGS, fileName_doc},
    {"libName", (PyCFunction)File400_libName, METH_VARARGS, libName_doc},
    {"fieldCount",(PyCFunction)File400_fieldCount, METH_VARARGS, fieldCount_doc},
    {"fieldList",(PyCFunction)File400_fieldList, METH_VARARGS, fieldList_doc},
    {"fieldDesc",(PyCFunction)File400_fieldDesc, METH_VARARGS, fieldDesc_doc},
    {"fieldSize",(PyCFunction)File400_fieldSize, METH_VARARGS, fieldSize_doc},
    {"fieldType",(PyCFunction)File400_fieldType, METH_VARARGS, fieldType_doc},
    {"recordSize",(PyCFunction)File400_recordSize, METH_VARARGS, recordSize_doc},
    {"keyCount",(PyCFunction)File400_keyCount, METH_VARARGS, keyCount_doc},
    {"keyList",(PyCFunction)File400_keyList, METH_VARARGS, keyList_doc},
    {"set", (PyCFunction)File400_set, METH_VARARGS, set_doc},
    {"setBuffer",(PyCFunction)File400_setBuffer, METH_VARARGS, setBuffer_doc},
    {"__exit__", (PyCFunction)File400_exit,METH_VARARGS, exit_doc},
	{"__enter__", (PyCFunction)File400_self,METH_NOARGS, enter_doc},
    {NULL}       /* sentinel */
};

static PyObject *
File400_getattro(File400Object *self, PyObject *nameobj)
{
    if (PyUnicode_Check(nameobj)) {
        if (PyUnicode_READ_CHAR(nameobj, 0) == '_' && PyUnicode_READ_CHAR(nameobj, 1) != '_')
            return File400_subscript(self, nameobj);
    }
    return PyObject_GenericGetAttr((PyObject *)self, nameobj);
}

static int
File400_setattro(File400Object *self, PyObject *nameobj, PyObject *v)
{
    if (PyUnicode_Check(nameobj)) {
        if (PyUnicode_READ_CHAR(nameobj, 0) == '_')
            return File400_ass_subscript(self, nameobj, v);
    }
    return -1;
}


static int
file400_initFile(File400Object *f)
{
    int result, bufsize;
    char name2[12];
    PyObject *obj;
    fieldInfoStruct *fi;
    fieldInfoStruct *ky;
    result = call_fileInit(f->fileno);
    if (!result)
        result = call_fileGetStruct(f->fileno, &f->fi);
    if (result == -1) {
        PyErr_SetString(file400Error, "File initialization failed.");
        return -1;
    }
    // allocate
    bufsize = f->fi.fieldCount * sizeof(fieldInfoStruct);
    f->fieldArr = fi = PyMem_Malloc(bufsize);
    result = call_fileGetFields(f->fileno, f->fieldArr, bufsize);
    if (result == -1) {
        PyErr_SetString(file400Error, "File initialization failed, getting field information.");
        return -1;
    }
    /* add to dictionary */
    f->fieldDict = PyDict_New();
    for (int i = 0; i < f->fi.fieldCount; i++) {
        Py_INCREF(Py_None);
        obj = PyLong_FromLong(i);
        // Save versions of the field name
        PyDict_SetItemString(f->fieldDict, fi->name, obj);
        strcpy(name2, fi->name);
        strtolower(name2);
        PyDict_SetItemString(f->fieldDict, name2, obj);
        sprintf(name2, "_%s", fi->name);
        PyDict_SetItemString(f->fieldDict, name2, obj);
        strtolower(name2);
        PyDict_SetItemString(f->fieldDict, name2, obj);
        Py_DECREF(obj);
        fi++;
    }
    /* get key info */
    if (f->fi.keyCount > 0) {
        bufsize = f->fi.keyCount * sizeof(fieldInfoStruct);
        f->keyArr = ky = PyMem_Malloc(bufsize);
        result = call_fileGetKeyFields(f->fileno, f->keyArr, bufsize);
        if (result == -1) {
            PyErr_SetString(file400Error, "File initialization failed, getting key information.");
            return -1;
        }
        f->keyDict = PyDict_New();
        for (int j = 0; j < f->fi.keyCount; j++) {
            obj = PyDict_GetItemString(f->fieldDict, ky->name);
            if (obj != NULL) {
                strcpy(ky->desc, f->fieldArr[PyLong_AS_LONG(obj)].desc);
                ky->ccsid = f->fieldArr[PyLong_AS_LONG(obj)].ccsid;
            } else {
                *ky->desc = '\0';
                ky->ccsid = 0;
            }
            obj = PyLong_FromLong(j);
            PyDict_SetItemString(f->keyDict, ky->name, obj);
            sprintf(name2, "_%s", ky->name);
            PyDict_SetItemString(f->keyDict, name2, obj);
            Py_DECREF(obj);
            ky++;
        }
    }
    // allocate storage for record
    f->recbuf = PyMem_Malloc(f->fi.recLen + 1);
    // clear the record
    f_clear(f, f->recbuf);
    /* return ok */
    return 0;
}

char File400_doc[] =
"File400(Filename[mode, lib, mbr]) -> File400 Object\n\
\n\
Creates a new File400 Object.\n\
Filename must exist.\n\
mode - Open mode\n\
    None - (default) read.\n\
    'r'  - open for read.\n\
    'a'  - open for append.\n\
    'r+' - open for read, update and append.\n\
lib     - Library, special values are *LIBL(default) and *CURLIB.\n\
member  - Member to be opened, special value are *FIRST(default).\n\
\n\
Methodes:\n\
  open       - Open file.\n\
  close      - Close file.\n\
  read..     - Read from file.\n\
  pos..      - Position in file.\n\
  write      - Write new record to file.\n\
  update     - Update current record.\n\
  rlsLock    - Release lock on current record.\n\
  clear      - Clear the record buffer.\n\
  get        - Get values from record buffer.\n\
  getRrn     - Get relative record number.\n\
  mode       - Returns open mode (r, a, r+).\n\
  fileName   - Returns Name of the file.\n\
  libName    - Returns the library name.\n\
  fieldCount - Number of fields in file.\n\
  fieldList  - Returns tuple of field names.\n\
  fieldDesc  - Get field description.\n\
  fieldSize  - Get field size.\n\
  fieldType  - Get field type.\n\
  recordSize - Record size in bytes.\n\
  keyCount   - Number of key fields.\n\
  keyList    - Returns tuple of key field names.\n\
  set        - Set field values into record buffer.\n\
  setKey     - Set key values into key buffer.\n\
\n\
See the __doc__ string on each method for details.\n\
>>> f = File400('YOURFILE')\n\
>>> print f.open__doc__";

static PyObject*
File400_new(PyTypeObject *type, PyObject *args, PyObject *keywds)
{
    char *file;
    char *lib = "*LIBL";
    char *mbr = "*FIRST";
    int omode;
    static char *kwlist[] = {"file", "mode", "lib", "mbr", NULL};
    PyObject *mode = Py_None;
    File400Object *nf;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "s|Oss:File400", kwlist, &file, &mode, &lib, &mbr))
        return NULL;
    if (strlen(file) > 10 || strlen(lib) > 10 || strlen(mbr) > 10) {
        PyErr_SetString(file400Error, "File,Lib and Member have max length of 10.");
        return NULL;
    }
    if (mode == Py_None) {
        omode = OPEN_READ;
    } else {
        char *smode;
        if (!PyUnicode_Check(mode)) {
            PyErr_SetString(file400Error, "Open mode not a valid type.");
            return NULL;
        }
        smode = PyUnicode_AsUTF8(mode);
        if (!strcmp(smode, "r")) {
            omode = OPEN_READ;
        } else if (!strcmp(smode, "a")) {
            omode = OPEN_WRITE;
        } else if (!strcmp(smode, "r+")) {
            omode = OPEN_UPDATE;
        } else {
            PyErr_SetString(file400Error, "Open mode not valid.");
            return NULL;
        }
    }
    nf = PyObject_New(File400Object, &File400_Type);
    if (nf == NULL)
        return NULL;
    nf->fileno = call_fileNew(file, lib, mbr, omode);
    if (nf->fileno < 0) {
        PyErr_SetString(file400Error, "Failed creating File400 object.");
        return NULL;
    }
    strcpy(nf->fi.name, file);
    strcpy(nf->fi.lib, lib);
    nf->fieldArr = NULL;
    nf->keyArr = NULL;
    nf->fieldDict = NULL;
    nf->keyDict = NULL;
    nf->recbuf = NULL;
    return (PyObject *) nf;
}

static PyObject*
setFieldtype(PyObject* self, PyObject* args)
{
    PyObject *func;
    if (!PyArg_ParseTuple(args, "O", &func))
        return NULL;
    if (func != Py_None && !PyCallable_Check(func)) {
        PyErr_SetString(file400Error, "Function is not callable.");
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

PyTypeObject File400_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "File400",
    .tp_doc = File400_doc,
    .tp_basicsize = sizeof(File400Object),
    .tp_dealloc = (destructor)File400_dealloc,
    .tp_as_mapping = &File400_as_mapping,
    .tp_getattro = (getattrofunc)File400_getattro,
    .tp_setattro = (setattrofunc)File400_setattro,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_methods = File400Object_methods,
	.tp_iter = (getiterfunc)File400_self,
	.tp_iternext = (iternextfunc)File400_iternext,
	.tp_new = File400_new
};

/* List of functions defined in the module */
static PyMethodDef file400_memberlist[] = {
    {"setFieldtypeFunction", (PyCFunction)setFieldtype, METH_VARARGS, "Set factory function for field types."},
    {NULL}
};

static struct PyModuleDef moduledef = {
		PyModuleDef_HEAD_INIT,
		"file400",
		"IBM i record level access",
		-1,
		file400_memberlist,
	};

/* Initialization function for the module */
PyMODINIT_FUNC
PyInit_file400(void)
{
    PyObject *m;
    char *lib;
    /* Create the module and add the functions */
	if (PyType_Ready(&File400_Type) < 0) {
        Py_FatalError("Failed in File400 type ready");
		return NULL;
	}
	m = PyModule_Create(&moduledef);
    /* Add some symbolic constants to the module */
    file400Error = PyErr_NewException("file400.error", NULL, NULL);
    PyModule_AddObject(m, "Error", file400Error);
    Py_INCREF(&File400_Type);
    PyModule_AddObject(m, "File400", (PyObject *)&File400_Type);
    if (PyErr_Occurred() ) {
        Py_FatalError("Can not initialize file400");
		return NULL;
    }
    lib = getenv("PY3RECLEVACC");
    if (lib)
        strncpy(reclevacc_lib, lib, 10);
    else
        strcpy(reclevacc_lib, "PYTHON3");
    fileRowClass = PyObject_CallFunctionObjArgs((PyObject *)&PyType_Type, PyUnicode_FromString("FileRow"),
         PyTuple_New(0), PyDict_New(), NULL);
	// return module
    return m;
}
