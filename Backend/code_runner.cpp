#include "code_runner.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

CodeRunner::CodeRunner() {
    for (int i = 0; i < 3; i++)
        workers.emplace_back(&CodeRunner::worker, this);
}

CodeRunner::~CodeRunner() {
    {
        std::lock_guard<std::mutex> lock(qm);
        stop = true;
    }

    qcv.notify_all();

    for (auto& t : workers)
        t.join();
}

std::shared_ptr<CodeRunner::Job> CodeRunner::submit(
    int id, const std::string& source,
    const std::vector<std::pair<std::string, std::string>>& tests,
    int timeLimitSec, int memoryMb)
{
    auto j = std::make_shared<Job>();

    j->id = id * 100000 + nextJob.fetch_add(1);
    j->source = source;
    j->tests = tests;
    j->timeLimitMs = std::max(1, timeLimitSec) * 1000;
    j->memoryMb = std::max(16, memoryMb);

    {
        std::lock_guard<std::mutex> lock(qm);
        q.push(j);
    }

    qcv.notify_one();
    return j;
}

bool CodeRunner::compileProgram(const std::string& dir) {
    pid_t p = fork();

    if (p == 0) {
        chdir(dir.c_str());

        setenv("PATH",
            "/usr/bin:/usr/local/bin:/opt/homebrew/bin:/opt/homebrew/opt/llvm/bin",
            1);

        int err = open("compile_error.txt",
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(err, STDERR_FILENO);
        close(err);

        int out = open("/dev/null", O_WRONLY);
        dup2(out, STDOUT_FILENO);
        close(out);

        execlp("g++", "g++", "-std=c++17", "solution.cpp",
               "-O2", "-o", "program", (char*)nullptr);

        dprintf(STDERR_FILENO, "Could not start g++: %s\n",
                strerror(errno));
        _exit(127);
    }

    if (p < 0)
        return false;

    int status;
    waitpid(p, &status, 0);

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool CodeRunner::runProgram(
    const std::string& dir, const std::string& input,
    int limitMs, int memoryMb, int& elapsedMs, bool& timedOut)
{
    timedOut = false;

    std::ofstream(dir + "/input.txt") << input;

    pid_t p = fork();

    if (p == 0) {
        chdir(dir.c_str());

        int in = open("input.txt", O_RDONLY);
        int out = open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int err = open("runtime_error.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

        dup2(in, STDIN_FILENO);
        dup2(out, STDOUT_FILENO);
        dup2(err, STDERR_FILENO);

        close(in);
        close(out);
        close(err);

        struct rlimit limit;
        limit.rlim_cur = limit.rlim_max =
            (rlim_t)memoryMb * 1024 * 1024;

#ifdef __APPLE__
        setrlimit(RLIMIT_DATA, &limit);
#else
        setrlimit(RLIMIT_AS, &limit);
#endif

        execl("./program", "./program", (char*)nullptr);
        _exit(127);
    }

    if (p < 0)
        return false;

    auto start = std::chrono::steady_clock::now();
    int status = 0;

    while (true) {
        pid_t result = waitpid(p, &status, WNOHANG);

        if (result == p)
            break;

        if (result < 0)
            return false;

        auto now = std::chrono::steady_clock::now();

        elapsedMs = (int)std::chrono::duration_cast<
            std::chrono::milliseconds>(now - start).count();

        if (elapsedMs > limitMs) {
            timedOut = true;
            kill(p, SIGKILL);
            waitpid(p, &status, 0);
            return false;
        }

        usleep(10000);
    }

    elapsedMs = (int)std::chrono::duration_cast<
        std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string CodeRunner::trim(const std::string& s) {
    size_t start = 0, end = s.size();

    while (start < end && std::isspace((unsigned char)s[start]))
        start++;

    while (end > start && std::isspace((unsigned char)s[end - 1]))
        end--;

    return s.substr(start, end - start);
}

std::string CodeRunner::jsonEscape(const std::string& s) {
    std::string result;

    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;
        }
    }

    return result;
}

void CodeRunner::worker() {
    while (true) {
        std::shared_ptr<Job> j;

        {
            std::unique_lock<std::mutex> lock(qm);

            qcv.wait(lock, [&] {
                return stop || !q.empty();
            });

            if (stop && q.empty())
                return;

            j = q.front();
            q.pop();
        }

        fs::path base = fs::current_path();
        fs::path submissions = fs::exists(base / "submissions")
            ? base / "submissions"
            : base.parent_path() / "submissions";

        fs::path jobDir = submissions / ("job_" + std::to_string(j->id));
        std::string dir;

        try {
            fs::create_directories(jobDir);
            dir = fs::absolute(jobDir).string();
        } catch (const std::exception& e) {
            j->verdict = "COMPILATION_ERROR";
            j->compileError = std::string("Server filesystem error: ")
                + e.what()
                + "\nPlease ensure the submissions/ directory is writable.";

            {
                std::lock_guard<std::mutex> lock(j->m);
                j->done = true;
            }

            j->cv.notify_one();
            continue;
        }

        std::ofstream(dir + "/solution.cpp") << j->source;

        if (!compileProgram(dir)) {
            j->verdict = "COMPILATION_ERROR";

            std::ifstream errorFile(dir + "/compile_error.txt");
            j->compileError.assign(
                std::istreambuf_iterator<char>(errorFile), {});
        } else {
            bool allPassed = true;

            for (const auto& test : j->tests) {
                int ms = 0;
                bool timedOut = false;
                TestResult result;

                bool ok = runProgram(
                    dir, test.first, j->timeLimitMs,
                    j->memoryMb, ms, timedOut);

                result.timeMs = ms;
                j->timeMs = std::max(j->timeMs, ms);

                if (timedOut) {
                    result.verdict = "TIME_LIMIT_EXCEEDED";
                    j->verdict = result.verdict;
                    j->testResults.push_back(result);
                    allPassed = false;
                    break;
                }

                if (!ok) {
                    result.verdict = "RUNTIME_ERROR";

                    std::ifstream errorFile(dir + "/runtime_error.txt");
                    j->runtimeError.assign(
                        std::istreambuf_iterator<char>(errorFile), {});

                    j->verdict = result.verdict;
                    j->testResults.push_back(result);
                    allPassed = false;
                    break;
                }

                std::ifstream outputFile(dir + "/output.txt");
                std::string actual(
                    std::istreambuf_iterator<char>(outputFile), {});

                result.actual = trim(actual);
                result.expected = trim(test.second);

                if (result.actual == result.expected) {
                    result.verdict = "PASSED";
                    j->passed++;
                } else {
                    result.verdict = "WRONG_ANSWER";
                    j->verdict = result.verdict;
                    allPassed = false;
                }

                j->testResults.push_back(result);
            }

            if (j->tests.empty())
                j->verdict = "NO_TEST_CASES";
            else if (allPassed)
                j->verdict = "ACCEPTED";
        }

        {
            std::lock_guard<std::mutex> lock(j->m);
            j->done = true;
        }

        j->cv.notify_one();
    }
}

CodeRunner runner;