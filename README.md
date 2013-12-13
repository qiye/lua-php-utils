lua-php-utils
=============

PHP-style utility functions for LuaJIT applications

目前支持的php函数(相关函数均从PHP C源码中移植过来),使用方法请参考PHP手册(将不断的增加新PHP函数)
-------------

1. str_pad
1. genid 获取一个唯一的ID
1. trim
1. rtrim
1. ltrim
1. split
1. strncmp
1. explode
1. hash(key, seed)
1. ip2long
1. long2ip
1. ctype_upper
1. ctype_lower
1. ctype_alpha
1. ctype_alnum
1. ctype_lower
1. ctype_digit
1. addslashes
1. stripslashesa
1. xxtea_decrypt
1. xxtea_encrypt


编译方法
-------------
<pre>
gcc -O3 -shared -fPIC -I/usr/local/include/luajit-2.0/   -c -o php.o php.c
gcc -O3 -shared -fPIC -I/usr/local/include/luajit-2.0/ -ansi -pedantic -Wall -o php.so php.o
</pre>

运行例子程序
-------------
luajit-2.0.2 test.lua

