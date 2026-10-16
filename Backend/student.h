#ifndef STUDENT_H
#define STUDENT_H
#include "common.h"
#include "database.h"
#include "code_runner.h"
bool handleStudent(int fd, Request& r, const Session& s, Database& db);
#endif
