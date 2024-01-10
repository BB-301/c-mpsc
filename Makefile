# NOTE: Here I do a little trick to determine whether we are using GCC or CLANG.
# As you may know, on macOS, running '/usr/bin/gcc' will in fact run 'clang'. Here,
# I want to make it explicite which one is being used, because I need it later on, at
# line 47, as part of an "if/else" block, because clang and gcc appear to have a few
# differences with regards to compiling for POSIX (e.g., clang does not require the
# -lpthread flag, while GCC appears to require so; and clang will compile for POSIX by
# default, even when using -std=c17, while -std=gnu17 appears to be required for GCC).
CC = $(shell if gcc --version | grep -q 'clang'; then echo "clang"; else echo "gcc"; fi;)
ARCHIVER = ar

BUILD_DIR = build-library
EXAMPLES_BUILD_DIR = build-examples
DOCS_BUILD_DIR = build-doxygen
SOURCE_DIR = src
INCLUDE_DIR = include
EXAMPLES_DIR = examples

DOCKER_DOXYGEN_IMAGE_NAME=my_local_images/doxygen

LIB_VERSION = $(shell cat VERSION)
CURRENT_DIRECTORY = $(shell pwd)

INSTALL_PATH_PREFIX = /usr/local
INSTALL_PATH_MAN = /usr/local/man/man3
INSTALL_PATH_LIB = /usr/local/lib
INSTALL_PATH_INCLUDE = /usr/local/include

LIB_NAME = mpsc
LIB_FULL_NAME = mpsc.$(LIB_VERSION)

C_VERSION = c17
ifneq ($(CC),clang)
	C_VERSION = gnu17
endif

OPTIMIZATION_LEVEL = -O0

# Other flags to consider: -g (for debugging)
# Review whether `-fPIC` is properly used here

CFLAGS = $(OPTIMIZATION_LEVEL) \
	-std=$(C_VERSION) \
	-fpic \
	-Wall -Werror -Wextra -pedantic \
	-pthread \
	-I./$(INCLUDE_DIR)

ARCHIVER_FLAGS = rcs

.DEFAULT_GOAL := help

# =======================================
#               EXAMPLES
# =======================================

example_quick_example: \
	$(EXAMPLES_BUILD_DIR) \
	$(INCLUDE_DIR)/$(LIB_NAME).h \
	$(SOURCE_DIR)/$(LIB_NAME).c \
	$(EXAMPLES_DIR)/quick_example.c
	$(CC) $(CFLAGS) \
		$(SOURCE_DIR)/$(LIB_NAME).c $(EXAMPLES_DIR)/quick_example.c \
		-o $(EXAMPLES_BUILD_DIR)/quick_example
	./$(EXAMPLES_BUILD_DIR)/quick_example

example_empty_messages: \
	$(EXAMPLES_BUILD_DIR) \
	$(INCLUDE_DIR)/$(LIB_NAME).h \
	$(SOURCE_DIR)/$(LIB_NAME).c \
	$(EXAMPLES_DIR)/empty_messages.c
	$(CC) $(CFLAGS) \
		$(SOURCE_DIR)/$(LIB_NAME).c $(EXAMPLES_DIR)/empty_messages.c \
		-o $(EXAMPLES_BUILD_DIR)/empty_messages
	./$(EXAMPLES_BUILD_DIR)/empty_messages

example_complex_message_type: \
	$(EXAMPLES_BUILD_DIR) \
	$(INCLUDE_DIR)/$(LIB_NAME).h \
	$(SOURCE_DIR)/$(LIB_NAME).c \
	$(EXAMPLES_DIR)/complex_message_type.c
	$(CC) $(CFLAGS) \
		$(SOURCE_DIR)/$(LIB_NAME).c $(EXAMPLES_DIR)/complex_message_type.c \
		-o $(EXAMPLES_BUILD_DIR)/complex_message_type
	./$(EXAMPLES_BUILD_DIR)/complex_message_type

