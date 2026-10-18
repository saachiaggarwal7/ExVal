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

static int gradeAttempt(Database& db, long long aid,const vector<vector<string>>& qs)
{
    int obt = 0;
    for (auto& q : qs)
    {
        string query = "SELECT answer_id, selected_option_id, answer_text "
                       "FROM student_answers WHERE attempt_id="
                       + to_string(aid) + " AND question_id=" + q[0];

        auto ans = db.rows(query);
        if (ans.empty())
            continue;

        int marks = 0;
        if (q[1] == "MCQ")
        {
            query = "SELECT option_id FROM mcq_options WHERE question_id="
                    + q[0] + " AND is_correct=1 LIMIT 1";

            auto correct = db.rows(query);

            if (!correct.empty() && ans[0][1] == correct[0][0])
                marks = stoi(q[2]);
        }
        else if (q[1] == "CODING" && !ans[0][2].empty())
        {
            query = "SELECT er.passed_tests, er.total_tests "
                    "FROM code_submissions cs "
                    "JOIN execution_results er "
                    "ON er.submission_id=cs.submission_id "
                    "WHERE cs.answer_id=" + ans[0][0]
                    + " ORDER BY cs.submission_id DESC LIMIT 1";
            auto latest = db.rows(query);
            int passed = 0;
            int total = 0;
            if (!latest.empty())
            {
                passed = stoi(latest[0][0]);
                total = stoi(latest[0][1]);
            }
            else
            {
                query = "SELECT test_case_id, input_data, expected_output "
                        "FROM test_cases WHERE question_id=" + q[0]
                        + " ORDER BY test_case_id";
                auto tc = db.rows(query);
                vector<pair<string, string>> tests;
                vector<string> caseIds;
                for (auto& t : tc)
                {
                    caseIds.push_back(t[0]);
                    tests.push_back({t[1], t[2]});
                }
                auto job = runner.submit(
                    stoi(ans[0][0]), ans[0][2], tests,
                    max(1, stoi(q[3])), max(16, stoi(q[4]))
                );
                unique_lock<mutex> lock(job->m);
                job->cv.wait(lock, [&] { return job->done; });
                lock.unlock();
                query = "INSERT INTO code_submissions "
                        "(answer_id, source_code, status) VALUES ("
                        + ans[0][0] + ", '" + db.escape(ans[0][2])
                        + "', '" + job->verdict + "')";
                db.exec(query);
                long long sid = db.lastId();
                query = "INSERT INTO execution_results "
                        "(submission_id, verdict, passed_tests, total_tests, "
                        "execution_time_ms) VALUES ("
                        + to_string(sid) + ", '" + job->verdict + "', "
                        + to_string(job->passed) + ", "
                        + to_string((int)tests.size()) + ", "
                        + to_string(job->timeMs) + ")";
                db.exec(query);
                saveCaseResults(db, sid, caseIds, job);
                passed = job->passed;
                total = (int)tests.size();
            }
            if (total > 0)
                marks = passed * stoi(q[2]) / total;
        }
        query = "UPDATE student_answers SET marks_awarded="  + to_string(marks) + " WHERE answer_id=" + ans[0][0];
        db.exec(query);
        obt += marks;
    }
    return obt;
}

static bool finalizeAttempt(Database& db, long long aid, int total, int obtained)
{
    string query = "CALL finalize_exam_attempt(" + to_string(aid) + ", "+ to_string(total) + ", "+ to_string(obtained) + ")";
    if (db.exec(query))
        return true;
    query = "UPDATE exam_attempts SET status='SUBMITTED', "
            "submitted_at=NOW(), total_marks=" + to_string(total)
            + ", obtained_marks=" + to_string(obtained)
            + " WHERE attempt_id=" + to_string(aid)
            + " AND status='IN_PROGRESS'";
    return db.exec(query);
}

