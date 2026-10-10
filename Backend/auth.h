#ifndef AUTH_H
#define AUTH_H
#include "common.h"
#include "database.h"

std::string token();
bool authenticate(const Request& request, Session& out);
bool handleAuth(int fd, const Request& request, Database& db);

#endif
