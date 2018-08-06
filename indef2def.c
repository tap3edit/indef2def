/****************************************************************************
|*
|* tap3edit Tools (http://www.tap3edit.com)
|*
|* Copyright (c) 2007-2018, Javier Gutierrez <https://github.com/tap3edit/indef2def>
|*
|* Permission to use, copy, modify, and/or distribute this software for any
|* purpose with or without fee is hereby granted, provided that the above
|* copyright notice and this permission notice appear in all copies.
|* 
|* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
|* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
|* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
|* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
|* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
|* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
|* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.|*
|*
|*
|* Module: indef2def.c
|*
|* Description: Convert a file with indefinite length into one with definite length.
|*
|* Return:
|*      0: successful
|*      1: error
|*
|*
|*
|* Author: Javier Gutierrez (JG)
|*
|* Modifications:
|*
|* When        Who    Pos.    What
|* 20050726    JG             Initial version
|*
****************************************************************************/

/* 1. Includes */

#include<stdio.h>
#include<stdlib.h>
#include<ctype.h>
#include<string.h>
#include<errno.h>


/* 2. Defines */

#ifndef TRUE
    #define FALSE 0
    #define TRUE (!FALSE)
#endif


/* 3. Typedefs and structures */

typedef unsigned char uchar;
typedef struct _asn1item
{
    unsigned    class: 2;       /* Class */
    unsigned    pc: 1;          /* Primitive/Constructed */
    int         tag;            /* Tag: decimal format */
    uchar       tag_x[4];       /* Tag: bcd format */
    char        tag_h[9];       /* Tag: hexadecimal string format */
    int         tag_l;          /* Tag: number of bytes in file */
    long        size;           /* Size: decimal format */
    uchar       size_x[8];      /* Size: bcd format */
    char        size_h[17];     /* Size: hexadecimal format */
    int         size_l;         /* Size: number of bytes in file */
} asn1item;

typedef struct _indef_len_item
{
    long        pos;            /* Position into the file where the length item begins */
    long        len;            /* Indefinite length inclusive \0\0 */
    long        len_def;        /* Indefinite length exclusive \0\0 */
    struct _indef_len_item *next;
} indef_len_item;


/* 4. Prototypes */

int     write_tap       (FILE *file, FILE *outfile, long size, indef_len_item **len_list);
int     decode_size     (FILE *file, asn1item *a_item);
int     decode_tag      (FILE *file, asn1item *a_item);
int     collect_indef   (FILE *file, long size, indef_len_item *len_list, long *len, long *len_def);
void    bcd_2_hexa      (char *str2, const uchar *str1, const int len);
int     encode_size     (uchar *size2,int size1, int *len);

void    dump_indef      (indef_len_item* len_list);

/* 5. Global Variables */

long pos=0;         /* Current position in file. */
int  all_file=0;    /* Converts all file */


