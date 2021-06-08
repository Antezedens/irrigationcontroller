#include <string>
#include <cstdarg>
#include <syslog.h>
#include <cstring>
#include <unistd.h>
#include "Util.h"
#include <fcntl.h>

std::string format(const char* fmt, ...) {
	{
		char buffer[1024];

		va_list ap;
		va_start(ap, fmt);
		auto result = vsnprintf(buffer, sizeof(buffer), fmt, ap);
		va_end(ap);

		if (result < static_cast<int>(sizeof(buffer) - 1)) {
			return std::string(buffer, result);
		}
	}

	{
		char* malloced = nullptr;
		va_list ap;
		va_start(ap, fmt);
		auto result = vasprintf(&malloced, fmt, ap);
		va_end(ap);

		if (result < 0) {
			return std::string("out of memory");
		}

		auto returnvalue = std::string(malloced, result + 1);
		free(malloced);

		return returnvalue;
	}
}

void writeFile(const char* file, const char* string) {
	FILE* f = fopen(file, "w");
	if (not f) {
		syslog(LOG_ERR, "could not write file %s: %s", file, strerror(errno));
		return;
	}
	fputs(string, f);
	fclose(f);
}

std::string readFileMax1024byte(const char* file) {
	FILE* f = fopen(file, "r");
	if (not f) {
		syslog(LOG_ERR, "could not read file %s: %s", file, strerror(errno));
		return {};
	}
	char buffer[1024];
	auto bytes = fread(buffer, 1, 1024, f);
	fclose(f);
	return std::string(buffer, bytes);
}

bool fileExists(const char* file) {
	const int fileDesc = open(file, O_RDONLY);
	if (fileDesc < 0) {
		return false;
	}
	close(fileDesc);
	return true;
}
