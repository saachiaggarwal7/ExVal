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
    









