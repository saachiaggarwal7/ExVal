#ifndef DATABASE_H
#define DATABASE_H
#include <mysql/mysql.h>
#include <string>
#include <vector>
class Database {
public:
    MYSQL *connection;
    std::string lastError;
    Database();
    ~Database();
    bool connect();
    bool exec(const std::string& sql);
    std::vector<std::vector<std::string>> rows(const std::string& sql);
    std::string escape(const std::string& text);
    bool begin();
    bool commit();
    bool rollback();
    long long lastId();
    void closeExpiredExams();
};
#endif
