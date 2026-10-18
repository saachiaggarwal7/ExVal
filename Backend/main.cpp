#include "common.h"
#include "database.h"
#include "auth.h"
#include "student.h"
#include "examiner.h"
#include "admin.h"
#include <iostream>
#include <arpa/inet.h>
#include <cstdlib>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace std;
static string databaseErrorPage(const Database& db) {
    string detail = db.lastError.empty() ? "Unknown database error" : html(db.lastError);
    return page("Database connection", "<h2>Cannot connect to the database</h2>"
        "<p>The server is running, but it cannot reach MySQL/MariaDB.</p>"
        "<p class='card'><b>Server message:</b><br><code>"+detail+"</code></p>"
        "<p>Start MySQL, import the files in <code>database/</code>, then start the app with the correct "
        "<code>DB_HOST</code>, <code>DB_USER</code>, <code>DB_PASSWORD</code>, and <code>DB_NAME</code> settings.</p>");
}

void handleRequest(int fd) {
    Request r=readRequest(fd);
    if(r.path.empty()) {
        sendResponse(fd,400,"Bad request","text/plain");
        return;
    }

    // Login/logout are kept in auth.cpp.
    if(r.path=="/" || r.path=="/login" || r.path=="/logout") {
        Database db;
        if(!db.connect()) { sendResponse(fd,500,databaseErrorPage(db)); return; }
        handleAuth(fd,r,db);
        return;
    }

    Session s;
    if(!authenticate(r,s)) { redirect(fd,"/login"); return; }

    Database db;
    if(!db.connect()) { sendResponse(fd,500,databaseErrorPage(db)); return; }

    // Each teammate's module gets its own routes.
    if(handleStudent(fd,r,s,db)) return;
    if(handleExaminer(fd,r,s,db)) return;
    if(handleAdmin(fd,r,s,db)) return;
    sendResponse(fd,404,page("Not Found","<h2>Page not found</h2>"));
}

int main() {
    int port=8080;
    const char* pe=getenv("PORT");
    if(pe) port=std::atoi(pe);
    cout<<"ExVal C++ server starting on port "<<port<<"\n";

    int server=socket(AF_INET,SOCK_STREAM,0);
    int opt=1;
    setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons(port);
    if(bind(server,(sockaddr*)&addr,sizeof(addr))<0) { perror("bind"); return 1; }
    if(listen(server,20)<0) { perror("listen"); return 1; }

    while(true) {
        int fd=accept(server,nullptr,nullptr);
        if(fd<0) continue;
        thread([fd]{handleRequest(fd); close(fd);}).detach();
    }
    close(server);
}
