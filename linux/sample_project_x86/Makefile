all:
	cp ../lib_x86/sunvox.so ./
	cp ../../resources/* ./
	gcc -m32 -g -c test1.cpp
	gcc -m32 -g -c test2.cpp
	gcc -m32 -g -c test3.cpp
	gcc -m32 -g -c test4.cpp
	gcc -m32 -g -c test5.cpp
	gcc -m32 -g -c test6.cpp
	gcc -m32 -lstdc++ -ldl -lm -o test1 test1.o
	gcc -m32 -lstdc++ -ldl -lm -o test2 test2.o
	gcc -m32 -lstdc++ -ldl -lm -o test3 test3.o
	gcc -m32 -lstdc++ -ldl -lm -o test4 test4.o
	gcc -m32 -lstdc++ -ldl -lm -o test5 test5.o
	gcc -m32 -lstdc++ -ldl -lm -o test6 test6.o

clean:
	rm -f *.o *.so test1 test2 test3 test4 test5 test6 *.sunvox *.sunsynth *.xi *.wav