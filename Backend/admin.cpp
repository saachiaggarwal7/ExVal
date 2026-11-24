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
        b += "<div class='admin-wrap'><h1>Admin Dashboard</h1>"
             "<p>Welcome, "+html(s.name)+" | <a href='/logout'>Logout</a></p>";

        // Stats row
        b += "<div class='stat-grid'>";
        b += "<div class='stat-box'><div class='num'>"+(uCount.empty()?"0":uCount[0][0])+"</div><div class='lbl'>Users</div></div>";
        b += "<div class='stat-box'><div class='num'>"+(eCount.empty()?"0":eCount[0][0])+"</div><div class='lbl'>Exams</div></div>";
        b += "<div class='stat-box'><div class='num'>"+(atCount.empty()?"0":atCount[0][0])+"</div><div class='lbl'>Attempts</div></div>";
        b += "<div class='stat-box'><div class='num'>"+(subCount.empty()?"0":subCount[0][0])+"</div><div class='lbl'>Code Submissions</div></div>";
        b += "</div>";

        auto people = db.rows(
            "SELECT u.user_id,u.name,u.email,r.role_name,u.is_active FROM users u "
            "JOIN roles r ON r.role_id=u.role_id ORDER BY r.role_name,u.name");
        b += "<div class='admin-card'><h2>Manage Users &amp; Roles</h2>"
             "<form method='post' action='/admin_create_user' style='display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;align-items:end'>"
             "<label>Name<input name='name' required></label>"
             "<label>Email<input name='email' type='email' required></label>"
             "<label>Password<input name='password' required></label>"
             "<label>Role<select name='role'><option>STUDENT</option><option>EXAMINER</option><option>ADMIN</option></select></label>"
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

        // System-wide cohort analytics. Teachers see the equivalent data for
        // their own exam on /exam_results; admin can see the whole platform.
        auto score = db.rows("SELECT COUNT(*),COALESCE(ROUND(AVG(100.0*obtained_marks/NULLIF(total_marks,0)),1),0),(SELECT COALESCE(ROUND(AVG(NULLIF(sa.time_spent_seconds,0))/60,1),0) FROM student_answers sa JOIN exam_attempts ea2 ON ea2.attempt_id=sa.attempt_id WHERE ea2.status='SUBMITTED'),(SELECT COUNT(*) FROM student_answers sa JOIN exam_attempts ea2 ON ea2.attempt_id=sa.attempt_id WHERE ea2.status='SUBMITTED' AND sa.time_spent_seconds>0) FROM exam_attempts WHERE status='SUBMITTED'");
        auto difficult = db.rows(
            "SELECT e.title,q.question_type,q.question_text,COALESCE(ROUND(100.0*AVG(sa.marks_awarded/NULLIF(q.marks,0)),1),0),COALESCE(ROUND(AVG(sa.time_spent_seconds)),0) "
            "FROM student_answers sa JOIN questions q ON q.question_id=sa.question_id JOIN exams e ON e.exam_id=q.exam_id JOIN exam_attempts ea ON ea.attempt_id=sa.attempt_id "
            "WHERE ea.status='SUBMITTED' GROUP BY q.question_id,e.title,q.question_type,q.question_text HAVING COUNT(sa.answer_id)>0 ORDER BY AVG(sa.marks_awarded/NULLIF(q.marks,0)) ASC LIMIT 8");
        b += "<div class='admin-card'><h2>Platform analytics</h2>";
        if (!score.empty() && score[0][0] != "0") {
            b += "<p><b>Submitted attempts:</b> "+score[0][0]+" &nbsp; <b>Average score:</b> "+score[0][1]+"% &nbsp; <b>Average active question time:</b> "+(score[0][3]=="0"?std::string("Not tracked yet"):score[0][2]+" min")+"</p>";
            b += "<h3>Hardest questions by observed performance</h3><table class='db-table'><tr><th>Exam</th><th>Type</th><th>Question</th><th>Accuracy</th><th>Avg time</th></tr>";
            for (const auto& row : difficult)
                b += "<tr><td>"+html(row[0])+"</td><td>"+html(row[1])+"</td><td>"+html(row[2].substr(0,90))+(row[2].size()>90?"…":"")+"</td><td>"+row[3]+"%</td><td>"+row[4]+" sec</td></tr>";
            b += "</table>";
        } else b += "<p>No submitted attempts yet.</p>";
        b += "</div>";

        // Tab bar for tables
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
            {"activity_logs",    "Activity Logs"},
        };

        b += "<div class='admin-card'><h2>Database Tables</h2><div class='tab-bar'>";
        for (size_t i = 0; i < tables.size(); ++i)
            b += std::string("<button class='tab-btn") + (i==0?" active":"") + "' onclick='showTab(\""+tables[i].first+"\")' id='btn_"+tables[i].first+"'>"+tables[i].second+"</button>";
        b += "</div>";

        for (size_t i = 0; i < tables.size(); ++i) {
            const std::string& tname = tables[i].first;
            b += std::string("<div class='tab-content") + (i==0?" active":"") + "' id='tab_"+tname+"'>";

            auto cols = db.rows("SHOW COLUMNS FROM " + tname);
            auto data = db.rows("SELECT * FROM " + tname + " ORDER BY 1 DESC LIMIT 100");

            if (data.empty()) {
                b += "<p style='color:#888'>No records.</p>";
            } else {
                b += "<p style='font-size:13px;color:#666'>Showing "+std::to_string(data.size())+" rows (max 100)</p>";
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
        b += "</div>"; // admin-card

        // Recent activity
        b += "<div class='admin-card'><h2>Recent Activity (last 30)</h2>";
        auto logs = db.rows(
            "SELECT al.created_at,u.name,al.action,al.details "
            "FROM activity_logs al LEFT JOIN users u ON al.user_id=u.user_id "
            "ORDER BY al.log_id DESC LIMIT 30");
        if (logs.empty()) {
            b += "<p>No activity yet.</p>";
        } else {
            b += "<table class='db-table'><tr><th>Time</th><th>User</th><th>Action</th><th>Details</th></tr>";
            for (auto& l : logs)
                b += "<tr><td>"+html(l[0])+"</td><td>"+html(l[1])+"</td><td>"+html(l[2])+"</td><td>"+html(l[3])+"</td></tr>";
            b += "</table>";
        }
        b += "</div>";

        b += R"HTML(<script>
function showTab(name) {
    var contents = document.getElementsByClassName("tab-content");
    var buttons = document.getElementsByClassName("tab-btn");
    var i;

    for (i = 0; i < contents.length; i++) {
        contents[i].classList.remove("active");
    }

    for (i = 0; i < buttons.length; i++) {
        buttons[i].classList.remove("active");
    }

    var selectedTab = document.getElementById("tab_" + name);
    var selectedButton = document.getElementById("btn_" + name);

    if (selectedTab != null) {
        selectedTab.classList.add("active");
    }

    if (selectedButton != null) {
        selectedButton.classList.add("active");
    }
}
</script>)HTML";

        b += "</div>"; // admin-wrap
        sendResponse(fd, 200, page("Admin", b));
        return true;
    }

    return false;
}
