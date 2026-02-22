#!/bin/bash

. ./scripts/validate_env_vars.sh;

echo "Monitoring \"${COMINTER_DEVICE_NAME}\" on \"${COMINTER_DEVICE_PORT}\"\n";

source ${HOME}/.espressif/tools/activate_idf_v5.5.2.sh;
${IDF_PATH}/tools/idf.py monitor --port ${COMINTER_DEVICE_PORT};