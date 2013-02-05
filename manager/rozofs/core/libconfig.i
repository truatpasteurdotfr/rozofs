%module libconfig

/*
%include "cpointer.i"

%pointer_functions(int, intp);
%pointer_functions(long, longp);
%pointer_functions(long long, llongp);
%pointer_functions(double, doublep);
*/

%{
#define SWIG_FILE_WITH_INIT
#include <libconfig.h>
%}

%include "libconfig.h"
