#ifndef EXAMINER_H
#define EXAMINER_H
#include "common.h"
#include "database.h"
bool handleExaminer(int fd, Request& r, const Session& s, Database& db);
#endif
