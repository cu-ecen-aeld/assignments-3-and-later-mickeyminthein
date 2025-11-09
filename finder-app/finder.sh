#!/bin/sh

# Check if both arguments are provided
if [ $# -ne 2 ]; then
    echo "Error: Two arguments required. Usage: finder.sh <filesdir> <searchstr>"
    exit 1
fi

filesdir="$1"
searchstr="$2"

# Check if filesdir is a valid directory
if [ ! -d "$filesdir" ]; then
    echo "Error: '$filesdir' is not a valid directory."
    exit 1
fi

# Count the number of files (regular files only) in the directory and subdirectories
file_count=$(find "$filesdir" -type f | wc -l)

# Count the number of matching lines containing searchstr
match_count=$(grep -r "$searchstr" "$filesdir" 2>/dev/null | wc -l)

# Output the result
echo "The number of files are $file_count and the number of matching lines are $match_count"
