/*
 * _db2  Database access
 *
 *--------------------------------------------------------------------
 * Copyright (c) 2010-2019 by Per Gummedal.
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
#include "structmember.h"
#include <sqlcli.h>
#define STR_LEN 255

#define USE_DECIMAL 0

/* output */
#define LIST 0
#define OBJ 1
#define DICT 2

/* Cursor counter */
static int cursor_count = 0;
/* Error Objects */
static PyObject *dbError;
static PyObject *dbWarning;

/* Factory functions for date/time etc */
static PyObject * DateFunc = NULL;
static PyObject * TimeFunc = NULL;
static PyObject * DatetimeFunc = NULL;
static PyObject * FieldtypeFunc = NULL;
static PyObject * decimalcls = NULL;

/* Field information structure */
typedef struct {
	char        name[STR_LEN];
    SQLSMALLINT type;
    SQLINTEGER  prec;
    SQLSMALLINT scale;
    SQLSMALLINT nulls;
    int         ctype;
	int         offset;
	int         size;
} fieldInfoStruct;

/* Parameter structure */
typedef struct {
    SQLSMALLINT type;
    SQLINTEGER  prec;
    SQLSMALLINT scale;
    SQLSMALLINT nulls;
    SQLINTEGER  ctype;
    SQLPOINTER  data;
    PyObject    *obj;
	SQLINTEGER  size;
} paramInfoStruct;

/* Connection object */
typedef struct {
    PyObject_HEAD
    SQLHENV henv;
    SQLHDBC hdbc;
    SQLRETURN rc;
    char *dsn;
    char *user;
    char *pwd;
    char *database;
} ConnectionObject;

/* Cursor description Object */
typedef struct {
    PyObject_HEAD
    PyObject *fieldDict;
    fieldInfoStruct *fieldArr;
    SQLSMALLINT numCols;
    int totalsize;
} CursordescObject;

/* row Object */
typedef struct {
    PyObject_VAR_HEAD
    CursordescObject *curdesc;
	unsigned char  buffer[1];
} RowObject;

/* Cursor Object */
typedef struct {
    PyObject_HEAD
    PyObject *con;
    PyObject *stmt;
    SQLHSTMT hstmt;
    PyObject *name;
    CursordescObject *curdesc;
    paramInfoStruct *paraminfo;
    int     arraysize;
    SQLINTEGER rowcount;
    SQLSMALLINT numCols;
    SQLSMALLINT numParams;
	RowObject    *row;
    PyObject *buflist;
} CursorObject;

extern PyTypeObject Connection_Type;
extern PyTypeObject Cursordesc_Type;
extern PyTypeObject Row_Type;
extern PyTypeObject Cursor_Type;

#define ConnectionObject_Check(v) ((v)->ob_type == &Connection_Type)
#define CursordescObject_Check(v) ((v)->ob_type == &Cursordesc_Type)
#define RowObject_Check(v) ((v)->ob_type == &Row_Type)
#define CursorObject_Check(v) ((v)->ob_type == &Cursor_Type)
#define PyClass_Check(obj) PyObject_IsInstance(obj, (PyObject *)&PyType_Type)

static PyObject *
f_error(SQLSMALLINT htype, SQLINTEGER handle) {
    SQLINTEGER  errorcode;
    SQLCHAR     state[5];
    SQLCHAR     msg[STR_LEN];
    SQLSMALLINT msgLen;
    char errorstr[STR_LEN];
    SQLRETURN rc;
    rc = SQLGetDiagRec(htype, handle, (SQLSMALLINT)1, state,
            &errorcode, msg, sizeof(msg), &msgLen);
    if ( rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
        msg[msgLen] = '\0';
        sprintf(errorstr, "SQLState: %s, Error code: %d\n%s",
            state, (int)errorcode, (char *)msg);
    } else {
        strcpy(errorstr, "No error information found.");
    }
    PyErr_SetString(dbError, errorstr);
    return NULL;
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

/* connection methods */

char connection_doc[] =
"Connection([dsn, user, pwd, database, autocommit, sysnaming, servermode]) -> Connection object\n\
\n\
Creates a new Connection object. All parameters are optional.\n\
Use the command dsprdbdire if you don't know the name to use for dsn.\n\
database , default library (not valid with sysnaming).\n\
autocommit (default = False), statments are committed as it is executed.\n\
sysnaming (default = False), system naming mode, use of *LIBL and / delimiter.\n\
servermode (default = False), run all sql commands in a server job,\n\
this is required if you need more than one open connection to the same dsn.";

static PyObject*
connection_new(PyTypeObject *type, PyObject *args, PyObject *keywds)
{
    ConnectionObject *o;
	o = (ConnectionObject *)(type->tp_alloc(type, 0));
    if (o != NULL) {
        o->dsn = NULL;
        o->user = NULL;
        o->pwd = NULL;
        o->database = NULL;
    }
    return (PyObject *)o;
}

static int
connection_init(PyObject *self, PyObject *args, PyObject *keywds)
{
    char    *dsn = NULL;
    char    *user = NULL;
    char    *pwd = NULL;
    char    *database = NULL;
    int     autocommit = 0;
    int     sysnaming = 0;
    int     servermode = 0;
    static char *kwlist[] = {"dsn","user","pwd","database","autocommit","sysnaming",
                             "servermode", NULL};
    SQLRETURN rc;
    int vparm;
    ConnectionObject *c = (ConnectionObject *)self;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|ssssiii:connect", kwlist,
                                     &dsn, &user, &pwd, &database, &autocommit,
                                     &sysnaming, &servermode))
        return -1;
    rc = SQLAllocEnv(&c->henv);
    if (rc == SQL_SUCCESS) {
        /* use UTF8 */
        vparm = SQL_TRUE;
        rc = SQLSetEnvAttr(c->henv, SQL_ATTR_UTF8, &vparm, 0);
        if (dsn != NULL)
        {
            c->dsn = PyMem_Malloc(strlen(dsn) + 1);
            strcpy(c->dsn, dsn);
        }
        if (user != NULL)
        {
            c->user = PyMem_Malloc(strlen(user) + 1);
            strcpy(c->user, user);
        }
        if (pwd != NULL)
        {
            c->pwd = PyMem_Malloc(strlen(pwd) + 1);
            strcpy(c->pwd, pwd);
        }
        if (database != NULL)
        {
            c->database = PyMem_Malloc(strlen(database) + 1);
            strcpy(c->database, database);
        }
        /* run in server mode */
        if (servermode) {
            vparm = SQL_TRUE;
            rc = SQLSetEnvAttr(c->henv, SQL_ATTR_SERVER_MODE, &vparm, 0);
        }
        /* sort sequence job */
        vparm = SQL_TRUE;
        rc = SQLSetEnvAttr(c->henv, SQL_ATTR_JOB_SORT_SEQUENCE, &vparm, 0);
        /* naming convension */
        vparm = sysnaming ? SQL_TRUE: SQL_FALSE;
        rc = SQLSetEnvAttr(c->henv, SQL_ATTR_SYS_NAMING, &vparm, 0);
        if (rc == SQL_SUCCESS && database != NULL) {
            /* default database */
            rc = SQLSetEnvAttr(c->henv, SQL_ATTR_DEFAULT_LIB, c->database,
                               strlen(c->database));
        }
        /* allocate connection handle */
        rc = SQLAllocConnect(c->henv, &c->hdbc);
    }
    if (rc != SQL_SUCCESS) {
        f_error(SQL_HANDLE_ENV, c->henv);
        return -1;
    }
    /* Autocommit */
    vparm = autocommit ? SQL_TXN_NO_COMMIT: SQL_TXN_READ_UNCOMMITTED;
    rc = SQLSetConnectOption(c->hdbc, SQL_ATTR_COMMIT, &vparm);
    if (rc == SQL_SUCCESS)
        /* connect */
        rc = SQLConnect (c->hdbc, c->dsn, SQL_NTS, c->user, SQL_NTS, c->pwd, SQL_NTS);
    if (rc != SQL_SUCCESS) {
        f_error(SQL_HANDLE_DBC, c->hdbc);
        return -1;
    }
    return 0;
}

