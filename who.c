/*-
 * Copyright (c) 2002 Tim J. Robbins.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/usr.bin/who/who.c,v 1.22.14.1 2010/12/21 17:10:29 kensmith Exp $");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <timeconv.h>
#include <unistd.h>
#include <utmp.h>

#include <GeoIP.h>

static void	heading(void);
static void	process_utmp(FILE *);
static void	quick(FILE *);
static void	row(struct utmp *);
static int	ttywidth(void);
static void	usage(void);
static void	whoami(FILE *);

static int	Hflag;			/* Write column headings */
static int	mflag;			/* Show info about current terminal */
static int	qflag;			/* "Quick" mode */
static int	sflag;			/* Show name, line, time */
static int	Tflag;			/* Show terminal state */
static int	uflag;			/* Show idle time */

#define W_DISPGEOSIZE 20
const char* geoiplookup(const char*);
int should_show_user(const char*);

int
main(int argc, char *argv[])
{
	int ch;
	const char *file;
	FILE *fp;

	setlocale(LC_TIME, "");

	while ((ch = getopt(argc, argv, "HTmqsu")) != -1) {
		switch (ch) {
		case 'H':		/* Write column headings */
			Hflag = 1;
			break;
		case 'T':		/* Show terminal state */
			Tflag = 1;
			break;
		case 'm':		/* Show info about current terminal */
			mflag = 1;
			break;
		case 'q':		/* "Quick" mode */
			qflag = 1;
			break;
		case 's':		/* Show name, line, time */
			sflag = 1;
			break;
		case 'u':		/* Show idle time */
			uflag = 1;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;

	if (argc >= 2 && strcmp(argv[0], "am") == 0 &&
	    (strcmp(argv[1], "i") == 0 || strcmp(argv[1], "I") == 0)) {
		/* "who am i" or "who am I", equivalent to -m */
		mflag = 1;
		argc -= 2;
		argv += 2;
	}
	if (argc > 1)
		usage();

	if (*argv != NULL)
		file = *argv;
	else
		file = _PATH_UTMP;
	if ((fp = fopen(file, "r")) == NULL)
		err(1, "%s", file);

	if (qflag)
		quick(fp);
	else {
		if (sflag)
			Tflag = uflag = 0;
		if (Hflag)
			heading();
		if (mflag)
			whoami(fp);
		else
			process_utmp(fp);
	}

	fclose(fp);

	exit(0);
}

static void
usage(void)
{

	fprintf(stderr, "usage: who [-HmqsTu] [am I] [file]\n");
	exit(1);
}

static void
heading(void)
{

	printf("%-*s ", UT_NAMESIZE, "NAME");
	if (Tflag)
		printf("S ");
	printf("%-*s ", UT_LINESIZE, "LINE");
	printf("%-*s ", 12, "TIME");
	if (uflag)
		printf("IDLE  ");
	printf("%-*s", W_DISPGEOSIZE, "GEO");
	printf("%-*s", UT_HOSTSIZE, "FROM");
	putchar('\n');
}

static void
row(struct utmp *ut)
{
	char buf[80], tty[sizeof(_PATH_DEV) + UT_LINESIZE];
	struct stat sb;
	time_t idle, t;
	static int d_first = -1;
	struct tm *tm;
	char state;

	if (d_first < 0)
		d_first = (*nl_langinfo(D_MD_ORDER) == 'd');

	state = '?';
	idle = 0;
	if (Tflag || uflag) {
		snprintf(tty, sizeof(tty), "%s%.*s", _PATH_DEV,
			UT_LINESIZE, ut->ut_line);
		if (stat(tty, &sb) == 0) {
			state = sb.st_mode & (S_IWOTH|S_IWGRP) ?
			    '+' : '-';
			idle = time(NULL) - sb.st_mtime;
		}
	}

	printf("%-*.*s ", UT_NAMESIZE, UT_NAMESIZE, ut->ut_name);
	if (Tflag)
		printf("%c ", state);
	printf("%-*.*s ", UT_LINESIZE, UT_LINESIZE, ut->ut_line);
	t = _time32_to_time(ut->ut_time);
	tm = localtime(&t);
	strftime(buf, sizeof(buf), d_first ? "%e %b %R" : "%b %e %R", tm);
	printf("%-*s ", 12, buf);
	if (uflag) {
		if (idle < 60)
			printf("  .   ");
		else if (idle < 24 * 60 * 60)
			printf("%02d:%02d ", (int)(idle / 60 / 60),
			    (int)(idle / 60 % 60));
		else
			printf(" old  ");
	}
	if (*ut->ut_host != '\0') {
		char host_buf[UT_HOSTSIZE + 1];
		const char *country_name = NULL;
		host_buf[UT_HOSTSIZE] = '\0';
		strncpy(host_buf, ut->ut_host, UT_HOSTSIZE);
		country_name = geoiplookup(host_buf);

		printf("%-*.*s", W_DISPGEOSIZE, W_DISPGEOSIZE, country_name ? country_name : "-");
		printf("(%.*s)", UT_HOSTSIZE, ut->ut_host);
	}
	putchar('\n');
}

static int
ttystat(char *line, int sz)
{
	struct stat sb;
	char ttybuf[MAXPATHLEN];

	(void)snprintf(ttybuf, sizeof(ttybuf), "%s%.*s", _PATH_DEV, sz, line);
	if (stat(ttybuf, &sb) == 0) {
		return (0);
	} else
		return (-1);
}

static void
process_utmp(FILE *fp)
{
	struct utmp ut;

	while (fread(&ut, sizeof(ut), 1, fp) == 1) {
		if (*ut.ut_name == '\0')
			continue;
		if (!should_show_user(ut.ut_name))
			continue;
		if (ttystat(ut.ut_line, UT_LINESIZE) != 0)
			continue;
		row(&ut);
	}
}

static void
quick(FILE *fp)
{
	struct utmp ut;
	int col, ncols, num;

	ncols = ttywidth();
	col = num = 0;
	while (fread(&ut, sizeof(ut), 1, fp) == 1) { 
		if (*ut.ut_name == '\0')
			continue;
		if (!should_show_user(ut.ut_name))
			continue;
		printf("%-*.*s", UT_NAMESIZE, UT_NAMESIZE, ut.ut_name);
		if (++col < ncols / (UT_NAMESIZE + 1))
			putchar(' ');
		else {
			col = 0;
			putchar('\n');
		}
		num++;
	}
	if (col != 0)
		putchar('\n');

	printf("# users = %d\n", num);
}

static void
whoami(FILE *fp)
{
	struct utmp ut;
	struct passwd *pwd;
	const char *name, *p, *tty;

	if ((tty = ttyname(STDIN_FILENO)) == NULL)
		tty = "tty??";
	else if ((p = strrchr(tty, '/')) != NULL)
		tty = p + 1;

	/* Search utmp for our tty, dump first matching record. */
	while (fread(&ut, sizeof(ut), 1, fp) == 1) {
		if (!should_show_user(ut.ut_name))
			continue;
		if (*ut.ut_name != '\0' && strncmp(ut.ut_line, tty,
		    UT_LINESIZE) == 0) {
			row(&ut);
			return;
		}
	}

	/* Not found; fill the utmp structure with the information we have. */
	memset(&ut, 0, sizeof(ut));
	if ((pwd = getpwuid(getuid())) != NULL)
		name = pwd->pw_name;
	else
		name = "?";
	strncpy(ut.ut_name, name, UT_NAMESIZE);
	strncpy(ut.ut_line, tty, UT_LINESIZE);
	ut.ut_time = _time_to_time32(time(NULL));
	row(&ut);
}

int should_show_user(const char *name) {
	static struct passwd *pw = NULL;
	if (name == NULL)
		return 0;

	if (geteuid() == 0)
		return 1;

	if (pw == NULL)
		pw = getpwuid(getuid());

	if (!strncmp(name, pw->pw_name, UT_NAMESIZE))
		return 1;
	return 0;
}

const char* geoiplookup(const char *name) {
	const char *country_name = NULL;

	int gip_type = strchr(name, ':') ? GEOIP_COUNTRY_EDITION_V6 : GEOIP_COUNTRY_EDITION;
	if (GeoIP_db_avail(gip_type)) {
		GeoIP *gip = GeoIP_open_type(gip_type, GEOIP_STANDARD);

		if (gip) {
			int country_id = gip_type == GEOIP_COUNTRY_EDITION ? GeoIP_id_by_name(gip, name) : GeoIP_id_by_name_v6(gip, name);
			if (country_id > 0)
				country_name = GeoIP_country_name[country_id];
			GeoIP_delete(gip);
		}
	}

	return country_name;
}

static int
ttywidth(void)
{
	struct winsize ws;
	long width;
	char *cols, *ep;

	if ((cols = getenv("COLUMNS")) != NULL && *cols != '\0') {
		errno = 0;
		width = strtol(cols, &ep, 10);
		if (errno || width <= 0 || width > INT_MAX || ep == cols ||
		    *ep != '\0')
			warnx("invalid COLUMNS environment variable ignored");
		else
			return (width);
	}
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1)
		return (ws.ws_col);

	return (80);
}
