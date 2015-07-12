/* 
	AUTHOR:			Daniel Bennett (skjolbrot@gmail.com)
	DATE:			24/01/2015
	DESCRIPTION:	Multiple interacting Peter Blasser style "Bounds 
					and Bounce" triangle oscillators.
					cursor, imagined as a ball moving along vector (s,s)
					with s defined by user
					y component reverses sign when the cursor reaches bounds.
					At fixed bounds (-1..1) behaviour for a single voice 
					instance is as normal, crude (non bandlimited ) triangle 
					oscillator. But then why would you use it that way?
					for want of a more meaningful value, rate input is the pitch 
					for that specific case
		
	Version:		0.3
	
	DevNotes:		Having made the PTR calcs optional, triangle symmetry calcs
					are probably the highest single overhead remaining. I tried
					moving these outside of the audio loop but found that the
					constantly changing width neccessitates checking that gradient
					is within limits & conditionally recalculating on a sample-by-sample
					basis; so the potential gains here are minimal (@ optimisation -O3),
					& cost in code complexity is considerable. However, if anyone
					finds an elegant solution to improving efficiency of the calcs
					I'd be really interested to see.
					Probably (for x86 processors) int truncation is worth optimising?


	TODO:	
		- improve efficiency of waveshaping (better mechanism)

		- Then move on to db.friction 
				output modes - energy @ each collision
							 - waveforms (as now)
				control for friction per ball
*/


#include "ALL_MAXMSP.h"

#define sign(a) ( ( (a) < 0 )  ?  -1   : ( (a) > 0 ) )

#define MAX_VOICES 10
#define THINNESTPIPE 0.0044		// the smallest distance allowed between bounds
#define DCBLOCK_GAIN 0.998		// Steepness of DC block filter 
#define SYMMMIN 0.001
#define SYMMMAX 0.999
#define FMIN 0.001
#define FMAX 15000.f
#define MAXFM 40
#define LKTBL_LNGTH 2048

#define DEBUG_ON 0
#define POLL_PER_SAMPLES 10000	// debugging - report at this number of sample calculations
#define POLL_NO_SAMPLES 1028	// debugging - report this number of sample calculations

#if DEBUG_ON == 0
	#define dan_debug_f(label, var) ;//nowt
	#define dan_debug_d(label, var) ;//nowt
	#define dan_debug_cntr() ;//nowt
#endif
#if DEBUG_ON == 1
	#define dan_debug_f(label, var) if(x->poll_count < POLL_NO_SAMPLES && x->curr_v == 0 && x->stopdebug != 1){ post(#label " = %f", var);};
	#define dan_debug_d(label, var) if(x->poll_count < POLL_NO_SAMPLES && x->curr_v == 0 && x->stopdebug != 1){ post(#label " = %i", var);};
	#define dan_debug_cntr() if(x->poll_count == 0 && x->curr_v == 0 && x->stopdebug != 1){ x->poll_count = POLL_PER_SAMPLES-1; post("-*******************END SEQUENCE*****************-"); } else if (x->curr_v == 0) {x->poll_count--; x->debug_count++ ;};
#endif
#if DEBUG_ON == 2
#define dan_debug_f(label, var) if(x->poll_count < POLL_NO_SAMPLES && x->curr_v == 0 ){ post(#label " = %f", var);};
#define dan_debug_d(label, var) if(x->poll_count < POLL_NO_SAMPLES && x->curr_v == 0 ){ post(#label " = %i", var);};
#define dan_debug_cntr() if(x->poll_count == 0 && x->curr_v == 0 ){ x->poll_count = POLL_PER_SAMPLES-1; post("-*******************END SEQUENCE*****************-");} else if(x->curr_v == 0 ){x->poll_count--;};
#endif


