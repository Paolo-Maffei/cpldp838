/*
    project        : bin2ris, binary to RIS format converter for CPLDP-8
    author         : omiokone
    bin2ris.cpp    : bin2ris main
*/

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nextch()                             // remove rubout pair
{
    int rubout, c;

    rubout=0;
    for(;;){
        c=getchar();
        if(c==EOF){
            fputs("error: unexpected end of file\n", stderr);
            exit(1);
        }
        if(c==0xff) rubout=!rubout; else
        if(!rubout) break;
    }
    return c;
}

int main(int argc, char *argv[])
{
    int     address, data1, data2, c, i;

    setmode(fileno(stdin),  O_BINARY);
    setmode(fileno(stdout), O_BINARY);

    address=0;
    do c=nextch(); while(!(c&0x40));            // find address or field
    for(;;){
        if(c&0x80){
            if(c&0x40){                         // field setting
                if(c&0x38){                     // field not 0
                    fputs("error: multi field not allowed\n", stderr);
                    exit(1);
                }
                c=nextch();
            }else break;                        // trailing
        }else{
            if(c&0x40){                         // origin setting
                address=((c&0x3f)<<6)|(nextch()&0x3f);
                c=nextch();
            }else{                              // data
                data1=c; data2=nextch();
                c=nextch();
                if(c!=0x80){                    // data is not checksum
                    putchar((unsigned char)(((address>>6)&0x3f)|(0x40)));
                    putchar((unsigned char)(address&0x3f));
                    putchar((unsigned char)data1);
                    putchar((unsigned char)data2);
                    address++;
                }
            }
        }
    }

    for(i=1; i<argc; i++){      // start address info. (specific to CPLDP-8)
        if(strncmp(argv[i], "-s", 2)==0){
            address=(int)strtoul(argv[i]+2, NULL, 8);
            putchar(0x7f); putchar(0x3f);
            putchar((unsigned char)((address>>6)&0x3f));
            putchar((unsigned char)(address&0x3f));
        }
    }

    return 0;
}
