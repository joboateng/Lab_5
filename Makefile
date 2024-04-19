CC=gcc

%.o: %.c 
    $(CC) -c -w $<

oss: oss.o SM.o time.o queue.o
    $(CC) -o oss oss.o SM.o time.o queue.o -pthread -w
    
worker: worker.o SM.o time.o queue.o
    $(CC) -o worker worker.o SM.o time.o queue.o -pthread -w

clean:
    rm oss worker *.o *.out
