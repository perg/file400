/*
 * reclevacc  Record level access
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
 */

#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <xxcvt.h>
#include <qusec.h>
#include <qdbrtvfd.h>
#include <iconv.h>
#include <qtqiconv.h>
#include <float.h>
#include <recio.h>

/* Open modes */
#define OPEN_READ 10
#define OPEN_UPDATE 12
#define OPEN_WRITE 14
#define _FILE_MAX 2048
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

/* Internal File info */
typedef struct {
	int       fileno;
	char      name[11];
	char      lib[11];
	char      mbr[11];
	int       omode;
	int       lmode;
	char      *recbuf;	/* record buffer */
	char      *tmpbuf;	/* record buffer */
	char      *keybuf;	/* key buffer */
	char      recName[11];
	char      recId[14];
	int       recLen;
	int       keyLen;
	int       fieldCount;
	int       keyCount;
	int       curKeyLen;
	fieldInfoStruct *fieldArr;
	fieldInfoStruct *keyArr;
} IntFileInfo;

/* Internal File info */
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
    IntFileInfo  *fi;
    _RFILE    *fp;
} FileHead;

static FileHead *fileArr[_FILE_MAX] = {NULL};
/* hold utf conversion descriptors */
static int utfInit = 0;
static iconv_t cdToUtf;
static iconv_t cdFromUtf;

int fileInit(int fileno);
int fileOpen(int fileno);

// Init an open if not already done
#pragma inline(f_init)
static int
f_init(FileHead *fh)
{
    IntFileInfo *fi = fh->fi;
    if (!fi->recbuf) {
        if (fileInit(fi->fileno) < 0)
            return -1;
    }
    return 0;
}

#pragma inline(f_open)
static int
f_open(FileHead *fh) {
    if (!fh->fp) {
        if (fh->fi->omode != 0) {
            if (fileOpen(fh->fi->fileno) < 0)
                return -1;
        } else {
            fprintf(stderr, "File not open. %s/%s\n", fh->fi->lib, fh->fi->name);
            return -1;
        }
    }
    return 0;
}

/* returns conversion descriptors  */
static iconv_t
initConvert(int fromccsid, int toccsid) {
    QtqCode_T qtfrom, qtto;
    memset(&qtfrom, 0x00, sizeof(qtfrom));
    memset(&qtto, 0x00, sizeof(qtto));
    qtfrom.CCSID = fromccsid;
    qtto.CCSID = toccsid;
    return QtqIconvOpen(&qtto, &qtfrom);
}

static void
initUtf(void)
{
    cdFromUtf = initConvert(1208, 0);
    cdToUtf = initConvert(0, 1208);
    utfInit = 1;
}

/* convert as/400 string to utf, returns length not including ending NULL */
static int
strLenToUtf(char *in, int len, char *out)
{
    char *__ptr128 p1, *__ptr128 p2;
    size_t sizefrom, sizeto;
    char *p;
    /* remove trailing blanks */
    p = in + len - 1;
    while (p >= in && *p == 0x40)
        p--;
    sizefrom = (p - in) + 1;
    if (sizefrom <= 0) {
        *out = '\0';
        return 0;
    }
    if (utfInit == 0) initUtf();
    p1 = in;
    p2 = out;
    sizeto = len;
    iconv(cdToUtf, &p1, &sizefrom, &p2, &sizeto);
    if (errno == E2BIG)
        errno = 0;
    *p2 = '\0';
    return len - sizeto;
}

/* convert string from utf to job ccsid padded with blank up to len */
static char *
utfToStrLen(char *in, char *out, int len, int add_null)
{
    char *__ptr128 p1, *__ptr128 p2;
    size_t sizefrom, sizeto;
    if (*in != '\0') {
        if (utfInit == 0) initUtf();
        p1 = in;
        p2 = out;
        sizefrom = strlen(in);
        sizeto = len;
        iconv(cdFromUtf, &p1, &sizefrom, &p2, &sizeto);
        if (errno == E2BIG)
            errno = 0;
        if (sizeto > 0)
            memset(p2, 0x40, sizeto);
    } else
        memset(out, 0x40, len);
    if (add_null == 1)
        out[len] = '\0';
    return out;
}

