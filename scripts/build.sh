#!/bin/bash

Echo "Building...\n"

source ${HOME}/.espressif/tools/activate_idf_v5.5.2.sh;

# Build with Ninja to do what the IDF Cursor extension recommends
${IDF_PATH}/tools/idf.py -G Ninja build;