main(int argc, char **argv)
{
    FILE*               file, *outfile;
    char*               inFilename, *outFilename;
    indef_len_item*     len_list;
    long                len_tmp=0, len_def_tmp=0;
    long                size=0;


    /* 1. Checking parameters */

    if (argc != 3 && argc != 4)
    {
        fprintf(stderr, "Copyright (c) 2016 Javier Gutierrez. (http://www.tap3edit.com)\n");
        fprintf(stderr, "Usage: %s [ -a ] infilename outfilename\n", argv[0]);
        fprintf(stderr, "   -a : converts all file\n");
        exit(1);
    }

    if (strcmp(argv[1], "-a") == 0)
    {
        all_file = 1;
        argv++;
    }

    inFilename=argv[1];
    outFilename=argv[2];


    /* 2. Open Input Files */
    
    if ( ( file=fopen(inFilename, "rb") ) == NULL )
    {
        fprintf(stderr, "Cannot open file %s\n", inFilename);
        exit(1);
    }

    if ( ( outfile=fopen(outFilename, "wb") ) == NULL )
    {
        fprintf(stderr, "Cannot open file %s\n", outFilename);
        exit(1);
    }


    /* 3. Get file size */

    if (fseek(file, 0, SEEK_END) != 0) // seek to end of file
    {
        fprintf(stderr, "Error moving to the end of the file: %s\n", strerror(errno));
        exit(1);
    }
    size = ftell(file); // get current file pointer
    if (fseek(file, 0, SEEK_SET) != 0) // seek back to beginning of file
    {
        fprintf(stderr, "Error moving to the beginning of the file: %s\n", strerror(errno));
        exit(1);
    }


    /* 4. Find all indefinite lengths */

    if ( ( len_list=(indef_len_item*)malloc(sizeof(indef_len_item)) ) == NULL )
    {
        fprintf(stderr, "Problems allocating memory\n");
        exit(1);
    }
    memset(len_list, 0x00, sizeof(indef_len_item));

    if (collect_indef(file,all_file?size:-1, len_list, &len_tmp, &len_def_tmp) == -1 )
    {
        fprintf(stderr, "Error decoding file\n");
        exit(1);
    }


    /* 5. Decode and prints file */

    pos=0;

    rewind(file);

    if ( (write_tap(file,outfile,all_file?size:1,&len_list) )==-1 )
    {
        fprintf(stderr, "Error decoding file\n");
        exit(1);
    }


    /* 6. Closing and End. */
    if (len_list)
    {
        free(len_list);
    }

    fclose(file);
    fclose(outfile);
}

/****************************************************************************
|* 
|* Function: write_tap
|* 
|* Description; 
|* 
|*     Here we write the file again but with definite length.
|* 
|* Return:
|*      0: Successful
|*     -1: Error decoding
|* 
|* 
|* Author: Javier Gutierrez (JG)
|* 
|* Modifications:
|* 20050726    JG    Initial version
|* 
****************************************************************************/
int write_tap(
    FILE*               file,           /* File handler to decode */
    FILE*               outfile,        /* File handler to write */
    long                size,           /* Size within to decode */
    indef_len_item**    len_list        /* List of indefinite length */
)
{
    asn1item            a_item;
    int                 indef_flag;
    indef_len_item*     len_list_free;
    long                size_indef;

    /* 1. Process all size received from our parent */

    while (size >0)
    {

        indef_flag=0;


        /* 1.1. TAG:   decode */

        if (decode_tag(file, &a_item)==-1)
        {
            fprintf(stderr, "Error decoding tag at position: %d\n", pos);
            return -1;
        }

        size-=a_item.tag_l;


        /* 1.2. SIZE:  decode */

        if (decode_size(file, &a_item)==-1)
        {
            fprintf(stderr, "Error decoding size at position: %d\n", pos);
            return -1;
        }

        size-=a_item.size_l;
        size_indef=a_item.size;


        /* 1.3. Did we find 2 null bytes? */

        if ( !a_item.tag && !a_item.size )
        {

            /* 1.3.1. End of indefinite length found */

            break;

        }


        /* 1.4. VALUE: collect indef sizes inside our value */

        { 
            int i;

            /* 1.4.1. Arrange if indefinite Length */

            if ( !a_item.size && a_item.size_x[0] )
            {
                if ( pos != (*len_list)->pos )
                {
                    fprintf(stderr, "Mismatch with list of indefinite Length.  pos: %d, (*len_list)->pos: %d\n", pos, (*len_list)->pos );
                    return -1;
                }

                a_item.size=(*len_list)->len_def;
                size_indef=(*len_list)->len;

                len_list_free=*len_list;
                *len_list=(*len_list)->next;
                free(len_list_free);

                indef_flag=1;

            }


            /* 1.4.2. Proceed according to Primitive and Constructed Tag */

            if (!a_item.pc)
            {
                /* 1.4.2.1. Primitive */

                {
                    /* 1.4.2.1.1 Write */

                    fwrite(a_item.tag_x, a_item.tag_l, 1, outfile);
                    fwrite(a_item.size_x, a_item.size_l, 1, outfile);

                    for(i=0;i<a_item.size;i++)
                    {
                        fputc(fgetc(file), outfile);
                        if(feof(file))
                        {
                            fprintf(stderr, "Found end of file too soon at position: %d\n", pos);
                            return -1;
                        }
                    }

                }

                pos+=a_item.size;

            }
            else
            {
                /* 1.4.2.2. Constructed */

                {
                    /* 1.4.2.2.1 Write */

                    if( (encode_size(a_item.size_x, a_item.size, &(a_item.size_l)))==-1)
                        return -1;

                    fwrite(a_item.tag_x, a_item.tag_l, 1, outfile);
                    fwrite(a_item.size_x, a_item.size_l, 1, outfile);

                }
                
                
                if ( (write_tap(file,outfile,size_indef,len_list ) )==-1 )
                    return -1;

            }

            size-=size_indef;

        }

    }

    return 0;
}


