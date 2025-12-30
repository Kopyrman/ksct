// ksct - kopyrman's X11 set color temperature

#include "ksct.h"
#include <stdint.h>

static inline void usage(const char *restrict const pname)
{
    printf(
		"Ksct (%s)\n"
		"Usage: %s [options] [temperature] [brightness]\n"
		"\tIf the argument is 0, ksct resets the display to the default temperature (6500K)\n"
		"\tIf no arguments are passed, ksct estimates the current display temperature and brightness\n"
		"Options:\n"
		"\t-h, --help \t ksct will display this usage information\n"
		"\t-v, --verbose \t ksct will display debugging information\n"
		"\t-B, --default\t ksct will change the default temperature to the values given\n"
		"\t-d, --delta\t ksct will consider temperature and brightness parameters as relative shifts\n"
		"\t-s, --screen N\t ksct will only select screen specified by given zero-based index\n"
		"\t-t, --toggle \t ksct will toggle between 'day' and 'night' mode\n"
		"\t-N, --night \t ksct will change the night mode temperature and brightness to the values given\n"
		"\t-D, --day \t ksct will change the day mode temperature and brightness to the values given\n"
		"\t-c, --crtc N\t ksct will only select CRTC specified by given zero-based index\n",
		KSCT_VERSION, pname
	);
}


static inline double DoubleTrim(double x, double a, double b)
{
    const double buff[3] = {a, x, b};
    return buff[(x > a) + (x > b)];
}


static screen_status get_sct_for_screen(Display *dpy, int screen, int icrtc, const bool fdebug)
{
    Window root = RootWindow(dpy, screen);
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

    int n, c;
    double t = 0.0;
	screen_gamma gamma = {0.0, 0.0, 0.0};
    screen_status temp;
    temp.temp = 0;

    n = res->ncrtc;
    if ((icrtc >= 0) && (icrtc < n))
        n = 1;
    else
        icrtc = 0;

    for (c = icrtc; c < (icrtc + n); c++)
    {
        RRCrtc crtcxid;
        int size;
        XRRCrtcGamma *crtc_gamma;
        crtcxid = res->crtcs[c];
        crtc_gamma = XRRGetCrtcGamma(dpy, crtcxid);
        size = crtc_gamma->size;
        gamma.r += crtc_gamma->red[size - 1];
        gamma.g += crtc_gamma->green[size - 1];
        gamma.b += crtc_gamma->blue[size - 1];

        XRRFreeGamma(crtc_gamma);
    }

    XFree(res);
    temp.brightness = (gamma.r > gamma.g) ? gamma.r : gamma.g;
    temp.brightness = (gamma.b > temp.brightness) ? gamma.b : temp.brightness;

    if (temp.brightness > 0.0 && n > 0)
    {
        gamma.r /= temp.brightness;
        gamma.g /= temp.brightness;
        gamma.b /= temp.brightness;
        temp.brightness /= n;
        temp.brightness /= BRIGHTHESS_DIV;
        temp.brightness = DoubleTrim(temp.brightness, 0.0, 1.0);
        if (fdebug)
			fprintf(stderr, "DEBUG: Gamma: %f, %f, %f, brightness: %f\n", gamma.r, gamma.g, gamma.b, temp.brightness);
        const double gammad = gamma.b - gamma.r;

        if (gammad < 0.0)
        {
            if (gamma.b > 0.0)
                t = exp((gamma.g + 1.0 + gammad - (GAMMA_K0GR + GAMMA_K0BR)) / (GAMMA_K1GR + GAMMA_K1BR)) + TEMPERATURE_ZERO;
            else
                t = (gamma.g > 0.0) ? (exp((gamma.g - GAMMA_K0GR) / GAMMA_K1GR) + TEMPERATURE_ZERO) : TEMPERATURE_ZERO;
        }
        else
            t = exp((gamma.g + 1.0 - gammad - (GAMMA_K0GB + GAMMA_K0RB)) / (GAMMA_K1GB + GAMMA_K1RB)) + (TEMPERATURE_NORM - TEMPERATURE_ZERO);
    }
    else
        temp.brightness = DoubleTrim(temp.brightness, 0.0, 1.0);

    temp.temp = (int16_t)(t + 0.5);

    return temp;
}

