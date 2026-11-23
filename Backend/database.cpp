#include "database.h"
#include <cstdlib>
#include <iostream>

static void drainResults(MYSQL* connection) {
    while (mysql_next_result(connection) == 0) {
        MYSQL_RES* extra = mysql_store_result(connection);
        if (extra) mysql_free_result(extra);
    }
}

Database::Database(){connection=mysql_init(nullptr);}
Database::~Database(){if(connection)mysql_close(connection);}
bool Database::connect(){
    const char*h=getenv("DB_HOST"); const char*u=getenv("DB_USER");
    const char*p=getenv("DB_PASSWORD"); const char*d=getenv("DB_NAME");
    if (!connection) { lastError="Could not initialise the MySQL client."; return false; }
    if(!mysql_real_connect(connection,h?h:"localhost",u?u:"exval_user",p?p:"exval123",d?d:"exval",0,nullptr,CLIENT_MULTI_RESULTS)){
        lastError=mysql_error(connection); std::cerr<<"MySQL connection: "<<lastError<<"\n"; return false;
    }
    return true;
}
bool Database::exec(const std::string&sql){
    if(mysql_query(connection,sql.c_str())!=0){std::cerr<<"MySQL: "<<mysql_error(connection)<<"\n";return false;}
    MYSQL_RES*r=mysql_store_result(connection);if(r)mysql_free_result(r);
    drainResults(connection);
    return true;
}
std::vector<std::vector<std::string>> Database::rows(const std::string&sql){
    std::vector<std::vector<std::string>>out;
    if(mysql_query(connection,sql.c_str())!=0){std::cerr<<"MySQL: "<<mysql_error(connection)<<"\n";return out;}
    MYSQL_RES*r=mysql_store_result(connection);
    if(r){
        MYSQL_ROW row;unsigned n=mysql_num_fields(r);
        while((row=mysql_fetch_row(r))){std::vector<std::string>v;for(unsigned i=0;i<n;i++)v.push_back(row[i]?row[i]:"");out.push_back(v);}
        mysql_free_result(r);
    }
    drainResults(connection);
    return out;
}
std::string Database::escape(const std::string&t){std::string o(t.size()*2+1,'\0');unsigned long n=mysql_real_escape_string(connection,o.data(),t.c_str(),(unsigned long)t.size());o.resize(n);return o;}
bool Database::begin(){return exec("START TRANSACTION");}
bool Database::commit(){return exec("COMMIT");}
bool Database::rollback(){return exec("ROLLBACK");}
long long Database::lastId(){return (long long)mysql_insert_id(connection);}
void Database::closeExpiredExams(){
    exec("UPDATE exams SET status='CLOSED' WHERE status='PUBLISHED' "
         "AND published_at IS NOT NULL "
         "AND DATE_ADD(published_at, INTERVAL duration_minutes MINUTE) <= NOW()");
}