/* get key length and update key buffer */
static int
f_setKey(FileHead *fh, char *keyval, int keyLen) {
    IntFileInfo *fi = fh->fi;
    if (keyLen <= 0) {
        // Use current key
        if (fi->curKeyLen <= 0) {
            fprintf(stderr, "Key not valid %s/%s\n", fi->lib, fi->name);
            return -1;
        }
        return 0;
    }
    memcpy(fh->fp->riofb.key, keyval, keyLen);
    memcpy(fi->keybuf, keyval, keyLen);
    fi->curKeyLen = keyLen;
    return 0;
}

/* convert string to upper */
static char * strtoupper(char *s)
{
    int c = 0;
    while (s[c] != '\0') {
        if (s[c] >= 'a' && s[c] <= 'z') {
            s[c] = s[c] - 32;
        }
        c++;
    }
}

int int2zoned(char *buf, int len, int dec, int value) {
    QXXITOZ(buf, len, dec, value);
    return 0;
}

int float2zoned(char *buf, int len, int dec, double value) {
    QXXDTOZ(buf, len, dec, value);
    return 0;
}

int int2packed(char *buf, int len, int dec, int value) {
    QXXITOP(buf, len, dec, value);
    return 0;
}

int float2packed(char *buf, int len, int dec, double value) {
    QXXDTOP(buf, len, dec, value);
    return 0;
}

int fileNew(char *name, char *lib, char *mbr, int mode) {
    int fileno;
    FileHead *fh;
    IntFileInfo *fi;

    for (fileno=0; fileno < _FILE_MAX; fileno++) {
        fh = fileArr[fileno];
        if (fh == NULL) {
            fh = malloc(sizeof(FileHead));
            fh->fi = malloc(sizeof(IntFileInfo));
            fh->fp = NULL;
            fi = fh->fi;
            fi->fileno = fileno;
            fileArr[fileno] = fh;
            break;
        }
    }
    if (fileno == _FILE_MAX) {
        fprintf(stderr, "Maximum number of open files reached\n");
        return -1;
    }
    strtoupper(strcpy(fi->name, name));
    strtoupper(strcpy(fi->lib, lib));
    strtoupper(strcpy(fi->mbr, mbr));
    fi->lmode = -1;
    fi->omode = mode;
    fi->recbuf = NULL;
    fi->tmpbuf = NULL;
    fi->keybuf = NULL;
    fi->fieldArr = NULL;
    fi->keyArr = NULL;
    return fileno;
}

