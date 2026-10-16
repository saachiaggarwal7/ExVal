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
