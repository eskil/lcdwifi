BIN=lcdwifi
OBJS=lcdwifi.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $< -lpopt -liw

clean:
	rm -f $(BIN) $(OBJS)