# Note: This example assumes that `libcurl` is installed on the system.
example_fetch_multiple_urls: \
	$(EXAMPLES_BUILD_DIR) \
	$(INCLUDE_DIR)/$(LIB_NAME).h \
	$(SOURCE_DIR)/$(LIB_NAME).c \
	$(EXAMPLES_DIR)/fetch_multiple_urls.c
	$(CC) $(CFLAGS) -lcurl \
		$(SOURCE_DIR)/$(LIB_NAME).c $(EXAMPLES_DIR)/fetch_multiple_urls.c \
		-o $(EXAMPLES_BUILD_DIR)/fetch_multiple_urls
	./$(EXAMPLES_BUILD_DIR)/fetch_multiple_urls
	
example_the_first_wins: \
	$(EXAMPLES_BUILD_DIR) \
	$(INCLUDE_DIR)/$(LIB_NAME).h \
	$(SOURCE_DIR)/$(LIB_NAME).c \
	$(EXAMPLES_DIR)/the_first_wins.c
	$(CC) $(CFLAGS) \
		$(SOURCE_DIR)/$(LIB_NAME).c $(EXAMPLES_DIR)/the_first_wins.c \
		-o $(EXAMPLES_BUILD_DIR)/the_first_wins
	./$(EXAMPLES_BUILD_DIR)/the_first_wins
	
# NOTE: This example assumes that OpenSSL is installed on the system.
example_proof_of_work: \
	$(EXAMPLES_BUILD_DIR) \
	$(INCLUDE_DIR)/$(LIB_NAME).h \
	$(SOURCE_DIR)/$(LIB_NAME).c \
	$(EXAMPLES_DIR)/proof_of_work.c
	$(CC) $(CFLAGS) -lssl -lcrypto \
		$(SOURCE_DIR)/$(LIB_NAME).c $(EXAMPLES_DIR)/proof_of_work.c \
		-o $(EXAMPLES_BUILD_DIR)/proof_of_work
	./$(EXAMPLES_BUILD_DIR)/proof_of_work

# =======================================
#                LIBRARY
# =======================================

library_object: \
	$(BUILD_DIR) \
	$(INCLUDE_DIR)/$(LIB_NAME).h \
	$(SOURCE_DIR)/$(LIB_NAME).c
	$(CC) $(CFLAGS) -c $(SOURCE_DIR)/$(LIB_NAME).c -o $(BUILD_DIR)/$(LIB_FULL_NAME).o

library: library_object
	$(CC) -shared $(BUILD_DIR)/$(LIB_FULL_NAME).o -o $(BUILD_DIR)/lib$(LIB_FULL_NAME).so
	ln -sf $(BUILD_DIR)/lib$(LIB_FULL_NAME).so $(BUILD_DIR)/lib$(LIB_NAME).so
	$(ARCHIVER) $(ARCHIVER_FLAGS) $(BUILD_DIR)/lib$(LIB_FULL_NAME).a $(BUILD_DIR)/$(LIB_FULL_NAME).o
	ln -sf $(BUILD_DIR)/lib$(LIB_FULL_NAME).a $(BUILD_DIR)/lib$(LIB_NAME).a

# NOTE ABOUT USING SUDO
# Putting sudo here avoids that the library files in the build directory
# be owned by the `root` user.
install: library
	sudo cp $(BUILD_DIR)/lib$(LIB_FULL_NAME).a $(INSTALL_PATH_LIB)/lib$(LIB_FULL_NAME).a
	sudo ln -sf $(INSTALL_PATH_LIB)/lib$(LIB_FULL_NAME).a $(INSTALL_PATH_LIB)/lib$(LIB_NAME).a
	sudo cp $(BUILD_DIR)/lib$(LIB_FULL_NAME).so $(INSTALL_PATH_LIB)/lib$(LIB_FULL_NAME).so
	sudo ln -sf $(INSTALL_PATH_LIB)/lib$(LIB_FULL_NAME).so $(INSTALL_PATH_LIB)/lib$(LIB_NAME).so
	sudo cp $(INCLUDE_DIR)/$(LIB_NAME).h $(INSTALL_PATH_INCLUDE)/$(LIB_NAME).h

install_with_docs: docs install
	sudo cp $(DOCS_BUILD_DIR)/man/man3/$(LIB_NAME).h.3 $(INSTALL_PATH_MAN)/$(LIB_NAME).h.3