/****************************************************************************
|* 
|* Function: collect_indef
|* 
|* Description; 
|* 
|*     recursive function to find and stack all indef length in the file
|* 
|* Return:
|*      int > -1: Size of scanned item
|*     -1: Error decoding
|* 
|* 
|* Author: Javier Gutierrez (JG)
|* 
|* Modifications:
|* 20050726    JG    Initial version
|* 
****************************************************************************/
int collect_indef(
    FILE*               file,           /* File handler */
    long                size,           /* Size of parent. If 0, parent has indefinite length */
    indef_len_item*     len_list,       /* List of indefinite length */
    long*               len,            /* To store indefinite Length */
    long*               len_def         /* To store definite Length */
)
{
    int         parent_indef=FALSE,head=FALSE,i;
    long        tot_size=0, tot_size_def=0, len_tmp=0, len_def_tmp=0;
    asn1item    a_item;
    uchar       buffin, buffin_str[4];


    /* 1. We need to identify if this is the head, or if our parent has indefinite length for our loop */

    if (size==-1)
        head=TRUE;
    else
        parent_indef=!size ? TRUE : FALSE;


    /* 2. Process all size received from our parent OR 'til a \0\0 is found */


    while ( parent_indef || size > 0 || head )
    {
        /* 2.1. TAG:   decode */

        if (decode_tag(file, &a_item)==-1)
        {
            fprintf(stderr, "Error decoding tag at position: %d\n", pos);
            return -1;
        }

        tot_size+=a_item.tag_l;
        tot_size_def+=a_item.tag_l;
        size-=a_item.tag_l;


        /* 2.2. SIZE:  decode */

        if (decode_size(file, &a_item)==-1)
        {
            fprintf(stderr, "Error decoding size at position: %d\n", pos);
            return -1;
        }

        tot_size+=a_item.size_l;
        tot_size_def+=a_item.size_l;
        size-=a_item.size_l;


        /* 2.3. Did we find 2 null bytes? */

        if ( !a_item.tag && !a_item.size )
        {

            /* 2.3.1. End of indefinite length found */

            tot_size_def-=2;
            break;
            
        }


        /* 2.4. VALUE: collect indef sizes inside our value */

        if ( a_item.size || !a_item.size_x[0] )
        {
            /* 2.4.1. Definite Length */

            if (a_item.pc)
            {
                /* 2.4.1.1. Constucted */

                while(!(!len_list->len && len_list->next==NULL))
                {
                    len_list=len_list->next;
                }

                if (a_item.size)
                    if( (collect_indef(file,a_item.size, len_list, &len_tmp, &len_def_tmp) ) == -1 )
                        return -1;

                tot_size+=len_tmp;
                tot_size_def+=len_def_tmp;
                
            }
            else
            {
                /* 2.4.1.2. Primitive */

                for (i=0;i<a_item.size;i++)
                {
                    buffin=fgetc(file);
                    if(feof(file))
                    {
                        fprintf(stderr, "Found end of file too soon at position: %d\n", pos);
                        return -1;
                    }
                }
                pos+=a_item.size;
                tot_size+=a_item.size;
                tot_size_def+=a_item.size;
            }


        }
        else
        {
            /* 2.4.2. Indefinite Length */

            if (!a_item.pc)
            {
                /* 2.4.2.1. Primitive !!?? */
                fprintf(stderr, "Primitive Tag item with Indefinite Length at pos: %d\n", pos );
                return -1;
            }

            while(!(!len_list->len && len_list->next==NULL))
            {
                len_list=len_list->next;
            }

            if ( ( len_list->next=(indef_len_item*)malloc(sizeof(indef_len_item)) ) == NULL )
            {
                fprintf(stderr, "Problems allocating memory\n");
                return -1;
            }
            memset(len_list->next, 0x00, sizeof(indef_len_item));

            len_list->pos=pos;

            if ( (collect_indef(file,a_item.size, len_list->next, &len_tmp, &len_def_tmp) )==-1 )
                return -1;

            tot_size+=len_list->len=len_tmp;
            len_list->len_def=len_def_tmp;

            if( ( len_def_tmp+=encode_size( buffin_str, len_def_tmp, (int *)&len_tmp)-1 ) == -1 )
                return -1;

            tot_size_def+=len_def_tmp;
    
        }


        /* 2.5. The HEAD Tag will be executed just once */

        if (head)
        {
            if (!a_item.size)
            {
                pos+=2;
                tot_size+=2;
                tot_size_def+=2;
            }
            break;
                
        }

        size-=tot_size;

    }


    /* 3. End */

    *len=tot_size;
    *len_def=tot_size_def;
    return 0;
}



