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
#include "xxtea.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#define emalloc(size)       malloc(size)
#define ecalloc(count,size) calloc(count,size)
#define erealloc(ptr,size)  realloc(ptr,size)
#define efree(ptr)          free(ptr)

#define STR_PAD_LEFT     0
#define STR_PAD_RIGHT    1
#define STR_PAD_BOTH     2

#define safe_emalloc(nmemb, size, offset)  malloc(nmemb * size + offset)

#define CTYPE(iswhat) \
    size_t str_len; \
    char *p, *e; \
    p = (char *)lua_tolstring(L, 1, &str_len); \
    if(str_len < 1) \
    { \
            lua_pushboolean(L, 0); \
            return 1; \
    } \
    e = p + str_len; \
    while (p < e)  \
    { \
        if(!iswhat((int)*(unsigned char *)(p++))) \
        { \
            lua_pushboolean(L, 0); \
            return 1; \
        } \
    } \
    lua_pushboolean(L, 1); \
    return 1; \

/* }}} */

static __inline__ uint64_t rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static xxtea_long *xxtea_to_long_array(unsigned char *data, xxtea_long len, int include_length, xxtea_long *ret_len) {
    xxtea_long i, n, *result;
    n = len >> 2;
    n = (((len & 3) == 0) ? n : n + 1);
    if (include_length) {
        result = (xxtea_long *)emalloc((n + 1) << 2);
        result[n] = len;
        *ret_len = n + 1;
    } else {
        result = (xxtea_long *)emalloc(n << 2);
        *ret_len = n;
    }
    memset(result, 0, n << 2);
    for (i = 0; i < len; i++) {
        result[i >> 2] |= (xxtea_long)data[i] << ((i & 3) << 3);
    }
    return result;
}

static unsigned char *xxtea_to_byte_array(xxtea_long *data, xxtea_long len, int include_length, xxtea_long *ret_len) {
    xxtea_long i, n, m;
    unsigned char *result;
    n = len << 2;
    if (include_length) {
        m = data[len - 1];
        if ((m < n - 7) || (m > n - 4)) return NULL;
        n = m;
    }
    result = (unsigned char *)emalloc(n + 1);
    for (i = 0; i < n; i++) {
        result[i] = (unsigned char)((data[i >> 2] >> ((i & 3) << 3)) & 0xff);
    }
    result[n] = '\0';
    *ret_len = n;
    return result;
}

static unsigned char *php_xxtea_encrypt(unsigned char *data, xxtea_long len, unsigned char *key, xxtea_long *ret_len) {
    unsigned char *result;
    xxtea_long *v, *k, v_len, k_len;
    v = xxtea_to_long_array(data, len, 1, &v_len);
    k = xxtea_to_long_array(key, 16, 0, &k_len);
    xxtea_long_encrypt(v, v_len, k);
    result = xxtea_to_byte_array(v, v_len, 0, ret_len);
    efree(v);
    efree(k);
    return result;
}

static unsigned char *php_xxtea_decrypt(unsigned char *data, xxtea_long len, unsigned char *key, xxtea_long *ret_len) {
    unsigned char *result;
    xxtea_long *v, *k, v_len, k_len;
    v = xxtea_to_long_array(data, len, 0, &v_len);
    k = xxtea_to_long_array(key, 16, 0, &k_len);
    xxtea_long_decrypt(v, v_len, k);
    result = xxtea_to_byte_array(v, v_len, 1, ret_len);
    efree(v);
    efree(k);
    return result;
}


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
    int result = 0;

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

static int L_strncmp(lua_State *L)
{
    const char *str1 = lua_tostring(L, 1);
    const char *str2 = lua_tostring(L, 2);
    lua_Number n     = lua_tonumber(L, 3);

    if(strncmp(str1, str2, n) == 0)
        lua_pushboolean(L, 1);
    else
        lua_pushboolean(L, 0);

    return 1;
}


static unsigned int GenHashFunction(const void *key, int len, uint32_t seed) 
{
    /* 'm' and 'r' are mixing constants generated offline.
     They're not really 'magic', they just happen to work well.  */
  
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) 
    {
        uint32_t k = *(uint32_t*)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    /* Handle the last few bytes of the input array  */
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0]; h *= m;
    };

    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (unsigned int)h;
}

static int L_hash(lua_State *L)
{
    unsigned int result;
    lua_Integer  n, len;
    const char   *key;
    
    if(!lua_isstring(L, 1))
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    n      = lua_tointeger(L, 2);
    key    = lua_tolstring(L, 1, &len);
    result = GenHashFunction(key, len, n);

    lua_pushinteger(L, result);
    return 1;
}

static int genid(lua_State *L)
{
    char       buf[32];
    size_t     len, r;
    uint64_t   tsc;

    tsc = rdtsc();

    memset(buf, 0, 32);
    srand(tsc);

    r   = rand() % 9+ 0;  
    len = snprintf(buf, sizeof(buf), "%" PRIu64 "%d", tsc, r);
    

    lua_pushlstring(L, buf, len);
    return 1;
}

