# FAT32 FILE SYSTEM IMPLEMENTATION

This project is an implementation a FAT32 filesystem written in C

## Group Number
12

## Group Name
Group 12

## Group Members
- **Ryan Baker**: rb20i@fsu.edu
- **Alexander Kajda**: ak21v@fsu.edu
- **Ian O'Neill**: ico19@fsu.edu
## Division of Labor

### Part 1:  Mount the Image 
- **Contributed:**: Ian O'Neill, Ryan Baker

### Part 2: Navigation (LS and CD commands)
- **Contributed**: Ryan Baker

### Part 3: Create Directories and Files
- **Contributed**: Alexander Kajda, Ryan Baker

### Part 4: Read Files
- **Contributed**: Alexander Kajda

### Part 3c: Update
- **Contributed**: Alexander Kajda

### Part 3d: Delete
- **Contributed**: Ryan Baker

  


## File Listing
```
├── include/
| └──lexer.h
├── src/
│ └── filesys.c
├── README.md
└── Makefile
```
## How to Compile & Execute

### Requirements
- **Compiler**: `gcc' 

### Compilation
In the root directory, run:
```bash
make
```
### Run Program
In the root directory, run:
```
./bin/filesys fat32.img
```



