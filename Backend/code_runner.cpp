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