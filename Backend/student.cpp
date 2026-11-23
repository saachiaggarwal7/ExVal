#include "student.h"
#include "phase 3/anti_cheat.h"
#include "phase 3/code_versioning.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// execution_results keeps the overall coding verdict. This stores the raw
// outcome for every test case so later analytics do not need to infer it.
static void saveCaseResults(Database& db, long long submissionId,
                            const std::vector<std::string>& caseIds,
                            const std::shared_ptr<CodeRunner::Job>& job) {
    const size_t count = std::min(caseIds.size(), job->testResults.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& result = job->testResults[i];
        db.exec("INSERT INTO code_test_case_results(submission_id,test_case_id,verdict,execution_time_ms) VALUES("
                +std::to_string(submissionId)+","+caseIds[i]+",'"+db.escape(result.verdict)+"',"
                +std::to_string(result.timeMs)+")");
    }
}

static void upsertAnswer(Database& db, long long aid, const std::string& qid, bool isMCQ,
                         const std::string& val, bool overwriteEmpty) {
    std::string mcqVal = (isMCQ && !val.empty()) ? val : "NULL";
    auto old = db.rows("SELECT answer_id,COALESCE(selected_option_id,''),COALESCE(answer_text,'') FROM student_answers WHERE attempt_id="
                       +std::to_string(aid)+" AND question_id="+qid);
    if (old.empty()) {
        db.exec("INSERT INTO student_answers(attempt_id,question_id,selected_option_id,answer_text,marks_awarded) VALUES("
                +std::to_string(aid)+","+qid+","+mcqVal+","
                +(isMCQ?"NULL":"'"+db.escape(val)+"'")+",0)");
        return;
    }
    if (!overwriteEmpty && val.empty()) return;
    db.exec("UPDATE student_answers SET selected_option_id="+mcqVal
            +",answer_text="+(isMCQ?"NULL":"'"+db.escape(val)+"'")
            +" WHERE answer_id="+old[0][0]);
}

static int gradeAttempt(Database& db, long long aid, const std::vector<std::vector<std::string>>& qs) {
    int obt = 0;
    for (auto& q : qs) {
        // For CODING questions: if student chose a specific version to submit, ensure answer_text has that version's code
        if (q[1] == "CODING") {
            auto selVer = db.rows("SELECT version_id, source_code FROM code_versions WHERE attempt_id="
                                  +std::to_string(aid)+" AND question_id="+q[0]+" AND is_selected=1 LIMIT 1");
            if (!selVer.empty() && !selVer[0][1].empty()) {
                db.exec("UPDATE student_answers SET answer_text='"+db.escape(selVer[0][1])
                        +"', selected_version_id="+selVer[0][0]
                        +" WHERE attempt_id="+std::to_string(aid)+" AND question_id="+q[0]);
            }
        }

        auto ans = db.rows("SELECT answer_id,selected_option_id,COALESCE(answer_text,'') FROM student_answers WHERE attempt_id="
                           +std::to_string(aid)+" AND question_id="+q[0]);
        if (ans.empty()) continue;
        if (q[1] == "MCQ") {
            auto c = db.rows("SELECT option_id FROM mcq_options WHERE question_id="+q[0]+" AND is_correct=1 LIMIT 1");
            int m = (!c.empty() && !ans[0][1].empty() && ans[0][1] == c[0][0]) ? std::stoi(q[2]) : 0;
            db.exec("UPDATE student_answers SET marks_awarded="+std::to_string(m)+" WHERE answer_id="+ans[0][0]);
            obt += m;
        } else if (q[1] == "CODING" && !ans[0][2].empty()) {
            auto latest = db.rows(
                "SELECT er.passed_tests,er.total_tests FROM code_submissions cs "
                "JOIN execution_results er ON er.submission_id=cs.submission_id "
                "WHERE cs.answer_id="+ans[0][0]+" ORDER BY cs.submission_id DESC LIMIT 1");
            int passed = 0, totalTests = 0;
            if (!latest.empty()) {
                passed = std::stoi(latest[0][0]);
                totalTests = std::stoi(latest[0][1]);
            } else {
                auto tc = db.rows("SELECT test_case_id,input_data,expected_output FROM test_cases WHERE question_id="+q[0]+" ORDER BY test_case_id");
                std::vector<std::pair<std::string,std::string>> tests;
                std::vector<std::string> caseIds;
                for (auto& t : tc) { caseIds.push_back(t[0]); tests.push_back({t[1], t[2]}); }
                auto j = runner.submit(std::stoi(ans[0][0]), ans[0][2], tests,
                                       std::max(1, std::stoi(q[3])), std::max(16, std::stoi(q[4])));
                std::unique_lock<std::mutex> lk(j->m);
                j->cv.wait(lk, [&]{ return j->done; });
                db.exec("INSERT INTO code_submissions(answer_id,source_code,status) VALUES("
                        +ans[0][0]+",'"+db.escape(ans[0][2])+"','"+j->verdict+"')");
                long long sid = db.lastId();
                db.exec("INSERT INTO execution_results(submission_id,verdict,passed_tests,total_tests,execution_time_ms) VALUES("
                        +std::to_string(sid)+",'"+j->verdict+"',"
                        +std::to_string(j->passed)+","
                        +std::to_string((int)tests.size())+","
                        +std::to_string(j->timeMs)+")");
                saveCaseResults(db, sid, caseIds, j);
                passed = j->passed;
                totalTests = (int)tests.size();
            }
            int m = totalTests == 0 ? 0 : (passed * std::stoi(q[2]) / totalTests);
            db.exec("UPDATE student_answers SET marks_awarded="+std::to_string(m)+" WHERE answer_id="+ans[0][0]);
            obt += m;
        }
    }
    return obt;
}

static bool finalizeAttempt(Database& db, long long aid, int total, int obtained, bool isDisqualified = false, const std::string& reason = "") {
    std::string disqSql = isDisqualified ? ",is_disqualified=1,disqualification_reason='"+db.escape(reason)+"'" : "";
    std::string q = "UPDATE exam_attempts SET status='SUBMITTED',submitted_at=NOW(),total_marks="
                    +std::to_string(total)+",obtained_marks="+std::to_string(obtained)
                    +disqSql+" WHERE attempt_id="+std::to_string(aid)+" AND status='IN_PROGRESS'";
    return db.exec(q);
}

