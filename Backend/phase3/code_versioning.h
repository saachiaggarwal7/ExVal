#ifndef CODE_VERSIONING_H
#define CODE_VERSIONING_H

#include "../common.h"
#include "../database.h"

bool handleCodeVersioningRequest(int fd, const Request& request, const Session& session, Database& db);

#endif
