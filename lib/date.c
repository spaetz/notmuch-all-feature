/*
 * Copyright © 2009 Keith Packard <keithp@keithp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include "notmuch.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DAY	(24 * 60 * 60)

static void
today(struct tm *result, time_t after) {
    time_t	t;

    if (after)
	t = after;
    else
	time(&t);
    localtime_r(&t, result);
    result->tm_sec = result->tm_min = result->tm_hour = 0;
}

static int parse_today(const char *text, time_t *first, time_t *last, time_t after) {
    (void)after; /*disable unused paramter warning*/
    if (strcasecmp(text, "today") == 0) {
	struct tm n;
	today(&n, 0);
	*first = mktime(&n);
	*last = *first + DAY;
	return 0;
    }
    return 1;
}

static int parse_yesterday(const char *text, time_t *first, time_t *last, time_t after) {
    if (strcasecmp(text, "yesterday") == 0) {
	struct tm n;
	today(&n, 0);
	*last = mktime(&n);
	*first = *last - DAY;
	return 0;
    }
    return 1;
    (void)after; /*disable unused paramter warning*/
}

static int parse_thisweek(const char *text, time_t *first, time_t *last, time_t after) {
    if (strcasecmp(text, "thisweek") == 0) {
	struct tm n;
	today(&n, 0);
	*first = mktime(&n) - (n.tm_wday * DAY);
	*last = *first + DAY * 7;
	return 0;
    }
    return 1;
    (void)after; /*disable unused paramter warning*/
}

static int parse_lastweek(const char *text, time_t *first, time_t *last, time_t after) {
    if (strcasecmp(text, "lastweek") == 0) {
	struct tm n;
	today(&n, 0);
	*last = mktime(&n) - (n.tm_wday * DAY);
	*first = *last - DAY * 7;
	return 0;
    }
    return 1;
    (void)after; /*disable unused paramter warning*/
}

static int parse_thismonth(const char *text, time_t *first, time_t *last, time_t after) {
    if (strcasecmp(text, "thismonth") == 0) {
	struct tm n;
	today(&n, 0);
	n.tm_mday = 1;
	*first = mktime(&n);
	if (n.tm_mon++ == 12) {
	    n.tm_mon = 0;
	    n.tm_year++;
	}
	*last = mktime(&n);
	return 0;
    }
    return 1;
    (void)after; /*disable unused paramter warning*/
}

static int parse_lastmonth(const char *text, time_t *first, time_t *last, time_t after) {
    if (strcasecmp(text, "lastmonth") == 0) {
	struct tm n;
	today(&n, 0);
	n.tm_mday = 1;
	if (n.tm_mon == 0) {
	    n.tm_year--;
	    n.tm_mon = 11;
	} else
	    n.tm_mon--;
	*first = mktime(&n);
	if (n.tm_mon++ == 12) {
	    n.tm_mon = 0;
	    n.tm_year++;
	}
	*last = mktime(&n);
	return 0;
    }
    return 1;
    (void)after; /*disable unused paramter warning*/
}

static const char *months[12][2] = {
    { "January", "Jan" },
    { "February", "Feb" },
    { "March", "Mar" },
    { "April", "Apr" },
    { "May", "May" },
    { "June", "Jun" },
    { "July", "Jul" },
    { "August", "Aug" },
    { "September", "Sep" },
    { "October", "Oct" },
    { "November", "Nov" },
    { "December", "Dec" },
};

static int year(const char *text, int *y) {
    char *end;
    *y = strtol(text, &end, 10);
    if (end == text)
	return 1;
    if (*end != '\0')
	return 1;
    if (*y < 1970 || *y > 2038)
	return 1;
    *y -= 1900;
    return 0;
}

static int month(const char *text, int *m) {
    char *end;
    int i;
    for (i = 0; i < 12; i++) {
	if (strcasecmp(text, months[i][0]) == 0 ||
	    strcasecmp(text, months[i][1]) == 0)
	{
	    *m = i;
	    return 0;
	}
    }
    *m = strtol(text, &end, 10);
    if (end == text)
	return 1;
    if (*end != '\0')
	return 1;
    if (*m < 1 || *m > 12)
	return 1;
    *m -= 1;
    return 0;
}

static int day(const char *text, int *d) {
    char *end;
    *d = strtol(text, &end, 10);
    if (end == text)
	return 1;
    if (*end != '\0')
	return 1;
    if (*d < 1 || *d > 31)
	return 1;
    return 0;
}

