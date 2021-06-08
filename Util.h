#pragma once

#define FORMAT_ATTRIB(p) __attribute__ ((__format__ (__printf__, p,p+1)))

std::string format(const char* fmt, ...) FORMAT_ATTRIB(1);

bool fileExists(const char* file);
void writeFile(const char* file, const char* string);
std::string readFileMax1024byte(const char* file);