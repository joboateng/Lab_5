all: oss worker

%.o: %.c 
	$(CC) -c  -w $<

oss: oss.o SM.o time.o queue.o
	gcc -o oss oss.o SM.o time.o queue.o  -pthread -w
	
worker: worker.o SM.o time.o queue.o
	gcc -o worker worker.o SM.o time.o queue.o -pthread -w

clean:
	rm oss worker *.o *.out

