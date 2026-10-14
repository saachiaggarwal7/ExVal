#include "common.h"
#include <cstdlib>
#include <sstream>
#include <string>
#include <unistd.h>

std::map<std::string, Session> sessions;
std::mutex sessionMutex;

std::string urlDecode(const std::string &s) {
    std::string r;
    for (size_t i=0; i<s.size(); ++i) {
        if (s[i]=='%' && i+2<s.size()) {
            std::string h=s.substr(i+1,2);
            r += static_cast<char>(strtol(h.c_str(),nullptr,16));
            i += 2;
        } else if (s[i]=='+') r += ' ';
        else r += s[i];
    }
    return r;
}
