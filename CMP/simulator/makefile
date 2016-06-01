C = gcc -Wall
SRCS = ./*.c
OBS = ./*.o

CMP: SRC
	$(CC) -o $@ $(OBS)

SRC: $(SRCS)
		$(CC) -c $(SRCS)

clean: $(OBS)
	rm $(OBS) CMP

