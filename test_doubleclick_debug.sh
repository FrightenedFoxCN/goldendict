#!/bin/bash
# Script to test double-click functionality with debug output

pkill SilverDict 2>/dev/null
sleep 1

# Run the app and capture debug output
echo "Starting SilverDict with debug output capture..."
echo "Try double-clicking on words in the article viewer."
echo "Debug output will appear below:"
echo "========================================"

/Users/ffox/goldendict/SilverDict.app/Contents/MacOS/SilverDict 2>&1 | grep -E "doubleClicked|doubleClickTranslates|Selected text|Initiating translation"
