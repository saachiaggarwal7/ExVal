#include "../code_runner.h"
#include <iostream>
#include <cassert>

int main() {
    CodeRunner runner;

    std::string source = R"(
        #include <iostream>
        using namespace std;

        int main() {
            int a, b;
            cin >> a >> b;
            cout << a + b;
            return 0;
        }
    )";

    std::vector<std::pair<std::string, std::string>> tests = {
        {"2 3", "5"},
        {"10 20", "30"},
        {"-1 1", "0"}
    };

    auto job = runner.submit(1, source, tests, 2, 128);

    {
        std::unique_lock<std::mutex> lock(job->m);
        job->cv.wait(lock, [&] {
            return job->done;
        });
    }

    std::cout << "Verdict: " << job->verdict << '\n';

    assert(job->verdict == "ACCEPTED");

    std::cout << "Test passed!\n";
}