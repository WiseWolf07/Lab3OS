#include <stdio.h>
#include <string.h>

int readFile(){
    FILE *matrixA;

    matrixA = fopen("matrices/A.txt", "r");

    char matrixAChar[100];

    while(fgets(matrixAChar, 100, matrixA)){
        
    }

    fclose(matrixA);
    return 0;
}

int main(){
    readFile();
    return 0;
}
