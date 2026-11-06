#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <strings.h>
#define HEX 16
class String : public std::string {
public:
  String() {}
  String(const char*s):std::string(s?s:""){}
  String(const std::string&s):std::string(s){}
  String(char c):std::string(1,c){}
  String(int v, int base=10){char b[32]; snprintf(b,32,base==16?"%x":"%d",v); assign(b);}
  String(unsigned v){assign(std::to_string(v));}
  String(unsigned long v){assign(std::to_string(v));}
  unsigned length() const {return size();}
  void reserve(size_t n){std::string::reserve(n);}
  String& operator+=(const String&o){append(o);return *this;}
  String& operator+=(const char*o){append(o);return *this;}
  String& operator+=(char c){push_back(c);return *this;}
  friend String operator+(const String&a,const String&b){return String(std::string(a)+std::string(b));}
  friend String operator+(const String&a,const char*b){return String(std::string(a)+b);}
  friend String operator+(const char*a,const String&b){return String(std::string(a)+std::string(b));}
};