char con_cursor_doc[] =
"cursor() -> Cursor object\n\
\n\
Creates a new Cursor object.";

static PyObject *
con_cursor(PyObject *self)
{
    CursorObject *cursor;
    PyObject *name;
    cursor = PyObject_New(CursorObject, &Cursor_Type);
    if (cursor != NULL) {
        ConnectionObject *c = (ConnectionObject *)self;
        SQLHSTMT hstmt;
        SQLRETURN rc;
        if (!c->hdbc) {
            PyErr_SetString(dbError, "Connection is closed.");
            return NULL;
        }
        rc = SQLAllocStmt(c->hdbc, &hstmt);
        if (rc != SQL_SUCCESS)
            return f_error(SQL_HANDLE_DBC, c->hdbc);
        cursor_count += 1;
        name = PyUnicode_FromFormat("stmt%d", cursor_count);
        rc = SQLSetCursorName(hstmt, PyUnicode_AsUTF8(name), PyUnicode_GetLength(name));
        if (rc != SQL_SUCCESS)
            return f_error(SQL_HANDLE_DBC, c->hdbc);
        cursor->con = self;
        Py_INCREF(self);
        cursor->hstmt = hstmt;
        cursor->stmt = NULL;
        cursor->curdesc = NULL;
        cursor->paraminfo = NULL;
        cursor->name = name;
        cursor->arraysize = 10;
        cursor->rowcount = 0;
        cursor->numCols = 0;
        cursor->numParams = 0;
        cursor->row = NULL;
        cursor->buflist = NULL;
    }
    return (PyObject *)cursor;
}

char con_commit_doc[] =
"commit() -> None.\n\
\n\
Commit pending transactions.";

static PyObject *
con_commit(PyObject *self)
{
    SQLRETURN rc;
    SQLHDBC hdbc = ((ConnectionObject *)self)->hdbc;
    if (!hdbc) {
        PyErr_SetString(dbError, "Connection is closed.");
        return NULL;
    }
    rc = SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_COMMIT_HOLD);
    if (rc != SQL_SUCCESS)
        return f_error(SQL_HANDLE_DBC, hdbc);
    Py_INCREF(Py_None);
    return Py_None;
}

char con_rollback_doc[] =
"rollback() -> None.\n\
\n\
rollback pending transactions.";

static PyObject *
con_rollback(PyObject *self)
{
    SQLRETURN rc;
    SQLHDBC hdbc = ((ConnectionObject *)self)->hdbc;
    if (!hdbc) {
        PyErr_SetString(dbError, "Connection is closed.");
        return NULL;
    }
    rc = SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK_HOLD);
    if (rc != SQL_SUCCESS)
        return f_error(SQL_HANDLE_DBC, hdbc);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
curdesc_description(CursordescObject *curdesc)
{
    PyObject *t, *v, *o;
    int i;
    fieldInfoStruct *fi;
    if (curdesc) {
        t = PyTuple_New(curdesc->numCols);
        fi = curdesc->fieldArr;
        for (i = 0; i < curdesc->numCols; i++) {
            o = PyLong_FromLong(fi->type);
            if (o == NULL) return NULL;
            v = Py_BuildValue("sNOOiii", fi->name, o, Py_None, Py_None, fi->prec, fi->scale, fi->nulls);
            if (v == NULL) return NULL;
            PyTuple_SET_ITEM(t, i, v);
            fi++;
        }
        return t;
    } else {
        PyErr_SetString(dbError, "No description exists.");
        return NULL;
    }
}

static PyObject *
curdesc_fields(CursordescObject *curdesc)
{
    PyObject *t;
    int i;
    fieldInfoStruct *fi;
    if (curdesc) {
        t = PyTuple_New(curdesc->numCols);
        fi = curdesc->fieldArr;
        for (i = 0; i < curdesc->numCols; i++) {
            PyTuple_SET_ITEM(t, i, PyUnicode_FromString(fi->name));
            fi++;
        }
        return t;
    } else {
        PyErr_SetString(dbError, "No description exists.");
        return NULL;
    }
}

/* internal routine to get field position */
static int
curdesc_getFieldPos(CursordescObject *curdesc, PyObject *field)
{
    PyObject *posO;
    int i = -1;

    Py_INCREF(field);
    if (PyLong_Check(field)) {
        i = PyLong_AS_LONG(field);
        if (i >= curdesc->numCols)
            i = -1;
    } else if (PyUnicode_Check(field)) {
        posO = PyDict_GetItem(curdesc->fieldDict, field);
        if (posO != NULL)
            i = PyLong_AS_LONG(posO);
    }
    Py_DECREF(field);
    return i;
}

char con_close_doc[] =
"close() -> Close connection\n\
\n\
Close a connection, open cursors will be invalid.";

