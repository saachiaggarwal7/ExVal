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