static const string EXAM_CSS = R"HTML(
<style>
.coding-panel{border:1px solid #c5cfe0;border-radius:10px;margin:18px 0;background:#fff;overflow:hidden}
.coding-panel-header{background:#1a3a6b;color:#fff;padding:12px 18px;display:flex;justify-content:space-between;align-items:center}
.coding-panel-header h3{margin:0;font-size:15px}
.coding-panel-header .badge{background:#e8f0fe;color:#1a3a6b;padding:3px 10px;border-radius:12px;font-size:13px;font-weight:bold}
.coding-panel-body{padding:16px 18px}
.code-area{width:100%;box-sizing:border-box;font-family:'Courier New',monospace;font-size:13px;min-height:180px;border:1px solid #bfc7d2;border-radius:6px;padding:10px;resize:vertical;background:#1e1e1e;color:#d4d4d4;line-height:1.5}
.run-btn{background:#1a6b3a;color:#fff;border:0;padding:9px 20px;border-radius:6px;cursor:pointer;font-size:14px;margin-top:10px}
.run-btn:disabled{background:#888;cursor:not-allowed}
.verdict-box{margin-top:12px;border-radius:8px;overflow:hidden}
.verdict-header{padding:10px 16px;font-weight:bold;font-size:14px}
.verdict-header.accepted{background:#d4edda;color:#155724}
.verdict-header.wrong{background:#f8d7da;color:#721c24}
.verdict-header.compile-err{background:#fff3cd;color:#856404}
.verdict-header.runtime{background:#f8d7da;color:#721c24}
.verdict-header.tle{background:#cce5ff;color:#004085}
.test-row{display:grid;grid-template-columns:28px 1fr 1fr auto;gap:8px;padding:8px 16px;border-top:1px solid #eee;font-size:13px;align-items:start}
.test-row.pass{background:#f6fff8}
.test-row.fail{background:#fff6f6}
.test-row pre{margin:0;font-size:12px;white-space:pre-wrap;word-break:break-all;background:#f4f4f4;padding:4px 6px;border-radius:4px}
.compile-pre{background:#1e1e1e;color:#ff6b6b;padding:12px 16px;font-family:monospace;font-size:12px;white-space:pre-wrap;margin:0}
.spinner{display:inline-block;width:14px;height:14px;border:2px solid #ccc;border-top-color:#1a3a6b;border-radius:50%;animation:spin .7s linear infinite;vertical-align:middle;margin-right:6px}
.exam-layout{display:grid;grid-template-columns:190px minmax(0,1fr);gap:18px;align-items:start}.question-nav{position:sticky;top:12px;background:#fff;border:1px solid #c5cfe0;border-radius:10px;padding:10px}.question-nav b{display:block;margin:4px 6px 10px}.question-nav button{width:100%;text-align:left;margin:3px 0;padding:9px;border:1px solid #d6deea;border-radius:6px;background:#f6f8fc;cursor:pointer}.question-nav button.active{background:#173b72;color:#fff;border-color:#173b72}.exam-question{display:none}.exam-question.active{display:block}.question-actions{display:flex;justify-content:space-between;gap:12px;margin-top:20px}.question-actions button{width:auto}.question-card{border:1px solid #d7dfea;border-radius:10px;padding:20px;background:#fff;min-height:300px}
@keyframes spin{to{transform:rotate(360deg)}}
@media(max-width:700px){.exam-layout{grid-template-columns:1fr}.question-nav{position:static;display:flex;overflow-x:auto;gap:5px}.question-nav b{display:none}.question-nav button{min-width:52px;text-align:center}.question-actions{flex-wrap:wrap}}
</style>
)HTML";

static const string RESULT_CSS = R"HTML(
<style>
.result-hero{background:linear-gradient(135deg,#173b72,#275a9f);color:#fff;border-radius:14px;padding:26px;margin:16px 0}
.result-hero h2{color:#fff;margin:0 0 7px}.result-hero p{margin:6px 0}.result-score{font-size:36px;font-weight:bold;margin:12px 0}
.metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(155px,1fr));gap:12px;margin:16px 0}.metric-card{border:1px solid #d8e2f0;border-radius:10px;padding:14px;background:#fff}.metric-card b{display:block;color:#60708a;font-size:13px}.metric-card span{display:block;color:#173b72;font-size:22px;font-weight:bold;margin-top:5px}
.score-bar{height:10px;border-radius:8px;background:#dbe6f5;overflow:hidden;margin-top:12px}.score-bar div{height:100%;background:#56aa70;border-radius:8px}.answer-card{border-left:5px solid #cdd9eb}.answer-card.good{border-left-color:#43a060}.answer-card.needs-work{border-left-color:#d65a5a}.question-meta{color:#657184;font-size:14px}
</style>
)HTML";

static string examScript(int remaining) {
    return R"JS(
<script>
let left=)JS" + to_string(remaining) + R"JS(;
let currentQuestionStarted=Date.now();
const questionTimes={};
function recordVisibleQuestionTime(){
  const active=document.querySelector('.exam-question.active');
  if(!active) return;
  const q=active.dataset.questionId;
  if(!q) return;
  questionTimes[q]=(questionTimes[q]||0)+Math.max(0,Math.round((Date.now()-currentQuestionStarted)/1000));
  currentQuestionStarted=Date.now();
  fetch('/save_question_time',{method:'POST',keepalive:true,
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({attempt_id:document.getElementById('attempt_id').value,question_id:q,time_spent_seconds:String(questionTimes[q])})});
}
function tick(){
  let m=Math.floor(left/60),s=left%60;
  document.getElementById('timer').textContent=m+':'+(s<10?'0':'')+s;
  if(left<=0){document.getElementById('examForm').submit();return;}
  left--;
}
setInterval(tick,1000); tick();
function showQuestion(index){
  recordVisibleQuestionTime();
  const questions=document.querySelectorAll('.exam-question');
  const buttons=document.querySelectorAll('.question-nav button');
  questions.forEach((el,i)=>el.classList.toggle('active',i===index));
  buttons.forEach((el,i)=>el.classList.toggle('active',i===index));
  window.scrollTo({top:0,behavior:'smooth'});
}
async function saveAnswer(a,q,v){
  fetch('/save_answer',{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({attempt_id:String(a),question_id:String(q),value:v,time_spent_seconds:String(questionTimes[q]||0)})
  });
}

async function runCode(aid,qid){
  const ta  = document.getElementById('code_'+qid);
  const btn = document.getElementById('runbtn_'+qid);
  const box = document.getElementById('result_'+qid);
  if(!ta||!btn||!box) return;
  btn.disabled=true;
  box.innerHTML='<div class="verdict-box"><div class="verdict-header compile-err"><span class="spinner"></span>Compiling &amp; running...</div></div>';
  try{
    const resp = await fetch('/run_code',{
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body: new URLSearchParams({attempt_id:String(aid),question_id:String(qid),source:ta.value})
    });
    if(!resp.ok){ const t=await resp.text(); throw new Error(t); }
    const d = await resp.json();
    renderResult(qid, d);
  } catch(e){
    box.innerHTML='<div class="verdict-box"><div class="verdict-header wrong">Server error — '+escHtml(e.message)+'</div></div>';
  } finally { btn.disabled=false; }
}

function escHtml(s){ const d=document.createElement('div'); d.appendChild(document.createTextNode(s)); return d.innerHTML; }
function renderResult(qid, d){
  const box = document.getElementById('result_'+qid);
  let cls='wrong', label=d.verdict;
  if(d.verdict==='ACCEPTED') cls='accepted';
  else if(d.verdict==='COMPILATION_ERROR') cls='compile-err';
  else if(d.verdict==='TIME_LIMIT_EXCEEDED') cls='tle';
  else if(d.verdict==='RUNTIME_ERROR') cls='runtime';
  let html='<div class="verdict-box">';
  html+='<div class="verdict-header '+cls+'">'+label;
  if(d.verdict!=='COMPILATION_ERROR')
    html+=' &mdash; Passed: '+d.passed+'/'+d.total+' &mdash; Time: '+d.time_ms+' ms';
  html+='</div>';

  if(d.verdict==='COMPILATION_ERROR'||d.verdict==='NO_TEST_CASES'){
    html+='<pre class="compile-pre">'+escHtml(d.error||d.compile_error||'(no output)')+'</pre>';
  } else if(d.verdict==='RUNTIME_ERROR'){
    html+='<pre class="compile-pre">'+escHtml(d.runtime_error||'(no stderr)')+'</pre>';
    // still show test rows if any
  }
  if(d.tests && d.tests.length){
    html+='<div class="test-row" style="background:#f0f4fa;font-weight:bold"><span>#</span><span>Input / Expected</span><span>Your Output</span><span>Result</span></div>';
    d.tests.forEach((t,i)=>{
      const rowCls = t.verdict==='PASSED'?'pass':'fail';
      const icon   = t.verdict==='PASSED'?'✓':'✗';
      html+='<div class="test-row '+rowCls+'">';
      html+='<span>'+(i+1)+'</span>';
      html+='<span><pre>'+escHtml(t.input)+'</pre><small>Expected:</small><pre>'+escHtml(t.expected)+'</pre></span>';
      html+='<span><pre>'+escHtml(t.actual||'—')+'</pre></span>';
      html+='<span style="font-weight:bold;color:'+(t.verdict==='PASSED'?'green':'#b00020')+'">'+icon+'</span>';
      html+='</div>';
    });
  }
  html+='</div>';
  box.innerHTML=html;
}
document.getElementById('examForm').addEventListener('submit',recordVisibleQuestionTime);
window.addEventListener('pagehide',recordVisibleQuestionTime);
</script>
)JS";
}

bool handleStudent(int fd, Request& r, const Session& s, Database& db) {
    if (s.role != "STUDENT") return false;
    db.closeExpiredExams();
    if (r.path == "/analytics")
{
    auto stats = db.rows(
        "SELECT COUNT(*), "
        "COALESCE(ROUND(AVG(100.0 * obtained_marks / "
        "NULLIF(total_marks, 0)), 1), 0), "
        "COALESCE(SUM(obtained_marks), 0), "
        "COALESCE(SUM(total_marks), 0) "
        "FROM exam_attempts "
        "WHERE student_id = " + to_string(s.userId) +
        " AND status = 'SUBMITTED'"
    );

    auto history = db.rows(
        "SELECT ea.attempt_id, e.title, "
        "ea.obtained_marks, ea.total_marks, "
        "DATE_FORMAT(ea.submitted_at, '%Y-%m-%d %H:%i') "
        "FROM exam_attempts ea "
        "JOIN exams e ON e.exam_id = ea.exam_id "
        "WHERE ea.student_id = " + to_string(s.userId) +
        " AND ea.status = 'SUBMITTED' "
        "ORDER BY ea.submitted_at DESC"
    );

    string b = "<h1>My Analytics</h1>";

    b += "<p><a href='/student'>Back to Dashboard</a> | ";
    b += "<a href='/logout'>Logout</a></p>";

    if (!stats.empty())
    {
        b += "<div class='card'>";
        b += "<h3>Performance Summary</h3>";

        b += "<p>Exams completed: " + stats[0][0] + "</p>";
        b += "<p>Average score: " + stats[0][1] + "%</p>";
        b += "<p>Total marks obtained: " + stats[0][2] +
             " / " + stats[0][3] + "</p>";

        b += "</div>";
    }

    b += "<h2>Exam History</h2>";

    if (history.empty())
    {
        b += "<p>You haven't completed any exams yet.</p>";
    }
    else
    {
        b += "<div class='card'><table>";
        b += "<tr><th>Exam</th><th>Score</th>";
        b += "<th>Submitted</th><th>Result</th></tr>";

        for (auto& row : history)
        {
            b += "<tr>";
            b += "<td>" + html(row[1]) + "</td>";
            b += "<td>" + row[2] + " / " + row[3] + "</td>";
            b += "<td>" + html(row[4]) + "</td>";

            b += "<td><a href='/result?attempt_id=" +
                 row[0] + "'>View Result</a></td>";

            b += "</tr>";
        }

        b += "</table></div>";
    }

    sendResponse(fd, 200, page("My Analytics", b));
    return true;
}
    if (r.path == "/student")
    {
    auto exams = db.rows(
        "SELECT exam_id, title, duration_minutes, allow_multiple_attempts "
        "FROM exams WHERE status='PUBLISHED' ORDER BY exam_id DESC"
    );
    string b = "<h1>Student Dashboard</h1>";
    b += "<p>Welcome, " + html(s.name);
    b += " | <a href='/analytics'>My Analytics</a>";
    b += " | <a href='/logout'>Logout</a></p>";
    b += "<h2>Available Exams</h2>";
    for (auto& e : exams)
    {
        b += "<div class='card'><h3>" + html(e[1]) + "</h3>";
        b += "<p>Duration: " + e[2] + " minutes</p>";
        string buttonText = "Start / Resume Exam";
        if (e[3] == "1")
            buttonText = "Start New / Resume Exam";
        b += "<form method='post' action='/start'>";
        b += "<input type='hidden' name='exam_id' value='" + e[0] + "'>";
        b += "<button class='btn'>" + buttonText + "</button>";
        b += "</form></div>";
    }
    if (exams.empty())
        b += "<p>No published exams.</p>";

    auto previous = db.rows(
        "CALL get_student_results(" + to_string(s.userId) + ")"
    );
    if (previous.empty())
    {
        previous = db.rows(
            "SELECT attempt_id, student_name, exam_name, total_marks, "
            "obtained_marks, status, "
            "DATE_FORMAT(submitted_at,'%Y-%m-%d %H:%i') "
            "FROM student_result_view WHERE student_id="
            + to_string(s.userId) + " ORDER BY attempt_id DESC"
        );
    }
    b += "<h2>Previous Attempts</h2>";
    if (previous.empty())
    {
        b += "<p>No submitted exams yet.</p>";
    }
    else
    {
        b += "<div class='card'><table>";
        b += "<tr><th>Exam</th><th>Score</th><th>Submitted</th><th></th></tr>";
        for (auto& row : previous)
        {
            string submitted = "";
            if (row.size() > 6)
                submitted = row[6];
            b += "<tr><td>" + html(row[2]) + "</td>";
            b += "<td>" + row[4] + " / " + row[3] + "</td>";
            b += "<td>" + html(submitted) + "</td>";
            b += "<td><a href='/result?attempt_id=" + row[0] + "'>";
            b += "View result</a></td></tr>";
        }
        b += "</table></div>";
    }
    sendResponse(fd, 200, page("Student", b));
    return true;
    }
    
    if (r.path == "/start" && r.method == "POST")
    {
        long long eid = idOf(r.form["exam_id"]);
        string query = "SELECT allow_multiple_attempts FROM exams "
                      "WHERE exam_id=" + to_string(eid)
                      + " AND status='PUBLISHED'";
        auto exam = db.rows(query);
        if (exam.empty())
        {
            sendResponse(fd, 400, page("Exam unavailable",
                "<h2>This exam is not currently published.</h2>"
                "<p><a href='/student'>Back to dashboard</a></p>"));
            return true;
        }
        query = "SELECT attempt_id FROM exam_attempts "
                "WHERE exam_id=" + to_string(eid)
                + " AND student_id=" + to_string(s.userId)
                + " AND status='IN_PROGRESS' "
                "ORDER BY attempt_id DESC LIMIT 1";
        auto active = db.rows(query);
        query = "SELECT attempt_id FROM exam_attempts "
                "WHERE exam_id=" + to_string(eid)
                + " AND student_id=" + to_string(s.userId)
                + " ORDER BY attempt_id DESC LIMIT 1";
        auto previous = db.rows(query);
        long long aid;
        if (!active.empty())
        {
            aid = idOf(active[0][0]);
        }
        else if (!previous.empty() && exam[0][0] != "1")
        {
            redirect(fd, "/result?attempt_id=" + previous[0][0]);
            return true;
        }
        else
        {
            db.exec("INSERT INTO exam_attempts(exam_id, student_id) VALUES("
                    + to_string(eid) + ", " + to_string(s.userId) + ")");

            aid = db.lastId();
        }
        redirect(fd, "/take?attempt_id=" + to_string(aid));
        return true;
    }

    if (r.path == "/save_answer" && r.method == "POST")
    {
        long long aid = idOf(r.form["attempt_id"]);
        long long qid = idOf(r.form["question_id"]);
        string query = "SELECT attempt_id FROM exam_attempts "
                      "WHERE attempt_id=" + to_string(aid)
                      + " AND student_id=" + to_string(s.userId)
                      + " AND status='IN_PROGRESS'";
        auto own = db.rows(query);
        query = "SELECT question_type FROM questions WHERE question_id="
                + to_string(qid);
        auto q = db.rows(query);
        if (own.empty() || q.empty())
        {
            sendResponse(fd, 400, "invalid", "text/plain");
            return true;
        }
        string val = "";
        if (r.form.count("value"))
            val = r.form.at("value");
        int spent = max(0, (int)idOf(r.form["time_spent_seconds"]));
        bool isMCQ = (q[0][0] == "MCQ");
        string mcqVal = "NULL";
        if (isMCQ && !val.empty())
            mcqVal = val;
        query = "SELECT answer_id FROM student_answers WHERE attempt_id="
                + to_string(aid) + " AND question_id=" + to_string(qid);
        auto old = db.rows(query);
        string answerText = "NULL";
        if (!isMCQ)
            answerText = "'" + db.escape(val) + "'";
        if (old.empty())
        {
            query = "INSERT INTO student_answers "
                    "(attempt_id, question_id, selected_option_id, "
                    "answer_text, time_spent_seconds) VALUES ("
                    + to_string(aid) + ", " + to_string(qid) + ", "
                    + mcqVal + ", " + answerText + ", "
                    + to_string(spent) + ")";
            db.exec(query);
        }
        else
        {
            query = "UPDATE student_answers SET selected_option_id="
                    + mcqVal + ", answer_text=" + answerText
                    + ", time_spent_seconds=GREATEST(time_spent_seconds, "
                    + to_string(spent) + ") WHERE answer_id=" + old[0][0];
            db.exec(query);
        }
        sendResponse(fd, 200, "saved", "text/plain");
        return true;
    }
  
    if (r.path == "/save_question_time" && r.method == "POST")
    {
        long long aid = idOf(r.form["attempt_id"]);
        long long qid = idOf(r.form["question_id"]);
        int spent = max(0, (int)idOf(r.form["time_spent_seconds"]));
        string query = "SELECT ea.attempt_id FROM exam_attempts ea "
                      "JOIN questions q ON q.exam_id = ea.exam_id "
                      "WHERE ea.attempt_id=" + to_string(aid)
                      + " AND ea.student_id=" + to_string(s.userId)
                      + " AND ea.status='IN_PROGRESS' "
                      "AND q.question_id=" + to_string(qid);
        auto own = db.rows(query);
        if (own.empty())
        {
            sendResponse(fd, 400, "invalid", "text/plain");
            return true;
        }
        query = "INSERT INTO student_answers "
                "(attempt_id, question_id, time_spent_seconds) VALUES ("
                + to_string(aid) + ", "
                + to_string(qid) + ", "
                + to_string(spent) + ") "
                "ON DUPLICATE KEY UPDATE "
                "time_spent_seconds = GREATEST(time_spent_seconds, "
                "VALUES(time_spent_seconds))";
        db.exec(query);
        sendResponse(fd, 200, "saved", "text/plain");
        return true;
    }
    
    if (r.path == "/run_code" && r.method == "POST")
    {
        long long aid = idOf(r.form["attempt_id"]);
        long long qid = idOf(r.form["question_id"]);
        string source = "";
        if (r.form.count("source"))
            source = r.form.at("source");
        auto sendJson = [&](int status, const string& body)
        {
            sendResponse(fd, status, body, "application/json");
        };
        // Check attempt and question
        string query = "SELECT attempt_id FROM exam_attempts WHERE attempt_id="
                    + to_string(aid) + " AND student_id="
                    + to_string(s.userId) + " AND status='IN_PROGRESS'";

        auto own = db.rows(query);

        query = "SELECT time_limit_seconds, memory_limit_mb FROM questions "
                "WHERE question_id=" + to_string(qid)
                + " AND question_type='CODING'";
        auto qrow = db.rows(query);
        if (own.empty() || qrow.empty())
        {
            sendJson(400, "{\"verdict\":\"ERROR\",\"error\":\"Invalid attempt or question\","
                          "\"passed\":0,\"total\":0,\"time_ms\":0,\"tests\":[],"
                          "\"compile_error\":\"\",\"runtime_error\":\"\"}");
            return true;
        }
        if (source.empty())
        {
            sendJson(400, "{\"verdict\":\"ERROR\",\"error\":\"No code submitted\","
                          "\"passed\":0,\"total\":0,\"time_ms\":0,\"tests\":[],"
                          "\"compile_error\":\"\",\"runtime_error\":\"\"}");
            return true;
        }
        // Load test cases
        query = "SELECT test_case_id, input_data, expected_output FROM test_cases "
                "WHERE question_id=" + to_string(qid)
                + " ORDER BY test_case_id";
        auto tc = db.rows(query);
        vector<pair<string, string>> tests;
        vector<string> caseIds;
        for (auto& t : tc)
        {
            caseIds.push_back(t[0]);
            tests.push_back({t[1], t[2]});
        }
        query = "SELECT answer_id FROM student_answers WHERE attempt_id="
                + to_string(aid) + " AND question_id=" + to_string(qid);
        auto old = db.rows(query);
        long long answerId;
        if (old.empty())
        {
            query = "INSERT INTO student_answers "
                    "(attempt_id, question_id, answer_text, marks_awarded) VALUES ("
                    + to_string(aid) + ", " + to_string(qid) + ", '"
                    + db.escape(source) + "', 0)";
            db.exec(query);
            answerId = db.lastId();
        }
        else
        {
            answerId = idOf(old[0][0]);

            query = "UPDATE student_answers SET answer_text='"
                    + db.escape(source) + "' WHERE answer_id="
                    + to_string(answerId);
            db.exec(query);
        }
        // Run the submitted code
        int timeLimit = max(1, (int)idOf(qrow[0][0]));
        int memoryLimit = max(16, (int)idOf(qrow[0][1]));
        auto job = runner.submit(answerId, source, tests, timeLimit, memoryLimit);
        unique_lock<mutex> lock(job->m);
        job->cv.wait(lock, [&] { return job->done; });
        lock.unlock();
        // Save execution results
        query = "INSERT INTO code_submissions "
                "(answer_id, source_code, status) VALUES ("
                + to_string(answerId) + ", '" + db.escape(source)
                + "', '" + job->verdict + "')";
        db.exec(query);
        long long sid = db.lastId();
        query = "INSERT INTO execution_results "
                "(submission_id, verdict, passed_tests, total_tests, execution_time_ms) VALUES ("
                + to_string(sid) + ", '" + job->verdict + "', "
                + to_string(job->passed) + ", "
                + to_string((int)tests.size()) + ", "
                + to_string(job->timeMs) + ")";
        db.exec(query);
        saveCaseResults(db, sid, caseIds, job);
        // Build JSON response
        string json = "{";
        json += "\"verdict\":\"" + job->verdict + "\",";
        json += "\"passed\":" + to_string(job->passed) + ",";
        json += "\"total\":" + to_string(tests.size()) + ",";
        json += "\"time_ms\":" + to_string(job->timeMs) + ",";
        json += "\"compile_error\":\"" + CodeRunner::jsonEscape(job->compileError) + "\",";
        json += "\"runtime_error\":\"" + CodeRunner::jsonEscape(job->runtimeError) + "\",";
        string error = job->compileError;
        if (error.empty())
            error = job->runtimeError;
        json += "\"error\":\"" + CodeRunner::jsonEscape(error) + "\",";
        json += "\"tests\":[";
        for (size_t i = 0; i < job->testResults.size(); i++)
        {
            auto result = job->testResults[i];
            if (i > 0)
                json += ",";
            json += "{";
            json += "\"verdict\":\"" + result.verdict + "\",";
            json += "\"input\":\"" + CodeRunner::jsonEscape(tests[i].first) + "\",";
            json += "\"expected\":\"" + CodeRunner::jsonEscape(result.expected) + "\",";
            json += "\"actual\":\"" + CodeRunner::jsonEscape(result.actual) + "\",";
            json += "\"time_ms\":" + to_string(result.timeMs);
            json += "}";
        }
        json += "]}";
        sendJson(200, json);
        return true;
    }
    if (r.path == "/submit_exam" && r.method == "POST")
{
    long long aid = idOf(r.form["attempt_id"]);

    // Check that this attempt belongs to the student
    string query =
        "SELECT exam_id, status FROM exam_attempts "
        "WHERE attempt_id=" + to_string(aid) +
        " AND student_id=" + to_string(s.userId);

    auto attempt = db.rows(query);

    if (attempt.empty())
    {
        sendResponse(fd, 404, "Attempt not found", "text/plain");
        return true;
    }

    // If already submitted, show the result
    if (attempt[0][1] == "SUBMITTED")
    {
        redirect(fd, "/result?attempt_id=" + to_string(aid));
        return true;
    }

    // Get the exam questions
    query =
        "SELECT question_id, question_type, marks, "
        "time_limit_seconds, memory_limit_mb "
        "FROM exam_questions_safe WHERE exam_id=" + attempt[0][0];

    auto questions = db.rows(query);

    if (questions.empty())
    {
        questions = db.rows(
            "SELECT question_id, question_type, marks, "
            "time_limit_seconds, memory_limit_mb "
            "FROM questions WHERE exam_id=" + attempt[0][0]
        );
    }

    // Save any answers included in the final form submission
    for (auto& q : questions)
    {
        string key = "q_" + q[0];

        if (r.form.count(key))
        {
            bool isMCQ = (q[1] == "MCQ");

            upsertAnswer(
                db,
                aid,
                q[0],
                isMCQ,
                r.form.at(key),
                true
            );
        }
    }

    // Calculate the total marks
    int total = 0;

    for (auto& q : questions)
        total += stoi(q[2]);

    // Evaluate the answers
    int obtained = gradeAttempt(db, aid, questions);

    // Save the final result
    db.begin();

    if (finalizeAttempt(db, aid, total, obtained))
        db.commit();
    else
        db.rollback();

    // Open the result page
    redirect(fd, "/result?attempt_id=" + to_string(aid));
    return true;
}
if (r.path == "/take")
{
    long long aid = idOf(r.query["attempt_id"]);

    string query = "SELECT ea.exam_id, e.title, e.duration_minutes, "
                   "ea.status, ea.started_at, "
                   "TIMESTAMPDIFF(SECOND, ea.started_at, NOW()) "
                   "FROM exam_attempts ea JOIN exams e "
                   "ON ea.exam_id = e.exam_id "
                   "WHERE ea.attempt_id=" + to_string(aid)
                   + " AND ea.student_id=" + to_string(s.userId);

    auto a = db.rows(query);

    if (a.empty())
    {
        sendResponse(fd, 404, "Attempt not found");
        return true;
    }

    if (a[0][3] == "SUBMITTED")
    {
        redirect(fd, "/result?attempt_id=" + to_string(aid));
        return true;
    }

    int duration = stoi(a[0][2]) * 60;
    int elapsed = stoi(a[0][5]);
    int remaining = max(0, duration - elapsed);

    // Submit automatically when time is over
    if (remaining == 0)
    {
        query = "SELECT question_id, question_type, marks, "
                "time_limit_seconds, memory_limit_mb "
                "FROM exam_questions_safe WHERE exam_id=" + a[0][0];
        auto qs = db.rows(query);
        if (qs.empty())
        {
            qs = db.rows(
                "SELECT question_id, question_type, marks, "
                "time_limit_seconds, memory_limit_mb FROM questions "
                "WHERE exam_id=" + a[0][0]
            );
        }
        int total = 0;

        for (auto& q : qs)
            total += stoi(q[2]);
        int obtained = gradeAttempt(db, aid, qs);

        db.begin();

        if (finalizeAttempt(db, aid, total, obtained))
            db.commit();
        else
            db.rollback();

        redirect(fd, "/result?attempt_id=" + to_string(aid));
        return true;
    }

    // Load exam questions
    query = "SELECT question_id, question_type, question_text, marks "
            "FROM exam_questions_safe WHERE exam_id=" + a[0][0]
            + " ORDER BY question_id";

    auto qs = db.rows(query);
    if (qs.empty())
    {
        qs = db.rows(
            "SELECT question_id, question_type, question_text, marks "
            "FROM questions WHERE exam_id=" + a[0][0]
            + " ORDER BY question_id"
        );
    }

    // Load saved answers
    auto savedRows = db.rows(
        "SELECT question_id, COALESCE(selected_option_id,''), "
        "COALESCE(answer_text,'') FROM student_answers "
        "WHERE attempt_id=" + to_string(aid)
    );

    map<string, pair<string, string>> saved;

    for (auto& answer : savedRows)
        saved[answer[0]] = {answer[1], answer[2]};

    // Build exam page
    string b = EXAM_CSS;

    b += "<h1>" + html(a[0][1]) + "</h1>";
    b += "<p><b>Time left: <span id='timer'>--:--</span></b>";
    b += " | <a href='/student'>Dashboard</a></p>";

    b += "<form id='examForm' method='post' action='/submit_exam'>";
    b += "<input id='attempt_id' type='hidden' name='attempt_id' value='"
         + to_string(aid) + "'>";

    b += "<div class='exam-layout'><nav class='question-nav'>";
    b += "<b>Questions</b>";

    for (size_t i = 0; i < qs.size(); i++)
    {
        string active = "";

        if (i == 0)
            active = "active";

        b += "<button type='button' class='" + active
             + "' onclick='showQuestion(" + to_string(i) + ")'>Q"
             + to_string(i + 1) + "</button>";
    }

    b += "</nav><div>";
    int qnum = 0;
    for (auto& q : qs)
    {
        qnum++;
        string qid = q[0];
        string qtype = q[1];
        string qtext = q[2];
        string marks = q[3];
        string active = "";
        if (qnum == 1)
            active = "active";
        b += "<section class='exam-question " + active
             + "' data-question-id='" + qid
             + "' id='question_" + to_string(qnum - 1) + "'>";
        // Coding question
        if (qtype == "CODING")
        {
            b += "<div class='coding-panel'>";
            b += "<div class='coding-panel-header'>";
            b += "<h3>Q" + to_string(qnum) + ". " + html(qtext) + "</h3>";
            b += "<span class='badge'>" + marks + " marks | C++</span>";
            b += "</div><div class='coding-panel-body'>";
            b += "<textarea id='code_" + qid + "' name='q_" + qid
                 + "' class='code-area' "
                 "placeholder='// Write your C++ code here\n"
                 "#include &lt;iostream&gt;\nusing namespace std;\n"
                 "int main(){\\n    // ...\n}' "
                 "oninput=\"saveAnswer(" + to_string(aid) + "," + qid
                 + ",this.value)\">" + html(saved[qid].second)
                 + "</textarea>";
            b += "<br><button type='button' id='runbtn_" + qid
                 + "' class='run-btn' onclick=\"runCode("
                 + to_string(aid) + "," + qid
                 + ")\">▶ Compile &amp; Run</button>";

            b += "<div id='result_" + qid + "'></div>";
            b += "</div></div>";
        }
        else
        {
            // MCQ or descriptive question
            b += "<div class='card'>";
            b += "<h3>Q" + to_string(qnum)
                 + ". <span class='badge' "
                 "style='background:#e9f0fb;color:#1f447d'>"
                 + qtype + "</span> (" + marks + " marks)</h3>";
            b += "<p>" + html(qtext) + "</p>";
            if (qtype == "MCQ")
            {
                query = "SELECT option_id, option_text FROM mcq_options "
                        "WHERE question_id=" + qid + " ORDER BY option_id";

                auto opts = db.rows(query);
                for (auto& o : opts)
                {
                    string checked = "";
                    if (saved[qid].first == o[0])
                        checked = "checked ";

                    b += "<label style='display:block;margin:6px 0'>";
                    b += "<input type='radio' name='q_" + qid
                         + "' value='" + o[0] + "' " + checked
                         + "onchange=\"saveAnswer(" + to_string(aid)
                         + "," + qid + ",this.value)\"> "
                         + html(o[1]) + "</label>";
                }
            }
            else
            {
                b += "<textarea name='q_" + qid
                     + "' placeholder='Write your answer...' "
                     "oninput=\"saveAnswer(" + to_string(aid) + ","
                     + qid + ",this.value)\">"
                     + html(saved[qid].second) + "</textarea>";
            }
            b += "</div>";
        }
        // Previous and Next buttons
        b += "<div class='question-actions'>";
        if (qnum > 1)
        {
            b += "<button type='button' class='btn-secondary' "
                 "onclick='showQuestion(" + to_string(qnum - 2)
                 + ")'>← Previous</button>";
        }
        else
        {
            b += "<span></span>";
        }
        if (qnum < (int)qs.size())
        {
            b += "<button type='button' class='btn' "
                 "onclick='showQuestion(" + to_string(qnum)
                 + ")'>Next →</button>";
        }
        else
        {
            b += "<button class='btn' style='padding:12px 24px'>"
                 "Submit Exam</button>";
        }
        b += "</div></section>";
    }
    b += "</div></div></form>";
    b += examScript(remaining);
    sendResponse(fd, 200, page("Take Exam", b));
    return true;
}

if (r.path == "/result") {
    long long aid = idOf(r.query["attempt_id"]);

    // Get the exam result
    std::string query =
        "SELECT exam_name, total_marks, obtained_marks, status, "
        "COALESCE((SELECT SUM(time_spent_seconds) "
        "FROM student_answers WHERE attempt_id=v.attempt_id),0) "
        "FROM student_result_view v "
        "WHERE v.attempt_id=" + to_string(aid) +
        " AND v.student_id=" + to_string(s.userId);

    auto a = db.rows(query);

    // If result is not found, check whether the exam is still running
    if (a.empty()) {
        query = "SELECT status FROM exam_attempts WHERE attempt_id=" +
                to_string(aid) +
                " AND student_id=" + to_string(s.userId);

        auto live = db.rows(query);

        if (!live.empty() && live[0][0] == "IN_PROGRESS") {
            redirect(fd, "/take?attempt_id=" + to_string(aid));
            return true;
        }

        sendResponse(fd, 404,
            page("Result not found",
                 "<h2>Result not found</h2>"
                 "<p><a href='/student'>Dashboard</a></p>"));
        return true;
    }

    // Get answers and the latest coding result for each answer
    query =
        "SELECT q.question_text, q.question_type, sa.marks_awarded, "
        "q.marks, sa.time_spent_seconds, "
        "COALESCE(latest.verdict,''), "
        "COALESCE(latest.passed_tests,0), "
        "COALESCE(latest.total_tests,0), "
        "COALESCE(sa.answer_text,''), "
        "COALESCE(mo.option_text,''), COALESCE(sa.feedback,'') "
        "FROM student_answers sa "
        "JOIN questions q ON sa.question_id=q.question_id "
        "LEFT JOIN mcq_options mo ON mo.option_id=sa.selected_option_id "
        "LEFT JOIN ("
        "SELECT cs.answer_id, er.verdict, er.passed_tests, er.total_tests "
        "FROM code_submissions cs "
        "JOIN execution_results er ON er.submission_id=cs.submission_id "
        "WHERE cs.submission_id=("
        "SELECT MAX(cs2.submission_id) FROM code_submissions cs2 "
        "WHERE cs2.answer_id=cs.answer_id)) latest "
        "ON latest.answer_id=sa.answer_id "
        "WHERE sa.attempt_id=" + to_string(aid);

    auto ans = db.rows(query);

    // Calculate percentage
    int totalMarks = stoi(a[0][1]);
    int obtainedMarks = stoi(a[0][2]);
    int percentage = 0;

    if (totalMarks != 0) {
        percentage = obtainedMarks * 100 / totalMarks;
    }

    // Start building the result page
    std::string b = RESULT_CSS;
    b += "<h1>Exam Result</h1>";
    b += "<p><a href='/student'>Dashboard</a> | "
         "<a href='/analytics'>My Analytics</a> | "
         "<a href='/logout'>Logout</a></p>";

    b += "<div class='result-hero'>";
    b += "<h2>" + html(a[0][0]) + "</h2>";
    b += "<p>Final score</p>";

    b += "<div class='result-score'>" +
         a[0][2] + " / " + a[0][1] + " · " +
         std::to_string(percentage) + "%</div>";

    int scoreWidth = max(0, min(100, percentage));

    b += "<div class='score-bar'><div style='width:" +
        to_string(scoreWidth) + "%'></div></div>";
    b += "</div>";

    // Count attempted questions and full-score answers
    int answered = 0;
    int fullMarks = 0;

    for (const auto& x : ans) {
        bool attempted = x[2] != "0" ||
                         x[4] != "0" ||
                         !x[8].empty() ||
                         !x[9].empty();

        if (attempted) {
            answered++;
        }
        if (x[2] == x[3] && x[3] != "0") {
            fullMarks++;
        }
    }
    // Display summary cards
    string activeTime = a[0][4];
    if (activeTime == "0") {
        activeTime = "Not tracked";
    } else {
        activeTime += " sec";
    }
    b += "<div class='metric-grid'>";
    b += "<div class='metric-card'><b>Questions attempted</b><span>" +
         to_string(answered) + " / " +
         to_string(ans.size()) + "</span></div>";

    b += "<div class='metric-card'><b>Full-score answers</b><span>" +
         to_string(fullMarks) + "</span></div>";

    b += "<div class='metric-card'><b>Active question time</b><span>" +
         activeTime + "</span></div>";

    b += "</div>";
    b += "<h2>Answer Summary</h2>";

    // Display each question's result
    for (const auto& x : ans) {
        bool good = x[2] == x[3] && x[3] != "0";

        std::string verdictBadge = "";

        if (x[1] == "CODING" && !x[5].empty()) {
            verdictBadge =
                " &nbsp;<span style='background:#e9f0fb;padding:2px 8px;"
                "border-radius:10px;font-size:13px'>" +
                html(x[5]) + "</span>";
        }

        b += "<div class='card answer-card ";
        b += good ? "good" : "needs-work";
        b += "'>";

        b += "<b>" + html(x[1]) + "</b>" + verdictBadge;
        b += "<p>" + html(x[0]) + "</p>";

        // Show the student's answer
        if (x[1] == "MCQ") {
          string answer = x[9].empty()
                                 ? "<i>Not answered</i>"
                                 : html(x[9]);

            b += "<p><b>Your answer:</b> " + answer + "</p>";
        }
        else if (x[1] == "DESCRIPTIVE") {
            string answer = x[8].empty()
                                 ? "<i>Not answered</i>"
                                 : html(x[8]);

            b += "<p><b>Your answer:</b></p><p>" + answer + "</p>";
        }
        else if (!x[8].empty()) {
            b += "<pre style='background:#1e1e1e;color:#d4d4d4;"
                 "padding:12px;border-radius:6px;overflow-x:auto'>" +
                 html(x[8]) + "</pre>";
        }

        // Show feedback, marks and time
        if (!x[10].empty()) {
            b += "<p class='question-meta'>Feedback: " +
                 html(x[10]) + "</p>";
        }

        string questionTime = x[4];

        if (questionTime == "0") {
            questionTime = "Not tracked";
        } else {
            questionTime += " sec";
        }
        b += "<p class='question-meta'>Marks: <b>" +
             x[2] + "</b> / " + x[3] +
             " &nbsp;|&nbsp; Active time: " + questionTime;
        if (x[1] == "CODING") {
            b += " &nbsp;|&nbsp; Tests: " + x[6] +
                 " / " + x[7] + " passed";
        }
        b += "</p>";
        b += "</div>";
    }
    sendResponse(fd, 200, page("Result", b));
    return true;
}
return false;
}