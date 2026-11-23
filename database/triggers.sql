USE exval;
DROP TRIGGER IF EXISTS after_attempt_submit;
DELIMITER $$
CREATE TRIGGER after_attempt_submit
AFTER UPDATE ON exam_attempts
FOR EACH ROW
BEGIN
    IF OLD.status <> 'SUBMITTED' AND NEW.status = 'SUBMITTED' THEN
        INSERT INTO activity_logs(user_id, action, details)
        VALUES (NEW.student_id, 'EXAM_SUBMITTED', CONCAT('Attempt ', NEW.attempt_id, ' submitted'));
    END IF;
END$$
DELIMITER ;