static void sct_for_screen(Display *dpy, int screen, int icrtc, screen_status temp, const bool fdebug)
{
    double t = 0.0, b = 1.0;
	screen_gamma gamma = {0};
    int n, c;
    Window root = RootWindow(dpy, screen);
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);

    t = (double)temp.temp;
    b = DoubleTrim(temp.brightness, 0.0, 1.0);
    if (temp.temp < TEMPERATURE_NORM)
    {
        gamma.r = 1.0;
        if (temp.temp > TEMPERATURE_ZERO)
        {
            const double g = log(t - TEMPERATURE_ZERO);
            gamma.g = DoubleTrim(GAMMA_K0GR + GAMMA_K1GR * g, 0.0, 1.0);
            gamma.b = DoubleTrim(GAMMA_K0BR + GAMMA_K1BR * g, 0.0, 1.0);
        }
        else
        {
            gamma.g = 0.0;
            gamma.b = 0.0;
        }
    }
    else
    {
        const double g = log(t - (TEMPERATURE_NORM - TEMPERATURE_ZERO));
        gamma.r = DoubleTrim(GAMMA_K0RB + GAMMA_K1RB * g, 0.0, 1.0);
        gamma.g = DoubleTrim(GAMMA_K0GB + GAMMA_K1GB * g, 0.0, 1.0);
        gamma.b = 1.0;
    }
    if (fdebug)
		fprintf(stderr, "DEBUG: Gamma: %f, %f, %f, brightness: %f\n", gamma.r, gamma.g, gamma.b, b);

    n = res->ncrtc;

    if ((icrtc >= 0) && (icrtc < n))
        n = 1;
    else
        icrtc = 0;

    for (c = icrtc; c < (icrtc + n); c++)
    {
        int size, i;
        RRCrtc crtcxid;
        XRRCrtcGamma *crtc_gamma;
        crtcxid = res->crtcs[c];
        size = XRRGetCrtcGammaSize(dpy, crtcxid);

        crtc_gamma = XRRAllocGamma(size);

        for (i = 0; i < size; i++)
        {
            const double g = GAMMA_MULT * b * (double)i / (double)size;
            crtc_gamma->red[i] = (unsigned short int)(g * gamma.r + 0.5);
            crtc_gamma->green[i] = (unsigned short int)(g * gamma.g + 0.5);
            crtc_gamma->blue[i] = (unsigned short int)(g * gamma.b + 0.5);
        }

        XRRSetCrtcGamma(dpy, crtcxid, crtc_gamma);
        XRRFreeGamma(crtc_gamma);
    }

    XFree(res);
}

static void bound_temp(screen_status *const temp)
{
    if (temp->temp <= 0)
    {
        // identical behavior when ksct is called in absolute mode with temp == 0
        fprintf(stderr, "WARNING! Temperatures below %d cannot be displayed.\n", 0);
        temp->temp = TEMPERATURE_NORM;
    }
    else if (temp->temp < TEMPERATURE_ZERO)
    {
        fprintf(stderr, "WARNING! Temperatures below %d cannot be displayed.\n", TEMPERATURE_ZERO);
        temp->temp = TEMPERATURE_ZERO;
    }

    if (temp->brightness < 0.0)
    {
        fprintf(stderr, "WARNING! Brightness values below 0.0 cannot be displayed.\n");
        temp->brightness = 0.0;
    }
    else if (temp->brightness > 1.0)
    {
        fprintf(stderr, "WARNING! Brightness values above 1.0 cannot be displayed.\n");
        temp->brightness = 1.0;
    }
}

// gets command line arguments and fill the options, and temp params
static inline bool getArgs(const int argc, const char** argv, uint8_t *restrict options, screen_status *restrict temp, int16_t *restrict crtc, int8_t *restrict screen)
{
	*crtc = -1;
	*screen = -1;
	*options = 0;
	
	for (int8_t i = 1; i < argc; i++)
    {
        if		(!strcmp(argv[i],"-h") || !strcmp(argv[i],"--help"))	*options |= OPT_HELP;
        else if (!strcmp(argv[i],"-v") || !strcmp(argv[i],"--verbose"))	*options |= OPT_DEBUG;
        else if (!strcmp(argv[i],"-d") || !strcmp(argv[i],"--delta"))	*options |= OPT_DELTA;
        else if (!strcmp(argv[i],"-t") || !strcmp(argv[i],"--toggle"))	*options |= OPT_TOGGLE;
        else if (!strcmp(argv[i],"-s") || !strcmp(argv[i],"--screen"))
        {
            i++;

            if (i < argc)
                *screen = (int8_t)atoi(argv[i]);
            else
			{
                fprintf(stderr, "ERROR! Required value for screen not specified!\n");
				return true;
            }
        }
        else if ((strcmp(argv[i],"-c") == 0) || (strcmp(argv[i],"--crtc") == 0))
        {
            i++;

            if (i < argc)
                *crtc = (int8_t)atoi(argv[i]);
            else
			{
                fprintf(stderr, "ERROR! Required value for crtc not specified!\n");
				return true;
            }
        }
        else if (temp->temp == DELTA_MIN)
			temp->temp = (int16_t)atoi(argv[i]);
        else if (temp->brightness == DELTA_MIN)
			temp->brightness = atof(argv[i]);
        else
        {
            fprintf(stderr, "ERROR! Unknown parameter: %s\n!", argv[i]);
			return true;
        }
    }

	return false;
}