// ── CSS injected once on the exam page ───────────────────────────────────────
static const std::string EXAM_CSS = R"HTML(
<style>
header{display:none!important}
main{max-width:none!important;width:100%!important;margin:0!important;padding:0!important;background:#fff!important;border-radius:0!important;box-shadow:none!important}
body{background:#fff;overflow:hidden;height:100vh}
.exam-workspace{height:100vh;display:flex;flex-direction:column;user-select:none;-webkit-user-select:none}
.exam-telemetry{flex:0 0 auto;background:#fff;padding:8px 12px;border-bottom:1px solid #e2e8f0;display:flex;justify-content:space-between;align-items:center;gap:12px}
.exam-info h1{margin:0;font-size:14px;font-weight:600;color:#0f172a}
.exam-info p{margin:2px 0 0;color:#64748b;font-size:11px}
.exam-timer{display:flex;align-items:center;gap:6px;background:#f8fafc;padding:4px 10px;border:1px solid #e2e8f0;font-family:ui-monospace,Menlo,Consolas,monospace;font-weight:600;font-size:14px}
.anticheat-shield{display:flex;align-items:center;gap:6px;background:#fef2f2;border:1px solid #fecaca;padding:4px 10px;color:#991b1b;font-size:12px;font-weight:600}
.anticheat-shield.warning-active{background:#fff1f2;border-color:#f43f5e}
.exam-split{flex:1;min-height:0;display:flex;background:#e2e8f0}
.col-nav,.col-problem,.col-answer{background:#fff;min-height:0;overflow:auto}
.col-nav{flex:0 0 180px;width:180px;min-width:120px;max-width:360px;padding:8px}
.col-problem{flex:0 0 380px;width:380px;min-width:200px;max-width:70vw}
.col-answer{flex:1 1 auto;min-width:280px;display:flex;flex-direction:column}
.split-gutter{flex:0 0 5px;width:5px;cursor:col-resize;background:#e2e8f0}
.split-gutter:hover,.split-gutter.dragging{background:#94a3b8}
.col-nav h3{margin:0 0 8px;font-size:11px;font-weight:600;color:#64748b;text-transform:uppercase;letter-spacing:.04em}
.col-nav button{width:100%;text-align:left;margin:0 0 4px;padding:8px 10px;border:1px solid transparent;background:#fff;color:#334155;cursor:pointer;font-size:12px}
.col-nav button:hover{background:#f8fafc}
.col-nav button.active{background:#0f172a;color:#fff}
.col-nav button .q-dot{width:6px;height:6px;border-radius:50%;display:inline-block;margin-right:6px;background:#cbd5e1}
.col-nav button.answered .q-dot{background:#059669}
.exam-pane{display:none;height:100%;min-height:0}
.exam-pane.active{display:flex;flex-direction:column;flex:1}
.exam-problem{padding:16px 18px;overflow:auto}
.exam-problem h2{margin:0 0 8px;font-size:15px;font-weight:600;color:#0f172a}
.question-text{color:#334155;line-height:1.55;font-size:13px;white-space:pre-wrap}
.problem-meta{margin-top:14px;padding-top:12px;border-top:1px solid #e2e8f0;font-size:12px;color:#475569}
.problem-meta p{margin:4px 0}
.exam-answer{min-height:0;flex:1}
.exam-answer .answer-body{flex:1;min-height:0;display:flex;flex-direction:column;overflow:auto;padding:0}
.coding-editor{flex:1;min-height:0;display:flex;flex-direction:column;background:#0d1117;overflow:hidden}
.editor-shell{flex:1;min-height:0;position:relative}

.version-bar{background:#161b22;color:#fff;padding:6px 10px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px;border-bottom:1px solid #30363d;flex:0 0 auto}
.version-left{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.version-left label{font-size:11px;font-weight:600;color:#8b949e}
.version-select{background:#21262d;color:#e6edf3;border:1px solid #30363d;padding:4px 8px;font-size:12px;outline:none;max-width:220px}
.version-badge{background:#047857;color:#ecfdf5;font-size:10px;font-weight:600;padding:3px 8px}
.version-badge.not-selected{background:#30363d;color:#8b949e}
.version-actions{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.version-input{background:#21262d;color:#e6edf3;border:1px solid #30363d;padding:4px 8px;font-size:12px;outline:none;width:140px}
.btn-ver{background:#21262d;color:#e6edf3;border:1px solid #30363d;padding:4px 8px;cursor:pointer;font-size:11px}
.btn-ver:hover{background:#30363d}
.btn-ver-submit{background:#238636;border-color:#2ea043;color:#fff}
.btn-ver-danger{background:#da3633;border-color:#f85149;color:#fff}
.save-status{font-size:11px;color:#8b949e}
.code-area{width:100%;height:100%;box-sizing:border-box;font-family:ui-monospace,'JetBrains Mono',Menlo,Consolas,monospace;font-size:13px;min-height:0;border:none;padding:12px 14px;resize:none;background:#0d1117;color:#e6edf3;line-height:1.55;tab-size:4;-moz-tab-size:4;outline:none;user-select:text;-webkit-user-select:text}
.suggest-box{position:absolute;z-index:40;min-width:220px;max-height:220px;overflow:auto;background:#1e1e1e;border:1px solid #3c3c3c;box-shadow:0 8px 24px rgba(0,0,0,.4);display:none}
.suggest-box.visible{display:block}
.suggest-item{padding:4px 10px;font:12px/1.4 ui-monospace,Menlo,Consolas,monospace;color:#d4d4d4;cursor:pointer}
.suggest-item small{color:#8b949e;margin-left:8px;font-size:10px}
.suggest-item.active,.suggest-item:hover{background:#04395e}
.ide-footer{flex:0 0 auto;padding:8px 10px;background:#161b22;border-top:1px solid #30363d;display:flex;justify-content:space-between;align-items:center;gap:8px}
.run-btn{background:#238636;color:#fff;border:0;padding:6px 12px;cursor:pointer;font-size:12px}
.run-btn:disabled{opacity:.5;cursor:not-allowed}
.verdict-box{margin:0;overflow:auto;max-height:220px;border-top:1px solid #30363d;background:#fff}
.verdict-header{padding:8px 12px;font-weight:500;font-size:12px;display:flex;align-items:center;gap:8px}
.verdict-header.accepted{background:#ecfdf5;color:#059669}
.verdict-header.wrong{background:#fef2f2;color:#dc2626}
.verdict-header.compile-err{background:#fffbeb;color:#d97706}
.verdict-header.runtime{background:#fef2f2;color:#dc2626}
.verdict-header.tle{background:#eff6ff;color:#2563eb}
.test-row{display:grid;grid-template-columns:28px 1fr 1fr auto;gap:8px;padding:8px 12px;border-top:1px solid #e2e8f0;font-size:12px;align-items:start}
.test-row.pass{background:#f0fdf4}
.test-row.fail{background:#fef2f2}
.test-row pre{margin:0;font-size:11px;white-space:pre-wrap;word-break:break-all;background:#f8fafc;padding:4px 6px}
.compile-pre{background:#1e293b;color:#fca5a5;padding:10px 12px;font-family:ui-monospace,monospace;font-size:12px;white-space:pre-wrap;margin:0}
.spinner{display:inline-block;width:12px;height:12px;border:2px solid #cbd5e1;border-top-color:#0f172a;border-radius:50%;animation:spin .7s linear infinite;vertical-align:middle;margin-right:6px}
.question-actions{display:flex;justify-content:space-between;gap:8px;padding:8px 10px;border-top:1px solid #e2e8f0;flex:0 0 auto;background:#fff}
.question-actions button{width:auto}
.mcq-options{display:flex;flex-direction:column;gap:8px;padding:16px}
.mcq-option{display:flex;align-items:flex-start;gap:10px;padding:10px;border:1px solid #e2e8f0;cursor:pointer;font-size:13px;user-select:text}
.mcq-option:hover{background:#f8fafc}
.mcq-option input[type=radio]{margin-top:3px}
.desc-area{width:100%;flex:1;min-height:200px;box-sizing:border-box;padding:12px;border:0;font:14px/1.5 inherit;resize:none;user-select:text}

/* Anti-Cheat Modal Overlays */
.ac-modal-overlay{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(15,23,42,0.88);backdrop-filter:blur(6px);z-index:999999;display:flex;align-items:center;justify-content:center;padding:20px}
.ac-modal{background:#fff;border-radius:12px;max-width:540px;width:100%;box-shadow:0 25px 50px -12px rgba(0,0,0,0.5);overflow:hidden;border:1px solid #cbd5e1;text-align:center}
.ac-modal-header{background:#0f172a;color:#fff;padding:24px 20px}
.ac-modal-header.danger{background:#b91c1c}
.ac-modal-header.warning{background:#c2410c}
.ac-modal-header h2{margin:0;font-size:22px;color:#fff}
.ac-modal-body{padding:24px;color:#334155;font-size:15px;line-height:1.6}
.ac-modal-body ul{text-align:left;margin:16px auto;max-width:420px;padding-left:20px}
.ac-modal-body li{margin:8px 0}
.ac-modal-footer{padding:16px 24px;background:#f8fafc;border-top:1px solid #e2e8f0}
.ac-btn{background:#0f172a;color:#fff;border:none;padding:12px 28px;border-radius:8px;font-size:15px;font-weight:600;cursor:pointer;transition:background 0.2s}
.ac-btn:hover{background:#1e293b}
.ac-btn-danger{background:#dc2626}
.ac-btn-danger:hover{background:#b91c1c}

/* Toast for blocked operations */
#ac-toast{position:fixed;top:20px;left:50%;transform:translateX(-50%);background:#1e293b;color:#f8fafc;padding:12px 24px;border-radius:8px;box-shadow:0 10px 15px -3px rgba(0,0,0,0.3);z-index:9999999;font-weight:600;font-size:14px;display:none;align-items:center;gap:10px;border:1px solid #ef4444}

@keyframes spin{to{transform:rotate(360deg)}}
@media(max-width:800px){.exam-split{flex-direction:column}.col-nav,.col-problem{flex:0 0 auto;width:100%!important;max-width:none;max-height:28vh}.split-gutter{display:none}}
</style>
)HTML";

static const std::string RESULT_CSS = R"HTML(
<style>
.result-hero{background:linear-gradient(135deg,#0f172a,#1e293b);color:#f8fafc;border-radius:12px;padding:32px;margin:20px 0;box-shadow:0 4px 6px rgba(0,0,0,0.1)}
.result-hero h2{color:#f8fafc;margin:0 0 8px;font-size:24px}.result-hero p{margin:8px 0;color:#cbd5e1}.result-score{font-size:48px;font-weight:600;margin:16px 0;color:#f8fafc}
.metric-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:16px;margin:20px 0}.metric-card{border:1px solid #e2e8f0;border-radius:8px;padding:20px;background:#fff;box-shadow:0 1px 3px rgba(0,0,0,0.05)}.metric-card b{display:block;color:#64748b;font-size:13px;font-weight:500}.metric-card span{display:block;color:#0f172a;font-size:28px;font-weight:600;margin-top:8px}
.score-bar{height:12px;border-radius:6px;background:#e2e8f0;overflow:hidden;margin-top:16px}.score-bar div{height:100%;background:#0f172a;border-radius:6px}.answer-card{border-left:4px solid #cbd5e1;padding:16px;background:#fff;border-radius:0 8px 8px 0;margin:12px 0;box-shadow:0 1px 3px rgba(0,0,0,0.05)}.answer-card.good{border-left-color:#059669}.answer-card.needs-work{border-left-color:#dc2626}.question-meta{color:#64748b;font-size:14px;margin-bottom:8px}
.disq-banner{background:#fef2f2;border:2px solid #ef4444;border-radius:10px;padding:20px;margin:20px 0;color:#991b1b}
.disq-banner h3{margin-top:0;color:#b91c1c}
</style>
)HTML";

// ── JS for the exam page with Full Anti-Cheat & Versioning ─────────────────────
static std::string examScript(int remaining, int currentWarnings, long long attemptId) {
    return R"JS(
<div id="ac-toast"></div>

<!-- Fullscreen Entry Modal -->
<div id="fsModal" class="ac-modal-overlay">
  <div class="ac-modal">
    <div class="ac-modal-header">
      <h2>🔒 Proctored Examination</h2>
    </div>
    <div class="ac-modal-body">
      <p><b>This exam is protected by ExVal Anti-Cheat Security System:</b></p>
      <ul>
        <li><b>Full-Screen Mode:</b> Must remain full screen at all times.</li>
        <li><b>Copy &amp; Paste:</b> Strictly blocked and logged.</li>
        <li><b>Tab &amp; Application Switching:</b> Monitored in real-time.</li>
        <li><b>3-Warning Rule:</b> Leaving the window 3 times will <u>automatically submit your exam</u>!</li>
      </ul>
      <p style="color:#64748b;font-size:13px">By clicking below, full screen will be activated and your session recorded.</p>
    </div>
    <div class="ac-modal-footer">
      <button type="button" class="ac-btn" onclick="requestExamFullscreen()">Enter Full Screen &amp; Begin</button>
    </div>
  </div>
</div>

<!-- Warning / Fullscreen Exit Modal -->
<div id="warnModal" class="ac-modal-overlay" style="display:none">
  <div class="ac-modal">
    <div id="warnModalHeader" class="ac-modal-header warning">
      <h2 id="warnModalTitle">⚠️ Anti-Cheat Warning</h2>
    </div>
    <div class="ac-modal-body">
      <p id="warnModalMsg" style="font-size:16px;font-weight:600;color:#b91c1c"></p>
      <div style="background:#fff1f2;padding:12px;border-radius:8px;margin:16px 0;border:1px solid #fecdd3">
        <span style="font-size:14px;color:#9f1239">Warnings Accrued: </span>
        <b id="warnCountDisplay" style="font-size:20px;color:#be123c">0 / 3</b>
        <p style="margin:6px 0 0;font-size:13px;color:#e11d48">Exceeding 3 warnings results in instant auto-submission!</p>
      </div>
    </div>
    <div class="ac-modal-footer">
      <button type="button" id="warnModalBtn" class="ac-btn" onclick="dismissWarningModal()">Return to Exam</button>
    </div>
  </div>
</div>

<script>
let left = )JS" + std::to_string(remaining) + R"JS(;
let warnings = )JS" + std::to_string(currentWarnings) + R"JS(;
const attemptId = )JS" + std::to_string(attemptId) + R"JS(;
let currentQuestionStarted = Date.now();
const questionTimes = {};
let isFullscreenActive = false;
let autoSubmitted = false;
let examLeaving = false;
let lastBlurTime = 0;
const versionStore = {};
const saveTimers = {};
const autosaveTimers = {};
let versionSelectSilent = false;

function beginExamSubmit() {
  examLeaving = true;
  autoSubmitted = true;
  Object.keys(saveTimers).forEach(k => clearTimeout(saveTimers[k]));
  Object.keys(autosaveTimers).forEach(k => clearTimeout(autosaveTimers[k]));
  document.querySelectorAll('.code-area').forEach(ta => {
    const qid = (ta.id || '').replace('code_', '');
    if (qid) {
      saveAnswer(attemptId, qid, ta.value);
      autosaveCodeVersion(attemptId, qid, true);
    }
  });
}

// Update UI badge
function updateWarningBadge() {
  const el = document.getElementById('acWarningBadge');
  if (el) {
    el.innerHTML = '⚠️ Warnings: ' + warnings + ' / 3';
    if (warnings > 0) el.classList.add('warning-active');
  }
}

// Toast notification
function showToast(msg) {
  const t = document.getElementById('ac-toast');
  if (!t) return;
  t.innerHTML = msg;
  t.style.display = 'flex';
  clearTimeout(t._toastTimer);
  t._toastTimer = setTimeout(() => { t.style.display = 'none'; }, 3200);
}

// ── Anti-Cheat: Fullscreen Enforcement ──────────────────────────────────────
function requestExamFullscreen() {
  const doc = document.documentElement;
  const req = doc.requestFullscreen || doc.webkitRequestFullscreen || doc.mozRequestFullScreen || doc.msRequestFullscreen;
  if (req) {
    req.call(doc).then(() => {
      document.getElementById('fsModal').style.display = 'none';
      isFullscreenActive = true;
    }).catch(err => {
      // In case browser requires manual grant, still hide overlay and allow student
      document.getElementById('fsModal').style.display = 'none';
      isFullscreenActive = true;
    });
  } else {
    document.getElementById('fsModal').style.display = 'none';
    isFullscreenActive = true;
  }
}

document.addEventListener('fullscreenchange', handleFullscreenChange);
document.addEventListener('webkitfullscreenchange', handleFullscreenChange);
document.addEventListener('mozfullscreenchange', handleFullscreenChange);

function handleFullscreenChange() {
  const isFull = !!(document.fullscreenElement || document.webkitFullscreenElement || document.mozFullScreenElement);
  if (!isFull && !autoSubmitted && !examLeaving) {
    isFullscreenActive = false;
    logAnticheatEvent('FULLSCREEN_EXIT', 'Student exited full screen mode');
  } else if (isFull) {
    isFullscreenActive = true;
  }
}

// ── Anti-Cheat: Block Copy, Cut, Paste, Context Menu & Drag ─────────────────
['copy', 'cut', 'paste', 'contextmenu', 'dragstart', 'drop'].forEach(eventType => {
  document.addEventListener(eventType, function(e) {
    e.preventDefault();
    let msg = '⚠️ Copying and pasting is strictly prohibited during the exam.';
    let evCode = 'COPY_BLOCKED';
    if (eventType === 'paste') { msg = '⚠️ Pasting text into the exam is prohibited.'; evCode = 'PASTE_BLOCKED'; }
    else if (eventType === 'contextmenu') { msg = '⚠️ Right-click context menu is disabled.'; evCode = 'RIGHT_CLICK_BLOCKED'; }
    showToast(msg);
    logAnticheatEvent(evCode, 'Student attempted ' + eventType + ' operation');
    return false;
  }, true);
});

// Block suspicious shortcut keys: Ctrl+C, Ctrl+V, Ctrl+X, Ctrl+U, F12, DevTools
document.addEventListener('keydown', function(e) {
  const isCtrlOrCmd = e.ctrlKey || e.metaKey;
  const key = e.key.toLowerCase();
  
  // Inspect / View source / DevTools
  if (e.key === 'F12' || (isCtrlOrCmd && e.shiftKey && (key === 'i' || key === 'j' || key === 'c')) || (isCtrlOrCmd && key === 'u')) {
    e.preventDefault();
    showToast('⚠️ Developer Tools and Page Source are locked.');
    logAnticheatEvent('DEVTOOLS_BLOCKED', 'Key combo ' + (isCtrlOrCmd ? 'Ctrl+' : '') + e.key);
    return false;
  }
  
  // Copy, Cut, Paste combos
  if (isCtrlOrCmd && (key === 'c' || key === 'v' || key === 'x')) {
    e.preventDefault();
    showToast('⚠️ Keyboard clipboard shortcuts (Ctrl/Cmd+' + key.toUpperCase() + ') are blocked.');
    logAnticheatEvent(key === 'v' ? 'PASTE_BLOCKED' : 'COPY_BLOCKED', 'Shortcut ' + (isCtrlOrCmd ? 'Ctrl/Cmd+' : '') + key.toUpperCase());
    return false;
  }
});

// ── Anti-Cheat: Tab Switching & Window Blur Detection (3-Warnings Rule) ──────
function handleTabOrAppSwitch(source) {
  if (autoSubmitted || examLeaving) return;
  const now = Date.now();
  if (now - lastBlurTime < 2000) return;
  lastBlurTime = now;
  logAnticheatEvent('TAB_SWITCH', 'Switched tab or navigated away from window (' + source + ')');
}

window.addEventListener('blur', function() {
  setTimeout(() => {
    if (!document.hasFocus() || document.hidden) {
      handleTabOrAppSwitch('Window lost focus');
    }
  }, 350);
});

document.addEventListener('visibilitychange', function() {
  if (document.hidden) {
    handleTabOrAppSwitch('Tab hidden');
  }
});

// Warn before closing tab
window.addEventListener('beforeunload', function(e) {
  if (autoSubmitted || examLeaving) return;
  e.preventDefault();
  e.returnValue = 'Your exam is currently in progress. Navigating away will record an anti-cheat incident!';
  return e.returnValue;
});

// ── Send Anti-Cheat log to server ───────────────────────────────────────────
async function logAnticheatEvent(eventType, details) {
  if (autoSubmitted || examLeaving) return;
  try {
    const res = await fetch('/log_anticheat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        attempt_id: String(attemptId),
        event_type: eventType,
        details: details
      })
    });
    const data = await res.json();
    if (data.status === 'ok') {
      warnings = data.warning_count;
      updateWarningBadge();
      
      if (data.auto_submit || warnings >= 3) {
        triggerAutoSubmit('Exceeded maximum allowed anti-cheat warnings (3 tab switches / window exits).');
      } else if (eventType === 'TAB_SWITCH' || eventType === 'FULLSCREEN_EXIT') {
        showWarningModal(eventType);
      }
    }
  } catch (err) {
    console.error('Anticheat sync error:', err);
  }
}

function showWarningModal(eventType) {
  const modal = document.getElementById('warnModal');
  const countDisp = document.getElementById('warnCountDisplay');
  const msgDisp = document.getElementById('warnModalMsg');
  
  countDisp.textContent = warnings + ' / 3';
  if (eventType === 'FULLSCREEN_EXIT') {
    msgDisp.textContent = 'Fullscreen Mode Exited! You must remain in full screen for exam integrity.';
  } else {
    msgDisp.textContent = 'Tab Switching or Application Switching Detected!';
  }
  modal.style.display = 'flex';
}

function dismissWarningModal() {
  document.getElementById('warnModal').style.display = 'none';
  // Re-request fullscreen if not active
  if (!document.fullscreenElement) {
    requestExamFullscreen();
  }
}

function triggerAutoSubmit(reason) {
  if (autoSubmitted) return;
  examLeaving = true;
  autoSubmitted = true;
  const modal = document.getElementById('warnModal');
  const header = document.getElementById('warnModalHeader');
  const title = document.getElementById('warnModalTitle');
  const msgDisp = document.getElementById('warnModalMsg');
  const btn = document.getElementById('warnModalBtn');
  
  header.className = 'ac-modal-header danger';
  title.textContent = '🚨 Test Auto-Submitted';
  msgDisp.innerHTML = '<b>Policy Violation:</b> ' + reason + '<br><br>Your exam has been finalized and submitted to your examiner.';
  btn.style.display = 'none';
  modal.style.display = 'flex';
  
  // Submit exam form automatically with anti-cheat flag
  setTimeout(() => {
    const form = document.getElementById('examForm');
    let disqInput = document.getElementById('anticheat_disqualified');
    if (!disqInput) {
      disqInput = document.createElement('input');
      disqInput.type = 'hidden';
      disqInput.name = 'anticheat_disqualified';
      disqInput.id = 'anticheat_disqualified';
      form.appendChild(disqInput);
    }
    disqInput.value = '1';
    form.submit();
  }, 2200);
}

// ── Exam Timer ──────────────────────────────────────────────────────────────
function recordVisibleQuestionTime(){
  const active=document.querySelector('.exam-answer.active') || document.querySelector('.exam-pane.active');
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
  if(left<=0 && !autoSubmitted && !examLeaving){
    beginExamSubmit();
    document.getElementById('examForm').submit();
    return;
  }
  left--;
}
setInterval(tick,1000); tick();
updateWarningBadge();

function showQuestion(index){
  recordVisibleQuestionTime();
  document.querySelectorAll('.exam-problem').forEach((el,i)=>el.classList.toggle('active',i===index));
  document.querySelectorAll('.exam-answer').forEach((el,i)=>el.classList.toggle('active',i===index));
  document.querySelectorAll('.col-nav button').forEach((el,i)=>el.classList.toggle('active',i===index));
}

function setSaveStatus(qid, text) {
  const el = document.getElementById('savestat_' + qid);
  if (el) el.textContent = text;
}

function saveAnswer(a,q,v){
  const key = String(q);
  clearTimeout(saveTimers[key]);
  setSaveStatus(q, 'Saving…');
  saveTimers[key] = setTimeout(() => {
    fetch('/save_answer',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({attempt_id:String(a),question_id:String(q),value:v,time_spent_seconds:String(questionTimes[q]||0)})
    }).then(() => setSaveStatus(q, 'Saved')).catch(() => setSaveStatus(q, 'Save failed'));
  }, 700);
}

function queueAutosaveVersion(aid, qid) {
  const key = String(qid);
  clearTimeout(autosaveTimers[key]);
  autosaveTimers[key] = setTimeout(() => autosaveCodeVersion(aid, qid, false), 2500);
}

// ── CODE VERSIONING SYSTEM ──────────────────────────────────────────────────
async function saveNewCodeVersion(aid, qid) {
  const ta = document.getElementById('code_' + qid);
  if (!ta) return;
  const currentCode = ta.value;
  if (!currentCode.trim()) {
    showToast('⚠️ Please write some code before saving a version.');
    return;
  }
  const labelInput = document.getElementById('ver_label_' + qid);
  const label = labelInput ? labelInput.value.trim() : '';
  if (labelInput) labelInput.value = '';

  try {
    const resp = await fetch('/save_code_version', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        attempt_id: String(aid),
        question_id: String(qid),
        source: currentCode,
        label: label,
        set_selected: '1',
        autosave: '0'
      })
    });
    const d = await resp.json();
    if (d.status === 'ok') {
      await loadCodeVersions(aid, qid, d.version_id);
      setSaveStatus(qid, 'Version saved');
      showToast('Version "' + d.version_label + '" saved');
    } else {
      showToast('⚠️ Error saving version: ' + (d.error || 'Unknown error'));
    }
  } catch (e) {
    showToast('⚠️ Failed to save code version: ' + e.message);
  }
}

async function autosaveCodeVersion(aid, qid, immediate) {
  const ta = document.getElementById('code_' + qid);
  if (!ta || !ta.value.trim()) return;
  try {
    const resp = await fetch('/save_code_version', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      keepalive: !!immediate,
      body: new URLSearchParams({
        attempt_id: String(aid),
        question_id: String(qid),
        source: ta.value,
        label: 'Autosave',
        set_selected: '0',
        autosave: '1'
      })
    });
    const d = await resp.json();
    if (d.status === 'ok') {
      setSaveStatus(qid, 'Autosaved');
      const sel = document.getElementById('ver_select_' + qid);
      let known = false;
      if (sel) {
        for (let i = 0; i < sel.options.length; i++) if (sel.options[i].value == d.version_id) known = true;
      }
      if (!known) await loadCodeVersions(aid, qid, sel ? sel.value : d.version_id);
    }
  } catch (e) {}
}

async function loadCodeVersions(aid, qid, selectVersionId) {
  try {
    const resp = await fetch('/get_code_versions?attempt_id=' + aid + '&question_id=' + qid);
    const versions = await resp.json();
    const select = document.getElementById('ver_select_' + qid);
    if (!select) return;

    versionStore[qid] = {};
    (versions || []).forEach(v => {
      versionStore[qid][String(v.version_id)] = v;
    });

    const keep = selectVersionId ? String(selectVersionId) : select.value;
    versionSelectSilent = true;
    select.innerHTML = '';
    if (!versions || versions.length === 0) {
      select.innerHTML = '<option value="">Working draft</option>';
      versionSelectSilent = false;
      updateVersionBadgeUI(qid);
      return;
    }

    versions.forEach(v => {
      const opt = document.createElement('option');
      opt.value = v.version_id;
      let tag = v.version_label;
      if (v.is_autosave) tag = 'Autosave';
      if (v.is_selected) tag += ' ★';
      opt.textContent = tag;
      if (keep && String(v.version_id) === String(keep)) opt.selected = true;
      else if (!keep && v.is_selected) opt.selected = true;
      select.appendChild(opt);
    });
    if (!select.value && select.options.length) select.options[select.options.length - 1].selected = true;
    versionSelectSilent = false;
    updateVersionBadgeUI(qid);
  } catch (e) {
    versionSelectSilent = false;
  }
}

function onVersionDropdownChange(aid, qid) {
  if (versionSelectSilent) return;
  const select = document.getElementById('ver_select_' + qid);
  const ta = document.getElementById('code_' + qid);
  if (!select || !ta || !select.value) return;
  const v = (versionStore[qid] || {})[String(select.value)];
  if (v && typeof v.source_code === 'string') {
    ta.value = v.source_code;
    saveAnswer(aid, qid, ta.value);
  }
  updateVersionBadgeUI(qid);
}

function updateVersionBadgeUI(qid) {
  const select = document.getElementById('ver_select_' + qid);
  const badge = document.getElementById('ver_badge_' + qid);
  if (!select || !select.selectedOptions.length || !badge) return;
  const opt = select.selectedOptions[0];
  if (opt.dataset && opt.dataset.selected === '1') {
    badge.className = 'version-badge';
    badge.innerHTML = '✓ Selected for Final Submission';
  } else {
    badge.className = 'version-badge not-selected';
    badge.innerHTML = 'Draft (Click "Submit This Version" to choose)';
  }
}

async function selectCurrentVersionForSubmission(aid, qid) {
  const select = document.getElementById('ver_select_' + qid);
  if (!select || !select.value) {
    showToast('⚠️ Please save a version first before designating it for submission.');
    return;
  }
  const vid = select.value;
  try {
    const resp = await fetch('/select_code_version', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        attempt_id: String(aid),
        question_id: String(qid),
        version_id: String(vid)
      })
    });
    const d = await resp.json();
    if (d.status === 'ok') {
      await loadCodeVersions(aid, qid, vid);
      showToast('✓ Version designated as your final submission version!');
    }
  } catch (e) {
    showToast('⚠️ Error setting submission version: ' + e.message);
  }
}

async function deleteSelectedVersion(aid, qid) {
  const select = document.getElementById('ver_select_' + qid);
  if (!select || !select.value) return;
  const vid = select.value;
  try {
    const resp = await fetch('/delete_code_version', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        attempt_id: String(aid),
        question_id: String(qid),
        version_id: String(vid)
      })
    });
    const d = await resp.json();
    if (d.status === 'ok') {
      await loadCodeVersions(aid, qid);
      showToast('🗑 Version deleted.');
    }
  } catch (e) {
    showToast('⚠️ Error deleting version: ' + e.message);
  }
}

// ── Code Runner ─────────────────────────────────────────────────────────────
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
document.getElementById('examForm').addEventListener('submit', function() {
  beginExamSubmit();
  recordVisibleQuestionTime();
});
window.addEventListener('pagehide', function() {
  if (!examLeaving) recordVisibleQuestionTime();
});

const CPP_PAIRS = {'(':')','[':']','{':'}','"':'"',"'":"'"};
const CPP_SUGGESTIONS = [
  {l:'#include <bits/stdc++.h>', i:'#include <bits/stdc++.h>\n', k:'include'},
  {l:'#include <iostream>', i:'#include <iostream>\n', k:'include'},
  {l:'#include <vector>', i:'#include <vector>\n', k:'include'},
  {l:'#include <string>', i:'#include <string>\n', k:'include'},
  {l:'#include <algorithm>', i:'#include <algorithm>\n', k:'include'},
  {l:'#include <map>', i:'#include <map>\n', k:'include'},
  {l:'using namespace std;', i:'using namespace std;\n', k:'using'},
  {l:'int main()', i:'int main() {\n    \n    return 0;\n}', k:'main'},
  {l:'std::cout', i:'std::cout << ', k:'cout'},
  {l:'std::cin', i:'std::cin >> ', k:'cin'},
  {l:'std::endl', i:'std::endl', k:'endl'},
  {l:'std::vector', i:'std::vector<', k:'vector'},
  {l:'std::string', i:'std::string', k:'string'},
  {l:'std::sort', i:'std::sort(', k:'sort'},
  {l:'std::max', i:'std::max(', k:'max'},
  {l:'std::min', i:'std::min(', k:'min'},
  {l:'std::swap', i:'std::swap(', k:'swap'},
  {l:'std::lower_bound', i:'std::lower_bound(', k:'lower_bound'},
  {l:'std::upper_bound', i:'std::upper_bound(', k:'upper_bound'},
  {l:'std::find', i:'std::find(', k:'find'},
  {l:'std::reverse', i:'std::reverse(', k:'reverse'},
  {l:'std::accumulate', i:'std::accumulate(', k:'accumulate'},
  {l:'std::make_pair', i:'std::make_pair(', k:'make_pair'},
  {l:'std::map', i:'std::map<', k:'map'},
  {l:'std::set', i:'std::set<', k:'set'},
  {l:'std::unordered_map', i:'std::unordered_map<', k:'unordered_map'},
  {l:'std::queue', i:'std::queue<', k:'queue'},
  {l:'std::stack', i:'std::stack<', k:'stack'},
  {l:'std::priority_queue', i:'std::priority_queue<', k:'priority_queue'},
  {l:'push_back', i:'push_back(', k:'push_back'},
  {l:'pop_back', i:'pop_back()', k:'pop_back'},
  {l:'emplace_back', i:'emplace_back(', k:'emplace_back'},
  {l:'memset', i:'memset(', k:'memset'},
  {l:'strlen', i:'strlen(', k:'strlen'},
  {l:'sizeof', i:'sizeof(', k:'sizeof'},
  {l:'nullptr', i:'nullptr', k:'nullptr'},
  {l:'for', i:'for (int i = 0; i < n; ++i) {\n    \n}', k:'for'},
  {l:'while', i:'while () {\n    \n}', k:'while'},
  {l:'if', i:'if () {\n    \n}', k:'if'},
  {l:'else', i:'else {\n    \n}', k:'else'},
  {l:'return', i:'return ', k:'return'},
  {l:'long long', i:'long long ', k:'long'}
];

function currentToken(ta) {
  const pos = ta.selectionStart;
  const before = ta.value.slice(0, pos);
  const m = before.match(/[#A-Za-z_:][A-Za-z0-9_:<>]*$/);
  return m ? m[0] : '';
}

function insertAtCursor(ta, text, replaceLen) {
  const start = ta.selectionStart - replaceLen;
  const end = ta.selectionEnd;
  ta.value = ta.value.slice(0, start) + text + ta.value.slice(end);
  const cursor = start + text.length;
  ta.selectionStart = ta.selectionEnd = cursor;
  ta.dispatchEvent(new Event('input'));
}

function caretBox(ta) {
  const mirror = document.createElement('div');
  const cs = getComputedStyle(ta);
  mirror.style.cssText = 'position:absolute;visibility:hidden;white-space:pre-wrap;word-wrap:break-word;overflow:hidden';
  ['fontFamily','fontSize','fontWeight','lineHeight','letterSpacing','padding','border','boxSizing'].forEach(p => mirror.style[p] = cs[p]);
  mirror.style.width = ta.clientWidth + 'px';
  const left = ta.value.slice(0, ta.selectionStart);
  mirror.textContent = left;
  const marker = document.createElement('span');
  marker.textContent = '|';
  mirror.appendChild(marker);
  document.body.appendChild(mirror);
  const tr = ta.getBoundingClientRect();
  const mr = marker.getBoundingClientRect();
  const br = mirror.getBoundingClientRect();
  const x = tr.left + (mr.left - br.left) - ta.scrollLeft;
  const y = tr.top + (mr.top - br.top) - ta.scrollTop + 18;
  document.body.removeChild(mirror);
  return {x, y};
}

function hideSuggest(box) { if (box) box.classList.remove('visible'); }

function showSuggest(ta, box, items, token) {
  if (!items.length) { hideSuggest(box); return; }
  box.innerHTML = '';
  items.forEach((s, i) => {
    const div = document.createElement('div');
    div.className = 'suggest-item' + (i === 0 ? ' active' : '');
    div.innerHTML = escHtml(s.l) + '<small>' + escHtml(s.k) + '</small>';
    div.onmousedown = function(ev) {
      ev.preventDefault();
      insertAtCursor(ta, s.i, token.length);
      hideSuggest(box);
      ta.focus();
    };
    box.appendChild(div);
  });
  const pos = caretBox(ta);
  box.style.left = Math.max(8, pos.x) + 'px';
  box.style.top = Math.max(8, pos.y) + 'px';
  box.style.position = 'fixed';
  box.classList.add('visible');
}

function activeSuggest(box) {
  return box ? box.querySelector('.suggest-item.active') : null;
}

function moveSuggest(box, dir) {
  const items = [...box.querySelectorAll('.suggest-item')];
  if (!items.length) return;
  let i = items.findIndex(el => el.classList.contains('active'));
  items.forEach(el => el.classList.remove('active'));
  i = (i + dir + items.length) % items.length;
  items[i].classList.add('active');
  items[i].scrollIntoView({block:'nearest'});
}

function setupCodeEditor(ta) {
  if (!ta) return;
  const qid = (ta.id || '').replace('code_', '');
  let box = document.getElementById('suggest_' + qid);
  if (!box) {
    box = document.createElement('div');
    box.id = 'suggest_' + qid;
    box.className = 'suggest-box';
    document.body.appendChild(box);
  }
  ta.addEventListener('keydown', function(e) {
    const open = box.classList.contains('visible');
    if (open && (e.key === 'ArrowDown' || e.key === 'ArrowUp')) {
      e.preventDefault();
      moveSuggest(box, e.key === 'ArrowDown' ? 1 : -1);
      return;
    }
    if (open && (e.key === 'Enter' || e.key === 'Tab') && !e.shiftKey) {
      const cur = activeSuggest(box);
      if (cur) { e.preventDefault(); cur.dispatchEvent(new MouseEvent('mousedown', {bubbles:true})); return; }
    }
    if (e.key === 'Escape') { hideSuggest(box); }
    if (e.key === 'Tab' && !open) {
      e.preventDefault();
      const start = this.selectionStart, end = this.selectionEnd;
      this.value = this.value.substring(0, start) + '    ' + this.value.substring(end);
      this.selectionStart = this.selectionEnd = start + 4;
      this.dispatchEvent(new Event('input'));
      return;
    }
    if (e.ctrlKey && e.key === ' ') {
      e.preventDefault();
      const tok = currentToken(this);
      const items = CPP_SUGGESTIONS.filter(s => s.k.toLowerCase().includes(tok.toLowerCase()) || s.l.toLowerCase().includes(tok.toLowerCase())).slice(0, 12);
      showSuggest(this, box, items, tok);
      return;
    }
    if (CPP_PAIRS[e.key] && !e.ctrlKey && !e.metaKey) {
      const close = CPP_PAIRS[e.key];
      const start = this.selectionStart, end = this.selectionEnd;
      if (start !== end) {
        e.preventDefault();
        const sel = this.value.slice(start, end);
        this.value = this.value.slice(0, start) + e.key + sel + close + this.value.slice(end);
        this.selectionStart = start + 1;
        this.selectionEnd = end + 1;
        this.dispatchEvent(new Event('input'));
        return;
      }
      const next = this.value[start];
      if (e.key === '"' || e.key === "'") {
        if (next === e.key) { e.preventDefault(); this.selectionStart = this.selectionEnd = start + 1; return; }
      }
      e.preventDefault();
      this.value = this.value.slice(0, start) + e.key + close + this.value.slice(end);
      this.selectionStart = this.selectionEnd = start + 1;
      this.dispatchEvent(new Event('input'));
      return;
    }
    if ((e.key === ')' || e.key === ']' || e.key === '}' || e.key === '"' || e.key === "'") && this.value[this.selectionStart] === e.key) {
      e.preventDefault();
      this.selectionStart = this.selectionEnd = this.selectionStart + 1;
      return;
    }
    if (e.key === 'Backspace') {
      const start = this.selectionStart;
      const a = this.value[start - 1], b = this.value[start];
      if (CPP_PAIRS[a] && CPP_PAIRS[a] === b) {
        e.preventDefault();
        this.value = this.value.slice(0, start - 1) + this.value.slice(start + 1);
        this.selectionStart = this.selectionEnd = start - 1;
        this.dispatchEvent(new Event('input'));
      }
    }
  });
  ta.addEventListener('input', function() {
    saveAnswer(attemptId, qid, this.value);
    queueAutosaveVersion(attemptId, qid);
    const tok = currentToken(this);
    if (tok.length >= 1) {
      const low = tok.toLowerCase();
      const items = CPP_SUGGESTIONS.filter(s => s.k.toLowerCase().startsWith(low) || s.l.toLowerCase().startsWith(low) || s.k.toLowerCase().includes(low)).slice(0, 10);
      showSuggest(this, box, items, tok);
    } else hideSuggest(box);
  });
  ta.addEventListener('blur', function() { setTimeout(() => hideSuggest(box), 150); });
}

function initSplitters() {
  document.querySelectorAll('.split-gutter').forEach(g => {
    g.addEventListener('mousedown', function(e) {
      e.preventDefault();
      const col = document.getElementById(g.dataset.target);
      if (!col) return;
      g.classList.add('dragging');
      const startX = e.clientX;
      const startW = col.getBoundingClientRect().width;
      function mv(ev) {
        const w = Math.max(120, Math.min(window.innerWidth * 0.7, startW + (ev.clientX - startX)));
        col.style.flex = '0 0 ' + w + 'px';
        col.style.width = w + 'px';
      }
      function up() {
        g.classList.remove('dragging');
        document.removeEventListener('mousemove', mv);
        document.removeEventListener('mouseup', up);
      }
      document.addEventListener('mousemove', mv);
      document.addEventListener('mouseup', up);
    });
  });
}

window.addEventListener('DOMContentLoaded', () => {
  document.querySelectorAll('.coding-editor').forEach(el => {
    const qid = el.dataset.questionId;
    if (qid) loadCodeVersions(attemptId, qid);
  });
  document.querySelectorAll('.code-area').forEach(setupCodeEditor);
  initSplitters();
});
</script>
)JS";
}

bool handleStudent(int fd, Request& r, const Session& s, Database& db) {
    if (s.role != "STUDENT") return false;
    db.closeExpiredExams();

    // ── Dashboard ────────────────────────────────────────────────────────────
    if (r.path == "/student") {
        auto exams = db.rows("SELECT exam_id,title,duration_minutes,allow_multiple_attempts FROM exams WHERE status='PUBLISHED' ORDER BY exam_id DESC");
        std::string b = "<h1>Student Dashboard</h1><p style='color:#64748b;margin-bottom:24px'>Welcome, "+html(s.name)+" | <a href='/analytics'>My Analytics</a> | <a href='/logout'>Logout</a></p>";

        // Exam live search input
        b += "<div style='margin-bottom:24px;display:flex;gap:12px;align-items:center'>"
             "<input type='text' id='examSearchInput' placeholder='🔍 Search available exams by title...' oninput='filterAvailableExams()' "
             "style='flex:1;padding:12px 16px;border:1px solid #cbd5e1;border-radius:8px;font-size:14px;outline:none;background:#fff'>"
             "<button type='button' class='btn' style='background:#f1f5f9;color:#475569;border:1px solid #cbd5e1;padding:12px 18px' onclick='clearExamSearch()'>Clear</button>"
             "</div>";

        b += "<h2 style='margin-bottom:16px'>Available Exams</h2>";
        b += "<div id='noExamsMatch' style='display:none;background:#f8fafc;padding:18px;border-radius:8px;border:1px dashed #cbd5e1;text-align:center;color:#64748b;margin-bottom:16px'>No published exams matched your search query.</div>";

        for (auto& e : exams) {
            std::string eid = e[0];
            std::string title = e[1];
            std::string duration = e[2];

            // Inspect student attempt status for this exam
            auto atts = db.rows("SELECT attempt_id, status, obtained_marks, total_marks FROM exam_attempts WHERE exam_id="
                                + eid + " AND student_id=" + std::to_string(s.userId) + " ORDER BY attempt_id DESC");
            std::string inProgId = "";
            std::string lastSubId = "";
            std::string scoreStr = "";
            for (auto& a : atts) {
                if (a[1] == "IN_PROGRESS" && inProgId.empty()) {
                    inProgId = a[0];
                } else if (a[1] == "SUBMITTED" && lastSubId.empty()) {
                    lastSubId = a[0];
                    scoreStr = a[2] + " / " + a[3];
                }
            }

            std::string btnText = "Start";
            std::string btnStyle = "background:#0f172a;";
            std::string mode = "start";
            std::string badgeHtml = "<span style='background:#f1f5f9;color:#475569;padding:4px 10px;border-radius:12px;font-size:12px;font-weight:600'>Not Started</span>";
            std::string viewResultHtml = "";

            if (!inProgId.empty()) {
                btnText = "Resume";
                btnStyle = "background:#d97706;";
                mode = "resume";
                badgeHtml = "<span style='background:#fef3c7;color:#92400e;padding:4px 10px;border-radius:12px;font-size:12px;font-weight:600'>⏳ In Progress</span>";
            } else if (!lastSubId.empty()) {
                btnText = "Reattempt";
                btnStyle = "background:#2563eb;";
                mode = "reattempt";
                badgeHtml = "<span style='background:#dcfce7;color:#166534;padding:4px 10px;border-radius:12px;font-size:12px;font-weight:600'>✓ Completed (" + scoreStr + ")</span>";
                viewResultHtml = "<a href='/result?attempt_id=" + lastSubId + "' style='margin-right:14px;color:#0f172a;font-size:13px;font-weight:600;text-decoration:none'>View Result</a>";
            }

            b += "<div class='card exam-card-item' data-title='" + html(title) + "' style='display:flex;justify-content:space-between;align-items:center;margin-bottom:12px'>"
                 "<div>"
                 "<div style='display:flex;align-items:center;gap:10px;margin-bottom:6px'>"
                 "<h3 style='margin:0'>" + html(title) + "</h3>"
                 + badgeHtml +
                 "</div>"
                 "<p style='margin:0;color:#64748b;font-size:14px'>Duration: " + duration + " minutes</p>"
                 "</div>"
                 "<div style='display:flex;align-items:center'>"
                 + viewResultHtml +
                 "<form method='post' action='/start' style='margin:0'>"
                 "<input type='hidden' name='exam_id' value='" + eid + "'>"
                 "<input type='hidden' name='mode' value='" + mode + "'>";
            if (!inProgId.empty()) {
                b += "<input type='hidden' name='attempt_id' value='" + inProgId + "'>";
            }
            b += "<button class='btn' style='" + btnStyle + "'>" + btnText + "</button></form>"
                 "</div></div>";
        }
        if (exams.empty()) b += "<p style='color:#64748b'>No published exams.</p>";

        auto previous = db.rows("CALL get_student_results("+std::to_string(s.userId)+")");
        if (previous.empty())
            previous = db.rows("SELECT attempt_id,student_name,exam_name,total_marks,obtained_marks,status,DATE_FORMAT(submitted_at,'%Y-%m-%d %H:%i') FROM student_result_view WHERE student_id="+std::to_string(s.userId)+" ORDER BY attempt_id DESC");
        b += "<h2 style='margin-top:36px;margin-bottom:16px'>Previous Attempts</h2>";
        if (previous.empty()) b += "<p style='color:#64748b'>No submitted exams yet.</p>";
        else {
            b += "<div class='card' style='padding:0;overflow:hidden'><table style='width:100%;border-collapse:collapse'><thead><tr style='background:#f8fafc;border-bottom:1px solid #e2e8f0'><th style='padding:12px 16px;text-align:left;font-size:13px;font-weight:600;color:#475569'>Exam</th><th style='padding:12px 16px;text-align:left;font-size:13px;font-weight:600;color:#475569'>Score</th><th style='padding:12px 16px;text-align:left;font-size:13px;font-weight:600;color:#475569'>Submitted</th><th style='padding:12px 16px;text-align:left;font-size:13px;font-weight:600;color:#475569'>Action</th></tr></thead><tbody>";
            for (auto& row : previous) {
                b += "<tr style='border-bottom:1px solid #e2e8f0'><td style='padding:12px 16px;font-weight:500'>"+html(row[2])+"</td><td style='padding:12px 16px'>"+row[4]+" / "+row[3]+"</td><td style='padding:12px 16px;color:#64748b'>"+(row.size()>6?html(row[6]):std::string(""))+"</td><td style='padding:12px 16px'><a href='/result?attempt_id="+row[0]+"' style='color:#0f172a;font-weight:500'>View Result</a></td></tr>";
            }
            b += "</tbody></table></div>";
        }

        // Search script for student dashboard
        b += R"HTML(
<script>
function filterAvailableExams() {
  const query = (document.getElementById('examSearchInput').value || '').toLowerCase().trim();
  const cards = document.querySelectorAll('.exam-card-item');
  let visibleCount = 0;
  cards.forEach(card => {
    const title = (card.getAttribute('data-title') || '').toLowerCase();
    if (!query || title.includes(query)) {
      card.style.display = 'flex';
      visibleCount++;
    } else {
      card.style.display = 'none';
    }
  });
  const noMatch = document.getElementById('noExamsMatch');
  if (noMatch) {
    noMatch.style.display = (visibleCount === 0 && cards.length > 0) ? 'block' : 'none';
  }
}
function clearExamSearch() {
  const input = document.getElementById('examSearchInput');
  if (input) { input.value = ''; filterAvailableExams(); }
}
</script>
)HTML";

        sendResponse(fd, 200, page("Student Dashboard", b));
        return true;
    }

    // ── Start / Resume / Restart exam ────────────────────────────────────────
    if (r.path == "/start" && r.method == "POST") {
        long long eid = idOf(r.form["exam_id"]);
        std::string mode = r.form.count("mode") ? r.form.at("mode") : "";
        auto exam = db.rows("SELECT allow_multiple_attempts, published_at FROM exams WHERE exam_id="+std::to_string(eid)+" AND status='PUBLISHED'");
        if (exam.empty()) {
            sendResponse(fd, 400, page("Exam unavailable", "<h2>This exam is not currently published.</h2><p><a href='/student'>Back to dashboard</a></p>"));
            return true;
        }

        long long aid = 0;
        if (mode == "resume" || (mode.empty() && r.form.count("attempt_id"))) {
            long long reqAid = idOf(r.form["attempt_id"]);
            if (reqAid > 0) {
                auto check = db.rows("SELECT attempt_id FROM exam_attempts WHERE attempt_id="
                                     +std::to_string(reqAid)+" AND student_id="+std::to_string(s.userId)+" AND status='IN_PROGRESS'");
                if (!check.empty()) aid = reqAid;
            }
            if (aid <= 0) {
                auto active = db.rows("SELECT attempt_id FROM exam_attempts WHERE exam_id="+std::to_string(eid)
                                      +" AND student_id="+std::to_string(s.userId)+" AND status='IN_PROGRESS' ORDER BY attempt_id DESC LIMIT 1");
                if (!active.empty()) aid = idOf(active[0][0]);
            }
        }

        // If mode is "start" or "reattempt" (or no in-progress attempt found to resume):
        if (aid <= 0) {
            db.exec("INSERT INTO exam_attempts(exam_id,student_id) VALUES("+std::to_string(eid)+","+std::to_string(s.userId)+")");
            aid = db.lastId();
            if (aid <= 0) {
                auto newAtt = db.rows("SELECT attempt_id FROM exam_attempts WHERE exam_id="+std::to_string(eid)
                                      +" AND student_id="+std::to_string(s.userId)+" ORDER BY attempt_id DESC LIMIT 1");
                if (!newAtt.empty()) aid = idOf(newAtt[0][0]);
            }
            if (aid > 0) {
                db.exec("INSERT INTO activity_logs(user_id,action,details) VALUES("+std::to_string(s.userId)+",'EXAM_STARTED','Attempt "+std::to_string(aid)+"')");
            }
        }

        if (aid <= 0) {
            sendResponse(fd, 500, page("Error", "<h2>Could not initialize exam attempt.</h2><p><a href='/student'>Back to dashboard</a></p>"));
            return true;
        }
        redirect(fd, "/take?attempt_id="+std::to_string(aid));
        return true;
    }

    // ── Autosave answer (AJAX) ───────────────────────────────────────────────
    if (r.path == "/save_answer" && r.method == "POST") {
        long long aid = idOf(r.form["attempt_id"]);
        long long qid = idOf(r.form["question_id"]);
        auto own = db.rows("SELECT attempt_id FROM exam_attempts WHERE attempt_id="+std::to_string(aid)+" AND student_id="+std::to_string(s.userId)+" AND status='IN_PROGRESS'");
        auto q = db.rows("SELECT question_type FROM questions WHERE question_id="+std::to_string(qid));
        if (own.empty() || q.empty()) { sendResponse(fd, 400, "invalid", "text/plain"); return true; }
        std::string val = r.form.count("value") ? r.form.at("value") : "";
        int spent = std::max(0, (int)idOf(r.form["time_spent_seconds"]));
        bool isMCQ = (q[0][0] == "MCQ");
        std::string mcqVal = (isMCQ && !val.empty()) ? val : "NULL";
        auto old = db.rows("SELECT answer_id FROM student_answers WHERE attempt_id="+std::to_string(aid)+" AND question_id="+std::to_string(qid));
        if (old.empty()) {
            db.exec("INSERT INTO student_answers(attempt_id,question_id,selected_option_id,answer_text,time_spent_seconds) VALUES("
                    +std::to_string(aid)+","+std::to_string(qid)+","+mcqVal+","
                    +(isMCQ?"NULL":"'"+db.escape(val)+"'")+","+std::to_string(spent)+")");
        } else {
            db.exec("UPDATE student_answers SET selected_option_id="+mcqVal
                    +",answer_text="+(isMCQ?"NULL":"'"+db.escape(val)+"'")
                    +",time_spent_seconds=GREATEST(time_spent_seconds,"+std::to_string(spent)+")"
                    +" WHERE answer_id="+old[0][0]);
        }
        sendResponse(fd, 200, "saved", "text/plain");
        return true;
    }

    // ── Save question active time (AJAX) ─────────────────────────────────────
    if (r.path == "/save_question_time" && r.method == "POST") {
        long long aid = idOf(r.form["attempt_id"]);
        long long qid = idOf(r.form["question_id"]);
        int spent = std::max(0, (int)idOf(r.form["time_spent_seconds"]));
        auto own = db.rows("SELECT ea.attempt_id FROM exam_attempts ea JOIN questions q ON q.exam_id=ea.exam_id WHERE ea.attempt_id="+std::to_string(aid)+" AND ea.student_id="+std::to_string(s.userId)+" AND ea.status='IN_PROGRESS' AND q.question_id="+std::to_string(qid));
        if (own.empty()) { sendResponse(fd, 400, "invalid", "text/plain"); return true; }
        db.exec("INSERT INTO student_answers(attempt_id,question_id,time_spent_seconds) VALUES("
                +std::to_string(aid)+","+std::to_string(qid)+","+std::to_string(spent)+") "
                "ON DUPLICATE KEY UPDATE time_spent_seconds=GREATEST(time_spent_seconds,VALUES(time_spent_seconds))");
        sendResponse(fd, 200, "saved", "text/plain");
        return true;
    }

    if (handleAntiCheatRequest(fd, r, s, db)) return true;

    if (handleCodeVersioningRequest(fd, r, s, db)) return true;

    // ── Run & test code (returns JSON) ───────────────────────────────────────
    if (r.path == "/run_code" && r.method == "POST") {
        long long aid = idOf(r.form["attempt_id"]);
        long long qid = idOf(r.form["question_id"]);
        std::string source = r.form.count("source") ? r.form.at("source") : "";

        auto sendJson = [&](int status, const std::string& body) {
            sendResponse(fd, status, body, "application/json");
        };

        auto own = db.rows("SELECT attempt_id FROM exam_attempts WHERE attempt_id="+std::to_string(aid)+" AND student_id="+std::to_string(s.userId)+" AND status='IN_PROGRESS'");
        auto qrow = db.rows("SELECT time_limit_seconds,memory_limit_mb FROM questions WHERE question_id="+std::to_string(qid)+" AND question_type='CODING'");
        if (own.empty() || qrow.empty()) {
            sendJson(400, "{\"verdict\":\"ERROR\",\"error\":\"Invalid attempt or question\",\"passed\":0,\"total\":0,\"time_ms\":0,\"tests\":[],\"compile_error\":\"\",\"runtime_error\":\"\"}");
            return true;
        }
        if (source.empty()) {
            sendJson(400, "{\"verdict\":\"ERROR\",\"error\":\"No code submitted\",\"passed\":0,\"total\":0,\"time_ms\":0,\"tests\":[],\"compile_error\":\"\",\"runtime_error\":\"\"}");
            return true;
        }

        auto tc = db.rows("SELECT test_case_id,input_data,expected_output FROM test_cases WHERE question_id="+std::to_string(qid)+" ORDER BY test_case_id");
        std::vector<std::pair<std::string,std::string>> tests;
        std::vector<std::string> caseIds;
        for (auto& t : tc) { caseIds.push_back(t[0]); tests.push_back({t[1], t[2]}); }

        // Upsert answer row
        auto old = db.rows("SELECT answer_id FROM student_answers WHERE attempt_id="+std::to_string(aid)+" AND question_id="+std::to_string(qid));
        long long answerId = 0;
        if (old.empty()) {
            db.exec("INSERT INTO student_answers(attempt_id,question_id,answer_text,marks_awarded) VALUES("
                    +std::to_string(aid)+","+std::to_string(qid)+",'"+db.escape(source)+"',0)");
            answerId = db.lastId();
        } else {
            answerId = idOf(old[0][0]);
            db.exec("UPDATE student_answers SET answer_text='"+db.escape(source)+"' WHERE answer_id="+std::to_string(answerId));
        }

        int tlSec  = std::max(1, (int)idOf(qrow[0][0]));
        int memMb  = std::max(16, (int)idOf(qrow[0][1]));
        auto job = runner.submit((int)answerId, source, tests, tlSec, memMb);
        std::unique_lock<std::mutex> lk(job->m);
        job->cv.wait(lk, [&]{ return job->done; });

        // Save to DB
        db.exec("INSERT INTO code_submissions(answer_id,source_code,status) VALUES("
                +std::to_string(answerId)+",'"+db.escape(source)+"','"+job->verdict+"')");
        long long sid = db.lastId();
        db.exec("INSERT INTO execution_results(submission_id,verdict,passed_tests,total_tests,execution_time_ms) VALUES("
                +std::to_string(sid)+",'"+job->verdict+"',"
                +std::to_string(job->passed)+","
                +std::to_string((int)tests.size())+","
                +std::to_string(job->timeMs)+")");
        saveCaseResults(db, sid, caseIds, job);

        // Build JSON response with per-test details
        std::string json = "{";
        json += "\"verdict\":\"" + job->verdict + "\",";
        json += "\"passed\":"    + std::to_string(job->passed) + ",";
        json += "\"total\":"     + std::to_string((int)tests.size()) + ",";
        json += "\"time_ms\":"   + std::to_string(job->timeMs) + ",";
        json += "\"compile_error\":\"" + CodeRunner::jsonEscape(job->compileError) + "\",";
        json += "\"runtime_error\":\"" + CodeRunner::jsonEscape(job->runtimeError) + "\",";
        json += "\"error\":\"" + CodeRunner::jsonEscape(job->compileError.empty() ? job->runtimeError : job->compileError) + "\",";
        json += "\"tests\":[";
        for (size_t i = 0; i < job->testResults.size(); ++i) {
            const auto& tr = job->testResults[i];
            if (i) json += ",";
            json += "{";
            json += "\"verdict\":\"" + tr.verdict + "\",";
            json += "\"input\":\""   + CodeRunner::jsonEscape(tests[i].first)  + "\",";
            json += "\"expected\":\"" + CodeRunner::jsonEscape(tr.expected)    + "\",";
            json += "\"actual\":\""   + CodeRunner::jsonEscape(tr.actual)      + "\",";
            json += "\"time_ms\":"    + std::to_string(tr.timeMs);
            json += "}";
        }
        json += "]}";
        sendJson(200, json);
        return true;
    }

    // ── Take exam ────────────────────────────────────────────────────────────
    if (r.path == "/take") {
        long long aid = idOf(r.query["attempt_id"]);
        if (aid <= 0) {
            auto active = db.rows("SELECT attempt_id FROM exam_attempts WHERE student_id="
                                  +std::to_string(s.userId)+" AND status='IN_PROGRESS' ORDER BY attempt_id DESC LIMIT 1");
            if (!active.empty()) aid = idOf(active[0][0]);
            else {
                auto latest = db.rows("SELECT attempt_id FROM exam_attempts WHERE student_id="
                                      +std::to_string(s.userId)+" ORDER BY attempt_id DESC LIMIT 1");
                if (!latest.empty()) {
                    redirect(fd, "/result?attempt_id="+latest[0][0]);
                    return true;
                }
            }
        }
        if (aid <= 0) {
            redirect(fd, "/student");
            return true;
        }

        auto a = db.rows(
            "SELECT ea.exam_id,e.title,e.duration_minutes,ea.status,ea.started_at,"
            "TIMESTAMPDIFF(SECOND,ea.started_at,NOW()) "
            "FROM exam_attempts ea JOIN exams e ON ea.exam_id=e.exam_id "
            "WHERE ea.attempt_id="+std::to_string(aid)+" AND ea.student_id="+std::to_string(s.userId));
        if (a.empty()) {
            auto alt = db.rows("SELECT attempt_id, status FROM exam_attempts WHERE student_id="+std::to_string(s.userId)+" ORDER BY attempt_id DESC LIMIT 1");
            if (!alt.empty()) {
                if (alt[0][1] == "IN_PROGRESS") redirect(fd, "/take?attempt_id="+alt[0][0]);
                else redirect(fd, "/result?attempt_id="+alt[0][0]);
                return true;
            }
            redirect(fd, "/student");
            return true;
        }
        if (a[0][3] == "SUBMITTED") {
            redirect(fd, "/result?attempt_id="+std::to_string(aid));
            return true;
        }

        int durationSec = std::stoi(a[0][2]) * 60;
        int elapsed     = std::stoi(a[0][5]);
        int remaining   = std::max(0, durationSec - elapsed);

        int currentWarnings = 0;
        auto warnRow = db.rows("SELECT COALESCE(anticheat_warnings, 0) FROM exam_attempts WHERE attempt_id="+std::to_string(aid));
        if (!warnRow.empty() && !warnRow[0][0].empty()) currentWarnings = std::stoi(warnRow[0][0]);

        if (remaining <= 0) {
            auto qs = db.rows("SELECT question_id,question_type,marks,time_limit_seconds,memory_limit_mb FROM exam_questions_safe WHERE exam_id="+a[0][0]);
            if (qs.empty()) qs = db.rows("SELECT question_id,question_type,marks,time_limit_seconds,memory_limit_mb FROM questions WHERE exam_id="+a[0][0]);
            int total = 0;
            for (auto& q : qs) total += std::stoi(q[2]);
            int obt = gradeAttempt(db, aid, qs);
            db.begin();
            if (!finalizeAttempt(db, aid, total, obt)) db.rollback();
            else db.commit();
            redirect(fd, "/result?attempt_id="+std::to_string(aid));
            return true;
        }

        auto qs = db.rows("SELECT question_id,question_type,question_text,marks,time_limit_seconds,memory_limit_mb FROM exam_questions_safe WHERE exam_id="+a[0][0]+" ORDER BY question_id");
        if (qs.empty()) qs = db.rows("SELECT question_id,question_type,question_text,marks,time_limit_seconds,memory_limit_mb FROM questions WHERE exam_id="+a[0][0]+" ORDER BY question_id");
        auto savedRows = db.rows("SELECT question_id,COALESCE(selected_option_id,''),COALESCE(answer_text,'') "
                                 "FROM student_answers WHERE attempt_id="+std::to_string(aid));
        std::map<std::string, std::pair<std::string,std::string>> saved;
        for (const auto& answer : savedRows) saved[answer[0]] = {answer[1], answer[2]};

        std::string b = EXAM_CSS;
        b += "<div class='exam-workspace'>";
        b += "<form id='examForm' method='post' action='/submit_exam' style='display:flex;flex-direction:column;height:100%'>"
             "<input id='attempt_id' type='hidden' name='attempt_id' value='"+std::to_string(aid)+"'>"
             "<input id='anticheat_disqualified' type='hidden' name='anticheat_disqualified' value='0'>";

        b += "<div class='exam-telemetry'>"
             "<div class='exam-info'>"
             "<h1>"+html(a[0][1])+"</h1>"
             "<p>Proctored exam</p>"
             "</div>"
             "<div style='display:flex;align-items:center;gap:8px;flex-wrap:wrap'>"
             "<div id='acWarningBadge' class='anticheat-shield"+(currentWarnings>0?" warning-active":"")+"'>"
             "Warnings "+std::to_string(currentWarnings)+" / 3"
             "</div>"
             "<div class='exam-timer'><span id='timer'>--:--</span></div>"
             "<button type='submit' class='btn' style='background:#059669;padding:7px 14px;font-size:12px' "
             "onclick=\"if(!this.dataset.confirmed){this.dataset.confirmed='1';this.innerText='Click again to submit';this.style.background='#dc2626';return false;}\">Submit exam</button>"
             "</div></div>";

        b += "<div class='exam-split'>"
             "<nav class='col-nav' id='colNav'><h3>Questions</h3>";
        for (size_t i = 0; i < qs.size(); ++i) {
            bool answered = saved.count(qs[i][0]) && (!saved[qs[i][0]].first.empty() || !saved[qs[i][0]].second.empty());
            b += "<button type='button' class='"+(i==0?std::string("active "):std::string(""))+(answered?"answered":"")+"' onclick='showQuestion("+std::to_string(i)+")'>"
                 "<span class='q-dot'></span>Q"+std::to_string(i+1)+" · "+html(qs[i][1])+"</button>";
        }
        b += "</nav><div class='split-gutter' data-target='colNav'></div>";

        b += "<div class='col-problem' id='colProblem'>";
        for (size_t i = 0; i < qs.size(); ++i) {
            const auto& q = qs[i];
            std::string tl = (q.size() > 4 && !q[4].empty()) ? q[4] : "2";
            std::string ml = (q.size() > 5 && !q[5].empty()) ? q[5] : "64";
            b += "<div class='exam-pane exam-problem "+(i==0?std::string("active"):std::string(""))+"'>"
                 "<h2>Q"+std::to_string(i+1)+" · "+html(q[3])+" marks</h2>"
                 "<div class='question-text'>"+html(q[2])+"</div>";
            if (q[1] == "CODING") {
                b += "<div class='problem-meta'>"
                     "<p><b>Language</b> C++17</p>"
                     "<p><b>Time</b> "+html(tl)+"s &nbsp; <b>Memory</b> "+html(ml)+" MB</p>"
                     "<p>Code is autosaved. Use Save Version to snapshot, then mark one version for grading.</p>"
                     "</div>";
            }
            b += "</div>";
        }
        b += "</div><div class='split-gutter' data-target='colProblem'></div>";

        b += "<div class='col-answer' id='colAnswer'>";
        for (size_t i = 0; i < qs.size(); ++i) {
            const auto& q = qs[i];
            const std::string& qid = q[0];
            b += "<div class='exam-pane exam-answer "+(i==0?std::string("active"):std::string(""))+"' data-question-id='"+qid+"'>"
                 "<div class='answer-body'>";

            if (q[1] == "CODING") {
                std::string starter = saved[qid].second.empty()
                    ? "#include <bits/stdc++.h>\nusing namespace std;\n\nint main() {\n    \n    return 0;\n}\n"
                    : saved[qid].second;
                b += "<div class='coding-editor' data-question-id='"+qid+"'>"
                     "<div class='version-bar'>"
                     "<div class='version-left'>"
                     "<label>Version</label>"
                     "<select id='ver_select_"+qid+"' class='version-select' onchange='onVersionDropdownChange("+std::to_string(aid)+","+qid+")'>"
                     "<option value=''>Working draft</option>"
                     "</select>"
                     "<span id='ver_badge_"+qid+"' class='version-badge not-selected'>Draft</span>"
                     "</div>"
                     "<div class='version-actions'>"
                     "<input type='text' id='ver_label_"+qid+"' class='version-input' placeholder='Label' maxlength='50'>"
                     "<button type='button' class='btn-ver' onclick='saveNewCodeVersion("+std::to_string(aid)+","+qid+")'>Save</button>"
                     "<button type='button' class='btn-ver btn-ver-submit' onclick='selectCurrentVersionForSubmission("+std::to_string(aid)+","+qid+")'>Use this</button>"
                     "<button type='button' class='btn-ver btn-ver-danger' onclick='deleteSelectedVersion("+std::to_string(aid)+","+qid+")'>Delete</button>"
                     "</div></div>"
                     "<div class='editor-shell'>"
                     "<textarea id='code_"+qid+"' name='q_"+qid+"' class='code-area' spellcheck='false'>"
                     +html(starter)+"</textarea>"
                     "</div>"
                     "<div class='ide-footer'>"
                     "<button type='button' id='runbtn_"+qid+"' class='run-btn' onclick=\"runCode("+std::to_string(aid)+","+qid+")\">Run</button>"
                     "<span id='savestat_"+qid+"' class='save-status'>Saved</span>"
                     "</div>"
                     "<div id='result_"+qid+"'></div>"
                     "</div>";
            } else if (q[1] == "MCQ") {
                auto opts = db.rows("SELECT option_id,option_text FROM mcq_options WHERE question_id="+qid+" ORDER BY option_id");
                b += "<div class='mcq-options'>";
                for (auto& o : opts) {
                    bool checked = saved[qid].first == o[0];
                    b += "<label class='mcq-option'><input type='radio' name='q_"+qid+"' value='"+o[0]+"'"
                         +(checked?" checked":"")
                         +" onchange=\"saveAnswer("+std::to_string(aid)+","+qid+",this.value)\">"
                         "<span>"+html(o[1])+"</span></label>";
                }
                b += "</div>";
            } else {
                b += "<textarea name='q_"+qid+"' class='desc-area' "
                     "placeholder='Write your answer…' oninput=\"saveAnswer("+std::to_string(aid)+","+qid+",this.value)\">"
                     +html(saved[qid].second)+"</textarea>";
            }

            b += "</div><div class='question-actions'><div>";
            if (i > 0)
                b += "<button type='button' class='btn' style='background:#f1f5f9;color:#475569;border:1px solid #cbd5e1' onclick='showQuestion("+std::to_string(i-1)+")'>Previous</button>";
            if (i + 1 < qs.size())
                b += "<button type='button' class='btn' onclick='showQuestion("+std::to_string(i+1)+")'>Next</button>";
            b += "</div></div></div>";
        }

        b += "</div></div></form></div>";
        b += examScript(remaining, currentWarnings, aid);
        sendResponse(fd, 200, page("Proctored Exam", b));
        return true;
    }

    // ── Final exam submission (POST) ─────────────────────────────────────────
    if (r.path == "/submit_exam" && r.method == "POST") {
        long long aid = idOf(r.form["attempt_id"]);
        bool isDisqualified = (r.form.count("anticheat_disqualified") && r.form.at("anticheat_disqualified") == "1");

        auto att = db.rows("SELECT exam_id,status FROM exam_attempts WHERE attempt_id="
                           +std::to_string(aid)+" AND student_id="+std::to_string(s.userId));
        if (att.empty()) { redirect(fd, "/student"); return true; }
        if (att[0][1] == "SUBMITTED") { redirect(fd, "/result?attempt_id="+std::to_string(aid)); return true; }

        int curWarnings = 0;
        auto wRow = db.rows("SELECT COALESCE(anticheat_warnings,0) FROM exam_attempts WHERE attempt_id="+std::to_string(aid));
        if (!wRow.empty() && !wRow[0][0].empty()) curWarnings = std::stoi(wRow[0][0]);
        if (curWarnings >= 3) {
            isDisqualified = true;
        }

        auto qs = db.rows("SELECT question_id,question_type,marks,time_limit_seconds,memory_limit_mb FROM exam_questions_safe WHERE exam_id="+att[0][0]);
        if (qs.empty()) qs = db.rows("SELECT question_id,question_type,marks,time_limit_seconds,memory_limit_mb FROM questions WHERE exam_id="+att[0][0]);

        // Save answers submitted in the form
        for (auto& q : qs) {
            std::string key = "q_" + q[0];
            if (r.form.count(key)) {
                // If question is CODING, check if a selected version exists first
                if (q[1] == "CODING") {
                    auto sel = db.rows("SELECT source_code FROM code_versions WHERE attempt_id="
                                       +std::to_string(aid)+" AND question_id="+q[0]+" AND is_selected=1 LIMIT 1");
                    if (!sel.empty() && !sel[0][0].empty()) {
                        upsertAnswer(db, aid, q[0], false, sel[0][0], true);
                        continue;
                    }
                }
                upsertAnswer(db, aid, q[0], q[1]=="MCQ", r.form.at(key), true);
            }
        }

        int total = 0;
        for (auto& q : qs) total += std::stoi(q[2]);
        int obt = gradeAttempt(db, aid, qs);

        std::string disqReason = isDisqualified ? "Auto-submitted: Exceeded 3 anti-cheat warnings (tab switching/window exits)" : "";

        db.begin();
        if (!finalizeAttempt(db, aid, total, obt, isDisqualified, disqReason)) {
            db.rollback();
        } else {
            db.commit();
            if (isDisqualified) {
                recordAntiCheatAutoSubmission(db, aid, s.userId, curWarnings, disqReason);
            } else {
                db.exec("INSERT INTO activity_logs(user_id, action, details) VALUES("
                        +std::to_string(s.userId)+",'EXAM_SUBMITTED','Attempt "+std::to_string(aid)+"')");
            }
        }
        redirect(fd, "/result?attempt_id="+std::to_string(aid));
        return true;
    }

    // ── Personal analytics ───────────────────────────────────────────────────
    if (r.path == "/analytics") {
        auto stats = db.rows("SELECT COUNT(*),COALESCE(ROUND(AVG(100.0*obtained_marks/NULLIF(total_marks,0)),1),0),COALESCE(SUM(obtained_marks),0),COALESCE(SUM(total_marks),0) "
                             "FROM exam_attempts WHERE student_id="+std::to_string(s.userId)+" AND status='SUBMITTED'");
        auto history = db.rows("SELECT ea.attempt_id,e.title,ea.obtained_marks,ea.total_marks,DATE_FORMAT(ea.submitted_at,'%Y-%m-%d %H:%i') "
                               "FROM exam_attempts ea JOIN exams e ON e.exam_id=ea.exam_id "
                               "WHERE ea.student_id="+std::to_string(s.userId)+" AND ea.status='SUBMITTED' ORDER BY ea.submitted_at DESC");
        std::string b = "<h1>My Analytics</h1><p><a href='/student'>Dashboard</a> | <a href='/logout'>Logout</a></p>";
        if (!stats.empty())
            b += "<div class='card'><h3>Performance Summary</h3><p>Exams completed: "+stats[0][0]+"</p><p>Average score: "+stats[0][1]+"%</p><p>Total marks: "+stats[0][2]+" / "+stats[0][3]+"</p></div>";
        b += "<h2>Exam History</h2>";
        if (history.empty()) b += "<p>You haven't completed any exams yet.</p>";
        else {
            b += "<div class='card'><table><tr><th>Exam</th><th>Score</th><th>Submitted</th><th></th></tr>";
            for (auto& row : history)
                b += "<tr><td>"+html(row[1])+"</td><td>"+row[2]+" / "+row[3]+"</td><td>"+html(row[4])+"</td><td><a href='/result?attempt_id="+row[0]+"'>View Result</a></td></tr>";
            b += "</table></div>";
        }
        sendResponse(fd, 200, page("My Analytics", b));
        return true;
    }

    // ── Result page ──────────────────────────────────────────────────────────
    if (r.path == "/result") {
        long long aid = idOf(r.query["attempt_id"]);
        if (aid <= 0) {
            auto latest = db.rows("SELECT attempt_id FROM exam_attempts WHERE student_id="
                                  +std::to_string(s.userId)+" ORDER BY attempt_id DESC LIMIT 1");
            if (!latest.empty()) aid = idOf(latest[0][0]);
        }
        if (aid <= 0) {
            redirect(fd, "/student");
            return true;
        }

        auto a = db.rows(
            "SELECT e.title, ea.total_marks, ea.obtained_marks, ea.status, "
            "COALESCE((SELECT SUM(time_spent_seconds) FROM student_answers WHERE attempt_id=ea.attempt_id),0) "
            "FROM exam_attempts ea JOIN exams e ON ea.exam_id=e.exam_id "
            "WHERE ea.attempt_id="+std::to_string(aid)+" AND ea.student_id="+std::to_string(s.userId));
        if (a.empty()) {
            redirect(fd, "/student");
            return true;
        }
        if (a[0][3] == "IN_PROGRESS") {
            redirect(fd, "/take?attempt_id="+std::to_string(aid));
            return true;
        }

        auto acRow = db.rows("SELECT COALESCE(anticheat_warnings,0), COALESCE(is_disqualified,0), COALESCE(disqualification_reason,'') "
                             "FROM exam_attempts WHERE attempt_id="+std::to_string(aid));
        int warnings = (!acRow.empty() && !acRow[0][0].empty()) ? std::stoi(acRow[0][0]) : 0;
        bool isDisqualified = (!acRow.empty() && acRow[0][1] == "1");
        std::string disqReason = (!acRow.empty()) ? acRow[0][2] : "";

        auto ans = db.rows(
            "SELECT q.question_text,q.question_type,sa.marks_awarded,q.marks,"
            "sa.time_spent_seconds,COALESCE(latest.verdict,''),"
            "COALESCE(latest.passed_tests,0),COALESCE(latest.total_tests,0),"
            "COALESCE(sa.answer_text,''),COALESCE(mo.option_text,''),COALESCE(sa.feedback,''),"
            "COALESCE(cv.version_label, ''), q.question_id "
            "FROM student_answers sa "
            "JOIN questions q ON sa.question_id=q.question_id "
            "LEFT JOIN mcq_options mo ON mo.option_id=sa.selected_option_id "
            "LEFT JOIN code_versions cv ON cv.version_id=sa.selected_version_id "
            "LEFT JOIN ("
            "  SELECT cs.answer_id, er.verdict,er.passed_tests,er.total_tests "
            "  FROM code_submissions cs "
            "  JOIN execution_results er ON er.submission_id=cs.submission_id "
            "  WHERE cs.submission_id=("
            "    SELECT MAX(cs2.submission_id) FROM code_submissions cs2 WHERE cs2.answer_id=cs.answer_id"
            "  )"
            ") latest ON latest.answer_id=sa.answer_id "
            "WHERE sa.attempt_id="+std::to_string(aid));

        int pct = (a[0][1] != "0") ? (std::stoi(a[0][2]) * 100 / std::stoi(a[0][1])) : 0;

        std::string b = RESULT_CSS + "<h1>Exam Result</h1>";
        b += "<p><a href='/student'>Dashboard</a> | <a href='/analytics'>My Analytics</a> | <a href='/logout'>Logout</a></p>";

        if (isDisqualified) {
            b += "<div class='disq-banner'>"
                 "<h3>🚨 Proctored Submission Notice (Anti-Cheat Violation)</h3>"
                 "<p><b>Status:</b> Automatically Submitted by Anti-Cheat Proctor</p>"
                 "<p><b>Violation:</b> " + (disqReason.empty() ? std::string("Exceeded maximum tab switches (3 warnings)") : html(disqReason)) + "</p>"
                 "<p style='margin-bottom:0;font-size:13px'>All logged anti-cheat events and warning timestamps have been forwarded to the exam administration.</p>"
                 "</div>";
        }

        b += "<div class='result-hero'>";
        b += "<h2>"+html(a[0][0])+"</h2><p>Final score</p>";
        b += "<div class='result-score'>"+a[0][2]+" / "+a[0][1]+" · "+std::to_string(pct)+"%</div>";
        b += "<div class='score-bar'><div style='width:"+std::to_string(std::max(0,std::min(100,pct)))+"%'></div></div>";
        b += "</div>";

        int answered=0, fullMarks=0;
        for (const auto& x : ans) {
            bool attempted = x[2] != "0" || x[4] != "0" || !x[8].empty() || !x[9].empty();
            if (attempted) answered++;
            if (x[2] == x[3] && x[3] != "0") fullMarks++;
        }

        b += "<div class='metric-grid'>"
             "<div class='metric-card'><b>Questions attempted</b><span>"+std::to_string(answered)+" / "+std::to_string(ans.size())+"</span></div>"
             "<div class='metric-card'><b>Full-score answers</b><span>"+std::to_string(fullMarks)+"</span></div>"
             "<div class='metric-card'><b>Active question time</b><span>"+(a[0][4]=="0"?std::string("Not tracked"):a[0][4]+" sec")+"</span></div>"
             "<div class='metric-card'><b>Anti-Cheat Warnings</b><span style='color:"+(warnings>=3?"#dc2626":(warnings>0?"#d97706":"#059669"))+"'>"+std::to_string(warnings)+" / 3</span></div>"
             "</div>";

        b += "<h2>Answer Summary</h2>";
        for (const auto& x : ans) {
            std::string verdBadge = "";
            if (x[1] == "CODING" && !x[5].empty())
                verdBadge = " &nbsp;<span style='background:#e9f0fb;padding:2px 8px;border-radius:10px;font-size:13px'>"+html(x[5])+"</span>";
            bool good = x[2] == x[3] && x[3] != "0";
            b += "<div class='card answer-card "+std::string(good?"good":"needs-work")+"'>";
            b += "<b>"+html(x[1])+"</b>"+verdBadge;
            b += "<p>"+html(x[0])+"</p>";
            if (x[1] == "MCQ")
                b += "<p><b>Your answer:</b> "+(x[9].empty()?std::string("<i>Not answered</i>"):html(x[9]))+"</p>";
            else if (x[1] == "DESCRIPTIVE")
                b += "<p><b>Your answer:</b></p><p>"+(x[8].empty()?std::string("<i>Not answered</i>"):html(x[8]))+"</p>";
            else if (!x[8].empty()) {
                std::string verLabel = x.size() > 11 && !x[11].empty() ? x[11] : "Working Draft";
                b += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:6px'>"
                     "<span style='font-size:13px;font-weight:600;color:#0f172a'>Submitted Solution:</span>"
                     "<span style='background:#0f172a;color:#fff;font-size:11px;font-weight:bold;padding:3px 8px;border-radius:10px'>✓ " + html(verLabel) + "</span>"
                     "</div>";
                b += "<pre style='background:#1e1e1e;color:#d4d4d4;padding:12px;border-radius:6px;overflow-x:auto'>"+html(x[8])+"</pre>";
            }
            if (!x[10].empty()) b += "<p class='question-meta'>Feedback: "+html(x[10])+"</p>";
            b += "<p class='question-meta'>Marks: <b>"+x[2]+"</b> / "+x[3]+" &nbsp;|&nbsp; Active time: "+(x[4]=="0"?std::string("Not tracked"):x[4]+" sec");
            if (x[1] == "CODING") b += " &nbsp;|&nbsp; Tests: "+x[6]+" / "+x[7]+" passed";
            b += "</p>";
            b += "</div>";
        }
        sendResponse(fd, 200, page("Result", b));
        return true;
    }

    return false;
}
