#!/bin/bash

# AESD Assignment 1
# Author: Daanish Shariff
# Description: This is the writer file to write

# Validate th number of arguments
if [ $# -ne 2 ]
then
	echo "Invalid number of arguments. Expected 2!"
	exit 1
fi

file_path=$1
writestr=$2

# Directory Name
directory=$(dirname "${file_path}")

# Create Directory and write string to it
mkdir -p "${directory}" && echo "${writestr}" > $file_path

# Check if the file was created in given directory

if [ ! -f "$file_path" ]
then
	echo "File not created."
	exit 1
fi

exit 0