int main(int argc, const char **argv)
{
    Display *dpy = XOpenDisplay(NULL);

    if (!dpy)
    {
        perror("XOpenDisplay(NULL) failed");
        fprintf(stderr, "ERROR! Ensure DISPLAY is set correctly!\n");
        return EXIT_FAILURE;
    }
	
	int8_t screenLast  = (int8_t)XScreenCount(dpy) - 1;
	int8_t screenFirst = 0;


	uint8_t options = 0;
    screen_status temp = {DELTA_MIN, DELTA_MIN};
	int16_t crtc;
	int8_t screen;

	if (getArgs(argc, argv, &options, &temp, &crtc, &screen))
		return EXIT_FAILURE;
	

	if (screen >= 0)
		screenLast = (int8_t)screen;

    if (options & OPT_HELP)
	{
        usage(argv[0]);
		return EXIT_SUCCESS;
	}

	if (screen > screenLast)
	{
        fprintf(stderr, "ERROR! Invalid screen index: %d!\n", screen);
		XCloseDisplay(dpy);
		return EXIT_FAILURE;
    }

	if ((options & OPT_DELTA) & (options & OPT_TOGGLE))
	{
		fprintf(stderr, "ERROR! options --delta and --toggle are exclusive to each other!\n");
		XCloseDisplay(dpy);
		return EXIT_FAILURE;
	}


	if (options & OPT_DELTA)
	{
		// Delta mode: Shift temperature and optionally brightness of each screen by given value
		if (temp.temp == DELTA_MIN || temp.brightness == DELTA_MIN)
		{
			fprintf(stderr, "ERROR! Temperature and brightness delta must both be specified!\n");
			XCloseDisplay(dpy);
			return EXIT_FAILURE;
		}

		for (int8_t scr = screenFirst; screen <= screenLast; screen++)
		{
			screen_status tempDiff = get_sct_for_screen(dpy, scr, crtc, options & OPT_DEBUG);

			tempDiff.temp += temp.temp;
			tempDiff.brightness += temp.brightness;

			bound_temp(&tempDiff);

			sct_for_screen(dpy, scr, crtc, tempDiff, options & OPT_DEBUG);
		}
	}
	else if (options & OPT_TOGGLE)
	{
		// Check if the temp is above 100 less than the norm and change to NIGHT if it is
		// The threashold was chosen to give some room for varients in temp
		for (int8_t scr = screenFirst; scr <= screenLast; scr++) // iterate throught every screen
		{
			temp = get_sct_for_screen(dpy, scr, crtc, options & OPT_DEBUG);

			if (temp.temp > (TEMPERATURE_NORM - 100))
				temp.temp = TEMPERATURE_NIGHT;
			else
				temp.temp = TEMPERATURE_NORM;

			sct_for_screen(dpy, scr, crtc, temp, options & OPT_DEBUG);
		}
	}


	// Set temperature to given value or default for a value of 0
	if (temp.temp == 0)
		temp.temp = TEMPERATURE_NORM;
	else
		bound_temp(&temp);


	// TODO: add a condition to this below (im so fucking blue)

	// No options, so simply set screen to geven temperature
	for (int8_t scr = screenFirst; scr <= screenLast; scr++)
	   sct_for_screen(dpy, scr, crtc, temp, options & OPT_DEBUG);


	// No arguments, so print estimated temperature for each screen
	for (int8_t scr = screenFirst; scr <= screenLast; scr++)
	{
		temp = get_sct_for_screen(dpy, scr, crtc, options & OPT_DEBUG);
		printf("Screen %d: temperature ~ %d %f\n", scr, temp.temp, temp.brightness);
	}

    XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
