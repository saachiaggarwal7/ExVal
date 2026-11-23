-- Run this as MySQL root if you want the simple project database user.
CREATE USER IF NOT EXISTS 'exval_user'@'localhost' IDENTIFIED BY 'exval123';
GRANT ALL PRIVILEGES ON exval.* TO 'exval_user'@'localhost';
FLUSH PRIVILEGES;
