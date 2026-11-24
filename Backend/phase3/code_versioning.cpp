#include "code_versioning.h"

#include "../code_runner.h"

#include <string>

static void syncCodingAnswer(Database& db, long long attemptId, long long questionId,
                             const std::string& source) {
    auto answer = db.rows("SELECT answer_id FROM student_answers WHERE attempt_id="
                          +std::to_string(attemptId)+" AND question_id="+std::to_string(questionId));
    if (answer.empty()) {
        db.exec("INSERT INTO student_answers(attempt_id,question_id,answer_text,marks_awarded) VALUES("
                +std::to_string(attemptId)+","+std::to_string(questionId)+",'"
                +db.escape(source)+"',0)");
    } else {
        db.exec("UPDATE student_answers SET answer_text='"+db.escape(source)
                +"' WHERE answer_id="+answer[0][0]);
    }
}
bool handleCodeVersioningRequest(int fd, const Request& request, const Session& session, Database& db) {
    auto sendJson = [&](int status, const std::string& body) {
        sendResponse(fd, status, body, "application/json");
    };

    if (request.path == "/save_code_version" && request.method == "POST") {
        long long attemptId = idOf(request.form.count("attempt_id") ? request.form.at("attempt_id") : "");
        long long questionId = idOf(request.form.count("question_id") ? request.form.at("question_id") : "");
        std::string source = request.form.count("source") ? request.form.at("source") : "";
        std::string label = request.form.count("label") ? request.form.at("label") : "";
        bool setSelected = request.form.count("set_selected") && request.form.at("set_selected") == "1";
        bool autosave = request.form.count("autosave") && request.form.at("autosave") == "1";

        auto own = db.rows("SELECT attempt_id FROM exam_attempts WHERE attempt_id="
                           +std::to_string(attemptId)+" AND student_id="+std::to_string(session.userId)
                           +" AND status='IN_PROGRESS'");
        if (own.empty()) {
            sendJson(400, "{\"status\":\"error\",\"error\":\"Invalid attempt\"}");
            return true;
        }

        if (autosave) {
            auto existing = db.rows("SELECT version_id FROM code_versions WHERE attempt_id="
                                    +std::to_string(attemptId)+" AND question_id="+std::to_string(questionId)
                                    +" AND is_autosave=1 LIMIT 1");
            syncCodingAnswer(db, attemptId, questionId, source);
            if (!existing.empty()) {
                db.exec("UPDATE code_versions SET source_code='"+db.escape(source)+"', version_label='Autosave' WHERE version_id="+existing[0][0]);
                sendJson(200, "{\"status\":\"ok\",\"version_id\":"+existing[0][0]+",\"version_number\":0,\"version_label\":\"Autosave\",\"is_selected\":false,\"autosave\":true}");
                return true;
            }
            label = "Autosave";
            setSelected = false;
        }

        auto count = db.rows("SELECT COUNT(*) FROM code_versions WHERE attempt_id="
                             +std::to_string(attemptId)+" AND question_id="+std::to_string(questionId));
        int versionNumber = (count.empty() ? 0 : std::stoi(count[0][0])) + 1;
        if (label.empty()) label = "Version " + std::to_string(versionNumber);
        if (versionNumber == 1 && !autosave) setSelected = true;

        if (setSelected) {
            db.exec("UPDATE code_versions SET is_selected=0 WHERE attempt_id="
                    +std::to_string(attemptId)+" AND question_id="+std::to_string(questionId));
        }

        db.exec("INSERT INTO code_versions(attempt_id, question_id, student_id, version_number, version_label, source_code, is_selected, is_autosave) VALUES("
                +std::to_string(attemptId)+","+std::to_string(questionId)+","+std::to_string(session.userId)+","
                +std::to_string(versionNumber)+",'"+db.escape(label)+"','"+db.escape(source)+"',"
                +(setSelected?"1":"0")+","+(autosave?"1":"0")+")");
        long long versionId = db.lastId();

        syncCodingAnswer(db, attemptId, questionId, source);
        if (setSelected) {
            db.exec("UPDATE student_answers SET selected_version_id="+std::to_string(versionId)
                    +" WHERE attempt_id="+std::to_string(attemptId)+" AND question_id="+std::to_string(questionId));
        }

        std::string json = "{\"status\":\"ok\",\"version_id\":"+std::to_string(versionId)
                         +",\"version_number\":"+std::to_string(versionNumber)
                         +",\"version_label\":\""+CodeRunner::jsonEscape(label)
                         +"\",\"is_selected\":"+(setSelected ? "true" : "false")+"}";
        sendJson(200, json);
        return true;
    }

    if (request.path == "/get_code_versions") {
        long long attemptId = idOf(request.query.count("attempt_id") ? request.query.at("attempt_id") : "");
        long long questionId = idOf(request.query.count("question_id") ? request.query.at("question_id") : "");
        auto own = db.rows("SELECT attempt_id FROM exam_attempts WHERE attempt_id="
                           +std::to_string(attemptId)+" AND student_id="+std::to_string(session.userId));
        if (own.empty()) {
            sendJson(403, "[]");
            return true;
        }

        auto versions = db.rows("SELECT version_id, version_number, version_label, source_code, is_selected, "
                                "DATE_FORMAT(created_at, '%H:%i:%s'), COALESCE(is_autosave,0) FROM code_versions WHERE attempt_id="
                                +std::to_string(attemptId)+" AND question_id="+std::to_string(questionId)
                                +" ORDER BY is_autosave ASC, version_number ASC");
        std::string json = "[";
        for (size_t i = 0; i < versions.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"version_id\":"+versions[i][0]+",\"version_number\":"+versions[i][1]
                    +",\"version_label\":\""+CodeRunner::jsonEscape(versions[i][2])
                    +"\",\"source_code\":\""+CodeRunner::jsonEscape(versions[i][3])
                    +"\",\"is_selected\":"+(versions[i][4] == "1" ? "true" : "false")
                    +",\"created_at\":\""+CodeRunner::jsonEscape(versions[i][5])
                    +"\",\"is_autosave\":"+(versions[i][6] == "1" ? "true" : "false")+"}";
        }
        sendJson(200, json+"]");
        return true;
    }

    if (request.path == "/select_code_version" && request.method == "POST") {
        long long attemptId = idOf(request.form.count("attempt_id") ? request.form.at("attempt_id") : "");
        long long questionId = idOf(request.form.count("question_id") ? request.form.at("question_id") : "");
        long long versionId = idOf(request.form.count("version_id") ? request.form.at("version_id") : "");
        auto target = db.rows("SELECT source_code FROM code_versions WHERE version_id="+std::to_string(versionId)
                              +" AND attempt_id="+std::to_string(attemptId)+" AND question_id="+std::to_string(questionId)
                              +" AND student_id="+std::to_string(session.userId));
        if (target.empty()) {
            sendJson(404, "{\"status\":\"error\",\"error\":\"Version not found\"}");
            return true;
        }
        db.exec("UPDATE code_versions SET is_selected=0 WHERE attempt_id="+std::to_string(attemptId)
                +" AND question_id="+std::to_string(questionId));
        db.exec("UPDATE code_versions SET is_selected=1 WHERE version_id="+std::to_string(versionId));
        syncCodingAnswer(db, attemptId, questionId, target[0][0]);
        db.exec("UPDATE student_answers SET selected_version_id="+std::to_string(versionId)
                +" WHERE attempt_id="+std::to_string(attemptId)+" AND question_id="+std::to_string(questionId));
        sendJson(200, "{\"status\":\"ok\",\"selected_version_id\":"+std::to_string(versionId)+"}");
        return true;
    }

    if (request.path == "/delete_code_version" && request.method == "POST") {
        long long attemptId = idOf(request.form.count("attempt_id") ? request.form.at("attempt_id") : "");
        long long questionId = idOf(request.form.count("question_id") ? request.form.at("question_id") : "");
        long long versionId = idOf(request.form.count("version_id") ? request.form.at("version_id") : "");
        auto target = db.rows("SELECT is_selected FROM code_versions WHERE version_id="+std::to_string(versionId)
                              +" AND attempt_id="+std::to_string(attemptId)+" AND student_id="+std::to_string(session.userId));
        if (target.empty()) {
            sendJson(404, "{\"status\":\"error\",\"error\":\"Version not found\"}");
            return true;
        }
        bool wasSelected = target[0][0] == "1";
        db.exec("DELETE FROM code_versions WHERE version_id="+std::to_string(versionId));
        if (wasSelected) {
            auto remaining = db.rows("SELECT version_id, source_code FROM code_versions WHERE attempt_id="
                                    +std::to_string(attemptId)+" AND question_id="+std::to_string(questionId)
                                    +" ORDER BY version_number DESC LIMIT 1");
            if (!remaining.empty()) {
                db.exec("UPDATE code_versions SET is_selected=1 WHERE version_id="+remaining[0][0]);
                syncCodingAnswer(db, attemptId, questionId, remaining[0][1]);
                db.exec("UPDATE student_answers SET selected_version_id="+remaining[0][0]
                        +" WHERE attempt_id="+std::to_string(attemptId)+" AND question_id="+std::to_string(questionId));
            }
        }
        sendJson(200, "{\"status\":\"ok\"}");
        return true;
    }

    return false;
}
