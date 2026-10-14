#ifndef COMMON_H
#define COMMON_H
#include <map>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <vector>

struct Request {
    std::string method, path, body;
    std::map<std::string,std::string> query, form, headers;
};

struct Session { int userId; std::string role; std::string name; };

extern std::map<std::string, Session> sessions;
extern std::mutex sessionMutex;

std::string urlDecode(const std::string &s);
std::map<std::string,std::string> parsePairs(const std::string &s);
std::string html(const std::string &s);
std::string page(const std::string &title, const std::string &body);
void sendResponse(int fd, int status, const std::string &body, const std::string &ctype="text/html");
void redirect(int fd, const std::string &to);
Request readRequest(int fd);
long long idOf(const std::string &s);

#endif
