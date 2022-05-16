project = proj10

.PHONY: all
all: 
	g++ -lpthread -Wall -o $(project) proj10.student.c

.PHONY: clean
clean:
	rm -f $(project)
	rm -f *.o