int fileInit(int fileno) {
    FileHead *fh;
    IntFileInfo *fi;
    char *p, retFileLib[20], fileLib[21];
    Qus_EC_t error;
    Qdb_Qddfmt_t *foHd;
    Qdb_Qddffld_t *fiHd;
    Qdb_Qdbwh_t *kyHd;
    Qdb_Qdbwhkey_t *kyKey;
    char *fibuff, *kibuff;
    int i, size, fibuffsize;
    fieldInfoStruct *fieldInfo;
    fieldInfoStruct *keyInfo;

    // Get file structure
    fh = fileArr[fileno];
    fi = fh->fi;
    // File information
    error.Bytes_Provided = sizeof(error);
    //
    // File information
    utfToStrLen(fi->name, fileLib, 10, 0);
    utfToStrLen(fi->lib, fileLib + 10, 10, 1);
    fibuff = malloc(100000);
    fibuffsize = 100000;
#pragma convert(37)
    QDBRTVFD(fibuff, fibuffsize, retFileLib, "FILD0200", fileLib,
                 "*FIRST    ", "0", "*LCL      ", "*EXT      ", &error);
#pragma convert(0)
    if (error.Bytes_Available > 0) {
        free(fibuff);
        return -1;
    }
    foHd = (Qdb_Qddfmt_t *) fibuff;
    if (foHd->Qddbyava > fibuffsize) {
        fibuffsize = foHd->Qddbyava;
        realloc(fibuff, fibuffsize);
#pragma convert(37)
        QDBRTVFD(fibuff, fibuffsize, retFileLib, "FILD0200", fileLib,
                 "          ", "0", "*LCL      ", "*EXT      ", &error);
#pragma convert(0)
        if (error.Bytes_Available > 0) {
            free(fibuff);
            return -1;
        }
        foHd = (Qdb_Qddfmt_t *) fibuff;
    }
    /* get record info */
    strLenToUtf(retFileLib + 10, 10, fi->lib);
    strLenToUtf(foHd->Qddfname, 10, fi->recName);
    strLenToUtf(foHd->Qddfseq, 13, fi->recId);
    fi->recLen = foHd->Qddfrlen;
    /* get field info */
    fi->fieldCount = foHd->Qddffldnum;
    fi->fieldArr = fieldInfo = malloc(fi->fieldCount * sizeof(fieldInfoStruct));
    p = fibuff + sizeof(Qdb_Qddfmt_t);
    fiHd = (Qdb_Qddffld_t *) p;
    for (i = 0; i < fi->fieldCount; i++) {
        strLenToUtf(fiHd->Qddffldi, 10, fieldInfo->name);
        memcpy(&fieldInfo->type, fiHd->Qddfftyp, 2);
        fieldInfo->offset = fiHd->Qddffobo;
        fieldInfo->len = fiHd->Qddffldb;
        fieldInfo->digits = fiHd->Qddffldd;
        fieldInfo->dec = fiHd->Qddffldp;
        fieldInfo->ccsid = fiHd->Qddfcsid;
        fieldInfo->alwnull = fiHd->Qddffldst2.Qddffnul;
        fieldInfo->dft = fiHd->Qddfdftd;
        /* description */
        strLenToUtf(p + fiHd->Qddftxtd, 50, fieldInfo->desc);
        /* next field */
        fieldInfo++;
        p += fiHd->Qddfdefl;
        fiHd = (Qdb_Qddffld_t *) p;
    }
    /* free allocated storage */
    free(fibuff);
    // Key field information
    fi->keyCount = 0;
    fi->keyLen = 0;
    fi->curKeyLen = 0;
    kibuff = malloc(1024);
#pragma convert(37)
    QDBRTVFD(kibuff, 1024, retFileLib, "FILD0300", fileLib,
             "          ", "0", "*LCL      ", "*EXT      ", &error);
#pragma convert(0)
    if (error.Bytes_Available == 0) {
        kyHd = (Qdb_Qdbwh_t *) kibuff;
        if (kyHd->Byte_Avail > 1024) {
            realloc(kibuff, kyHd->Byte_Avail);
            kyHd = (Qdb_Qdbwh_t *) kibuff;
    #pragma convert(37)
            QDBRTVFD(kibuff, kyHd->Byte_Avail, retFileLib, "FILD0300", fileLib,
                     "          ", "0", "*LCL      ", "*EXT      ", &error);
    #pragma convert(0)
            if (error.Bytes_Available > 0) {
                free(kibuff);
                return -1;
            }
        }
        p = kibuff;
        /* find offset to first record format */
        fi->keyCount = kyHd->Rec_Key_Info->Num_Of_Keys;
        if (fi->keyCount > 0) {
            p += kyHd->Rec_Key_Info->Key_Info_Offset;
            kyKey = (Qdb_Qdbwhkey_t *) p;
            fi->keyArr = keyInfo = malloc(fi->keyCount * sizeof(fieldInfoStruct));
            for (int j = 0; j < fi->keyCount; j++) {
                strLenToUtf(kyKey->Int_Field_Name, 10, keyInfo->name);
                keyInfo->type = kyKey->Data_Type;
                keyInfo->offset = fi->keyLen;
                keyInfo->len = kyKey->Field_Len;
                keyInfo->digits = kyKey->Num_Of_Digs;
                keyInfo->dec = kyKey->Dec_Pos;
                /* get info from fieldArr */
                for (i = 0; i < fi->fieldCount; i++) {
                    fieldInfo = &fi->fieldArr[i];
                    if (!strcmp(fieldInfo->name, keyInfo->name)) {
                        strcpy(keyInfo->desc, fieldInfo->desc);
                        keyInfo->ccsid = fieldInfo->ccsid;
                        break;
                    }
                    fieldInfo++;
                }
                fi->keyLen += keyInfo->len;
                keyInfo++;
                kyKey++;
            }
        }
        fi->keybuf = malloc(fi->keyLen + 1);
    }
    free(kibuff);
    fi->recbuf = malloc(fi->recLen + 1);
    fi->tmpbuf = malloc(fi->recLen + 1);
    /* return ok */
    return 0;
}

