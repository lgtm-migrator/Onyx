#!/bin/python3

import sys
import subprocess

def main():
    if len(sys.argv) != 2:
        print("bad argv")
        exit(1)
    
    with open(sys.argv[1], "rb") as file:
        elf_sig = file.read(4)

        if elf_sig != b"\x7fELF":
            #print(f'{sys.argv[1]} not an ELF file, sig {elf_sig}')
            exit(0)
        
        subprocess.run(["strip", sys.argv[1]], check=True)

if __name__ == "__main__":
    main()
