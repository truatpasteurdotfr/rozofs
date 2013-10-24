/*
 * Copyright (c) 2013 Fizians SAS. <http://www.fizians.com>
 * This file is part of Rozofs.
 *
 * Rozofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, version 2.
 *
 * Rozofs is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */


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