int fileOpen(int fileno) {
    char openKeyw[100];
    char fullName[35];
    FileHead *fh;
    IntFileInfo *fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (!fi->recbuf) {
        if (fileInit(fileno) < 0)
            return -1;
    }
    sprintf(fullName, "%s/%s(%s)", fi->lib, fi->name, fi->mbr);
    if (fi->omode == OPEN_READ) {
        strcpy(openKeyw, "rr");
    } else if (fi->omode == OPEN_WRITE) {
        strcpy(openKeyw, "ar");
    } else if (fi->omode == OPEN_UPDATE) {
        strcpy(openKeyw, "rr+");
    }
    strcat(openKeyw, ", ccsid=65535");
    if (fi->omode == OPEN_READ)
        strcat(openKeyw, ", blkrcd=Y");
    fh->fp = _Ropen(fullName, openKeyw);
    if (!fh->fp) {
        fprintf(stderr, "Open failed %s/%s\n", fi->lib, fi->name);
        return -1;
    }
    /* check record length */
    if (fh->fp->buf_length != fi->recLen) {
        fprintf(stderr, "Record size check failed. %s/%s\n", fi->lib, fi->name);
        return -1;
    }
    /* clear the record */
    if (fi->omode != OPEN_WRITE) {
        /* position to before first will also initialize iofb */
        _Rlocate(fh->fp, NULL, 0, __START);
    }
    return 0;
}

int fileGetStruct(int fileno, FileInfo *ei) {
    FileHead *fh;
    IntFileInfo *fi;
    fh = fileArr[fileno];
    fi = fh->fi;
    strncpy(ei->name, fi->name, 11);
    strncpy(ei->lib, fi->lib, 11);
    ei->omode = fi->omode;
    ei->recLen = fi->recLen;
    ei->keyLen = fi->keyLen;
    ei->fieldCount = fi->fieldCount;
    ei->keyCount = fi->keyCount;
    return 0;
}

int fileGetFields(int fileno, char *buf, int size) {
    FileHead *fh;
    fh = fileArr[fileno];
    memcpy(buf, (char *)fh->fi->fieldArr, size);
    return 0;
}

int fileGetKeyFields(int fileno, char *buf, int size) {
    FileHead *fh;
    fh = fileArr[fileno];
    memcpy(buf, (char *)fh->fi->keyArr, size);
    return 0;
}

int fileGetData(int fileno, char *buf, int size) {
    FileHead *fh;
    fh = fileArr[fileno];
    memcpy(buf, fh->fi->recbuf, size);
    return 0;
}

int fileClose(int fileno) {
    FileHead *fh;
    IntFileInfo *fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (fh->fp && _Rclose(fh->fp)) {
        fprintf(stderr, "Error closing file. %s/%s\n", fh->fi->lib, fh->fi->name);
        return -1;
    }
    fh->fp = NULL;
    return 0;
}

int fileFree(int fileno) {
    FileHead *fh;
    IntFileInfo *fi;

    fh = fileArr[fileno];
    if (fh) {
        fi = fh->fi;
        if (fi->recbuf) free(fi->recbuf);
        if (fi->tmpbuf) free(fi->tmpbuf);
        if (fi->keybuf) free(fi->keybuf);
        if (fi->keyArr) free(fi->keyArr);
        if (fi->fieldArr) free(fi->fieldArr);
        if (fh->fp) _Rclose(fh->fp);
        free(fh->fi);
        free(fh);
        fileArr[fileno] = NULL;
    }
    return 0;
}

