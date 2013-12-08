local php = require("php");


function print_r ( t ) 
    local print_r_cache={}
    local function sub_print_r(t,indent)
        if (print_r_cache[tostring(t)]) then
            print(indent.."*"..tostring(t))
        else
            print_r_cache[tostring(t)]=true
            if (type(t)=="table") then
                for pos,val in pairs(t) do
                    if (type(val)=="table") then
                        print(indent.."["..pos.."] => "..tostring(t).." {")
                        sub_print_r(val,indent..string.rep(" ",string.len(pos)+8))
                        print(indent..string.rep(" ",string.len(pos)+6).."}")
                    else
                        print(indent.."["..pos.."] => "..tostring(val))
                    end
                end
            else
                print(indent..tostring(t))
            end
        end
    end
    sub_print_r(t,"  ")
end

--local data = php.xxtea_encrypt("olsen", "K9bLgYZB8TsTG3h8");
--print(data);

print(php.str_pad("Alien", 10, "-=", php.STR_PAD_LEFT));

print("genid=", php.genid());
print("hash(olsen, 18)=", php.hash("olsen", 18));

local str  = "https:/gist.github.com/nrk/31175";
local data = php.explode(str, "/");
print_r(data);

local ip  = php.ip2long("192.168.1.2");
print("192.168.1.2=", ip);
print("ip", php.long2ip(ip));

print("split");
local data  = php.split("http://stackoverflow.com/questions/908759/using-strsep-with-dynamic-array-of-strings-in-c", "/-");
print_r(data);

print("addslashes");
local str  = "Is your name O'reilly?";
local data = php.addslashes(str);
print(data);

print("stripslashes");
print(php.stripslashes(data));

print("ctype_alnum");
if php.ctype_alnum("kiosl") then
    print("match")
else
    print("not match")
end


print("trim");
print(php.trim(" dasd 解决 "));
