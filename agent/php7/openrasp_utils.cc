/*
 * Copyright 2017-2018 Baidu Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "openrasp.h"
#include "openrasp_ini.h"
#include "openrasp_utils.h"
extern "C"
{
#include "php_ini.h"
#include "ext/pcre/php_pcre.h"
#include "ext/standard/file.h"
#include "Zend/zend_builtin_functions.h"
}
#include <string>

std::string format_debug_backtrace_str()
{
    zval trace_arr;
    std::string buffer;
    zend_fetch_debug_backtrace(&trace_arr, 0, 0, 0);
    if (Z_TYPE(trace_arr) == IS_ARRAY)
    {
        int i = 0;
        HashTable *hash_arr = Z_ARRVAL(trace_arr);
        zval *ele_value = NULL;
        ZEND_HASH_FOREACH_VAL(hash_arr, ele_value)
        {
            if (++i > openrasp_ini.log_maxstack)
            {
                break;
            }
            if (Z_TYPE_P(ele_value) != IS_ARRAY)
            {
                continue;
            }
            zval *trace_ele;
            if ((trace_ele = zend_hash_str_find(Z_ARRVAL_P(ele_value), ZEND_STRL("file"))) != NULL &&
                Z_TYPE_P(trace_ele) == IS_STRING)
            {
                buffer.append(Z_STRVAL_P(trace_ele), Z_STRLEN_P(trace_ele));
            }
            buffer.push_back('(');
            if ((trace_ele = zend_hash_str_find(Z_ARRVAL_P(ele_value), ZEND_STRL("function"))) != NULL &&
                Z_TYPE_P(trace_ele) == IS_STRING)
            {
                buffer.append(Z_STRVAL_P(trace_ele), Z_STRLEN_P(trace_ele));
            }
            buffer.push_back(':');
            //line number
            if ((trace_ele = zend_hash_str_find(Z_ARRVAL_P(ele_value), ZEND_STRL("line"))) != NULL &&
                Z_TYPE_P(trace_ele) == IS_LONG)
            {
                buffer.append(std::to_string(Z_LVAL_P(trace_ele)));
            }
            else
            {
                buffer.append("-1");
            }
            buffer.append(")\n");
        }
        ZEND_HASH_FOREACH_END();
    }
    zval_dtor(&trace_arr);
    if (buffer.length() > 0)
    {
        buffer.pop_back();
    }
    return buffer;
}

void format_debug_backtrace_str(zval *backtrace_str)
{
    auto trace = format_debug_backtrace_str(TSRMLS_C);
    ZVAL_STRINGL(backtrace_str, trace.c_str(), trace.length());
}

std::vector<std::string> format_debug_backtrace_arr()
{
    zval trace_arr;
    std::vector<std::string> array;
    zend_fetch_debug_backtrace(&trace_arr, 0, 0, 0);
    if (Z_TYPE(trace_arr) == IS_ARRAY)
    {
        int i = 0;
        HashTable *hash_arr = Z_ARRVAL(trace_arr);
        zval *ele_value = NULL;
        ZEND_HASH_FOREACH_VAL(hash_arr, ele_value)
        {
            if (++i > openrasp_ini.log_maxstack)
            {
                break;
            }
            if (Z_TYPE_P(ele_value) != IS_ARRAY)
            {
                continue;
            }
            std::string buffer;
            zval *trace_ele;
            if ((trace_ele = zend_hash_str_find(Z_ARRVAL_P(ele_value), ZEND_STRL("file"))) != NULL &&
                Z_TYPE_P(trace_ele) == IS_STRING)
            {
                buffer.append(Z_STRVAL_P(trace_ele), Z_STRLEN_P(trace_ele));
            }
            if ((trace_ele = zend_hash_str_find(Z_ARRVAL_P(ele_value), ZEND_STRL("function"))) != NULL &&
                Z_TYPE_P(trace_ele) == IS_STRING)
            {
                buffer.push_back('@');
                buffer.append(Z_STRVAL_P(trace_ele), Z_STRLEN_P(trace_ele));
            }
            array.push_back(buffer);
        }
        ZEND_HASH_FOREACH_END();
    }
    zval_dtor(&trace_arr);
    return array;
}

void format_debug_backtrace_arr(zval *backtrace_arr)
{
    auto array = format_debug_backtrace_arr();
    for (auto &str : array)
    {
        add_next_index_stringl(backtrace_arr, str.c_str(), str.length());
    }
}

void openrasp_error(int type, int error_code, const char *format, ...)
{
    va_list arg;
    char *message = nullptr;
    va_start(arg, format);
    vspprintf(&message, 0, format, arg);
    va_end(arg);
    zend_error(type, "[OpenRASP] %d %s", error_code, message);
    efree(message);
}

int recursive_mkdir(const char *path, int len, int mode)
{
    struct stat sb;
    if (VCWD_STAT(path, &sb) == 0 && (sb.st_mode & S_IFDIR) != 0)
    {
        return 1;
    }
    char *dirname = estrndup(path, len);
    int dirlen = php_dirname(dirname, len);
    int rst = recursive_mkdir(dirname, dirlen, mode);
    efree(dirname);
    if (rst)
    {
#ifndef PHP_WIN32
        mode_t oldmask = umask(0);
        rst = VCWD_MKDIR(path, mode);
        umask(oldmask);
#else
        rst = VCWD_MKDIR(path, mode);
#endif
        if (rst == 0 || EEXIST == errno)
        {
            return 1;
        }
        openrasp_error(E_WARNING, CONFIG_ERROR, _("Could not create directory '%s': %s"), path, strerror(errno));
    }
    return 0;
}

const char *fetch_url_scheme(const char *filename)
{
    if (nullptr == filename)
    {
        return nullptr;
    }
    const char *p;
    for (p = filename; isalnum((int)*p) || *p == '+' || *p == '-' || *p == '.'; p++)
        ;
    if ((*p == ':') && (p - filename > 1) && (p[1] == '/') && (p[2] == '/'))
    {
        return p;
    }
    return nullptr;
}

long fetch_time_offset()
{
    time_t t = time(NULL);
    struct tm lt = {0};
    localtime_r(&t, &lt);
    return lt.tm_gmtoff;
}

void openrasp_scandir(const std::string dir_abs, std::vector<std::string> &plugins, std::function<bool(const char *filename)> file_filter)
{
    DIR *dir;
    std::string result;
    struct dirent *ent;
    if ((dir = opendir(dir_abs.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            if (file_filter)
            {
                if (file_filter(ent->d_name))
                {
                    plugins.push_back(std::string(ent->d_name));
                }
            }
        }
        closedir(dir);
    }
}

bool same_day_in_current_timezone(long src, long target, long offset)
{
    long day = 24 * 60 * 60;
    return ((src + offset) / day == (target + offset) / day);
}

zend_string *openrasp_format_date(char *format, int format_len, time_t ts)
{
    char buffer[128];
    struct tm *tm_info;

    time(&ts);
    tm_info = localtime(&ts);

    strftime(buffer, 64, format, tm_info);
    return zend_string_init(buffer, strlen(buffer), 0);
}

void openrasp_pcre_match(zend_string *regex, zend_string *subject, zval *return_value)
{
    pcre_cache_entry *pce;
    zval *subpats = NULL;
    zend_long flags = 0;
    zend_long start_offset = 0;
    int global = 0;

    if (ZEND_SIZE_T_INT_OVFL(ZSTR_LEN(subject)))
    {
        RETURN_FALSE;
    }

    if ((pce = pcre_get_compiled_regex_cache(regex)) == NULL)
    {
        RETURN_FALSE;
    }

    pce->refcount++;
    php_pcre_match_impl(pce, ZSTR_VAL(subject), (int)ZSTR_LEN(subject), return_value, subpats,
                        global, 0, flags, start_offset);
    pce->refcount--;
}

long get_file_st_ino(std::string filename)
{
    struct stat sb;
    if (VCWD_STAT(filename.c_str(), &sb) == 0 && (sb.st_mode & S_IFREG) != 0)
    {
        return (long)sb.st_ino;
    }
    return 0;
}