#include "auth.h"
#include <random>
#include <sstream>
#include <mutex>
#include <iostream>
#include <string>
using namespace std;

string token()
{
    random_device rd;
    ostringstream out;
    static mt19937_64 generate_token{rd()}; 
    out << hex << generate_token() << generate_token();
    return out.str();
}

bool authenticate(const Request&req,Session&out) {
    auto it=req.headers.find("Cookie");
    if(it==req.headers.end()){
         return false;
    }
    string cookie=it->second;
    size_t pos=cookie.find("EXVAL_SESSION=");
    if(pos==string::npos)
    {
        return false;
    }
    string token=cookie.substr(pos+14);
    size_t semicolon=token.find(';');
    if(semicolon!=string::npos) 
    {
        token=token.substr(0,semicolon);
    }
    lock_guard<mutex> g(sessionMutex);
    auto s=sessions.find(token);
    if(s==sessions.end())
    {
        return false;
    }
    out=s->second;
    return true;
}

bool handleAuth(int fd,const Request&req,Database&db) {
    if(req.path!="/" && req.path!="/login" && req.path!="/logout") 
        return false;

    if(req.path=="/logout") {
        auto it=req.headers.find("Cookie");
        if(it!=req.headers.end()){
            string cookie=it->second;
            size_t pos=cookie.find("EXVAL_SESSION=");
            if(pos!=string::npos)
            {
                string token=cookie.substr(pos+14);
                size_t semicolon=token.find(';');
                if(semicolon!=string::npos) 
                {
                    token=token.substr(0,semicolon);
                }
                lock_guard<mutex> g(sessionMutex);
                sessions.erase(token);
            }
        }
        string msg="HTTP/1.1 302 Found\r\nLocation: /login\r\nSet-Cookie: EXVAL_SESSION=deleted; Path=/; Max-Age=0\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(fd,msg.c_str(),msg.size(),0);
        return true;
    }

    if (req.method == "POST")
    {
        string email = "";
        string pass = "";
        if (req.form.count("email"))
            email = db.escape(req.form.at("email"));
        if (req.form.count("password"))
            pass = db.escape(req.form.at("password"));
        string query =
        "SELECT u.user_id, u.name, r.role_name "
        "FROM users u "
        "JOIN roles r ON u.role_id = r.role_id "
        "WHERE u.email = '" + email +
        "' AND u.password_hash = '" + pass +
        "' AND u.is_active = 1 LIMIT 1";
       auto rows = db.rows(query);
        if (rows.empty())
        {
            sendResponse(fd, 401,page("Login","<h2>Invalid login</h2><a href='/login'>Try again</a>"));
            return true;
        }
        string tok = token();
        int userId = (int)idOf(rows[0][0]);
        string name = rows[0][1];
        string role = rows[0][2];
        Session s{userId, role, name};
        {
            lock_guard<mutex> g(sessionMutex);
            sessions[tok] = s;
        }
        string logQuery ="INSERT INTO activity_logs(user_id, action, details) VALUES (" +to_string(userId) +", 'LOGIN', 'User logged in')";
        db.exec(logQuery);
        string destination;
        if (role == "STUDENT")
            destination = "student";
        else if (role == "EXAMINER")
            destination = "examiner";
        else
            destination = "admin";
        string msg =
        "HTTP/1.1 302 Found\r\nLocation: /" + destination + "\r\nSet-Cookie: EXVAL_SESSION=" + tok +"; Path=/; HttpOnly\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(fd, msg.c_str(), msg.size(), 0);
        return true;
    }
    sendResponse(fd,200,page("Login","<h1>ExVal Login</h1><p class='muted'>Student, Examiner or Admin</p><form method='post' action='/login'><label>Email</label><input name='email' required><label>Password</label><input name='password' type='password' required><button class='btn'>Login</button></form><p class='small'>Sample accounts are in database/seed.sql.</p>"));
    return true;
}
