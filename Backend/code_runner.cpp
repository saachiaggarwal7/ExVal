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