/****************************************************************************
|* 
|* Function: decode_tag
|* 
|* Description; 
|* 
|*     decodes tag into a asn1item
|* 
|* Return:
|*      0: Successful
|*     -1: Error decoding
|* 
|* 
|* Author: Javier Gutierrez (JG)
|* 
|* Modifications:
|* 20050715    JG    Initial version
|* 
****************************************************************************/
int decode_tag(
    FILE*           file,         /* File handler */
    asn1item*       a_item        /* pointer asn1item where to store the information */
)
{
    uchar           buffin;
    int             i;


    a_item->tag=0;

    /* 1. Read from file */

    buffin=fgetc(file);
    if(feof(file))
    {
        fprintf(stderr, "Found end of file too soon at position: %d\n", pos);
        return -1;
    }
    pos++;


    /* 2. Store class, primitive/constructed info and hexa tag */

    a_item->class=buffin>>6;
    a_item->pc=(buffin>>5)&0x1;
    a_item->tag_x[0]=buffin;
    a_item->tag_l=1;


    /* 3. Work according to number of tag octets */

    if ( ( buffin&0x1F ) == 0x1F )
    {
        /* 3.1 Tag has more than one octet */

        for(i=1;i<=4;i++) 
        {
            buffin=fgetc(file);
            if(feof(file))
            {
                fprintf(stderr, "Found end of file too soon at position: %d\n", pos);
                return -1;
            }
            pos++;

            a_item->tag<<=7;
            a_item->tag+=(int)(buffin&0x7F);
            a_item->tag_x[i]=buffin;
            a_item->tag_l+=1;

            if ( (buffin>>7) == 0 ) 
                break;

        }

        if ( i>3 )
        {
            fprintf(stderr, "Found tag bigger than 4 bytes at position: %d\n", pos);
            return -1;
        }

    }
    else
    {
        /* 3.2 Tag has just one octet */

        a_item->tag=(int)buffin&0x1F;
    }

    bcd_2_hexa(a_item->tag_h, a_item->tag_x, a_item->tag_l);

    return 0;

}


