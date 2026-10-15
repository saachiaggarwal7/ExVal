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
std::string page(const std::string &title,const std::string &body) {
    return "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>ExVal - "+html(title)+"</title><style>body{font-family:Arial;margin:0;background:#f4f6fb;color:#202534}header{background:#173b72;color:white;padding:16px 28px;display:flex;justify-content:space-between}main{max-width:1050px;margin:25px auto;background:white;padding:25px;border-radius:12px;box-shadow:0 2px 10px #ddd}h1,h2{color:#173b72}.card{border:1px solid #ddd;padding:16px;border-radius:10px;margin:12px 0}.btn{background:#173b72;color:white;border:0;padding:9px 15px;border-radius:6px;cursor:pointer}input,textarea,select{width:100%;box-sizing:border-box;padding:9px;margin:6px 0 12px;border:1px solid #bbb;border-radius:5px}textarea{min-height:110px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.ok{color:green}.bad{color:#b00020}.muted{color:#666}.small{font-size:13px}@media(max-width:700px){.grid{grid-template-columns:1fr}}</style></head><body><header><b>ExVal</b><span>Online Examination & Assessment</span></header><main>"+body+"</main></body></html>";
}
void sendResponse(int fd,int status,const std::string&body,const std::string&ctype) {
    std::string text = status==200?"OK":status==302?"Found":status==400?"Bad Request":status==401?"Unauthorized":status==403?"Forbidden":status==404?"Not Found":"Error";
    std::string msg="HTTP/1.1 "+std::to_string(status)+" "+text+"\r\nContent-Type: "+ctype+"; charset=utf-8\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
    ::send(fd,msg.c_str(),msg.size(),0);
}
void redirect(int fd,const std::string&to) {
    std::string msg="HTTP/1.1 302 Found\r\nLocation: "+to+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    ::send(fd,msg.c_str(),msg.size(),0);
}


