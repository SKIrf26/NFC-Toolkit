#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <strings.h>
#include <vector>
#define HEX 16
uint32_t millis();
inline void delay(uint32_t){}
inline int digitalRead(int){return 1;}
inline void *ps_malloc(size_t n){return malloc(n);}
class String : public std::string {
public:
  String() {}
  String(const char*s):std::string(s?s:""){}
  String(const std::string&s):std::string(s){}
  String(char c):std::string(1,c){}
  String(int v, int base=10){char b[32]; snprintf(b,32,base==16?"%x":"%d",v); assign(b);}
  String(unsigned v){assign(std::to_string(v));}
  String(long v){assign(std::to_string(v));}
  String(unsigned long v){assign(std::to_string(v));}
  String(unsigned char v){assign(std::to_string(v));}
  unsigned length() const {return size();}
  String substring(size_t a, size_t b=std::string::npos) const { if(a>size()) return String(); return String(substr(a, b==std::string::npos? npos : b-a)); }
  int indexOf(char c, size_t from=0) const { size_t p=find(c,from); return p==npos?-1:(int)p; }
  int indexOf(const char*s, size_t from=0) const { size_t p=find(s,from); return p==npos?-1:(int)p; }
  void replace(const char*a,const char*b){ std::string A(a),B(b); size_t p=0; while((p=find(A,p))!=npos){ std::string::replace(p,A.size(),B); p+=B.size(); } }
  String& operator+=(const String&o){append(o);return *this;}
  String& operator+=(const char*o){append(o);return *this;}
  String& operator+=(char c){push_back(c);return *this;}
  friend String operator+(const String&a,const String&b){return String(std::string(a)+std::string(b));}
  friend String operator+(const String&a,const char*b){return String(std::string(a)+b);}
  friend String operator+(const char*a,const String&b){return String(std::string(a)+std::string(b));}
};
