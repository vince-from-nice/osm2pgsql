/* Helper functions for the postgresql connections */
#include "pgsql.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <libpq-fe.h>
#include <boost/format.hpp>

void escape(const char* src, std::string& dst)
{
    for(; *src; ++src)
    {
        switch(*src) {
            case '\\':  dst.append("\\\\"); break;
            //case 8:   dst.append("\\\b"); break;
            //case 12:  dst.append("\\\f"); break;
            case '\n':  dst.append("\\\n"); break;
            case '\r':  dst.append("\\\r"); break;
            case '\t':  dst.append("\\\t"); break;
            //case 11:  dst.append("\\\v"); break;
            default:    dst.push_back(*src); break;
        }
    }
}

void escape(char *out, const int len, const char *in)
{
    /* Apply escaping of TEXT COPY data
    Escape: backslash itself, newline, carriage return, and the current delimiter character (tab)
    file:///usr/share/doc/postgresql-8.1.8/html/sql-copy.html
    */
    int count = 0;
    const char *old_in = in, *old_out = out;

    if (!len)
        return;

    while(*in && count < len-3) {
        switch(*in) {
            case '\\': *out++ = '\\'; *out++ = '\\'; count+= 2; break;
                /*    case    8: *out++ = '\\'; *out++ = '\b'; count+= 2; break; */
                /*    case   12: *out++ = '\\'; *out++ = '\f'; count+= 2; break; */
            case '\n': *out++ = '\\'; *out++ = '\n'; count+= 2; break;
            case '\r': *out++ = '\\'; *out++ = '\r'; count+= 2; break;
            case '\t': *out++ = '\\'; *out++ = '\t'; count+= 2; break;
                /*    case   11: *out++ = '\\'; *out++ = '\v'; count+= 2; break; */
            default:   *out++ = *in; count++; break;
        }
        in++;
    }
    *out = '\0';

    if (*in)
        fprintf(stderr, "%s truncated at %d chars: %s\n%s\n", __FUNCTION__, count, old_in, old_out);
}

boost::shared_ptr<PGresult> pgsql_exec_simple(PGconn *sql_conn, const ExecStatusType expect, const std::string& sql)
{
    return pgsql_exec_simple(sql_conn, expect, sql.c_str());
}

boost::shared_ptr<PGresult> pgsql_exec_simple(PGconn *sql_conn, const ExecStatusType expect, const char *sql)
{
    PGresult* res;
#ifdef DEBUG_PGSQL
    fprintf( stderr, "Executing: %s\n", sql );
#endif
    res = PQexec(sql_conn, sql);
    if (PQresultStatus(res) != expect) {
        PQclear(res);
        throw std::runtime_error((boost::format("%1% failed: %2%\n") % sql % PQerrorMessage(sql_conn)).str());
    }
    return boost::shared_ptr<PGresult>(res, &PQclear);
}

int pgsql_exec(PGconn *sql_conn, const ExecStatusType expect, const char *fmt, ...)
{

    va_list ap;
    char *sql, *nsql;
    int n, size = 100;

    /* Based on vprintf manual page */
    /* Guess we need no more than 100 bytes. */

    if ((sql = (char *)malloc(size)) == NULL)
        throw std::runtime_error("Memory allocation failed in pgsql_exec");

    while (1) {
        /* Try to print in the allocated space. */
        va_start(ap, fmt);
        n = vsnprintf(sql, size, fmt, ap);
        va_end(ap);
        /* If that worked, return the string. */
        if (n > -1 && n < size)
            break;
        /* Else try again with more space. */
        if (n > -1)    /* glibc 2.1 */
            size = n+1; /* precisely what is needed */
        else           /* glibc 2.0 */
            size *= 2;  /* twice the old size */
        if ((nsql = (char *)realloc (sql, size)) == NULL) {
            free(sql);
            throw std::runtime_error("Memory re-allocation failed in pgsql_exec");
        } else {
            sql = nsql;
        }
    }

#ifdef DEBUG_PGSQL
    fprintf( stderr, "Executing: %s\n", sql );
#endif
    PGresult* res = PQexec(sql_conn, sql);
    if (PQresultStatus(res) != expect) {
        std::string err_msg = (boost::format("%1% failed: %2%") % sql % PQerrorMessage(sql_conn)).str();
        free(sql);
        PQclear(res);
        throw std::runtime_error(err_msg);
    }
    free(sql);
    PQclear(res);
    return 0;
}

void pgsql_CopyData(const char *context, PGconn *sql_conn, const char *sql)
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "%s>>> %s\n", context, sql );
#endif
    int r = PQputCopyData(sql_conn, sql, strlen(sql));
    switch(r)
    {
        //need to wait for write ready
        case 0:
            throw std::runtime_error((boost::format("%1% - bad result during COPY, data %2%") % context % sql % PQerrorMessage(sql_conn)).str());
            break;
        //error occurred
        case -1:
            throw std::runtime_error((boost::format("%1%: %2% - bad result during COPY, data %3%") % PQerrorMessage(sql_conn) % context % sql).str());
            break;
        //other possibility is 1 which means success
    }
}

PGresult *pgsql_execPrepared( PGconn *sql_conn, const char *stmtName, const int nParams, const char *const * paramValues, const ExecStatusType expect)
{
#ifdef DEBUG_PGSQL
    fprintf( stderr, "ExecPrepared: %s\n", stmtName );
#endif
    //run the prepared statement
    PGresult *res = PQexecPrepared(sql_conn, stmtName, nParams, paramValues, NULL, NULL, 0);
    if(PQresultStatus(res) != expect)
    {
        std::string message = (boost::format("%1% failed: %2%(%3%)\n") % stmtName % PQerrorMessage(sql_conn) % PQresultStatus(res)).str();
        if(nParams)
        {
             message += "Arguments were: ";
            for(int i = 0; i < nParams; i++)
            {
                message += paramValues[i];
                message += ", ";
            }
        }
        PQclear(res);
        throw std::runtime_error(message);
    }

    //TODO: this seems a bit strange
    //if you decided you wanted to expect something other than this you didnt want to use the result?
    if( expect != PGRES_TUPLES_OK )
    {
        PQclear(res);
        res = NULL;
    }
    return res;
}
