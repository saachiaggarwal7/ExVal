DROP DATABASE IF EXISTS exval;
CREATE DATABASE exval;
USE exval;

CREATE TABLE roles (
    role_id INT AUTO_INCREMENT PRIMARY KEY,
    role_name VARCHAR(20) NOT NULL UNIQUE
);

CREATE TABLE users (
    user_id INT AUTO_INCREMENT PRIMARY KEY,
    role_id INT NOT NULL,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(100) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    is_active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (role_id) REFERENCES roles(role_id)
);

CREATE TABLE exams (
    exam_id INT AUTO_INCREMENT PRIMARY KEY,
    created_by INT NOT NULL,
    title VARCHAR(150) NOT NULL,
    duration_minutes INT NOT NULL,
    status ENUM('DRAFT','PUBLISHED','CLOSED') DEFAULT 'DRAFT',
    published_at TIMESTAMP NULL,
    allow_multiple_attempts BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (created_by) REFERENCES users(user_id),
    INDEX idx_exam_creator (created_by),
    INDEX idx_exam_status (status)
);

CREATE TABLE questions (
    question_id INT AUTO_INCREMENT PRIMARY KEY,
    exam_id INT NOT NULL,
    question_type ENUM('MCQ','DESCRIPTIVE','CODING') NOT NULL,
    question_text TEXT NOT NULL,
    marks INT NOT NULL,
    time_limit_seconds INT DEFAULT 2,
    memory_limit_mb INT DEFAULT 64,
    FOREIGN KEY (exam_id) REFERENCES exams(exam_id) ON DELETE CASCADE,
    INDEX idx_question_exam (exam_id)
);

CREATE TABLE mcq_options (
    option_id INT AUTO_INCREMENT PRIMARY KEY,
    question_id INT NOT NULL,
    option_text VARCHAR(255) NOT NULL,
    is_correct BOOLEAN DEFAULT FALSE,
    FOREIGN KEY (question_id) REFERENCES questions(question_id) ON DELETE CASCADE,
    INDEX idx_option_question (question_id)
);

CREATE TABLE test_cases (
    test_case_id INT AUTO_INCREMENT PRIMARY KEY,
    question_id INT NOT NULL,
    input_data TEXT NOT NULL,
    expected_output TEXT NOT NULL,
    FOREIGN KEY (question_id) REFERENCES questions(question_id) ON DELETE CASCADE,
    INDEX idx_test_question (question_id)
);

CREATE TABLE exam_attempts (
    attempt_id INT AUTO_INCREMENT PRIMARY KEY,
    exam_id INT NOT NULL,
    student_id INT NOT NULL,
    started_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    submitted_at TIMESTAMP NULL,
    status ENUM('IN_PROGRESS','SUBMITTED') DEFAULT 'IN_PROGRESS',
    total_marks INT DEFAULT 0,
    obtained_marks INT DEFAULT 0,
    FOREIGN KEY (exam_id) REFERENCES exams(exam_id),
    FOREIGN KEY (student_id) REFERENCES users(user_id),
    INDEX idx_attempt_exam_student (exam_id, student_id),
    INDEX idx_attempt_student (student_id),
    INDEX idx_attempt_exam (exam_id)
);

CREATE TABLE student_answers (
    answer_id INT AUTO_INCREMENT PRIMARY KEY,
    attempt_id INT NOT NULL,
    question_id INT NOT NULL,
    selected_option_id INT NULL,
    answer_text TEXT NULL,
    time_spent_seconds INT NOT NULL DEFAULT 0,
    marks_awarded INT DEFAULT 0,
    feedback TEXT NULL,
    evaluated_by INT NULL,
    FOREIGN KEY (attempt_id) REFERENCES exam_attempts(attempt_id) ON DELETE CASCADE,
    FOREIGN KEY (question_id) REFERENCES questions(question_id),
    FOREIGN KEY (selected_option_id) REFERENCES mcq_options(option_id),
    FOREIGN KEY (evaluated_by) REFERENCES users(user_id),
    UNIQUE KEY one_answer_per_question (attempt_id, question_id),
    INDEX idx_answer_attempt (attempt_id),
    INDEX idx_answer_question (question_id)
);

CREATE TABLE code_submissions (
    submission_id INT AUTO_INCREMENT PRIMARY KEY,
    answer_id INT NOT NULL,
    source_code MEDIUMTEXT NOT NULL,
    status VARCHAR(30) DEFAULT 'WAITING',
    submitted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (answer_id) REFERENCES student_answers(answer_id) ON DELETE CASCADE,
    INDEX idx_submission_answer (answer_id),
    INDEX idx_submission_time (submitted_at)
);

CREATE TABLE execution_results (
    result_id INT AUTO_INCREMENT PRIMARY KEY,
    submission_id INT NOT NULL,
    verdict VARCHAR(30) NOT NULL,
    passed_tests INT DEFAULT 0,
    total_tests INT DEFAULT 0,
    execution_time_ms INT DEFAULT 0,
    FOREIGN KEY (submission_id) REFERENCES code_submissions(submission_id) ON DELETE CASCADE,
    INDEX idx_execution_submission (submission_id)
);

-- Raw per-test-case outcomes power coding difficulty and failure analytics.
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

CREATE TABLE activity_logs (
    log_id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NULL,
    action VARCHAR(50) NOT NULL,
    details TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id),
    INDEX idx_log_user (user_id),
    INDEX idx_log_action (action)
);
