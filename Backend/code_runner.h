#ifndef CODE_RUNNER_H
#define CODE_RUNNER_H
#include <condition_variable>
#include <memory>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class CodeRunner {
public:
    struct TestResult {
        std::string verdict;  
        std::string actual;    
        std::string expected;  
        int timeMs = 0;
    };

    struct Job {
        int id;
        std::string source;
        std::vector<std::pair<std::string,std::string>> tests;
        int timeLimitMs;
        int memoryMb;
        // Overall verdict
        std::string verdict = "WAITING";
        int passed = 0;
        int timeMs = 0;
        std::string compileError;   
        std::string runtimeError;   
        std::vector<TestResult> testResults;
        bool done = false;
        std::mutex m;
        std::condition_variable cv;
    };

    CodeRunner();
    ~CodeRunner();

    std::shared_ptr<Job> submit(int id,
        const std::string& source,
        const std::vector<std::pair<std::string,std::string>>& tests,
        int timeLimitSec, int memoryMb);
    static std::string jsonEscape(const std::string& s);

private:
    std::queue<std::shared_ptr<Job>> q;
    std::mutex qm;
    std::condition_variable qcv;
    std::vector<std::thread> workers;
    bool stop = false;
    std::atomic<int> nextJob{1};

    static bool compileProgram(const std::string& dir);
    static bool runProgram(const std::string& dir, const std::string& input,
                           int limitMs, int memoryMb,
                           int& elapsedMs, bool& timedOut);
    static std::string trim(const std::string& s);
    void worker();
};

extern CodeRunner runner;
#endif