typedef struct _bounce {
	t_pxobject	obj;			
	t_double  srate;
	t_double  slx4; // sample len * 4 
	t_double  fmax;

	t_double	  bound_lo;		// lower bound for entire ensemble
	t_double	  bound_hi;		// upper bound for entire ensemble
	t_double	  **hz;			// "master" pitch for each voice
	t_double	  *hzFloat;
	t_double	  **symm;		// symmetry (per voice)
	t_double  *grad;		// variables for optimising non-signal rate calcs of symm
	t_double  *gradb;
	t_double  *aOverb;
	t_double  *bOvera;

	t_double  *ball_loc;	// location of the ball
	t_int	  *direction;	// current direction of ball (-1/1)
	t_double	  **fm;			// 2d matrix controling cross modulation between voices
	t_double	  *shape;
	t_double	  **out;		// output pointer
	t_double  *sin;			// sine wavetable
	t_double  *sinh;		// hyperbolic sine wavetable

	t_bool	  *dcblock_on;
	t_double	  *dc_prev_in;	// history for dcblock
	t_double	  *dc_prev_out;

	t_int	  *hz_conn;		// track inlet signal connection
	t_int	  *symm_conn;		
	t_int	  bound_lo_conn;	
	t_int	  bound_hi_conn;	
	t_int	  mode;

	t_int	  voice_count;	
	t_int	  curr_v;
#if DEBUG_ON == 1 || DEBUG_ON == 2
	t_int poll_count;	// DEBUG
	t_int stopdebug;	// DEBUG
	t_int debug_count;	// DEBUG
#endif
	t_bool fm_on; // controls whether cross modulation is on or off (saves computation)
}	t_bounce;

static t_class *bounce_class;	// pointer to the class of this object

