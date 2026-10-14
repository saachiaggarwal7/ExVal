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
std::map<std::string,std::string> parsePairs(const std::string &s) {
    std::map<std::string,std::string> m;
    size_t st=0;
    while (st<=s.size()) {
        size_t e=s.find('&',st);
        if (e==std::string::npos) e=s.size();
        std::string p=s.substr(st,e-st);
        size_t eq=p.find('=');
        if (eq!=std::string::npos) m[urlDecode(p.substr(0,eq))]=urlDecode(p.substr(eq+1));
        if (e==s.size()) break;
        st=e+1;
    }
    return m;
}
std::string html(const std::string &s) {
    std::string r;
    for (char c:s) {
        if(c=='&') r+="&amp;";
        else if(c=='<') r+="&lt;";
        else if(c=='>') r+="&gt;";
        else if(c=='\"') r+="&quot;";
        else r+=c;
    }
    return r;
}
