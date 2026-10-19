# =============================================================================
#  Makefile - Gestionnaire de reservations (C99)
# =============================================================================

CC       := gcc
CFLAGS   := -Wall -Wextra -std=c99 -pedantic -Werror -O2
CPPFLAGS := -Iinclude
DEPFLAGS  = -MMD -MP
LDFLAGS  :=

SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin
DATA_DIR := data

TARGET   := $(BIN_DIR)/reservation_manager
SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS     := $(OBJS:.o=.d)

.PHONY: all clean distclean run debug valgrind

# Cible par defaut : executable + dossier de donnees.
all: $(TARGET) | $(DATA_DIR)

# Edition de liens.
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Compilation separee de chaque module (+ generation des dependances .d).
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Creation automatique des dossiers (order-only prerequisites).
$(BIN_DIR) $(OBJ_DIR) $(DATA_DIR):
	mkdir -p $@

# Lancement direct.
run: all
	./$(TARGET)

# Build de debogage : symboles, pas d'optimisation, sanitizers.
debug: CFLAGS := -Wall -Wextra -std=c99 -pedantic -Werror -g -O0 -fsanitize=address,undefined
debug: LDFLAGS := -fsanitize=address,undefined
debug: clean all

# Verification des fuites memoire (necessite valgrind).
valgrind: all
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./$(TARGET)

# Nettoyage des artefacts de compilation (les donnees sont conservees).
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Nettoyage complet, donnees comprises.
distclean: clean
	rm -rf $(DATA_DIR)

-include $(DEPS)