int fileClear(int fileno) {
    int i;
    char *p, *buf;
    short *s, *e;
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    buf = fi->recbuf;
    for (i = 0; i < fi->fieldCount; i++) {
        p = buf + fi->fieldArr[i].offset;
        switch(fi->fieldArr[i].type) {
        /* binary, float*/
        case 0: case 1:
            memset(p, 0x00, fi->fieldArr[i].len);
            break;
        /* zoned */
        case 2:
            memset(p, 0xf0, fi->fieldArr[i].len);
            break;
        /* packed */
        case 3:
            memset(p, 0x00, fi->fieldArr[i].len);
            p += fi->fieldArr[i].len - 1;
            memset(p, 0x0f, 1);
            break;
        /* char */
        case 4:
            if (fi->fieldArr[i].ccsid == 1208)
                memset(p, 0x20, fi->fieldArr[i].len);
            else
                memset(p, 0x40, fi->fieldArr[i].len);
            break;
        /* Grapics (unicode) */
        case 5:
            s = (short *)p;
            e = s + fi->fieldArr[i].len / 2;
            while (s < e) {
                *s = 0x0020;
                s++;
            }
            break;
        /* date, time, timestamp */
        case 11: case 12: case 13:
            memset(p, 0x00, fi->fieldArr[i].len);
            break;
        /* varchar */
        case 0x8004: case 0x8005:
            memset(p, 0x00, fi->fieldArr[i].len);
            break;
        default:
            memset(p, 0x00, fi->fieldArr[i].len);
        }
    }
    return 0;
}

int fileRlsLock(int fileno) {
    FileHead * fh;
    fh = fileArr[fileno];
    if (fh->fp && fh->fi->omode == OPEN_UPDATE)
        _Rrlslck(fh->fp);
    return 0;
}

int filePosb(int fileno, char *key, int keyLen, int lock) {
    int found = 0;
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    fi->lmode = lock;
    if (f_setKey(fh, key, keyLen) < 0)
        return -1;
    errno = 0;
    _Rlocate(fh->fp, fh->fp->riofb.key, keyLen, __KEY_EQ|__PRIOR|__NO_LOCK);
    if (fh->fp->riofb.num_bytes == 0) {
        _Rlocate(fh->fp, fh->fp->riofb.key, keyLen, __KEY_GT|__PRIOR|__NO_LOCK);
        if (fh->fp->riofb.num_bytes == 0)
            _Rlocate(fh->fp, NULL, 0, __END);
    } else
        found = 1;
    if (errno != 0 && errno != EIORECERR) {
        fprintf(stderr, "%s %s/%s\n", strerror(errno), fi->lib, fi->name);
        return -1;
    }
    fh->fp->riofb.blk_count = 0;
    return found;
}

int filePosa(int fileno, char *key, int keyLen, int lock) {
    int  found = 0;
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    fi->lmode = lock;
    if (f_setKey(fh, key, keyLen) < 0)
        return -1;
    errno = 0;
    _Rlocate(fh->fp, fh->fp->riofb.key, keyLen, __KEY_GT|__PRIOR|__NO_LOCK);
    if (fh->fp->riofb.num_bytes == 0)
        _Rlocate(fh->fp, NULL, 0, __END);
    if (errno != 0 && errno != EIORECERR) {
        fprintf(stderr, "%s %s/%s\n", strerror(errno), fi->lib, fi->name);
        return -1;
    }
    fh->fp->riofb.blk_count = 0;
    return 0;
}

int filePosf(int fileno, int lock) {
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    fi->lmode = lock;
    fi->curKeyLen = 0;
    errno = 0;
    _Rlocate(fh->fp, NULL, 0, __START);
    if (errno != 0 && errno != EIORECERR) {
        fprintf(stderr, "%s %s/%s\n", strerror(errno), fi->lib, fi->name);
        return -1;
    }
    fh->fp->riofb.blk_count = 0;
    return 0;
}

