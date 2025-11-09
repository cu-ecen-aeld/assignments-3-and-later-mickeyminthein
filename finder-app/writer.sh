#!/bin/sh

# Check if both arguments are provided
if [ $# -ne 2 ]; then
    echo "Error: Two arguments required. Usage: writer.sh <writefile> <writestr>"
    exit 1
fi

writefile="$1"
writestr="$2"
dirpath=$(dirname "$writefile")

# Check if the directory exists, create if it doesn't
if [ ! -d "$dirpath" ]; then
    mkdir -p "$dirpath"
    if [ $? -ne 0 ]; then
        echo "Error: Directory '${dirpath}' cannot be created"
        exit 1
    fi
fi

# Try to create the file
touch "$writefile"
if [ ! -f "$writefile" ]; then
    echo "Error: File '${writefile}' could not be created"
    exit 1
fi

# Write the string to the file
echo "$writestr" > "$writefile"
if [ $? -ne 0 ]; then
    echo "Error: Failed to write to file '${writefile}'"
    exit 1
fi

exit 0


