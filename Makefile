EXECS=scheduler_os
CC=g++
MYFILE=main.cpp

all: $(EXECS)

$(EXECS): $(MYFILE)
	$(CC) -o $(EXECS) $(MYFILE) -lpthread -lcurl -std=c++11

clean:
	rm -f $(EXECS)

