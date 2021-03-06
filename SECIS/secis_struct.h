/*
  Last changed Time-stamp: <2006-07-17 11:37:26 xtof>
  $Id: secis_struct.h,v 1.1 2006/07/18 14:08:49 xtof Exp $
*/
/***************************************************************************
                    struct.h  -  functions dealing with the structure
                             -------------------
    begin                : 23-03-2006
    copyright            : (C) 2006 by Anke Busch
    email                : abusch@informatik.uni-freiburg.de
***************************************************************************/

#ifndef _STRUCT__
#define _STRUCT__

#include <stdlib.h>
#include "secis_basics.h"

using namespace std;

int check_brackets(char *line);
int* make_BasePair_Table(char *structure);

#endif   // _STRUCT_
