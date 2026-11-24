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