/****************************************************************************
|* 
|* Function: decode_size
|* 
|* Description; 
|* 
|*     decodes size into a asn1item
|* 
|* Return:
|*      0: Successful
|*     -1: Error decoding
|* 
|* 
|* Author: Javier Gutierrez (JG)
|* 
|* Modifications:
|* 20050715    JG    Initial version
|* 
****************************************************************************/
int decode_size(
    FILE*           file,         /* File handler */
    asn1item*       a_item        /* pointer asn1item where to store the information */
)
{
    uchar           buffin;
    int             i;

    
    a_item->size=0;


    /* 1. Read from file */

    buffin=fgetc(file);
    if(feof(file))
    {
        fprintf(stderr, "Found end of file too soon at position: %d\n", pos);
        return -1;
    }
    pos++;


    /* 2. Storing size_x */

    a_item->size_x[0]=buffin;
    a_item->size_l=1;


    /* 3. Work according the number of octets */

    if (buffin>>7)
    {
        /* 3.1. Size with more than one octet */

        for(i=1;(i<=(int)(a_item->size_x[0]&0x7F)) && (i<=4);i++)
        {
            buffin=fgetc(file);
            if(feof(file))
            {
                fprintf(stderr, "Found end of file too soon at position: %d\n", pos);
                return -1;
            }
            pos++;

            a_item->size<<=8;
            a_item->size+=(int)buffin;
            a_item->size_x[i]=buffin;
            a_item->size_l+=1;
        }

        if ( i>7 )
        {
            fprintf(stderr, "Found size bigger than 8 bytes at position: %d\n", pos);
            return -1;
        }

    }
    else
    {
        /* 3.2. Size with just one octet */

        a_item->size=(int)(buffin);
    }

    bcd_2_hexa(a_item->size_h, a_item->size_x, a_item->size_l);

    return 0;

}


/****************************************************************************
|* 
|* Function: bcd_2_hexa
|* 
|* Description; 
|* 
|*     Converts a bcd chain into an hexadecimal string
|* 
|* Return:
|*      void
|* 
|* 
|* Author: Javier Gutierrez (JG)
|* 
|* Modifications:
|* 20050719    JG    Initial version
|* 
****************************************************************************/
void bcd_2_hexa(
    char*           str2,       /* String to store the converted value */
    const uchar*    str1,       /* String to convert */ 
    const int       len         /* Because the string can contain \0 we cannot use strlen() */
)
{
    int     i;
    
    for (i=0;i<len;i++)
        sprintf(&str2[i*2],"%02x",str1[i]);

}


/****************************************************************************
|* 
|* Function: encode_size
|* 
|* Description; 
|* 
|*     Encode a size in ASN1 format
|* 
|* Return:
|*     *len
|*     -1: Error
|* 
|* Author: Javier Gutierrez (JG)
|* 
|* Modifications:
|* 20050726    JG    Initial version
|* 
****************************************************************************/
int encode_size(
    uchar*      size2,      /* Where to store the encoded size */
    int         size1,      /* size in integer format */
    int*        len         /* Where to store the length of the new size */
)
{

    int i, size1_cpy=size1, size1_oct=0;

    if (size1>>7)
    {
        /* 1. Size with more than one octet */

        while(size1_cpy)
        {
            /* 1.2. Check how many bytes got our size */
            size1_cpy>>=8;
            size1_oct++;
        }

        if (size1_oct>7)
        {
            fprintf(stderr, "Found size bigger than 8 bytes at position: %d\n", pos);
            return -1;
        }

        for (i=size1_oct; (i>=1);i--)
        {
            size2[i]=(uchar)size1&0xFF;
            size1>>=8;
        }
        size2[0]=(uchar)(0x80|size1_oct);
        *len=size1_oct+1;
    }
    else
    {
        /* 2. Size with just one octet */

        size2[0]=(uchar)size1;
        *len=1;
    }

    return *len;

}

/****************************************************************************
|* 
|* Function: dump_indef
|* 
|* Description; 
|* 
|*     List the tree of indefinite lengths
|* 
|* Return:
|*      void
|* 
|* 
|* Author: Javier Gutierrez (JG)
|* 
|* Modifications:
|* 20160308    JG    Initial version
|* 
****************************************************************************/
void dump_indef(
    indef_len_item*     len_list        /* List of indefinite length */
    )
{
    int i = 0;

    while(len_list)
    {
        printf("pos: %6ld, len: %6ld, len_def: %6ld\n", 
                len_list->pos,
                len_list->len,
                len_list->len_def);
        len_list = len_list->next;
    }
}