static int xxtea_encrypt(lua_State *L)
{
    unsigned char *data, *key;
    unsigned char *result;
    xxtea_long data_len, key_len, ret_length;
    
    data = (unsigned char *)lua_tolstring(L, 1, (size_t *)&data_len);
    key  = (unsigned char *)lua_tolstring(L, 2, (size_t *)&key_len);

    if(key_len != 16)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    result = php_xxtea_encrypt(data, data_len, key, &ret_length);
    if (result != NULL) 
    {
        lua_pushlstring(L, (char *)result, ret_length);
        free(result);
    }
    else 
        lua_pushboolean(L, 0);

    return 1;
}

static int str_pad(lua_State *L)
{
size_t  num_pad_chars;   /* Number of padding characters (total - input size) */
char    *result = NULL;  /* Resulting string */
int     result_len = 0;  /* Length of the resulting string */
   
   int     i, left_pad=0, right_pad=0;
   
   const char *input;
   size_t input_len;
   long pad_length;
   
const char  *pad_str_val = " ";     // Pointer to padding string 
size_t pad_str_len       = 1;    // Length of the padding string 
   long   pad_type_val      = STR_PAD_RIGHT; /* The padding type value */
   
   input        = lua_tolstring(L, 1, &input_len);
   pad_length   = lua_tonumber(L, 2);
   pad_str_val  = lua_tolstring(L, 3, &pad_str_len);
   pad_type_val = lua_tonumber(L, 4);
   
if (pad_length <= 0 || (pad_length - input_len) <= 0) 
   {
       lua_pushlstring(L, input, input_len);
return 1;
}

if (pad_str_len == 0) 
   {
       lua_pushnil(L);
       lua_pushlstring(L, "Padding string cannot be empty", 30);
return 1;
}

if (pad_type_val < STR_PAD_LEFT || pad_type_val > STR_PAD_BOTH) 
   {    
       lua_pushnil(L);
       lua_pushlstring(L, "Padding type has to be STR_PAD_LEFT, STR_PAD_RIGHT, or STR_PAD_BOTH", 67);
       return 1;
}
   
   
num_pad_chars = pad_length - input_len;
if (num_pad_chars >= INT16_MAX) 
   {
       lua_pushnil(L);
       lua_pushlstring(L, "Padding length is too long", 26);
return 1;
}
       
result = (char *)emalloc(input_len + num_pad_chars + 1);

// We need to figure out the left/right padding lengths.
switch (pad_type_val) 
   {
case STR_PAD_RIGHT:
left_pad  = 0;
right_pad = num_pad_chars;
break;

case STR_PAD_LEFT:
left_pad  = num_pad_chars;
right_pad = 0;
break;

case STR_PAD_BOTH:
left_pad  = num_pad_chars / 2;
right_pad = num_pad_chars - left_pad;
break;
}

// First we pad on the left. 
for (i = 0; i < left_pad; i++)
result[result_len++] = pad_str_val[i % pad_str_len];

// Then we copy the input string. 
memcpy(result + result_len, input, input_len);
result_len += input_len;

// Finally, we pad on the right. 
for (i = 0; i < right_pad; i++)
result[result_len++] = pad_str_val[i % pad_str_len];

result[result_len] = '\0';
   
   lua_pushlstring(L, result, result_len);
   efree(result);
   
   return 1;
}

int luaopen_php(lua_State *L)
{
    static const luaL_reg php_lib[] = {
        {"trim",         trim          },
        {"rtrim",        rtrim         },
        {"ltrim",        ltrim         },
        {"split",        split         },
        {"genid",        genid         },
        {"str_pad",      str_pad       },
        {"hash",         L_hash        },
        {"strncmp",      L_strncmp     },
        {"explode",      explode       },
        {"ip2long",      ip2long       },
        {"long2ip",      long2ip       },
        {"long2ip",      long2ip       },
        {"xxtea_encrypt",  xxtea_encrypt },
        {"ctype_upper",    ctype_upper   },
        {"ctype_lower",    ctype_lower   },
        {"ctype_alpha",    ctype_alpha   },
        {"ctype_alnum",  ctype_alnum   },
        {"ctype_lower",  ctype_lower   },
        {"ctype_digit",  ctype_digit   },
        {"addslashes",   addslashes    },
        {"stripslashes", stripslashes  },
        {NULL,        NULL}
    };


    lua_newtable(L);
    luaL_register(L, NULL, php_lib);

    lua_pushstring(L,"STR_PAD_LEFT");
    lua_pushnumber(L,STR_PAD_LEFT);
    lua_settable(L,-3);

    lua_pushstring(L,"STR_PAD_RIGHT");
    lua_pushnumber(L,STR_PAD_RIGHT);
    lua_settable(L,-3);

    lua_pushstring(L,"STR_PAD_BOTH");
    lua_pushnumber(L,STR_PAD_BOTH);
    lua_settable(L,-3);
    return 1;
}
