lua-php-utils
=============

PHP-style utility functions for LuaJIT applications

目前支持的php函数(相关函数均从PHP C源码中移植过来),使用方法请参考PHP手册
trim
rtrim
ltrim
split
explode
ip2long
long2ip
ctype_upper
ctype_lower
ctype_alpha
ctype_alnum
ctype_lower
ctype_digit
addslashes
stripslashes

不断的增加新PHP函数

编译方法
gcc -O3 -shared -fPIC -I/usr/local/include/luajit-2.0/   -c -o php.o php.c
gcc -O3 -shared -fPIC -I/usr/local/include/luajit-2.0/ -ansi -pedantic -Wall -o php.so php.o

运行例子程序
luajit-2.0.2 test.lua

