#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define safe_emalloc(nmemb, size, offset)  malloc(nmemb * size + offset)

/* {{{ ctype
 */

#define SUCCESS 0

#define CTYPE(iswhat) \
    size_t str_len; \
    char *p, *e; \
    p = (char *)lua_tolstring(L, 1, &str_len); \
    e = p + str_len; \
    while (p < e)  \
    { \
        if(!isalnum((int)*(unsigned char *)(p++))) \
        { \
            lua_pushboolean(L, 0); \
            return 1; \
        } \
    } \
    lua_pushboolean(L, 1); \
    return 1; \

/* }}} */

static inline char *php_memnstr(char *haystack, char *needle, int needle_len, char *end)
{
    char *p = haystack;
    char ne = needle[needle_len-1];

    if (needle_len == 1) 
    {
        return (char *)memchr(p, *needle, (end-p));
    }

    if (needle_len > end-haystack) 
    {
        return NULL;
    }

    end -= needle_len;

    while (p <= end) 
    {
        if ((p = (char *)memchr(p, *needle, (end-p+1))) && ne == p[needle_len-1]) 
        {
            if (!memcmp(needle, p, needle_len-1)) 
            {
                return p;
            }
        }

        if (p == NULL) 
        {
            return NULL;
        }

        p++;
    }

    return NULL;
}


void php_explode(lua_State *L, char *delim, size_t delim_len, char *str, size_t str_len, long limit)
{
    size_t i = 1;
    char *p1, *p2, *endp;

    endp = str + str_len;
    p1   = str;
    p2   = php_memnstr(str, delim, delim_len, endp);

    lua_newtable(L); 
    if (p2 == NULL) 
    {
        lua_pushinteger(L, i++);
        lua_pushlstring(L, p1, str_len);
        lua_settable(L, -3);
    } 
    else 
    {
        do {
                lua_pushinteger(L, i++);
                lua_pushlstring(L, p1, p2 - p1);
                lua_settable(L, -3);
                p1 = p2 + delim_len;
        } while ((p2 = php_memnstr(p1, delim, delim_len, endp)) != NULL && --limit > 1);

        if (p1 <= endp)
        {
            lua_pushinteger(L, i++);
            lua_pushlstring(L, p1, endp-p1);
            lua_settable(L, -3);
        }
            
    }
}

static int explode(lua_State *L)
{
    long  limit;
    const char *str, *delim;
    size_t str_len, delim_len;


    str   = lua_tolstring(L, 1, &str_len);
    delim = lua_tolstring(L, 2, &delim_len);

    limit = (lua_isnumber(L, 3) == 1) ? lua_tointeger(L, 3) : LONG_MAX;

    php_explode(L, (char *)delim, delim_len, (char *)str, str_len, limit);
	return 1;
}

static int long2ip(lua_State *L)
{
    lua_Integer ip;
    struct in_addr addr;

    ip = lua_tointeger(L, 1);
    addr.s_addr = htonl(ip);
    lua_pushstring(L, inet_ntoa(addr));
    return 1;
}

static int ip2long(lua_State *L)
{
    int ret;
    const char *ip;
    struct in_addr addr;

    ip  = lua_tostring(L, 1);
    ret = inet_aton(ip, &addr);
    if (ret == 0)
        lua_pushinteger(L, 0);
    else
        lua_pushinteger(L, ntohl(addr.s_addr));
    return 1;
}

static int split(lua_State *L)
{
    
    size_t str_len;
    long   limit, count;
    char *spliton, *token;
    const char *str, *delim;

    count   = 1;
    str     = lua_tolstring(L, 1, &str_len);
    delim   = lua_tostring(L, 2);
    limit   = (lua_isnumber(L, 3) == 1) ? lua_tointeger(L, 3) : LONG_MAX;

    spliton =  strndup(str, str_len);

    lua_newtable(L); 
    do {
        token = strsep(&spliton, delim);
        
        if (token && strlen(token) > 0 && count < limit) 
        {
            lua_pushinteger(L, count++);
            lua_pushstring(L, token);
            lua_settable(L, -3);
        }
    } while(token && count < limit);

    free(spliton);
    spliton = NULL;
    return 1;
}


char *php_addslashes(char *str, int length, int *new_length)
{
    /* maximum string length, worst case situation */
    char *new_str;
    char *source, *target;
    char *end;
    int local_new_length;

    if (!new_length) 
    {
        new_length = &local_new_length;
    }
    if (!str) 
    {
        *new_length = 0;
        return str;
    }

    new_str = (char *) safe_emalloc(2, (length ? length : (length = strlen(str))), 1);
    source  = str;
    end     = source + length;
    target  = new_str;

    while (source < end) 
    {
        switch (*source) 
        {
            case '\0':
                *target++ = '\\';
                *target++ = '0';
                break;
            case '\'':
            case '\"':
            case '\\':
                *target++ = '\\';
                /* break is missing *intentionally* */
            default:
                *target++ = *source;
                break;
        }

        source++;
    }

    *target     = 0;
    *new_length = target - new_str;

    new_str = (char *) realloc(new_str, *new_length + 1);
    return new_str;
}

static int addslashes(lua_State *L)
{
    int len;
    char *result;
    const char *str;
    size_t str_len;

    str    = lua_tolstring(L, 1, &str_len);
    result = php_addslashes((char *)str, str_len, &len);
    lua_pushlstring(L, result, len);
    free(result);

    return 1;
}


