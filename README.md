# ExVal - Exam Evaluation

ExVal is an online exam system where students can attempt MCQs, write descriptive answers and solve C++ coding questions, and examiners can create exams and check the results.

Everything is built with a C++ backend, MySQL, and plain HTML/CSS/JS. No Node, React, Docker or Redis.

## What it can do

- Three roles: Student, Examiner, Admin
- MCQ, descriptive and C++ coding questions
- MCQs are checked automatically
- Descriptive answers are checked by the examiner
- Coding answers are compiled and run against test cases
- Exam timer and autosave
- Result and analytics pages for students and examiners
- Activity logs for the admin

## How it works

```text
Browser -> C++ backend -> MySQL
               |
        submission queue
               |
         worker threads
               |
     fork/exec -> g++ -> test cases
```

When a student submits code, it goes into a queue. Three worker threads pick jobs up one by one (FCFS), compile the code with `g++`, run it with a time and memory limit, and compare the output with the test cases.

## User Authorization

|User| Role |
|---|---|
| Examiner (teacher) | Create an exam -> Manage Questions -> add MCQ options, a descriptive question, a coding question and its test cases -> Publish |
| Student (student1) | Open the exam -> answer MCQs -> write the descriptive answer -> write C++ code and click **Run & Test Code** -> Submit -> view result -> check **My Analytics** |
| Examiner (again) | Results -> Review Descriptive -> give marks and feedback -> save. Then look at the Results page for score distribution, question difficulty and test-case pass rates |
| Admin | View users and the recent activity logs |

## OS concepts used

- **Process management:** `fork()`, `exec()`, `waitpid()` and `kill()` to compile and run student code
- **Multithreading:** 3 worker threads take jobs from the submission queue
- **Synchronization:** a mutex and condition variable protect the shared queue
- **File management:** temp folders with files like `solution.cpp`, `input.txt`, `output.txt`, `compile_error.txt`, `runtime_error.txt` and `program`
- **Scheduling:** the queue works in simple FCFS order
- **Resource management:** each coding run gets a time limit and a basic memory limit using `setrlimit()`

## DBMS concepts used

- **Normalization:** exams, questions, options, test cases, attempts, answers and submissions are in separate related tables
- **Transactions:** saving answers uses a transaction (commit / rollback)
- **Concurrency control:** MySQL handles many users submitting together, and the queue is protected by a mutex on the app side
- **Indexing:** indexes on student ID, exam ID and submission time
- **View:** `student_result_view` for a simple result summary
- **Trigger:** logs an activity entry when an attempt becomes SUBMITTED
- **Stored procedure:** `get_student_results(studentId)` returns a student's results


