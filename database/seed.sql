USE exval;

INSERT INTO roles(role_name) VALUES ('STUDENT'),('EXAMINER'),('ADMIN');

INSERT INTO users(role_id,name,email,password_hash) VALUES
(3,'Administrator','admin@example.com','password'),
(2,'Teacher','teacher@example.com','password'),
(1,'Student One','student1@example.com','password'),
(1,'Student Two','student2@example.com','password');

INSERT INTO exams(created_by,title,duration_minutes,status,published_at,allow_multiple_attempts)
VALUES (2,'Operating Systems & DBMS Test',30,'PUBLISHED',NOW(),TRUE);

INSERT INTO questions(exam_id,question_type,question_text,marks,time_limit_seconds,memory_limit_mb) VALUES
(1,'MCQ','Which OS component selects the next process?',2,2,64),
(1,'MCQ','Which SQL command reads data?',2,2,64),
(1,'MCQ','Which normal form removes partial dependency?',2,2,64),
(1,'DESCRIPTIVE','Explain the difference between a process and a thread.',5,2,64),
(1,'CODING','Read two integers and print their sum.',5,2,64);

INSERT INTO mcq_options(question_id,option_text,is_correct) VALUES
(1,'Scheduler',1),(1,'Compiler',0),(1,'Loader',0),(1,'Linker',0),
(2,'SELECT',1),(2,'DELETE',0),(2,'UPDATE',0),(2,'INSERT',0),
(3,'2NF',1),(3,'1NF',0),(3,'3NF',0),(3,'BCNF',0);

INSERT INTO test_cases(question_id,input_data,expected_output) VALUES
(5,'2 3\n','5\n'),
(5,'10 7\n','17\n');