int filePosl(int fileno, int lock) {
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    fi->lmode = lock;
    fi->curKeyLen = 0;
    errno = 0;
    _Rlocate(fh->fp, NULL, 0, __END);
    if (errno != 0 && errno != EIORECERR) {
        fprintf(stderr, "%s %s/%s\n", strerror(errno), fi->lib, fi->name);
        return -1;
    }
    fh->fp->riofb.blk_count = 0;
    return 0;
}

int fileReadeq(int fileno, char *recbuf, char *key, int keyLen, int lock) {
    int keyOpt, lockOpt = __DFT;
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    if (lock == 0) {
        lockOpt = __NO_LOCK;
        if (fi->omode == OPEN_UPDATE)
            _Rrlslck(fh->fp);
    }
    fi->lmode = lock;
    if (f_setKey(fh, key, fi->keyLen) < 0)
        return -1;
    errno = 0;
    keyOpt = (lockOpt == __DFT) ? __KEY_EQ : __KEY_EQ | lockOpt;
    _Rreadk(fh->fp, fi->recbuf, fi->recLen, keyOpt, fh->fp->riofb.key, keyLen);
    if (errno != 0 && errno != EIORECERR) {
        fprintf(stderr, "%s %s/%s\n", strerror(errno), fi->lib, fi->name);
        return -1;
    }
    if (fh->fp->riofb.num_bytes == fi->recLen) {
        memcpy(recbuf, fi->recbuf, fi->recLen);
        return 0;
    }
    return 1;
}

int fileReadrrn(int fileno, char *recbuf, int rrn, int lock) {
    int keyOpt, lockOpt = __DFT;
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    if (lock == 0) {
        lockOpt = __NO_LOCK;
        if (fi->omode == OPEN_UPDATE)
            _Rrlslck(fh->fp);
    }
    errno = 0;
    _Rreadd(fh->fp, fi->recbuf, fi->recLen, lockOpt, rrn);
    if (errno != 0 && errno != EIORECERR) {
        fprintf(stderr, "%s %s/%s\n", strerror(errno), fi->lib, fi->name);
        return -1;
    }
    if (fh->fp->riofb.num_bytes == fi->recLen) {
        memcpy(recbuf, fi->recbuf, fi->recLen);
        return 0;
    }
    return 1;
}

static int
f_readCommon(FileHead *fh, char *recbuf, int oper, int lock, int keyLen) {
    int lockOpt = __DFT;
    IntFileInfo *fi = fh->fi;
    // Open if not already opened
    if (f_open(fh) < 0) return -1;
    if (lock == -1) {
        if (fi->lmode != -1)
            lock = fi->lmode;
        else
            lock = 1;
    }
    if (lock == 0) {
        lockOpt = __NO_LOCK;
        if (fi->omode == OPEN_UPDATE)
            _Rrlslck(fh->fp);
    }
    // readn/readne
    if (oper == 11 || oper == 21) {
        /* locate if block count and last read was previous */
        if (fh->fp->riofb.blk_filled_by == __READ_PREV && fh->fp->riofb.blk_count > 0) {
            fprintf(stderr, "Not valid to reverse read sequence when in block mode. %s/%s\n", fi->lib, fi->name);
            return -1;
        }
    }
    // readp/readpe
    if (oper == 12 || oper == 22) {
        /* locate if block count and last read was previous */
        if (fh->fp->riofb.blk_filled_by == __READ_NEXT && fh->fp->riofb.blk_count > 0) {
            fprintf(stderr, "Not valid to reverse read sequence when in block mode. %s/%s\n", fi->lib, fi->name);
            return -1;
        }
    }
    // readne/readpe
    if ((oper == 21 || oper == 22) && keyLen > 0) {
        // set key from previous read or parameter
        fi->curKeyLen = keyLen;
    }
    errno = 0;
    switch (oper) {
        case 1:
            _Rreadf(fh->fp, fi->recbuf, fi->recLen, lockOpt);
            break;
        case 2:
            _Rreadl(fh->fp, fi->recbuf, fi->recLen, lockOpt);
            break;
        case 11:
        case 21:
            _Rreadn(fh->fp, fi->recbuf, fi->recLen, lockOpt);
            break;
        case 12:
        case 22:
            _Rreadp(fh->fp, fi->recbuf, fi->recLen, lockOpt);
            break;
    }
    if (errno != 0 && errno != EIORECERR) {
        fprintf(stderr, "%s %s/%s\n", strerror(errno), fi->lib, fi->name);
        return -1;
    }
    // readne/readpe
    if (oper == 21 || oper == 22) {
        if (fh->fp->riofb.num_bytes < 0 || memcmp(fi->keybuf, fh->fp->riofb.key, fi->curKeyLen) != 0) {
            fh->fp->riofb.num_bytes = 0;
            if (fi->omode == OPEN_UPDATE && lockOpt == __DFT)
                _Rrlslck(fh->fp);
            return 1;
        }
    }
    if (fh->fp->riofb.num_bytes == fi->recLen) {
        memcpy(recbuf, fi->recbuf, fi->recLen);
        return 0;
    }
    return 1;
}