static PyObject *
con_close(PyObject *self)
{
    ConnectionObject *c = (ConnectionObject *)self;
    if (c->hdbc) {
        SQLDisconnect(c->hdbc);
        SQLFreeConnect(c->hdbc);
        SQLFreeEnv(c->henv);
        c->hdbc = 0;
        c->henv = 0;
        if (c->dsn) PyMem_Free(c->dsn);
        if (c->user) PyMem_Free(c->user);
        if (c->pwd) PyMem_Free(c->pwd);
        if (c->database) PyMem_Free(c->database);
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static void
con_dealloc(PyObject *self)
{
    con_close(self);
    PyObject_Del(self);
}

static void
setColumnType(CursorObject *c, fieldInfoStruct *fi, int fieldno)
{
    int ccsid;
    SQLRETURN rc;
    switch (fi->type) {
        case SQL_DECIMAL:
        case SQL_NUMERIC:
            if (fi->scale == 0 && fi->prec < 19) {
                fi->ctype = SQL_BIGINT;
                fi->size = sizeof(long long);
            } else {
                // Use decimal field decimal places
                if (USE_DECIMAL == 1) {
                    fi->ctype = SQL_CHAR;
                    fi->size = fi->prec + 3;
                } else {
                    fi->ctype = SQL_DOUBLE;
                    fi->size = sizeof(SQLDOUBLE);
                }
            }
            break;
        case SQL_FLOAT:
        case SQL_REAL:
        case SQL_DOUBLE:
            fi->ctype = SQL_DOUBLE;
            fi->size = sizeof(SQLDOUBLE);
            break;
        case SQL_SMALLINT:
        case SQL_INTEGER:
        case SQL_BIGINT:
            fi->ctype = SQL_BIGINT;
            fi->size = sizeof(long long);
            break;
		case SQL_BLOB:
		case SQL_BINARY:
		case SQL_VARBINARY:
            fi->ctype = SQL_BINARY;
            fi->size = fi->prec;
		    break;
        case SQL_WCHAR:
        case SQL_WVARCHAR:
        case SQL_GRAPHIC:
        case SQL_VARGRAPHIC:
            fi->ctype = SQL_WCHAR;
            fi->size = fi->prec;
		    break;
		case SQL_VARCHAR:
            rc = SQLColAttributes(c->hstmt, fieldno, SQL_DESC_COLUMN_CCSID, NULL, 0, NULL, &ccsid);
            if (rc == SQL_SUCCESS && ccsid == 65535) {
                fi->ctype = SQL_BINARY;
                fi->size = fi->prec;
                break;
            }
		default:
            fi->ctype = SQL_CHAR;
            fi->size = fi->prec * 2 + 1;
            if (fi->size > 32765)
                fi->size = 32765;
		    break;
    }
}

/* bind parameters from supplied python tuple/list */
static PyObject *
bindParams(CursorObject *self, PyObject *args)
{
    int i;
    Py_ssize_t slen;
    paramInfoStruct *pi;
    SQLRETURN rc;
    char errbuf[255];
    pi = self->paraminfo;
    for (i = 0; i < self->numParams; i++) {
        Py_XDECREF(pi->obj);
        pi->obj = PySequence_GetItem(args, i);
        if (pi->obj == Py_None) {
            pi->ctype = SQL_CHAR;
            pi->size = SQL_NULL_DATA;
        } else {
            switch (pi->type) {
            case SQL_INTEGER:
            case SQL_SMALLINT:
            case SQL_BIGINT:
            case SQL_FLOAT:
            case SQL_DECIMAL:
            case SQL_NUMERIC:
            case SQL_REAL:
                if (PyLong_Check(pi->obj)) {
                    Py_DECREF(pi->obj);
                    pi->obj = PyObject_Str(pi->obj);
                    if (PyErr_Occurred())
                        return NULL;
                    pi->size = PyUnicode_GET_LENGTH(pi->obj);
                    pi->ctype = SQL_CHAR;
                    pi->data = (SQLPOINTER)PyUnicode_AsUTF8(pi->obj);
                } else if (PyFloat_Check(pi->obj)) {
                    pi->ctype = SQL_DOUBLE;
                    pi->size = sizeof(SQLDOUBLE);
                    pi->data = (SQLPOINTER)&(PyFloat_AS_DOUBLE(pi->obj));
                } else if (PyUnicode_Check(pi->obj) && PyUnicode_GetLength(pi->obj) > 0) {
                    pi->data = (SQLPOINTER)(PyUnicode_AsUTF8AndSize(pi->obj, &slen));
                    pi->size = slen;
                    pi->ctype = SQL_CHAR;
                } else {
                    sprintf(errbuf, "Param %d: Number expected.", i);
                    PyErr_SetString(dbError, errbuf);
                    return NULL;
                }
                break;
            case SQL_WCHAR:
            case SQL_WVARCHAR:
            case SQL_GRAPHIC:
            case SQL_VARGRAPHIC:
            case SQL_DBCLOB:
                /* unicode */
                if (PyUnicode_Check(pi->obj)) {
                    pi->data = (SQLPOINTER)(PyUnicode_AsUTF8AndSize(pi->obj, &slen));
                    pi->size = slen;
                    pi->ctype = SQL_CHAR;
                } else {
                    sprintf(errbuf, "Param %d: Unicode object expected.", i);
                    PyErr_SetString(dbError, errbuf);
                    return NULL;
                }
                break;
            case SQL_CHAR:
            case SQL_VARCHAR:
            case SQL_CLOB:
                if (PyUnicode_Check(pi->obj)) {
                    pi->size = PyUnicode_GetLength(pi->obj);
                    if (pi->size == 0) {
                        pi->size = 1;
                        pi->data = " ";
                    } else
                        pi->data = (SQLPOINTER)(PyUnicode_AsUTF8(pi->obj));
                    pi->ctype = SQL_CHAR;
                } else if (PyBytes_Check(pi->obj)) {
                    pi->ctype = SQL_CHAR;
                    pi->data = (SQLPOINTER)(PyBytes_AsString(pi->obj));
                    pi->size = PyBytes_GET_SIZE(pi->obj);
                } else {
                    sprintf(errbuf, "Param %d: String expected.", i);
                    PyErr_SetString(dbError, errbuf);
                    return NULL;
                }
                break;
            case SQL_DATE:
            case SQL_TIME:
            case SQL_TIMESTAMP:
                if (PyUnicode_Check(pi->obj)) {
                    pi->size = PyUnicode_GetLength(pi->obj);
                    if (pi->size == 0) {
                        pi->size = 1;
                        pi->data = " ";
                    } else {
                        pi->data = (SQLPOINTER)(PyUnicode_AsUTF8AndSize(pi->obj, &slen));
                        pi->size = slen;
                    }
                    pi->ctype = SQL_CHAR;
                } else {
                    Py_DECREF(pi->obj);
                    pi->obj = PyObject_Str(pi->obj);
                    if (PyErr_Occurred())
                        return NULL;
                    pi->ctype = SQL_CHAR;
                    pi->data = (SQLPOINTER)(PyUnicode_AsUTF8AndSize(pi->obj, &slen));
                    pi->size = slen;
                }
                /* the format of the string is checked when executed */
                break;
            case SQL_BLOB:
            case SQL_BINARY:
            case SQL_VARBINARY:
                if (PyBytes_Check(pi->obj)) {
                    pi->ctype = SQL_BINARY;
                    pi->data = (SQLPOINTER)(PyBytes_AsString(pi->obj));
                    pi->size = PyBytes_GET_SIZE(pi->obj);
                    break;
                }
            sprintf(errbuf, "Param %d: Unknown type.", i);
            PyErr_SetString(dbError, errbuf);
            return NULL;
            }
        }
        rc = SQLBindParameter(self->hstmt, i + 1, SQL_PARAM_INPUT,
                             pi->ctype, pi->type, pi->prec, pi->scale,
                             pi->data, 0, &pi->size);
        if (rc != SQL_SUCCESS)
            return f_error(SQL_HANDLE_STMT, self->hstmt);
        pi++;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

/* copy row object (to return) */
static PyObject *
row_copy(RowObject *self)
{
    RowObject *o;
    int size = self->curdesc->totalsize;
    o = PyObject_NewVar(RowObject, &Row_Type, size);
    if (o == NULL)
        return NULL;
    o->curdesc = self->curdesc;
    Py_INCREF(o->curdesc);
    memcpy(o->buffer, self->buffer, size);
    return (PyObject *)o;
}

/* cursor methods */

static void
cur_unbind(CursorObject *self)
{
    int i;
    if (self->hstmt) {
        SQLFreeStmt(self->hstmt, SQL_RESET_PARAMS);
        Py_XDECREF(self->curdesc);
        Py_XDECREF(self->row);
        if (self->numParams > 0) {
            for (i = 0; i < self->numParams; i++)
                Py_XDECREF(self->paraminfo[i].obj);
            PyMem_Free(self->paraminfo);
            self->paraminfo = NULL;
            self->numParams = 0;
        }
        self->curdesc = NULL;
        self->row = NULL;
        SQLFreeStmt(self->hstmt, SQL_UNBIND);
    }
}

char cur_execute_doc[] =
"execute(string,[Parameters]) -> None.\n\
\n\
Execute sql statement. Parameters should be a tuple or list.";

static PyObject *
cur_execute(PyObject *self, PyObject *args)
{
    CursorObject *c = (CursorObject *)self;
    PyObject *params = NULL, *iobj, *stmt;
    SQLRETURN rc;
    int i;
    if (!PyArg_ParseTuple(args, "U|O:execute", &stmt, &params))
        return NULL;
    if (params && params != Py_None && (!PyTuple_Check(params) && !PyList_Check(params))) {
        PyErr_SetString(dbError, "Parameters must be a tuple or list.");
        return NULL;
    }
    if (!c->hstmt) {
        PyErr_SetString(dbError, "Cursor is closed.");
        return NULL;
    }
    rc = SQLFreeStmt(c->hstmt, SQL_CLOSE);
    /* check if identical statement */
    if (!c->stmt || PyUnicode_Compare(c->stmt, stmt) != 0)
    {
        cur_unbind(c);
        Py_XDECREF(c->stmt);
        c->stmt = stmt;
        Py_INCREF(c->stmt);
        /* prepare */
        Py_BEGIN_ALLOW_THREADS
        rc = SQLPrepare(c->hstmt, PyUnicode_AsUTF8(stmt), SQL_NTS);
        Py_END_ALLOW_THREADS
        if (rc != SQL_SUCCESS)
            return f_error(SQL_HANDLE_STMT, c->hstmt);
        /* number of columns */
        rc = SQLNumResultCols(c->hstmt, &c->numCols);
        if (rc != SQL_SUCCESS)
            return f_error(SQL_HANDLE_STMT, c->hstmt);
        if (c->numCols > 0) {
            unsigned char *buffer;
            fieldInfoStruct *fi;
            int totalsize = c->numCols * sizeof(SQLINTEGER);
            c->curdesc = PyObject_New(CursordescObject, &Cursordesc_Type);
            /* column information */
            c->curdesc->fieldDict = PyDict_New();
            c->curdesc->fieldArr = fi = PyMem_Malloc(c->numCols * sizeof(fieldInfoStruct));
            for (i = 0; i < c->numCols; i++) {
                SQLSMALLINT nameLength;
                rc = SQLDescribeCol(c->hstmt, i + 1, fi->name, sizeof(fi->name),
                                    &nameLength, &fi->type, &fi->prec, &fi->scale,
                                    &fi->nulls);
                setColumnType(c, fi, i + 1);
                iobj = PyLong_FromLong(i);
                PyDict_SetItemString(c->curdesc->fieldDict, fi->name, iobj);
                // Also set lower case version of field
                for(int j = 0; fi->name[j]; j++){
                  fi->name[j] = tolower(fi->name[j]);
                }
                PyDict_SetItemString(c->curdesc->fieldDict, fi->name, iobj);
                Py_DECREF(iobj);
                fi->offset = totalsize;
                totalsize += fi->size;
                fi++;
            }
            c->curdesc->numCols = c->numCols;
            c->curdesc->totalsize = totalsize;
            /* allocate one row object for buffer */
            c->row = PyObject_NewVar(RowObject, &Row_Type, totalsize);
            c->row->curdesc = c->curdesc;
            Py_INCREF(c->curdesc);
            buffer = c->row->buffer;
            fi = c->curdesc->fieldArr;
            for (i = 0; i < c->numCols; i++) {
                /* bind column */
                SQLBindCol(c->hstmt, i + 1, fi->ctype,
                           buffer + fi->offset, fi->size,
                           (SQLINTEGER *)(buffer + sizeof(SQLINTEGER) * i));
                fi++;
            }
        }
        /* get parameter information */
        rc = SQLNumParams(c->hstmt, &c->numParams);
        if (rc != SQL_SUCCESS)
            return f_error(SQL_HANDLE_STMT, c->hstmt);
        if (c->numParams > 0) {
            paramInfoStruct *pi;
            c->paraminfo = pi = PyMem_Malloc(c->numParams * sizeof(paramInfoStruct));
            for (i = 0; i < c->numParams; i++) {
                rc = SQLDescribeParam(c->hstmt, i + 1, &pi->type,
                                      &pi->prec, &pi->scale, &pi->nulls);
                pi->obj = NULL;
                pi++;
            }
        }
    }
    if (c->numParams > 0) {
        if (!params || params == Py_None || PySequence_Size(params) != c->numParams) {
            PyErr_SetString(dbError, "Number of parameters don't match.");
            return NULL;
        }
        if (bindParams(c, params) == NULL)
            return NULL;
    }
    /* execute */
    Py_BEGIN_ALLOW_THREADS
    rc = SQLExecute(c->hstmt);
    Py_END_ALLOW_THREADS
    if (rc == SQL_SUCCESS)
        rc = SQLRowCount(c->hstmt, &c->rowcount);
    else if (rc == SQL_NO_DATA_FOUND)
        c->rowcount = 0;
    else
        return f_error(SQL_HANDLE_STMT, c->hstmt);
    /* return self for iteration */
    Py_INCREF(self);
    return self;
}/* end cur_execute */

char cur_fetchone_doc[] =
"fetchone() -> Row object (sequence)\n\
\n\
Fetch one row.";

static PyObject *
cur_fetchone(PyObject *self)
{
    CursorObject *c = (CursorObject *)self;
    SQLRETURN rc;
    if (!c->hstmt) {
        PyErr_SetString(dbError, "Cursor is closed.");
        return NULL;
    }
    if (c->numCols == 0) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    Py_BEGIN_ALLOW_THREADS
    rc = SQLFetch(c->hstmt);
    Py_END_ALLOW_THREADS
    if (rc == SQL_NO_DATA_FOUND) {
        Py_INCREF(Py_None);
        return Py_None;
    } else if (rc != SQL_SUCCESS)
        return f_error(SQL_HANDLE_STMT, c->hstmt);
    else
        return row_copy(c->row);
}

char cur_fetchmany_doc[] =
"fetchmany([size]) -> Tuple of Row objects.\n\
\n\
Fetch size number of rows. If size is not given,\n\
arraysize number of rows is fetched.";

static PyObject *
cur_fetchmany(PyObject *self, PyObject *args)
{
    CursorObject *c = (CursorObject *)self;
    SQLRETURN rc;
    int i, size = c->arraysize;
    PyObject *t, *o;
    if (!PyArg_ParseTuple(args, "|i:fetchmany", &size))
        return NULL;
    if (!c->hstmt) {
        PyErr_SetString(dbError, "Cursor is closed.");
        return NULL;
    }
    t = PyList_New(0);
    if (t == NULL)
        return NULL;
    for (i = 0; i < size; i++) {
        Py_BEGIN_ALLOW_THREADS
        rc = SQLFetch(c->hstmt);
        Py_END_ALLOW_THREADS
        if (rc == SQL_NO_DATA_FOUND)
            break;
        else if (rc != SQL_SUCCESS)
            return f_error(SQL_HANDLE_STMT, c->hstmt);
        o = row_copy(c->row);
        if (o == NULL)
            return NULL;
        if (PyList_Append(t, o) == -1)
            return NULL;
        Py_DECREF(o);
    }
    return t;
}

char cur_fetchall_doc[] =
"fetchall() -> List of Row objects.\n\
\n\
Fetch all(remaining) rows.";

static PyObject *
cur_fetchall(PyObject *self)
{
    CursorObject *c = (CursorObject *)self;
    SQLRETURN rc;
    PyObject *t, *o;
    if (!c->hstmt) {
        PyErr_SetString(dbError, "Cursor is closed.");
        return NULL;
    }
    t = PyList_New(0);
    if (t == NULL)
        return NULL;
    while (1) {
        Py_BEGIN_ALLOW_THREADS
        rc = SQLFetch(c->hstmt);
        Py_END_ALLOW_THREADS
        if (rc == SQL_NO_DATA_FOUND)
            break;
        else if (rc != SQL_SUCCESS)
            return f_error(SQL_HANDLE_STMT, c->hstmt);
        o = row_copy(c->row);
        if (o == NULL)
            return NULL;
        if (PyList_Append(t, o) == -1)
            return NULL;
        Py_DECREF(o);
    }
    return t;
}

char cur_close_doc[] =
"close() -> None.\n\
\n\
Close the cursor. It can not be reopened.";

static PyObject *
cur_close(PyObject *self)
{
    CursorObject *c = (CursorObject *)self;
    if (c->hstmt) {
        cur_unbind(c);
        SQLFreeStmt(c->hstmt, SQL_DROP);
        c->hstmt = 0;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

char cur_nextset_doc[] =
"nextset() -> None.\n\
\n\
Make cursor skip to the next set. (not yet implemented.)";

static PyObject *
cur_nextset(PyObject *self)
{
    CursorObject *c = (CursorObject *)self;
    if (c->hstmt) {
    }
    Py_INCREF(Py_None);
    return Py_None;
}

char cur_executemany_doc[] =
"executemany(string,seq of parameters) -> Tuple of result sets.\n\
\n\
Run a statement agains many parameter sequences.";

static PyObject *
cur_executemany(PyObject *self, PyObject *args)
{
    CursorObject *c = (CursorObject *)self;
    char *stmt;
    PyObject *params, *res, *v, *p;
    int i, size;
    if (!PyArg_ParseTuple(args, "UO:executemany", &stmt, &params))
        return NULL;
    if (params && (!PyTuple_Check(params) && !PyList_Check(params))) {
        PyErr_SetString(dbError, "Parameters must be a tuple or list of tuple or list.");
        return NULL;
    }
    size = PySequence_Size(params);
    res = PyTuple_New(size);
    for (i = 0; i < size; i++) {
        p = Py_BuildValue("UN", stmt, PySequence_GetItem(params, i));
        v = cur_execute(self, p);
        Py_XDECREF(p);
        if (v == NULL)
            return NULL;
        /* fetch the results if result set exists */
        if (c->numCols > 0)
            PyTuple_SET_ITEM(res, i, cur_fetchall(self));
    }
    if (c->numCols > 0)
        return res;
    else {
        Py_DECREF(res);
        Py_INCREF(Py_None);
        return Py_None;
    }
}

char cur_description_doc[] =
"description -> Describes all columns.";

static PyObject *
cur_description(CursorObject *self)
{
    return curdesc_description(self->curdesc);
}

char cur_fields_doc[] =
"fields -> List all column names.";

static PyObject *
cur_fields(CursorObject *self)
{
    return curdesc_fields(self->curdesc);
}

static void
cursor_dealloc(PyObject *self)
{
    CursorObject *crs = (CursorObject *)self;
    PyObject *con = crs->con;
    cur_close(self);
    Py_XDECREF(crs->stmt);
    Py_XDECREF(crs->buflist);
    Py_XDECREF(crs->name);
    PyObject_Del(self);
    Py_XDECREF(con);
}

static PyObject *
cursor_repr(CursorObject *self)
{
    if (self->stmt) {
        Py_INCREF(self->stmt);
        return self->stmt;
    } else
        return PyUnicode_FromString("Cursor object");
}

static PyObject *
cursor_iter(PyObject *self)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
cursor_next(PyObject *self)
{
    PyObject *row;
    CursorObject *c = (CursorObject *)self;
    if (c->buflist == NULL) {
        PyObject *parm, *res;
        parm = PyTuple_New(0);
        res = cur_fetchmany(self, parm);
        Py_DECREF(parm);
        if (res == Py_None || PyList_Size(res) == 0) {
            PyErr_SetObject(PyExc_StopIteration, Py_None);
            return NULL;
        }
        PyObject_CallMethod(res, "reverse", NULL);
        c->buflist = res;
    }
    row = PyObject_CallMethod(c->buflist, "pop", NULL);
    if (PyList_Size(c->buflist) == 0) {
        Py_DECREF(c->buflist);
        c->buflist = NULL;
    }
    return row;
}

char cur_enter_doc[] =
"__enter__() -> self.";

static PyObject *
cur_enter(CursorObject *self)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

char cur_exit_doc[] =
"__exit__(*excinfo) -> None.  closes the cursor.";

static PyObject *
cur_exit(CursorObject *self, PyObject *args)
{
    return cur_close((PyObject *)self);
}

/* row methods       */

/* convert one row field to python object */
static PyObject *
cvtToPy(unsigned char *buf, fieldInfoStruct fi, int i)
{
    int size;
    char *p;
    size = *(int *)(buf + sizeof(int) * i);
    p = (char *)buf + fi.offset;
    if (size == SQL_NULL_DATA) {
        Py_INCREF(Py_None);
        return Py_None;
    } else if (fi.ctype == SQL_BIGINT) {
        return PyLong_FromLongLong(*(long long *)p);
    } else if (fi.ctype == SQL_DOUBLE) {
        return PyFloat_FromDouble(*(double *)p);
    } else if (fi.ctype == SQL_CHAR) {
        if (size == SQL_NTS) {
            size = strlen(p);
        } else if (size == 0) {
            if (fi.type == SQL_VARCHAR) size = strlen(p);
            else size = fi.size - 1;
        }
        while (size > 0 && isspace(Py_CHARMASK(p[size - 1])))
            size--;
        if (fi.type == SQL_NUMERIC || fi.type == SQL_DECIMAL) {
            return PyObject_CallFunction(decimalcls, "s", p);
        } else if (fi.type == SQL_DATE && DateFunc)
            return PyObject_CallFunction(DateFunc, "s#", p, size);
        else if (fi.type == SQL_TIME && TimeFunc)
            return PyObject_CallFunction(TimeFunc, "s#", p, size);
        else if (fi.type == SQL_TIMESTAMP && TimeFunc)
            return PyObject_CallFunction(DatetimeFunc, "s#", p, size);
        else
            return PyUnicode_FromStringAndSize(p, size);
    } else if (fi.ctype == SQL_WCHAR) {
        if (size == SQL_NTS)
            size = strlen(p);
        while (size > 0 && isspace(Py_CHARMASK(p[size - 1])))
            size--;
		return PyUnicode_FromUnicode((Py_UNICODE *)p, 2);
    } else if (fi.ctype == SQL_BINARY) {
        if (fi.type != SQL_BINARY && fi.type != SQL_BLOB && fi.type != SQL_VARBINARY) {
            if (size == SQL_NTS)
                size = strlen(p);
            while (size > 0 && p[size - 1] == '\00')
                size--;
            }
        return PyBytes_FromStringAndSize(p, size);
    } else {
        PyErr_SetString(dbError, "Data conversion error.");
        return NULL;
    }
}

static PyObject *
row_subscript(RowObject *self, PyObject *v)
{
    int pos;
    pos = curdesc_getFieldPos(self->curdesc, v);
    if (pos < 0) {
        PyErr_SetString(dbError, "Parameter not valid.");
        return NULL;
    } else
        return cvtToPy(self->buffer, self->curdesc->fieldArr[pos], pos);
}

static PyObject *
row_getattro(RowObject *self, PyObject *field)
{
    PyObject *f1, *res;
    int chr;
    if (PyUnicode_Check(field)) {
        PyUnicode_READY(field);
        if (PyUnicode_GET_LENGTH(field) > 0) {
            chr = PyUnicode_READ_CHAR(field, 0);
            if (chr == '_') {
                f1 = PyUnicode_Substring(field, 1, PyUnicode_GET_LENGTH(field));
                res = row_subscript(self, f1);
                Py_DECREF(f1);
                return res;
            }
        }
    }
    return PyObject_GenericGetAttr((PyObject *)self, field);
}

static int
row_length(RowObject *self)
{
	return self->curdesc->numCols;
}

static PyObject *
row_item(RowObject *self, int i)
{
	if (i < 0 || i >= self->curdesc->numCols) {
		PyErr_SetString(PyExc_IndexError, "row index out of range");
		return NULL;
	}
    return cvtToPy(self->buffer, self->curdesc->fieldArr[i], i);
}

static PyObject *
row_slice(RowObject *self, int ilow, int ihigh)
{
	PyObject *t, *v;
	int i, size = self->curdesc->numCols;
	if (ilow < 0)
		ilow = 0;
	if (ihigh > size)
		ihigh = size;
	if (ihigh < ilow)
		ihigh = ilow;
	t = PyTuple_New(ihigh - ilow);
	if (t == NULL)
		return NULL;
	for (i = ilow; i < ihigh; i++) {
        v = cvtToPy(self->buffer, self->curdesc->fieldArr[i], i);
        if (v == NULL)
            return NULL;
		PyTuple_SET_ITEM(t, i - ilow, v);
	}
	return t;
}

static char row_get_doc[] =
"f.get([fields][output][cls][labels]) -> List or Value.\n\
\n\
Get the Row as a tuple(default), object or dict.\n\
f.get() - Get list of all values.\n\
output can have the following values:\n\
 0 - output as a tuple(default)\n\
 1 - output as a object.\n\
 2 - output as a dictionary.\n\
cls is class of object if output object is selected.\n\
labels should be a tuple of labels to be used as field names\n\
if output is a object or dict.";


static PyObject *
row_get(RowObject *self, PyObject *args, PyObject *keywds)
{
    PyObject *fields= Py_None, *labels = Py_None, *cls = Py_None, *va, *obj, *iobj, *dict;
    int i, j, len, flen = 0, output = LIST;
    static char *kwlist[] = {"fields", "output", "cls", "labels", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|OiOO:get", kwlist,
                                     &fields, &output, &cls, &labels))
        return NULL;
    if (output == OBJ && !PyClass_Check(cls) && !PyType_Check(cls)) {
        PyErr_SetString(dbError, "cls not a valid type.");
        return NULL;
    }
    /* Number of columns */
    len = self->curdesc->numCols;
    /* check of fields exists */
    if (fields != Py_None) {
        if ((!PyTuple_Check(fields) && !PyList_Check(fields)) || PySequence_Size(fields) > len) {
            PyErr_SetString(dbError, "Fields parameter not valid.");
            return NULL;
        }
        flen = PySequence_Size(fields);
    }
    if (output == LIST) {
        if (fields != Py_None) {
            obj = PyTuple_New(flen);
            for (j = 0; j < flen; j++) {
                if (PyTuple_Check(fields)) {
                    iobj = PyDict_GetItem(self->curdesc->fieldDict, PyTuple_GET_ITEM(fields, j));
                } else {
                    iobj = PyDict_GetItem(self->curdesc->fieldDict, PyList_GET_ITEM(fields, j));
                }
                if (iobj == NULL) {
                    PyErr_SetString(dbError, "Field not valid");
                    return NULL;
                }
                i = PyLong_AsLong(iobj);
                va = cvtToPy(self->buffer, self->curdesc->fieldArr[i], i);
                if (va == NULL) return NULL;
                PyTuple_SET_ITEM(obj, j, va);
            }
        } else {
            obj = PyTuple_New(len);
            for (i = 0; i < len; i++) {
                va = cvtToPy(self->buffer, self->curdesc->fieldArr[i], i);
                if (va == NULL) return NULL;
                PyTuple_SET_ITEM(obj, i, va);
            }
        }
    } else {
        if ((!PyTuple_Check(labels) && !PyList_Check(labels))) {
            PyErr_SetString(dbError, "Missing labels.");
            return NULL;
        } else if ((flen > 0 && PySequence_Size(labels) != flen) || (flen == 0 && PySequence_Size(labels) != len)) {
            PyErr_SetString(dbError, "Number of labels does not match with fields.");
            return NULL;
        }
        if (output == OBJ) {
            obj = f_createObject(cls);
            if(obj == NULL) {
                PyErr_SetString(dbError, "Could not create object.");
                return NULL;
            }
            dict = PyObject_GetAttrString(obj, "__dict__");
            if(dict == NULL) {
                PyErr_SetString(dbError, "Can not get objects __dict__.");
                return NULL;
            }
            Py_DECREF(dict);
        } else {
            obj = dict = PyDict_New();
        }
        if (fields != Py_None) {
            for (j = 0; j < flen; j++) {
                if (PyTuple_Check(fields)) {
                    iobj = PyDict_GetItem(self->curdesc->fieldDict, PyTuple_GET_ITEM(fields, j));
                } else {
                    iobj = PyDict_GetItem(self->curdesc->fieldDict, PyList_GET_ITEM(fields, j));
                }
                if (iobj == NULL) {
                    PyErr_SetString(dbError, "Field not valid");
                    return NULL;
                }
                i = PyLong_AsLong(iobj);
                va = cvtToPy(self->buffer, self->curdesc->fieldArr[i], i);
                if (va == NULL) return NULL;
                if (PyTuple_Check(labels)) {
                    PyDict_SetItem(dict, PyTuple_GET_ITEM(labels, j), va);
                } else {
                    PyDict_SetItem(dict, PyList_GET_ITEM(labels, j), va);
                }
                Py_DECREF(va);
            }
        } else {
            for (i = 0; i < len; i++) {
                va = cvtToPy(self->buffer, self->curdesc->fieldArr[i], i);
                if (va == NULL) return NULL;
                if (PyTuple_Check(labels)) {
                    PyDict_SetItem(dict, PyTuple_GET_ITEM(labels, i), va);
                } else {
                    PyDict_SetItem(dict, PyList_GET_ITEM(labels, i), va);
                }
                Py_DECREF(va);
            }
        }
    }
    return obj;
}

char row_description_doc[] =
"description -> Describes all columns.";

static PyObject *
row_description(RowObject *self)
{
    return curdesc_description(self->curdesc);
}

char row_fields_doc[] =
"fields -> List all column names.";

static PyObject *
row_fields(RowObject *self)
{
    return curdesc_fields(self->curdesc);
}

static void
row_dealloc(PyObject *self)
{
    Py_XDECREF(((RowObject*)self)->curdesc);
    PyObject_Del(self);
}

static PyMethodDef connection_methods[] = {
    {"cursor", (PyCFunction)con_cursor, METH_NOARGS, con_cursor_doc},
    {"commit", (PyCFunction)con_commit, METH_NOARGS, con_commit_doc},
    {"rollback", (PyCFunction)con_rollback, METH_NOARGS, con_rollback_doc},
    {"close", (PyCFunction)con_close, METH_NOARGS, con_close_doc},
    {NULL}  /* sentinel */
};

PyTypeObject Connection_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "Connection",
    .tp_doc = connection_doc,
    .tp_basicsize = sizeof(ConnectionObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)con_dealloc,
	.tp_getattro = PyObject_GenericGetAttr,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_methods = connection_methods,
	.tp_init = connection_init,
	.tp_alloc = PyType_GenericAlloc,
	.tp_new = connection_new
};

static void
cursordesc_dealloc(PyObject *self)
{
    PyMem_Free(((CursordescObject *)self)->fieldArr);
    Py_XDECREF(((CursordescObject *)self)->fieldDict);
    PyObject_Del(self);
}

PyTypeObject Cursordesc_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "Cursordesc",
    .tp_basicsize = sizeof(CursordescObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)cursordesc_dealloc,
	.tp_alloc = PyType_GenericAlloc,
};

static PyMethodDef cursor_methods[] = {
    {"execute", (PyCFunction)cur_execute, METH_VARARGS, cur_execute_doc},
    {"executemany", (PyCFunction)cur_executemany, METH_VARARGS, cur_executemany_doc},
    {"fetchone", (PyCFunction)cur_fetchone, METH_NOARGS, cur_fetchone_doc},
    {"fetchmany", (PyCFunction)cur_fetchmany, METH_VARARGS, cur_fetchmany_doc},
    {"fetchall", (PyCFunction)cur_fetchall, METH_NOARGS, cur_fetchall_doc},
    {"close", (PyCFunction)cur_close, METH_NOARGS, cur_close_doc},
    {"fieldDescription", (PyCFunction)cur_description, METH_NOARGS, cur_description_doc},
    {"fieldList", (PyCFunction)cur_fields, METH_NOARGS, cur_fields_doc},
    {"nextset", (PyCFunction)cur_nextset, METH_NOARGS, cur_nextset_doc},
    {"__exit__", (PyCFunction)cur_exit, METH_VARARGS, cur_exit_doc},
    {"__enter__", (PyCFunction)cur_enter, METH_NOARGS, cur_enter_doc},
    {NULL}  /* sentinel */
};

static PyMemberDef cursor_members[] = {
    {"connection", T_OBJECT_EX, offsetof(CursorObject, con), READONLY,
     "Connection object"},
    {"name", T_OBJECT, offsetof(CursorObject, name), READONLY,
     "Name of cursor"},
    {"arraysize", T_INT, offsetof(CursorObject, arraysize), 0,
     "Default number of rows to fetch."},
    {"rowcount", T_INT, offsetof(CursorObject, rowcount), READONLY,
     "Number of records in sql statement"},
    {NULL}  /* sentinel */
};

PyTypeObject Cursor_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "Cursor",
    .tp_doc = "Cursor object",
    .tp_basicsize = sizeof(CursorObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)cursor_dealloc,
    .tp_repr = (reprfunc)cursor_repr,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_iter = (getiterfunc)cursor_iter,
	.tp_iternext = (iternextfunc)cursor_next,
	.tp_methods = cursor_methods,
	.tp_members = cursor_members,
	.tp_alloc = PyType_GenericAlloc,
};

static PyMethodDef row_methods[] = {
    {"get", (PyCFunction)row_get, METH_VARARGS | METH_KEYWORDS, row_get_doc},
    {"fieldList", (PyCFunction)row_fields, METH_VARARGS | METH_KEYWORDS, row_fields_doc},
    {"fieldDescription", (PyCFunction)row_description, METH_VARARGS | METH_KEYWORDS, row_description_doc},
    {NULL}  /* sentinel */
};

static PySequenceMethods row_as_sequence = {
	(lenfunc)row_length,
	0,          /* sq_concat */
	0,         	/* sq_repeat */
	(ssizeargfunc)row_item,		/* sq_item */
	(ssizeargfunc)row_slice,		/* sq_slice */
	0,					/* sq_ass_item */
	0,					/* sq_ass_slice */
	0,                  /* sq_contains */
	0,                  /* sq_inplace_concat */
	0,                  /* sq_inplace_repeat */
};

static PyMappingMethods row_as_mapping = {
    (lenfunc)row_length, /*mp_length*/
    (binaryfunc)row_subscript, /*mp_subscript*/
    NULL, /*mp_ass_subscript*/
};

PyTypeObject Row_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "Row",
    .tp_doc = "Row object",
    .tp_basicsize = sizeof(RowObject),
    .tp_itemsize = 1,
    .tp_dealloc = (destructor)row_dealloc,
    .tp_as_sequence = &row_as_sequence,
    .tp_as_mapping = &row_as_mapping,
    .tp_getattro = (getattrofunc)row_getattro,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_methods = row_methods,
};

static PyObject*
setFieldtype(PyObject* self, PyObject* args)
{
    PyObject *func;
    if (!PyArg_ParseTuple(args, "O", &func))
        return NULL;
    if (func != Py_None && !PyCallable_Check(func)) {
        PyErr_SetString(dbError, "Function is not callable.");
        return NULL;
    }
    Py_XDECREF(FieldtypeFunc);
    if (func != Py_None) {
        FieldtypeFunc = func;
        Py_INCREF(FieldtypeFunc);
    } else
        FieldtypeFunc = NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject*
setDatetime(PyObject* self, PyObject* args)
{
    PyObject *datef, *timef, *datetimef;
    if (!PyArg_ParseTuple(args, "OOO", &datef, &timef, &datetimef))
        return NULL;
    if (datef != Py_None && !PyCallable_Check(datef)) {
        PyErr_SetString(dbError, "Date function is not callable.");
        return NULL;
    }
    if (timef != Py_None && !PyCallable_Check(timef)) {
        PyErr_SetString(dbError, "Time function is not callable.");
        return NULL;
    }
    if (datetimef != Py_None && !PyCallable_Check(datetimef)) {
        PyErr_SetString(dbError, "Datetime function is not callable.");
        return NULL;
    }
    Py_XDECREF(DateFunc);
    Py_XDECREF(TimeFunc);
    Py_XDECREF(DatetimeFunc);
    DateFunc = TimeFunc = DatetimeFunc = NULL;
    if (datef != Py_None) {
        DateFunc = datef;
        Py_INCREF(DateFunc);
    }
    if (timef != Py_None) {
        TimeFunc = timef;
        Py_INCREF(TimeFunc);
    }
    if (datetimef != Py_None) {
        DatetimeFunc = datetimef;
        Py_INCREF(DatetimeFunc);
    }
    Py_INCREF(Py_None);
    return Py_None;
}

/* List of functions defined in the module */
static PyMethodDef db_memberlist[] = {
    /* TODO methods to set date/time/timestamp factory functions */
    {"setFieldtypeFunction", (PyCFunction)setFieldtype, METH_VARARGS, "Set factory function for field types."},
    {"setDatetimeFunctions", (PyCFunction)setDatetime, METH_VARARGS, "Set date and time factory functions"},
	/* An end-of-listing sentinel: */
	{NULL}
};

static struct PyModuleDef moduledef = {
		PyModuleDef_HEAD_INIT,
		"_db2",
		"IBM Database API v2.0 for IBM i db2",
		-1,
		db_memberlist,
	};

PyMODINIT_FUNC
PyInit__db2(void)
{
    PyObject *m, *decimalmod;
	if (PyType_Ready(&Connection_Type) < 0)
		return NULL;
	if (PyType_Ready(&Cursordesc_Type) < 0)
		return NULL;
	if (PyType_Ready(&Cursor_Type) < 0)
		return NULL;
	if (PyType_Ready(&Row_Type) < 0)
		return NULL;
	m = PyModule_Create(&moduledef);
	// Exception classes
    dbError = PyErr_NewException("_db2.Error", NULL, NULL);
    Py_INCREF(dbError);
    PyModule_AddObject(m, "Error", dbError);
    dbWarning = PyErr_NewException("_db2.Warning", NULL, NULL);
    Py_INCREF(dbWarning);
    PyModule_AddObject(m, "Warning", dbWarning);
    // Decimal class
    decimalmod = PyImport_ImportModule("decimal");
    decimalcls = PyObject_GetAttrString(decimalmod, "Decimal");
	// Connection and cursor object
    Py_INCREF(&Connection_Type);
	PyModule_AddObject(m, "Connection", (PyObject *)&Connection_Type);
    Py_INCREF(&Cursor_Type);
	PyModule_AddObject(m, "Cursor", (PyObject *)&Cursor_Type);
	// Set pase ccsid
    SQLOverrideCCSID400(1208);
    return m;
}
