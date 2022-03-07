CC := clang
CFLAGS := -g -Wall -Werror -Wno-unused-function -Wno-unused-variable

all: p2pchat

clean:
	rm -f p2pchat

p2pchat: p2pchat.c ui.c ui.h message.c message.h
	$(CC) $(CFLAGS) -o p2pchat p2pchat.c ui.c message.c -lform -lncurses -lpthread