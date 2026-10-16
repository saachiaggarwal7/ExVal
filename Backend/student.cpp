#include "student.h"
#include <iostream>
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
using namespace std;

static void saveCaseResults(Database& db, long long submissionId,const vector<string>& caseIds,const shared_ptr<CodeRunner::Job>& job)
{
    for (size_t i = 0; i < caseIds.size() && i < job->testResults.size(); i++)
    {
        auto result = job->testResults[i];
        string query = "INSERT INTO code_test_case_results "
                       "(submission_id, test_case_id, verdict, execution_time_ms) VALUES ("
                       + to_string(submissionId) + ", "
                       + caseIds[i] + ", '"
                       + db.escape(result.verdict) + "', "
                       + to_string(result.timeMs) + ")";
        db.exec(query);
    }
}

static void upsertAnswer(Database& db, long long aid, const string& qid, bool isMCQ, const string& val, bool overwriteEmpty)
{
    string mcqVal = "NULL";
    if (isMCQ && !val.empty())
        mcqVal = val;
    string query = "SELECT answer_id FROM student_answers WHERE attempt_id="+ to_string(aid) + " AND question_id=" + qid;
    auto old = db.rows(query);
    string answerText = "NULL";
    if (!isMCQ)
        answerText = "'" + db.escape(val) + "'";
    if (old.empty())
    {
        query = "INSERT INTO student_answers "
                "(attempt_id, question_id, selected_option_id, answer_text, marks_awarded) VALUES ("
                + to_string(aid) + ", " + qid + ", " + mcqVal + ", "
                + answerText + ", 0)";

        db.exec(query);
        return;
    }
    if (val.empty() && !overwriteEmpty)
        return;
    query = "UPDATE student_answers SET selected_option_id=" + mcqVal
          + ", answer_text=" + answerText
          + " WHERE answer_id=" + old[0][0];
    db.exec(query);
}