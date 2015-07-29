#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <zconf.h>
#include <pthread.h>
#include <getopt.h>

#define UNUSED(x) (void)(x)
#define DEFAULT_BUFFER_SIZE 16384

enum Step {
    Step_Read,
    Step_Write,
    Step_Prepare,
    Step_Compress,
    Step_End,
    Step_Error
};

FILE *src = NULL, *dest = NULL;
unsigned buffer_size = DEFAULT_BUFFER_SIZE;
int compression_level = Z_DEFAULT_COMPRESSION;
enum Step step = Step_Read;
Bytef *buffer_in = Z_NULL;
Bytef *buffer_out = Z_NULL;


pthread_t rw, cu;
pthread_cond_t cond_step;
pthread_mutex_t mutex;


int zflush = Z_NO_FLUSH;
uInt zread, zwrite;
z_stream strm;

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

int compress_data()
{
    strm.avail_out = buffer_size;
    strm.next_out = buffer_out;

    deflate(&strm, zflush);
    zwrite = buffer_size - strm.avail_out;

    step = Step_Write;
    return Z_OK;
}

int prepare_data()
{
    strm.avail_in = zread;
    strm.next_in = buffer_in;

    compress_data();
    return Z_OK;
}

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
                goto exit;
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
                prepare_data();
                break;
            case Step_Compress:
                compress_data();
                break;
            case Step_End:
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
    pthread_exit(NULL);
}

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
    if (argc == 1) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    const struct option longopts[] = {
        {"input",      required_argument, 0, 'i'},
        {"output",     required_argument, 0, 'o'},
        {"buffer",     required_argument, 0, 'b'},
        {"level",      required_argument, 0, 'l'},
        {"help",       no_argument,       0, 'h'},
        {0,            0,                 0,  0 }
    };

    int opt, long_index = 0;
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
                if (compression_level < 0 || compression_level > 9) {
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
