mkdir -p build
cd build

echo "--------------"
echo "Clang > GCC-11"
echo "--------------"

CC=clang CXX=clang++ cmake .. &> /dev/null
make clean && make &> /dev/null
./gdelta.exe -e -o gdelta.gdelta ../gdelta.h ../gdelta.cpp

CC=gcc-11 CXX=g++-11 cmake ..  &> /dev/null
make clean && make &> /dev/null
./gdelta.exe -d -o gdelta.out ../gdelta.h ./gdelta.gdelta

if cmp -s ./gdelta.out ../gdelta.cpp; then
   echo "Successfully reconstructed gdelta.h from gdelta.cpp, no issues found"
else
   echo "Failed to delta/reconstruct gdelta.h from gdelta.cpp, this is likely a bug please compare build/gdelta.out, gdelta.h, gdelta.cpp"
   exit
fi

CC=clang CXX=clang++ cmake .. &> /dev/null
make clean && make &> /dev/null
./gdelta.exe -e -o gdelta.gdelta ../gdelta.cpp ../gdelta.h

CC=gcc-11 CXX=g++-11 cmake ..  &> /dev/null
make clean && make &> /dev/null
./gdelta.exe -d -o gdelta.out ../gdelta.cpp ./gdelta.gdelta

if cmp -s ./gdelta.out ../gdelta.h; then
   echo "Successfully reconstructed gdelta.h from gdelta.cpp, no issues found"
else
   echo "Failed to delta/reconstruct gdelta.h from gdelta.cpp, this is likely a bug please compare build/gdelta.out, gdelta.h, gdelta.cpp"
   exit
fi

echo "--------------"
echo "GCC-11 > Clang"
echo "--------------"

CC=gcc-11 CXX=g++-11 cmake .. &> /dev/null
make clean && make &> /dev/null
./gdelta.exe -e -o gdelta.gdelta ../gdelta.h ../gdelta.cpp

CC=clang CXX=clang++ cmake .. &> /dev/null
make clean && make &> /dev/null
./gdelta.exe -d -o gdelta.out ../gdelta.h ./gdelta.gdelta

if cmp -s ./gdelta.out ../gdelta.cpp; then
   echo "Successfully reconstructed gdelta.h from gdelta.cpp, no issues found"
else
   echo "Failed to delta/reconstruct gdelta.h from gdelta.cpp, this is likely a bug please compare build/gdelta.out, gdelta.h, gdelta.cpp"
   exit
fi


