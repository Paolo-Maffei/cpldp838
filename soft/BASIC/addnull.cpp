/*
    project        : addnull
    author         : omiokone
    addnull.cpp    : addnull main
*/

#include <stdio.h>

int main(int, char *argv[])
{
    FILE    *inf, *outf;
    int     c, i;

    inf =fopen(argv[1], "rb");
    outf=fopen(argv[2], "wb");

    if(inf && outf){
        while((c=fgetc(inf))!=EOF){
            fputc(c, outf);
            if(c==0x0d) for(i=0; i<9; i++) fputc(0, outf);
        }
    }

    fclose(inf);
    fclose(outf);
    return 0;
}
