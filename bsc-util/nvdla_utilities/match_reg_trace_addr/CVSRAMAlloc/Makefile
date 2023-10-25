CC = g++-7
FLAGS = -g -Wall -Wextra
GRBPATH = /home/lactose/Software/gurobi1003/

ActAlloc: ActAlloc.cpp
	$(CC) $(FLAGS) ActAlloc.cpp -o ActAlloc -I$(GRBPATH)/linux64/include -L$(GRBPATH)/linux64/lib -lgurobi_c++ -lgurobi100

clean:
	rm -f ActAlloc *.o *.log