#!/bin/bash

# Check if the `COMINTER_DEVICE_NAME` and `COMINTER_DEVICE_PORT` environment variables are set
if [ -z "$COMINTER_DEVICE_NAME" ]; then
  echo "Error: environment variable COMINTER_DEVICE_NAME is not set."
  exit 1
fi

if [ -z "$COMINTER_DEVICE_PORT" ]; then
  echo "Error: environment variable COMINTER_DEVICE_PORT is not set."
  exit 1
fi