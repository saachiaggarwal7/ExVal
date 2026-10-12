#ifndef ADMIN_H
#define ADMIN_H
#include "common.h"
#include "database.h"
bool handleAdmin(int fd, Request& r, const Session& s, Database& db);
#endif
