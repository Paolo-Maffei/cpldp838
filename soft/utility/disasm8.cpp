/*
    project        : disasm8, PDP-8 disassembler
    author         : omiokone
    disasm8.cpp    : disasm8 main
*/

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>

struct instruction_table {
    int     mask;
    int     code;
    char   *mnemonic;
    int     mode;
    };

static struct instruction_table instructions[]={
    {07000, 00000, "AND",  1},
    {07000, 01000, "TAD",  1},
    {07000, 02000, "ISZ",  1},
    {07000, 03000, "DCA",  1},
    {07000, 04000, "JMS",  1},
    {07000, 05000, "JMP",  1},

    {07777, 06000, "SKON", 0},
    {07777, 06001, "ION",  0},
    {07777, 06002, "IOF",  0},
    {07777, 06003, "SRQ",  0},
    {07777, 06004, "GTF",  0},
    {07777, 06005, "RTF",  0},
    {07777, 06006, "SGT",  0},
    {07777, 06007, "CAF",  0},

    {07777, 06010, "RPE",  0},
    {07777, 06011, "RSF",  0},
    {07773, 06012, "RRB",  0},
    {07775, 06014, "RFC",  0},
    {07777, 06020, "PCE",  0},
    {07777, 06021, "PSF",  0},
    {07777, 06022, "PCF",  0},
    {07777, 06024, "PPC",  0},
    {07777, 06026, "PLS",  0},

    {07777, 06030, "KCF",  0},
    {07777, 06031, "KSF",  0},
    {07777, 06032, "KCC",  0},
    {07777, 06034, "KRS",  0},
    {07777, 06035, "KIE",  0},
    {07777, 06036, "KRB",  0},
    {07777, 06040, "TFL",  0},
    {07777, 06041, "TSF",  0},
    {07777, 06042, "TCF",  0},
    {07777, 06044, "TPC",  0},
    {07777, 06045, "TSK",  0},
    {07777, 06046, "TLS",  0},

    {07705, 06201, "CDF",  2},
    {07706, 06202, "CIF",  2},
    {07777, 06214, "RDF",  0},
    {07777, 06224, "RIF",  0},
    {07777, 06234, "RIB",  0},
    {07777, 06244, "RMF",  0},

    {07777, 07000, "NOP",  0},
    {07600, 07200, "CLA",  0},
    {07500, 07100, "CLL",  0},
    {07440, 07040, "CMA",  0},
    {07420, 07020, "CML",  0},
    {07401, 07001, "IAC",  0},
    {07412, 07010, "RAR",  0},
    {07412, 07012, "RTR",  0},
    {07406, 07004, "RAL",  0},
    {07406, 07006, "RTL",  0},
    {07416, 07002, "BSW",  0},

    {07777, 07400, "NOP",  0},
    {07571, 07410, "SKP",  0},
    {07511, 07500, "SMA",  0},
    {07451, 07440, "SZA",  0},
    {07431, 07420, "SNL",  0},
    {07511, 07510, "SPA",  0},
    {07451, 07450, "SNA",  0},
    {07431, 07430, "SZL",  0},
    {07601, 07600, "CLA",  0},
    {07405, 07404, "OSR",  0},
    {07403, 07402, "HLT",  0},

    {07601, 07601, "CLA",  0},
    {07501, 07501, "MQA",  0},
    {07421, 07421, "MQL",  0},

    {00000, 00000, NULL,   0}
    };

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

int main(int, char *[])
{
    int    field, address, data, mode, c;
    struct instruction_table *p;

    setmode(fileno(stdin),  O_BINARY);

    field=address=0;
    do c=nextch(); while(!(c&0x40));            // find address or field
    for(;;){
        if(c&0x80){
            if(c&0x40){                         // field setting
                field=(c&0070)<<9;
                c=nextch();
            }else break;                        // trailing
        }else{
            if(c&0x40){                         // origin setting
                address=((c&0x3f)<<6)|(nextch()&0x3f);
                c=nextch();
            }else{                              // data
                data=((c&0x3f)<<6)|(nextch()&0x3f);
                c=nextch();
                if(c!=0x80){                    // data is not checksum
                    printf("%05o %04o ", field|address, data);
                    mode=0;
                    for(p=instructions; p->mask; p++)
                        if((data&(p->mask))==p->code){
                            printf(p->mnemonic); putchar(' ');
                            mode=p->mode;
                        }
                    if(mode==1){                // memory address
                        printf(data&00400? "I ": "  ");
                        printf("%04o",
                               (data&00177)|(data&00200? address&07600: 0));
                    }else
                    if(mode==2){                // CDF CIF field
                        printf("%04o", data&00070);
                    }
                    putchar('\n');
                    address++;
                }
            }
        }
    }

    return 0;
}
