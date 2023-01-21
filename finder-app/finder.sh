#!/bin/bash

# AESD Assignment 1
# Author: Daanish Shariff
# Description: This is the finder.sh file to search strings in a given directory

filesdir=$1
searchstr=$2

# Validate the number of arguments

if [ $# -ne 2 ]
then
	echo "Incorrect number of arguements. Expected 2!"
	exit 1
fi

#Check if file directory is valid

if [ ! -d "$filesdir" ]
then
	echo "File directory ${filesdir} not present"
	exit 1
fi

# Store the variables for file count and matching line count
filecount=0
linecount=0

# Using grep to get the file count and line count
filecount=$(grep -r -l $searchstr $filesdir | wc -l)
linecount=$(grep -r $searchstr $filesdir | wc -l)

echo "The number of files are ${filecount} and the number of matching lines are ${linecount}"

exit 0
