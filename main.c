

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <zconf.h>
#include <pthread.h>
#include <getopt.h>

#include "main.h"       /* TJ definicje i inne pierdoly w pliku nakglowkowym*/

/* czy na pewno te wszystkie zmienne sa potrzebne jako zmienne globalne ???*/
local FILE            *src = NULL;
local FILE            *dest = NULL;
local unsigned        buffer_size = DEFAULT_BUFFER_SIZE;
local int             compression_level = Z_DEFAULT_COMPRESSION;
local enum Step       step = Step_Read;
local Bytef           *buffer_in = Z_NULL;
local Bytef           *buffer_out = Z_NULL;

local pthread_t       rw, cu;
local pthread_cond_t  cond_step;
local pthread_mutex_t mutex;

local int             zflush = Z_NO_FLUSH;
local uInt            zread, zwrite;
local z_stream        strm;

/*lepsze miejsce na ta dfinicje*/
local const struct option longopts[] = {
    {"input",      required_argument, 0, 'i'},
    {"output",     required_argument, 0, 'o'},
    {"buffer",     required_argument, 0, 'b'},
    {"level",      required_argument, 0, 'l'},
    {"help",       no_argument,       0, 'h'},
    {0,            0,                 0,  0 }
};

/*TJ komentarze przed kazda funkcja co robi jakie ma argumenty i do czego sluza*/
int read_data()
{
    zread = fread(buffer_in, 1, buffer_size, src);
    if (ferror(src)) {
        step = Step_Error;
        return Z_ERRNO;
    }

    zflush = feof(src) ? Z_FINISH : Z_NO_FLUSH;

    step = Step_Prepare;
    return Z_OK;
}

/*TJ komentarze przed kazda funkcja co robi jakie ma argumenty i do czego sluza*/
int write_data()
{
    if (fwrite(buffer_out, 1, zwrite, dest) != zwrite || ferror(dest)) {
        step = Step_Error;
        return Z_ERRNO;
    }

    if (strm.avail_out != 0) {
        if (zflush == Z_FINISH) {
            step = Step_End;
        } else {
            step = Step_Read;
            read_data();
        }
    } else {
        step = Step_Compress;
    }

    return Z_OK;
}

/*TJ komentarze przed kazda funkcja co robi jakie ma argumenty i do czego sluza*/
int compress_data()
{
    strm.avail_out = buffer_size;
    strm.next_out = buffer_out;

    deflate(&strm, zflush);     /*TJ. Nie przewidujesz zadnych bledow z deflata?*/
    zwrite = buffer_size - strm.avail_out;

    step = Step_Write;
    return Z_OK;
}

/*TJ komentarze przed kazda funkcja co robi jakie ma argumenty i do czego sluza*/
void prepare_data()
{
    strm.avail_in = zread;
    strm.next_in = buffer_in;

//    compress_data();      /*skoro zwracasz tylko Z_OK to po co co kolwiek zwracac*/
//    return Z_OK;          /* to jest przygotowanie kompresji wiec nie wywoluj tego tutaj)
}

/*TJ komentarze przed kazda funkcja co robi jakie ma argumenty i do czego sluza*/
void* th_readwrite(void* p)
{
    UNUSED(p);

    while (1) {
        pthread_mutex_lock(&mutex);

        switch (step) {
            case Step_Read:
                read_data();
                break;
            case Step_Write:
                write_data();
                break;
            case Step_End:
                pthread_mutex_unlock(&mutex);
                goto exit;      /*TJ w C nie uzywamy goto*/
            case Step_Error:
                pthread_cond_signal(&cond_step);
                pthread_mutex_unlock(&mutex);
                goto exit;
            default:
                break;
        }

        pthread_cond_signal(&cond_step);
        pthread_cond_wait(&cond_step, &mutex);
        pthread_mutex_unlock(&mutex);
    }

exit:
    (void)deflateEnd(&strm);
    pthread_exit(NULL);
}


