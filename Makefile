NAME          = zapp-mt

CC            = gcc
DEFINES       =
CFLAGS        = -pipe -std=c11 -Wall -Wextra $(DEFINES)
LIBS          = -lz -lpthread

FILES         = main.c

build:
	$(CC) -o $(NAME) $(CFLAGS) $(FILES) $(LIBS)
	
run:
	./$(NAME) --input=main.c --output=main.c-compressed
	
clean:
	rm $(NAME)
