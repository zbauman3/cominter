#!/bin/bash

. ./scripts/validate_env_vars.sh;

echo "Killing monitor for \"${COMINTER_DEVICE_NAME}\" on \"${COMINTER_DEVICE_PORT}\""

PIDS=$(lsof -t "${COMINTER_DEVICE_PORT}" "${COMINTER_DEVICE_PORT/tty./cu.}" 2>/dev/null)
if [ -n "$PIDS" ]; then
  kill $PIDS
else
  echo "No running monitor found for \"${COMINTER_DEVICE_NAME}\""
fi