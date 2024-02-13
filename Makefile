all: sef

sef: sef.c
	gcc sef.c -o sef

clean:
	rm -f sef