int fileReadf(int fileno, char *recbuf, int lock) {
    return f_readCommon(fileArr[fileno], recbuf, 1, lock, 0);
}

int fileReadl(int fileno, char *recbuf, int lock) {
    return f_readCommon(fileArr[fileno], recbuf, 2, lock, 0);
}

int fileReadn(int fileno, char *recbuf, int lock) {
    return f_readCommon(fileArr[fileno], recbuf, 11, lock, 0);
}

int fileReadp(int fileno, char *recbuf, int lock) {
    return f_readCommon(fileArr[fileno], recbuf, 12, lock, 0);
}

int fileReadne(int fileno, char *recbuf, int keyLen, int lock) {
    return f_readCommon(fileArr[fileno], recbuf, 21, lock, keyLen);
}

int fileReadpe(int fileno, char *recbuf, int keyLen, int lock) {
    return f_readCommon(fileArr[fileno], recbuf, 22, lock, keyLen);
}

int fileWrite(int fileno, char *buf) {
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    memcpy(fi->recbuf, buf, fi->recLen);
    /* save the key and restore it after write */
    if (fi->keyLen > 0)
        memcpy(fi->keybuf, fh->fp->riofb.key, fi->keyLen);
    _Rwrite(fh->fp, fi->recbuf, fi->recLen);
    if (fh->fp->riofb.num_bytes < fi->recLen) {
        fprintf(stderr, "Error writing record. %s/%s\n", fi->lib, fi->name);
        return -1;
    }
    if (fi->keyLen > 0)
        memcpy(fh->fp->riofb.key, fi->keybuf, fi->keyLen);
    return 0;
}

int fileUpdate(int fileno, char *buf) {
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    memcpy(fi->recbuf, buf, fi->recLen);
    /* save the key and restore it after update */
    if (fi->keyLen > 0)
        memcpy(fi->keybuf, fh->fp->riofb.key, fi->keyLen);
    _Rupdate(fh->fp, fi->recbuf, fi->recLen);
    if (fh->fp->riofb.num_bytes < fi->recLen) {
        fprintf(stderr, "Error updating record. %s/%s\n", fi->lib, fi->name);
        return -1;
    }
    if (fi->keyLen > 0)
        memcpy(fh->fp->riofb.key, fi->keybuf, fi->keyLen);
    return 0;
}

int fileDelete(int fileno) {
    FileHead * fh;
    IntFileInfo * fi;

    fh = fileArr[fileno];
    fi = fh->fi;
    if (f_open(fh) < 0) return -1;
    /* save the key and restore it after delete */
    if (fi->keyLen > 0)
        memcpy(fi->keybuf, fh->fp->riofb.key, fi->keyLen);
    _Rdelete(fh->fp);
    if (fh->fp->riofb.num_bytes == 0) {
        fprintf(stderr, "Error deleting record. %s/%s\n", fi->lib, fi->name);
        return -1;
    }
    if (fi->keyLen > 0)
        memcpy(fh->fp->riofb.key, fi->keybuf, fi->keyLen);
    return 0;
}

int fileGetRrn(int fileno) {
    FileHead * fh;
    fh = fileArr[fileno];
    return fh->fp->riofb.rrn;
}