/*TJ komentarze przed kazda funkcja co robi jakie ma argumenty i do czego sluza*/
void* th_compress(void* p)
{
    UNUSED(p);

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    int ret = deflateInit(&strm, compression_level);
    if (ret != Z_OK) {
        pthread_exit(NULL);
    }

    while (1) {
        pthread_mutex_lock(&mutex);

        switch (step) {
        case Step_Prepare:
            prepare_data();     /*teraz sam wejdzie po prepare do compress*/

        case Step_Compress:
            compress_data();
            break;

        case Step_End:
        case Step_Error:
            deflateEnd(&strm);      /*wymagane przez API ZLIB'a*/
            pthread_cond_signal(&cond_step);
            pthread_mutex_unlock(&mutex);
            goto exit;      /*TJ w C nie uzywamy goto*/

        default:
            break;
        }

        pthread_cond_signal(&cond_step);
        pthread_cond_wait(&cond_step, &mutex);
        pthread_mutex_unlock(&mutex);
    }

exit:
    pthread_exit(NULL);
}

/*TJ komentarze przed kazda funkcja co robi jakie ma argumenty i do czego sluza*/
void print_usage()
{
    fputs("OPTIONS\n"
          "\t-i INPUT --input=INPUT\n"
          "\t\tSet INPUT as input file (Mandatory)\n\n"
          "\t-o OUTPUT --output=OUTPUT\n"
          "\t\tSet OUTPUT as output file (Mandatory)\n\n"
          "\t-b BUFFER --buffer=BUFFER\n"
          "\t\tSet buffer size to BUFFER\n\n"
          "\t-l LEVEL --level=LEVEL\n"
          "\t\tSet compression level to LEVEL\n\n"
          "\t-h --help\n"
          "\t\tPrint this text\n", stderr);
}

int main(int argc, char* argv[])
{
    int opt, long_index = 0;    /*TJ staraj sie umieszczac deklaracje zmiennych na poczatku maina - piszemy w C*/

    if (argc == 1) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    while ((opt = getopt_long(argc, argv, "i:o:b:l:h",
                              longopts, &long_index)) != -1) {
        switch (opt) {
            case 'i':
                src = fopen(optarg, "r");
                if (!src) {
                    fputs("Cannot open input file!", stderr);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'o':
                dest = fopen(optarg, "w");
                if (!dest) {
                    fputs("Cannot open output file!", stderr);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'b':
                buffer_size = atoi(optarg);
                break;
            case 'l':
                compression_level = atoi(optarg);
                if (compression_level < 0 || compression_level > 9) {   /*TJ nie uzywaj magic numer'ow*/
                    compression_level = Z_DEFAULT_COMPRESSION;
                    fputs("Valid range for compression level is 0-9,"
                          " setting default level.\n", stderr);
                }
                break;
            case 'h':
            default:
                print_usage();
                exit(EXIT_FAILURE);
        }
    }

    if (src == NULL || dest == NULL) {
        fputs("Input and output file parameters are mandatory!\n", stderr);
        exit(EXIT_FAILURE);
    }

    buffer_in = malloc(buffer_size);
    if (!buffer_in) {
        fputs("Input buffer allocation failed!\n", stderr);
        exit(EXIT_FAILURE);
    }
    buffer_out = malloc(buffer_size);
    if (!buffer_out) {
        fputs("Output buffer allocation failed!\n", stderr);
        exit(EXIT_FAILURE);
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    pthread_cond_init(&cond_step, NULL);
    pthread_mutex_init(&mutex, NULL);


    pthread_create(&cu, &attr, th_readwrite, NULL);
    pthread_create(&rw, &attr, th_compress, NULL);
    pthread_join(rw, NULL);
    pthread_join(cu, NULL);


    fclose(src);
    fclose(dest);

    pthread_cond_destroy(&cond_step);
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&mutex);
    pthread_exit(NULL);
}
