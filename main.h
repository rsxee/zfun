

#ifndef MAIN_H
#define MAIN_H

#define UNUSED(x) (void)(x)
#define DEFAULT_BUFFER_SIZE 16384

#define local static /* zmienna tego typu nie bedzie widoczna dla innych modulow: makro latwo mozna podmienic ze */
/* static na extern w przypadku unit testow */

enum Step {
    Step_Read,
    Step_Write,
    Step_Prepare,
    Step_Compress,
    Step_End,
    Step_Error
};

#endif