// MSP infrastructure functions
void	*bounce_new(t_symbol *s, short argc, t_atom *argv);
void	bounce_dsp64(t_bounce *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void	bounce_assist(t_bounce *x, void *b, long msg, long arg, char *dst);	
void	bounce_dsp_free(t_bounce *x);

// handle incoming symbols
void	bounce_bang(t_bounce *x, double f);		
void	bounce_float(t_bounce *x, double f);	
void	bounce_dcblock_set(t_bounce *x, t_symbol *msg, short argc, t_atom *argv);
void	bounce_fm_set(t_bounce *x, t_symbol *msg, short argc, t_atom *argv);
void	bounce_shape_set(t_bounce *x, t_symbol *msg, short argc, t_atom *argv);
void	bounce_fmax_set(t_bounce *x, t_symbol *msg, short argc, t_atom *argv);


// Audio Calc functions
void 	bounce_PerformWrapper(t_bounce *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam);
void 	bounce_perform64(t_bounce *x, double **ins, double **outs, long sampleframes, void (*voicemode)(void *, t_double, t_double, t_double, t_double));
void	 bounce_ptr_voicecalc (t_bounce *x, t_double lo, t_double hi, t_double grad, t_double t);
void 	bounce_shaper_voicecalc (t_bounce *x, t_double lo, t_double hi, t_double grad, t_double t);

t_double  bounce_dcblock(t_double input, t_double *lastinput, t_double *lastoutput, t_double gain);
t_double bounce_fmcalc (t_bounce *x, t_int curr_voice);
t_double ptr_correctmax(t_double p, t_double a, t_double b, t_double t, t_double pmin, t_double pmax);
t_double ptr_correctmin(t_double p, t_double a, t_double b, t_double t, t_double pmin, t_double pmax);
void	 setup_lktables (t_bounce* x, t_int shape);
double	 do_shaping (t_bounce *x, t_double lo, t_double hi) ;
double	 bounce_alimit(double a, double width, double t);

// my infrastructure functions
double infr_scale_param(double in, double in_min, double in_max, double out_min, double out_max);
void	bounce_fm_onoff(t_bounce *x, t_symbol *msg, short argc, t_atom *argv);


/************************************************************

!!!!!!!!!!!!	MSP INFRASTRUCTURE FUNCTIONS	!!!!!!!!!!!!

*************************************************************/

// initialization routine 
int C74_EXPORT main (void)
{
	bounce_class = class_new("db.bounce~", (method)bounce_new, (method)bounce_dsp_free, 
		sizeof(t_bounce), 0L, A_GIMME, 0);

	// register methods to handle incoming messages
	class_addmethod(bounce_class, (method)bounce_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(bounce_class, (method)bounce_assist, "assist", A_CANT, 0);
	class_addmethod(bounce_class, (method)bounce_bang, "bang", A_FLOAT, 0);
	class_addmethod(bounce_class, (method)bounce_float, "float", A_FLOAT, 0);
	class_addmethod(bounce_class, (method)bounce_dcblock_set, "dc", A_GIMME, 0);
	class_addmethod(bounce_class, (method)bounce_fm_set, "fm", A_GIMME, 0);
	class_addmethod(bounce_class, (method)bounce_fm_onoff, "fmoff", A_GIMME, 0);
	class_addmethod(bounce_class, (method)bounce_shape_set, "shape", A_GIMME, 0);
	class_addmethod(bounce_class, (method)bounce_fmax_set, "fmax", A_GIMME, 0);
	

	class_dspinit(bounce_class);
	class_register(CLASS_BOX, bounce_class);

	post("db.bounce~ by Daniel Bennett skjolbrot@gmail.com");
	post("- Peter Blasser inspired Triangle \"Bounce & Bounds\" oscillators");
	post("args:- 1) no of voices (default 1 - henceforth \"n\") ");
	post("args:- 2) Lower bound for voice 1 (default -1)");
	post("args:- 3) Upper bound for voice n (default 1) ");
	post("args:- 4) mode - 0: waveshaping 1: antialiased triangle (via ptr) ");

	// report to the MAX window
	return 0;
}

void bounce_dsp_free (t_bounce *x)
{
	int i;

	dsp_free((t_pxobject *)x);

	t_freebytes(x->hz, x->voice_count * sizeof(t_double *));
	t_freebytes(x->out, x->voice_count * sizeof(t_double *));
	t_freebytes(x->symm, x->voice_count * sizeof(t_double *));
	t_freebytes(x->hzFloat, x->voice_count * sizeof(t_double ));
	t_freebytes(x->grad, x->voice_count * sizeof(t_double ));
	t_freebytes(x->gradb, x->voice_count * sizeof(t_double ));
	t_freebytes(x->aOverb, x->voice_count * sizeof(t_double ));
	t_freebytes(x->bOvera, x->voice_count * sizeof(t_double ));
	t_freebytes(x->ball_loc, x->voice_count * sizeof(t_double ));
	t_freebytes(x->shape, x->voice_count * sizeof(t_double ));
	t_freebytes(x->direction, x->voice_count * sizeof(t_int ));
	t_freebytes(x->hz_conn, x->voice_count * sizeof(t_int ));
	t_freebytes(x->symm_conn, x->voice_count * sizeof(t_int ));
	t_freebytes(x->dcblock_on, x->voice_count * sizeof(t_int ));
	t_freebytes(x->dc_prev_in, x->voice_count * sizeof(t_double ));
	t_freebytes(x->dc_prev_out, x->voice_count * sizeof(t_double ));
	t_freebytes(x->sin, LKTBL_LNGTH * sizeof(t_double ));
	t_freebytes(x->sinh, LKTBL_LNGTH * sizeof(t_double ));

	for (i =0; i < x->voice_count; i++){
			t_freebytes(x->fm[i], x->voice_count * sizeof(t_double));
		}
	t_freebytes(x->fm, x->voice_count * sizeof(t_double *));


}


// function to create new instance and initialise its parameters
void *bounce_new(t_symbol *s, short argc, t_atom *argv)
{
	t_double bound_lo = -1.0, bound_hi = 1.0;
	t_int i = 0, j = 0, dir = -1;
	t_bounce *x = object_alloc(bounce_class); // set aside memory for the struct for the object

	x->mode = 0;
	x->fmax = FMAX * 0.5;
	x->bound_lo = bound_lo; 
	x->bound_hi = bound_hi;
	atom_arg_getlong(&(x->voice_count), 0, argc, argv);
	atom_arg_getdouble(&(x->bound_lo), 1, argc, argv);
	atom_arg_getdouble(&(x->bound_hi), 2, argc, argv);
	x->mode = atom_getintarg(3,argc,argv); 
	if(x->mode < 0) x->mode = 0;
	else if (x->mode > 1) x->mode = 1;

	//protect against invalid parameters
	if(x->voice_count > MAX_VOICES) {
		x->voice_count = MAX_VOICES; 
	} else if (x->voice_count < 1) {
		x->voice_count = 1; 
	}
	// add to dsp chain, set up inlets 
	dsp_setup((t_pxobject *)x, 2*x->voice_count + 2); // upper and lower bounds, plus hz and symm per voice
	x->obj.z_misc |= Z_NO_INPLACE; // force independent signal vectors

	// allocate memory for variable arrays
	x->hz = (t_double **) t_getbytes(x->voice_count * sizeof(t_double *));
	x->symm = (t_double **) t_getbytes(x->voice_count * sizeof(t_double *));
	x->out = (t_double **) t_getbytes(x->voice_count * sizeof(t_double *));
	x->hzFloat = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->grad = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->gradb = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->aOverb = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->bOvera = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->ball_loc = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->direction = (t_int *) t_getbytes(x->voice_count * sizeof(t_int));
	x->hz_conn = (t_int *) t_getbytes(x->voice_count * sizeof(t_int));
	x->symm_conn = (t_int *) t_getbytes(x->voice_count * sizeof(t_int));
	x->dcblock_on = (t_bool *) t_getbytes(x->voice_count * sizeof(t_bool));
	x->dc_prev_in = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->dc_prev_out = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->fm = (t_double **) t_getbytes(x->voice_count * sizeof(t_double *));
	x->shape = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
	x->sin = (t_double *)  t_getbytes(LKTBL_LNGTH * sizeof(t_double)); 
	x->sinh = (t_double *)  t_getbytes(LKTBL_LNGTH * sizeof(t_double)); 

	setup_lktables(x,0); // build lookup tables for waveshaper

	//set up outlets & and get rate for each voice from args
	x->ball_loc[0] = bound_lo + THINNESTPIPE; 
	x->direction[0] = 1;
	for(i=0; i < x->voice_count; i++){
		x->fm[i] = (t_double *) t_getbytes(x->voice_count * sizeof(t_double));
		outlet_new((t_object *)x, "signal"); 

		x->shape[i] = 0.1f;
		x->grad[i] = 2;
		x->hzFloat[i] = 100;
		x->ball_loc[i] =  (x->ball_loc[i-1] + THINNESTPIPE); // begin near bottom of current bound
		dir *= -1,  x->direction[i] = dir;	// alternate up and down 
		x->dcblock_on[i] = 0;
		x->dc_prev_in[i] = x->dc_prev_out[i] = 0.f;
		for(j =0; j< x->voice_count; j++){
			x->fm[i][j] = 0.0;
		}
	}

	// initialize remaining parameters
	x->srate = (t_double)sys_getsr();
	x->slx4 = (4 / x->srate);
	x->fm_on = 0;

#if DEBUG_ON == 1|| DEBUG_ON == 2
	x->poll_count = POLL_NO_SAMPLES-1;
	x->stopdebug = x->debug_count = 0;
#endif

	return x;
}

//function to connect to DSP chain
void	bounce_dsp64(t_bounce *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
	t_int i;

	// Check sample rate in object against vector and update if neccessary
	if(x->srate != (t_double) sys_getsr()){
		x->srate = (t_double) sys_getsr();
		x->slx4 =  (4 / x->srate);
	}

	object_method(dsp64, gensym("dsp_add64"), x, bounce_PerformWrapper, 0, NULL);

	// check if signals are connected
	x->bound_lo_conn = count[0];
	x->bound_hi_conn = count[1];
	for(i=0; i< x->voice_count; i++){
		x->hz_conn[i] = count[i+2];
		x->symm_conn[i] = count[i + 2 + x->voice_count];
	}


}


void bounce_assist(t_bounce *x, void *b, long msg, long arg, char *dst)
{
	if (msg==ASSIST_INLET){
		switch (arg) {
		case 0: sprintf(dst,"(signal/float) Lower Bound"); break;
		case 1: sprintf(dst,"(signal/float) Upper Bound"); break;
		default:
			if(arg > 1 && arg < x->voice_count + 1){
				sprintf(dst,"(signal/float) freq %ld", arg - 1);
			} else {
				sprintf(dst,"(signal/float) symmetry %d, (0-1)", arg - x->voice_count);
			}				
			break;
		}
	}
	else if (msg==ASSIST_OUTLET){
		sprintf(dst,"(signal) Wave Output"); 
		}
}


/************************************************************
!!!!!!!!!!!!	INCOMING MESSAGE HANDLING		!!!!!!!!!!!!
*************************************************************/

void bounce_float(t_bounce *x, double f)
{
	double grad, gradb;
	double symm;
	int inlet = ((t_pxobject*)x)->z_in;


	switch(inlet){
		case 0: x->bound_lo = (t_double) f; break;
		case 1: x->bound_hi = (t_double) f; break;
		default: 
			if (inlet < x->voice_count + 2 && inlet > 0) {
				x->hzFloat[inlet - 2] = (t_double) fabs(f);
			} else if (inlet -2 < x->voice_count * 2 && inlet > 0) {

				if(f < SYMMMIN) symm = SYMMMIN;
				else if (f > SYMMMAX) symm = SYMMMAX;
				else symm = f;
				
				grad  = 1/symm;
				gradb = -grad/(grad-1);
				x->aOverb[inlet - (2 + x->voice_count)] = grad/gradb;
				x->bOvera[inlet - (2 + x->voice_count)] = gradb/grad;
				x->grad[inlet - (2 + x->voice_count)] = grad;
				x->gradb[inlet - (2 + x->voice_count)] = gradb;
			}
			break;
	}
}

// MSG BANG input - outputs list of values in output buffer
void bounce_bang(t_bounce *x, t_double f)
{

	post("%f", x->fmax);


	post("bang does nowt");
}

// MSG "dc" symbol input, turns on/off clipping to -1...1
void	bounce_dcblock_set(t_bounce *x, t_symbol *msg, short argc, t_atom *argv)
{
	int i;

	if(argc >= 1){
		for(i =0; i < argc && i < x->voice_count; i++){
			x->dcblock_on[i] = (t_bool) atom_getintarg(i,argc,argv);
		}
	}
}

// MSG "shape" symbol input + int + float sets waveshape for voice
void bounce_shape_set(t_bounce *x, t_symbol *msg, short argc, t_atom *argv)
{
	t_int v;
	t_double amt = 0;

	v =  atom_getintarg(0,argc, argv);
	atom_arg_getdouble(&amt, 1, argc, argv);
	v-= 1;
	if(v < x->voice_count && v >= 0 ){
		if(amt < 0){
			if (amt > -0.05f) amt = -0.05f;
			else if (amt < -1.f) amt = -1.f;
			x->shape[v] = amt;		
		} else {
			if (amt < 0.05f) amt = 0.05f;
			else if (amt > 1.f) amt = 1.f;
			x->shape[v] = amt;
		}
	}
}

// MSG "fmax" symbol input + float sets maximum frequency
void bounce_fmax_set(t_bounce *x, t_symbol *msg, short argc, t_atom *argv)
{
	t_double f;
	atom_arg_getdouble(&f, 0, argc, argv);
	if(f<=FMAX && f>=FMIN){
		x->fmax = f * 0.5;
	}
}


// MSG "fm" symbol input, controls modulation amounts via list of 2 ints and a float (from, to, amt)
void	bounce_fm_set(t_bounce *x, t_symbol *msg, short argc, t_atom *argv)
{
	t_int in, out;
	t_double val = 0;
	if(argc == 3){
			in =  atom_getintarg(0,argc, argv);
			out = atom_getintarg(1,argc, argv);
			atom_arg_getdouble(&val, 2, argc, argv);
			in -= 1, out -=1;

			if(in <0 || in >= x->voice_count || out <0 || out >= x->voice_count){
				post("ERROR - invalid cross mod argument");
				in = 0, out = 0, val = 0;
			} else {
			x->fm[in][out] = val < MAXFM ? val: MAXFM;
			}
			
			if(val == 0.){
				// check if any other modulation is on, and set fm_on flag accordingly
				for(in = 0; in < x->voice_count; in++){
					for(out = 0; out < x->voice_count; out++){
						if(fabs(x->fm[in][out]) > 0.0001){
							x->fm_on = 1;
							goto quit_check_loop;
						}
					}
					x->fm_on = 0; // only reached if no modulation is on
				}
			}else{
				x->fm_on = 1;
			}
	}
quit_check_loop:
	;
}

// MSG "fmoff" symbol input, turns off modulation
void	bounce_fm_onoff(t_bounce *x, t_symbol *msg, short argc, t_atom *argv)
{
	int in, out;
	for(in = 0; in < x->voice_count; in++){
		for(out = 0; out < x->voice_count; out++){
				x->fm[in][out] = 0;
		}
	}	
	x->fm_on = 0;
}




/************************************************************
!!!!!!!!!!!!	MY HELPER FUNCTIONS		!!!!!!!!!!!!
*************************************************************/

// scales float in range 0 - 127 to float in range min - max
double infr_scale_param(double in, double in_min, double in_max, double out_min, double out_max)
{
	//Clip incoming values to range
	if(in < in_min) { in = in_min;}
	else if(in > in_max) { in = in_max;}

	return ((in - in_min)/(in_max - in_min))*(in_max-in_min) + in_min;

}


/************************************************************
!!!!!!!!!!!!	AUDIO CALC FUNCTIONS		!!!!!!!!!!!!
*************************************************************/

t_double bounce_dcblock(t_double input, t_double *lastinput, t_double *lastoutput, t_double gain)
{	
	t_double output;
	output = input - *lastinput + gain * *lastoutput;
	*lastinput  = input;
	*lastoutput = output;
	return output;
}

t_double bounce_fmcalc (t_bounce *x, t_int curr_voice)
{	
	t_double  modsum, modhz;
	t_int i;
	if(x->fm_on){
		//get sum of modulations
		modsum = 1;
		for(i =0; i <x->voice_count; i++){
			if(x->fm[i][curr_voice] != 0){	//i!=curr_voice && 
				modsum += x->ball_loc[i] * x->fm[i][curr_voice];
			}
		}
		// apply modulation to freq of this voice
		modhz = fabs(*(x->hz[curr_voice]) * modsum);
		modhz = modhz < 0 ? 0 : modhz;
		return modhz;
	} else {
		return *x->hz[curr_voice];
	}
}

// Correction functions for Polynomial Transition Region algorithm
t_double ptr_correctmax(t_double p, t_double a, t_double b, t_double t, t_double pmin, t_double pmax)
{
	t_double denom, atpmax, a2, a1, a0;
	denom = 2*a*a*t;
	atpmax = (a*t)-pmax;
	a2 = (b - a) / (2 * denom);
	a1 = ((a*t*(a + b)) + (pmax*(a-b))) / denom;
	a0 = ((b - a)* atpmax * atpmax)/ (2 * denom);
	return (a2*p*p) + (a1*p) + a0;
}

t_double ptr_correctmin(t_double p, t_double a, t_double b, t_double t, t_double pmin, t_double pmax)
{
	t_double denom, btpmin, b2, b1, b0;
	denom = 2*b*b*t;
	btpmin = b*t-pmin;
	b2 = (a-b) /(2*denom);
	b1 = (b*t*(a+b)+(pmin*(b-a)))/ denom;
	b0 = (a-b)*(btpmin*btpmin)/ (2*denom);	
	return (b2*p*p) + (b1*p) + b0;
}

void setup_lktables (t_bounce *x, t_int shape)
{	// create lookup for 1/4 sine and hyperbolic sine cycles -- could add alternative lookups
	int i;
	if(shape==0){
		for(i=0; i< LKTBL_LNGTH; i++){
			x->sin[i] = sin(PI * i * 0.5 / (LKTBL_LNGTH-1)) ;
			x->sinh[i] = sinh(PI * i * 0.5 / (LKTBL_LNGTH-1)) ;
		}
	}
}

double bounce_alimit(double a, double width, double t){
	// gradient can't be more than f/sr - (f @ width)
	// I've limited further to avoid antialiasing at higher freqs

	double amax, amin;
	amax = width / (4 * t);  
	if(amax < 2 ) amax = 2;
	amin = amax/(amax-1);	// cover downward gradient
	if(a>amax) {
		return amax;
	} else if (a < amin) {
		return amin;
	} else {
		return a;
	}
}


double do_shaping (t_bounce *x, t_double lo, t_double hi) 
// shape comes in as restricted to (-1...-0.05, 0.05 ...1), defines the portion of lookup to use
// pos between -1 and 1
{		
	t_double midpoint, halfwidth, ph, fracph, shaped, pos, shape, shapesign;
	t_int maxph, intph, sign, v;
	v = x->curr_v;
	if (x->shape[v] >= 0.1 || x->shape[v] <= -0.1){		
		pos = x->ball_loc[v];
		shape = x->shape[v];
		// get relative position between bounds for waveshaping lookup
		midpoint = lo + 0.5f * (hi - lo);
		halfwidth = midpoint - lo;
		// prepare phase values for lookups
		shapesign = sign(shape);
		shape = fabs(shape);
		maxph = (t_int)(shape * LKTBL_LNGTH-1);
		ph = (pos - midpoint) * maxph /  halfwidth;
		sign = sign(ph);
		ph = ph * sign;
		intph = (t_int)ph;
		fracph = ph - intph;
		// lookup, scale & lerp
		if(shapesign<0)	shaped = sign * (x->sinh[intph] * (1.f - fracph) + x->sinh[intph+1] * fracph) / x->sinh[maxph];
		else  shaped = sign * (x->sin[intph] * (1.f - fracph) + x->sin[intph+1] * fracph) / x->sin[maxph];
		//now return  waveshaping output scaled to actual bounds
		return midpoint + shaped * halfwidth;
	}
	else {
		return x->ball_loc[v];
	}
}


void 	bounce_PerformWrapper(t_bounce *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
	if(x->mode==0){
		bounce_perform64(x, ins, outs, sampleframes, bounce_shaper_voicecalc);
	} else{
		bounce_perform64(x, ins, outs, sampleframes, bounce_ptr_voicecalc);
	}
}


void 	bounce_perform64(t_bounce *x, double **ins, double **outs, long sampleframes, void (*voicemode)(void *, t_double, t_double, t_double, t_double))
{	
	t_double **hz, **symm, **out;
	t_double *bound_lo, *bound_hi;
	t_double this_lo, this_hi, width, symm_l, f0, fmax, grad, t;
	t_int samples, i, v;

	// Dereference
	hz = x->hz;
	symm = x->symm;
	out = x->out;
	samples = sampleframes;

	// 2 & 3 = Lo & hi
	if(x->bound_lo_conn){	// if signal connected, point at signal in
		bound_lo = (t_double *) (ins[0]);
	} else {				// if not, point at value in bounce object
		bound_lo = &(x->bound_lo);
	}

	if(x->bound_hi_conn){	// if signal connected, point at signal in
		bound_hi = (t_double *) (ins[1]);
	} else {				// if not, point at value in bounce object
		bound_hi = &(x->bound_hi);
	}

	// hz inputs,
	for  (i = 0; i< x->voice_count; i++){
		if(x->hz_conn[i]){	// if signal connected, point at signal in
			hz[i] = (t_double *) (ins[i + 1]);
		} else{				// if not, point at value in bounce object
			hz[i] = &(x->hzFloat[i]);
		}
	}

	// symm inputs,
	for  (i = 0; i< x->voice_count; i++){
		if(x->symm_conn[i]){	// if signal connected, point at signal in
			symm[i] = (t_double *) (ins[i + x->voice_count + 2]);
		} 
	}

	//  outputs
	for  (i = 0; i< x->voice_count; i++){
		out[i] = (t_double *) (outs[i]);
	}

	// Loop through samples in vector performing audio calcs
	while(samples--){
	
		// enforce legal values for bounds
		if (*bound_lo > *bound_hi - THINNESTPIPE){
			*bound_hi = (t_double) (*bound_lo + ((x->voice_count + 1) * THINNESTPIPE));
		}
		// Loop through voices
		this_lo = *bound_lo;
		for(x->curr_v=0; x->curr_v < x->voice_count; x->curr_v++){			
			v = x->curr_v;
			// hi bound is next ball's pos @ last sample 
			if(v == x->voice_count - 1){
				this_hi = *bound_hi; 			// except last ball which gets the outer hi bound
			}else{
				this_hi = x->ball_loc[v+1] < *bound_hi ? x->ball_loc[v+1] : *bound_hi ;
			}

			if(this_lo >= this_hi - THINNESTPIPE){
				this_hi = this_lo + THINNESTPIPE;
			}
			width = this_hi - this_lo;
			// get freq from freq modulation
			f0 = bounce_fmcalc (x, v);
			// determine freq & gradient limits at this width
			fmax = x->fmax * width;
			// apply limits
			if(f0>fmax) {
				f0 = fmax;
			} else if (f0 < FMIN) {
				f0 = FMIN;
			}
			t = f0/x->srate;

			if(x->symm_conn[v]) { // WITH SYMM SIGNALS CONNECTED
				if(*x->symm[v] < SYMMMIN) symm_l = SYMMMIN;
				else if (*x->symm[v] > SYMMMAX) symm_l = SYMMMAX;
				else symm_l = *x->symm[v];

				grad = bounce_alimit(1/symm_l, width, t);
			} else {	// WITHOUT SYMM SIGNALS CONNECTED
				grad = bounce_alimit(x->grad[v], width, t);
			}

			// mode-specific voice calcs
			voicemode(x, this_lo, this_hi, grad, t);

				// next ball's lo bound is this ball's pos (limited to outer bound)
			this_lo = x->ball_loc[v] > *bound_lo ? x->ball_loc[v]: *bound_lo;

			// apply dcblock if on
			if(x->dcblock_on[v]){
				*(out[v]) = bounce_dcblock(*(out[v]),&x->dc_prev_in[v], &x->dc_prev_out[v], (t_double) DCBLOCK_GAIN);
			} 
		}
		
		//store hz @ end of vector
		for(i=0; i < x->voice_count; i++){
			x->hzFloat[i] = *hz[i];
		}		
		//increment pointers for next sample
		if(x->bound_lo_conn) bound_lo++;
		if(x->bound_hi_conn) bound_hi++;
		for(i=0; i < x->voice_count; i++){
			if(x->hz_conn[i]) hz[i]++;
			if(x->symm_conn[i]) symm[i]++;
			 out[i]++;
		}
	}
}



void bounce_ptr_voicecalc (t_bounce *x, t_double lo, t_double hi, t_double grad, t_double t)
{
	t_double b;
	t_double *p;
	t_double *out;
	t_int v;
	t_int *dir;

	v = x->curr_v;
	dir = &x->direction[v];
	p = &x->ball_loc[v];
	out = x->out[v];

	if(*dir == 1){ //rising
		*p = *p + (2 * grad * t);
		if(*p > hi - grad*t){ // TRANSITION REGION
			b = -grad/(grad-1);
			*out = (t_double) ptr_correctmax(*p, grad, b, t, lo, hi);
			*p = (hi + (*p - hi)*(b/grad));
			*dir = -1;
			if(v < x->voice_count - 2){
				*(dir+1) = 1;
			}
		} else { // linear
			*out = (t_double) *p;
		}
	} else { // counting down
		b = -grad/(grad-1);
		*p = *p + (2 * b * t);
		if(*p < lo - b*t){ // TRANSITION REGION
			*out = (t_double)ptr_correctmin(*p, grad, b, t, lo, hi);
				*p = (lo + (*p - lo)*(grad/b));
				*dir = 1;
				if(v > 0){
					*(dir-1) = -1;
				}
		} else { // linear
			*out = (t_double)*p;
		}
	}

	if(*p > hi) {
		*p = hi, *dir = -1;
	} else if(*p < lo) {
		*p = lo, *dir = +1;
	}
}



void bounce_shaper_voicecalc (t_bounce *x, t_double lo, t_double hi, t_double grad, t_double t)
{
	t_double b, b_over_a;
	t_double *p;
	t_double *out;
	t_int v;
	t_int *dir;

	v = x->curr_v;
	dir = &x->direction[v];
	p = &x->ball_loc[v];
	out = x->out[v];

	//cursor movement calcs
	if(*dir == 1){ //rising
		*p = *p + (2 * grad * t);
		if(*p >= hi){ // TRANSITION
			b_over_a = -1/(grad-1);
			*p = (hi + (*p - hi)*b_over_a);
			*dir = -1;
			if(v < x->voice_count - 2){
				*(dir+1) = 1;
			}
		}
	} else { // counting down
		b = -grad/(grad-1);
		*p = *p + (2 * b * t);
		if(*p <= lo){ // TRANSITION
			*p = (lo + (*p - lo)*(grad/b));
			*dir = 1;
			if(v > 0){
				*(dir-1) = -1;
			}
		}
	}

	if(*p > hi) {
		*p = hi, *dir = -1;
	} else if(*p < lo) {
		*p = lo, *dir = +1;
	}
	*out = (t_double) do_shaping(x, lo, hi);
}
