# BUN Parser - Group [N]

A C11 parser for the BUN (Binary UNified assets) game asset bundle format.

## Group members
- Teammate B (-) - GitHub: @teammate-b
- Dennis Quek (-) - GitHub: @dendenpixel
- Teammate C (-) - GitHub: @teammate-c
- Teammate A (-) - GitHub: @teammate-a

## Repository
https://github.com/teammate-b/Project

## Build
    make clean && make all
    sudo apt-get install check  # only if you want make test

Produces `./bun_parser` in the project root.

## Run
    ./bun_parser path/to/file.bun

Valid files: header + all asset records printed to stdout, exit 0.  
Invalid files: violations on stderr, partial output on stdout, exit 1 or 2.

## Test
    make fixtures     # python script generates valid+invalid .bun files
    make test         # unit tests (libcheck)
    make samples-test # end-to-end against all fixture files

## Exit codes
0 - OK  
1 - malformed (spec violation)  
2 - unsupported feature  
3 - I/O error or wrong number of arguments  
4 - file not found