void php_stripslashes(char *str, size_t *len)
{
    char *s, *t;
    int l;

    if (len != NULL) 
    {
        l = *len;
    } 
    else 
    {
        l = strlen(str);
    }
    s = str;
    t = str;

    while (l > 0) {
        if (*t == '\\') 
        {
            t++;                            /* skip the slash */
            if (len != NULL) 
            {
                (*len)--;
            }
            l--;
            if (l > 0) 
            {
                if (*t == '0') 
                {
                    *s++='\0';
                    t++;
                } 
                else 
                {
                    *s++ = *t++;    /* preserve the next character */
                }
                l--;
            }
        } 
        else 
        {
            *s++ = *t++;
            l--;
        }
    }
    if (s != t) 
    {
        *s = '\0';
    }
}

static int stripslashes(lua_State *L)
{
    size_t  str_len;
    char *result;
    const char *str;
    
    
    str    = lua_tolstring(L, 1, &str_len);
    result = strndup(str, str_len);

    php_stripslashes(result, &str_len);
    lua_pushlstring(L, result, str_len);
    free(result);
    result = NULL;
    return 1;
}

static int ctype_alnum(lua_State *L)
{
    CTYPE(isalnum)
}

static int ctype_alpha(lua_State *L)
{
    CTYPE(isalpha)
}

static int ctype_digit(lua_State *L)
{
    CTYPE(isdigit)
}

static int ctype_lower(lua_State *L)
{
    CTYPE(islower)
}

static int ctype_upper(lua_State *L)
{
    CTYPE(isupper)
}

static inline int php_charmask(unsigned char *input, int len, char *mask)
{
    unsigned char *end;
    unsigned char c;
    int result = SUCCESS;

    memset(mask, 0, 256);
    for (end = input+len; input < end; input++) 
    {
            c=*input;
            if ((input+3 < end) && input[1] == '.' && input[2] == '.'
                            && input[3] >= c) {
                    memset(mask+c, 1, input[3] - c + 1);
                    input+=3;
            } 
            else if ((input+1 < end) && input[0] == '.' && input[1] == '.') {
                    /* Error, try to be as helpful as possible:
                       (a range ending/starting with '.' won't be captured here) */
                    if (end-len >= input) { /* there was no 'left' char */
                            //php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid '..'-range, no character to the left of '..'");
                            //result = FAILURE;
                            continue;
                    }
                    if (input+2 >= end) { /* there is no 'right' char */
                            //php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid '..'-range, no character to the right of '..'");
                            //result = FAILURE;
                            continue;
                    }
                    if (input[-1] > input[2]) { /* wrong order */
                            //php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid '..'-range, '..'-range needs to be incrementing");
                            //result = FAILURE;
                            continue;
                    }
                    /* FIXME: better error (a..b..c is the only left possibility?) */
                    //php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid '..'-range");
                    //result = FAILURE;
                    continue;
            } else {
                    mask[c]=1;
            }
    }
    return result;
}

/* {{{ php_trim()
 * mode 1 : trim left
 * mode 2 : trim right
 * mode 3 : trim left and right
 * what indicates which chars are to be trimmed. NULL->default (' \t\n\r\v\0')
 */
static void php_trim(lua_State *L, char *c, int len, char *what, int what_len, int mode)
{
    register int i;
    int trimmed = 0;
    char mask[256];

    if (what) 
    {
        php_charmask((unsigned char*)what, what_len, mask);
    } 
    else 
    {
        php_charmask((unsigned char*)" \n\r\t\v\0", 6, mask);
    }

    if (mode & 1) 
    {
        for (i = 0; i < len; i++) 
        {
            if (mask[(unsigned char)c[i]]) 
            {
                trimmed++;
            } 
            else 
            {
                break;
            }
        }
        len -= trimmed;
        c   += trimmed;
    }
     if (mode & 2) 
     {
        for (i = len - 1; i >= 0; i--) 
        {
            if (mask[(unsigned char)c[i]]) 
            {
                len--;
            } 
            else 
            {
                break;
            }
        }
    }

    lua_pushlstring(L, c, len);
}


static void php_do_trim(lua_State *L, int mode)
{
    char *str, *what;
    size_t  str_len, what_len;   
    
    str  = (char *)lua_tolstring(L, 1, &str_len);
    if (lua_isstring(L, 2))
    {
        what = (char *)lua_tolstring(L, 2, &what_len);
    }
    else
    {
        what     = NULL;
        what_len = 0;
    }
    

    php_trim(L, str, str_len, what, what_len, mode);
}

static int rtrim(lua_State *L)
{
    php_do_trim(L, 2);
    return 1;
}

static int ltrim(lua_State *L)
{
    php_do_trim(L, 1);
    return 1;
}

static int trim(lua_State *L)
{
    php_do_trim(L, 3);
    return 1;
}

int luaopen_php(lua_State *L)
{
    static const luaL_reg php_lib[] = {
        {"trim",         trim         },
        {"rtrim",        rtrim        },
        {"ltrim",        ltrim        },
        {"split",        split        },
        {"explode",      explode      },
        {"ip2long",      ip2long      },
        {"long2ip",      long2ip      },
        {"ctype_upper",  ctype_upper  },
        {"ctype_lower",  ctype_lower  },
        {"ctype_alpha",  ctype_alpha  },
        {"ctype_alnum",  ctype_alnum  },
        {"ctype_lower",  ctype_lower  },
        {"ctype_digit",  ctype_digit  },
        {"addslashes",   addslashes   },
        {"stripslashes", stripslashes },
        {NULL,        NULL}
    };


    lua_newtable(L);
    luaL_register(L, NULL, php_lib);
    return 1;
}
