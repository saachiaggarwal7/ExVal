#include "anti_cheat.h"

#include <string>

bool handleAntiCheatRequest(int fd, const Request& request, const Session& session, Database& db) {
    if (request.path != "/log_anticheat" || request.method != "POST") return false;

    long long attemptId = idOf(request.form.count("attempt_id") ? request.form.at("attempt_id") : "");
    std::string eventType = request.form.count("event_type") ? request.form.at("event_type") : "UNKNOWN";
    std::string details = request.form.count("details") ? request.form.at("details") : "";

    auto sendJson = [&](int status, const std::string& body) {
        sendResponse(fd, status, body, "application/json");
    };

    auto attempt = db.rows("SELECT attempt_id, anticheat_warnings FROM exam_attempts WHERE attempt_id="
                           +std::to_string(attemptId)+" AND student_id="+std::to_string(session.userId)
                           +" AND status='IN_PROGRESS'");
    if (attempt.empty()) {
        sendJson(400, "{\"status\":\"error\",\"error\":\"Invalid or finished attempt\"}");
        return true;
    }

    int warningCount = std::stoi(attempt[0][1]);
    if (eventType == "TAB_SWITCH" || eventType == "WINDOW_BLUR" || eventType == "FULLSCREEN_EXIT") {
        warningCount++;
        db.exec("UPDATE exam_attempts SET anticheat_warnings="+std::to_string(warningCount)
                +" WHERE attempt_id="+std::to_string(attemptId));
    }

    db.exec("INSERT INTO anticheat_logs(attempt_id, student_id, event_type, event_detail, warning_count) VALUES("
            +std::to_string(attemptId)+","+std::to_string(session.userId)+",'"+db.escape(eventType)+"','"
            +db.escape(details)+"',"+std::to_string(warningCount)+")");
    db.exec("INSERT INTO activity_logs(user_id, action, details) VALUES("
            +std::to_string(session.userId)+",'ANTICHEAT_"+db.escape(eventType)+"','Attempt "
            +std::to_string(attemptId)+" - Warnings: "+std::to_string(warningCount)+"')");

    sendJson(200, "{\"status\":\"ok\",\"warning_count\":"+std::to_string(warningCount)
             +",\"auto_submit\":"+(warningCount >= 3 ? "true" : "false")+"}");
    return true;
}

void recordAntiCheatAutoSubmission(Database& db, long long attemptId, int studentId,
                                   int warningCount, const std::string& reason) {
    db.exec("INSERT INTO anticheat_logs(attempt_id, student_id, event_type, event_detail, warning_count) VALUES("
            +std::to_string(attemptId)+","+std::to_string(studentId)+",'AUTO_SUBMITTED','"
            +db.escape(reason)+"',"+std::to_string(warningCount)+")");
    db.exec("INSERT INTO activity_logs(user_id, action, details) VALUES("
            +std::to_string(studentId)+",'EXAM_AUTO_SUBMITTED_ANTICHEAT','Attempt "
            +std::to_string(attemptId)+"')");
}
