//Bram Willemsen.
//bramwillemsen -at- gmail.com
//May 2016

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <disp_fun.h>
#define PI 3.14159265358979323846

int get_disp_crv(int N, double* alphas, double* betas, double* rhos, double* ds, double* phase_vels, double* freqs, int nfreqs, double C_min, double C_def_step, int NQUAD, int verbose)
{
	/* Evaluates the dispersion function for a material with N layers
	 * of P velocities alpha, S velocities beta, densities rho and thicknesses d.
	 * The 'continental model' in figure 10 of Schwab and Knopoff 1972 is implemented.
	 * Only the code in Figure 11 is used in addition to a quadratic root finding algorithm.
	 */
	double freq, omega;
	double *arr1, *arr2, *arr3;
	double *bot_scales, *top_scales, *mid_scales;
	double bot_val, top_val, mid_val;
	double bot_val_sc, mid_val_sc, top_val_sc;
	double bot_C, top_C, mid_C, next_mid_C;
	double relscale_mid, relscale_top;
	double B0, B1, B2; //quadratic form constants E, p95. Not using E to avoid confusion with exponent notation
	double D, quadroot1, quadroot2;
	double denom_0, denom_1, denom_2;
	double num_eps;
	int ifreq, iquad, MM;

	//allocate arrays
	arr1 =(double *) malloc((N-1)*sizeof(double));
	arr2 =(double *) malloc((N-1)*sizeof(double));
	arr3 =(double *) malloc((N-1)*sizeof(double));

	bot_scales = arr1;
	top_scales = arr2;
	mid_scales = arr3;

	num_eps = 1.0e-12;
	C_def_step = C_def_step + 1.0e-10; //page 133
	for(ifreq=0;ifreq<nfreqs;ifreq++) { //loop over frequencies
		freq  = freqs[ifreq];
		omega = 2*PI*freq;
		if(verbose) printf("Starting loop for frequency %e Hz\n\n", freq);

		//Find root for frequency.
		//Start with C_min
		bot_C   = C_min;
		bot_val = eval_rayleigh_disp_fun(N, alphas, betas, rhos, ds, bot_scales, omega, bot_C);

		//Loop over phase velocities
		top_C = bot_C; //intialize
		while(1){
			top_C   = bot_C + C_def_step;

			if (top_C > 0.999*betas[N-1]){//a attempted C larger than bottom Vs will result in Nan. Should never get above there
				top_C = 0.999*betas[N-1];
				if(bot_C == top_C){ //Apparently we also had this problem in the last iteration. Will not be able to advance
					printf("ERROR: Cannot find root below Vs of bottom layer\n");
					printf("Empirically, this seems to be resolvable by using smaller c_def_step\n");
					return(3);
				}
			}


			top_val = eval_rayleigh_disp_fun(N, alphas, betas, rhos, ds, top_scales, omega, top_C);

			if(verbose){
				printf("----------------------------------------------------\n");
				printf("Starting outer loop with bot_C %e and top_C %e\n", bot_C, top_C);
				printf("bot_val = %e and top_val = %e\n", bot_val, top_val);
				printf("----------------------------------------------------\n");
			}

			if (top_val == 0){ //if exactly on root. Hard to imagine...
				phase_vels[ifreq] = top_C;
				break;
			}

			//See if bot and top have different sign, then we know a root (zero) must be in between.
			if (bot_val*top_val < 0){ //We found the bracket. Now do quadratic search
				mid_C   = 0.5*(top_C + bot_C);
				mid_val = eval_rayleigh_disp_fun(N, alphas, betas, rhos, ds, mid_scales, omega, mid_C);

				//use bot, top and mid to find new C. Do NQUAD steps
				for(iquad=0; iquad<NQUAD; iquad++){
					//take care of difference in scaling values
					//normalize scaling towards bot scales (arbitrary choice)
					//I'm currently scaling in every loop. Not most efficient.

					if(verbose){
						printf("Starting quadratic loop %i: \n", iquad);
						printf("bot_C = %e, mid_C = %e, top_C = %e\n",bot_C, mid_C, top_C);
						printf("bot_val = %e, mid_val = %e, top_val = %e\n\n",bot_val, mid_val, top_val);
					}

					if(sqrt(mid_val*mid_val) < num_eps){ //We are very close to 0. Sometimes rounding errors will cause crashes if we continue. Just accept this solution
						break;
					}

					//init
					relscale_mid = 1;
					relscale_top = 1;
					for(MM=0; MM<N-1;MM++){
						relscale_mid = relscale_mid*mid_scales[MM]/bot_scales[MM];
						relscale_top = relscale_top*top_scales[MM]/bot_scales[MM];
					}

					bot_val_sc = bot_val;
					mid_val_sc = mid_val*relscale_mid;
					top_val_sc = top_val*relscale_top;

					//convenience
					denom_0 = (bot_C-mid_C)*(bot_C-top_C);
					denom_1 = (mid_C-bot_C)*(mid_C-top_C);
					denom_2 = (top_C-bot_C)*(top_C-mid_C);

					//the quadratic constants B0, B1, B2
					B0      =  ( bot_val_sc*mid_C*top_C/denom_0
							    +mid_val_sc*bot_C*top_C/denom_1
							    +top_val_sc*bot_C*mid_C/denom_2);

					B1      = -( bot_val_sc*(mid_C+top_C)/denom_0
							    +mid_val_sc*(bot_C+top_C)/denom_1
								+top_val_sc*(bot_C+mid_C)/denom_2);

					B2      = bot_val_sc/denom_0 + mid_val_sc/denom_1 + top_val_sc/denom_2;

					//Quadratic representation: F = B0 + B1*C + B2*C**2
					//Find root
					D = B1*B1-4e0*B2*B0; //determinant
					if(D<0){ //No roots, should not be possible with our brackets
						printf("ERROR: Determinant negative. Should not be possible.\n");
						return(1); //error
					}

					//Otherwise the roots are (-B1+-sqrt(D))/2B2
					quadroot1 = (-B1-sqrt(D))/(2*B2);
					quadroot2 = (-B1+sqrt(D))/(2*B2);

					//select the one within the bracket (should only be one)
					if(quadroot1 >= bot_C && quadroot1 <= top_C){       //we want root 1
						next_mid_C = quadroot1;
					}
					else if(quadroot2 >= bot_C && quadroot2 <= top_C) { //we want root 2
						next_mid_C = quadroot2;
					}
					else{ //We are not supposed to end up here. Just leaving it to catch any error in code
						printf("ERROR: Root condition statement gives unexpected result. Terminating.\n");
						return(2);
					}

					//See how the next mid C compares with old mid C
					//This way we can see what our next bot and top val will be

					if(next_mid_C == mid_C){ //if exactly the same, then mid_C must have been root
						break;
					}
					else if(next_mid_C < mid_C){ //old mid C becomes top
						top_C      = mid_C;
						top_val    = mid_val;
						top_scales = mid_scales;
					}
					else if(next_mid_C > mid_C){ //old mid C becomes bot
						bot_C      = mid_C;
						bot_val    = mid_val;
						bot_scales = mid_scales;
					}

					//populate mid with the new mid
					mid_C = next_mid_C;

					if(iquad<NQUAD-1){//If another iteration will take place, calculate mid_val and mid_scales as well
						mid_val = eval_rayleigh_disp_fun(N, alphas, betas, rhos, ds, mid_scales, omega, mid_C);
					}

				}
				//Done searching for root, accept mid_C as being close enough.
				//mid_C is the last quadratic interpolation step
				phase_vels[ifreq] = mid_C;
				if(verbose){
					printf("For omega %e, phase velocity %e is selected \n", omega, phase_vels[ifreq]);
				}

				break; //Stop loop over C, we are done
			}
			else{ //if same sign, increase with C_def_step again until we find a bracket. Prepare for next iteration
				bot_C      = top_C;
				bot_val    = top_val;
				bot_scales = top_scales;
			}


		}
	}


	//FREE ARRAYS
	free(arr1);
	free(arr2);
	free(arr3);

	return(0);
}
