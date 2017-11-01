/*-----------------------------------------------------------------
 *	$Id$
 *
 *      Copyright (c) 1999-2017 by P. Wessel
 *      See LICENSE.TXT file for copying and redistribution conditions.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU Lesser General Public License as published by
 *      the Free Software Foundation; version 3 or any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU Lesser General Public License for more details.
 *
 *      Contact info: www.soest.hawaii.edu/pwessel
 *--------------------------------------------------------------------*/
/* x2sys_cross will calculate crossovers generated by the
 * intersections of two tracks.  Optionally, it will also evaluate
 * the interpolated datafields at the crossover locations.
 *
 * Author:	Paul Wessel
 * Date:	15-JUN-2004
 * Version:	1.0, based on the spirit of the old xsystem code,
 *		but with a smarter algorithm based on the book
 *		"Algorithms in C" by R. Sedgewick.
 *		31-MAR-2006: Changed -O to -L to avoid clash with GMT.
 *
 */

#include "gmt_dev.h"
#include "mgd77/mgd77.h"
#include "x2sys.h"

#define THIS_MODULE_NAME	"x2sys_cross"
#define THIS_MODULE_LIB		"x2sys"
#define THIS_MODULE_PURPOSE	"Calculate crossovers between track data files"
#define THIS_MODULE_KEYS	">D}"
#define THIS_MODULE_NEEDS	""
#define THIS_MODULE_OPTIONS "->JRVbd"

/* Control structure for x2sys_cross */

#define HHI	0
#define VLO	1
#define	VHI	2

struct X2SYS_CROSS_CTRL {
	struct X2S_CROSS_A {	/* -A */
		bool active;
		char *file;
	} A;
	struct X2S_CROSS_C {	/* -C */
		bool active;
		char *file;
	} C;
	struct X2S_CROSS_I {	/* -I */
		bool active;
		int mode;
	} I;
	struct X2S_CROSS_S {	/* -S */
		bool active[2];
		double limit[3];
	} S;
	struct X2S_CROSS_T {	/* -T */
		bool active;
		char *TAG;
	} T;
	struct X2S_CROSS_W {	/* -W */
		bool active;
		unsigned int width;
	} W;
	struct X2S_CROSS_Q {	/* -Q */
		bool active;
		int mode;
	} Q;
	struct X2S_CROSS_Z {	/* -Z */
		bool active;
	} Z;
};

struct PAIR {				/* Used with -Kkombinations.lis option */
	char *id1, *id2;
};

GMT_LOCAL void *New_Ctrl (struct GMT_CTRL *GMT) {	/* Allocate and initialize a new control structure */
	struct X2SYS_CROSS_CTRL *C;

	C = gmt_M_memory (GMT, NULL, 1, struct X2SYS_CROSS_CTRL);

	/* Initialize values whose defaults are not 0/false/NULL */

	C->S.limit[VHI] = DBL_MAX;	/* Ignore crossovers on segments that implies speed higher than this */
	C->W.width = 3;			/* Number of points on either side in the interpolation */
	return (C);
}

GMT_LOCAL void Free_Ctrl (struct GMT_CTRL *GMT, struct X2SYS_CROSS_CTRL *C) {	/* Deallocate control structure */
	if (!C) return;
	gmt_M_str_free (C->A.file);
	gmt_M_str_free (C->C.file);
	gmt_M_str_free (C->T.TAG);
	gmt_M_free (GMT, C);
}

