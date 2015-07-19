
/* 
 *	ALLMAXMSP.h
 *	AUTHOR:			Daniel Bennett
 *	DATE:			03/07/2015
 *	DESCRIPTION:	Universal Header for all my MAX MSP Dev
 *
 */

#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>					
#include "ext.h"
#include "z_dsp.h"
#include "jit.math.h"
#include "ext_obex.h"

double danScaler(t_double x, t_double in_l, t_double in_h, t_double out_l, t_double out_h, t_double pow);
double danScaler_lin(t_double x, t_double in_l, t_double in_h, t_double out_l, t_double out_h);


double danScaler(t_double x, t_double in_l, t_double in_h, t_double out_l, t_double out_h, t_double pow)
{
	// convenient parameter scaling function for exponential curves. Won't do linear.
	if(x<in_l) x=in_l;
	else if(x>in_h) x=in_h;
	if(pow==0) pow=0.00001;

	double sc = (x-in_l)/(in_h-in_l);
	double ex = (exp(sc*pow) - 1) / (exp(pow) -1);
	return (sc*(out_h-out_l)*ex) + out_l;
}

double danScaler_lin(t_double x, t_double in_l, t_double in_h, t_double out_l, t_double out_h)
{
	// convenient parameter scaling function for linear scaling. Won't do curves.
	if(x<in_l) x=in_l;
	else if(x>in_h) x=in_h;

	return ((x-in_l)*(x-in_l)/(in_h-in_l)*(out_h-out_l)/(in_h-in_l)) + out_l;
}

