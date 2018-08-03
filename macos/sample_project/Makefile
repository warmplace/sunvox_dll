all:
	cp ../lib_x86_64/sunvox.dylib ./
	cp ../../resources/* ./
	gcc -m64 -g -c test1.cpp
	gcc -m64 -g -c test2.cpp
	gcc -m64 -g -c test3.cpp
	gcc -m64 -g -c test4.cpp
	gcc -m64 -g -c test5.cpp
	gcc -m64 -g -c test6.cpp
	gcc -m64 -lstdc++ -o test1 test1.o
	gcc -m64 -lstdc++ -o test2 test2.o
	gcc -m64 -lstdc++ -o test3 test3.o
	gcc -m64 -lstdc++ -o test4 test4.o
	gcc -m64 -lstdc++ -o test5 test5.o
	gcc -m64 -lstdc++ -o test6 test6.o

clean:
	rm -f *.o *.dylib test1 test2 test3 test4 test5 test6 *.sunvox *.sunsynth *.xi *.wav