/* month[-day] */
static int parse_month(const char *text, time_t *first, time_t *last, time_t after) {
    int		m = 0, d = 0;
    int		i;
    struct tm	n;
    char	tmp[80];
    char	*t;
    char	*save;
    char	*token;

    if(strlen (text) >= sizeof (tmp))
	return 1;
    strcpy(tmp, text);

    t = tmp;
    save = NULL;
    i = 0;
    while ((token = strtok_r(t, "-", &save)) != NULL) {
	i++;
	switch(i) {
	case 1:
	    if (month(token, &m) != 0)
		return 1;
	    break;
	case 2:
	    if (day(token, &d) != 0)
		return 1;
	    break;
	default:
	    return 1;
	}
	t = NULL;
    }
    today(&n, after);
    if (after) {
	if (m < n.tm_mon)
	    n.tm_year++;
    } else {
	if (m > n.tm_mon)
	    n.tm_year--;
    }
    switch (i) {
    case 1:
	n.tm_mday = 1;
	n.tm_mon = m;
	*first = mktime(&n);
	if (++n.tm_mon > 11) {
	    n.tm_mon = 0;
	    n.tm_year++;
	}
	*last = mktime(&n);
	return 0;
    case 2:
	n.tm_mday = d;
	n.tm_mon = m;
	*first = mktime(&n);
	*last = *first + DAY;
	return 0;
    }
    return 1;
}

/* year[-month[-day]] */
static int parse_iso(const char *text, time_t *first, time_t *last, time_t after) {
    int		y = 0, m = 0, d = 0;
    int		i;
    struct tm	n;
    char	tmp[80];
    char	*t;
    char	*save;
    char	*token;

    if(strlen (text) >= sizeof (tmp))
	return 1;
    strcpy(tmp, text);

    t = tmp;
    save = NULL;
    i = 0;
    while ((token = strtok_r(t, "-", &save)) != NULL) {
	i++;
	switch(i) {
	case 1:
	    if (year(token, &y) != 0)
		return 1;
	    break;
	case 2:
	    if (month(token, &m) != 0)
		return 1;
	    break;
	case 3:
	    if (day(token, &d) != 0)
		return 1;
	    break;
	default:
	    return 1;
	}
	t = NULL;
    }
    today(&n, 0);
    switch (i) {
    case 1:
	n.tm_mday = 1;
	n.tm_mon = 0;
	n.tm_year = y;
	*first = mktime(&n);
	n.tm_year = y + 1;
	*last = mktime(&n);
	return 0;
    case 2:
	n.tm_mday = 1;
	n.tm_mon = m;
	n.tm_year = y;
	*first = mktime(&n);
	if (++n.tm_mon > 11) {
	    n.tm_mon = 0;
	    n.tm_year++;
	}
	*last = mktime(&n);
	return 0;
    case 3:
	n.tm_mday = d;
	n.tm_mon = m;
	n.tm_year = y;
	*first = mktime(&n);
	*last = *first + DAY;
	return 0;
    }
    return 1;
    (void)after; /*disable unused paramter warning*/
}

/* month[/day[/year]] */
static int parse_us(const char *text, time_t *first, time_t *last, time_t after) {
    int		y = 0, m = 0, d = 0;
    int		i;
    struct tm	n;
    char	tmp[80];
    char	*t;
    char	*save;
    char	*token;

    if(strlen (text) >= sizeof (tmp))
	return 1;
    strcpy(tmp, text);

    t = tmp;
    save = NULL;
    i = 0;
    while ((token = strtok_r(t, "/", &save)) != NULL) {
	i++;
	switch(i) {
	case 1:
	    if (month(token, &m) != 0)
		return 1;
	    break;
	case 2:
	    if (day(token, &d) != 0)
		return 1;
	    break;
	case 3:
	    if (year(token, &y) != 0)
		return 1;
	    break;
	default:
	    return 1;
	}
	t = NULL;
    }
    today(&n, after);
    if (after) {
	if (m < n.tm_mon)
	    n.tm_year++;
    } else {
	if (m > n.tm_mon)
	    n.tm_year--;
    }
    switch (i) {
    case 1:
	n.tm_mday = 1;
	n.tm_mon = m;
	*first = mktime(&n);
	if (++n.tm_mon > 11) {
	    n.tm_mon = 0;
	    n.tm_year++;
	}
	*last = mktime(&n);
	return 0;
    case 2:
	n.tm_mday = d;
	n.tm_mon = m;
	*first = mktime(&n);
	*last = *first + DAY;
	return 0;
    case 3:
	n.tm_mday = d;
	n.tm_mon = m;
	n.tm_year = y;
	*first = mktime(&n);
	*last = *first + DAY;
	return 0;
    }
    return 1;
}

static int (*parsers[])(const char *text, time_t *first, time_t *last, time_t after) = {
    parse_today,
    parse_yesterday,
    parse_thisweek,
    parse_lastweek,
    parse_thismonth,
    parse_lastmonth,
    parse_month,
    parse_iso,
    parse_us,
    0,
};

notmuch_status_t
notmuch_parse_date(const char *text, time_t *first, time_t *last, time_t after)
{
    int		i;
    for (i = 0; parsers[i]; i++)
	if (parsers[i](text, first, last, after) == 0)
	    return NOTMUCH_STATUS_SUCCESS;
    return NOTMUCH_STATUS_INVALID_DATE;
}
