# db.bounce
Max MSP audio external. Chaotic Triangle Wave Oscillator Bank with Waveshaping, FM, ++


	AUTHOR:			Daniel Bennett (skjolbrot@gmail.com)
	DATE:			24/01/2015
	DESCRIPTION:	Multiple interacting Peter Blasser style "Bounds 	
      			and Bounce" triangle oscillators.
      			Overview:
      			- Imaginary balls moving along vector (s,s) with s defined by user per ball
      			- Y component reverses sign when the cursor reaches bound.
      			- lower bound of ball n is location of ball n-1
      			  upper bound of ball n is location of ball n+1
      			- At fixed bounds (-1..1) behaviour for a single voice 
      			  instance is as normal, crude (non bandlimited ) triangle 
      			  oscillator. 
      			for want of a more meaningful value, rate input scaled as the frequency
      			of the fixed bounds (-1...1) case.
      			Works on Max 6 & higher, 32 and 64 bit.
		
	Version:		0.3
	
	DevNotes:		Having made the PTR calcs optional (mode), triangle symmetry calcs
    				are probably the highest single overhead remaining. I tried
    				moving these outside of the audio loop but found that the
    				constantly changing width neccessitates checking that gradient
    				is within limits & conditionally recalculating on a sample-by-sample
    				basis; so the potential gains here are minimal (@ optimisation -O3),
    				& cost in code complexity is considerable. However, if anyone
    				finds an elegant solution to improving efficiency of the calcs
    				I'd be really interested to see.
    				Probably (for x86 processors) int truncation is worth optimising?
