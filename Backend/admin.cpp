#include "admin.h"
#include <string>
#include <utility>
#include <vector>

static std::string adminStyles() {
    return R"HTML(
<style>
.admin-wrap { max-width: 1400px; margin: 0 auto; padding: 0 16px; }
.admin-header { margin-bottom: 24px; }
.admin-header h1 { margin: 0 0 8px; font-size: 24px; font-weight: 600; color: #0f172a; }
.admin-header p { margin: 0; color: #64748b; font-size: 14px; }
.action-bar { display: flex; gap: 8px; margin-bottom: 24px; flex-wrap: wrap; }
.action-bar button { padding: 8px 16px; border: 1px solid #cbd5e1; border-radius: 6px; background: #fff; color: #475569; font-size: 13px; font-weight: 500; cursor: pointer; transition: all 0.2s; }
.action-bar button:hover { background: #f8fafc; border-color: #94a3b8; }
.action-bar button.primary { background: #0f172a; color: #f8fafc; border-color: #0f172a; }
.stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 16px; margin-bottom: 24px; }
.stat-card { background: #fff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 20px; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }
.stat-card.alert-card { border-left: 4px solid #dc2626; }
.stat-label { color: #64748b; font-size: 13px; font-weight: 500; margin-bottom: 8px; }
.stat-value { color: #0f172a; font-size: 28px; font-weight: 600; margin-bottom: 4px; }
.stat-meta { color: #94a3b8; font-size: 12px; }
.topology-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
.topology-card { background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; padding: 16px; }
.topology-card h3 { margin: 0 0 12px; font-size: 14px; font-weight: 600; color: #0f172a; }
.topology-item { display: flex; justify-content: space-between; margin-bottom: 8px; font-size: 13px; }
.topology-item:last-child { margin-bottom: 0; }
.item-label { color: #64748b; }
.item-value { color: #0f172a; font-weight: 500; font-family: monospace; }
.admin-card { background: white; border: 1px solid #e2e8f0; border-radius: 8px; padding: 24px; margin-bottom: 24px; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }
.admin-card h2 { margin: 0 0 16px; font-size: 18px; font-weight: 600; color: #0f172a; }
.db-table { width: 100%; border-collapse: collapse; margin-top: 10px; }
.db-table th { background: #f8fafc; color: #475569; padding: 12px 16px; text-align: left; font-size: 13px; font-weight: 600; border-bottom: 1px solid #e2e8f0; }
.db-table td { padding: 12px 16px; border-bottom: 1px solid #e2e8f0; font-size: 14px; }
.tab-bar { margin-bottom: 16px; display: flex; gap: 4px; flex-wrap: wrap; }
.tab-btn { padding: 8px 16px; border: 1px solid #cbd5e1; border-radius: 6px; background: #fff; color: #475569; font-size: 13px; font-weight: 500; cursor: pointer; transition: all 0.2s; }
.tab-btn:hover { background: #f8fafc; }
.tab-btn.active { background: #0f172a; color: #f8fafc; border-color: #0f172a; }
.tab-content { display: none; }
.tab-content.active { display: block; }
.badge-clean{background:#ecfdf5;color:#059669;padding:3px 8px;border-radius:12px;font-size:12px;font-weight:600}
.badge-warn{background:#fffbeb;color:#d97706;padding:3px 8px;border-radius:12px;font-size:12px;font-weight:600}
.badge-disq{background:#fef2f2;color:#dc2626;padding:3px 8px;border-radius:12px;font-size:12px;font-weight:600}
</style>
)HTML";
}

bool handleAdmin(int fd, Request& r, const Session& s, Database& db) {
    if (s.role != "ADMIN") return false;

    if (r.path == "/admin_create_user" && r.method == "POST") {
        std::string name = r.form.count("name") ? r.form.at("name") : "";
        std::string email = r.form.count("email") ? r.form.at("email") : "";
        std::string pass = r.form.count("password") ? r.form.at("password") : "";
        std::string role = r.form.count("role") ? r.form.at("role") : "";
        if (name.empty() || email.empty() || pass.empty() || (role != "STUDENT" && role != "EXAMINER" && role != "ADMIN")) {
            sendResponse(fd, 400, page("Invalid user", "<h2>Name, email, password and a valid role are required.</h2><p><a href='/admin'>Back</a></p>"));
            return true;
        }
        if (!db.exec("CALL create_platform_user('"+db.escape(name)+"','"+db.escape(email)+"','"+db.escape(pass)+"','"+db.escape(role)+"')")) {
            sendResponse(fd, 400, page("Could not create user", "<h2>Could not create the user.</h2><p>The email may already exist, or the stored procedure is missing. Import database/procedures.sql and try again.</p><p><a href='/admin'>Back</a></p>"));
            return true;
        }
        db.exec("INSERT INTO activity_logs(user_id,action,details) VALUES("+std::to_string(s.userId)+",'USER_CREATED','Created "+db.escape(email)+" as "+db.escape(role)+"')");
        redirect(fd, "/admin");
        return true;
    }

    if (r.path == "/admin_toggle_user" && r.method == "POST") {
        long long uid = idOf(r.form["user_id"]);
        if (uid == s.userId) { redirect(fd, "/admin"); return true; }
        db.exec("UPDATE users SET is_active = IF(is_active=1,0,1) WHERE user_id="+std::to_string(uid));
        db.exec("INSERT INTO activity_logs(user_id,action,details) VALUES("+std::to_string(s.userId)+",'USER_TOGGLED','Toggled user "+std::to_string(uid)+"')");
        redirect(fd, "/admin");
        return true;
    }
    if (r.path == "/admin_table") {
        std::string tbl = r.query.count("name") ? r.query.at("name") : "";
        // Whitelist of allowed tables
        static const std::vector<std::string> allowed = {
            "users","roles","exams","questions","mcq_options","test_cases",
            "exam_attempts","student_answers","code_submissions","execution_results","code_test_case_results","activity_logs",
            "code_test_case_results","activity_logs","code_versions","anticheat_logs"
        };
        bool ok = false;
        for (auto& t : allowed) if (t == tbl) { ok = true; break; }
        if (!ok) { redirect(fd, "/admin"); return true; }

        // Get column names
        auto cols = db.rows("SHOW COLUMNS FROM " + tbl);
        auto data = db.rows("SELECT * FROM " + tbl + " ORDER BY 1 DESC LIMIT 200");

        std::string b = adminStyles();
        b += "<div class='admin-wrap'><h1>Table: "+html(tbl)+"</h1>"
             "<p><a href='/admin'>← Admin Dashboard</a> &nbsp;|&nbsp; "
             +std::to_string(data.size())+" rows (max 200)</p>"
             "<div style='overflow-x:auto'><table class='db-table'><tr>";
        for (auto& c : cols) b += "<th>"+html(c[0])+"</th>";
        b += "</tr>";
        for (auto& row : data) {
            b += "<tr>";
            for (auto& cell : row)
                b += "<td title='"+html(cell)+"'>"+html(cell.size()>80 ? cell.substr(0,80)+"…" : cell)+"</td>";
            b += "</tr>";
        }
        b += "</table></div></div>";
        sendResponse(fd, 200, page("Table: "+tbl, b));
        return true;
    }
    if (r.path == "/admin") {
        // Stats
        auto uCount  = db.rows("SELECT COUNT(*) FROM users");
        auto eCount  = db.rows("SELECT COUNT(*) FROM exams");
        auto atCount = db.rows("SELECT COUNT(*) FROM exam_attempts");
        auto subCount= db.rows("SELECT COUNT(*) FROM code_submissions");
        auto acCount  = db.rows("SELECT COUNT(*) FROM anticheat_logs");
        auto disqCount= db.rows("SELECT COUNT(*) FROM exam_attempts WHERE is_disqualified = 1");

        std::string b = adminStyles();
        b += "<div class='admin-wrap'>"
             "<div class='admin-header'>"
             "<h1>Admin Dashboard &amp; Proctor Center</h1>"
             "<p>System Architecture, User Roles &amp; Platform-Wide Anti-Cheat Monitoring</p>"
             "<p style='color:#64748b;margin-top:8px'>Welcome, "+html(s.name)+" | <a href='/logout'>Logout</a></p>"
             "</div>";

        // Stats row
        b += "<div class='stat-grid'>";
        b += "<div class='stat-card'>"
             "<div class='stat-label'>Active Concurrency</div>"
             "<div class='stat-value'>"+(atCount.empty()?"0":atCount[0][0])+"</div>"
             "<div class='stat-meta'>Exam sessions total</div>"
             "</div>";
        b += "<div class='stat-card'>"
             "<div class='stat-label'>Total Users</div>"
             "<div class='stat-value'>"+(uCount.empty()?"0":uCount[0][0])+"</div>"
             "<div class='stat-meta'>Students, Examiners &amp; Admins</div>"
             "</div>";
        b += "<div class='stat-card'>"
             "<div class='stat-label'>Published Exams</div>"
             "<div class='stat-value'>"+(eCount.empty()?"0":eCount[0][0])+"</div>"
             "<div class='stat-meta'>Active assessments</div>"
             "</div>";
        b += "<div class='stat-card'>"
             "<div class='stat-label'>Code Submissions</div>"
             "<div class='stat-value'>"+(subCount.empty()?"0":subCount[0][0])+"</div>"
             "<div class='stat-meta'>Sandboxed executions</div>"
             "</div>";
        b += "<div class='stat-card alert-card'>"
             "<div class='stat-label'>Anti-Cheat Incidents</div>"
             "<div class='stat-value' style='color:#dc2626'>"+(acCount.empty()?"0":acCount[0][0])+"</div>"
             "<div class='stat-meta'>"+(disqCount.empty()?"0":disqCount[0][0])+" auto-submitted for cheating</div>"
             "</div>";
        b += "</div>";

        // Anti-Cheat Proctoring Security Center (High-Risk Attempts)
        auto flaggedAttempts = db.rows(
            "SELECT ea.attempt_id, u.name, u.email, e.title, ea.anticheat_warnings, "
            "ea.is_disqualified, COALESCE(ea.disqualification_reason, ''), "
            "DATE_FORMAT(ea.started_at, '%Y-%m-%d %H:%i') "
            "FROM exam_attempts ea "
            "JOIN users u ON ea.student_id = u.user_id "
            "JOIN exams e ON ea.exam_id = e.exam_id "
            "WHERE ea.anticheat_warnings > 0 OR ea.is_disqualified = 1 "
            "ORDER BY ea.is_disqualified DESC, ea.anticheat_warnings DESC LIMIT 20");

        b += "<div class='admin-card' style='border-left:5px solid #dc2626'>"
             "<h2 style='color:#991b1b;display:flex;align-items:center;gap:8px'>🛡 Anti-Cheat Proctoring &amp; High-Risk Activity</h2>"
             "<p style='color:#64748b;font-size:14px;margin-bottom:16px'>Real-time tracking of students with warnings (tab switches, fullscreen exits, copy/paste attempts) or automatic disqualification.</p>";
        if (flaggedAttempts.empty()) {
            b += "<p style='color:#059669;font-weight:500'>✓ All clear. No students have exceeded warning thresholds or triggered cheating flags.</p>";
        } else {
            b += "<div style='overflow-x:auto'><table class='db-table'><tr>"
                 "<th>Student</th><th>Email</th><th>Exam</th><th>Warnings</th><th>Status</th><th>Violation Detail</th><th>Started</th></tr>";
            for (auto& row : flaggedAttempts) {
                int warns = std::stoi(row[4]);
                bool isDisq = (row[5] == "1");
                std::string stBadge = "<span class='badge-clean'>Active</span>";
                if (isDisq || warns >= 3) stBadge = "<span class='badge-disq'>🚨 Auto-Submitted (3 Warnings)</span>";
                else if (warns > 0) stBadge = "<span class='badge-warn'>⚠️ " + std::to_string(warns) + " / 3 Warnings</span>";

                b += "<tr>"
                     "<td><b>"+html(row[1])+"</b></td>"
                     "<td>"+html(row[2])+"</td>"
                     "<td>"+html(row[3])+"</td>"
                     "<td><b>"+std::to_string(warns)+" / 3</b></td>"
                     "<td>"+stBadge+"</td>"
                     "<td>"+(row[6].empty()?std::string("Tab/window switching detected"):html(row[6]))+"</td>"
                     "<td>"+html(row[7])+"</td>"
                     "</tr>";
            }
            b += "</table></div>";
        }
        b += "</div>";

        // System topology grid
        b += "<div class='admin-card'>"
             "<h2>System Architecture &amp; Sandboxing Engine</h2>"
             "<div class='topology-grid'>"
             "<div class='topology-card'>"
             "<h3>Queue Dispatcher</h3>"
             "<div class='topology-item'><span class='item-label'>Algorithm Policy</span><span class='item-value'>FCFS Priority</span></div>"
             "<div class='topology-item'><span class='item-label'>Concurrency Primitive</span><span class='item-value'>std::mutex / std::condition_variable</span></div>"
             "<div class='topology-item'><span class='item-label'>Current Status</span><span class='item-value' style='color:#059669'>Active</span></div>"
             "</div>"
             "<div class='topology-card'>"
             "<h3>Thread Worker Pool</h3>"
             "<div class='topology-item'><span class='item-label'>Worker Threads</span><span class='item-value'>N=3 POSIX Threads</span></div>"
             "<div class='topology-item'><span class='item-label'>Compilation Sandbox</span><span class='item-value'>fork() / exec() / waitpid()</span></div>"
             "<div class='topology-item'><span class='item-label'>Resource Limits</span><span class='item-value'>setrlimit(RLIMIT_DATA/AS)</span></div>"
             "</div>"
             "<div class='topology-card'>"
             "<h3>Database &amp; Integrity Cluster</h3>"
             "<div class='topology-item'><span class='item-label'>Engine</span><span class='item-value'>MySQL InnoDB</span></div>"
             "<div class='topology-item'><span class='item-label'>Isolation Level</span><span class='item-value'>REPEATABLE READ</span></div>"
             "<div class='topology-item'><span class='item-label'>Code Versioning</span><span class='item-value' style='color:#059669'>Multi-Version Snapshots</span></div>"
             "</div>"
             "</div>"
             "</div>";

        // User management
        auto people = db.rows(
            "SELECT u.user_id,u.name,u.email,r.role_name,u.is_active FROM users u "
            "JOIN roles r ON r.role_id=u.role_id ORDER BY r.role_name,u.name");
        b += "<div class='admin-card'><h2>Manage Users &amp; Roles</h2>"
             "<form method='post' action='/admin_create_user' style='display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;align-items:end;margin-bottom:20px'>"
             "<label style='display:block'><span style='display:block;margin-bottom:6px;color:#475569;font-weight:500;font-size:14px'>Name</span><input name='name' required style='width:100%;box-sizing:border-box;padding:10px 12px;border:1px solid #cbd5e1;border-radius:6px;font-size:14px;background:#fff'></label>"
             "<label style='display:block'><span style='display:block;margin-bottom:6px;color:#475569;font-weight:500;font-size:14px'>Email</span><input name='email' type='email' required style='width:100%;box-sizing:border-box;padding:10px 12px;border:1px solid #cbd5e1;border-radius:6px;font-size:14px;background:#fff'></label>"
             "<label style='display:block'><span style='display:block;margin-bottom:6px;color:#475569;font-weight:500;font-size:14px'>Password</span><input name='password' required style='width:100%;box-sizing:border-box;padding:10px 12px;border:1px solid #cbd5e1;border-radius:6px;font-size:14px;background:#fff'></label>"
             "<label style='display:block'><span style='display:block;margin-bottom:6px;color:#475569;font-weight:500;font-size:14px'>Role</span><select name='role' style='width:100%;box-sizing:border-box;padding:10px 12px;border:1px solid #cbd5e1;border-radius:6px;font-size:14px;background:#fff'><option>STUDENT</option><option>EXAMINER</option><option>ADMIN</option></select></label>"
             "<button class='btn'>Create user</button></form>"
             "<table class='db-table'><tr><th>Name</th><th>Email</th><th>Role</th><th>Active</th><th></th></tr>";
        for (const auto& u : people) {
            b += "<tr><td>"+html(u[1])+"</td><td>"+html(u[2])+"</td><td>"+html(u[3])+"</td><td>"+(u[4]=="1"?"Yes":"No")+"</td><td>";
            if (u[0] != std::to_string(s.userId))
                b += "<form method='post' action='/admin_toggle_user' style='margin:0'><input type='hidden' name='user_id' value='"+u[0]+"'>"
                     "<button class='tab-btn' type='submit'>"+(u[4]=="1"?std::string("Deactivate"):std::string("Activate"))+"</button></form>";
            b += "</td></tr>";
        }
        b += "</table></div>";

        // Tab bar for tables (including code_versions and anticheat_logs)
        static const std::vector<std::pair<std::string,std::string>> tables = {
            {"users",            "Users"},
            {"roles",            "Roles"},
            {"exams",            "Exams"},
            {"questions",        "Questions"},
            {"mcq_options",      "MCQ Options"},
            {"test_cases",       "Test Cases"},
            {"exam_attempts",    "Attempts"},
            {"student_answers",  "Answers"},
            {"code_submissions", "Submissions"},
            {"execution_results","Exec Results"},
            {"code_test_case_results","Test Case Results"},
            {"code_versions",    "Code Versions"},
            {"anticheat_logs",   "Anti-Cheat Logs"},
            {"activity_logs",    "Activity Logs"},
        };

        b += "<div class='admin-card'><h2>Database Tables Browser</h2><div class='tab-bar'>";
        for (size_t i = 0; i < tables.size(); ++i)
            b += std::string("<button class='tab-btn") + (i==0?" active":"") + "' onclick='showTab(\""+tables[i].first+"\")' id='btn_"+tables[i].first+"'>"+tables[i].second+"</button>";
        b += "</div>";

        for (size_t i = 0; i < tables.size(); ++i) {
            const std::string& tname = tables[i].first;
            b += std::string("<div class='tab-content") + (i==0?" active":"") + "' id='tab_"+tname+"'>";

            auto cols = db.rows("SHOW COLUMNS FROM " + tname);
            auto data = db.rows("SELECT * FROM " + tname + " ORDER BY 1 DESC LIMIT 100");

            if (data.empty()) {
                b += "<p style='color:#64748b'>No records.</p>";
            } else {
                b += "<p style='font-size:13px;color:#64748b'>Showing "+std::to_string(data.size())+" rows (max 100)</p>";
                b += "<div style='overflow-x:auto'><table class='db-table'><tr>";
                for (auto& c : cols) b += "<th>"+html(c[0])+"</th>";
                b += "</tr>";
                for (auto& row : data) {
                    b += "<tr>";
                    for (auto& cell : row)
                        b += "<td title='"+html(cell)+"'>"+html(cell.size()>60 ? cell.substr(0,60)+"…" : cell)+"</td>";
                    b += "</tr>";
                }
                b += "</table></div>";
            }
            b += "</div>";
        }
        b += "</div>";

        // Recent activity
        b += "<div class='admin-card'><h2>Recent Activity (last 30)</h2>";
        auto logs = db.rows(
            "SELECT al.created_at,u.name,al.action,al.details "
            "FROM activity_logs al LEFT JOIN users u ON al.user_id=u.user_id "
            "ORDER BY al.log_id DESC LIMIT 30");
        if (logs.empty()) {
            b += "<p style='color:#64748b'>No activity yet.</p>";
        } else {
            b += "<table class='db-table'><tr><th>Time</th><th>User</th><th>Action</th><th>Details</th></tr>";
            for (auto& l : logs)
                b += "<tr><td>"+html(l[0])+"</td><td>"+html(l[1])+"</td><td>"+html(l[2])+"</td><td>"+html(l[3])+"</td></tr>";
            b += "</table>";
        }
        b += "</div>";

        b += R"HTML(<script>
function showTab(name){
  document.querySelectorAll('.tab-content').forEach(el=>el.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(el=>el.classList.remove('active'));
  const t=document.getElementById('tab_'+name);
  const b=document.getElementById('btn_'+name);
  if(t) t.classList.add('active');
  if(b) b.classList.add('active');
}
</script>)HTML";

        b += "</div>"; // admin-wrap
        sendResponse(fd, 200, page("Admin Dashboard", b));
        return true;
    }

    return false;
}


        
