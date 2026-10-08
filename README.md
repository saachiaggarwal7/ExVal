# ExVal - Online Exam Platform

ExVal is our college project for **Operating Systems + DBMS**. It's an online exam system where students can attempt MCQs, write descriptive answers and solve C++ coding questions, and examiners can create exams and check the results.

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

## Which OS can run it?

The backend uses POSIX stuff (`fork()`, `waitpid()`, sockets), so it **won't run on native Windows**. Windows users need WSL2. The browser can be on any OS.

| Your OS | Works? | What to do |
|---|---|---|
| Linux | Yes | Follow the Linux steps below |
| macOS | Yes | Follow the macOS steps below |
| Windows | Only with WSL2 | Install Ubuntu on WSL2, then follow the Linux steps *inside Ubuntu* |

## Steps to run it (quick table)

| Step | Linux (Ubuntu/Debian/Kali) | macOS | Windows (WSL2) |
|---|---|---|---|
| 1. Setup | Nothing extra | Install [Homebrew](https://brew.sh/) | Run `wsl --install -d Ubuntu` in PowerShell (as admin), restart, open Ubuntu app |
| 2. Install tools | `sudo apt update`<br>`sudo apt install g++ make libmariadb-dev mariadb-server` | `brew install gcc make mysql` | Same as Linux, inside Ubuntu |
| 3. Start database | `sudo service mariadb start` (or `mysql` if you use MySQL) | `brew services start mysql` | Same as Linux |
| 4. Go to project | `cd /path/to/ExVal_Modular_Phase3` | same | same (keep the project inside WSL, like `~/ExVal_Modular_Phase3`) |
| 5. Import database | See "Database setup" below | same | same |
| 6. Build | `make -C backend` | `make -C backend` | `make -C backend` |
| 7. Run | See "Start the server" below | same | same |
| 8. Open | http://localhost:8080 | http://localhost:8080 | http://localhost:8080 in your Windows browser |

> Windows people: PowerShell is only for installing WSL. Every other command goes in the Ubuntu terminal.

> macOS: if `mysql` or `mysql_config` isn't found, run `export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"`

## Database setup

Do this only once, from the project root. First check that MySQL works:

```bash
mysql -u root -e "SELECT VERSION();"
```

If that works, import the files in this order:

```bash
mysql -u root < database/schema.sql
mysql -u root < database/seed.sql
mysql -u root < database/views.sql
mysql -u root < database/triggers.sql
mysql -u root < database/procedures.sql
mysql -u root < database/setup_user.sql
```

If it asks for a password or fails, add `-p` to every command (`mysql -u root -p < ...`) and type your MySQL root password.

The SQL files create the app's own user, which is different from root:

```text
Username: exval_user
Password: exval123
Database: exval
```

**Common mistake:** if your terminal is inside `backend/`, either do `cd ..` first or use `../database/...`.

### Upgrading an old database?

A fresh install doesn't need this. But if you made your database before the analytics update, run this once:

```sql
USE exval;
ALTER TABLE student_answers
  ADD COLUMN time_spent_seconds INT NOT NULL DEFAULT 0 AFTER answer_text;

CREATE TABLE code_test_case_results (
  case_result_id INT AUTO_INCREMENT PRIMARY KEY,
  submission_id INT NOT NULL,
  test_case_id INT NOT NULL,
  verdict VARCHAR(30) NOT NULL,
  execution_time_ms INT DEFAULT 0,
  FOREIGN KEY (submission_id) REFERENCES code_submissions(submission_id) ON DELETE CASCADE,
  FOREIGN KEY (test_case_id) REFERENCES test_cases(test_case_id) ON DELETE CASCADE,
  UNIQUE KEY one_case_result_per_submission (submission_id, test_case_id),
  INDEX idx_case_result_test_case (test_case_id),
  INDEX idx_case_result_submission (submission_id)
);
```

Old attempts will show `0 sec` for question time and won't have per-test-case history, but they still work.

## Build

```bash
make -C backend
```

If it says `Nothing to be done for 'all'`, the build is already up to date. For a fresh rebuild:

```bash
make -C backend clean
make -C backend
```

The executable ends up at `backend/exval_server`.

## Start the server

From the project root:

```bash
DB_HOST=localhost DB_USER=exval_user DB_PASSWORD=exval123 DB_NAME=exval ./run.sh
```

Then open http://localhost:8080.

Port 8080 busy? Use another one:

```bash
PORT=8081 DB_HOST=localhost DB_USER=exval_user DB_PASSWORD=exval123 DB_NAME=exval ./run.sh
```

The frontend files (`frontend/`) are expected at `http://localhost:5500` by default. You can change this with the `FRONTEND_ORIGIN` variable.

### Stopping and restarting

Press `Ctrl+C` in the terminal to stop. Run the same command again to restart. Your data stays safe in the database. Just make sure MySQL/MariaDB is still running.

If the port is still busy after closing the terminal:

```bash
lsof -nP -iTCP:8080 -sTCP:LISTEN
kill <PID>
```

## Demo logins

| Role | Email | Password |
|---|---|---|
| Admin | admin@example.com | password |
| Examiner | teacher@example.com | password |
| Student | student1@example.com | password |
| Student | student2@example.com | password |

These are just for testing.

## How to test the whole thing

| Who | What to do |
|---|---|
| Examiner (teacher) | Create an exam -> Manage Questions -> add MCQ options, a descriptive question, a coding question and its test cases -> Publish |
| Student (student1) | Open the exam -> answer MCQs -> write the descriptive answer -> write C++ code and click **Run & Test Code** -> Submit -> view result -> check **My Analytics** |
| Examiner (again) | Results -> Review Descriptive -> give marks and feedback -> save. Then look at the Results page for score distribution, question difficulty and test-case pass rates |
| Admin | View users and the recent activity logs |

## OS concepts we used

- **Process management:** `fork()`, `exec()`, `waitpid()` and `kill()` to compile and run student code
- **Multithreading:** 3 worker threads take jobs from the submission queue
- **Synchronization:** a mutex and condition variable protect the shared queue
- **File management:** temp folders with files like `solution.cpp`, `input.txt`, `output.txt`, `compile_error.txt`, `runtime_error.txt` and `program`
- **Scheduling:** the queue works in simple FCFS order
- **Resource management:** each coding run gets a time limit and a basic memory limit using `setrlimit()`

## DBMS concepts we used

- **Normalization:** exams, questions, options, test cases, attempts, answers and submissions are in separate related tables
- **Transactions:** saving answers uses a transaction (commit / rollback)
- **Concurrency control:** MySQL handles many users submitting together, and the queue is protected by a mutex on the app side
- **Indexing:** indexes on student ID, exam ID and submission time
- **View:** `student_result_view` for a simple result summary
- **Trigger:** logs an activity entry when an attempt becomes SUBMITTED
- **Stored procedure:** `get_student_results(studentId)` returns a student's results

## Project files

```text
backend/
  main.cpp          starts the server and connects everything
  common.h/.cpp     request parsing, HTML helpers, shared structs
  auth.h/.cpp       login, logout, sessions, roles
  student.h/.cpp    dashboard, exam attempt, autosave, timer, results
  examiner.h/.cpp   exams, questions, test cases, evaluation
  admin.h/.cpp      users and activity dashboard
  code_runner.h/.cpp  queue, worker threads, fork/exec, limits
  database.h/.cpp   MySQL connection, queries, transactions
  Makefile
database/           schema, seed, views, triggers, procedures, setup_user
frontend/           HTML, CSS and JS files
docs/               os-concepts.md, dbms-concepts.md, team-module-guide.md
```

## Team and who studies what

| Member | Main module | OS concept | DBMS concept |
|---|---|---|---|
| Saachi | `auth.cpp` + `student.cpp` | Process Management | Transactions |
| Aashaka | `examiner.cpp` | File Management | Normalization |
| Rakshit | `code_runner.cpp` | Multithreading + Synchronization | Concurrency Control |
| Anshika | `admin.cpp` + result parts of `student.cpp` | Scheduling + Resource Management | Indexing + Query Optimization |

Everyone should still understand `main.cpp`, `database.cpp` and the full request flow before the viva. More details are in `docs/team-module-guide.md`.

## Limitations (please read)

This is a college prototype, not a real online judge.

- Student C++ code runs as a normal child process on the machine hosting the server, so **only run it locally** (Linux, macOS or WSL2). Never put it on the public internet.
- Passwords and sessions are kept simple on purpose, since authentication wasn't the focus of the project.
