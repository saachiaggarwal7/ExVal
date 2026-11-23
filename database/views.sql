USE exval;

-- Result summaries used by student analytics and examiner score tables.
CREATE OR REPLACE VIEW student_result_view AS
SELECT
    ea.attempt_id,
    ea.student_id,
    u.name AS student_name,
    e.exam_id,
    e.title AS exam_name,
    ea.total_marks,
    ea.obtained_marks,
    ea.status,
    ea.submitted_at,
    ea.anticheat_warnings,
    ea.is_disqualified,
    ea.disqualification_reason
FROM exam_attempts ea
JOIN users u ON ea.student_id=u.user_id
JOIN exams e ON ea.exam_id=e.exam_id
WHERE ea.status='SUBMITTED';

-- Public question rows: no answers, options, or test-case expected output.
CREATE OR REPLACE VIEW exam_questions_safe AS
SELECT question_id, exam_id, question_type, question_text, marks,
       time_limit_seconds, memory_limit_mb
FROM questions;

-- Anti-cheat summary per attempt: total warnings, events by type.
CREATE OR REPLACE VIEW anticheat_summary_view AS
SELECT
    ea.attempt_id,
    ea.student_id,
    u.name AS student_name,
    e.exam_id,
    e.title AS exam_name,
    ea.anticheat_warnings,
    ea.is_disqualified,
    ea.disqualification_reason,
    COUNT(al.log_id) AS total_events,
    SUM(CASE WHEN al.event_type = 'TAB_SWITCH' THEN 1 ELSE 0 END) AS tab_switches,
    SUM(CASE WHEN al.event_type = 'WINDOW_BLUR' THEN 1 ELSE 0 END) AS window_blurs,
    SUM(CASE WHEN al.event_type = 'COPY_BLOCKED' THEN 1 ELSE 0 END) AS copy_attempts,
    SUM(CASE WHEN al.event_type = 'PASTE_BLOCKED' THEN 1 ELSE 0 END) AS paste_attempts,
    SUM(CASE WHEN al.event_type = 'FULLSCREEN_EXIT' THEN 1 ELSE 0 END) AS fullscreen_exits,
    SUM(CASE WHEN al.event_type = 'AUTO_SUBMITTED' THEN 1 ELSE 0 END) AS auto_submitted_count
FROM exam_attempts ea
JOIN users u ON ea.student_id = u.user_id
JOIN exams e ON ea.exam_id = e.exam_id
LEFT JOIN anticheat_logs al ON ea.attempt_id = al.attempt_id
GROUP BY ea.attempt_id, ea.student_id, u.name, e.exam_id, e.title, ea.anticheat_warnings, ea.is_disqualified, ea.disqualification_reason;