GMT_LOCAL int usage (struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose (API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return (GMT_NOERROR);
	GMT_Message (API, GMT_TIME_NONE, "usage: x2sys_cross <files> -T<TAG> [-A<combi.lis>] [-C[<fname>]] [-Il|a|c] [%s] [-Qe|i]\n", GMT_J_OPT);
	GMT_Message (API, GMT_TIME_NONE, "\t[%s] [-Sl|h|u<speed>] [%s] [-W<size>] [-Z]\n", GMT_Rgeo_OPT, GMT_V_OPT);
	GMT_Message (API, GMT_TIME_NONE, "\t[%s] [%s]\n\n", GMT_bo_OPT, GMT_do_OPT);

	GMT_Message (API, GMT_TIME_NONE, "\tOutput is x y t1 t2 d1 d2 az1 az2 v1 v2 xval1 xmean1 xval2 xmean2 ...\n");
	GMT_Message (API, GMT_TIME_NONE, "\tIf time is not selected (or present) we use record numbers as proxies i1 i2\n\n");
	
	if (level == GMT_SYNOPSIS) return (GMT_MODULE_SYNOPSIS);

	GMT_Message (API, GMT_TIME_NONE, "\t<files> is one or more datafiles, or give =<files.lis> for a file with a list of datafiles.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-T <TAG> is the system tag for the data set.\n");
	GMT_Message (API, GMT_TIME_NONE, "\n\tOPTIONS:\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-A Give list of file pairs that are ok to compare [Default is all combinations].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-C Print run time for each pair. Optionally append <fname> to save them in file.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-I Set the interpolation mode.  Choose among:\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     l Linear interpolation [Default].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     a Akima spline interpolation.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     c Cubic spline interpolation.\n");
	GMT_Option (API, "J-");
	GMT_Message (API, GMT_TIME_NONE, "\t-Q Append e for external crossovers.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Append i for internal crossovers [Default is all crossovers].\n");
	GMT_Option (API, "R");
	GMT_Message (API, GMT_TIME_NONE, "\t-S Set limits on lower and upper speeds (units determined by -Ns):\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     -Sl sets lower speed [Default is 0].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     -Sh no headings should be computed if velocity drops below this value [0].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t     -Su sets upper speed [Default is Infinity].\n");
	GMT_Option (API, "V");
	GMT_Message (API, GMT_TIME_NONE, "\t-W Set maximum points on either side of crossover to use in interpolation [Default is 3].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-Z Return z-values for each track [Default is crossover and mean value].\n");
	GMT_Option (API, "bo,do,.");
	
	return (GMT_MODULE_USAGE);
}

GMT_LOCAL int parse (struct GMT_CTRL *GMT, struct X2SYS_CROSS_CTRL *Ctrl, struct GMT_OPTION *options) {

	/* This parses the options provided to grdcut and sets parameters in CTRL.
	 * Any GMT common options will override values set previously by other commands.
	 * It also replaces any file names specified as input or output with the data ID
	 * returned when registering these sources/destinations with the API.
	 */

	unsigned int n_errors = 0, n_files[2] = {0, 0};
	struct GMT_OPTION *opt = NULL;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {	/* Process all the options given */

		switch (opt->option) {
			/* Common parameters */

			case '<':	/* Skip input files since their paths depend on tag */
				n_files[GMT_IN]++;
				break;
			case '>':	/* Got named output file */
				n_files[GMT_OUT]++;
				break;

			/* Processes program-specific parameters */
			
			case 'A':	/* Get list of approved filepair combinations to check */
				if ((Ctrl->A.active = gmt_check_filearg (GMT, 'A', opt->arg, GMT_IN, GMT_IS_DATASET)) != 0)
					Ctrl->A.file = strdup (opt->arg);
				else
					n_errors++;
				break;
			case 'C':
				Ctrl->C.active = true;
				if (strlen(opt->arg))
					Ctrl->C.file = strdup (opt->arg);
				break;
			case 'I':
				Ctrl->I.active = true;
				switch (opt->arg[0]) {
					case 'l':
						Ctrl->I.mode = 0;
						break;
					case 'a':
						Ctrl->I.mode = 1;
						break;
					case 'c':
						Ctrl->I.mode = 2;
						break;
					case 'n':
						Ctrl->I.mode = 3;
						break;
					default:
						n_errors++;
						break;
				}
				break;
			case 'S':	/* Speed checks */
				switch (opt->arg[0]) {
					case 'L':
					case 'l':	/* Lower cutoff speed */
						Ctrl->S.limit[VLO] = atof (&opt->arg[1]);
						Ctrl->S.active[VLO] = true;
						break;
					case 'U':
					case 'u':	/* Upper cutoff speed */
						Ctrl->S.limit[VHI] = atof (&opt->arg[1]);
						Ctrl->S.active[VLO] = true;
						break;
					case 'H':
					case 'h':	/* Heading calculation cutoff speed */
						Ctrl->S.limit[HHI] = atof (&opt->arg[1]);
						Ctrl->S.active[HHI] = true;
						break;
					default:
						GMT_Report (API, GMT_MSG_NORMAL, "Syntax error: -S<l|h|u><speed>\n");
						n_errors++;
						break;
				}
				break;
			case 'T':
				Ctrl->T.active = true;
				Ctrl->T.TAG = strdup (opt->arg);
				break;
			case 'W':	/* Get new window half-width as number of points */
				Ctrl->W.active = true;
				Ctrl->W.width = atoi (opt->arg);
				break;
			case 'Q':	/* Specify internal or external only */
				Ctrl->Q.active = true;
				if (opt->arg[0] == 'e') Ctrl->Q.mode = 1;
				else if (opt->arg[0] == 'i') Ctrl->Q.mode = 2;
				else Ctrl->Q.mode = 3;
				break;
			case 'Z':	/* Return z1, z1 rather than (z1-z1) and 0.5 * (z1 + z2) */
				Ctrl->Z.active = true;
				break;
			default:	/* Report bad options */
				n_errors += gmt_default_error (GMT, opt->option);
				break;
		}
	}

	n_errors += gmt_M_check_condition (GMT, n_files[GMT_IN] == 0, "Syntax error: No track files given\n");
	n_errors += gmt_M_check_condition (GMT, n_files[GMT_OUT] > 1, "Syntax error: More than one output file given\n");
	n_errors += gmt_M_check_condition (GMT, !Ctrl->T.active || !Ctrl->T.TAG, "Syntax error: -T must be used to set the TAG\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->W.width < 1, "Syntax error: Error -W: window must be at least 1\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->S.limit[VLO] > Ctrl->S.limit[VHI], "Syntax error: Error -S: lower speed cutoff higher than upper cutoff!\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->Q.mode == 3, "Syntax error: Error -Q: Only one of -Qe -Qi can be specified!\n");


	return (n_errors ? GMT_PARSE_ERROR : GMT_NOERROR);
}

GMT_LOCAL int combo_ok (char *name_1, char *name_2, struct PAIR *pair, uint64_t n_pairs) {
	uint64_t i;

	/* Return true if this particular combination is found in the list of pairs */

	for (i = 0; i < n_pairs; i++) {
		if (!(strcmp (name_1, pair[i].id1) || strcmp (name_2, pair[i].id2))) return (true);
		if (!(strcmp (name_2, pair[i].id1) || strcmp (name_1, pair[i].id2))) return (true);
	}
	return (false);
}

GMT_LOCAL void free_pairs (struct GMT_CTRL *GMT, struct PAIR **pair, uint64_t n_pairs) {
	/* Free the array of pairs */
	uint64_t k;
	struct PAIR *P = *pair;
	for (k = 0; k < n_pairs; k++) {
		gmt_M_str_free (P[k].id1);
		gmt_M_str_free (P[k].id2);
	}
	gmt_M_free (GMT, pair);

}
#define bailout(code) {gmt_M_free_options (mode); return (code);}
#define Return(code) {Free_Ctrl (GMT, Ctrl); gmt_end_module (GMT, GMT_cpy); bailout (code);}

int GMT_x2sys_cross (void *V_API, int mode, void *args) {
	char **trk_name = NULL;			/* Name of tracks */
	char line[GMT_BUFSIZ] = {""};		/* buffer */
	char item[GMT_BUFSIZ] = {""};		/* buffer */
	char t_or_i;				/* t = time, i = dummy node time */
	char name1[80] = {""}, name2[80] = {""};		/* Name of two files to be examined */
	char *x2sys_header = "%s %d %s %d %s";

	uint64_t n_rec[2];			/* Number of data records for both files */
	uint64_t window_width;			/* Max number of points to use in the interpolation */
	uint64_t n_tracks = 0;			/* Total number of data sets to compare */
	uint64_t nx;				/* Number of crossovers found for this pair */
	uint64_t *col_number = NULL;		/* Array with the column numbers of the data fields */
	unsigned int n_output;			/* Number of columns on output */
	uint64_t n_pairs = 0;			/* Number of acceptable combinations */
	uint64_t A, B, i, j, col, k, start, n_bad;	/* Misc. counters and local variables */
	uint64_t end, first, n_ok;
	uint64_t n_data_col, left[2], t_left;
	uint64_t n_left, right[2], t_right, n_right;
	uint64_t n_duplicates, n_errors;
	uint64_t add_chunk;
	int scol;
	int error = 0;				/* nonzero for invalid arguments */
	unsigned int *ok = NULL;

	bool xover_locations_only = false;	/* true if only x,y (and possible indices) to be output */
	bool internal = true;			/* false if only external xovers are needed */
	bool external = true;			/* false if only internal xovers are needed */
	bool do_project = false;		/* true if we must mapproject first */
	bool got_time = false;			/* true if there is a time column */
	bool first_header = true;		/* true for very first crossover */
	bool first_crossover;			/* true for first crossover between two data sets */
	bool same = false;			/* true when the two tracks we compare have the same name */
	bool has_time[2];			/* true for each tracks that actually has a time column */
	bool *duplicate = NULL;			/* Array, true for any track that is already listed */
	bool cmdline_files = false;		/* true if files where given directly on the command line */
	bool wrap = false;			/* true if data wraps so -Rg was given */
	
	size_t n_alloc = 1;

	double dt;				/* Time between crossover and previous node */
	double dist_x[2];			/* Distance(s) along track at the crossover point */
	double time_x[2];			/* Time(s) along track at the crossover point */
	double deld, delt;			/* Differences in dist and time across xpoint */
	double speed[2];			/* speed across the xpoint ( = deld/delt) */
	double **data[2] = {NULL, NULL};	/* Data matrices for the two data sets to be checked */
	double *xdata[2] = {NULL, NULL};	/* Data vectors with estimated values at crossover points */
	double *dist[2] = {NULL, NULL};		/* Data vectors with along-track distances */
	double *time[2] = {NULL, NULL};		/* Data vectors with along-track times (or dummy node indices) */
	double *t = NULL, *y = NULL;		/* Interpolation y(t) arrays */
	double *out = NULL;			/* Output record array */
	double X2SYS_NaN;			/* Value to write out when result is NaN */
	double xx, yy;				/* Temporary projection variables */
	double dist_scale;			/* Scale to give selected distance units */
	double vel_scale;			/* Scale to give selected velocity units */
	double t_scale;				/* Scale to give time in seconds */

	clock_t tic = 0, toc = 0;

	struct X2SYS_INFO *s = NULL;			/* Data format information  */
	struct GMT_XSEGMENT *ylist_A = NULL, *ylist_B = NULL;		/* y-indices sorted in increasing order */
	struct GMT_XOVER XC;				/* Structure with resulting crossovers */
	struct X2SYS_FILE_INFO data_set[2];		/* File information */
	struct X2SYS_BIX Bix;
	struct PAIR *pair = NULL;		/* Used with -Akombinations.lis option */
	FILE *fp = NULL, *fpC = NULL;
	struct GMT_RECORD *Out = NULL;
	struct X2SYS_CROSS_CTRL *Ctrl = NULL;
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct GMTAPI_CTRL *API = gmt_get_api_ptr (V_API);	/* Cast from void to GMTAPI_CTRL pointer */

/*----------------------------------END OF VARIABLE DECLARATIONS---------------------------------------------*/

	/*----------------------- Standard module initialization and parsing ----------------------*/

	if (API == NULL) return (GMT_NOT_A_SESSION);
	if (mode == GMT_MODULE_PURPOSE) return (usage (API, GMT_MODULE_PURPOSE));	/* Return the purpose of program */
	options = GMT_Create_Options (API, mode, args);	if (API->error) return (API->error);	/* Set or get option list */

	if (!options || options->option == GMT_OPT_USAGE) bailout (usage (API, GMT_USAGE));	/* Return the usage message */
	if (options->option == GMT_OPT_SYNOPSIS) bailout (usage (API, GMT_SYNOPSIS));	/* Return the synopsis */

	/* Parse the command-line arguments */

	if ((GMT = gmt_init_module (API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, &options, &GMT_cpy)) == NULL) bailout (API->error); /* Save current state */
	if (GMT_Parse_Common (API, THIS_MODULE_OPTIONS, options)) Return (API->error);
	Ctrl = New_Ctrl (GMT);	/* Allocate and initialize a new control structure */
	if ((error = parse (GMT, Ctrl, options)) != 0) Return (error);

	/*---------------------------- This is the x2sys_cross main code ----------------------------*/

	x2sys_err_fail (GMT, x2sys_set_system (GMT, Ctrl->T.TAG, &s, &Bix, &GMT->current.io), Ctrl->T.TAG);
	if (!s->geographic) {
		gmt_set_column (GMT, GMT_IO, GMT_X, GMT_IS_UNKNOWN);
		gmt_set_column (GMT, GMT_IO, GMT_Y, GMT_IS_UNKNOWN);
	}

	if (s->x_col == -1 || s->y_col == -1) {
		GMT_Report (API, GMT_MSG_NORMAL, "lon,lat or x,y are not among data columns!\n");
		Return (GMT_RUNTIME_ERROR);
	}
	
	if ((error = x2sys_get_tracknames (GMT, options, &trk_name, &cmdline_files)) == 0) {
		GMT_Report (API, GMT_MSG_NORMAL, "Must give at least one data set!\n");
		Return (GMT_RUNTIME_ERROR);		
	}
	n_tracks = (uint64_t)error;
	
	GMT->current.setting.interpolant = Ctrl->I.mode;
	if (Ctrl->Q.active) {
		if (Ctrl->Q.mode == 1) internal = false;
		if (Ctrl->Q.mode == 2) external = false;
	}

	GMT_Report (API, GMT_MSG_LONG_VERBOSE, "Files found: %" PRIu64 "\n", n_tracks);

	duplicate = gmt_M_memory (GMT, NULL, n_tracks, bool);

	GMT_Report (API, GMT_MSG_LONG_VERBOSE, "Checking for duplicates : ");
	/* Make sure there are no duplicates */
	for (A = n_duplicates = 0; A < n_tracks; A++) {	/* Loop over all files */
		if (duplicate[A]) continue;
		for (B = A + 1; B < n_tracks; B++) {
			if (duplicate[B]) continue;
			same = !strcmp (trk_name[A], trk_name[B]);
			if (same) {
				GMT_Report (API, GMT_MSG_NORMAL, "File %s repeated on command line - skipped\n", trk_name[A]);
				duplicate[B] = true;
				n_duplicates++;
			}
		}
	}
	GMT_Report (API, GMT_MSG_LONG_VERBOSE, "%" PRIu64 " found\n", n_duplicates);
	
	if (Ctrl->A.active) {	/* Read list of acceptable trk_name combinations */

		GMT_Report (API, GMT_MSG_LONG_VERBOSE, "Explicit combinations found: ");
		if ((fp = fopen (Ctrl->A.file, "r")) == NULL) {
			GMT_Report (API, GMT_MSG_NORMAL, "Could not open combinations file %s!\n", Ctrl->A.file);
			Return (GMT_ERROR_ON_FOPEN);
		}

		n_alloc = add_chunk = GMT_CHUNK;
		pair = gmt_M_memory (GMT, NULL, n_alloc, struct PAIR);

		while (fgets (line, GMT_BUFSIZ, fp)) {

			if (line[0] == '#' || line[0] == '\n') continue;	/* Skip comments and blanks */
			gmt_chop (line);	/* Get rid of CR, LF stuff */

			if (sscanf (line, "%s %s", name1, name2) != 2) {
				GMT_Report (API, GMT_MSG_NORMAL, "Error decoding combinations file for pair %" PRIu64 "!\n", n_pairs);
				fclose (fp);
				free_pairs (GMT, &pair, n_pairs);
				Return (GMT_RUNTIME_ERROR);
			}
			pair[n_pairs].id1 = strdup (name1);
			pair[n_pairs].id2 = strdup (name2);
			n_pairs++;
			if (n_pairs == n_alloc) {
				size_t old_n_alloc = n_alloc;
				add_chunk *= 2;
				n_alloc += add_chunk;
				pair = gmt_M_memory (GMT, pair, n_alloc, struct PAIR);
				gmt_M_memset (&(pair[old_n_alloc]), n_alloc - old_n_alloc, struct PAIR);
			}
		}
		fclose (fp);

		if (!n_pairs) {
			GMT_Report (API, GMT_MSG_NORMAL, "No combinations found in file %s!\n", Ctrl->A.file);
			gmt_M_free (GMT, duplicate);
			x2sys_free_list (GMT, trk_name, n_tracks);
			free_pairs (GMT, &pair, n_pairs);
			Return (GMT_RUNTIME_ERROR);
		}
		if (n_pairs < n_alloc) pair = gmt_M_memory (GMT, pair, n_pairs, struct PAIR);
		GMT_Report (API, GMT_MSG_LONG_VERBOSE, "%" PRIu64 "\n", n_pairs);
	}

	if (Ctrl->C.file) {	/* Open file to store the per pair run time */
		if ((fpC = fopen (Ctrl->C.file, "w")) == NULL) {
			GMT_Report (API, GMT_MSG_NORMAL, "Could not open save times file %s!\n", Ctrl->C.file);
			gmt_M_str_free (Ctrl->C.file);
		}
	}

	X2SYS_NaN = GMT->session.d_NaN;

	if (GMT->current.setting.interpolant == 0) Ctrl->W.width = 1;
	window_width = 2 * Ctrl->W.width;
	n_data_col = x2sys_n_data_cols (GMT, s);
	got_time = (s->t_col >= 0);
	if (!got_time) Ctrl->S.active[VLO] = false;	/* Cannot check speed if there is no time */

	n_output = (unsigned int)(10 + 2 * n_data_col);
	gmt_set_column (GMT, GMT_OUT, GMT_X, (!strcmp (s->info[s->x_col].name, "lon")) ? GMT_IS_LON : GMT_IS_FLOAT);
	gmt_set_column (GMT, GMT_OUT, GMT_Y, (!strcmp (s->info[s->y_col].name, "lat")) ? GMT_IS_LAT : GMT_IS_FLOAT);
	gmt_set_column (GMT, GMT_OUT, GMT_Z, (got_time) ? GMT_IS_ABSTIME : GMT_IS_FLOAT);
	gmt_set_column (GMT, GMT_OUT, 3, (got_time) ? GMT_IS_ABSTIME : GMT_IS_FLOAT);
	
	for (i = 0; i < n_data_col+2; i++) {
		gmt_set_column (GMT, GMT_OUT, 4+2*(unsigned int)i, GMT_IS_FLOAT);
		gmt_set_column (GMT, GMT_OUT, 5+2*(unsigned int)i, GMT_IS_FLOAT);
	}
	
	if (n_data_col == 0) {
		xover_locations_only = true;
		n_output = 2;
	}
	else {	/* Set the actual column numbers with data fields */
		t = gmt_M_memory (GMT, NULL, window_width, double);
		y = gmt_M_memory (GMT, NULL, window_width, double);
		col_number = gmt_M_memory (GMT, NULL, n_data_col, uint64_t);
		ok = gmt_M_memory (GMT, NULL, n_data_col, unsigned int);
		for (col = k = scol = 0; col < s->n_out_columns; col++, scol++) {
			if (scol == s->x_col || scol == s->y_col || scol == s->t_col) continue;
			col_number[k++] = col;
		}
		if (s->t_col < 0) GMT_Report (API, GMT_MSG_VERBOSE, "No time column, use dummy times\n");
	}

	out = gmt_M_memory (GMT, NULL, n_output, double);
	xdata[0] = gmt_M_memory (GMT, NULL, s->n_out_columns, double);
	xdata[1] = gmt_M_memory (GMT, NULL, s->n_out_columns, double);
	Out = gmt_new_record (GMT, out, NULL);	/* Since we only need to worry about numerics in this module */

	gmt_set_segmentheader (GMT, GMT_OUT, true);	/* Turn on segment headers on output */
	gmt_set_tableheader (GMT, GMT_OUT, true);	/* Turn on -ho explicitly */

	if (GMT->common.R.active[RSET] && GMT->current.proj.projection_GMT != GMT_NO_PROJ) {
		do_project = true;
		s->geographic = false;	/* Since we then have x,y projected coordinates, not lon,lat */
		s->dist_flag = 0;
		if (fpC) fclose (fpC);
		x2sys_free_list (GMT, trk_name, n_tracks);
		if (gmt_M_err_pass (GMT, gmt_proj_setup (GMT, GMT->common.R.wesn), "")) {
			gmt_M_free (GMT, duplicate);
			Return (GMT_PROJECTION_ERROR);
		}
	}

	gmt_init_distaz (GMT, s->dist_flag ? GMT_MAP_DIST_UNIT : 'X', s->dist_flag, GMT_MAP_DIST);
		
	MGD77_Set_Unit (GMT, s->unit[X2SYS_DIST_SELECTION], &dist_scale, -1);	/* Gets scale which multiplies meters to chosen distance unit */
	MGD77_Set_Unit (GMT, s->unit[X2SYS_SPEED_SELECTION], &vel_scale, -1);	/* Sets output scale for distances using in velocities */
	switch (s->unit[X2SYS_SPEED_SELECTION][0]) {
		case 'c':
			vel_scale = 1.0;
			break;
		case 'e':
			vel_scale /= dist_scale;			/* Must counteract any distance scaling to get meters. dt is in sec so we get m/s */
			break;
		case 'f':
			vel_scale /= (METERS_IN_A_FOOT * dist_scale);	/* Must counteract any distance scaling to get feet. dt is in sec so we get ft/s */
			break;
		case 'k':
			vel_scale *= (3600.0 / dist_scale);		/* Must counteract any distance scaling to get km. dt is in sec so 3600 gives km/hr */
			break;
		case 'm':
			vel_scale *= (3600.0 / dist_scale);		/* Must counteract any distance scaling to get miles. dt is in sec so 3600 gives miles/hr */
			break;
		case 'n':
			vel_scale *= (3600.0 / dist_scale);		/* Must counteract any distance scaling to get miles. dt is in sec so 3600 gives miles/hr */
			break;
		case 'u':
			vel_scale /= (METERS_IN_A_SURVEY_FOOT * dist_scale);	/* Must counteract any distance scaling to get survey feet. dt is in sec so we get ft/s */
			break;
		default:	/*Cartesian */
			break;
	}
	t_scale = GMT->current.setting.time_system.scale;	/* Convert user's TIME_UNIT to seconds */
	wrap = (gmt_M_is_geographic (GMT, GMT_IN) && GMT->common.R.active[RSET] && gmt_M_360_range (GMT->common.R.wesn[XLO], GMT->common.R.wesn[XHI]));
	
	if ((error = GMT_Set_Columns (API, GMT_OUT, n_output, GMT_COL_FIX_NO_TEXT)) != GMT_NOERROR) {
		gmt_M_free (GMT, duplicate);
		x2sys_free_list (GMT, trk_name, n_tracks);
		if (fpC) fclose (fpC);
		x2sys_end (GMT, s);
		Return (error);
	}
	if (GMT_Init_IO (API, GMT_IS_DATASET, GMT_IS_POINT, GMT_OUT, GMT_ADD_DEFAULT, 0, options) != GMT_NOERROR) {	/* Registers default output destination, unless already set */
		gmt_M_free (GMT, duplicate);
		x2sys_free_list (GMT, trk_name, n_tracks);
		if (fpC) fclose (fpC);
		x2sys_end (GMT, s);
		Return (API->error);
	}
	if (GMT_Begin_IO (API, GMT_IS_DATASET, GMT_OUT, GMT_HEADER_ON) != GMT_NOERROR) {	/* Enables data output and sets access mode */
		gmt_M_free (GMT, duplicate);
		x2sys_free_list (GMT, trk_name, n_tracks);
		if (fpC) fclose (fpC);
		x2sys_end (GMT, s);
		Return (API->error);
	}
	if (GMT_Set_Geometry (API, GMT_OUT, GMT_IS_POINT) != GMT_NOERROR) {	/* Sets output geometry */
		gmt_M_free (GMT, duplicate);
		x2sys_free_list (GMT, trk_name, n_tracks);
		if (fpC) fclose (fpC);
		x2sys_end (GMT, s);
		Return (API->error);
	}

	for (A = 0; A < n_tracks; A++) {	/* Loop over all files */
		if (duplicate[A]) continue;

		if (s->x_col < 0 || s->y_col < 0) {
			GMT_Report (API, GMT_MSG_NORMAL, "x and/or y column not found for track %s!\n", trk_name[A]);
			x2sys_free_list (GMT, trk_name, n_tracks);
			if (fpC) fclose (fpC);
			x2sys_end (GMT, s);
			Return (GMT_RUNTIME_ERROR);
		}

		x2sys_err_fail (GMT, (s->read_file) (GMT, trk_name[A], &data[0], s, &data_set[0], &GMT->current.io, &n_rec[0]), trk_name[A]);

		if (n_rec[0] == 0) {	/* No data in track A */
			x2sys_free_data (GMT, data[0], s->n_out_columns, &data_set[0]);
			continue;
		}
		
		has_time[0] = false;
		if (got_time) {	/* Check to make sure we do in fact have time */
			for (i = n_bad = 0; i < n_rec[0]; i++) n_bad += gmt_M_is_dnan (data[0][s->t_col][i]);
			if (n_bad < n_rec[0]) has_time[0] = true;
		}

		if (do_project) {	/* Convert all the coordinates */
			for (i = 0; i < n_rec[0]; i++) {
				gmt_geo_to_xy (GMT, data[0][s->x_col][i], data[0][s->y_col][i], &xx, &yy);
				data[0][s->x_col][i] = xx;
				data[0][s->y_col][i] = yy;
			}
		}

		if ((dist[0] = gmt_dist_array_2 (GMT, data[0][s->x_col], data[0][s->y_col], n_rec[0], dist_scale, s->dist_flag)) == NULL) gmt_M_err_fail (GMT, GMT_MAP_BAD_DIST_FLAG, "");

		time[0] = (has_time[0]) ? data[0][s->t_col] : x2sys_dummytimes (GMT, n_rec[0]) ;

		gmt_init_track (GMT, data[0][s->y_col], n_rec[0], &ylist_A);

		for (B = A; B < n_tracks; B++) {
			if (duplicate[B]) continue;

			same = !strcmp (trk_name[A], trk_name[B]);
			if (same && !(A == B)) {
				GMT_Report (API, GMT_MSG_NORMAL, "File %s repeated on command line - skipped\n", trk_name[A]);
				continue;
			}
			if (!internal &&  same) continue;	/* Only do external errors */
			if (!external && !same) continue;	/* Only do internal errors */

			if (Ctrl->A.active && !combo_ok (trk_name[A], trk_name[B], pair, n_pairs)) continue;	/* Do not want this combo */
			
			if (Ctrl->C.active) tic = clock();	/* To report execution time from this pair */

			GMT_Report (API, GMT_MSG_LONG_VERBOSE, "Processing %s - %s : ", trk_name[A], trk_name[B]);

			if (same) {	/* Just set pointers */
				data[1] = data[0];
				dist[1] = dist[0];
				time[1] = time[0];
				has_time[1] = has_time[0];
				n_rec[1] = n_rec[0];
				ylist_B = ylist_A;
				data_set[1] = data_set[0];
			}
			else {	/* Must read a second file */

				x2sys_err_fail (GMT, (s->read_file) (GMT, trk_name[B], &data[1], s, &data_set[1], &GMT->current.io, &n_rec[1]), trk_name[B]);

				if (n_rec[1] == 0) {	/* No data in track B */
					x2sys_free_data (GMT, data[1], s->n_out_columns, &data_set[1]);
					continue;
				}
				has_time[1] = false;
				if (got_time) {	/* Check to make sure we do in fact have time */
					for (i = n_bad = 0; i < n_rec[1]; i++) n_bad += gmt_M_is_dnan (data[1][s->t_col][i]);
					if (n_bad < n_rec[1]) has_time[1] = true;
				}
				
				if (do_project) {	/* Convert all the coordinates */
					for (i = 0; i < n_rec[0]; i++) {
						gmt_geo_to_xy (GMT, data[1][s->x_col][i], data[1][s->y_col][i], &xx, &yy);
						data[1][s->x_col][i] = xx;
						data[1][s->y_col][i] = yy;
					}
				}

				if ((dist[1] = gmt_dist_array_2 (GMT, data[1][s->x_col], data[1][s->y_col], n_rec[1], dist_scale, s->dist_flag)) == NULL) gmt_M_err_fail (GMT, GMT_MAP_BAD_DIST_FLAG, "");

				time[1] = (has_time[1]) ? data[1][s->t_col] : x2sys_dummytimes (GMT, n_rec[1]);

				gmt_init_track (GMT, data[1][s->y_col], n_rec[1], &ylist_B);
			}

			/* Calculate all possible crossover locations */

			nx = gmt_crossover (GMT, data[0][s->x_col], data[0][s->y_col], data_set[0].ms_rec, ylist_A, n_rec[0], data[1][s->x_col], data[1][s->y_col], data_set[1].ms_rec, ylist_B, n_rec[1], (A == B), wrap, &XC);

			if (nx && xover_locations_only) {	/* Report crossover locations only */
				sprintf (line, "%s - %s", trk_name[A], trk_name[B]);
				GMT_Put_Record (API, GMT_WRITE_SEGMENT_HEADER, line);
				for (i = 0; i < nx; i++) {
					out[0] = XC.x[i];
					out[1] = XC.y[i];
					if (s->geographic) gmt_lon_range_adjust (s->geodetic, &out[0]);
					GMT_Put_Record (API, GMT_WRITE_DATA, Out);	/* Write this to output */
				}
				gmt_x_free (GMT, &XC);
			}
			else if (nx) {	/* Got crossovers, now estimate crossover values */
				int64_t start_s;
				first_crossover = true;

				for (i = 0; i < nx; i++) {	/* For each potential crossover */

					gmt_M_memset (ok, n_data_col, unsigned int);
					n_ok = 0;

					for (k = 0; k < 2; k++) {	/* For each of the two data sets involved */

						/* Get node number to each side of crossover location */

				/*	--o----------o--------o------X-------o-------o----------o-- ----> time
							      ^      ^       ^
							    left   xover   right			*/

						left[k]  = lrint (floor (XC.xnode[k][i]));
						right[k] = lrint (ceil  (XC.xnode[k][i]));
						
						if (left[k] == right[k]) {	/* Crosses exactly on a node; move left or right so interpolation will work */
							if (left[k] > 0)
								left[k]--;	/* Move back so cross occurs at right[k] */
							else
								right[k]++;	/* Move forward so cross occurs at left[k] */
						}

						deld = dist[k][right[k]] - dist[k][left[k]];
						delt = time[k][right[k]] - time[k][left[k]];

						/* Check if speed is outside accepted domain */

						speed[k] = (delt == 0.0) ? GMT->session.d_NaN : vel_scale * (deld / (delt * t_scale));
						if (Ctrl->S.active[VLO] && !gmt_M_is_dnan (speed[k]) && (speed[k] < Ctrl->S.limit[VLO] || speed[k] > Ctrl->S.limit[VHI])) continue;

						/* Linearly estimate the crossover times and distances */

						dt = XC.xnode[k][i] - left[k];
						time_x[k] = time[k][left[k]];
						dist_x[k] = dist[k][left[k]];
						if (dt > 0.0) {
							time_x[k] += dt * delt;
							dist_x[k] += dt * deld;
						}


						for (j = 0; j < n_data_col; j++) {	/* Evaluate each field at the crossover */

							col = col_number[j];

							start = t_right = left[k];
							end = t_left = right[k];
							n_left = n_right = 0;

							xdata[k][col] = GMT->session.d_NaN;	/* In case of nuthin' */

							/* First find the required <window> points to the left of the xover */
							start_s = start;
							while (start_s >= 0 && n_left < Ctrl->W.width) {
								if (!gmt_M_is_dnan (data[k][col][start])) {
									n_left++;
									if (t_left > left[k]) t_left = start;
									y[Ctrl->W.width-n_left] = data[k][col][start];
									t[Ctrl->W.width-n_left] = time[k][start];
								}
								start--;
								start_s--;
							}

							if (!n_left) continue;
							if (got_time && ((time_x[k] - time[k][t_left]) > Bix.time_gap)) continue;
							if ((dist_x[k] - dist[k][t_left]) > Bix.dist_gap) continue;

							/* Ok, that worked.  Now for the right side: */

							while (end < n_rec[k] && n_right < Ctrl->W.width) {
								if (!gmt_M_is_dnan (data[k][col][end])) {
									y[Ctrl->W.width+n_right] = data[k][col][end];
									t[Ctrl->W.width+n_right] = time[k][end];
									n_right++;
									if (t_right < right[k]) t_right = end;
								}
								end++;
							}

							if (!n_right) continue;
							/* See if we pass any gap criteria */
							if (got_time && ((time[k][t_right] - time_x[k]) > Bix.time_gap)) continue;	/* Exceeded time gap */
							if ((dist[k][t_right] - dist_x[k]) > Bix.dist_gap) continue;			/* Exceeded distance gap */

							/* Ok, got enough data to interpolate at xover */

							first = Ctrl->W.width - n_left;
							n_errors = gmt_intpol (GMT, &t[first], &y[first], (n_left + n_right), 1, &time_x[k], &xdata[k][col], GMT->current.setting.interpolant);
							if (n_errors == 0) {	/* OK */
								ok[j]++;
								n_ok++;
							}
						}
					}

					/* Only output crossover if there are any data there */

					if (n_ok == 0) continue;
					for (j = n_ok = 0; j < n_data_col; j++) if (ok[j] == 2) n_ok++;
					if (n_ok == 0) continue;

					/* OK, got something to report */

					/* Load the out array */

					out[0] = XC.x[i];	/* Crossover location */
					out[1] = XC.y[i];

					for (k = 0; k < 2; k++) {	/* Get times, distances, headings, and velocities */

						/* Get time */

						out[2+k] = (got_time && !has_time[k]) ? X2SYS_NaN : time_x[k];

						/* Get cumulative distance at crossover */

						out[k+4] = dist_x[k];

						/* Estimate heading there */

						j = k + 6;
						out[j] = (!gmt_M_is_dnan (speed[k]) && (!Ctrl->S.active[HHI] || speed[k] > Ctrl->S.limit[HHI])) ? (*GMT->current.map.azimuth_func) (GMT, data[k][s->x_col][right[k]], data[k][s->y_col][right[k]], data[k][s->x_col][left[k]], data[k][s->y_col][left[k]], false) : X2SYS_NaN;

						/* Estimate velocities there */

						j = k + 8;
						out[j] = (has_time[k]) ? speed[k] : X2SYS_NaN;
					}

					/* Calculate crossover and mean value */

					for (k = 0, j = 10; k < n_data_col; k++) {
						if (Ctrl->Z.active) {
							col = col_number[k];
							out[j++] = xdata[0][col];
							out[j++] = xdata[1][col];
						}
						else {
							if (ok[k] == 2) {
								col = col_number[k];
								out[j++] = xdata[0][col] - xdata[1][col];
								out[j++] = 0.5 * (xdata[0][col] + xdata[1][col]);
							}
							else {
								out[j] = out[j+1] = X2SYS_NaN;
								j += 2;
							}
						}
					}

					if (first_header) {	/* Write the header record */
						char *cmd = NULL, *c = GMT->current.setting.io_col_separator;
						t_or_i = (got_time) ? 't' : 'i';
						sprintf (line, "Tag: %s", Ctrl->T.TAG);
						GMT_Put_Record (API, GMT_WRITE_TABLE_HEADER, line);
						cmd = GMT_Create_Cmd (API, options);
						sprintf (line, "Command: %s %s", THIS_MODULE_NAME, cmd);	/* Build command line argument string */
						gmt_M_free (GMT, cmd);
						GMT_Put_Record (API, GMT_WRITE_TABLE_HEADER, line);
						sprintf (line, "%s%s%s%s%c_1%s%c_2%sdist_1%sdist_2%shead_1%shead_2%svel_1%svel_2",
							s->info[s->out_order[s->x_col]].name, c, s->info[s->out_order[s->y_col]].name, c, t_or_i, c, t_or_i, c, c, c, c, c, c);
						for (j = 0; j < n_data_col; j++) {
							col = col_number[j];
							if (Ctrl->Z.active)
								sprintf (item, "%s%s_1%s%s_2", c, s->info[s->out_order[col]].name, c, s->info[s->out_order[col]].name);
							else
								sprintf (item, "%s%s_X%s%s_M", c, s->info[s->out_order[col]].name, c, s->info[s->out_order[col]].name);
							strcat (line, item);
						}
						GMT_Put_Record (API, GMT_WRITE_TABLE_HEADER, line);
						first_header = false;
					}

					if (first_crossover) {
						char info[GMT_BUFSIZ] = {""}, l_start[2][GMT_LEN64], stop[2][GMT_LEN64];
						for (k = 0; k < 2; k++) {
							if (has_time[k]) {	/* Find first and last record times */
								for (j = 0; j < n_rec[k] && gmt_M_is_dnan (time[k][j]); j++);	/* Find first non-NaN time */
								gmt_ascii_format_col (GMT, l_start[k], time[k][j], GMT_OUT, 2);
								for (j = n_rec[k]-1; j > 0 && gmt_M_is_dnan (time[k][j]); j--);	/* Find last non-NaN time */
								gmt_ascii_format_col (GMT, stop[k], time[k][j], GMT_OUT, 3);
							}
							else {
								strcpy (l_start[k], "NaN");
								strcpy (stop[k], "NaN");
							}
						}
						sprintf (info, "%s/%s/%g %s/%s/%g", l_start[0], stop[0], dist[0][n_rec[0]-1], l_start[1], stop[1], dist[1][n_rec[1]-1]);
						sprintf (line, x2sys_header, trk_name[A], data_set[0].year, trk_name[B], data_set[1].year, info);
						GMT_Put_Record (API, GMT_WRITE_SEGMENT_HEADER, line);
						first_crossover = false;
					}

					if (s->geographic) gmt_lon_range_adjust (s->geodetic, &out[0]);
					GMT_Put_Record (API, GMT_WRITE_DATA, Out);	/* Write this to output */
				}

				gmt_x_free (GMT, &XC);
			}

			if (!same) {	/* Must free up memory for B */
				x2sys_free_data (GMT, data[1], s->n_out_columns, &data_set[1]);
				gmt_M_free (GMT, dist[1]);
				if (!got_time) gmt_M_free (GMT, time[1]);
				gmt_M_free (GMT, ylist_B);
			}
			if (!Ctrl->C.active)
				GMT_Report (API, GMT_MSG_LONG_VERBOSE, "%" PRIu64 "\n", nx);
			else {
				toc = clock();
				GMT_Report (API, GMT_MSG_LONG_VERBOSE, "%" PRIu64 "\t%.3f sec\n", nx, (double)(toc - tic)/1000);
				if (fpC)	/* Save also the run time in file */
					fprintf (fpC, "%s\t%s\t%d\t%.3f\n", trk_name[A], trk_name[B], (int)nx, (double)(toc - tic)/1000);
			}
		}

		/* Must free up memory for A */

		x2sys_free_data (GMT, data[0], s->n_out_columns, &data_set[0]);
		gmt_M_free (GMT, dist[0]);
		if (!got_time) gmt_M_free (GMT, time[0]);
		gmt_M_free (GMT, ylist_A);
	}

	if (fpC) fclose (fpC);

	if (GMT_End_IO (API, GMT_OUT, 0) != GMT_NOERROR) {	/* Disables further data output */
		Return (API->error);
	}

	/* Free up other arrays */

	if (Ctrl->A.active) free_pairs (GMT, &pair, n_pairs);
	gmt_M_free (GMT, Out);
	gmt_M_free (GMT, xdata[0]);
	gmt_M_free (GMT, xdata[1]);
	gmt_M_free (GMT, out);
	gmt_M_free (GMT, duplicate);
	if (n_data_col) {
		gmt_M_free (GMT, t);
		gmt_M_free (GMT, y);
		gmt_M_free (GMT, col_number);
		gmt_M_free (GMT, ok);
	}
	x2sys_free_list (GMT, trk_name, n_tracks);

	x2sys_end (GMT, s);

	Return (GMT_NOERROR);
}