# NOTE ABOUT USING SUDO
# Unlike "install", sudo could be passed externally here (i.e. sudo make uninstall),
# but using it here makes it more consistent.
# And the first line with 'echo' is there because we use `-f` to rm, so
# if the password is wrong on the first file, make will ask again, and if the
# password is subsequently correct, the first file will not be deleted. So here,
# 'echo' will fail with a bad password and make will exit.
# TO DO: Test this again to make sure it's true
uninstall:
	@sudo echo "THANK YOU"
	sudo rm -f $(INSTALL_PATH_LIB)/lib$(LIB_NAME).a
	sudo rm -f $(INSTALL_PATH_LIB)/lib$(LIB_FULL_NAME).a
	sudo rm -f $(INSTALL_PATH_LIB)/lib$(LIB_NAME).so
	sudo rm -f $(INSTALL_PATH_LIB)/lib$(LIB_FULL_NAME).so
	sudo rm -f $(INSTALL_PATH_INCLUDE)/$(LIB_NAME).h
	sudo rm -f $(INSTALL_PATH_MAN)/$(LIB_NAME).h.3


# =======================================
#                  MISC
# =======================================

$(BUILD_DIR):
	@if ! [ -d $(BUILD_DIR) ]; then mkdir $(BUILD_DIR); fi;

$(EXAMPLES_BUILD_DIR):
	@if ! [ -d $(EXAMPLES_BUILD_DIR) ]; then mkdir $(EXAMPLES_BUILD_DIR); fi;

.PHONY: clean help examples docs docs_for_website

docs:
	docker build -f doxygen/Dockerfile -t $(DOCKER_DOXYGEN_IMAGE_NAME) .
	docker run \
    	--rm \
    	-v $(CURRENT_DIRECTORY):/my-dir \
    	-w /my-dir \
		-e DOXYGEN_PROJECT_NUMBER=$(LIB_VERSION) \
    	$(DOCKER_DOXYGEN_IMAGE_NAME) doxygen doxygen/Doxyfile

docs_for_website:
	docker build -f doxygen/Dockerfile -t $(DOCKER_DOXYGEN_IMAGE_NAME) .
	docker run \
    	--rm \
    	-v $(CURRENT_DIRECTORY):/my-dir \
		-v $(CURRENT_DIRECTORY)/../c-mpsc-docs/docs/v$(LIB_VERSION):/my-dir/build-doxygen \
    	-w /my-dir \
		-e DOXYGEN_PROJECT_NUMBER=$(LIB_VERSION) \
    	$(DOCKER_DOXYGEN_IMAGE_NAME) doxygen doxygen/Doxyfile


EXAMPLES_SOURCES = $(shell find $(EXAMPLES_DIR) -name "*.c" -type f)
EXAMPLES_SOURCES_NAMES = $(notdir $(EXAMPLES_SOURCES))
EXAMPLES_NAMES = $(EXAMPLES_SOURCES_NAMES:.c=)

examples:
	@echo "\nTo run the individual examples:"
	@for NAME in $(EXAMPLES_NAMES); do echo "\n- make example_$$NAME"; done;
	@echo ""

clean:
	rm -rf ./$(BUILD_DIR);
	rm -rf ./$(DOCS_BUILD_DIR);
	rm -rf ./$(EXAMPLES_BUILD_DIR);

help:
	@echo "\n- make library\n\tBuilds the library (both the shared and static versions)"
	@echo "\n- make install\n\tInstalls the library at '${INSTALL_PATH_PREFIX}' (note: this requires 'sudo' internally)"
	@echo "\n- make install_with_docs\n\tInstalls the library, including the man3 page, at '${INSTALL_PATH_PREFIX}' (notes: this requires 'sudo' internally; will call docker to build the docs)"
	@echo "\n- make uninstall\n\tUninstalls the library (note: this requires 'sudo' internally)"
	@echo "\n- make docs\n\tBuilds a Docker container containing Doxygen and runs it to generate the Doxygen documentation website"
	@echo "\n- make docs_for_website\n\tBuilds the Doxygen website using Docker and outputs the result into '../c-mpsc-docs/docs/v$(LIB_VERSION)'."
	@echo "\n- make clean\n\tCleans up (i.e., deletes the '${BUILD_DIR}', '${DOCS_BUILD_DIR}', and '${EXAMPLES_BUILD_DIR}' directories)"
	@echo "\n- make examples\n\tPrints the list of available example recipes"
	@echo "\n- make help\n\tPrints this summary of the available recipes"
	@echo ""