#!/bin/bash
set -e
cd "$(dirname "$0")/backend"
make
export DB_HOST=${DB_HOST:-localhost}
export DB_USER=${DB_USER:-exval_user}
export DB_PASSWORD=${DB_PASSWORD:-exval123}
export DB_NAME=${DB_NAME:-exval}
./exval_server
