#!/bin/sh

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Coverage
make

# Clean coverage files
find . -name '*.gcda' -exec rm {} \;

./gdelta.exe -e -o gdelta.gdelta ../gdelta.cpp ../gdelta.h
./gdelta.exe -d -o gdelta.out ../gdelta.cpp ./gdelta.gdelta
if cmp -s ./gdelta.out ../gdelta.h; then
   echo "Successfully reconstructed gdelta.h from gdelta.cpp, no issues found"
else
   echo "Failed to delta/reconstruct gdelta.h from gdelta.cpp, this is likely a bug please compare build/gdelta.out, gdelta.h, gdelta.cpp"
   exit
fi

./gdelta.exe -e -o gdelta.gdelta ../gdelta.h ../gdelta.cpp
./gdelta.exe -d -o gdelta.out ../gdelta.h ./gdelta.gdelta
if cmp -s ./gdelta.out ../gdelta.cpp; then
   echo "Successfully reconstructed gdelta.cpp from gdelta.h, no issues found"
else
   echo "Failed to delta/reconstruct gdelta.cpp from gdelta.h, this is likely a bug please compare build/gdelta.out, gdelta.h, gdelta.cpp"
   exit
fi

# Generate coverage files
find . -name '*.gcda' | xargs gcov
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory ../coverage
