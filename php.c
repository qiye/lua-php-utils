#define _GNU_SOURCE
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
    if(!lua_isstring(L, 1)) \
    { \
            lua_pushboolean(L, 0); \
            return 1; \
    } \
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

    spliton =  strdupa(str);

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

    //free(spliton);
    //spliton = NULL;
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

static int ctype_punct(lua_State *L)
{
    CTYPE(ispunct)
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

#ifdef __GNUC__
#define FORCE_INLINE __attribute__((always_inline)) inline
#else
#define FORCE_INLINE
#endif

static inline FORCE_INLINE uint64_t rotl64 ( uint64_t x, int8_t r )
{
    return (x << r) | (x >> (64 - r));
}

#define getblock(p, i) (p[i])
#define ROTL64(x,y)        rotl64(x,y)
#define BIG_CONSTANT(x) (x##LLU)

static inline FORCE_INLINE uint64_t fmix64 ( uint64_t k )
{
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xff51afd7ed558ccd);
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
  k ^= k >> 33;

  return k;
}

void MurmurHash3_x64_128 ( const void * key, const int len,
                           const uint32_t seed, void * out )
{
  const uint8_t * data = (const uint8_t*)key;
  const int nblocks = len / 16;
  int i;

  uint64_t h1 = seed;
  uint64_t h2 = seed;

  uint64_t c1 = BIG_CONSTANT(0x87c37b91114253d5);
  uint64_t c2 = BIG_CONSTANT(0x4cf5ad432745937f);

  //----------
  // body

  const uint64_t * blocks = (const uint64_t *)(data);

  for(i = 0; i < nblocks; i++)
  {
    uint64_t k1 = getblock(blocks,i*2+0);
    uint64_t k2 = getblock(blocks,i*2+1);

    k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;

    h1 = ROTL64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;

    k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

    h2 = ROTL64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
  }

  //----------
  // tail

  const uint8_t * tail = (const uint8_t*)(data + nblocks*16);

  uint64_t k1 = 0;
  uint64_t k2 = 0;

  switch(len & 15)
  {
  case 15: k2 ^= (uint64_t)(tail[14]) << 48;
  case 14: k2 ^= (uint64_t)(tail[13]) << 40;
  case 13: k2 ^= (uint64_t)(tail[12]) << 32;
  case 12: k2 ^= (uint64_t)(tail[11]) << 24;
  case 11: k2 ^= (uint64_t)(tail[10]) << 16;
  case 10: k2 ^= (uint64_t)(tail[ 9]) << 8;
  case  9: k2 ^= (uint64_t)(tail[ 8]) << 0;
           k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

  case  8: k1 ^= (uint64_t)(tail[ 7]) << 56;
  case  7: k1 ^= (uint64_t)(tail[ 6]) << 48;
  case  6: k1 ^= (uint64_t)(tail[ 5]) << 40;
  case  5: k1 ^= (uint64_t)(tail[ 4]) << 32;
  case  4: k1 ^= (uint64_t)(tail[ 3]) << 24;
  case  3: k1 ^= (uint64_t)(tail[ 2]) << 16;
  case  2: k1 ^= (uint64_t)(tail[ 1]) << 8;
  case  1: k1 ^= (uint64_t)(tail[ 0]) << 0;
           k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len; h2 ^= len;

  h1 += h2;
  h2 += h1;

  h1 = fmix64(h1);
  h2 = fmix64(h2);

  h1 += h2;
  h2 += h1;

  ((uint64_t*)out)[0] = h1;
  ((uint64_t*)out)[1] = h2;
}

static int L_hash(lua_State *L)
{
    char buf[128];
    uint64_t result[2];
    lua_Integer  n, len;
    const char   *key;
    
    if(!lua_isstring(L, 1))
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    n      = lua_tointeger(L, 2);
    key    = lua_tolstring(L, 1, &len);
    
    MurmurHash3_x64_128(key, len, n, result);

    lua_newtable(L); 
    lua_pushnumber(L, 0);
    len = snprintf(buf, sizeof(buf), "%" PRIu64, result[0]);
    lua_pushlstring(L, buf, len);
    lua_rawset(L, -3);
    
    lua_pushnumber(L, 1);
    len = snprintf(buf, sizeof(buf), "%" PRIu64, result[1]);
    lua_pushlstring(L, buf, len);
    lua_rawset(L, -3);
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

#define DELTA 0x9e3779b9
#define MX (((z>>5^y<<2) + (y>>3^z<<4)) ^ ((sum^y) + (k[(p&3)^e] ^ z)))

void btea(uint32_t *v, int n, uint32_t const k[4]) {
        uint32_t y, z, sum;
        unsigned p, rounds, e;
        if (n > 1) {          /* Coding Part */
                rounds = 8 + 52/n;
                sum = 0;
                z = v[n-1];
                do {
                        sum += DELTA;
                        e = (sum >> 2) & 3;
                        for (p=0; p<n-1; p++) {
                                y = v[p+1]; 
                                z = v[p] += MX;
                        }
                        y = v[0];
                        z = v[n-1] += MX;
                } while (--rounds);
        } else if (n < -1) {  /* Decoding Part */
                n = -n;
                rounds = 8 + 52/n;
                sum = rounds*DELTA;
                y = v[0];
                do {
                        e = (sum >> 2) & 3;
                        for (p=n-1; p>0; p--) {
                                z = v[p-1];
                                y = v[p] -= MX;
                        }
                        z = v[n-1];
                        y = v[0] -= MX;
                } while ((sum -= DELTA) != 0);
        }
}

void getkey( lua_State *L, const unsigned int pos, uint32_t *k ){
        unsigned char buf[5];
        int i,j;
        size_t keyLength;
        const unsigned char *key = luaL_checklstring( L, pos, &keyLength );
        buf[4] = 0;
        for( i=0,j=0; i<(keyLength-3), j<4; i+=4,j++ ){
                buf[0] = key[i];
                buf[1] = key[i+1];
                buf[2] = key[i+2];
                buf[3] = key[i+3];
                k[j] = strtoul( buf, NULL, 16 );
        }
}

inline size_t align( size_t n, size_t a ){
        if( (n & (a-1)) == 0 ){
                return n;
        }else{
                return n + a - (n & (a-1));
        }
}

static int xxtea_encrypt( lua_State *L ){
        uint32_t k[4];
        unsigned char *buf;
        size_t l;

        size_t textLength;
        const unsigned char *text = luaL_checklstring( L, 1, &textLength );
        getkey( L, 2, k );

        l = align(textLength + sizeof(textLength), 4);
        buf = malloc( l );
        memcpy( buf, (unsigned char *)&textLength, sizeof(textLength) );
        memcpy( &buf[sizeof(textLength)], text, textLength );

        btea( (uint32_t *)(buf+sizeof(textLength)), (l-sizeof(textLength))/sizeof(uint32_t), k );

        lua_pushlstring( L, buf, l );
        free( buf );
        return 1;
}

static int xxtea_decrypt( lua_State *L ){
        uint32_t k[4];
        unsigned char *buf;
        size_t l, offset;
        size_t retLength;

        size_t textLength;
        const unsigned char *text = luaL_checklstring( L, 1, &textLength );
        getkey( L, 2, k );
        
        l = align(textLength, 4);
        buf = malloc( l );
        memcpy( buf, text, textLength );
        
        btea( (uint32_t *)(buf+sizeof(retLength)), -((textLength-sizeof(retLength))/sizeof(uint32_t)), k );
        
        memcpy( (unsigned char *)&retLength, buf, sizeof(retLength) );
        if( retLength > (l - sizeof(retLength)) )  retLength = l - sizeof(retLength);

        lua_pushlstring( L, &buf[sizeof(retLength)], retLength );
        free( buf );
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
        {"trim",           trim          },
        {"rtrim",          rtrim         },
        {"ltrim",          ltrim         },
        {"split",          split         },
        {"genid",          genid         },
        {"str_pad",        str_pad       },
        {"hash",           L_hash        },
        {"strncmp",        L_strncmp     },
        {"explode",        explode       },
        {"ip2long",        ip2long       },
        {"long2ip",        long2ip       },
        {"long2ip",        long2ip       },
        {"xxtea_decrypt",  xxtea_decrypt },
        {"xxtea_encrypt",  xxtea_encrypt },
        {"ctype_upper",    ctype_upper   },
        {"ctype_lower",    ctype_lower   },
        {"ctype_alpha",    ctype_alpha   },
        {"ctype_alnum",    ctype_alnum   },
        {"ctype_lower",    ctype_lower   },
        {"ctype_digit",    ctype_digit   },
        {"ctype_punct",    ctype_punct   },
        {"addslashes",     addslashes    },
        {"stripslashes",   stripslashes  },
        {NULL,             NULL          }
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
