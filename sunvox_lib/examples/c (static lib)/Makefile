all:
	cp ../../resources/* ./
	gcc $(CFLAGS) -g -c test1.c
	gcc $(LDFLAGS) -o test1 test1.o $(LIBS)

clean:
	rm -f *.o *.a test1 *.sunvox *.sunsynth *.xi *.wav *.ogg
