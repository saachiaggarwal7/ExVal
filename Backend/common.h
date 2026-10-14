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
