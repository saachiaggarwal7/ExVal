#ifndef ANTI_CHEAT_H
#define ANTI_CHEAT_H

#include "../common.h"
#include "../database.h"

// Handles the AJAX endpoint used by the exam page to record anti-cheat events.
// Returns true when the request was handled.
bool handleAntiCheatRequest(int fd, const Request& request, const Session& session, Database& db);

// Records the final anti-cheat auto-submission in the audit tables.
void recordAntiCheatAutoSubmission(Database& db, long long attemptId, int studentId,
                                   int warningCount, const std::string& reason);

#endif
