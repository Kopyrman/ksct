// ksct - kopyrman's X11 set color temperature

#include "ksct.h"
#include <stdint.h>

static inline void usage(const char *restrict const pname)
{
    printf("Ksct (%s)\n"
           "Usage: %s [options] [temperature] [brightness]\n"
           "\tIf the argument is 0, ksct resets the display to the default temperature (6500K)\n"
           "\tIf no arguments are passed, ksct estimates the current display temperature and brightness\n"
           "Options:\n"
           "\t-h, --help \t ksct will display this usage information\n"
           "\t-v, --verbose \t ksct will display debugging information\n"
		   "\t-B, --default\t ksct will set the default temperature\n"
           "\t-d, --delta\t ksct will consider temperature and brightness parameters as relative shifts\n"
           "\t-s, --screen \t ksct will only select screen specified by given zero-based index\n"
           "\t-t, --toggle \t ksct will toggle between 'day' and 'night' mode\n"
		   "\t-N, --night \t ksct will set the night mode temperature and brightness\n"
		   "\t-D, --day \t ksct will set the day mode temperature and brightness\n"
           "\t-c, --crtc N\t ksct will only select CRTC specified by given zero-based index\n", KSCT_VERSION, pname);
}


static inline double DoubleTrim(double x, double a, double b)
{
    const double buff[3] = {a, x, b};
    return buff[(uint16_t)(x > a) + (uint16_t)(x > b)];
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
        if (fdebug > 0)
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

    temp.temp = (int)(t + 0.5);

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
    if (fdebug > 0)
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

int main(int argc, char **argv)
{
    int i, screen, screens;
    int screen_specified, screen_first, screen_last, crtc_specified;
    screen_status temp;
	uint8_t options = 0;
    Display *dpy = XOpenDisplay(NULL);
    unsigned char failed = 0;

    if (!dpy)
    {
        perror("XOpenDisplay(NULL) failed");
        fprintf(stderr, "ERROR! Ensure DISPLAY is set correctly!\n");
        return EXIT_FAILURE;
    }
	
    screens = XScreenCount(dpy);
    screen_first = 0;
    screen_last = screens - 1;
    screen_specified = -1;
    crtc_specified = -1;
    temp.temp = DELTA_MIN;
    temp.brightness = DELTA_MIN;

    for (i = 1; i < argc; i++)
    {
        if		((strcmp(argv[i],"-h") == 0) || (strcmp(argv[i],"--help") == 0))	options |= OPT_HELP;
        else if ((strcmp(argv[i],"-v") == 0) || (strcmp(argv[i],"--verbose") == 0)) options |= OPT_DEBUG;
        else if ((strcmp(argv[i],"-d") == 0) || (strcmp(argv[i],"--delta") == 0))	options |= OPT_DELTA;
        else if ((strcmp(argv[i],"-t") == 0) || (strcmp(argv[i],"--toggle") == 0))	options |= OPT_TOGGLE;
        else if ((strcmp(argv[i],"-s") == 0) || (strcmp(argv[i],"--screen") == 0))
        {
            i++;
            if (i < argc)
                screen_specified = atoi(argv[i]);
            else
			{
                fprintf(stderr, "ERROR! Required value for screen not specified!\n");
                failed = 1;
				options |= OPT_HELP;
            }
        }
        else if ((strcmp(argv[i],"-c") == 0) || (strcmp(argv[i],"--crtc") == 0))
        {
            i++;
            if (i < argc)
                crtc_specified = atoi(argv[i]);
            else
			{
                fprintf(stderr, "ERROR! Required value for crtc not specified!\n");
                failed = 1;
				options |= OPT_HELP;
            }
        }
        else if
			(temp.temp == DELTA_MIN) temp.temp = atoi(argv[i]);
        else if
			(temp.brightness == DELTA_MIN) temp.brightness = atof(argv[i]);
        else
        {
            fprintf(stderr, "ERROR! Unknown parameter: %s\n!", argv[i]);
            failed = 1;
			options |= OPT_HELP;
        }
    }




    if (options & OPT_HELP)
        usage(argv[0]);
    else if (screen_specified >= screens)
    {
        fprintf(stderr, "ERROR! Invalid screen index: %d!\n", screen_specified);
        failed = 1;
    }
    else
    {
        // Check if the temp is above 100 less than the norm and change to NIGHT if it is
        // The threashold was chosen to give some room for varients in temp
        if (!(options & OPT_TOGGLE))
        {
            for (screen = screen_first; screen <= screen_last; screen++)
            {
                temp = get_sct_for_screen(dpy, screen, crtc_specified, options & OPT_DEBUG);
                if (temp.temp > (TEMPERATURE_NORM - 100))
                    temp.temp = TEMPERATURE_NIGHT;
                else
                    temp.temp = TEMPERATURE_NORM;
                sct_for_screen(dpy, screen, crtc_specified, temp, options & OPT_DEBUG);
            }
        }

        if ((temp.brightness == DELTA_MIN) && !(options & OPT_DELTA))
			temp.brightness = 1.0;

        if (screen_specified >= 0)
        {
            screen_first = screen_specified;
            screen_last = screen_specified;
        }
        if ((temp.temp == DELTA_MIN) && !(options & OPT_DELTA))
        {
            // No arguments, so print estimated temperature for each screen
            for (screen = screen_first; screen <= screen_last; screen++)
            {
                temp = get_sct_for_screen(dpy, screen, crtc_specified, options & OPT_DEBUG);
                printf("Screen %d: temperature ~ %d %f\n", screen, temp.temp, temp.brightness);
            }
        }
        else
        {
            if ((options & OPT_DELTA))
            {
                // Set temperature to given value or default for a value of 0
                if (temp.temp == 0)
                    temp.temp = TEMPERATURE_NORM;
                else
                    bound_temp(&temp);

                for (screen = screen_first; screen <= screen_last; screen++)
                   sct_for_screen(dpy, screen, crtc_specified, temp, options & OPT_DEBUG);
            }
            else
            {
                // Delta mode: Shift temperature and optionally brightness of each screen by given value
                if (temp.temp == DELTA_MIN || temp.brightness == DELTA_MIN)
                {
                    fprintf(stderr, "ERROR! Temperature and brightness delta must both be specified!\n");
                    failed = 1;
                }
                else
                {
                    for (screen = screen_first; screen <= screen_last; screen++)
                    {
                        screen_status tempd = get_sct_for_screen(dpy, screen, crtc_specified, options & OPT_DEBUG);

                        tempd.temp += temp.temp;
                        tempd.brightness += temp.brightness;

                        bound_temp(&tempd);

                        sct_for_screen(dpy, screen, crtc_specified, tempd, options & OPT_DEBUG);
                    }
                }
            }
        }
    }

    XCloseDisplay(dpy);

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

