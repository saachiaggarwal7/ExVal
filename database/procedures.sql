USE exval;

DROP PROCEDURE IF EXISTS get_student_results;
DROP PROCEDURE IF EXISTS finalize_exam_attempt;
DROP PROCEDURE IF EXISTS create_platform_user;

DELIMITER $$
CREATE PROCEDURE get_student_results(IN studentId INT)
BEGIN
    SELECT attempt_id, student_name, exam_name, total_marks, obtained_marks, status,
           DATE_FORMAT(submitted_at,'%Y-%m-%d %H:%i') AS submitted_at
    FROM student_result_view
    WHERE student_id = studentId
    ORDER BY attempt_id DESC;
END$$

CREATE PROCEDURE finalize_exam_attempt(IN attemptId INT, IN totalMarks INT, IN obtainedMarks INT)
BEGIN
    UPDATE exam_attempts
    SET status='SUBMITTED',
        submitted_at=NOW(),
        total_marks=totalMarks,
        obtained_marks=obtainedMarks
    WHERE attempt_id=attemptId AND status='IN_PROGRESS';
END$$

CREATE PROCEDURE create_platform_user(
    IN p_name VARCHAR(100),
    IN p_email VARCHAR(100),
    IN p_password VARCHAR(255),
    IN p_role VARCHAR(20)
)
BEGIN
    DECLARE rid INT;
    SELECT role_id INTO rid FROM roles WHERE role_name = p_role LIMIT 1;
    IF rid IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Invalid role';
    END IF;
    INSERT INTO users(role_id, name, email, password_hash)
    VALUES (rid, p_name, p_email, p_password);
END$$
DELIMITER ;
