#include "examiner.h"
#include <algorithm>
#include <string>
 
 namespace {
 
 // Check whether this examiner created the exam.
 bool ownsExam(Database& db, long long examId, long long teacherId) {
     auto result = db.rows("SELECT exam_id FROM exams WHERE exam_id=" + std::to_string(examId)
                           + " AND created_by=" + std::to_string(teacherId));
 
     if (result.empty()) {
         return false;
     }
     return true;
 }
 
 // Check whether the question belongs to this examiner.
 bool ownsQuestion(Database& db, long long questionId, long long teacherId) {
     auto result = db.rows("SELECT q.question_id FROM questions q JOIN exams e ON q.exam_id=e.exam_id "
                           "WHERE q.question_id=" + std::to_string(questionId) + " AND e.created_by="
                           + std::to_string(teacherId));
 
     if (result.empty()) {
         return false;
     }
     return true;
 }
 
 void closeExpiredExams(Database& db) {
     db.exec("UPDATE exams SET status='CLOSED' WHERE status='PUBLISHED' "
             "AND published_at IS NOT NULL "
             "AND DATE_ADD(published_at, INTERVAL duration_minutes MINUTE) <= NOW()");
 }
 
 std::string fieldLabel(const std::string& label, const std::string& input) {
     return "<div class='field'><label><b>" + label + "</b></label>" + input + "</div>";
 }
 
 std::string examinerStyles() {
     return R"HTML(
 <style>
 .exam-wrap{max-width:1200px;margin:0 auto}
 .exam-grid{display:grid;grid-template-columns:minmax(320px,0.9fr) minmax(420px,1.4fr);gap:20px;align-items:start}
 .exam-card{background:#fff;border:1px solid #d8dee8;border-radius:12px;padding:20px;box-shadow:0 2px 8px rgba(0,0,0,.04);margin-bottom:18px}
 .exam-card h2{margin-top:0;color:#1f447d}
 .field{margin:14px 0}
 .field label{display:block;margin-bottom:7px;color:#26364d}
 .field input,.field textarea,.field select{width:100%;box-sizing:border-box;padding:11px;border:1px solid #bfc7d2;border-radius:7px;font-size:15px;background:#fff}
 .field textarea{min-height:110px;resize:vertical}
 .help-box{background:#f5f8fc;border-left:4px solid #1f4b87;padding:10px 12px;border-radius:5px;margin:12px 0}
 .option-row{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;margin:9px 0}
 .option-row input[type=text]{width:100%;box-sizing:border-box;padding:10px;border:1px solid #bfc7d2;border-radius:7px}
 .correct-box{display:flex;align-items:center;gap:6px;white-space:nowrap;font-size:14px}
 .correct-box input{width:auto}
 .btn-secondary{background:#eef3f9;border:1px solid #b9c6d8;color:#1f447d;padding:9px 13px;border-radius:7px;cursor:pointer}
 .btn-danger{background:#fff1f1;border:1px solid #e3aaaa;color:#9b2424;padding:9px 13px;border-radius:7px;cursor:pointer}
 .btn-edit{background:#fff8e1;border:1px solid #ffe082;color:#6d4c00;padding:7px 12px;border-radius:7px;cursor:pointer;font-size:13px}
 .meta{display:flex;gap:12px;flex-wrap:wrap}
 .meta .field{flex:1;min-width:140px}
 .question-card{border:1px solid #d8dee8;border-radius:10px;padding:18px;margin:14px 0;background:#fff}
 .question-head{display:flex;justify-content:space-between;gap:10px;align-items:center;margin-bottom:12px;flex-wrap:wrap}
 .badge{background:#e9f0fb;color:#1f447d;padding:5px 9px;border-radius:20px;font-size:13px;font-weight:bold}
 .option-display{padding:6px 0}
 .correct-answer{color:#16823b;font-weight:bold}
 .testcase{background:#f7f8fa;border-radius:7px;padding:9px;margin:7px 0;font-family:monospace;white-space:pre-wrap;font-size:13px}
 .small{font-size:13px;color:#657184}
 .back-link{display:inline-block;margin:8px 0 18px}
 .result-table{width:100%;border-collapse:collapse;font-size:14px}
 .result-table th{background:#1f447d;color:#fff;padding:10px 12px;text-align:left}
 .result-table td{padding:9px 12px;border-bottom:1px solid #e8edf4}
 .result-table tr:hover td{background:#f5f8ff}
 .pass{color:green;font-weight:bold}.fail{color:#b00020;font-weight:bold}
 @media(max-width:850px){.exam-grid{grid-template-columns:1fr}.option-row{grid-template-columns:1fr}}
 </style>
 )HTML";
 }
 
 std::string questionForm(long long eid) {
     std::string b;
     b += "<form method='post' action='/add_question'>";
     b += "<input type='hidden' name='exam_id' value='" + std::to_string(eid) + "'>";
     b += fieldLabel("Question Type",
         "<select name='type' id='qType' onchange='updateHelp()'>"
         "<option value='MCQ'>MCQ</option>"
         "<option value='DESCRIPTIVE'>Descriptive</option>"
         "<option value='CODING'>Coding (C++)</option>"
         "</select>");
     b += fieldLabel("Question Text",
         "<textarea name='text' placeholder='Enter the question...' required></textarea>");
     b += "<div class='meta'>";
     b += fieldLabel("Marks", "<input name='marks' type='number' min='1' value='5' required>");
     b += fieldLabel("Time Limit (s)", "<input name='time_limit' id='tl' type='number' min='1' value='2'>");
     b += fieldLabel("Memory (MB)",    "<input name='memory_limit' id='ml' type='number' min='1' value='64'>");
     b += "</div>";
     b += "<div class='help-box' id='qHelp'><p class='small'>Add options after saving the question.</p></div>";
     b += "<button class='btn' type='submit'>Add Question</button>";
     b += "</form>";
     b += R"HTML(<script>
 function updateHelp(){
   const t=document.getElementById('qType').value;
   const h=document.getElementById('qHelp');
   const tl=document.getElementById('tl'); const ml=document.getElementById('ml');
   if(t==='MCQ'){h.innerHTML='<p class="small">Add answer options after creating the question.</p>';tl.disabled=true;ml.disabled=true;}
   else if(t==='DESCRIPTIVE'){h.innerHTML='<p class="small">Students type an answer; evaluate manually from Review Descriptive.</p>';tl.disabled=true;ml.disabled=true;}
   else{h.innerHTML='<p class="small">Students submit C++ code. Set time and memory limits.</p>';tl.disabled=false;ml.disabled=false;}
 }
 updateHelp();
 </script>)HTML";
     return b;
 }
 
 std::string optionForm(const std::string& qid) {
     std::string b;
     b += "<form method='post' action='/add_option'>";
     b += "<input type='hidden' name='question_id' value='" + qid + "'>";
     b += "<div class='option-row'>";
     b += "<input name='text' placeholder='Enter option text...' required>";
     b += "<label class='correct-box'><input type='checkbox' name='correct' value='1'> Correct</label>";
     b += "</div>";
     b += "<button class='btn-secondary' type='submit'>+ Add Option</button>";
     b += "</form>";
     return b;
 }
 
 std::string testCaseForm(const std::string& qid) {
     std::string b;
     b += "<form method='post' action='/add_test'>";
     b += "<input type='hidden' name='question_id' value='" + qid + "'>";
     b += fieldLabel("Input",           "<input name='input' placeholder='e.g. 5 10' required>");
     b += fieldLabel("Expected Output", "<input name='expected' placeholder='e.g. 15' required>");
     b += "<button class='btn-secondary' type='submit'>+ Add Test Case</button>";
     b += "</form>";
     return b;
 }
 } // namespace
 
 // Handles pages and actions used by an examiner.
 bool handleExaminer(int fd, Request& r, const Session& s, Database& db) {
     // Students and other users should not use examiner pages.
     if (s.role != "EXAMINER") {
         return false;
     }
     closeExpiredExams(db);
     // 1. Show the examiner dashboard and list their exams.
     if (r.path == "/examiner") {
         auto exams = db.rows("SELECT exam_id,title,duration_minutes,status,allow_multiple_attempts,published_at "
                             "FROM exams WHERE created_by="+std::to_string(s.userId)+" ORDER BY exam_id DESC");
         std::string b = examinerStyles();
         b += "<div class='exam-wrap'><h1>Examiner Dashboard</h1><p>Welcome, "+html(s.name)+" | <a href='/logout'>Logout</a></p>";
         b += "<div class='exam-card'><h2>Create Exam</h2>"
              "<form method='post' action='/create_exam'>"
              "<div class='field'><label><b>Exam Title</b></label><input name='title' placeholder='e.g. Operating Systems Test' required></div>"
              "<div class='field'><label><b>Duration (minutes)</b></label><input name='duration' type='number' min='1' value='60' required></div>"
              "<button class='btn'>Create Exam</button></form></div>";
         b += "<h2>Your Exams</h2>";
         for (auto& e : exams) {
             std::string publishedText = "";
             if (!e[5].empty()) {
                 publishedText = " &nbsp;|&nbsp; Published: " + html(e[5]);
             }
 
             b += "<div class='exam-card'><h3>" + html(e[1]) + "</h3>"
                  "<p>Status: <b>" + html(e[3]) + "</b> &nbsp;|&nbsp; Duration: " + e[2] + " min"
                  + publishedText + "</p>"
                  "<a href='/manage_exam?id="+e[0]+"'>Manage Questions</a> &nbsp;|&nbsp; "
                  "<a href='/exam_results?exam_id="+e[0]+"'>Results</a> &nbsp;|&nbsp; "
                  "<a href='/review?exam_id="+e[0]+"'>Review Descriptive</a>";
             b += "<form method='post' action='/toggle_multiple' style='margin-top:12px;display:inline-block'>"
                  "<input type='hidden' name='exam_id' value='"+e[0]+"'>"
                  "<input type='hidden' name='enabled' value='"+(e[4]=="1"?std::string("0"):std::string("1"))+"'>"
                  "<button class='btn-secondary'>Multiple attempts: "+(e[4]=="1"?std::string("ON"):std::string("OFF"))+"</button></form>";
             if (e[3] == "DRAFT" || e[3] == "CLOSED")
                 b += "<form method='post' action='/publish' style='margin-top:12px'>"
                      "<input type='hidden' name='exam_id' value='"+e[0]+"'>"
                      "<button class='btn'>Publish / Restart Timer</button></form>";
             else if (e[3] == "PUBLISHED")
                 b += "<form method='post' action='/unpublish' style='margin-top:12px'>"
                      "<input type='hidden' name='exam_id' value='"+e[0]+"'>"
                      "<button class='btn-danger'>Unpublish Exam</button></form>";
             b += "</div>";
         }
         b += "</div>";
         sendResponse(fd, 200, page("Examiner", b));
         return true;
     }
     // 2. Save a newly created exam.
     if (r.path == "/create_exam" && r.method == "POST") {
         db.exec("INSERT INTO exams(created_by,title,duration_minutes,status) VALUES("
                 +std::to_string(s.userId)+",'"+db.escape(r.form["title"])+"',"
                 +std::to_string((int)idOf(r.form["duration"]))+",'DRAFT')");
         redirect(fd, "/examiner");
         return true;
     }
     // 3. Publish an exam and start its timer.
     if (r.path == "/publish" && r.method == "POST") {
         db.exec("UPDATE exams SET status='PUBLISHED',published_at=NOW() WHERE exam_id="+r.form["exam_id"]
                 +" AND created_by="+std::to_string(s.userId));
         redirect(fd, "/examiner");
         return true;
     }
     if (r.path == "/unpublish" && r.method == "POST") {
         db.exec("UPDATE exams SET status='DRAFT',published_at=NULL WHERE exam_id="+r.form["exam_id"]
                 +" AND created_by="+std::to_string(s.userId));
         redirect(fd, "/examiner");
         return true;
     }
     if (r.path == "/toggle_multiple" && r.method == "POST") {
         long long eid = idOf(r.form["exam_id"]);
         int enabled = idOf(r.form["enabled"]) ? 1 : 0;
         if (ownsExam(db, eid, s.userId))
             db.exec("UPDATE exams SET allow_multiple_attempts="+std::to_string(enabled)+" WHERE exam_id="+std::to_string(eid));
         redirect(fd, "/examiner");
         return true;
     }
     // 4. Show questions and forms for managing an exam.
     if (r.path == "/manage_exam") {
         long long eid = idOf(r.query["id"]);
         auto ex = db.rows("SELECT title,duration_minutes,status FROM exams WHERE exam_id="+std::to_string(eid)+" AND created_by="+std::to_string(s.userId));
         if (ex.empty()) {
             sendResponse(fd, 404, "Exam not found");
             return true;
         }
         auto qs = db.rows("SELECT question_id,question_type,question_text,marks,time_limit_seconds,memory_limit_mb FROM questions WHERE exam_id="+std::to_string(eid)+" ORDER BY question_id");
         std::string b = examinerStyles();
         b += "<div class='exam-wrap'><h1>Manage: "+html(ex[0][0])+"</h1>"
              "<p>Status: <b>"+html(ex[0][2])+"</b> | Duration: "+ex[0][1]+" min</p>"
              "<a class='back-link' href='/examiner'>← Back to Dashboard</a>"
              "<div class='exam-grid'>";
         b += "<div class='exam-card'><h2>Add Question</h2>" + questionForm(eid) + "</div>";
         b += "<div class='exam-card'><h2>Questions ("+std::to_string(qs.size())+")</h2>";
         if (qs.empty())
             b += "<p class='small'>No questions yet. Use the form on the left.</p>";
         for (auto& q : qs) {
             b += "<div class='question-card'>";
             b += "<div class='question-head'>";
             b += "<b>Q"+q[0]+"</b><span class='badge'>"+html(q[1])+"</span>";
             b += "<span style='margin-left:auto;display:flex;gap:6px'>"
                  "<a href='/edit_question?id="+q[0]+"&exam_id="+std::to_string(eid)+"'><button class='btn-edit' type='button'>✏ Edit</button></a>"
                  "<form method='post' action='/delete_question' style='margin:0'>"
                  "<input type='hidden' name='question_id' value='"+q[0]+"'>"
                  "<input type='hidden' name='exam_id' value='"+std::to_string(eid)+"'>"
                  "<button class='btn-danger' type='submit' onclick=\"return confirm('Delete this question?')\">✕ Delete</button>"
                  "</form></span>";
             b += "</div>";
             b += "<p>"+html(q[2])+"</p>";
             b += "<p><b>Marks:</b> "+q[3];
             if (q[1] == "CODING")
                 b += " &nbsp;|&nbsp; <b>Time:</b> "+q[4]+"s &nbsp;|&nbsp; <b>Mem:</b> "+q[5]+" MB";
             b += "</p>";
             if (q[1] == "MCQ") {
                 auto opts = db.rows("SELECT option_id,option_text,is_correct FROM mcq_options WHERE question_id="+q[0]+" ORDER BY option_id");
                 b += "<h4>Options</h4>";
                 if (opts.empty()) b += "<p class='small'>No options yet.</p>";
                 for (auto& o : opts) {
                     b += "<form method='post' action='/update_option' class='option-row'>"
                          "<input type='hidden' name='option_id' value='"+o[0]+"'>"
                          "<input type='text' name='text' value='"+html(o[1])+"' required>"
                          "<label class='correct-box'><input type='checkbox' name='correct' value='1' "
                          +(o[2]=="1"?"checked ":"")+"> Correct</label>"
                          "<button class='btn-secondary' type='submit'>Save option</button></form>";
                 }
                 b += "<hr>" + optionForm(q[0]);
             }
             if (q[1] == "CODING") {
                 auto tc = db.rows("SELECT test_case_id,input_data,expected_output FROM test_cases WHERE question_id="+q[0]+" ORDER BY test_case_id");
                 b += "<h4>Test Cases ("+std::to_string(tc.size())+")</h4>";
                 if (tc.empty()) b += "<p class='small'>No test cases yet.</p>";
                 for (auto& t : tc) {
                     b += "<form method='post' action='/update_test' class='testcase' style='display:grid;grid-template-columns:1fr 1fr auto auto;gap:8px;align-items:start'>"
                          "<input type='hidden' name='test_case_id' value='"+t[0]+"'>"
                          "<label>Input<textarea name='input' required>"+html(t[1])+"</textarea></label>"
                          "<label>Expected<textarea name='expected' required>"+html(t[2])+"</textarea></label>"
                          "<button class='btn-secondary' type='submit'>Save</button></form>"
                          "<form method='post' action='/delete_test' style='margin:4px 0'>"
                          "<input type='hidden' name='test_case_id' value='"+t[0]+"'>"
                          "<input type='hidden' name='question_id' value='"+q[0]+"'>"
                          "<input type='hidden' name='exam_id' value='"+std::to_string(eid)+"'>"
                          "<button class='btn-danger' type='submit' style='padding:4px 8px;font-size:12px'>✕</button>"
                          "</form></div>";
                 }
                 b += "<hr>" + testCaseForm(q[0]);
             }
             if (q[1] == "DESCRIPTIVE") {
                 b += "<p class='small'>Manually evaluate from <a href='/review?exam_id="+std::to_string(eid)+"'>Review Descriptive</a>.</p>";
             }
             b += "</div>";
         }
         b += "</div></div></div>";
         sendResponse(fd, 200, page("Manage Exam", b));
         return true;
     }
     if (r.path == "/edit_question") {
         long long qid = idOf(r.query["id"]);
         long long eid = idOf(r.query["exam_id"]);
         auto q = db.rows("SELECT question_type,question_text,marks,time_limit_seconds,memory_limit_mb "
                          "FROM questions q JOIN exams e ON q.exam_id=e.exam_id "
                          "WHERE q.question_id="+std::to_string(qid)+" AND e.created_by="+std::to_string(s.userId));
         if (q.empty()) {
             sendResponse(fd, 404, "Question not found");
             return true;
         }
         std::string b = examinerStyles();
         b += "<div class='exam-wrap'><h1>Edit Question</h1>"
              "<a class='back-link' href='/manage_exam?id="+std::to_string(eid)+"'>← Back to Exam</a>"
              "<div class='exam-card'>"
              "<form method='post' action='/update_question'>"
              "<input type='hidden' name='question_id' value='"+std::to_string(qid)+"'>"
              "<input type='hidden' name='exam_id' value='"+std::to_string(eid)+"'>";
         b += fieldLabel("Type",
             "<select name='type' id='editType' onchange='showCodingFields()'>"
             "<option value='MCQ' "+(q[0][0]=="MCQ"?std::string("selected"):std::string(""))+">MCQ</option>"
             "<option value='DESCRIPTIVE' "+(q[0][0]=="DESCRIPTIVE"?std::string("selected"):std::string(""))+">Descriptive</option>"
             "<option value='CODING' "+(q[0][0]=="CODING"?std::string("selected"):std::string(""))+">Coding (C++)</option>"
             "</select><p class='small'>Changing type is allowed only before students answer this question. Options/test cases from the old type are removed.</p>");
         b += fieldLabel("Question Text",
             "<textarea name='text' required>"+html(q[0][1])+"</textarea>");
         b += "<div class='meta'>";
         b += fieldLabel("Marks", "<input name='marks' type='number' min='1' value='"+q[0][2]+"' required>");
         b += "<div id='codingFields' style='"+(q[0][0]=="CODING"?std::string(""):std::string("display:none"))+"'>";
         b += fieldLabel("Time Limit (s)", "<input name='time_limit' type='number' min='1' value='"+q[0][3]+"'>");
         b += fieldLabel("Memory (MB)",    "<input name='memory_limit' type='number' min='16' value='"+q[0][4]+"'>");
         b += "</div>";
         b += "</div>";
         b += R"HTML(<script>
 function showCodingFields(){document.getElementById('codingFields').style.display=document.getElementById('editType').value==='CODING'?'block':'none';}
 </script>)HTML";
         b += "<button class='btn' type='submit'>Save Changes</button>"
              "</form></div></div>";
         sendResponse(fd, 200, page("Edit Question", b));
         return true;
     }
     if (r.path == "/update_question" && r.method == "POST") {
         long long qid = idOf(r.form["question_id"]);
         long long eid = idOf(r.form["exam_id"]);
         auto own = db.rows("SELECT q.question_id FROM questions q JOIN exams e ON q.exam_id=e.exam_id "
                            "WHERE q.question_id="+std::to_string(qid)+" AND e.created_by="+std::to_string(s.userId));
         if (own.empty()) {
             sendResponse(fd, 403, "Not allowed");
             return true;
         }
         const std::string type = r.form["type"];
         if (type != "MCQ" && type != "DESCRIPTIVE" && type != "CODING") { sendResponse(fd, 400, "Invalid question type"); return true; }
         auto oldType = db.rows("SELECT question_type FROM questions WHERE question_id="+std::to_string(qid));
         if (!oldType.empty() && oldType[0][0] != type) {
             auto answers = db.rows("SELECT COUNT(*) FROM student_answers WHERE question_id=" + std::to_string(qid));
             if (!answers.empty() && idOf(answers[0][0]) > 0) {
                 sendResponse(fd, 400, page("Cannot change type", "<h2>Question type cannot be changed after students have answered it.</h2><p>Edit the statement, marks, limits, options, or test cases instead.</p><p><a href='/manage_exam?id="+std::to_string(eid)+"'>Back to exam</a></p>"));
                 return true;
             }
             db.exec("DELETE FROM mcq_options WHERE question_id="+std::to_string(qid));
             db.exec("DELETE FROM test_cases WHERE question_id="+std::to_string(qid));
         }
         std::string tl  = r.form.count("time_limit")   ? std::to_string(std::max(1LL,idOf(r.form.at("time_limit"))))   : "2";
         std::string ml  = r.form.count("memory_limit") ? std::to_string(std::max(16LL,idOf(r.form.at("memory_limit")))) : "64";
         db.exec("UPDATE questions SET question_text='"+db.escape(r.form["text"])
                 +"',question_type='"+type+"',marks="+std::to_string(std::max(1LL,idOf(r.form["marks"])))
                 +",time_limit_seconds="+tl
                 +",memory_limit_mb="+ml
                 +" WHERE question_id="+std::to_string(qid));
         redirect(fd, "/manage_exam?id="+std::to_string(eid));
         return true;
     }
     if (r.path == "/delete_question" && r.method == "POST") {
         long long qid = idOf(r.form["question_id"]);
         long long eid = idOf(r.form["exam_id"]);
         auto own = db.rows("SELECT q.question_id FROM questions q JOIN exams e ON q.exam_id=e.exam_id "
                            "WHERE q.question_id="+std::to_string(qid)+" AND e.created_by="+std::to_string(s.userId));
         if (!own.empty())
             db.exec("DELETE FROM questions WHERE question_id="+std::to_string(qid));
         redirect(fd, "/manage_exam?id="+std::to_string(eid));
         return true;
     }
     // Add a question to an exam.
     if (r.path == "/add_question" && r.method == "POST") {
         long long eid = idOf(r.form["exam_id"]);
         const std::string type = r.form["type"];
         if (!ownsExam(db, eid, s.userId) || (type != "MCQ" && type != "DESCRIPTIVE" && type != "CODING")) {
             sendResponse(fd, 403, "Not allowed"); return true;
         }
         db.exec("INSERT INTO questions(exam_id,question_type,question_text,marks,time_limit_seconds,memory_limit_mb) VALUES("
                 +std::to_string(eid)+",'"+type+"','"+db.escape(r.form["text"])+"',"
                 +std::to_string(std::max(1LL,idOf(r.form["marks"])))+","
                 +std::to_string(std::max(1LL,idOf(r.form["time_limit"])))+","
                 +std::to_string(std::max(16LL,idOf(r.form["memory_limit"])))+")");
         redirect(fd, "/manage_exam?id="+std::to_string(eid));
         return true;
     }
     // Add an answer option to an MCQ.
     if (r.path == "/add_option" && r.method == "POST") {
         long long qid = idOf(r.form["question_id"]);
         if (!ownsQuestion(db, qid, s.userId)) { sendResponse(fd, 403, "Not allowed"); return true; }
         db.exec("INSERT INTO mcq_options(question_id,option_text,is_correct) VALUES("
                 +std::to_string(qid)+",'"+db.escape(r.form["text"])+"',"
                 +(r.form.count("correct") ? "1" : "0")+")");
         auto ex = db.rows("SELECT exam_id FROM questions WHERE question_id="+std::to_string(qid));
         std::string examId = "0";
         if (!ex.empty()) {
             examId = ex[0][0];
         }
         redirect(fd, "/manage_exam?id=" + examId);
         return true;
     }
     if (r.path == "/update_option" && r.method == "POST") {
         long long oid = idOf(r.form["option_id"]);
         auto q = db.rows("SELECT o.question_id FROM mcq_options o JOIN questions q ON o.question_id=q.question_id "
                          "JOIN exams e ON q.exam_id=e.exam_id WHERE o.option_id="+std::to_string(oid)
                          +" AND q.question_type='MCQ' AND e.created_by="+std::to_string(s.userId));
         if (q.empty()) {
             sendResponse(fd, 403, "Not allowed");
             return true;
         }
         db.exec("UPDATE mcq_options SET option_text='"+db.escape(r.form["text"])+"',is_correct="
                 +(r.form.count("correct") ? "1" : "0")+" WHERE option_id="+std::to_string(oid));
         auto ex = db.rows("SELECT exam_id FROM questions WHERE question_id="+q[0][0]);
         std::string examId = "0";
         if (!ex.empty()) {
             examId = ex[0][0];
         }
         redirect(fd, "/manage_exam?id=" + examId);
         return true;
     }
     // Add an input and expected output for a coding question.
     if (r.path == "/add_test" && r.method == "POST") {
         long long qid = idOf(r.form["question_id"]);
         if (!ownsQuestion(db, qid, s.userId)) { sendResponse(fd, 403, "Not allowed"); return true; }
         db.exec("INSERT INTO test_cases(question_id,input_data,expected_output) VALUES("
                 +std::to_string(qid)+",'"+db.escape(r.form["input"])+"','"+db.escape(r.form["expected"])+"')");
         auto ex = db.rows("SELECT exam_id FROM questions WHERE question_id="+std::to_string(qid));
         std::string examId = "0";
         if (!ex.empty()) {
             examId = ex[0][0];
         }
         redirect(fd, "/manage_exam?id=" + examId);
         return true;
     }
     if (r.path == "/update_test" && r.method == "POST") {
         long long tcid = idOf(r.form["test_case_id"]);
         auto q = db.rows("SELECT tc.question_id FROM test_cases tc JOIN questions q ON tc.question_id=q.question_id "
                          "JOIN exams e ON q.exam_id=e.exam_id WHERE tc.test_case_id="+std::to_string(tcid)
                          +" AND q.question_type='CODING' AND e.created_by="+std::to_string(s.userId));
         if (q.empty()) {
             sendResponse(fd, 403, "Not allowed");
             return true;
         }
         db.exec("UPDATE test_cases SET input_data='"+db.escape(r.form["input"])+"',expected_output='"
                 +db.escape(r.form["expected"])+"' WHERE test_case_id="+std::to_string(tcid));
         auto ex = db.rows("SELECT exam_id FROM questions WHERE question_id="+q[0][0]);
         std::string examId = "0";
         if (!ex.empty()) {
             examId = ex[0][0];
         }
         redirect(fd, "/manage_exam?id=" + examId);
         return true;
     }
     if (r.path == "/delete_test" && r.method == "POST") {
         long long tcid = idOf(r.form["test_case_id"]);
         long long eid  = idOf(r.form["exam_id"]);
         db.exec("DELETE tc FROM test_cases tc "
                 "JOIN questions q ON tc.question_id=q.question_id "
                 "JOIN exams e ON q.exam_id=e.exam_id "
                 "WHERE tc.test_case_id="+std::to_string(tcid)+" AND e.created_by="+std::to_string(s.userId));
         redirect(fd, "/manage_exam?id="+std::to_string(eid));
         return true;
     }
     // Show descriptive answers for manual marking.
     if (r.path == "/review") {
         long long eid = idOf(r.query["exam_id"]);
         auto ex = db.rows("SELECT title FROM exams WHERE exam_id="+std::to_string(eid)+" AND created_by="+std::to_string(s.userId));
         if (ex.empty()) {
             sendResponse(fd, 404, "Exam not found");
             return true;
         }
         auto rows = db.rows(
             "SELECT ea.attempt_id,u.name,q.question_id,q.question_text,sa.answer_id,sa.answer_text,sa.marks_awarded "
             "FROM exam_attempts ea JOIN users u ON ea.student_id=u.user_id "
             "JOIN student_answers sa ON ea.attempt_id=sa.attempt_id "
             "JOIN questions q ON sa.question_id=q.question_id "
             "WHERE ea.exam_id="+std::to_string(eid)+" AND q.question_type='DESCRIPTIVE' ORDER BY ea.attempt_id");
         std::string b = examinerStyles();
         b += "<div class='exam-wrap'><h1>Review Descriptive — "+html(ex[0][0])+"</h1>"
              "<p><a href='/examiner'>← Dashboard</a></p>";
         if (rows.empty()) b += "<div class='exam-card'><p>No descriptive answers yet.</p></div>";
         for (auto& row : rows) {
             auto qm = db.rows("SELECT marks FROM questions WHERE question_id="+row[2]);
             std::string mx = "0";
             if (!qm.empty()) {
                 mx = qm[0][0];
             }
             b += "<div class='exam-card'><p><b>Student:</b> "+html(row[1])+"</p>"
                  "<p><b>Question:</b> "+html(row[3])+"</p>"
                  "<p><b>Answer:</b><br>"+html(row[5])+"</p>"
                  "<form method='post' action='/evaluate'>"
                  "<input type='hidden' name='answer_id' value='"+row[4]+"'>"
                  "<input type='hidden' name='exam_id' value='"+std::to_string(eid)+"'>"
                  "<div class='field'><label><b>Marks (max "+mx+")</b></label>"
                  "<input name='marks' type='number' min='0' max='"+mx+"' value='"+row[6]+"'></div>"
                  "<div class='field'><label><b>Feedback (optional)</b></label>"
                  "<input name='feedback' placeholder='Write feedback...'></div>"
                  "<button class='btn'>Save Marks</button></form></div>";
         }
         b += "</div>";
         sendResponse(fd, 200, page("Review Descriptive", b));
         return true;
     }
     // Save marks and feedback for a descriptive answer.
     if (r.path == "/evaluate" && r.method == "POST") {
         long long aid = idOf(r.form["answer_id"]);
         long long eid = idOf(r.form["exam_id"]);
         long long marks = idOf(r.form["marks"]);
         auto own = db.rows(
             "SELECT sa.answer_id,q.marks FROM student_answers sa "
             "JOIN questions q ON sa.question_id=q.question_id "
             "JOIN exam_attempts ea ON sa.attempt_id=ea.attempt_id "
             "WHERE sa.answer_id="+std::to_string(aid)+" AND ea.exam_id="+std::to_string(eid)
             +" AND q.question_type='DESCRIPTIVE' "
             "AND EXISTS(SELECT 1 FROM exams e WHERE e.exam_id=ea.exam_id AND e.created_by="+std::to_string(s.userId)+")");
         if (own.empty()) { sendResponse(fd, 403, "Invalid answer"); return true; }
         marks = std::min(marks, idOf(own[0][1]));
         db.exec("UPDATE student_answers SET marks_awarded="+std::to_string(marks)
                 +",feedback='"+db.escape(r.form["feedback"])+"',evaluated_by="+std::to_string(s.userId)
                 +" WHERE answer_id="+std::to_string(aid));
         auto at = db.rows("SELECT attempt_id FROM student_answers WHERE answer_id="+std::to_string(aid));
         if (!at.empty()) {
             long long attempt = idOf(at[0][0]);
             auto sm = db.rows("SELECT COALESCE(SUM(marks_awarded),0) FROM student_answers WHERE attempt_id="+std::to_string(attempt));
             auto tm = db.rows("SELECT COALESCE(SUM(q.marks),0) FROM questions q JOIN exam_attempts ea ON q.exam_id=ea.exam_id WHERE ea.attempt_id="+std::to_string(attempt));
             if (!sm.empty() && !tm.empty())
                 db.exec("UPDATE exam_attempts SET obtained_marks="+sm[0][0]+",total_marks="+tm[0][0]+" WHERE attempt_id="+std::to_string(attempt));
         }
         redirect(fd, "/review?exam_id="+std::to_string(eid));
         return true;
     }
     // Show the exam summary and question statistics.
     if (r.path == "/exam_results") {
         long long eid = idOf(r.query["exam_id"]);
         auto ex = db.rows("SELECT title FROM exams WHERE exam_id="+std::to_string(eid)+" AND created_by="+std::to_string(s.userId));
         if (ex.empty()) {
             sendResponse(fd, 404, "Exam not found");
             return true;
         }
         auto qs = db.rows("SELECT question_id,question_type,question_text,marks FROM questions WHERE exam_id="+std::to_string(eid)+" ORDER BY question_id");
         auto attempts = db.rows(
             "SELECT ea.attempt_id,u.name,u.email,ea.obtained_marks,ea.total_marks,ea.status "
             "FROM exam_attempts ea JOIN users u ON ea.student_id=u.user_id "
             "WHERE ea.exam_id="+std::to_string(eid)+" ORDER BY u.name");
         std::string b = examinerStyles();
         b += "<div class='exam-wrap'><h1>Results: "+html(ex[0][0])+"</h1>"
              "<p><a href='/examiner'>← Dashboard</a></p>";
         if (attempts.empty()) {
             b += "<div class='exam-card'><p>No attempts yet.</p></div></div>";
             sendResponse(fd, 200, page("Results", b));
             return true;
         }
         b += "<div class='exam-card'><h2>Summary</h2><table class='result-table'><tr>"
              "<th>Student</th><th>Email</th><th>Score</th><th>%</th><th>Status</th><th>Details</th></tr>";
         for (auto& at : attempts) {
             int pct = 0;
             if (at[4] != "0") {
                 pct = std::stoi(at[3]) * 100 / std::stoi(at[4]);
             }
 
             std::string cls = "fail";
             if (pct >= 60) {
                 cls = "pass";
             }
             b += "<tr><td>"+html(at[1])+"</td><td>"+html(at[2])+"</td>"
                  "<td class='"+cls+"'>"+at[3]+" / "+at[4]+"</td>"
                  "<td class='"+cls+"'>"+std::to_string(pct)+"%</td>"
                  "<td>"+at[5]+"</td>"
                  "<td><a href='/student_detail?attempt_id="+at[0]+"&exam_id="+std::to_string(eid)+"'>View</a></td></tr>";
         }
         b += "</table></div>";
         b += "<div class='exam-card'><h2>Question Statistics</h2><table class='result-table'>"
              "<tr><th>#</th><th>Type</th><th>Question</th><th>Max</th><th>Avg Score</th></tr>";
         for (size_t i = 0; i < qs.size(); ++i) {
             auto avg = db.rows("SELECT COALESCE(AVG(sa.marks_awarded),0) FROM student_answers sa "
                                "JOIN exam_attempts ea ON sa.attempt_id=ea.attempt_id "
                                "WHERE sa.question_id="+qs[i][0]+" AND ea.exam_id="+std::to_string(eid));
             std::string avgStr = "0";
             if (!avg.empty()) {
                 avgStr = avg[0][0].substr(0, avg[0][0].find('.') + 3);
             }
             std::string questionText = html(qs[i][2].substr(0, 60));
             if (qs[i][2].size() > 60) {
                 questionText += "…";
             }
 
             b += "<tr><td>" + std::to_string(i + 1) + "</td><td><span class='badge'>" + qs[i][1] + "</span></td>"
                  "<td>" + questionText + "</td>"
                  "<td>" + qs[i][3] + "</td><td>" + avgStr + "</td></tr>";
         }
         b += "</table></div></div>";
         sendResponse(fd, 200, page("Results", b));
         return true;
     }
     if (r.path == "/student_detail") {
         long long attempt = idOf(r.query["attempt_id"]);
         long long eid     = idOf(r.query["exam_id"]);
         auto ex = db.rows("SELECT e.title FROM exams e JOIN exam_attempts ea ON ea.exam_id=e.exam_id "
                           "WHERE ea.attempt_id="+std::to_string(attempt)+" AND e.created_by="+std::to_string(s.userId));
         if (ex.empty()) { sendResponse(fd, 403, "Not allowed"); return true; }
         auto info = db.rows("SELECT u.name,ea.obtained_marks,ea.total_marks FROM exam_attempts ea "
                             "JOIN users u ON ea.student_id=u.user_id WHERE ea.attempt_id="+std::to_string(attempt));
         auto ans  = db.rows(
             "SELECT q.question_text,q.question_type,sa.marks_awarded,q.marks,"
             "sa.answer_text,COALESCE(latest.verdict,'') "
             "FROM student_answers sa "
             "JOIN questions q ON sa.question_id=q.question_id "
             "LEFT JOIN ("
             "  SELECT cs.answer_id,er.verdict "
             "  FROM code_submissions cs JOIN execution_results er ON er.submission_id=cs.submission_id "
             "  WHERE cs.submission_id=(SELECT MAX(cs2.submission_id) FROM code_submissions cs2 WHERE cs2.answer_id=cs.answer_id)"
             ") latest ON latest.answer_id=sa.answer_id "
             "WHERE sa.attempt_id="+std::to_string(attempt));
         std::string b = examinerStyles();
         b += "<div class='exam-wrap'><h1>"+html(ex[0][0])+" — "+html(info[0][0])+"</h1>"
              "<p>Score: <b>"+info[0][1]+" / "+info[0][2]+"</b> &nbsp;|&nbsp; "
              "<a href='/exam_results?exam_id="+std::to_string(eid)+"'>← Back to Results</a></p>";
         for (auto& a : ans) {
             std::string verdict = "";
             if (!a[5].empty()) {
                 verdict = " <span class='badge'>" + html(a[5]) + "</span>";
             }
             b += "<div class='exam-card'><b>"+html(a[1])+"</b>"+verdict
                  +"<p>"+html(a[0])+"</p>"
                  +"<p><b>Marks:</b> "+a[2]+" / "+a[3]+"</p>";
             if (!a[4].empty()) {
                 if (a[1] == "CODING")
                     b += "<pre style='background:#1e1e1e;color:#d4d4d4;padding:12px;border-radius:6px;font-size:12px;overflow-x:auto'>"+html(a[4])+"</pre>";
                 else
                     b += "<p style='background:#f4f6fb;padding:10px;border-radius:6px'>"+html(a[4])+"</p>";
             }
             b += "</div>";
         }
         b += "</div>";
         sendResponse(fd, 200, page("Student Detail", b));
         return true;
     }
     return false;
 }